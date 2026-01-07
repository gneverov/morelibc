// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "lwip/tcp.h"

#include "morelib/lwip/socket.h"


static void socket_tcp_lwip_err(void *arg, err_t err) {
    // printf("tcp_err: err=%i\n", (int)err);
    struct socket_lwip *socket = arg;
    socket->pcb.tcp = NULL;
    socket_lock(&socket->base);
    socket->errcode = (socket->connected || socket->listening) ? err_to_errno(err) : ECONNREFUSED;
    socket->peer_closed = 1;
    socket_notify(&socket->base, 0, POLLERR);
    socket_unlock(&socket->base);
}

static const struct socket_vtable socket_tcp_vtable;

static err_t socket_tcp_lwip_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);

static err_t socket_tcp_lwip_sent(void *arg, struct tcp_pcb *pcb, u16_t len);

struct socket *socket_tcp_socket(int domain, int type, int protocol) {
    u8_t iptype;
    if (socket_domain_to_lwip(domain, &iptype) < 0) {
        return NULL;
    }
    if ((protocol != 0) && (protocol != IPPROTO_TCP)) {
        errno = EPROTONOSUPPORT;
        return NULL;
    }

    struct socket_lwip *socket = socket_lwip_alloc(&socket_tcp_vtable, domain, type, protocol);
    if (!socket) {
        return NULL;
    }
    LOCK_TCPIP_CORE();
    struct tcp_pcb *pcb = tcp_new_ip_type(iptype);
    if (!pcb) {
        errno = ENOMEM;
        goto exit;
    }

    socket->pcb.tcp = pcb;
    tcp_arg(pcb, socket);
    tcp_err(pcb, socket_tcp_lwip_err);
    tcp_recv(pcb, socket_tcp_lwip_recv);
    tcp_sent(pcb, socket_tcp_lwip_sent);

exit:
    UNLOCK_TCPIP_CORE();
    if (!pcb) {
        socket_release(&socket->base);
        socket = NULL;
    }    
    return &socket->base;
}

struct socket_tcp_accept_result {
    err_t err;
    uint events;
    struct tcp_pcb *new_pcb;
    ip_addr_t addr;
    u16_t port;
};

static void socket_tcp_lwip_new_accept(struct socket_tcp_accept_result *accept_result, struct socket_lwip *new_socket) {
    struct tcp_pcb *new_pcb = accept_result->new_pcb;
    LOCK_TCPIP_CORE();
    if (new_socket) {
        if (new_pcb) {
            new_socket->pcb.tcp = new_pcb;
            tcp_arg(new_pcb, new_socket);
            tcp_err(new_pcb, socket_tcp_lwip_err);
            tcp_recv(new_pcb, socket_tcp_lwip_recv);
            tcp_sent(new_pcb, socket_tcp_lwip_sent);
            tcp_backlog_accepted(new_pcb);
        }
        new_socket->errcode = err_to_errno(accept_result->err);
        new_socket->connected = 1;
        new_socket->peer_closed = (accept_result->events & POLLHUP) ? 1 : 0;
        socket_notify(&new_socket->base, 0, accept_result->events);
    }
    else if (new_pcb) {
        tcp_abort(new_pcb);
    }
    UNLOCK_TCPIP_CORE();
    free(accept_result);
}

static struct socket *socket_tcp_accept(void *ctx, struct sockaddr *address, socklen_t *address_len) {
    struct socket_lwip *socket = ctx;
    if (!socket->listening) {
        errno = EINVAL;
        return NULL;
    }

    struct socket_tcp_accept_result *accept_result;
    if (socket_lwip_pop(socket, &accept_result, sizeof(accept_result)) < 0) {
        return NULL;
    } 

    struct socket_lwip *new_socket = socket_lwip_alloc(socket->base.func, socket->base.domain, socket->base.type, socket->base.protocol);
    if (address) {
        socket_sockaddr_from_lwip(address, address_len, &accept_result->addr, accept_result->port);
    }
    socket_tcp_lwip_new_accept(accept_result, new_socket);
    new_socket->timeout = socket->timeout;
    return &new_socket->base;
}

static int socket_tcp_bind(void *ctx, const struct sockaddr *address, socklen_t address_len) {
    struct socket_lwip *socket = ctx;
    ip_addr_t ipaddr;
    u16_t port;
    socket_sockaddr_to_lwip(address, address_len, &ipaddr, &port);

    int ret = -1;
    LOCK_TCPIP_CORE();
    if (socket->pcb.tcp) {
        err_t err = tcp_bind(socket->pcb.tcp, &ipaddr, port);
        ret = socket_lwip_check_ret(err);
    }
    else {
        errno = socket->errcode;
    }
    UNLOCK_TCPIP_CORE();    
    return ret;
}

