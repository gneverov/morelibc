// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include "morelib/poll.h"

#include "lwip/ip_addr.h"

#include "./socket.h"


__attribute__((visibility("hidden")))
int socket_check_ret(err_t err) {
    if (err >= 0) {
        return err;
    }
    else {
        errno = err_to_errno(err);
        return -1;
    }
}

__attribute__((visibility("hidden")))
void socket_lock(struct socket *socket) {
    if (!xSemaphoreTake(socket->mutex, portMAX_DELAY)) {
        assert(0);
    }
}

__attribute__((visibility("hidden")))
void socket_unlock(struct socket *socket) {
    if (!xSemaphoreGive(socket->mutex)) {
        assert(0);
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
struct socket *socket_alloc(const struct socket_vtable *vtable, int domain, int type, int protocol) {
    struct socket *socket = calloc(1, sizeof(struct socket));
    if (!socket) {
        return NULL;
    }
    poll_file_init(&socket->base, &socket_vtable, S_IFSOCK, 0);
    socket->func = vtable;
    socket->mutex = xSemaphoreCreateMutexStatic(&socket->xMutexBuffer);
    socket->domain = domain;
    socket->type = type;
    socket->protocol = protocol;
    socket->timeout = portMAX_DELAY;
    return socket;
}

__attribute__((visibility("hidden")))
void socket_free(struct socket *socket) {
    if (socket->rx_data) {
        if (socket->func->socket_cleanup) {
            socket->func->socket_cleanup(socket, socket->rx_data, socket->rx_offset, socket->rx_len);
        }
        pbuf_free(socket->rx_data);
    }
    socket->rx_data = NULL;
    vSemaphoreDelete(socket->mutex);
    free(socket);
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

__attribute__((visibility("hidden")))
bool socket_empty(struct socket *socket) {
    return socket->rx_data == NULL || socket->rx_len == 0;
}

__attribute__((visibility("hidden")))
int socket_pop(struct socket *socket, void *buffer, size_t size) {
    if (!size) {
        errno = EINVAL;
        return -1;
    }

    int ret = -1;
    socket_lock(socket);
    if (socket->errcode) {
        errno = socket->errcode;
        goto exit;
    }

    if (socket->rx_data != NULL && socket->rx_len > 0) {
        u16_t br = MIN(size, socket->rx_len);
        if (buffer) {
            br = pbuf_copy_partial(socket->rx_data, buffer, br, socket->rx_offset);
        }
        socket->rx_data = pbuf_advance(socket->rx_data, &socket->rx_offset, br);
        socket->rx_len -= br;
        ret = br;
    }
    else {
        poll_file_notify(&socket->base, POLLIN | POLLRDNORM, 0);
        if (socket->peer_closed) {
            ret = 0;
        }
        else {
            errno = EAGAIN;
        }
    }    
    
exit:
    socket_unlock(socket);
    return ret;
}

__attribute__((visibility("hidden")))
int socket_push(struct socket *socket, const void *buffer, size_t size) {
    int ret = -1;
    socket_lock(socket);
    if (socket->errcode) {
        errno = socket->errcode;
        goto exit;
    }

    u16_t offset = socket->rx_offset + socket->rx_len;
    u16_t new_len = (offset + size + 255) & ~255;
    socket->rx_data = pbuf_grow(socket->rx_data, new_len);

    if (pbuf_take_at(socket->rx_data, buffer, size, offset) != ERR_OK) {
        errno = ENOMEM;
        goto exit;
    }
    socket->rx_len += size;
    poll_file_notify(&socket->base, 0, POLLIN | POLLRDNORM);
    ret = size;

exit:
    socket_unlock(socket);
    return ret;
}

__attribute__((visibility("hidden")))
void socket_push_pbuf(struct socket *socket, struct pbuf *p) {
    assert(p);
    socket->rx_len += p->tot_len;
    socket->rx_data = pbuf_concat(socket->rx_data, p);
    assert(socket->rx_offset + socket->rx_len == socket->rx_data->tot_len);
    poll_file_notify(&socket->base, 0, POLLIN | POLLRDNORM);
}

__attribute__((visibility("hidden")))
int socket_dgram_recvfrom(struct socket *socket, void *buf, size_t len, ip_addr_t *ipaddr, u16_t *port) {
    struct socket_dgram_recv recv_result;
    int ret = socket_pop(socket, &recv_result, sizeof(recv_result));
    if (ret > 0) {
        ret = pbuf_copy_partial(recv_result.p, buf, len, 0);
        if (ipaddr) {
            ip_addr_copy(*ipaddr, recv_result.ipaddr);
        }
        if (port) {
            *port = recv_result.port;
        }
        pbuf_free(recv_result.p);
    }
    return ret;
}
__attribute__((visibility("hidden")))
void socket_dgram_cleanup(struct socket *socket, struct pbuf *p, u16_t offset, u16_t len) {
    struct socket_dgram_recv recv_result;
    while (p != NULL && len >= sizeof(recv_result)) {
        u16_t br = pbuf_copy_partial(p, &recv_result, sizeof(recv_result), offset);
        assert(br == sizeof(recv_result));
        p = pbuf_skip(p, offset + br, &offset);
        len -= br;
        pbuf_free(recv_result.p);
    }
}