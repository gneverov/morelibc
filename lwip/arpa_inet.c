// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <arpa/inet.h>
#include <errno.h>


const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
    const char *ret = NULL;
    switch (af) {
#if LWIP_IPV4
        case AF_INET:
            ret = ip4addr_ntoa_r((const ip4_addr_t *)src, dst, size);
            break;
#endif
#if LWIP_IPV6
        case AF_INET6:
            ret = ip6addr_ntoa_r((const ip6_addr_t *)src, dst, size);
            break;
#endif
        default:
            errno = EAFNOSUPPORT;
            return NULL;
    }
    if (!ret) {
        errno = ENOSPC;
    }
    return ret;
}

int inet_pton(int af, const char *src, void *dst) {
    int ret = -1;
    switch (af) {
        #if LWIP_IPV4
        case AF_INET: {
            ip4_addr_t ipaddr = { 0 };
            ret = ip4addr_aton(src, &ipaddr);
            inet_addr_from_ip4addr((struct in_addr *)dst, &ipaddr);
            break;
        }
        #endif
        #if LWIP_IPV6
        case AF_INET6: {
            ip6_addr_t ipaddr = { 0 };
            ret = ip6addr_aton(src, &ipaddr);
            inet6_addr_from_ip6addr((struct in6_addr *)dst, &ipaddr);
            break;
        }
        #endif
        default: {
            errno = EAFNOSUPPORT;
            break;
        }
    }
    return ret;
}