static int socket_tcp_close(void *ctx) {
    struct socket_lwip *socket = ctx;
    err_t err = ERR_OK;
    LOCK_TCPIP_CORE();
    struct tcp_pcb *pcb = socket->pcb.tcp;
    if (pcb) {
        if (pcb->state != LISTEN) {
            tcp_arg(pcb, NULL);
            tcp_err(pcb, NULL);
            tcp_accept(pcb, NULL);
            tcp_recv(pcb, NULL);
            tcp_sent(pcb, NULL);
        }
        err = tcp_close(pcb);
        socket->pcb.tcp = NULL;
    }
    UNLOCK_TCPIP_CORE();

    if (socket->rx_data) {
        if (socket->base.listening) {
            size_t offset = socket->rx_offset;
            while (offset < socket->rx_len) {
                struct socket_tcp_accept_result *result;
                pbuf_copy_partial(socket->rx_data, &result, sizeof(result), offset);
                socket_tcp_lwip_new_accept(result, NULL);
                offset += sizeof(result);
            }
        }
        pbuf_free(socket->rx_data);
    }
    return socket_lwip_check_ret(err);
}

static err_t socket_tcp_lwip_connected(void *arg, struct tcp_pcb *pcb, err_t err) {
    // err is always ERR_OK, failure returned through tcp_err
    struct socket_lwip *socket = arg;
    socket_lock(&socket->base);
    socket->connected = 1;
    socket_notify(&socket->base, 0, POLLOUT | POLLWRNORM);
    socket_unlock(&socket->base);
    return ERR_OK;
}

static int socket_tcp_connect(void *ctx, const struct sockaddr *address, socklen_t address_len) {
    struct socket_lwip *socket = ctx;
    ip_addr_t ipaddr;
    u16_t port;
    socket_sockaddr_to_lwip(address, address_len, &ipaddr, &port);
    
    int ret = -1;
    LOCK_TCPIP_CORE();
    if (!socket->pcb.tcp) {
        errno = socket->errcode;
    }
    else if (socket->pcb.tcp->state == SYN_SENT) {
        errno = EINPROGRESS;
    }    
    else {
        err_t err = tcp_connect(socket->pcb.tcp, &ipaddr, port, socket_tcp_lwip_connected);
        ret = socket_lwip_check_ret(err);
    }
    UNLOCK_TCPIP_CORE();
    if (ret < 0) {
        return ret;
    }

    TickType_t xTicksToWait = socket->timeout;
    do {
        ret = -1;        
        LOCK_TCPIP_CORE();
        if (!socket->pcb.tcp) {
            errno = socket->errcode;
        }
        else if (socket->connected) {
            ret = 0;
        }
        else {
            errno = EAGAIN;
        }
        UNLOCK_TCPIP_CORE();
    }
    while (POLL_SOCKET_CHECK(ret, &socket->base, POLLIN | POLLOUT, &xTicksToWait));
    return ret;
}

static void socket_tcp_lwip_err_unaccepted(void *arg, err_t err) {
    // printf("tcp_err_unaccepted: err=%i\n", (int)err);
    struct socket_tcp_accept_result *accept_result = arg;
    accept_result->err = err;
    accept_result->events = POLLERR;
    accept_result->new_pcb = NULL;
}

static err_t socket_tcp_lwip_recv_unaccepted(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    struct socket_tcp_accept_result *accept_result = arg;
    if (p) {
        accept_result->events |= POLLIN | POLLRDNORM;
    }
    else {
        accept_result->events |= POLLHUP;
    }
    return ERR_BUF;
}

static err_t socket_tcp_lwip_accept(void *arg, struct tcp_pcb *new_pcb, err_t err) {
    // printf("tcp_accept: local=%s:%hu", ipaddr_ntoa(&new_pcb->local_ip), new_pcb->local_port);
    // printf(", remote=%s:%hu, err=%i\n", ipaddr_ntoa(&new_pcb->remote_ip), new_pcb->remote_port, (int)err);
    struct socket_lwip *socket = arg;

    struct socket_tcp_accept_result *accept_result = calloc(1, sizeof(struct socket_tcp_accept_result));
    if (!accept_result) {
        tcp_abort(new_pcb);
        return ERR_ABRT;
    }
    accept_result->err = err;
    accept_result->new_pcb = new_pcb;
    accept_result->addr = new_pcb->remote_ip;
    accept_result->port = new_pcb->remote_port;
    accept_result->events = POLLOUT | POLLWRNORM;

    tcp_arg(new_pcb, accept_result);
    tcp_err(new_pcb, socket_tcp_lwip_err_unaccepted);
    tcp_recv(new_pcb, socket_tcp_lwip_recv_unaccepted);
    tcp_backlog_delayed(new_pcb);

    socket_lock(&socket->base);
    if (socket_lwip_push(socket, &accept_result, sizeof(accept_result)) < 0) {
        socket_unlock(&socket->base);
        tcp_abort(new_pcb);
        free(accept_result);
        return ERR_ABRT;
    }
    socket_unlock(&socket->base);
    return ERR_OK;
}

