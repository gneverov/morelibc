// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include "morelib/poll.h"

#include "FreeRTOS.h"
#include "task.h"

#include "mbedtls/asn1write.h"
#include "mbedtls/debug.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"
#include "mbedtls/sha1.h"

#include "morelib/lwip/tls.h"

#define POLL_SOCKET_TLS_CHECK(ret, file, events, pxTicksToWait) \
    (!((file)->base.base.base.flags & FNONBLOCK) && (ret < 0) && (events) && ((ret = poll_file_wait(&(file)->base.base, events, pxTicksToWait)) >= 0))


enum {
    SOCKET_TLS_STATE_INITIAL,
    SOCKET_TLS_STATE_CONNECTING,
    SOCKET_TLS_STATE_HANDSHAKING,
    SOCKET_TLS_STATE_DONE,
};

static int socket_tls_send_cb(void *ctx, const unsigned char *buf, size_t len) {
    struct socket_tls *socket = ctx;
    int ret = socket_send(socket->inner, buf, len, 0);
    if (ret >= 0) {
        return ret;
    }
    if (errno == EAGAIN) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return -errno;
}

static int socket_tls_recv_cb(void *ctx, unsigned char *buf, size_t len) {
    struct socket_tls *socket = ctx;
    int ret = socket_recv(socket->inner, buf, len, 0);
    if (ret >= 0) {
        return ret;
    }
    if (errno == EAGAIN) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return -errno;
}

static void socket_tls_notify_recv(const void *ptr, BaseType_t *pxHigherPriorityTaskWoken) {
    struct socket_tls *socket = (void *)(ptr - offsetof(struct socket_tls, desc));
    if (pxHigherPriorityTaskWoken) {
        poll_file_notify_from_isr(&socket->base.base, 0, socket->inner->base.events, pxHigherPriorityTaskWoken);
    }
    else {
        poll_file_notify(&socket->base.base, 0, socket->inner->base.events);
    }
}

static void socket_tls_notify_send(struct socket_tls *socket, uint clear, uint set) {
    taskENTER_CRITICAL();
    clear &= ~poll_file_poll(&socket->inner->base);
    socket_notify(&socket->base, clear, set);
    taskEXIT_CRITICAL();
}

static uint socket_tls_check_io(struct socket_tls *socket, int ret) {
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        socket_notify(&socket->base, 0, POLLHUP);
        return 0;
    }
    else if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
        socket_tls_notify_send(socket, POLLIN | POLLRDNORM, 0);
        return POLLIN;
    }
    else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        socket_tls_notify_send(socket, POLLOUT| POLLWRNORM, 0);
        return POLLOUT;
    }    
    return 0;
}

static const struct socket_vtable socket_tls_vtable;

static struct socket_tls *socket_tls_alloc(struct socket *inner, struct socket_tls_context *context, int flags) {
    if (inner->type != SOCK_STREAM) {
        errno = EOPNOTSUPP;
        return NULL;
    }

    struct socket_tls *socket = socket_alloc(sizeof(struct socket_tls), &socket_tls_vtable, inner->domain, inner->type, inner->protocol);
    if (!socket) {
        return NULL;
    }
    socket->timeout = portMAX_DELAY;
    socket->state = SOCKET_TLS_STATE_INITIAL;
    socket->flags = flags;

    poll_waiter_init(&socket->desc, -1, socket_tls_notify_recv);
    mbedtls_ssl_init(&socket->ssl);
    mbedtls_ssl_set_bio(&socket->ssl, socket, socket_tls_send_cb, socket_tls_recv_cb, NULL);

    // Setup the SSL context with the configuration
    socket->context = socket_tls_context_copy(context);
    if (socket_tls_check_ret(mbedtls_ssl_setup(&socket->ssl, &context->conf)) < 0) {
        goto exit;
    }

    socket->inner = inner;
    poll_waiter_add(&inner->base, &socket->desc);
    inner->base.base.flags |= O_NONBLOCK;
    return socket;

exit:
    socket_release(&socket->base);
    return NULL;
}

int socket_tls_wrap(int fd, struct socket_tls_context *context, int flags) {
    struct socket *inner = socket_acquire(fd);
    if (!inner) {
        return -1;
    }

    struct socket_tls *socket = socket_tls_alloc(inner, context, flags);
    if (!socket) {
        socket_release(inner);
        return -1;
    }

    int ret = socket_fd(&socket->base);
    socket_release(&socket->base);
    return ret;
}

struct socket_tls *socket_tls_acquire(int fd) {
    struct socket_tls *socket = (void *)socket_acquire(fd);
    if (!socket) {
        return NULL;
    }
    if (socket->base.func != &socket_tls_vtable) {
        errno = ENOTSOCK;
        socket_release(&socket->base);
        return NULL;
    }
    return socket;
}

