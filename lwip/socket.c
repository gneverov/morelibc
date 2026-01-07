// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>

#include "lwip/ip.h"

#include "morelib/lwip/socket.h"


__attribute__((visibility("hidden")))
int socket_lwip_check_ret(err_t err) {
    if (err >= 0) {
        return err;
    }
    else {
        errno = err_to_errno(err);
        return -1;
    }
}

__attribute__((visibility("hidden")))
struct socket_lwip *socket_lwip_alloc(const struct socket_vtable *vtable, int domain, int type, int protocol) {
    struct socket_lwip *socket = socket_alloc(sizeof(struct socket_lwip), vtable, domain, type, protocol);
    if (!socket) {
        return NULL;
    }
    socket->timeout = portMAX_DELAY;
    return socket;
}

__attribute__((visibility("hidden")))
int socket_domain_to_lwip(int domain, u8_t *iptype) {
    switch (domain) {
        #if LWIP_IPV4
        case AF_INET:
            *iptype = IPADDR_TYPE_V4;
            return 0;
        #endif
        #if LWIP_IPV6
        case AF_INET6:
            *iptype = IPADDR_TYPE_V6;
            return 0;
        #endif
        default:
            errno = EAFNOSUPPORT;
            return -1;
    }
}

__attribute__((visibility("hidden")))
void socket_sockaddr_to_lwip(const struct sockaddr *address, socklen_t address_len, ip_addr_t *ipaddr, u16_t *port) {
    switch (address->sa_family) {
        #if LWIP_IPV4
        case AF_INET: {
            assert(address_len >= sizeof(struct sockaddr_in));
            const struct sockaddr_in *sa = (const struct sockaddr_in *)address;
            IP_SET_TYPE(ipaddr, IPADDR_TYPE_V4);
            inet_addr_to_ip4addr(ip_2_ip4(ipaddr), &sa->sin_addr);
            *port = lwip_ntohs(sa->sin_port);
            break;
        }
        #endif        
        #if LWIP_IPV6
        case AF_INET6: {
            assert(address_len >= sizeof(struct sockaddr_in6));
            const struct sockaddr_in6 *sa = (const struct sockaddr_in6 *)address;
            IP_SET_TYPE(ipaddr, IPADDR_TYPE_V6);
            inet6_addr_to_ip6addr(ip_2_ip6(ipaddr), &sa->sin6_addr);
            ip6_addr_set_zone(ip_2_ip6(ipaddr), sa->sin6_scope_id);
            *port = lwip_ntohs(sa->sin6_port);
            break;
        }
        #endif
        default: {
            ip_addr_set_zero(ipaddr);
            *port = 0;
            break;
        }
    }
}

__attribute__((visibility("hidden")))
void socket_sockaddr_from_lwip(struct sockaddr *address, socklen_t *address_len, const ip_addr_t *ipaddr, u16_t port) {
    switch (IP_GET_TYPE(ipaddr)) {
        #if LWIP_IPV4
        case IPADDR_TYPE_V4: {
            assert(*address_len >= sizeof(struct sockaddr_in));
            struct sockaddr_in *sa = (struct sockaddr_in *)address;
            sa->sin_family = AF_INET;
            sa->sin_port = lwip_htons(port);
            inet_addr_from_ip4addr(&sa->sin_addr, ip_2_ip4(ipaddr));
            *address_len = sizeof(struct sockaddr_in);
            break;
        }
        #endif
        #if LWIP_IPV6
        case IPADDR_TYPE_V6: {
            assert(*address_len >= sizeof(struct sockaddr_in6));
            struct sockaddr_in6 *sa = (struct sockaddr_in6 *)address;
            sa->sin6_family = AF_INET6;
            sa->sin6_port = lwip_htons(port);
            sa->sin6_flowinfo = 0;
            inet6_addr_from_ip6addr(&sa->sin6_addr, ip_2_ip6(ipaddr));
            sa->sin6_scope_id = ip6_addr_zone(ip_2_ip6(ipaddr));
            *address_len = sizeof(struct sockaddr_in6);
            break;
        }
        #endif
        default: {
            address->sa_family = AF_UNSPEC;
            *address_len = sizeof(struct sockaddr);
            break;
        }
    }
}

__attribute__((visibility("hidden")))
struct pbuf *pbuf_advance(struct pbuf *p, u16_t *offset, u16_t len) {
    struct pbuf *new_p = pbuf_skip(p, *offset + len, offset);
    if (new_p != p) {
        pbuf_ref(new_p);
        pbuf_free(p);
    }
    return new_p;
}