static int socket_tcp_listen(void *ctx, int backlog) {
    struct socket_lwip *socket = ctx;
    int ret = -1;
    LOCK_TCPIP_CORE();
    if (!socket->pcb.tcp) {
        errno = socket->errcode;
        goto exit;
    }
    err_t err = ERR_OK;
    struct tcp_pcb *new_pcb = tcp_listen_with_backlog_and_err(socket->pcb.tcp, MIN((uint)backlog, 255), &err);
    if (new_pcb) {
        tcp_accept(new_pcb, socket_tcp_lwip_accept);       
        socket->pcb.tcp = new_pcb;
        socket->listening = 1;
        ret = 0;
    }
    else {
        ret = socket_lwip_check_ret(err);
    }

exit:
    UNLOCK_TCPIP_CORE();
    return ret;
}

static err_t socket_tcp_lwip_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    struct socket_lwip *socket = arg;
    socket_lock(&socket->base);
    if (!p) {
        socket->peer_closed = 1;
        socket_notify(&socket->base, 0, POLLHUP);    
    }
    else if (socket_lwip_push_pbuf(socket, p) < 0) {
        pbuf_free(p);
    }
    socket_unlock(&socket->base);
    return ERR_OK;
}

static int socket_tcp_recvfrom(void *ctx, void *buf, size_t len, struct sockaddr *address, socklen_t *address_len) {
    struct socket_lwip *socket = ctx;
    if (!socket->connected) {
        errno = ENOTCONN;
        return -1;
    }
    int ret = len ? socket_lwip_pop(socket, buf, len) : 0;
     if (ret > 0) {
        LOCK_TCPIP_CORE();
        tcp_recved(socket->pcb.tcp, ret);
        UNLOCK_TCPIP_CORE();
    }
    return ret;
}

static err_t socket_tcp_lwip_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    // printf("tcp_sent: local=%s:%hu", ipaddr_ntoa(&pcb->local_ip), pcb->local_port);
    // printf(", remote=%s:%hu, len=%hu\n", ipaddr_ntoa(&pcb->remote_ip), pcb->remote_port, len);
    struct socket_lwip *socket = arg;
    if (tcp_sndbuf(socket->pcb.tcp) >= 536) {
        socket_lock(&socket->base);
        socket_notify(&socket->base, 0, POLLOUT | POLLWRNORM);
        socket_unlock(&socket->base);
    }
    return ERR_OK;
}

static int socket_tcp_sendto(void *ctx, const void *buf, size_t len, const struct sockaddr *address, socklen_t address_len) {
    struct socket_lwip *socket = ctx;
    if (address != NULL) {
        errno = EINVAL;
        return -1;
    }

    TickType_t xTicksToWait = socket->timeout;
    int ret;
    do {
        ret = -1;
        LOCK_TCPIP_CORE();
        if (!socket->pcb.tcp) {
            errno = socket->errcode;
        }
        else if (!tcp_sndbuf(socket->pcb.tcp)) {
            errno = EAGAIN;
        }
        else {
            u16_t lwip_len = LWIP_MIN(len, tcp_sndbuf(socket->pcb.tcp));
            u8_t apiflags = TCP_WRITE_FLAG_COPY | (lwip_len < len ? TCP_WRITE_FLAG_MORE : 0);
            err_t err = tcp_write(socket->pcb.tcp, buf, lwip_len, apiflags);
            ret = (err == ERR_OK) ? lwip_len : socket_lwip_check_ret(err);
            if (tcp_sndbuf(socket->pcb.tcp) == 0) {
                socket_notify(&socket->base, POLLOUT | POLLWRNORM, 0);
            }
        }
        UNLOCK_TCPIP_CORE();
    }
    while (POLL_SOCKET_CHECK(ret, &socket->base, POLLOUT, &xTicksToWait));
    return ret;
}

