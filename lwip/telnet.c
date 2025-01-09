// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include "morelib/poll.h"
#include "morelib/telnet.h"
#include "morelib/termios.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include "lwip/tcpip.h"

#include "./socket.h"


enum {
    RX_STATE_NONE,
    RX_STATE_IAC,
    RX_STATE_SKIP,
    RX_STATE_CR,
};

typedef struct {
    struct poll_file base;
    SemaphoreHandle_t mutex;
    struct tcp_pcb *pcb;
    int rx_state : 3;
    int peer_closed : 1;
    int errcode;
    struct pbuf *rx_data;
    uint16_t rx_offset;
    uint16_t rx_len;    
    struct termios termios;
    StaticSemaphore_t xMutexBuffer;
} term_net_t;

static term_net_t *term_net_open(const struct telnet_server *server, struct tcp_pcb *pcb, mode_t mode);

static void telnet_server_err(void *arg, err_t err) {
    struct telnet_server *server = arg;
    server->pcb = NULL;
    errno = err_to_errno(err);
    perror("telnet server died");
}

static err_t telnet_server_accept(void *arg, struct tcp_pcb *new_pcb, err_t err) {
    struct telnet_server *server = arg;
    term_net_t *file = term_net_open(server, new_pcb, 0);
    if (!file) {
        tcp_abort(new_pcb);
        return ERR_ABRT;
    }

    int fd = poll_file_fd(&file->base, FREAD | FWRITE);
    if (fd >= 0) {
        server->accept_fn(server->accept_arg, fd);
    }
    poll_file_release(&file->base);
    return ERR_OK;
}

int telnet_server_init(struct telnet_server *server, struct sockaddr *address, socklen_t address_len, telnet_accept_fn accept_fn, void *accept_arg) {
    int ret = -1;
    struct tcp_pcb *pcb = NULL;
    LOCK_TCPIP_CORE();
    switch (address->sa_family) {
        #if LWIP_IPV4
        case AF_INET: {
            pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
            break;
        }
        #endif
        #if LWIP_IPV6
        case AF_INET6: {
            pcb = tcp_new_ip_type(IPADDR_TYPE_V6);
            break;
        }
        #endif
        default: {
            errno = EAFNOSUPPORT;
            goto exit;
        }
    }
    if (!pcb) {
        errno = ENOMEM;
        goto exit;
    }
    server->pcb = pcb;
    server->accept_fn = accept_fn;
    server->accept_arg = accept_arg;
    tcp_arg(pcb, server);
    tcp_err(pcb, telnet_server_err);

    ip_addr_t ipaddr;
    u16_t port;
    socket_sockaddr_to_lwip(address, address_len, &ipaddr, &port);
    ret = socket_check_ret(tcp_bind(pcb, &ipaddr, port));
    if (ret < 0) {
        goto exit;
    }
    
    ret = -1;
    struct tcp_pcb *new_pcb = tcp_listen(pcb);
    if (!new_pcb) {
        errno = ENOMEM;
        goto exit;
    }
    server->pcb = pcb = new_pcb;
    new_pcb = NULL;

    tcp_accept(pcb, telnet_server_accept);
    pcb = NULL;
    ret = 0;

exit:
    if (pcb) {
        tcp_abort(pcb);
    }
    UNLOCK_TCPIP_CORE();
    return ret;
}

void telnet_server_deinit(struct telnet_server *server) {
    LOCK_TCPIP_CORE();
    if (server->pcb) {
        tcp_arg(server->pcb, NULL);
        tcp_accept(server->pcb, NULL);
        tcp_close(server->pcb);
    }
    UNLOCK_TCPIP_CORE();
    server->pcb = NULL;
    server->accept_fn = NULL;
}

static inline bool term_net_empty(term_net_t *file) {
    return (file->rx_data == NULL) || (file->rx_len == 0);
}

static void term_net_err(void *arg, err_t err) {
    term_net_t *file = arg;
    xSemaphoreTake(file->mutex, portMAX_DELAY);
    file->pcb = NULL;
    file->errcode = err_to_errno(err);
    poll_file_notify(&file->base, 0, POLLERR);
    xSemaphoreGive(file->mutex);    
}

