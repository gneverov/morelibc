// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include "morelib/poll.h"

#include "FreeRTOS.h"
#include "semphr.h"


struct socket_vtable;

struct socket {
    struct poll_file base;
    const struct socket_vtable *func;
    SemaphoreHandle_t mutex;
    int domain : 8;
    int type : 8;
    int protocol : 8;
    int listening : 1;
    int connecting : 1;
    StaticSemaphore_t xMutexBuffer;
};

struct socket_vtable {
    struct socket *(*accept)(void *ctx, struct sockaddr *address, socklen_t *address_len);
    int (*bind)(void *ctx, const struct sockaddr *address, socklen_t address_len);
    int (*close)(void *ctx);
    int (*connect)(void *ctx, const struct sockaddr *address, socklen_t address_len);
    int (*getpeername)(void *ctx, struct sockaddr *address, socklen_t *address_len);
    int (*getsockname)(void *ctx, struct sockaddr *address, socklen_t *address_len);
    int (*getsockopt)(void *ctx, int level, int option_name, void *option_value, socklen_t *option_len);
    int (*listen)(void *ctx, int backlog);
    int (*recvfrom)(void *ctx, void *buf, size_t len, struct sockaddr *address, socklen_t *address_len);
    int (*setsockopt)(void *ctx, int level, int option_name, const void *option_value, socklen_t option_len);
    int (*sendto)(void *ctx, const void *buf, size_t len, const struct sockaddr *address, socklen_t address_len);
    int (*shutdown)(void *ctx, int how);
};


struct socket *socket_acquire(int fd);
void *socket_alloc(size_t size, const struct socket_vtable *vtable, int domain, int type, int protocol);

static inline void socket_lock(struct socket *socket) {
    xSemaphoreTake(socket->mutex, portMAX_DELAY);
}

static inline void socket_unlock(struct socket *socket) {
    xSemaphoreGive(socket->mutex);
}

static inline int socket_fd(struct socket *socket) {
    return poll_file_fd(&socket->base);
}

static inline void *socket_copy(struct socket *socket) {
    return poll_file_copy(&socket->base);
}

static inline void socket_release(struct socket *socket) {
    poll_file_release(&socket->base);
}

static inline void socket_notify(struct socket *socket, uint clear, uint set) {
    poll_file_notify(&socket->base, clear, set);
}

#define POLL_SOCKET_CHECK(ret, socket, events, pxTicksToWait) \
    POLL_CHECK(ret, &(socket)->base, events, pxTicksToWait)

static inline struct socket *socket_accept(struct socket *socket, struct sockaddr *address, socklen_t *address_len) {
    if (!socket->func->accept) {
        errno = EOPNOTSUPP;
        return NULL;
    }
    return socket->func->accept(socket, address, address_len);
}

static inline int socket_bind(struct socket *socket, const struct sockaddr *address, socklen_t address_len) {
    if (!socket->func->bind) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return socket->func->bind(socket, address, address_len);
}

static inline int socket_connect(struct socket *socket, const struct sockaddr *address, socklen_t address_len) {
    if (!socket->func->connect) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return socket->func->connect(socket, address, address_len);
}

static inline int socket_getpeername(struct socket *socket, struct sockaddr *address, socklen_t *address_len) {
    if (!socket->func->getpeername) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return socket->func->getpeername(socket, address, address_len);
}

static inline int socket_getsockname(struct socket *socket, struct sockaddr *address, socklen_t *address_len) {
    if (!socket->func->getsockname) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return socket->func->getsockname(socket, address, address_len);
}

void socket_getintopt(void *option_value, socklen_t *option_len, int value);
int socket_getsockopt(struct socket *socket, int level, int option_name, void *option_value, socklen_t *option_len);

static inline int socket_listen(struct socket *socket, int backlog) {
    if (!socket->func->listen) {
        errno = EOPNOTSUPP;
        return -1;
    }
    int ret = socket->func->listen(socket, backlog);
    if (ret >= 0) {
        socket->listening = 1;
    }
    return ret;
}

static inline int socket_recvfrom(struct socket *socket, void *buf, size_t len, int flags, struct sockaddr *address, socklen_t *address_len) {
    if (!socket->func->recvfrom) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return socket->func->recvfrom(socket, buf, len, address, address_len);
}

static inline int socket_recv(struct socket *socket, void *buf, size_t len, int flags) {
    return socket_recvfrom(socket, buf, len, flags, NULL, NULL);
}

static inline int socket_sendto(struct socket *socket, const void *buf, size_t len, int flags, const struct sockaddr *address, socklen_t address_len) {
    if (!socket->func->sendto) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return socket->func->sendto(socket, buf, len, address, address_len);
}

static inline int socket_send(struct socket *socket, const void *buf, size_t len, int flags) {
    return socket_sendto(socket, buf, len, flags, NULL, 0);
}

int socket_setintopt(const void *option_value, socklen_t option_len, int *value);
int socket_setsockopt(struct socket *socket, int level, int option_name, const void *option_value, socklen_t option_len);

static inline int socket_shutdown(struct socket *socket, int how) {
    if (!socket->func->shutdown) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return socket->func->shutdown(socket, how);
}


struct socket_family {
    sa_family_t family;
    struct socket *(*socket)(int domain, int type, int protocol);
    int (*getaddrinfo)(const char *nodename, const char *servname, const struct addrinfo *hints, struct addrinfo **res);
    int (*getnameinfo)(const struct sockaddr *sa, socklen_t salen, char *node, socklen_t nodelen, char *service, socklen_t servicelen, int flags);
};

extern const struct socket_family *socket_families[];
extern const size_t socket_num_families;

static inline struct socket *socket_socket(const struct socket_family *family, int domain, int type, int protocol) {
    if (!family->socket) {
        errno = EOPNOTSUPP;
        return NULL;
    }
    return family->socket(domain, type, protocol);
}
