// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "lwip/ip.h"

#include "morelib/lwip/socket.h"

static_assert(SO_BROADCAST == SOF_BROADCAST);
static_assert(SO_KEEPALIVE == SOF_KEEPALIVE);
static_assert(SO_REUSEADDR == SOF_REUSEADDR);


__attribute__((visibility("hidden")))
void socket_gettvopt(void *option_value, socklen_t *option_len, TickType_t value) {
    if (*option_len >= sizeof(struct timeval)) {
        struct timeval *tv = option_value;
        int timeout = (value == portMAX_DELAY) ? 0 : (value * portTICK_PERIOD_MS);
        tv->tv_sec = timeout / 1000;
        tv->tv_usec = (timeout % 1000) * 1000;
        *option_len = sizeof(struct timeval);
    }
}

__attribute__((visibility("hidden")))
int socket_lwip_getsockopt(void *ctx, int level, int option_name, void *option_value, socklen_t *option_len) {
    struct socket_lwip *socket = ctx;
    if (level != SOL_SOCKET) {
        errno = ENOPROTOOPT;
        return -1;
    }
    int ret = 0;
    switch (option_name) {
        case SO_BROADCAST:
        case SO_KEEPALIVE: 
        case SO_REUSEADDR: {
            LOCK_TCPIP_CORE();
            if (!socket->pcb.ip) {
                errno = EINVAL;
                ret = -1;
            }
            else {
                socket_getintopt(option_value, option_len, ip_get_option(socket->pcb.ip, option_name));
            }
            UNLOCK_TCPIP_CORE();
            break;
        }        
        case SO_ERROR: {
            socket_lock(&socket->base);
            socket_getintopt(option_value, option_len, socket->errcode);
            socket->errcode = 0;
            socket_unlock(&socket->base);
            break;
        }
        case SO_RCVTIMEO:
        case SO_SNDTIMEO: {
            socket_lock(&socket->base);
            socket_gettvopt(option_value, option_len, socket->timeout);
            socket_unlock(&socket->base);
            break;
        }
        default: {
            errno = ENOPROTOOPT;
            ret = -1;
            break;
        }
    }
    return ret;
}

__attribute__((visibility("hidden")))
int socket_settvopt(const void *option_value, socklen_t option_len, TickType_t *value) {
    if (option_len < sizeof(struct timeval)) {
        errno = EINVAL;
        return -1;
    }
    const struct timeval *tv = option_value;
    if ((tv->tv_sec == 0) && (tv->tv_usec == 0)) {
        *value = portMAX_DELAY;
    } else if ((tv->tv_sec >= 0) && (tv->tv_sec < (portMAX_DELAY / portTICK_PERIOD_MS)) && (tv->tv_usec >= 0) && (tv->tv_usec < 1000000)) {
        *value = pdMS_TO_TICKS((tv->tv_sec * 1000) + (tv->tv_usec / 1000));
    } else {
        errno = EDOM;
        return -1;
    }
    return 0;    
}

__attribute__((visibility("hidden")))
int socket_lwip_setsockopt(void *ctx, int level, int option_name, const void *option_value, socklen_t option_len) {
    struct socket_lwip *socket = ctx;
    if (level != SOL_SOCKET) {
        errno = ENOPROTOOPT;
        return -1;
    }
    int ret = 0;
    switch (option_name) {
        case SO_BROADCAST:
        case SO_KEEPALIVE: 
        case SO_REUSEADDR: {
            LOCK_TCPIP_CORE();
            if (!socket->pcb.ip) {
                errno = EINVAL;
                ret = -1;
            }
            else if (option_len < sizeof(int)) {
                errno = EINVAL;
                ret = -1;
            }
            else if (*(const int *)option_value) {
                ip_set_option(socket->pcb.ip, option_name);
            }
            else {
                ip_reset_option(socket->pcb.ip, option_name);
            }
            UNLOCK_TCPIP_CORE();
            break;
        }
        case SO_RCVTIMEO:
        case SO_SNDTIMEO: {
            socket_lock(&socket->base);
            ret = socket_settvopt(option_value, option_len, &socket->timeout);
            socket_unlock(&socket->base);
            break;
        }        
        default: {
            errno = ENOPROTOOPT;
            ret = -1;
            break;
        }
    }
    return ret;
}
