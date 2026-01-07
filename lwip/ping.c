// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <time.h>
#include "morelib/poll.h"

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ip4.h"
#include "lwip/raw.h"

#include "morelib/lwip/ping.h"
#include "morelib/lwip/socket.h"

// TTL for ping requests (0 means default)
#define PING_TTL 0

// Value of ICMP ID field for ping (arbitrary)
#define PING_ID 0x1

// Length of payload in ping request
#define PING_PAYLOAD_LEN 32

// Timeout waiting for ping response in milliseconds
#define PING_RX_TIMEOUT 3000

// Time between sending ping requests in milliseconds
#define PING_INTERVAL 1000

// The number of ping requests to send
#define PING_COUNT 4

// Global ping sequence number counter protected by lwip lock
static u16_t ping_seqno;

typedef struct ping_socket {
    TaskHandle_t task;
    struct raw_pcb *pcb;
    clock_t begin;
    ip_addr_t ipaddr;
    u16_t seqno;
    u8_t ttl;
    clock_t end;
} ping_socket_t;

static u8_t ping_socket_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr);

static int ping_socket_init(ping_socket_t *socket, const ip_addr_t *ipaddr, u8_t ttl) {
    socket->task = xTaskGetCurrentTaskHandle();
    u8_t type = IP_GET_TYPE(ipaddr);
    struct raw_pcb *pcb = raw_new_ip_type(type, (type == IPADDR_TYPE_V6) ? IP6_NEXTH_ICMP6 : IP_PROTO_ICMP);
    if (!pcb) {
        errno = ENOMEM;
        return -1;
    }
    if (ttl) {
        pcb->ttl = ttl;
    }
    raw_recv(pcb, ping_socket_recv, socket);
    struct netif *netif;
    const ip_addr_t *bind_addr;
    ip_route_get_local_ip(IP46_ADDR_ANY(type), ipaddr, netif, bind_addr);        
    raw_bind(pcb, bind_addr);
    socket->pcb = pcb;
    return 0;
}

static void ping_socket_deinit(ping_socket_t *socket) {
    if (socket->pcb) {
        LOCK_TCPIP_CORE();
        raw_remove(socket->pcb);
        UNLOCK_TCPIP_CORE();
    }
    socket->pcb = NULL;    
}

static int ping_socket_sendto(ping_socket_t *socket, const void *buf, u16_t len, const ip_addr_t *ipaddr) {
    struct pbuf *p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr) + len, PBUF_RAM);
    if (!p) {
        errno = ENOMEM;
        return -1;
    }

    u16_t seqno = ++ping_seqno;
    if (IP_IS_V6(ipaddr)) {
        struct icmp6_echo_hdr *hdr = p->payload;
        hdr->type = ICMP6_TYPE_EREQ;
        hdr->code = 0;
        hdr->chksum = 0;
        hdr->id = PING_ID;
        hdr->seqno = lwip_htons(seqno);
        pbuf_take_at(p, buf, len, sizeof(struct icmp6_echo_hdr));
        hdr->chksum = ip_chksum_pseudo(p, IP6_NEXTH_ICMP6, p->tot_len, &socket->pcb->local_ip, ipaddr);
    }
    else {
        struct icmp_echo_hdr *hdr = p->payload;
        ICMPH_TYPE_SET(hdr, ICMP_ECHO);
        ICMPH_CODE_SET(hdr, 0);
        hdr->chksum = 0;
        hdr->id = PING_ID;
        hdr->seqno = lwip_htons(seqno);
        pbuf_take_at(p, buf, len, sizeof(struct icmp_echo_hdr));
        hdr->chksum = inet_chksum_pbuf(p);
    }

    err_t err = raw_sendto(socket->pcb, p, ipaddr);
    socket->seqno = seqno;
    socket->ttl = 0;
    socket->begin = clock();
    socket->end = 0;    
    pbuf_free(p);
    return socket_lwip_check_ret(err);
}