static int socket_tcp_shutdown(void *ctx, int how) {
    struct socket_lwip *socket = ctx;
    bool shut_rx = (how == SHUT_RD) || (how == SHUT_RDWR);
    bool shut_tx = (how == SHUT_WR) || (how == SHUT_RDWR);

    int ret = 0;
    LOCK_TCPIP_CORE();
    if (!socket->pcb.tcp) {
        errno = socket->errcode;
        ret = -1;
        goto exit;
    }

    // We don't want the socket shutdown call to free the PCB, only socket close should do that, 
    // but it is unclear if tcp_shutdown is going to free the PCB or not. Definitely calling 
    // tcp_shutdown with shut_rx and shut_tx both set to 1 will free the PCB, but maybe if we call
    // it twice, with one flag set at a time, it will not freed?
    if (shut_rx) {
        err_t err = err = tcp_shutdown(socket->pcb.tcp, 1, 0);
        if (err == ERR_OK) {
            tcp_recved(socket->pcb.tcp, socket->rx_len);

            socket_lock(&socket->base);
            if (socket->rx_data) {
                pbuf_free(socket->rx_data);
            }
            socket->rx_data = NULL;
            socket->rx_len = 0;
            socket_notify(&socket->base, POLLIN | POLLRDNORM, 0);
            socket_unlock(&socket->base);
        }
        ret = socket_lwip_check_ret(err);
    }
    if ((ret >= 0) && shut_tx) {
        err_t err = err = tcp_shutdown(socket->pcb.tcp, 0, 1);
        if (err == ERR_OK) {
            socket_lock(&socket->base);
            socket_notify(&socket->base, POLLOUT | POLLWRNORM, 0);
            socket_unlock(&socket->base);
        }
        ret = socket_lwip_check_ret(err);
    }

exit:    
    UNLOCK_TCPIP_CORE();
    return ret;
}

static int socket_tcp_getpeername(void *ctx, struct sockaddr *address, socklen_t *address_len) {
    struct socket_lwip *socket = ctx;
    return socket_lwip_getpeername(socket, address, address_len, offsetof(struct tcp_pcb, remote_port));
}

static int socket_tcp_getsockname(void *ctx, struct sockaddr *address, socklen_t *address_len) {
    struct socket_lwip *socket = ctx;
    return socket_lwip_getsockname(socket, address, address_len, offsetof(struct tcp_pcb, local_port));    
}

static int socket_tcp_getsockopt(void *ctx, int level, int option_name, void *option_value, socklen_t *option_len) {
    if (level != IPPROTO_TCP) {
        return socket_lwip_getsockopt(ctx, level, option_name, option_value, option_len);
    }
    struct socket_lwip *socket = ctx;
    int ret = 0;
    LOCK_TCPIP_CORE();
    if (!socket->pcb.tcp) {
        errno = EINVAL;
        ret = -1;
        goto exit;
    }
    switch (option_name) {
        case TCP_NODELAY: {
            int value = tcp_nagle_disabled(socket->pcb.tcp);
            socket_getintopt(option_value, option_len, value);
            break;
        }
        default:
            errno = ENOPROTOOPT;
            ret = -1;
            break;
    }

    exit:
    UNLOCK_TCPIP_CORE();
    return ret;
}

static int socket_tcp_setsockopt(void *ctx, int level, int option_name, const void *option_value, socklen_t option_len) {
    if (level != IPPROTO_TCP) {
        return socket_lwip_setsockopt(ctx, level, option_name, option_value, option_len);
    }
    struct socket_lwip *socket = ctx;
    int ret = 0;
    LOCK_TCPIP_CORE();
    if (!socket->pcb.tcp) {
        errno = EINVAL;
        ret = -1;
        goto exit;
    }
    switch (option_name) {
        case TCP_NODELAY: {
            int value;
            ret = socket_setintopt(option_value, option_len, &value);
            if (ret >= 0) {
                if (value) {
                    tcp_nagle_enable(socket->pcb.tcp);
                } else {
                    tcp_nagle_disable(socket->pcb.tcp);
                }
            }
            break;
        }
        default:
            errno = ENOPROTOOPT;
            ret = -1;
            break;
    }

    exit:
    UNLOCK_TCPIP_CORE();
    return ret;
}

static const struct socket_vtable socket_tcp_vtable = {
    .accept = socket_tcp_accept,
    .bind = socket_tcp_bind,
    .close = socket_tcp_close,
    .connect = socket_tcp_connect,
    .getpeername = socket_tcp_getpeername,
    .getsockname = socket_tcp_getsockname,
    .getsockopt = socket_tcp_getsockopt,
    .listen = socket_tcp_listen,
    .recvfrom = socket_tcp_recvfrom,
    .sendto = socket_tcp_sendto,
    .setsockopt = socket_tcp_setsockopt,
    .shutdown = socket_tcp_shutdown,
};