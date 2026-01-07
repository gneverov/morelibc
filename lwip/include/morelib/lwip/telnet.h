// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <sys/socket.h>

#include "lwip/tcp.h"


typedef void (*telnet_accept_fn)(void *arg, int fd);

struct telnet_server {
    struct tcp_pcb *pcb;
    telnet_accept_fn accept_fn;
    void *accept_arg;
};

int telnet_server_init(struct telnet_server *server, struct sockaddr *address, socklen_t address_len, telnet_accept_fn accept_fn, void *accept_arg);
void telnet_server_deinit(struct telnet_server *server);