__attribute__((visibility("hidden")))
struct pbuf *pbuf_concat(struct pbuf *p, struct pbuf *new_p) {
    if (p) {
        if (new_p) {
            pbuf_cat(p, new_p);
        }
        return p;
    }
    else {
        return new_p;
    }
}

__attribute__((visibility("hidden")))
struct pbuf *pbuf_grow(struct pbuf *p, u16_t new_len) {
    ssize_t delta = new_len - (p ? p->tot_len : 0);
    if (delta > 0) {
        struct pbuf *new_p = pbuf_alloc(PBUF_RAW, delta, PBUF_RAM);
        p = pbuf_concat(p, new_p);
    }
    return p;
}

// __attribute__((visibility("hidden")))
// bool socket_empty(struct socket *socket) {
//     return socket->rx_data == NULL || socket->rx_len == 0;
// }

__attribute__((visibility("hidden")))
int socket_lwip_pop(struct socket_lwip *socket, void *buffer, size_t size) {
    if (!size) {
        errno = EINVAL;
        return -1;
    }

    int ret = -1;
    TickType_t xTicksToWait = socket->timeout;
    do {
        socket_lock(&socket->base);
        if (socket->rx_data != NULL && socket->rx_len > 0) {
            u16_t br = MIN(size, socket->rx_len);
            if (buffer) {
                br = pbuf_copy_partial(socket->rx_data, buffer, br, socket->rx_offset);
            }
            socket->rx_data = pbuf_advance(socket->rx_data, &socket->rx_offset, br);
            socket->rx_len -= br;
            if (socket->rx_data == NULL || socket->rx_len == 0) {
                socket_notify(&socket->base, POLLIN | POLLRDNORM, 0);
            }
            ret = br;
        }
        else {
            if (socket->peer_closed) {
                ret = 0;
            }
            else {
                errno = socket->errcode ? socket->errcode : EAGAIN;
                ret = -1;
            }
        }
        socket_unlock(&socket->base);
    }
    while (POLL_SOCKET_CHECK(ret, &socket->base, POLLIN, &xTicksToWait));
    return ret;
}

__attribute__((visibility("hidden")))
int socket_lwip_push(struct socket_lwip *socket, const void *buffer, size_t size) {
    if (socket->errcode) {
        errno = socket->errcode;
        return -1;
    }

    u16_t offset = socket->rx_offset + socket->rx_len;
    u16_t new_len = (offset + size + 255) & ~255;
    socket->rx_data = pbuf_grow(socket->rx_data, new_len);

    if (pbuf_take_at(socket->rx_data, buffer, size, offset) != ERR_OK) {
        errno = ENOMEM;
        return -1;
    }
    socket->rx_len += size;
    socket_notify(&socket->base, 0, POLLIN | POLLRDNORM);
    return size;
}

__attribute__((visibility("hidden")))
int socket_lwip_push_pbuf(struct socket_lwip *socket, struct pbuf *p) {
    assert(p);
    if (socket->errcode) {
        errno = socket->errcode;
        return -1;
    }    
    socket->rx_len += p->tot_len;
    socket->rx_data = pbuf_concat(socket->rx_data, p);
    assert(socket->rx_offset + socket->rx_len == socket->rx_data->tot_len);
    socket_notify(&socket->base, 0, POLLIN | POLLRDNORM);
    return p->tot_len;
}

__attribute__((visibility("hidden")))
int socket_lwip_dgram_recvfrom(void *ctx, void *buf, size_t len, struct sockaddr *address, socklen_t *address_len) {
    struct socket_lwip *socket = ctx;
    struct socket_lwip_dgram_recv recv_result;
    int ret = socket_lwip_pop(socket, &recv_result, sizeof(recv_result));
    if (ret > 0) {
        ret = pbuf_copy_partial(recv_result.p, buf, len, 0);
        if (address) {
            socket_sockaddr_from_lwip(address, address_len, &recv_result.ipaddr, recv_result.port);
        }
        pbuf_free(recv_result.p);
    }
    return ret;
}
__attribute__((visibility("hidden")))
void socket_lwip_dgram_close(struct socket_lwip *socket) {
    if (socket->rx_data) {
        size_t offset = socket->rx_offset;
        while (offset < socket->rx_len) {
            struct socket_lwip_dgram_recv recv_result;
            pbuf_copy_partial(socket->rx_data, &recv_result, sizeof(recv_result), offset);
            pbuf_free(recv_result.p);
            offset += sizeof(recv_result);
        }
        pbuf_free(socket->rx_data);
    }
}

