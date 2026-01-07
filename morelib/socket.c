// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <string.h>
#include "morelib/socket.h"


static const struct vfs_file_vtable socket_vtable;

struct socket *socket_acquire(int fd) {
    struct vfs_file *file = vfs_acquire_file(fd, FREAD | FWRITE);
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

void *socket_alloc(size_t size, const struct socket_vtable *vtable, int domain, int type, int protocol) {
    struct socket *socket = calloc(1, size);
    if (!socket) {
        return NULL;
    }
    poll_file_init(&socket->base, &socket_vtable, O_RDWR, 0);
    socket->func = vtable;
    socket->mutex = xSemaphoreCreateMutexStatic(&socket->xMutexBuffer);
    socket->domain = domain;
    socket->type = type;
    socket->protocol = protocol;
    return socket;
}

__attribute__((visibility("hidden")))
void socket_getintopt(void *option_value, socklen_t *option_len, int value) {
    if (*option_len >= sizeof(int)) {
        *(int *)option_value = value;
        *option_len = sizeof(int);
    }
}

int socket_getsockopt(struct socket *socket, int level, int option_name, void *option_value, socklen_t *option_len) {
    if (level == SOL_SOCKET) {
        switch (option_name) {
            case SO_ACCEPTCONN: {
                socket_getintopt(option_value, option_len, socket->listening);
                return 0;            
            }
            case SO_DOMAIN: {
                socket_getintopt(option_value, option_len, socket->domain);
                return 0;
            }
            case SO_PROTOCOL: {
                socket_getintopt(option_value, option_len, socket->protocol);
                return 0;
            }        
            case SO_TYPE: {
                socket_getintopt(option_value, option_len, socket->type);
                return 0;
            }
            default: {
                break;
            }
        }
    }

    if (!socket->func->getsockopt) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return socket->func->getsockopt(socket, level, option_name, option_value, option_len);
}

__attribute__((visibility("hidden")))
int socket_setintopt(const void *option_value, socklen_t option_len, int *value) {
    if (option_len < sizeof(int)) {
        errno = EINVAL;
        return -1;
    }
    *value = *(const int *)option_value;
    return 0;    
}

int socket_setsockopt(struct socket *socket, int level, int option_name, const void *option_value, socklen_t option_len) {
    if (level == SOL_SOCKET) {
        switch (option_name) {
            case SO_ACCEPTCONN:
            case SO_DOMAIN:
            case SO_PROTOCOL:
            case SO_TYPE: {
                errno = ENOPROTOOPT;
                return -1;
            }
            default: {
                break;
            }
        }
    }

    if (!socket->func->setsockopt) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return socket->func->setsockopt(socket, level, option_name, option_value, option_len);
}

// ### socket API
int accept(int fd, struct sockaddr *address, socklen_t *address_len) {
    struct socket *socket = socket_acquire(fd);
    if (!socket) {
        return -1;
    }

    int ret = -1;
    struct socket *new_socket = socket_accept(socket, address, address_len);
    if (new_socket) {
        ret = socket_fd(new_socket);
        socket_release(new_socket);
    }

    socket_release(socket);
    return ret;
}

int bind(int fd, const struct sockaddr *address, socklen_t address_len) {
    struct socket *socket = socket_acquire(fd);
    if (!socket) {
        return -1;
    }
    int ret = socket_bind(socket, address, address_len);
    socket_release(socket);
    return ret;
}

int connect(int fd, const struct sockaddr *address, socklen_t address_len) {
    struct socket *socket = socket_acquire(fd);
    if (!socket) {
        return -1;
    }

    int ret = socket_connect(socket, address, address_len);
    if (ret >= 0) {
        socket->connecting = 1;  
    }

    socket_release(socket);
    return ret;
}

int getpeername(int fd, struct sockaddr *address, socklen_t *address_len) {
    struct socket *socket = socket_acquire(fd);
    if (!socket) {
        return -1;
    }
    int ret = socket_getpeername(socket, address, address_len);
    socket_release(socket);
    return ret;
}

int getsockname(int fd, struct sockaddr *address, socklen_t *address_len) {
    struct socket *socket = socket_acquire(fd);
    if (!socket) {
        return -1;
    }
    int ret = socket_getsockname(socket, address, address_len);
    socket_release(socket);
    return ret;
}

int getsockopt(int fd, int level, int option_name, void *option_value, socklen_t *option_len) {
    struct socket *socket = socket_acquire(fd);
    if (!socket) {
        return -1;
    }
    int ret = socket_getsockopt(socket, level, option_name, option_value, option_len);
    socket_release(socket);
    return ret; 
}

int listen(int fd, int backlog) {
    struct socket *socket = socket_acquire(fd);
    if (!socket) {
        return -1;
    }
    
    int ret = socket_listen(socket, backlog);
    if (ret >= 0) {
        socket->listening = 1;
    }

    socket_release(socket);
    return ret;
}

ssize_t recvfrom(int fd, void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len) {
    struct socket *socket = socket_acquire(fd);
    if (!socket) {
        return -1;
    }
    int ret = socket_recvfrom(socket, buffer, length, flags, address, address_len);
    socket_release(socket);
    return ret;   
}

ssize_t recv(int fd, void *buffer, size_t length, int flags) {
    return recvfrom(fd, buffer, length, flags, NULL, NULL);
}

ssize_t sendto(int fd, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len) {
    struct socket *socket = socket_acquire(fd);
    if (!socket) {
        return -1;
    }
    int ret = socket_sendto(socket, message, length, flags, dest_addr, dest_len);
    socket_release(socket);
    return ret; 
}

ssize_t send(int fd, const void *buffer, size_t length, int flags) {
    return sendto(fd, buffer, length, flags, NULL, 0);
}

int setsockopt(int fd, int level, int option_name, const void *option_value, socklen_t option_len) {
    struct socket *socket = socket_acquire(fd);
    if (!socket) {
        return -1;
    }
    int ret = socket_setsockopt(socket, level, option_name, option_value, option_len);
    socket_release(socket);
    return ret; 
}

int shutdown(int fd, int how) {
    struct socket *socket = socket_acquire(fd);
    if (!socket) {
        return -1;
    }
    int ret = socket_shutdown(socket, how);
    socket_release(socket);
    return ret;
}

int socket(int domain, int type, int protocol) {
    const struct socket_family *family = NULL;
    for (size_t i = 0; i < socket_num_families; i++) {
        if (socket_families[i]->family == domain) {
            family = socket_families[i];
            break;
        }
    }
    if (!family) {
        errno = EAFNOSUPPORT;
        return -1;
    }

    int ret = -1;
    struct socket *socket = socket_socket(family, domain, type, protocol);
    if (socket) {
        ret = socket_fd(socket);
        socket_release(socket);
    }
    return ret;
}

__attribute__((weak))
int socketpair(int domain, int type, int protocol, int socket_vector[2]) {
    errno = EOPNOTSUPP;
    return -1;
}

static int socket_close(void *ctx) {
    struct socket *socket = ctx;
    int ret = 0;
    if (socket->func->close) {
        ret = socket->func->close(socket);
    }
    vSemaphoreDelete(socket->mutex);
    free(socket);
    return ret;
}

static int socket_fstat(void *ctx, struct stat *pstat) {
    pstat->st_mode = S_IFSOCK;
    return 0;
}

static int socket_read(void *ctx, void *buffer, size_t size) {
    struct socket *socket = ctx;
    return socket_recvfrom(socket, buffer, size, 0, NULL, NULL);
}

static int socket_write(void *ctx, const void *buffer, size_t size) {
    struct socket *socket = ctx;
    return socket_sendto(socket, buffer, size, 0, NULL, 0);
}

static const struct vfs_file_vtable socket_vtable = {
    .close = socket_close,
    .fstat = socket_fstat,
    .pollable = 1,
    .read = socket_read,
    .write = socket_write,
};
