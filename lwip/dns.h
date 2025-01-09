// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "./socket.h"


struct socket_dns_arg {
    struct socket *socket;
    ip_addr_t ipaddr;
    err_t err;
    u8_t addrtype;
    char hostname[];
};

int socket_dns(void);