static u8_t ping_socket_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *ipaddr) {
    ping_socket_t *socket = arg;
    
    u16_t seqno;
    u8_t ttl;
    if (IP_IS_V6(ipaddr)) {
        struct ip6_hdr *ip_hdr = p->payload;
        struct icmp6_echo_hdr hdr;
        u16_t hdr_len = pbuf_copy_partial(p, &hdr, sizeof(hdr), IP6_HLEN);
        if ((hdr_len < sizeof(hdr)) || (hdr.id != PING_ID) || (hdr.type != ICMP6_TYPE_EREP)) {
            return 0;
        }
        seqno = lwip_ntohs(hdr.seqno);
        ttl = IP6H_HOPLIM(ip_hdr);
    }
    else {
        struct ip_hdr *ip_hdr = p->payload;
        struct icmp_echo_hdr hdr;
        u16_t hdr_len = pbuf_copy_partial(p, &hdr, sizeof(hdr), IPH_HL_BYTES(ip_hdr));
        if ((hdr_len < sizeof(hdr)) || (hdr.id != PING_ID) || (hdr.type != ICMP_ER) || (lwip_ntohs(IPH_OFFSET(ip_hdr)) & IP_MF)) {
            return 0;
        }
        seqno = lwip_ntohs(hdr.seqno);
        ttl = IPH_TTL(ip_hdr);
    }
    if (socket->seqno == seqno) {
        ip_addr_set(&socket->ipaddr, ipaddr);
        socket->ttl = ttl;
        socket->end = clock();
        xTaskNotifyGive(socket->task);
    }
    pbuf_free(p);
    return 1;
}

static int ping_send(ping_socket_t *socket, const void *buf, size_t len, const ip_addr_t *ipaddr, uint32_t timeout_ms) {
    ulTaskNotifyTake(pdTRUE, 0);
    LOCK_TCPIP_CORE();
    int ret = ping_socket_sendto(socket, buf, len, ipaddr);
    UNLOCK_TCPIP_CORE();
    if (ret < 0) {
        return -1;
    }

    TickType_t xTicksToWait = pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        ret = poll_wait(&xTicksToWait);
        if (ret < 0) {
            return -1;
        }        
        if (ret == 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        LOCK_TCPIP_CORE();
        bool done = socket->end;
        UNLOCK_TCPIP_CORE();
        if (done) {
            return 0;
        }
    }
}

int ping(const ip_addr_t *ipaddr) {
    ping_socket_t socket;
    LOCK_TCPIP_CORE();
    int ret = ping_socket_init(&socket, ipaddr, PING_TTL);
    UNLOCK_TCPIP_CORE();
    if (ret < 0) {
        return ret;
    }

    char payload[PING_PAYLOAD_LEN];
    for (size_t i = 0; i < PING_PAYLOAD_LEN; i++) {
        payload[i] = 'a' + (i & 0x1f);
    }

    size_t remaining = PING_COUNT;    
    while (remaining > 0) {
        ret = ping_send(&socket, payload, PING_PAYLOAD_LEN, ipaddr, PING_RX_TIMEOUT);
        if (ret >= 0) {
            char addr_str[IP6ADDR_STRLEN_MAX];
            ipaddr_ntoa_r(&socket.ipaddr, addr_str, IP6ADDR_STRLEN_MAX);
            int time = (socket.end - socket.begin) * 1000 / CLOCKS_PER_SEC;
            printf("Reply from %s: bytes=%u time=%dms TTL=%hhu\n", addr_str, PING_PAYLOAD_LEN, time, socket.ttl);
            struct timespec sleep = { .tv_sec = PING_INTERVAL / 1000, .tv_nsec = (PING_INTERVAL % 1000) * 1000000 };
            if (nanosleep(&sleep, NULL) < 0) {
                break;
            }
        }
        else if (errno == ETIMEDOUT) {
            puts("Request timed out.");
            ret = 0;
        }
        else {
            break;
        }
        remaining--;        
    }
    ping_socket_deinit(&socket);
    return ret;
}