static err_t term_net_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    term_net_t *file = arg;
    xSemaphoreTake(file->mutex, portMAX_DELAY);
    assert(file->pcb == pcb);
    uint events;
    if (p) {
        file->rx_len += p->tot_len;
        file->rx_data = pbuf_concat(file->rx_data, p);
        if ((file->termios.c_lflag & ISIG) && (pbuf_strstr(p, "\003") != 0xffff)) {
            kill(0, SIGINT);
        }
        events = POLLIN | POLLRDNORM;
    }
    else {
        file->peer_closed = 1;
        events = POLLHUP;
    }
    poll_file_notify(&file->base, 0, events);    
    xSemaphoreGive(file->mutex);
    return ERR_OK;
}

static err_t term_net_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    term_net_t *file = arg;
    if (tcp_sndbuf(file->pcb) >= 536) {
        poll_file_notify(&file->base, 0, POLLOUT | POLLWRNORM);
    }
    return ERR_OK;
}

static void term_net_delayed_close(void *ctx) {
    struct tcp_pcb *pcb = ctx;
    printf("Telnet closed connection from %s.\n", ipaddr_ntoa(&pcb->remote_ip));
    tcp_close(pcb);
}

static int term_net_close(void *ctx) {
    term_net_t *file = ctx;
    if (file->pcb) {
        LOCK_TCPIP_CORE();
        tcp_shutdown(file->pcb, 1, 0);
        tcp_arg(file->pcb, NULL);
        tcp_err(file->pcb, NULL);
        tcp_recv(file->pcb, NULL);
        tcp_sent(file->pcb, NULL);
        if (tcpip_callback(term_net_delayed_close, file->pcb) != ERR_OK) {
            tcp_close(file->pcb);
        }
        UNLOCK_TCPIP_CORE();
    }
    if (file->rx_data) {
        pbuf_free(file->rx_data);
    }
    vSemaphoreDelete(file->mutex);
    free(file);
    return 0;
}

static int term_net_ioctl(void *ctx, unsigned long request, va_list args) {
    term_net_t *file = ctx;
    int ret = -1;
    xSemaphoreTake(file->mutex, portMAX_DELAY);
    switch (request) {
        case TCGETS: {
            struct termios *p = va_arg(args, struct termios *);
            *p = file->termios;
            ret = 0;
            break;
        }
        case TCSETS: {
            const struct termios *p = va_arg(args, const struct termios *);
            file->termios = *p;
            ret = 0;
            break;
        }
        default: {
            errno = EINVAL;
            break;
        }
    }
    xSemaphoreGive(file->mutex);
    return ret;
}

static int telnet_input(term_net_t *file, void *buffer, size_t size, size_t *total_br) {
    char *write = buffer;
    while (!term_net_empty(file)) {
        size_t recv_len = LWIP_MIN((char *)buffer + size - write, file->rx_len);
        size_t br = pbuf_copy_partial(file->rx_data, write, recv_len, file->rx_offset);
        file->rx_data = pbuf_advance(file->rx_data, &file->rx_offset, br);
        file->rx_len -= br;
        *total_br += br;
        const char *read = write;
        const char *end = read + br;
        
        while (read < end) {
            int ch = *read++;
            switch (file->rx_state) {
                case RX_STATE_NONE:
                case RX_STATE_CR: {
                    if (ch == 255) {
                        file->rx_state = RX_STATE_IAC;
                    }
                    else if ((ch == 0) && (file->rx_state == RX_STATE_CR)) {
                        file->rx_state = RX_STATE_NONE;
                    }
                    else {
                        if (ch == 13) {
                            file->rx_state = RX_STATE_CR;
                        }
                        *write++ = ch;
                    }
                    break;
                }
                case RX_STATE_IAC: {
                    switch (ch) {
                        case 255: {
                            *write++ = ch;
                            file->rx_state = RX_STATE_NONE;
                            break;
                        }
                        case 254:
                        case 253:
                        case 252:
                        case 251:
                        case 250:
                        case 240: {
                            file->rx_state = RX_STATE_SKIP;
                            break;
                        }
                        default: {
                            file->rx_state = RX_STATE_NONE;
                            break;
                        }
                    }
                    break;
                }
                case RX_STATE_SKIP: {
                    file->rx_state = RX_STATE_NONE;
                    break;
                }
            }
        }
    }    
    return write - (char *)buffer;
}

