// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include "morelib/poll.h"

#include "lwip/dns.h"

#include "morelib/lwip/dns.h"


static const struct socket_vtable socket_dns_vtable;

struct socket *socket_dns(void) {
    struct socket_lwip *socket = socket_lwip_alloc(&socket_dns_vtable, 0, 0, 0);
    return &socket->base;
}

static int socket_dns_close(void *ctx) {
    struct socket_lwip *socket = ctx;
    if (socket->rx_data) {
        size_t offset = socket->rx_offset;
        while (offset < socket->rx_len) {
            struct socket_dns_arg *arg;
            pbuf_copy_partial(socket->rx_data, &arg, sizeof(arg), offset);
            free(arg);
            offset += sizeof(arg);
        }    
        pbuf_free(socket->rx_data);
    }
    return 0;
}

static void socket_dns_lwip_result(struct socket_dns_arg *arg) {
    struct socket_lwip *socket = arg->socket;
    arg->socket = NULL;
    if (socket) {
        socket_lock(&socket->base);
        if (socket_lwip_push(socket, &arg, sizeof(arg)) < 0) {
            free(arg);
        }
        socket_unlock(&socket->base);
        socket_release(&socket->base);
    }
    else {
        free(arg);
    }
}

static void socket_dns_lwip_found(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    // printf("dns_found: name=%s, found=%s\n", name, ipaddr ? ipaddr_ntoa(ipaddr) : "");
    struct socket_dns_arg *arg = callback_arg;
    if (ipaddr) {
        ip_addr_copy(arg->ipaddr, *ipaddr);
    } else {
        ip_addr_set_zero(&arg->ipaddr);
    }
    arg->err = ERR_OK;
    socket_dns_lwip_result(arg);
}

static int socket_dns_sendto(void *ctx, const void *buf, size_t len, const struct sockaddr *address, socklen_t address_len) {
    struct socket_lwip *socket = ctx;
    if (address != NULL) {
        errno = EINVAL;
        return -1;
    }
    struct socket_dns_arg *arg;
    if (len < sizeof(arg)) {
        return 0;
    }
    arg = *(struct socket_dns_arg **)buf;
    arg->socket = socket_copy(&socket->base);
    ip_addr_set_zero(&arg->ipaddr);

    LOCK_TCPIP_CORE();
    arg->err = dns_gethostbyname_addrtype(arg->hostname, &arg->ipaddr, socket_dns_lwip_found, arg, arg->addrtype);
    if (arg->err == ERR_VAL) {
        // lwip returns ERR_VAL if there are no dns servers
        // return that as a not found result
        arg->err = ERR_OK;
    }
    if (arg->err != ERR_INPROGRESS) {
        socket_dns_lwip_result(arg);
    }
    UNLOCK_TCPIP_CORE();
    return sizeof(arg);
}

static int socket_dns_recvfrom(void *ctx, void *buf, size_t len, struct sockaddr *address, socklen_t *address_len) {
    struct socket_lwip *socket = ctx;
    if (address != NULL) {
        errno = EINVAL;
        return -1;
    }
    if (len < sizeof(struct socket_dns_arg *)) {
        return 0;
    }
    return socket_lwip_pop(socket, buf, sizeof(struct socket_dns_arg *));
}

static const struct socket_vtable socket_dns_vtable = {
    .close = socket_dns_close,
    .sendto = socket_dns_sendto,
    .recvfrom = socket_dns_recvfrom,
};
