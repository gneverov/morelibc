// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <net/if.h>


char *if_indextoname(unsigned ifindex, char *ifname) {
    LOCK_TCPIP_CORE();
    char *ret = netif_index_to_name(ifindex, ifname);
    UNLOCK_TCPIP_CORE();
    if (!ret) {
        errno = ENXIO;
    }
    return ret;
}

unsigned if_nametoindex(const char *ifname) {
    LOCK_TCPIP_CORE();
    u8_t ret = netif_name_to_index(ifname);
    UNLOCK_TCPIP_CORE();
    return ret;
}