void socket_tls_release(struct socket_tls *socket) {
    socket_release(&socket->base);
}

static struct socket *socket_tls_accept(void *ctx, struct sockaddr *address, socklen_t *address_len) {
    struct socket_tls *socket = ctx;

    int ret;
    uint events;
    struct socket *new_inner = NULL;
    TickType_t xTicksToWait = socket->timeout;
    do {
        new_inner = socket_accept(socket->inner, address, address_len);
        ret = 0;
        events = 0;
        if (!new_inner) {
            ret = -1;
            socket_tls_notify_send(socket, POLLIN | POLLOUT, 0);
            if (errno == EAGAIN) {
                events = POLLIN | POLLOUT;
            }
        }
    }
    while (POLL_SOCKET_TLS_CHECK(ret, socket, events, &xTicksToWait));
    if (ret < 0) {
        goto exit;
    }
    
    struct socket_tls *new_socket = socket_tls_alloc(new_inner, socket->context, socket->flags);
    if (!new_socket) {
        return NULL;
    }
    new_inner = NULL;

    if (new_socket->flags & SOCKET_TLS_FLAG_DO_HANDSHAKE_ON_CONNECT) {
        socket_tls_handshake(new_socket);
    }
    return &new_socket->base;

exit:
    if (new_inner) {
        socket_release(new_inner);
    }
    return NULL;
}

static int socket_tls_bind(void *ctx, const struct sockaddr *address, socklen_t address_len) {
    struct socket_tls *socket = ctx;
    socket_lock(&socket->base);
    int ret = socket_bind(socket->inner, address, address_len);
    socket_unlock(&socket->base);
    return ret;
}

static int socket_tls_close(void *ctx) {
    struct socket_tls *socket = ctx;
    if (!mbedtls_ssl_check_pending(&socket->ssl)) {
        mbedtls_ssl_close_notify(&socket->ssl);
    }
    mbedtls_ssl_free(&socket->ssl);
    if (socket->inner) {
        poll_waiter_remove(&socket->inner->base, &socket->desc);
        socket_release(socket->inner);
    }
    if (socket->context) {
        socket_tls_context_free(socket->context);
    }
    return 0;
}

static int socket_tls_connect(void *ctx, const struct sockaddr *address, socklen_t address_len) {
    struct socket_tls *socket = ctx;
    
    if (socket->state == SOCKET_TLS_STATE_INITIAL) {
        if ((socket_connect(socket->inner, address, address_len) < 0) && (errno != EINPROGRESS) && (errno != EAGAIN)) {
            return -1;
        }
        socket->state = SOCKET_TLS_STATE_CONNECTING;
    }

    if (socket->state == SOCKET_TLS_STATE_CONNECTING) {
        int ret;
        uint events;
        TickType_t xTicksToWait = socket->timeout;
        do {
            events = 0;
            socket_tls_notify_send(socket, POLLIN | POLLOUT, 0);

            struct sockaddr_storage address;
            socklen_t address_len = sizeof(address);
            ret = socket_getpeername(socket->inner, (struct sockaddr *)&address, &address_len);
            if ((ret < 0) && (errno == ENOTCONN)) {
                events = POLLIN | POLLOUT;
            }
        }
        while (POLL_SOCKET_TLS_CHECK(ret, socket, events, &xTicksToWait));
        if (ret < 0) {
            return ret;
        }

        socket->state = SOCKET_TLS_STATE_HANDSHAKING;
    }

    if (socket->state == SOCKET_TLS_STATE_HANDSHAKING) {
        int ret = 0;
        if (socket->flags & SOCKET_TLS_FLAG_DO_HANDSHAKE_ON_CONNECT) {
            ret = socket_tls_handshake(socket);
        }
        if (ret >= 0) {
            socket->state = SOCKET_TLS_STATE_INITIAL;
        }
        return ret;
    }

    assert(false);
    return 0;
}

static int socket_tls_listen(void *ctx, int backlog) {
    struct socket_tls *socket = ctx;
    socket_lock(&socket->base);
    int ret = socket_listen(socket->inner, backlog);
    socket_unlock(&socket->base);
    return ret;
}

int socket_tls_handshake(struct socket_tls *socket) {
    int ret = -1;
    uint events = 0;
    TickType_t xTicksToWait = socket->timeout;
    do {
        socket_lock(&socket->base);
        ret = mbedtls_ssl_handshake(&socket->ssl);
        events = socket_tls_check_io(socket, ret);
        ret = socket_tls_check_ret(ret);
        socket_unlock(&socket->base);
    }
    while (POLL_SOCKET_TLS_CHECK(ret, socket, events, &xTicksToWait));
    return ret;
}

