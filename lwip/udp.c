// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "morelib/poll.h"

#include "lwip/udp.h"

#include "./socket.h"


static void socket_udp_lwip_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);

static err_t socket_udp_lwip_new(struct socket *socket, u8_t iptype, u8_t proto) {
    if (socket->pcb.udp) {
        return ERR_VAL;
    }

    struct udp_pcb *pcb = udp_new_ip_type(iptype);
    if (!pcb) {
        return ERR_MEM;
    }
    
    socket->pcb.udp = pcb;
    udp_recv(pcb, socket_udp_lwip_recv, socket);
    return ERR_OK;
}

static err_t socket_udp_lwip_abort(struct socket *socket) {
    if (socket->pcb.udp) {
        udp_recv(socket->pcb.udp, NULL, NULL);
        udp_remove(socket->pcb.udp);
        socket->pcb.udp = NULL;
    }
    return ERR_OK;
}

static err_t socket_udp_lwip_bind(struct socket *socket, const ip_addr_t *ipaddr, u16_t port) {
    if (!socket->pcb.udp) {
        return ERR_ARG;
    }     
    err_t err = udp_bind(socket->pcb.udp, ipaddr, port);
    if (err == ERR_OK) {
        socket_lock(socket);
        socket->local_len = socket_sockaddr_storage_from_lwip(&socket->local, &socket->pcb.udp->local_ip, port);
        socket_unlock(socket);
    }
    return err;
}

static err_t socket_udp_lwip_connect(struct socket *socket, const ip_addr_t *ipaddr, u16_t port) {
    struct udp_pcb *pcb = socket->pcb.udp;
    if (!socket->pcb.udp) {
        return ERR_ARG;
    }  
    err_t err = udp_connect(pcb, ipaddr, port);
    if (err == ERR_OK) {
        socket_lock(socket);
        socket->connected = 1;
        socket->local_len = socket_sockaddr_storage_from_lwip(&socket->local, &pcb->local_ip, pcb->local_port);
        socket->remote_len = socket_sockaddr_storage_from_lwip(&socket->remote, &pcb->remote_ip, pcb->remote_port);
        socket_unlock(socket);
    }
    return err;
}

static void socket_udp_lwip_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    struct socket *socket = arg;
    struct socket_dgram_recv recv_result;
    recv_result.p = p;
    ip_addr_copy(recv_result.ipaddr, *addr);
    recv_result.port = port;

    if (socket_push(socket, &recv_result, sizeof(recv_result)) < 0) {
        pbuf_free(p);
    }
}

static err_t socket_udp_lwip_sendto(struct socket *socket, const void *buf, size_t len, const ip_addr_t *ipaddr, u16_t port) {
    if (!socket->pcb.udp) {
        return ERR_ARG;
    }      
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (!p) {
        return ERR_MEM;
    }
    err_t err = pbuf_take(p, buf, len);
    assert(err == ERR_OK);
    
    if (ipaddr == NULL) {
        err = udp_send(socket->pcb.udp, p);    
    }
    else {
        err = udp_sendto(socket->pcb.udp, p, ipaddr, port);
    }
    pbuf_free(p);
    return err;
}

__attribute__((visibility("hidden")))
const struct socket_vtable socket_udp_vtable = {
    .pcb_type = LWIP_PCB_UDP,

    .lwip_new = socket_udp_lwip_new,
    .lwip_close = socket_udp_lwip_abort,
    .lwip_abort = socket_udp_lwip_abort,
    .lwip_bind = socket_udp_lwip_bind,
    .lwip_connect = socket_udp_lwip_connect,
    .lwip_sendto = socket_udp_lwip_sendto,
    
    .socket_recvfrom = socket_dgram_recvfrom,
    .socket_cleanup = socket_dgram_cleanup,
};