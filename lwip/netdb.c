// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lwip/dns.h"

#include "./dns.h"


static int netdb_gethostbyname(const char *hostname, ip_addr_t *ipaddr, u8_t addrtype) {
    int fd = socket_dns();
    if (fd < 0) {
        return EAI_SYSTEM;
    }

    int ai_ret = EAI_FAIL;
    size_t len = strlen(hostname) + 1;
    struct socket_dns_arg *arg = calloc(1, sizeof(struct socket_dns_arg) + len);
    if (!arg) {
        ai_ret = EAI_MEMORY;
        goto exit;
    }
    arg->addrtype = addrtype;
    strcpy(arg->hostname, hostname);

    int ret = send(fd, &arg, sizeof(arg), 0);
    if (ret < 0) {
        ai_ret = (errno == EAGAIN) ? EAI_AGAIN : EAI_SYSTEM;
    }
    if (ret < sizeof(arg)) {
        goto exit;
    }

    arg = NULL;
    ret = recv(fd, &arg, sizeof(arg), 0);
    if (ret < 0) {
        ai_ret = (errno == EAGAIN) ? EAI_AGAIN : EAI_SYSTEM;
    }
    if (ret < sizeof(arg)) {
        goto exit;
    }

    if (arg->err != ERR_OK) {
        errno = err_to_errno(arg->err);
        ai_ret = EAI_SYSTEM;
        goto exit;
    }
    
    if (ip_addr_isany_val(arg->ipaddr)) {
        ai_ret = EAI_NONAME;
        goto exit;
    }

    ip_addr_copy(*ipaddr, arg->ipaddr);
    ai_ret = 0;   
    
exit:
    free(arg);
    close(fd);
    return ai_ret;
}

void freeaddrinfo(struct addrinfo *ai) {
    while (ai) {
        struct addrinfo *next = ai->ai_next;
        free(ai);
        ai = next;
    }
}

const char *gai_strerror(int ecode) {
    return NULL;
}

int getaddrinfo(const char *nodename, const char *servname, const struct addrinfo *hints, struct addrinfo **res) {
    if (!nodename && !servname) {
        return EAI_NONAME;
    }

    int flags = hints ? hints->ai_flags : 0;
    int family = hints ? hints->ai_family : AF_UNSPEC;
    int socktype = hints ? hints->ai_socktype : 0;
    int protocol = hints ? hints->ai_protocol : 0;
    u8_t addrtype;
    switch (family) {
        case AF_UNSPEC:
            addrtype = LWIP_DNS_ADDRTYPE_DEFAULT;
            break;
        case AF_INET:
            addrtype = LWIP_DNS_ADDRTYPE_IPV4;
            break;
        case AF_INET6:
            addrtype = (flags & AI_V4MAPPED) ? LWIP_DNS_ADDRTYPE_IPV6_IPV4 : LWIP_DNS_ADDRTYPE_IPV6;
            break;
        default:
           return EAI_FAMILY; 
    }

    size_t port = 0;
    if (servname) {
        /* service name specified: convert to port number
         * @todo?: currently, only ASCII integers (port numbers) are supported (AI_NUMERICSERV)! */
        char *end;
        port = strtoul(servname, &end, 0);
        if (*end) {
            /* atoi failed - service was not numeric */
            return EAI_SERVICE;
        }
        if (port > 0xffff) {
            return EAI_SERVICE;
        }
    }

    ip_addr_t ipaddr;
    if (nodename) {
        /* service location specified, try to resolve */
        LOCK_TCPIP_CORE();
        int ret = ipaddr_aton(nodename, &ipaddr);
        UNLOCK_TCPIP_CORE();
        if (ret) {
            if (IP_IS_V6_VAL(ipaddr) && (family == AF_INET)) {
                return EAI_NONAME;
            }
            if (IP_IS_V4_VAL(ipaddr) && (family == AF_INET6) && !(flags & AI_V4MAPPED)) {
                return EAI_NONAME;
            }            
        }
        else if (flags & AI_NUMERICHOST) {
            /* no DNS lookup, just parse for an address string */
            return EAI_NONAME;
        }
        else {
            ret = netdb_gethostbyname(nodename, &ipaddr, addrtype);
            if (ret < 0) {
                return ret;
            }
        }
    }
    else {
        /* service location specified, use loopback address */
        if (flags & AI_PASSIVE) {
            ip_addr_set_any_val(family == AF_INET6, ipaddr);
        }
        else {
            ip_addr_set_loopback_val(family == AF_INET6, ipaddr);
        }
    }

    if (!IP_IS_V6_VAL(ipaddr) && (family == AF_INET6)) {
        ip4_2_ipv4_mapped_ipv6(ip_2_ip6(&ipaddr), ip_2_ip4(&ipaddr));
        IP_SET_TYPE_VAL(ipaddr, IPADDR_TYPE_V6);
    }

    struct result {
        struct addrinfo addrinfo;
        struct sockaddr_storage sockaddr;
        char canonname[DNS_MAX_NAME_LENGTH];
    } *result = calloc(1, sizeof(struct result));
    if (!result) {
        return EAI_MEMORY;
    }

    result->addrinfo.ai_family = IP_IS_V6_VAL(ipaddr) ? AF_INET6 : AF_INET;
    result->addrinfo.ai_socktype = socktype ? socktype : SOCK_STREAM;
    result->addrinfo.ai_protocol = protocol;
    result->addrinfo.ai_addrlen = socket_sockaddr_storage_from_lwip(&result->sockaddr, &ipaddr, port);
    result->addrinfo.ai_addr = (struct sockaddr *)&result->sockaddr;
    result->addrinfo.ai_canonname = NULL;
    result->addrinfo.ai_next = NULL;
    *res = &result->addrinfo;
    return 0;
}
