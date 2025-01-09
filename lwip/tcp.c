// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include "morelib/poll.h"

#include "lwip/tcp.h"

#include "./socket.h"


static struct tcp_pcb *socket_tcp_lwip_free(struct socket *socket) {
    struct tcp_pcb *pcb = socket->pcb.tcp;
    socket->pcb.tcp = NULL;
    if (pcb != NULL && (pcb->state != LISTEN)) {
        tcp_arg(pcb, NULL);
        tcp_err(pcb, NULL);
        tcp_accept(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_sent(pcb, NULL);
    }
    return pcb;
}

static void socket_tcp_lwip_err(void *arg, err_t err) {
    // printf("tcp_err: err=%i\n", (int)err);
    struct socket *socket = arg;
    socket->pcb.tcp = NULL;
    socket_lock(socket);
    socket->errcode = err_to_errno(err);
    poll_file_notify(&socket->base, 0, POLLERR);
    socket_unlock(socket);
}

static err_t socket_tcp_lwip_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);

static err_t socket_tcp_lwip_sent(void *arg, struct tcp_pcb *pcb, u16_t len);

static err_t socket_tcp_lwip_new(struct socket *socket, u8_t iptype, u8_t proto) {
    if (socket->pcb.tcp) {
        return ERR_VAL;
    }
    
    struct tcp_pcb *pcb = tcp_new_ip_type(iptype);
    if (!pcb) {
        return ERR_MEM;
    }

    socket->pcb.tcp = pcb;
    tcp_arg(pcb, socket);
    tcp_err(pcb, socket_tcp_lwip_err);
    tcp_recv(pcb, socket_tcp_lwip_recv);
    tcp_sent(pcb, socket_tcp_lwip_sent);
    return ERR_OK;
}

static err_t socket_tcp_lwip_close(struct socket *socket) {
    err_t err = ERR_OK;
    if (socket->pcb.tcp) {
        struct tcp_pcb *pcb = socket_tcp_lwip_free(socket);
        err = tcp_close(pcb);
    }
    return err;
}

static err_t socket_tcp_lwip_abort(struct socket *socket) {
    if (socket->pcb.tcp) {
        struct tcp_pcb *pcb = socket_tcp_lwip_free(socket);
        if (socket->listening) {
            tcp_close(pcb);
        }
        else {
            tcp_abort(pcb);
        }
    }
    return ERR_OK;
}

static err_t socket_tcp_lwip_bind(struct socket *socket, const ip_addr_t *ipaddr, u16_t port) {
    if (!socket->pcb.tcp) {
        return ERR_ARG;
    }    
    err_t err = tcp_bind(socket->pcb.tcp, ipaddr, port);
    if (err == ERR_OK) {
        socket_lock(socket);
        socket->local_len = socket_sockaddr_storage_from_lwip(&socket->local, &socket->pcb.tcp->local_ip, socket->pcb.tcp->local_port);
        socket_unlock(socket);
    }
    return err;
}

struct socket_tcp_accept_result {
    err_t err;
    struct tcp_pcb *new_pcb;
    uint8_t local_len;
    uint8_t remote_len;
    struct sockaddr_storage local;
    struct sockaddr_storage remote;
};

static void socket_tcp_lwip_err_unaccepted(void *arg, err_t err) {
    // printf("tcp_err_unaccepted: err=%i\n", (int)err);
    struct pbuf *accept_arg = arg;
    struct socket_tcp_accept_result *accept_result = accept_arg->payload;
    accept_result->err = err;
    accept_result->new_pcb = NULL;
}

