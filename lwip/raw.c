// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <netinet/in.h>

#include "lwip/raw.h"

#include "morelib/lwip/socket.h"


static const struct socket_vtable socket_raw_vtable;

static u8_t socket_raw_lwip_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr);

__attribute__((visibility("hidden")))
struct socket *socket_raw_socket(int domain, int type, int protocol) {
    u8_t iptype;
    if (socket_domain_to_lwip(domain, &iptype) < 0) {
        return NULL;
    }
    if (protocol == 0) {
        errno = EPROTONOSUPPORT;
        return NULL;
    }
    struct socket_lwip *socket = socket_lwip_alloc(&socket_raw_vtable, domain, type, protocol);
    if (!socket) {
        return NULL;
    }
    LOCK_TCPIP_CORE();    
    struct raw_pcb *pcb = raw_new_ip_type(iptype, protocol);
    if (!pcb) {
        errno = ENOMEM;
        goto exit;
    }
    
    socket->pcb.raw = pcb;
    raw_recv(pcb, socket_raw_lwip_recv, socket);

exit:
    UNLOCK_TCPIP_CORE();
    if (!pcb) {
        socket_release(&socket->base);
        socket = NULL;
    }
    return &socket->base;
}

static int socket_raw_bind(void *ctx, const struct sockaddr *address, socklen_t address_len) {
    struct socket_lwip *socket = ctx;
    ip_addr_t ipaddr;
    u16_t port;
    socket_sockaddr_to_lwip(address, address_len, &ipaddr, &port);

    LOCK_TCPIP_CORE();
    err_t err = socket->pcb.raw ? raw_bind(socket->pcb.raw, &ipaddr) : socket->errcode;
    UNLOCK_TCPIP_CORE();    
    return socket_lwip_check_ret(err);
}

static int socket_raw_close(void *ctx) {
    struct socket_lwip *socket = ctx;
    LOCK_TCPIP_CORE();
    if (socket->pcb.raw) {
        raw_recv(socket->pcb.raw, NULL, NULL);
        raw_remove(socket->pcb.raw);
        socket->pcb.raw = NULL;
    }
    UNLOCK_TCPIP_CORE();

    socket_lwip_dgram_close(socket);
    return 0;
}

static int socket_raw_connect(void *ctx, const struct sockaddr *address, socklen_t address_len) {
    struct socket_lwip *socket = ctx;
    ip_addr_t ipaddr;
    u16_t port;
    socket_sockaddr_to_lwip(address, address_len, &ipaddr, &port);

    LOCK_TCPIP_CORE();
    err_t err = raw_connect(socket->pcb.raw, &ipaddr);
    socket->connected = 1;
    UNLOCK_TCPIP_CORE();    
    return socket_lwip_check_ret(err);
}

static u8_t socket_raw_lwip_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
    struct socket_lwip *socket = arg;
    struct socket_lwip_dgram_recv recv_result;
    recv_result.p = p;
    ip_addr_copy(recv_result.ipaddr, *addr);
    recv_result.port = 0;
    socket_lock(&socket->base);
    if (socket_lwip_push(socket, &recv_result, sizeof(recv_result)) < 0) {
        pbuf_free(p);
    }
    socket_unlock(&socket->base);
    return 1;
}

static int socket_raw_sendto(void *ctx, const void *buf, size_t len, const struct sockaddr *address, socklen_t address_len) {
    struct socket_lwip *socket = ctx;
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (!p) {
        errno = ENOMEM;
        return -1;
    }
    err_t err = pbuf_take(p, buf, len);
    assert(err == ERR_OK);
    
    LOCK_TCPIP_CORE();
    if (address == NULL) {
        err = raw_send(socket->pcb.raw, p);    
    }
    else {
        ip_addr_t ipaddr;
        u16_t port;
        socket_sockaddr_to_lwip(address, address_len, &ipaddr, &port);           
        err = raw_sendto(socket->pcb.raw, p, &ipaddr);
    }
    pbuf_free(p);
    UNLOCK_TCPIP_CORE();
    return socket_lwip_check_ret(err);
}

static int socket_raw_getpeername(void *ctx, struct sockaddr *address, socklen_t *address_len) {
    struct socket_lwip *socket = ctx;
    return socket_lwip_getsockname(socket, address, address_len, -1);
}

static int socket_raw_getsockname(void *ctx, struct sockaddr *address, socklen_t *address_len) {
    struct socket_lwip *socket = ctx;
    return socket_lwip_getsockname(socket, address, address_len, -1);
}

static const struct socket_vtable socket_raw_vtable = {
    .close = socket_raw_close,
    .bind = socket_raw_bind,
    .connect = socket_raw_connect,
    .getpeername = socket_raw_getpeername,
    .getsockname = socket_raw_getsockname,
    .getsockopt = socket_lwip_getsockopt,
    .recvfrom = socket_lwip_dgram_recvfrom,
    .sendto = socket_raw_sendto,
    .setsockopt = socket_lwip_setsockopt,        
};