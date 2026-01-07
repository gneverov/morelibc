// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "morelib/socket.h"

#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"


struct socket_lwip {
    struct socket base;
    union {
        struct ip_pcb *ip;
        struct tcp_pcb *tcp;
        struct udp_pcb *udp;
        struct raw_pcb *raw;
        void *ptr;
    } pcb;

    int listening : 1;
    int connected : 1;
    int peer_closed : 1;
    int errcode;
    TickType_t timeout;
    
    struct pbuf *rx_data;
    uint16_t rx_offset;
    uint16_t rx_len;
};

struct socket_lwip *socket_lwip_alloc(const struct socket_vtable *vtable, int domain, int type, int protocol);

// pbuf helpers
struct pbuf *pbuf_advance(struct pbuf *p, u16_t *offset, u16_t len);
struct pbuf *pbuf_concat(struct pbuf *p, struct pbuf *new_p);
struct pbuf *pbuf_grow(struct pbuf *p, u16_t new_len);

// lwip/posix conversions
int socket_lwip_check_ret(err_t err);
int socket_domain_to_lwip(int domain, u8_t *iptype);
void socket_sockaddr_to_lwip(const struct sockaddr *address, socklen_t address_len, ip_addr_t *ipaddr, u16_t *port);
void socket_sockaddr_from_lwip(struct sockaddr *address, socklen_t *address_len, const ip_addr_t *ipaddr, u16_t port);

static inline socklen_t socket_sockaddr_storage_from_lwip(struct sockaddr_storage *address, const ip_addr_t *ipaddr, u16_t port) {
    socklen_t address_len = sizeof(*address);
    socket_sockaddr_from_lwip((struct sockaddr *)address, &address_len, ipaddr, port);
    return address_len;
}

// socket queue operations
// bool socket_empty(struct socket *socket);
int socket_lwip_pop(struct socket_lwip *socket, void *buf, size_t size);
int socket_lwip_push(struct socket_lwip *socket, const void *buf, size_t size);
int socket_lwip_push_pbuf(struct socket_lwip *socket, struct pbuf *p);

// datagram helpers
struct socket_lwip_dgram_recv {
    struct pbuf *p;
    ip_addr_t ipaddr;
    u16_t port;
};

int socket_lwip_dgram_recvfrom(void *ctx, void *buf, size_t len, struct sockaddr *address, socklen_t *address_len);
void socket_lwip_dgram_close(struct socket_lwip *socket);

// socket get/set options
int socket_lwip_getsockopt(void *ctx, int level, int option_name, void *option_value, socklen_t *option_len);
int socket_lwip_setsockopt(void *ctx, int level, int option_name, const void *option_value, socklen_t option_len);

int socket_lwip_getpeername(struct socket_lwip *socket, struct sockaddr *address, socklen_t *address_len, int port_offset);
int socket_lwip_getsockname(struct socket_lwip *socket, struct sockaddr *address, socklen_t *address_len, int port_offset);

void socket_gettvopt(void *option_value, socklen_t *option_len, TickType_t value);
int socket_settvopt(const void *option_value, socklen_t option_len, TickType_t *value);