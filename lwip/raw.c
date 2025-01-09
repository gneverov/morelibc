// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "morelib/poll.h"

#include "lwip/raw.h"

#include "./socket.h"


static u8_t socket_raw_lwip_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr);

static err_t socket_raw_lwip_new(struct socket *socket, u8_t iptype, u8_t proto) {
    if (socket->pcb.raw) {
        return ERR_VAL;
    }

    struct raw_pcb *pcb = raw_new_ip_type(iptype, proto);
    if (!pcb) {
        return ERR_MEM;
    }
    
    socket->pcb.raw = pcb;
    raw_recv(pcb, socket_raw_lwip_recv, socket);
    return ERR_OK;
}

static err_t socket_raw_lwip_abort(struct socket *socket) {
    if (socket->pcb.raw) {
        raw_recv(socket->pcb.raw, NULL, NULL);
        raw_remove(socket->pcb.raw);
        socket->pcb.raw = NULL;
    }
    return ERR_OK;
}

static err_t socket_raw_lwip_bind(struct socket *socket, const ip_addr_t *ipaddr, u16_t port) {
    struct raw_pcb *pcb = socket->pcb.raw;
    if (!pcb) {
        return ERR_ARG;
    }
    err_t err = raw_bind(pcb, ipaddr);
    if (err == ERR_OK) {
        socket_lock(socket);
        socket->local_len = socket_sockaddr_storage_from_lwip(&socket->local, &pcb->local_ip, 0);
        socket_unlock(socket);
    }
    return err;
}

static err_t socket_raw_lwip_connect(struct socket *socket, const ip_addr_t *ipaddr, u16_t port) {
    struct raw_pcb *pcb = socket->pcb.raw;
    if (!pcb) {
        return ERR_ARG;
    }
    err_t err = raw_connect(pcb, ipaddr);
    if (err == ERR_OK) {
        socket_lock(socket);
        socket->connected = 1;
        socket->local_len = socket_sockaddr_storage_from_lwip(&socket->local, &pcb->local_ip, 0);
        socket->remote_len = socket_sockaddr_storage_from_lwip(&socket->remote, &pcb->remote_ip, 0);
        socket_unlock(socket);
    }
    return err;
}

static u8_t socket_raw_lwip_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
    struct socket *socket = arg;
    struct socket_dgram_recv recv_result;
    recv_result.p = p;
    ip_addr_copy(recv_result.ipaddr, *addr);
    recv_result.port = 0;

    if (socket_push(socket, &recv_result, sizeof(recv_result)) < 0) {
        pbuf_free(p);
    }
    return 1;
}

static err_t socket_raw_lwip_sendto(struct socket *socket, const void *buf, size_t len, const ip_addr_t *ipaddr, u16_t port) {
    if (!socket->pcb.raw) {
        return ERR_ARG;
    }      
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (!p) {
        return ERR_MEM;
    }
    err_t err = pbuf_take(p, buf, len);
    assert(err == ERR_OK);
    
    if (ipaddr == NULL) {
        err = raw_send(socket->pcb.raw, p);    
    }
    else {
        err = raw_sendto(socket->pcb.raw, p, ipaddr);
    }
    pbuf_free(p);
    return err;
}

__attribute__((visibility("hidden")))
const struct socket_vtable socket_raw_vtable = {
    .pcb_type = LWIP_PCB_RAW,

    .lwip_new = socket_raw_lwip_new,
    .lwip_close = socket_raw_lwip_abort,
    .lwip_abort = socket_raw_lwip_abort,
    .lwip_bind = socket_raw_lwip_bind,
    .lwip_connect = socket_raw_lwip_connect,
    .lwip_sendto = socket_raw_lwip_sendto,
    
    .socket_recvfrom = socket_dgram_recvfrom,
    .socket_cleanup = socket_dgram_cleanup,
};