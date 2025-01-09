// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <errno.h>
#include <sys/socket.h>


// for use in the flags field of the addrinfo structure
#define AI_PASSIVE 0x01                 // Socket address is intended for bind().
#define AI_CANONNAME 0x02               // Request for canonical name.
#define AI_NUMERICHOST 0x04             // Return numeric host address as name.
#define AI_NUMERICSERV 0x08             // Inhibit service name resolution.
#define AI_V4MAPPED 0x10                // If no IPv6 addresses are found, query for IPv4 addresses and return them to the caller as IPv4-mapped IPv6 addresses.
#define AI_ALL 0x20                     // Query for both IPv4 and IPv6 addresses.
#define AI_ADDRCONFIG 0x40              // Query for IPv4 addresses only when an IPv4 address is configured; query for IPv6 addresses only when an IPv6 address is configured.

// error values for getaddrinfo() and getnameinfo()
#define EAI_AGAIN (-1)                  // The name could not be resolved at this time. Future attempts may succeed.
#define EAI_BADFLAGS (-2)               // The flags had an invalid value.
#define EAI_FAIL (-3)                   // A non-recoverable error occurred.
#define EAI_FAMILY (-4)                 // The address family was not recognized or the address length was invalid for the specified family.
#define EAI_MEMORY (-5)                 // There was a memory allocation failure.
#define EAI_NONAME (-6)                 // The name does not resolve for the supplied parameters. NI_NAMEREQD is set and the host's name cannot be located, or both nodename and servname were null.
#define EAI_SERVICE (-7)                // The service passed was not recognized for the specified socket type.
#define EAI_SOCKTYPE (-8)               // The intended socket type was not recognized.
#define EAI_SYSTEM (-9)                 // A system error occurred. The error code can be found in errno.
#define EAI_OVERFLOW (-10)              // An argument buffer overflowed.


struct addrinfo {
    int ai_flags;                       // Input flags.
    int ai_family;                      // Address family of socket.
    int ai_socktype;                    // Socket type.
    int ai_protocol;                    // Protocol of socket.
    socklen_t ai_addrlen;               // Length of socket address.
    struct sockaddr *ai_addr;           // Socket address of socket.
    char *ai_canonname;                 // Canonical name of service location.
    struct addrinfo *ai_next;           // Pointer to next in list.
};

// void endhostent(void);
// void endnetent(void);
// void endprotoent(void);
// void endservent(void);
void freeaddrinfo(struct addrinfo *ai);
const char *gai_strerror(int ecode);
int getaddrinfo(const char *nodename, const char *servname, const struct addrinfo *hints, struct addrinfo **res);
// struct hostent *gethostent(void);
// int getnameinfo(const struct sockaddr *, socklen_t, char *, socklen_t, char *, socklen_t, int);
// struct netent *getnetbyaddr(uint32_t, int);
// struct netent *getnetbyname(const char *);
// struct netent *getnetent(void);
// struct protoent *getprotobyname(const char *);
// struct protoent *getprotobynumber(int);
// struct protoent *getprotoent(void);
// struct servent *getservbyname(const char *, const char *);
// struct servent *getservbyport(int, const char *);
// struct servent *getservent(void);
// void sethostent(int);
// void setnetent(int);
// void setprotoent(int);
// void setservent(int);
