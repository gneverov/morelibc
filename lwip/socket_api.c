// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include "morelib/poll.h"

#include "./socket.h"


static struct socket *socket_acquire(int fd, int *flags) {
    *flags = FREAD | FWRITE;
    struct vfs_file *file = vfs_acquire_file(fd, flags);
    if (!file) {
        return NULL;
    }
    if (file->func == &socket_vtable) {
        return (void *)file;
    }
    vfs_release_file(file);
    errno = ENOTSOCK;
    return NULL;
}

static inline void socket_release(struct socket *socket) {
    poll_file_release(&socket->base);
}

// ### socket API
int accept(int fd, struct sockaddr *address, socklen_t *address_len) {
    int flags;
    struct socket *socket = socket_acquire(fd, &flags);
    if (!socket) {
        return -1;
    }

    int ret = -1;
    if (!socket->func->socket_accept) {
        errno = EOPNOTSUPP;
        goto exit;
    }

    TickType_t xTicksToWait = portMAX_DELAY;
    struct socket *new_socket;
    do {
        ret = socket->func->socket_accept(socket, &new_socket);
    }
    while (POLL_CHECK(flags, ret, &socket->base, POLLIN, &xTicksToWait));

    if (ret >= 0) {
        if (address) {
            *address_len = MIN(*address_len, sizeof(struct sockaddr_storage));
            memcpy(address, &new_socket->remote, *address_len);
        }

        ret = poll_file_fd(&new_socket->base, flags);
        socket_release(new_socket);
    }

exit:
    socket_release(socket);
    return ret;
}

int bind(int fd, const struct sockaddr *address, socklen_t address_len) {
    int flags;
    struct socket *socket = socket_acquire(fd, &flags);
    if (!socket) {
        return -1;
    }

    int ret = -1;
    ip_addr_t ipaddr;
    u16_t port;
    socket_sockaddr_to_lwip(address, address_len, &ipaddr, &port);
    
    if (!socket->func->lwip_bind) {
        errno = EOPNOTSUPP;
        goto exit;
    }

    LOCK_TCPIP_CORE();
    ret = socket_check_ret(socket->func->lwip_bind(socket, &ipaddr, port));
    UNLOCK_TCPIP_CORE();

exit:
    socket_release(socket);
    return ret;
}

int connect(int fd, const struct sockaddr *address, socklen_t address_len) {
    int flags;
    struct socket *socket = socket_acquire(fd, &flags);
    if (!socket) {
        return -1;
    }

    int ret = -1;
    ip_addr_t ipaddr;
    u16_t port;
    socket_sockaddr_to_lwip(address, address_len, &ipaddr, &port);

    if (!socket->func->lwip_connect) {
        errno = EOPNOTSUPP;
        goto exit;
    }

    LOCK_TCPIP_CORE();
    ret = socket_check_ret(socket->func->lwip_connect(socket, &ipaddr, port));
    UNLOCK_TCPIP_CORE();
    if (ret < 0) {
        goto exit;
    }

    socket->connecting = 1;

    TickType_t xTicksToWait = socket->timeout;
    do {
        socket_lock(socket);
        if (socket->errcode) {
            errno = socket->errcode;
            ret = -1;
        }
        else if (!socket->connected) {
            errno = socket->connecting ? EAGAIN : ENOTCONN;
            ret = -1;
        }
        else {
            ret = 0;
        }
        socket_unlock(socket);
    }
    while(POLL_CHECK(flags, ret, &socket->base, POLLIN | POLLOUT, &xTicksToWait));

exit:
    socket_release(socket);
    return ret;
}

int getpeername(int fd, struct sockaddr *address, socklen_t *address_len) {
    int flags;
    struct socket *socket = socket_acquire(fd, &flags);
    if (!socket) {
        return -1;
    }

    int ret = -1;
    socket_lock(socket);
    if (socket->errcode) {
        errno = socket->errcode;
    }
    else if (!socket->connected) {
        errno = ENOTCONN;
    }
    else {
        *address_len = MIN(*address_len, socket->remote_len);
        memcpy(address, &socket->remote, *address_len);
        ret = 0;
    }
    socket_unlock(socket);
    return ret;
}