static err_t socket_tcp_lwip_accept(void *arg, struct tcp_pcb *new_pcb, err_t err) {
    // printf("tcp_accept: local=%s:%hu", ipaddr_ntoa(&new_pcb->local_ip), new_pcb->local_port);
    // printf(", remote=%s:%hu, err=%i\n", ipaddr_ntoa(&new_pcb->remote_ip), new_pcb->remote_port, (int)err);
    struct socket *socket = arg;

    struct pbuf *accept_arg = pbuf_alloc(PBUF_RAW, sizeof(struct socket_tcp_accept_result), PBUF_POOL);
    if (!accept_arg) {
        tcp_abort(new_pcb);
        return ERR_ABRT;
    }

    struct socket_tcp_accept_result *accept_result = accept_arg->payload;
    accept_result->err = ERR_OK;
    accept_result->new_pcb = new_pcb;
    accept_result->local_len = socket_sockaddr_storage_from_lwip(&accept_result->local, &new_pcb->local_ip, new_pcb->local_port);
    accept_result->remote_len = socket_sockaddr_storage_from_lwip(&accept_result->remote, &new_pcb->remote_ip, new_pcb->remote_port);

    tcp_arg(new_pcb, accept_arg);
    tcp_err(new_pcb, socket_tcp_lwip_err_unaccepted);
    tcp_backlog_delayed(new_pcb);

    if (socket_push(socket, &accept_arg, sizeof(accept_arg)) < 0) {
        tcp_abort(new_pcb);
        pbuf_free(accept_arg);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t socket_tcp_lwip_listen(struct socket *socket, u8_t backlog) {
    if (!socket->pcb.tcp) {
        return ERR_ARG;
    }
    err_t err = ERR_OK;
    struct tcp_pcb *new_pcb = tcp_listen_with_backlog_and_err(socket->pcb.tcp, backlog, &err);
    if (new_pcb) {
        tcp_accept(new_pcb, socket_tcp_lwip_accept);       
        socket->pcb.tcp = new_pcb;
    }
    return err;
}

static err_t socket_tcp_lwip_connected(void *arg, struct tcp_pcb *pcb, err_t err) {
    // err is always ERR_OK, failure returned through tcp_err
    struct socket *socket = arg;
    socket_lock(socket);
    socket->connected = 1;
    socket->local_len = socket_sockaddr_storage_from_lwip(&socket->local, &pcb->local_ip, pcb->local_port);
    socket->remote_len = socket_sockaddr_storage_from_lwip(&socket->remote, &pcb->remote_ip, pcb->remote_port);
    poll_file_notify(&socket->base, 0, POLLIN | POLLOUT);
    socket_unlock(socket);
    return ERR_OK;
}

static err_t socket_tcp_lwip_connect(struct socket *socket, const ip_addr_t *ipaddr, u16_t port) {
    if (!socket->pcb.tcp) {
        return ERR_ARG;
    }      
    return tcp_connect(socket->pcb.tcp, ipaddr, port, socket_tcp_lwip_connected);
}

static err_t socket_tcp_lwip_recved(struct socket *socket, u16_t len) {
    if (socket->pcb.tcp) {
        tcp_recved(socket->pcb.tcp, len);
    }
    return ERR_OK;
}

static err_t socket_tcp_lwip_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    struct socket *socket = arg;
    socket_lock(socket);
    if (p) {
        socket_push_pbuf(socket, p);
    }
    else {
        socket->peer_closed = 1;
        poll_file_notify(&socket->base, 0, POLLHUP);    
    }
    socket_unlock(socket);
    return ERR_OK;
}

static err_t socket_tcp_lwip_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    // printf("tcp_sent: local=%s:%hu", ipaddr_ntoa(&pcb->local_ip), pcb->local_port);
    // printf(", remote=%s:%hu, len=%hu\n", ipaddr_ntoa(&pcb->remote_ip), pcb->remote_port, len);
    struct socket *socket = arg;
    if (tcp_sndbuf(socket->pcb.tcp) >= 536) {
        poll_file_notify(&socket->base, 0, POLLOUT | POLLWRNORM);
    }
    return ERR_OK;
}

static err_t socket_tcp_lwip_sendto(struct socket *socket, const void *buf, size_t len, const ip_addr_t *ipaddr, u16_t port) {
    if (!socket->pcb.tcp) {
        return ERR_ARG;
    }      
    u16_t lwip_len = LWIP_MIN(len, tcp_sndbuf(socket->pcb.tcp));
    if (ipaddr != NULL) {
        return ERR_VAL;
    }
    u8_t apiflags = TCP_WRITE_FLAG_COPY | (lwip_len < len ? TCP_WRITE_FLAG_MORE : 0);
    err_t err = tcp_write(socket->pcb.tcp, buf, lwip_len, apiflags);
    if (tcp_sndbuf(socket->pcb.tcp) == 0) {
        poll_file_notify(&socket->base, POLLOUT | POLLWRNORM, 0);
    }
    return (err == ERR_OK) ? lwip_len : err;
}

