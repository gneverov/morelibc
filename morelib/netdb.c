// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <netdb.h>
#include "morelib/socket.h"


void freeaddrinfo(struct addrinfo *ai) {
    while (ai) {
        struct addrinfo *next = ai->ai_next;
        free(ai);
        ai = next;
    }
}

const char *gai_strerror(int ecode) {
    switch (ecode) {
        case EAI_AGAIN:
            return "Temporary failure in name resolution";
        case EAI_BADFLAGS:
            return "Bad value for ai_flags";
        case EAI_FAIL:
            return "Non-recoverable failure in name resolution";
        case EAI_FAMILY:
            return "ai_family not supported";
        case EAI_MEMORY:
            return "Memory allocation failure";
        case EAI_NONAME:
            return "Name or service not known";
        case EAI_SERVICE:
            return "Servname not supported for ai_socktype";
        case EAI_SOCKTYPE:
            return "ai_socktype not supported";
        case EAI_SYSTEM:
            return "System error";
        case EAI_OVERFLOW:
            return "Argument buffer too small";
        default:
            return NULL;
    }
}

int getaddrinfo(const char *nodename, const char *servname, const struct addrinfo *hints, struct addrinfo **res) {
    if (!nodename && !servname) {
        return EAI_NONAME;
    }

    int family = hints ? hints->ai_family : AF_UNSPEC;
    int ret = EAI_FAMILY;
    for (size_t i = 0; i < socket_num_families; i++) {
        if ((AF_UNSPEC != family) && (socket_families[i]->family != family)) {
            continue;
        }
        if (socket_families[i]->getaddrinfo) {
            ret = socket_families[i]->getaddrinfo(nodename, servname, hints, res);
        }
        if ((ret >= 0) || (AF_UNSPEC != family)) {
            return ret;
        }
    }
    return ret;
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *node, socklen_t nodelen, char *service, socklen_t servicelen, int flags) {
    int ret = EAI_FAMILY;
    for (size_t i = 0; i < socket_num_families; i++) {
        if ((socket_families[i]->family == sa->sa_family) && socket_families[i]->getnameinfo) {
            ret = socket_families[i]->getnameinfo(sa, salen, node, nodelen, service, servicelen, flags);
            break;
        }
    }
    return ret;
}