__attribute__((visibility("hidden")))
int socket_lwip_getpeername(struct socket_lwip *socket, struct sockaddr *address, socklen_t *address_len, int port_offset) {
    LOCK_TCPIP_CORE();
    int err = socket->errcode;
    if (socket->pcb.ip) {
        u16_t port = (port_offset >= 0) ? *(u16_t *)(socket->pcb.ptr + port_offset) : 0;
        socket_sockaddr_from_lwip(address, address_len, &socket->pcb.ip->remote_ip, port);
        err = socket->connected ? ERR_OK : ERR_CONN;
    }
    UNLOCK_TCPIP_CORE();
    return socket_lwip_check_ret(err);
}

__attribute__((visibility("hidden")))
int socket_lwip_getsockname(struct socket_lwip *socket, struct sockaddr *address, socklen_t *address_len, int port_offset) {
    LOCK_TCPIP_CORE();
    int err = socket->errcode;
    if (socket->pcb.ip) {
        u16_t port = (port_offset >= 0) ? *(u16_t *)(socket->pcb.ptr + port_offset) : 0;
        socket_sockaddr_from_lwip(address, address_len, &socket->pcb.ip->local_ip, port);
        err = ERR_OK;
    }
    UNLOCK_TCPIP_CORE();
    return socket_lwip_check_ret(err);
}

static struct socket *socket_lwip_create(int domain, int type, int protocol) {
    u8_t iptype;
    if (socket_domain_to_lwip(domain, &iptype) < 0) {
        return NULL;
    }

    struct socket *(*vtable)(int domain, int type, int protocol);
    switch (type) {
        #if LWIP_TCP
        case SOCK_STREAM:
            struct socket *socket_tcp_socket(int domain, int type, int protocol);
            vtable = socket_tcp_socket;
            break;
        #endif
        #if LWIP_RAW
        case SOCK_RAW:
            struct socket *socket_raw_socket(int domain, int type, int protocol);
            vtable = socket_raw_socket;
            break;
        #endif
        #if LWIP_UDP
        case SOCK_DGRAM:
            struct socket *socket_udp_socket(int domain, int type, int protocol);
            vtable = socket_udp_socket;
            break;
        #endif
        default:
            errno = ESOCKTNOSUPPORT;
            return NULL;
    }

    return vtable(domain, type, protocol);
}

int socketpair(int domain, int type, int protocol, int socket_vector[2]) {
    u8_t iptype;
    if (socket_domain_to_lwip(domain, &iptype) < 0) {
        return -1;
    }

    int ret = -1;
    int client = -1, accepted = -1;
    int server = socket(domain, type, protocol);
    if (server < 0) {
        goto cleanup;
    }

    ip_addr_t ipaddr;
    ip_addr_set_loopback_val(iptype == IPADDR_TYPE_V6, ipaddr);
    struct sockaddr_storage server_addr;
    socklen_t server_addr_len = sizeof(server_addr);
    socket_sockaddr_from_lwip((struct sockaddr *)&server_addr, &server_addr_len, &ipaddr, 0);
    if (bind(server, (struct sockaddr *)&server_addr, server_addr_len) < 0) {
        goto cleanup;
    }

    server_addr_len = sizeof(server_addr);
    if (getsockname(server, (struct sockaddr *)&server_addr, &server_addr_len) < 0) {
        goto cleanup;
    }

    if (listen(server, 1) < 0) {
        goto cleanup;
    }

    client = socket(domain, type, protocol);
    if (client < 0) {
        goto cleanup;
    }

    if (connect(client, (struct sockaddr *)&server_addr, server_addr_len) < 0) {
        goto cleanup;
    }

    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    accepted = accept(server, (struct sockaddr *)&client_addr, &client_addr_len);
    if (accepted < 0) {
        goto cleanup;
    }

    socket_vector[0] = client;
    client = -1;
    socket_vector[1] = accepted;
    accepted = -1;
    ret = 0;

cleanup:
    if (server >= 0) {
        close(server);
    }
    if (client >= 0) {
        close(client);
    }
    if (accepted >= 0) {
        close(accepted);
    }
    return ret;    
}

int lwip_getaddrinfo(const char *nodename, const char *servname, const struct addrinfo *hints, struct addrinfo **res);
int lwip_getnameinfo(const struct sockaddr *sa, socklen_t salen, char *node, socklen_t nodelen, char *service, socklen_t servicelen, int flags);

#if LWIP_IPV4
const struct socket_family lwip_ipv4_af = {
    .family = AF_INET,
    .socket = socket_lwip_create,
    .getaddrinfo = lwip_getaddrinfo,
    .getnameinfo = lwip_getnameinfo,
};
#endif

#if LWIP_IPV6
const struct socket_family lwip_ipv6_af = {
    .family = AF_INET6,
    .socket = socket_lwip_create,
    .getaddrinfo = lwip_getaddrinfo,
    .getnameinfo = lwip_getnameinfo,
};
#endif