int getsockname(int fd, struct sockaddr *address, socklen_t *address_len) {
    int flags;
    struct socket *socket = socket_acquire(fd, &flags);
    if (!socket) {
        return -1;
    }

    int ret = -1;
    socket_lock(socket);
    if (socket->errcode) {
        errno = socket->errcode;
    }
    else {
        *address_len = MIN(*address_len, socket->local_len);
        memcpy(address, &socket->local, *address_len);
        ret = 0;
    }
    socket_unlock(socket);
    return ret;
}

int getsockopt(int fd, int level, int option_name, void *option_value, socklen_t *option_len) {
    int flags;
    struct socket *socket = socket_acquire(fd, &flags);
    if (!socket) {
        return -1;
    }
    int ret = socket_getsockopt(socket, level, option_name, option_value, option_len);
    socket_release(socket);
    return ret; 
}

int listen(int fd, int backlog) {
    int flags;
    struct socket *socket = socket_acquire(fd, &flags);
    if (!socket) {
        return -1;
    }
    
    int ret = -1;
    if (!socket->func->lwip_listen) {
        errno = EOPNOTSUPP;
        goto exit;
    }

    LOCK_TCPIP_CORE();
    ret = socket_check_ret(socket->func->lwip_listen(socket, backlog));
    UNLOCK_TCPIP_CORE();
    
    if (ret >= 0) {
        socket->listening = 1;
    }

exit:
    socket_release(socket);
    return ret;
}

static ssize_t recvfrom_internal(struct socket *socket, int fd_flags, void *buffer, size_t length, int recv_flags, struct sockaddr *address, socklen_t *address_len) {
    if (!socket->func->socket_recvfrom) {
        errno = EOPNOTSUPP;
        return -1;
    }

    TickType_t xTicksToWait = socket->timeout;
    ip_addr_t ipaddr;
    u16_t port;
    int ret;
    do {
        ret = socket->func->socket_recvfrom(socket, buffer, length, address ? &ipaddr : NULL, address ? &port : NULL);
    }
    while (POLL_CHECK(fd_flags, ret, &socket->base, POLLIN, &xTicksToWait));

    if ((ret >= 0) && address) {
        socket_sockaddr_from_lwip(address, address_len, &ipaddr, port);
    }
    
    return ret;
}

ssize_t recvfrom(int fd, void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len) {
    int fd_flags;
    struct socket *socket = socket_acquire(fd, &fd_flags);
    if (!socket) {
        return -1;
    }
    int ret = recvfrom_internal(socket, fd_flags, buffer, length, flags, address, address_len);
    socket_release(socket);
    return ret;   
}

ssize_t recv(int fd, void *buffer, size_t length, int flags) {
    return recvfrom(fd, buffer, length, flags, NULL, NULL);
}

static ssize_t sendto_internal(struct socket *socket, int fd_flags, const void *message, size_t length, int send_flags, const struct sockaddr *dest_addr, socklen_t dest_len) {
    if (!socket->func->lwip_sendto) {
        errno = EOPNOTSUPP;
        return -1;
    }

    ip_addr_t ipaddr = { 0 };
    u16_t port = 0;
    if (dest_addr) {
        socket_sockaddr_to_lwip(dest_addr, dest_len, &ipaddr, &port);
    }

    TickType_t xTicksToWait = socket->timeout;
    int ret;
    do {
        LOCK_TCPIP_CORE();
        ret = socket_check_ret(socket->func->lwip_sendto(socket, message, length, dest_addr ? &ipaddr : NULL, port));
        UNLOCK_TCPIP_CORE();        
    }
    while (POLL_CHECK(fd_flags, ret, &socket->base, POLLOUT, &xTicksToWait));
    return ret;
}

ssize_t sendto(int fd, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len) {
    int fd_flags;
    struct socket *socket = socket_acquire(fd, &fd_flags);
    if (!socket) {
        return -1;
    }
    int ret = sendto_internal(socket, fd_flags, message, length, flags, dest_addr, dest_len);
    socket_release(socket);
    return ret; 
}