static int socket_tls_recvfrom(void *ctx, void *buf, size_t len, struct sockaddr *address, socklen_t *address_len) {
    struct socket_tls *socket = ctx;
    if (address != NULL) {
        errno = EINVAL;
        return -1;
    }

    int ret = -1;
    uint events = 0;
    TickType_t xTicksToWait = socket->timeout;
    do {
        ret = 0;
        if (len) {
            socket_lock(&socket->base);
            ret = mbedtls_ssl_read(&socket->ssl, buf, len);
            events = socket_tls_check_io(socket, ret);
            if (ret == 0) {
                if (!(socket->flags & SOCKET_TLS_FLAG_SUPPRESS_RAGGED_EOFS)) {
                    ret = MBEDTLS_ERR_SSL_CONN_EOF;
                }
            }
            else if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                ret = 0;
            }
            socket_unlock(&socket->base);
        }
        ret = socket_tls_check_ret(ret);
    }
    while (POLL_SOCKET_TLS_CHECK(ret, socket, events, &xTicksToWait));
    return ret;
}

static int socket_tls_sendto(void *ctx, const void *buf, size_t len, const struct sockaddr *address, socklen_t address_len) {
    struct socket_tls *socket = ctx;
    if (address != NULL) {
        errno = EINVAL;
        return -1;
    }

    int ret = -1;
    uint events = 0;
    TickType_t xTicksToWait = socket->timeout;
    do {    
        socket_lock(&socket->base);
        ret = mbedtls_ssl_write(&socket->ssl, buf, len);
        events = socket_tls_check_io(socket, ret);
        ret = socket_tls_check_ret(ret);
        socket_unlock(&socket->base);
    }
    while (POLL_SOCKET_TLS_CHECK(ret, socket, events, &xTicksToWait));
    return ret;
}

int socket_tls_close_notify(struct socket_tls *socket) {
    int ret = -1;
    uint events = 0;
    TickType_t xTicksToWait = socket->timeout;
    do {
        socket_lock(&socket->base);
        ret = mbedtls_ssl_close_notify(&socket->ssl);
        events = socket_tls_check_io(socket, ret);
        ret = socket_tls_check_ret(ret);
        socket_unlock(&socket->base);
    }
    while (POLL_SOCKET_TLS_CHECK(ret, socket, events, &xTicksToWait));
    return ret;
}

static int socket_tls_shutdown(void *ctx, int how) {
    struct socket_tls *socket = ctx;
    if ((how == SHUT_WR) || (how == SHUT_RDWR)) {
        if (socket_tls_close_notify(socket) < 0) {
            return -1;
        }
        if (socket_shutdown(socket->inner, SHUT_WR) < 0) {
            return -1;
        }
    }
    return 0;
}

static int socket_tls_getpeername(void *ctx, struct sockaddr *address, socklen_t *address_len) {
    struct socket_tls *socket = ctx;
    return socket_getpeername(socket->inner, address, address_len);
}

static int socket_tls_getsockname(void *ctx, struct sockaddr *address, socklen_t *address_len) {
    struct socket_tls *socket = ctx;
    return socket_getsockname(socket->inner, address, address_len);
}

static int socket_tls_getsockopt(void *ctx, int level, int option_name, void *option_value, socklen_t *option_len) {
    struct socket_tls *socket = ctx;
    if (level == SOL_SOCKET) {
        switch (option_name) {
            case SO_RCVTIMEO:
            case SO_SNDTIMEO: {
                socket_lock(&socket->base);
                socket_gettvopt(option_value, option_len, socket->timeout);
                socket_unlock(&socket->base);
                return 0;
            }
            default: {
                break;
            }
        }
    }
    return socket_getsockopt(socket->inner, level, option_name, option_value, option_len);
}

static int socket_tls_setsockopt(void *ctx, int level, int option_name, const void *option_value, socklen_t option_len) {
    struct socket_tls *socket = ctx;
    if (level == SOL_SOCKET) {
        switch (option_name) {
            case SO_RCVTIMEO:
            case SO_SNDTIMEO: {
                socket_lock(&socket->base);
                int ret = socket_settvopt(option_value, option_len, &socket->timeout);
                socket_unlock(&socket->base);
                return ret;
            }        
            default: {
                break;
            }
        }
    }
    return socket_setsockopt(socket->inner, level, option_name, option_value, option_len);
}

static const struct socket_vtable socket_tls_vtable = {
    .accept = socket_tls_accept,
    .bind = socket_tls_bind,
    .close = socket_tls_close,
    .connect = socket_tls_connect,
    .getpeername = socket_tls_getpeername,
    .getsockname = socket_tls_getsockname,
    .getsockopt = socket_tls_getsockopt,
    .listen = socket_tls_listen,
    .recvfrom = socket_tls_recvfrom,
    .sendto = socket_tls_sendto,
    .setsockopt = socket_tls_setsockopt,    
    .shutdown = socket_tls_shutdown, 
};