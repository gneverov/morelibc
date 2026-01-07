// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <netinet/in.h>

#include "lwip/udp.h"

#include "morelib/lwip/socket.h"


static const struct socket_vtable socket_udp_vtable;

static void socket_udp_lwip_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);

struct socket *socket_udp_socket(int domain, int type, int protocol) {
    u8_t iptype;
    if (socket_domain_to_lwip(domain, &iptype) < 0) {
        return NULL;
    }
    if (protocol == 0) {
        protocol = IPPROTO_UDP;
    }
    if (protocol != IPPROTO_UDP) {
        errno = EPROTONOSUPPORT;
        return NULL;
    }
    struct socket_lwip *socket = socket_lwip_alloc(&socket_udp_vtable, domain, type, protocol);
    if (!socket) {
        return NULL;
    }
    LOCK_TCPIP_CORE();
    struct udp_pcb *pcb = udp_new_ip_type(iptype);
    if (!pcb) {
        errno = ENOMEM;
        goto exit;
    }

    socket->pcb.udp = pcb;
    udp_recv(pcb, socket_udp_lwip_recv, socket);

exit:
    UNLOCK_TCPIP_CORE();
    if (!pcb) {
        socket_release(&socket->base);
        socket = NULL;
    }    
    return &socket->base;
}

static int socket_udp_bind(void *ctx, const struct sockaddr *address, socklen_t address_len) {
    struct socket_lwip *socket = ctx;
    ip_addr_t ipaddr;
    u16_t port;
    socket_sockaddr_to_lwip(address, address_len, &ipaddr, &port);

    LOCK_TCPIP_CORE();
    err_t err = udp_bind(socket->pcb.udp, &ipaddr, port);
    UNLOCK_TCPIP_CORE();    
    return socket_lwip_check_ret(err);
}

static int socket_udp_close(void *ctx) {
    struct socket_lwip *socket = ctx;
    LOCK_TCPIP_CORE();
    if (socket->pcb.udp) {
        udp_recv(socket->pcb.udp, NULL, NULL);
        udp_remove(socket->pcb.udp);
        socket->pcb.udp = NULL;
    }
    UNLOCK_TCPIP_CORE();

    socket_lwip_dgram_close(socket);
    return 0;
}

static int socket_udp_connect(void *ctx, const struct sockaddr *address, socklen_t address_len) {
    struct socket_lwip *socket = ctx;
    ip_addr_t ipaddr;
    u16_t port;
    socket_sockaddr_to_lwip(address, address_len, &ipaddr, &port);

    LOCK_TCPIP_CORE();
    err_t err = udp_connect(socket->pcb.udp, &ipaddr, port);
    socket->connected = 1;
    UNLOCK_TCPIP_CORE();    
    return socket_lwip_check_ret(err);
}

static void socket_udp_lwip_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    struct socket_lwip *socket = arg;
    struct socket_lwip_dgram_recv recv_result;
    recv_result.p = p;
    ip_addr_copy(recv_result.ipaddr, *addr);
    recv_result.port = port;
    socket_lock(&socket->base);
    if (socket_lwip_push(socket, &recv_result, sizeof(recv_result)) < 0) {
        pbuf_free(p);
    }
    socket_unlock(&socket->base);
}

static int socket_udp_sendto(void *ctx, const void *buf, size_t len, const struct sockaddr *address, socklen_t address_len) {
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
        err = udp_send(socket->pcb.udp, p);    
    }
    else {
        ip_addr_t ipaddr;
        u16_t port;
        socket_sockaddr_to_lwip(address, address_len, &ipaddr, &port);        
        err = udp_sendto(socket->pcb.udp, p, &ipaddr, port);
    }
    pbuf_free(p);
    UNLOCK_TCPIP_CORE();
    return socket_lwip_check_ret(err);
}

static int socket_udp_getpeername(void *ctx, struct sockaddr *address, socklen_t *address_len) {
    struct socket_lwip *socket = ctx;
    return socket_lwip_getsockname(socket, address, address_len, offsetof(struct udp_pcb, remote_port));
}

static int socket_udp_getsockname(void *ctx, struct sockaddr *address, socklen_t *address_len) {
    struct socket_lwip *socket = ctx;
    return socket_lwip_getsockname(socket, address, address_len, offsetof(struct udp_pcb, local_port));    
}

static const struct socket_vtable socket_udp_vtable = {
    .close = socket_udp_close,
    .bind = socket_udp_bind,
    .connect = socket_udp_connect,
    .getpeername = socket_udp_getpeername,
    .getsockname = socket_udp_getsockname,
    .getsockopt = socket_lwip_getsockopt,
    .recvfrom = socket_lwip_dgram_recvfrom,
    .sendto = socket_udp_sendto,
    .setsockopt = socket_lwip_setsockopt,    
};