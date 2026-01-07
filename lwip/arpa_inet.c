// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#include "lwip/netif.h"


#if LWIP_IPV6
/**
 * Replacement for ip6addr_ntoa_r that handles zone info.
 */
char *ip6addr_with_zone_ntoa_r(const ip6_addr_t *addr, char *buf, int buflen) {
    if (!ip6addr_ntoa_r(addr, buf, buflen)) {
        return NULL;
    }

    // lwIP returns uppercase hex for IPv6 addresses, convert to lowercase
    // to match the expected behavior of inet_ntop.
    strlwr(buf);
        
    // check for zone since ip6addr_ntoa_r doesn't do this
    int zone = ip6_addr_zone(addr);
    if (!zone) {
        return buf;
    }

    // lookup zone name
    char name[NETIF_NAMESIZE];
    LOCK_TCPIP_CORE();
    bool lookup = netif_index_to_name(zone, name);
    UNLOCK_TCPIP_CORE();
    if (!lookup) {
        return buf;
    }

    // add zone specifier to send of buf
    size_t used = strnlen(buf, buflen);
    buflen -= used;
    if (snprintf(buf + used, buflen, "%%%s", name) >= buflen) {
        return NULL;
    }

    return buf;
}
#endif

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
    char *ret = NULL;
    switch (af) {
#if LWIP_IPV4
        case AF_INET: {
            ip4_addr_t ipaddr = { 0 };
            inet_addr_to_ip4addr(&ipaddr, (struct in_addr *)src);
            ret = ip4addr_ntoa_r(&ipaddr, dst, size);
            break;
        }
#endif
#if LWIP_IPV6
        case AF_INET6: {
            ip6_addr_t ipaddr = { 0 };
            inet6_addr_to_ip6addr(&ipaddr, (struct in6_addr *)src);
            ret = ip6addr_with_zone_ntoa_r(&ipaddr, dst, size);
            break;
        }
#endif
        default: {
            errno = EAFNOSUPPORT;
            return NULL;
        }
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
            LOCK_TCPIP_CORE();  // for zone resolution
            ret = ip6addr_aton(src, &ipaddr);
            UNLOCK_TCPIP_CORE();
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
