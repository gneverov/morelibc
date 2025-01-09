// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <sys/socket.h>
#include "morelib/poll.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"


struct socket_vtable;

struct socket {
    struct poll_file base;
    const struct socket_vtable *func;
    SemaphoreHandle_t mutex;
    union {
        struct ip_pcb *ip;
        struct tcp_pcb *tcp;
        struct udp_pcb *udp;
        struct raw_pcb *raw;
        void *ptr;
    } pcb;
    int domain : 8;
    int type : 8;
    int protocol : 8;

    int connected : 1;
    int peer_closed : 1;
    int listening : 1;
    int connecting : 1;
    int user_closed : 1;

    uint8_t local_len;
    uint8_t remote_len;
    struct sockaddr_storage local;
    struct sockaddr_storage remote;
    int errcode;

    struct pbuf *rx_data;
    uint16_t rx_offset;
    uint16_t rx_len;
    TickType_t timeout;
       
    StaticSemaphore_t xMutexBuffer;
};

enum lwip_pcb {
    LWIP_PCB_DNS,
    LWIP_PCB_RAW,
    LWIP_PCB_TCP,
    LWIP_PCB_UDP,
};

struct socket_vtable {
    enum lwip_pcb pcb_type;
    err_t (*lwip_new)(struct socket *socket, u8_t iptype, u8_t proto);
    err_t (*lwip_close)(struct socket *socket);
    err_t (*lwip_abort)(struct socket *socket);
    err_t (*lwip_bind)(struct socket *socket, const ip_addr_t *ipaddr, u16_t port);
    err_t (*lwip_listen)(struct socket *socket, u8_t backlog);
    err_t (*lwip_connect)(struct socket *socket, const ip_addr_t *ipaddr, u16_t port);
    err_t (*lwip_sendto)(struct socket *socket, const void *buf, size_t len, const ip_addr_t *ipaddr, u16_t port);
    err_t (*lwip_shutdown)(struct socket *socket, int shut_rx, int shut_tx);
    err_t (*lwip_output)(struct socket *socket);

    int (*socket_accept)(struct socket *socket, struct socket **new_socket);
    int (*socket_recvfrom)(struct socket *socket, void *buf, size_t len, ip_addr_t *ipaddr, u16_t *port);
    void (*socket_cleanup)(struct socket *socket, struct pbuf *p, u16_t offset, u16_t len);
};

extern const struct vfs_file_vtable socket_vtable;
extern const struct socket_vtable socket_raw_vtable;
extern const struct socket_vtable socket_tcp_vtable;
extern const struct socket_vtable socket_udp_vtable;

// pbuf helpers
struct pbuf *pbuf_advance(struct pbuf *p, u16_t *offset, u16_t len);
struct pbuf *pbuf_concat(struct pbuf *p, struct pbuf *new_p);
struct pbuf *pbuf_grow(struct pbuf *p, u16_t new_len);

// lwip/posix conversions
int socket_check_ret(err_t err);
void socket_sockaddr_to_lwip(const struct sockaddr *address, socklen_t address_len, ip_addr_t *ipaddr, u16_t *port);
void socket_sockaddr_from_lwip(struct sockaddr *address, socklen_t *address_len, const ip_addr_t *ipaddr, u16_t port);

static inline socklen_t socket_sockaddr_storage_from_lwip(struct sockaddr_storage *address, const ip_addr_t *ipaddr, u16_t port) {
    socklen_t address_len = sizeof(*address);
    socket_sockaddr_from_lwip((struct sockaddr *)address, &address_len, ipaddr, port);
    return address_len;
}

// socket lock/unlock
void socket_lock(struct socket *socket);
void socket_unlock(struct socket *socket);

// socket alloc/free
struct socket *socket_alloc(const struct socket_vtable *vtable, int domain, int type, int protocol);
void socket_free(struct socket *socket);

// socket queue operations
bool socket_empty(struct socket *socket);
int socket_pop(struct socket *socket, void *buf, size_t size);
int socket_push(struct socket *socket, const void *buf, size_t size);
void socket_push_pbuf(struct socket *socket, struct pbuf *p);

// datagram helpers
struct socket_dgram_recv {
    struct pbuf *p;
    ip_addr_t ipaddr;
    u16_t port;
};

int socket_dgram_recvfrom(struct socket *socket, void *buf, size_t len, ip_addr_t *ipaddr, u16_t *port);
void socket_dgram_cleanup(struct socket *socket, struct pbuf *p, u16_t offset, u16_t len);

// socket get/set options
int socket_getsockopt(struct socket *socket, int level, int option_name, void *option_value, socklen_t *option_len);
int socket_setsockopt(struct socket *socket, int level, int option_name, const void *option_value, socklen_t option_len);