ssize_t send(int fd, const void *buffer, size_t length, int flags) {
    return sendto(fd, buffer, length, flags, NULL, 0);
}

int setsockopt(int fd, int level, int option_name, const void *option_value, socklen_t option_len) {
    int flags;
    struct socket *socket = socket_acquire(fd, &flags);
    if (!socket) {
        return -1;
    }
    int ret = socket_setsockopt(socket, level, option_name, option_value, option_len);
    socket_release(socket);
    return ret; 
}

int shutdown(int fd, int how) {
    int flags;
    struct socket *socket = socket_acquire(fd, &flags);
    if (!socket) {
        return -1;
    }
    bool shut_rx = (how == SHUT_RD) || (how == SHUT_RDWR);
    bool shut_tx = (how == SHUT_WR) || (how == SHUT_RDWR);

    int ret = -1;
    if (!socket->func->lwip_shutdown) {
        errno = EOPNOTSUPP;
        goto exit;
    }

    LOCK_TCPIP_CORE();
    ret = socket_check_ret(socket->func->lwip_shutdown(socket, shut_rx, shut_tx));
    UNLOCK_TCPIP_CORE();

exit:
    socket_release(socket);
    return ret;
}

int socket(int domain, int type, int protocol) {
    u8_t iptype = IPADDR_TYPE_ANY;
    switch (domain) {
        #if LWIP_IPV4
        case AF_INET:
            iptype = IPADDR_TYPE_V4;
            break;
        #endif
        #if LWIP_IPV6
        case AF_INET6:
            iptype = IPADDR_TYPE_V6;
            break;
        #endif
        default:
            errno = EAFNOSUPPORT;
            return -1;
    }

    const struct socket_vtable *vtable;
    int default_proto = 0;
    switch (type) {
        #if LWIP_TCP
        case SOCK_STREAM:
            vtable = &socket_tcp_vtable;
            default_proto = IPPROTO_TCP;
            break;
        #endif
        #if LWIP_RAW
        case SOCK_RAW:
            vtable = &socket_raw_vtable;
            break;
        #endif
        #if LWIP_UDP
        case SOCK_DGRAM:
            vtable = &socket_udp_vtable;
            default_proto = IPPROTO_UDP;
            break;
        #endif
        default:
            errno = ESOCKTNOSUPPORT;
            return -1;
    }
    if (protocol == 0) {
        protocol = default_proto;
    }
    if (((default_proto == 0) && (protocol == 0)) || ((default_proto != 0) && (protocol != default_proto))) {
        errno = EPROTONOSUPPORT;
        return -1;
    }

    struct socket *socket = socket_alloc(vtable, domain, type, protocol);
    if (!socket) {
        return -1;
    }

    LOCK_TCPIP_CORE();
    int ret = socket_check_ret(vtable->lwip_new(socket, iptype, protocol));
    UNLOCK_TCPIP_CORE();
    int fd = -1;
    if (ret >= 0)     {
        fd = poll_file_fd(&socket->base, FREAD | FWRITE);
    }

    socket_release(socket);
    return fd;
}

static int socket_close(void *ctx) {
    struct socket *socket = ctx;
    LOCK_TCPIP_CORE();
    int ret = socket_check_ret(socket->func->lwip_close(socket));
    UNLOCK_TCPIP_CORE();
    if (ret < 0) {
        socket->errcode = errno;
    }

    socket->user_closed = 1;
    if (!socket->errcode) {
        socket->errcode = EBADF;
    }
    
    socket_free(socket);
    return ret;
}

static int socket_read(void *ctx, void *buffer, size_t size, int flags) {
    struct socket *socket = ctx;
    return recvfrom_internal(socket, flags, buffer, size, 0, NULL, NULL);
}

static int socket_write(void *ctx, const void *buffer, size_t size, int flags) {
    struct socket *socket = ctx;
    return sendto_internal(socket, flags, buffer, size, 0, NULL, 0);
}

__attribute__((visibility("hidden")))
const struct vfs_file_vtable socket_vtable = {
    .close = socket_close,
    .pollable = 1,
    .read = socket_read,
    .write = socket_write,
};
