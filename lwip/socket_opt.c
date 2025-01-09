// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "lwip/ip.h"

#include "./socket.h"

static_assert(SO_BROADCAST == SOF_BROADCAST);
static_assert(SO_KEEPALIVE == SOF_KEEPALIVE);
static_assert(SO_REUSEADDR == SOF_REUSEADDR);


static void socket_getintopt(void *option_value, socklen_t *option_len, int value) {
    if (*option_len >= sizeof(int)) {
        *(int *)option_value = value;
        *option_len = sizeof(int);
    }
}

static void socket_gettvopt(void *option_value, socklen_t *option_len, TickType_t value) {
    if (*option_len >= sizeof(struct timeval)) {
        struct timeval *tv = option_value;
        int timeout = MAX(value, 0) * portTICK_PERIOD_MS;
        tv->tv_sec = timeout / 1000;
        tv->tv_usec = (timeout % 1000) * 1000;
        *option_len = sizeof(struct timeval);
    }
}

__attribute__((visibility("hidden")))
int socket_getsockopt(struct socket *socket, int level, int option_name, void *option_value, socklen_t *option_len) {
    if (level != SOL_SOCKET) {
        errno = EINVAL;
        return -1;
    }
    switch (option_name) {
        case SO_BROADCAST:
        case SO_KEEPALIVE: 
        case SO_REUSEADDR: {
            if (!socket->pcb.ip) {
                errno = EIO;
                return -1;
            }
            socket_getintopt(option_value, option_len, ip_get_option(socket->pcb.ip, option_name));
            return 0;
        }        
        case SO_ACCEPTCONN: {
            socket_getintopt(option_value, option_len, socket->listening);
            return 0;            
        }
        case SO_DOMAIN: {
            socket_getintopt(option_value, option_len, socket->domain);
            return 0;
        }
        case SO_ERROR: {
            socket_getintopt(option_value, option_len, socket->errcode);
            socket->errcode = 0;
            return 0;            
        }
        case SO_PROTOCOL: {
            socket_getintopt(option_value, option_len, socket->protocol);
            return 0;
        }        
        case SO_RCVTIMEO:
        case SO_SNDTIMEO: {
            socket_gettvopt(option_value, option_len, socket->timeout);
            return 0;
        }
        case SO_TYPE: {
            socket_getintopt(option_value, option_len, socket->type);
            return 0;
        }
        default: {
            errno = EINVAL;
            return -1;
        }
    }
}

static int socket_settvopt(const void *option_value, socklen_t option_len, TickType_t *value) {
    if (option_len < sizeof(struct timeval)) {
        errno = EINVAL;
        return -1;
    }
    const struct timeval *tv = option_value;
    if ((tv->tv_sec == 0) && (tv->tv_usec == 0)) {
        *value = portMAX_DELAY;
    } else if ((tv->tv_sec >= 0) && (tv->tv_usec >= 0) && (tv->tv_usec < 1000000)) {
        *value = pdMS_TO_TICKS((tv->tv_sec * 1000) + (tv->tv_usec / 1000));
    } else {
        errno = EDOM;
        return -1;
    }
    return 0;    
}

__attribute__((visibility("hidden")))
int socket_setsockopt(struct socket *socket, int level, int option_name, const void *option_value, socklen_t option_len) {
    if (level != SOL_SOCKET) {
        errno = EINVAL;
        return -1;
    }
    switch (option_name) {
        case SO_BROADCAST:
        case SO_KEEPALIVE: 
        case SO_REUSEADDR: {
            if (!socket->pcb.ip) {
                errno = EIO;
                return -1;
            }
            if (option_len < sizeof(int)) {
                errno = EINVAL;
                return -1;
            }
            if (*(const int *)option_value) {
                ip_set_option(socket->pcb.ip, option_name);
            }
            else {
                ip_reset_option(socket->pcb.ip, option_name);
            }
            return 0;
        }
        case SO_RCVTIMEO:
        case SO_SNDTIMEO: {
            return socket_settvopt(option_value, option_len, &socket->timeout);
        }        
        default: {
            errno = EINVAL;
            return -1;
        }
    }
}
