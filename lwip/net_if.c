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

struct if_nameindex *if_nameindex(void) {
    struct if_nameindex *ret = calloc(16, sizeof(struct if_nameindex));
    if (!ret) {
        return NULL;
    }
    int i = 0;
    LOCK_TCPIP_CORE();
    struct netif *netif = netif_list;
    while (netif && (i < 15)) {
        ret[i].if_index = netif_get_index(netif);
        netif_index_to_name(ret[i].if_index, ret[i].if_name);
        netif = netif->next;
        i++;
    }
    UNLOCK_TCPIP_CORE();
    return ret;
}

void if_freenameindex(struct if_nameindex *ptr) {
    free(ptr);
}