// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "morelib/lwip/socket.h"


struct socket_dns_arg {
    struct socket_lwip *socket;
    ip_addr_t ipaddr;
    err_t err;
    u8_t addrtype;
    char hostname[];
};

struct socket *socket_dns(void);