static int term_net_read(void *ctx, void *buffer, size_t size, int flags) {
    term_net_t *file = ctx;
    if (!size) {
        errno = EINVAL;
        return -1;
    }
    TickType_t xTicksToWait = portMAX_DELAY;
    int ret;
    do { 
        size_t total_br = 0;
        xSemaphoreTake(file->mutex, portMAX_DELAY);
        if (file->errcode) {
            errno = file->errcode;
            ret = -1;
        }
        else {
            ret = telnet_input(file, buffer, size, &total_br);
            if (!ret && !file->peer_closed) {
                errno = EAGAIN;
                ret = -1;
            }
            if (term_net_empty(file)) {
                poll_file_notify(&file->base, POLLIN | POLLRDNORM, 0);
            }
        }
        xSemaphoreGive(file->mutex); 

        if (total_br > 0) {
            LOCK_TCPIP_CORE();
            if (file->pcb) {
                tcp_recved(file->pcb, total_br);
            }
            UNLOCK_TCPIP_CORE();
        }
    }
    while (POLL_CHECK(flags, ret, &file->base, POLLIN, &xTicksToWait));    
    return ret;
}

static int telnet_output(term_net_t *file, const void *buffer, size_t size) {
    const void *buf = buffer;
    size_t remaining = size;
    err_t err = ERR_OK;
    LOCK_TCPIP_CORE();
    if (!file->pcb) {
        err = ERR_ARG;
        goto exit;
    }
    while (remaining > 0) {
        size_t send_len = MIN(remaining, tcp_sndbuf(file->pcb));
        if (send_len == 0) {
            break;
        }
        const void *ptr = memchr(buf, 255, send_len);
        send_len = ptr ? ptr - buf : send_len;
        err = tcp_write(file->pcb, buf, send_len, TCP_WRITE_FLAG_COPY | (send_len < remaining ? TCP_WRITE_FLAG_MORE : 0));
        if (err != ERR_OK) {
            break;
        }
        buf += send_len;
        remaining -= send_len;

        if (ptr) {            
            if (tcp_sndbuf(file->pcb) < 2) {
                break;
            }
            err = tcp_write(file->pcb, "\xff\xff", 2, (1 < remaining) ? TCP_WRITE_FLAG_MORE : 0);
            if (err != ERR_OK) {
                break;
            }
            buf += 1;
            remaining -= 1;
        }
    }

exit:
    UNLOCK_TCPIP_CORE();
    if (err != ERR_OK) {
        return socket_check_ret(err);
    }
    else if (remaining < size) {
        return size - remaining;
    }
    else {
        errno = EAGAIN;
        return -1;
    }
}

static int term_net_write(void *ctx, const void *buffer, size_t size, int flags) {
    term_net_t *file = ctx;
    if (!size) {
        errno = EINVAL;
        return -1;
    }    
    TickType_t xTicksToWait = portMAX_DELAY;
    int ret;
    do {
        ret = telnet_output(file, buffer, size);
        if ((ret < 0) && (errno == EAGAIN)) {
            poll_file_notify(&file->base, POLLOUT | POLLWRNORM, 0);
        }
    }
    while (POLL_CHECK(flags, ret, &file->base, POLLOUT, &xTicksToWait));
    return ret;
}

static const struct vfs_file_vtable term_net_vtable = {
    .close = term_net_close,
    .ioctl = term_net_ioctl,
    .isatty = 1,
    .pollable = 1,
    .read = term_net_read,
    .write = term_net_write,
};

static term_net_t *term_net_open(const struct telnet_server *server, struct tcp_pcb *pcb, mode_t mode) {
    term_net_t *file = calloc(1, sizeof(term_net_t));
    if (!file) {
        return NULL;
    }

    poll_file_init(&file->base, &term_net_vtable, mode | S_IFCHR, POLLIN | POLLOUT| POLLDRAIN);
    file->mutex = xSemaphoreCreateMutexStatic(&file->xMutexBuffer);
    file->pcb = pcb;
    termios_init(&file->termios, 0);

    tcp_arg(pcb, file);
    tcp_err(pcb, term_net_err);
    tcp_recv(pcb, term_net_recv);
    tcp_sent(pcb, term_net_sent);
    ip_set_option(pcb, SOF_KEEPALIVE);
    pcb->keep_idle = 60000;
    #if LWIP_TCP_KEEPALIVE
    pcb->keep_intvl = 60000;
    pcb->keep_cnt = 3;
    #endif

    printf("Telnet accepted connection from %s.\n", ipaddr_ntoa(&pcb->remote_ip));
    // IAC WILL ECHO
    // IAC DO SGA
    // IAC WILL SGA
    tcp_write(pcb, "\xff\xfb\x01\xff\xfd\x03\xff\xfb\x03", 9, 0);
    return file;
}