static err_t socket_tcp_lwip_shutdown(struct socket *socket, int shut_rx, int shut_tx) {
    if (!socket->pcb.tcp) {
        return ERR_ARG;
    }      
    err_t err = ERR_OK;
    // We don't want the socket shutdown call to free the PCB, only socket close should do that, 
    // but it is unclear if tcp_shutdown is going to free the PCB or not. Definitely calling 
    // tcp_shutdown with shut_rx and shut_tx both set to 1 will free the PCB, but maybe if we call
    // it twice, with one flag set at a time, it will not freed?
    if (shut_rx) {
        err = tcp_shutdown(socket->pcb.tcp, 1, 0);
        if (err == ERR_OK) {
            if (socket->rx_data) {
                pbuf_free(socket->rx_data);
            }
            socket->rx_data = NULL;
            poll_file_notify(&socket->base, POLLIN | POLLRDNORM, 0);
        }
    }
    if ((err == ERR_OK) && shut_tx) {
        err = tcp_shutdown(socket->pcb.tcp, 0, 1);
        if (err == ERR_OK) {
            poll_file_notify(&socket->base, POLLOUT | POLLWRNORM, 0);
        }
    }
    return err;
}

static err_t socket_tcp_lwip_output(struct socket *socket) {
    if (!socket->pcb.tcp) {
        return ERR_ARG;
    }
    return tcp_output(socket->pcb.tcp);
}

static err_t socket_tcp_lwip_new_accept(struct pbuf *accept_arg, struct socket *new_socket) {
    struct socket_tcp_accept_result *accept_result = accept_arg->payload;
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
        new_socket->local_len = accept_result->local_len;
        new_socket->remote_len = accept_result->remote_len;
        new_socket->local = accept_result->local;
        new_socket->remote = accept_result->remote;
    }
    else if (new_pcb) {
        tcp_abort(new_pcb);
    }
    UNLOCK_TCPIP_CORE();
    pbuf_free(accept_arg);
    return ERR_OK;
}

static int socket_tcp_accept(struct socket *socket, struct socket **new_socket) {
    if (!socket->listening) {
        errno = EINVAL;
        return -1;
    } 

    struct pbuf *accept_arg;
    if (socket_pop(socket, &accept_arg, sizeof(accept_arg)) < 0) {
        return -1;
    }
    *new_socket = socket_alloc(socket->func, socket->domain, socket->type, socket->protocol);
    return socket_check_ret(socket_tcp_lwip_new_accept( accept_arg, *new_socket));
}

static int socket_tcp_recvfrom(struct socket *socket, void *buf, size_t len, ip_addr_t *ipaddr, u16_t *port) {
    if (ipaddr != NULL) {
        errno = EINVAL;
        return -1;
    }
    
    socket_lock(socket);
    int connected = socket->connected && !socket->listening;
    socket_unlock(socket);
    if (!connected) {
        errno = ENOTCONN;
        return -1;
    }        

    int ret = len ? socket_pop(socket, buf, len) : 0;
    if (ret > 0) {
        LOCK_TCPIP_CORE();
        socket_tcp_lwip_recved(socket, ret);
        UNLOCK_TCPIP_CORE();
    }
    return ret;
}

static void socket_tcp_cleanup(struct socket *socket, struct pbuf *p, u16_t offset, u16_t len) {
    if (!socket->listening) {
        return;
    }

    struct pbuf *accept_arg;
    while (p != NULL && len >= sizeof(accept_arg)) {
        u16_t br = pbuf_copy_partial(p, &accept_arg, sizeof(accept_arg), offset);
        assert(br == sizeof(accept_arg));
        p = pbuf_skip(p, offset + br, &offset);
        len -= br;

        socket_tcp_lwip_new_accept(accept_arg, NULL);
    }
}

__attribute__((visibility("hidden")))
const struct socket_vtable socket_tcp_vtable = {
    .pcb_type = LWIP_PCB_TCP,
    .lwip_new = socket_tcp_lwip_new,
    .lwip_close = socket_tcp_lwip_close,
    .lwip_abort = socket_tcp_lwip_abort,
    .lwip_bind = socket_tcp_lwip_bind,
    .lwip_listen = socket_tcp_lwip_listen,
    .lwip_connect = socket_tcp_lwip_connect,
    .lwip_sendto = socket_tcp_lwip_sendto,
    .lwip_shutdown = socket_tcp_lwip_shutdown,
    .lwip_output = socket_tcp_lwip_output,
    
    .socket_accept = socket_tcp_accept,
    .socket_recvfrom = socket_tcp_recvfrom,
    .socket_cleanup = socket_tcp_cleanup,
};