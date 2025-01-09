// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <inttypes.h>
#include <sys/socket.h>

#include "lwip/inet.h"

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

#define IPPROTO_IP 0                    // Internet protocol.
#define IPPROTO_ICMP 1                  // Control message protocol.
#define IPPROTO_TCP 6                   // Transmission control protocol.
#define IPPROTO_UDP 17                  // User datagram protocol.
#define IPPROTO_IPV6 41                 // Internet Protocol Version 6.
#define IPPROTO_ICMPV6 58
#define IPPROTO_UDPLITE 136
#define IPPROTO_RAW 255                 // Raw IP Packets Protocol.


#if LWIP_IPV4
struct sockaddr_in {
  sa_family_t sin_family;               // AF_INET.
  in_port_t sin_port;                   // Port number.
  struct in_addr sin_addr;              // IP address.

};

static_assert(sizeof(struct sockaddr_in) <= sizeof(struct sockaddr_storage));
#endif

#if LWIP_IPV6
struct sockaddr_in6 {
    sa_family_t sin6_family;            // AF_INET6.
    in_port_t sin6_port;                // Port number.
    uint32_t sin6_flowinfo;             // IPv6 traffic class and flow information.
    struct in6_addr sin6_addr;          // IPv6 address.
    uint32_t sin6_scope_id;             // Set of interfaces for a scope.
};

static_assert(sizeof(struct sockaddr_in6) <= sizeof(struct sockaddr_storage));
#endif
