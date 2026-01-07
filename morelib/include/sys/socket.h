// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include "lwip/opt.h"

#define SOCK_STREAM 1                   // Byte-stream socket.
#define SOCK_RAW 2                      // Raw Protocol Interface.
#define SOCK_DGRAM 3                    // Datagram socket.
#define SOCK_SEQPACKET 4                // Sequenced-packet socket.

#define SOL_SOCKET 0xfff                // Options to be accessed at socket level, not protocol level.

// option_name argument in getsockopt() or setsockopt() calls
// POSIX options
#define SO_ACCEPTCONN   0x0002          // Socket is accepting connections.
#define SO_BROADCAST    0x0020          // Transmission of broadcast messages is supported.
#define SO_DEBUG        0x0001          // Debugging information is being recorded.
#define SO_DOMAIN       0x100c          // Socket domain.
#define SO_DONTROUTE    0x0010          // Bypass normal routing.
#define SO_ERROR        0x1007          // Socket error status.
#define SO_KEEPALIVE    0x0008          // Connections are kept alive with periodic messages.
#define SO_LINGER       0x0080          // Socket lingers on close.
#define SO_OOBINLINE    0x0100          // Out-of-band data is transmitted in line.
#define SO_PROTOCOL     0x100d          // Socket protocol.
#define SO_RCVBUF       0x1002          // Receive buffer size.
#define SO_RCVLOWAT     0x1004          // Receive ``low water mark''.
#define SO_RCVTIMEO     0x1006          // Receive timeout.
#define SO_REUSEADDR    0x0004          // Reuse of local addresses is supported.
#define SO_SNDBUF       0x1001          // Send buffer size.
#define SO_SNDLOWAT     0x1003          // Send ``low water mark''.
#define SO_SNDTIMEO     0x1005          // Send timeout.
#define SO_TYPE         0x1008          // Socket type.

// lwIP options
#define SO_REUSEPORT    0x0200          /* Unimplemented: allow local address & port reuse */
#define SO_USELOOPBACK  0x0040          /* Unimplemented: bypass hardware when possible */
#define SO_CONTIMEO     0x1009          /* Unimplemented: connect timeout */
#define SO_NO_CHECK     0x100a          /* don't create UDP checksum */
#define SO_BINDTODEVICE 0x100b          /* bind to device */

#define SOMAXCONN 255                   // The maximum backlog queue length.

#define AF_UNSPEC 0                     // Unspecified.
#define AF_UNIX 1                       // UNIX domain sockets.
#define AF_INET 2                       // Internet domain sockets for use with IPv4 addresses.
#define AF_INET6 10                     // Internet domain sockets for use with IPv6 addresses.


#define SHUT_RD 0                       // Disables further receive operations.
#define SHUT_RDWR 1                     // Disables further send and receive operations.
#define SHUT_WR 2                       // Disables further send operations.


typedef size_t socklen_t;

typedef uint8_t sa_family_t;

struct sockaddr {
  sa_family_t sa_family;                // Address family.
  char sa_data[];                       // Socket address (variable-length data).
};

struct sockaddr_storage {
    sa_family_t ss_family;
#if LWIP_IPV6
    uint32_t ss_data[6];
#elif LWIP_IPV4
    uint32_t ss_data[1];
#endif
};

struct linger {
    int l_onoff;                        // Indicates whether linger option is enabled.
    int l_linger;                       // Linger time, in seconds.
};

int accept(int fd, struct sockaddr *address, socklen_t *address_len);
int bind(int fd, const struct sockaddr *address, socklen_t address_len);
int connect(int fd, const struct sockaddr *address, socklen_t address_len);
int getpeername(int fd, struct sockaddr *address, socklen_t *address_len);
int getsockname(int fd, struct sockaddr *address, socklen_t *address_len);
int getsockopt(int fd, int level, int option_name, void *option_value, socklen_t *option_len);
int listen(int fd, int backlog);
ssize_t recv(int fd, void *buffer, size_t length, int flags);
ssize_t recvfrom(int fd, void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len);
// ssize_t recvmsg(int, struct msghdr *, int);
ssize_t send(int fd, const void *buffer, size_t length, int flags);
// ssize_t sendmsg(int, const struct msghdr *, int);
ssize_t sendto(int fd, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
int setsockopt(int fd, int level, int option_name, const void *option_value, socklen_t option_len);
int shutdown(int fd, int how);
// int sockatmark(int);
int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int socket_vector[2]);
