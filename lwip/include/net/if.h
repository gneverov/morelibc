// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "lwip/netif.h"


#define IF_NAMESIZE NETIF_NAMESIZE      // Interface name length.


struct if_nameindex {
    unsigned if_index;                  // Numeric index of the interface.
    char *if_name;                      // Null-terminated name of the interface.
};

// void if_freenameindex(struct if_nameindex *ptr);
char *if_indextoname(unsigned ifindex, char *ifname);
// struct if_nameindex *if_nameindex(void);
unsigned if_nametoindex(const char *ifname);
