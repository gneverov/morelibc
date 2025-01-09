// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include "morelib/poll.h"

#include "lwip/dns.h"

#include "./dns.h"


static const struct socket_vtable socket_dns_vtable;

int socket_dns(void) {
    struct socket *socket = socket_alloc(&socket_dns_vtable, 0, 0, 0);
    if (!socket) {
        return -1;
    }

    int fd = poll_file_fd(&socket->base, FREAD | FWRITE);
    poll_file_release(&socket->base);
    return fd;
}

static err_t socket_dns_lwip_abort(struct socket *socket) {
    return ERR_OK;
}

static void socket_dns_lwip_result(struct socket_dns_arg *arg, const ip_addr_t *ipaddr, err_t err) {
    if (ipaddr) {
        ip_addr_copy(arg->ipaddr, *ipaddr);
    }
    arg->err = err;
    
    struct socket *socket = arg->socket;
    arg->socket = NULL;
    if (socket) {
        if (socket_push(socket, &arg, sizeof(arg)) < 0) {
            free(arg);
        }
        poll_file_release(&socket->base);
    }
    else {
        free(arg);
    }
}

static void socket_dns_lwip_found(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    // printf("dns_found: name=%s, found=%s\n", name, ipaddr ? ipaddr_ntoa(ipaddr) : "");
    struct socket_dns_arg *arg = callback_arg;
    socket_dns_lwip_result(arg, ipaddr, ERR_OK);
}

static err_t socket_dns_lwip_sendto(struct socket *socket, const void *buf, size_t len, const ip_addr_t *ipaddr, u16_t port) {
    if (ipaddr != NULL) {
        return ERR_ARG;
    }

    struct socket_dns_arg *arg;
    if (len < sizeof(arg)) {
        return 0;
    }
    arg = *(struct socket_dns_arg **)buf;
    arg->socket = poll_file_copy(&socket->base);
    ip_addr_set_any_val(0, arg->ipaddr);

    err_t err = dns_gethostbyname_addrtype(arg->hostname, &arg->ipaddr, socket_dns_lwip_found, arg, arg->addrtype);
    if (err == ERR_OK) {
        socket_dns_lwip_result(arg, NULL, ERR_OK);
    } 
    else if (err != ERR_INPROGRESS) {
        socket_dns_lwip_result(arg, NULL, err);
    }
    return sizeof(arg);
}

static int socket_dns_recvfrom(struct socket *socket, void *buf, size_t len, ip_addr_t *ipaddr, u16_t *port) {
    if (ipaddr != NULL) {
        errno = EINVAL;
        return -1;
    }
    struct socket_dns_arg *arg;
    if (len < sizeof(arg)) {
        return 0;
    }    
    if (socket_pop(socket, &arg, sizeof(arg)) < 0) {
        return -1;
    }
    *(struct socket_dns_arg **)buf = arg;
    return sizeof(arg);
}

static void socket_dns_cleanup(struct socket *socket, struct pbuf *p, u16_t offset, u16_t len) {
    struct socket_dns_arg *arg;
    while (p != NULL && len >= sizeof(arg)) {
        u16_t br = pbuf_copy_partial(p, &arg, sizeof(arg), offset);
        assert(br == sizeof(arg));
        p = pbuf_skip(p, offset + br, &offset);
        len -= br;
        free(arg);
    }
}

static const struct socket_vtable socket_dns_vtable = {
    .pcb_type = LWIP_PCB_DNS,

    .lwip_close = socket_dns_lwip_abort,
    .lwip_abort = socket_dns_lwip_abort,
    .lwip_sendto = socket_dns_lwip_sendto,
    
    .socket_recvfrom = socket_dns_recvfrom,
    .socket_cleanup = socket_dns_cleanup,
};
