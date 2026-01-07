# Morelibc lwIP
List of socket-related functions implemented by Morelibc.

## sys/socket.h
| Function | Status | Notes |
| - | - | - |
| `accept` | 🟢 | |
| `accept4` | 🔴 | |
| `bind` | 🟢 | |
| `connect` | 🟢 | |
| `getpeername` | 🟢 | |
| `getsockname` | 🟢 | |
| `getsockopt` | 🟢 | |
| `listen` | 🟢 | |
| `recv` | 🟢 | |
| `recvfrom` | 🟢 | |
| `recvmsg` | 🔴 | |
| `send` | 🟢 | |
| `sendmsg` | 🔴 | |
| `sendto` | 🟢 | |
| `setsockopt` | 🟢 | |
| `shutdown` | 🟢 | |
| `sockatmark` | 🔴 | |
| `socket` | 🟢 | |
| `socketpair` | 🟢 | |

### Socket options
| Option | Status | Notes |
| - | - | - |
| `SO_ACCEPTCONN` | 🟢 | |
| `SO_BROADCAST` | 🟢 | |
| `SO_DEBUG` | 🔴 | |
| `SO_DOMAIN` | 🟢 | |
| `SO_DONTROUTE` | 🔴 | |
| `SO_ERROR` | 🟢 | |
| `SO_KEEPALIVE` | 🟢 | |
| `SO_LINGER` | 🔴 | |
| `SO_OOBINLINE` | 🔴 | |
| `SO_PROTOCOL` | 🟢 | |
| `SO_RCVBUF` | 🔴 | |
| `SO_RCVLOWAT` | 🔴 | |
| `SO_RCVTIMEO` | 🟢 | |
| `SO_REUSEADDR` | 🟢 | |
| `SO_SNDBUF` | 🔴 | |
| `SO_SNDLOWAT` | 🔴 | |
| `SO_SNDTIMEO` | 🟢 | |
| `SO_TYPE` | 🟢 | |

## netdb.h
| Function | Status | Notes |
| - | - | - |
| `endhostent` | 🔴 | |
| `endnetent` | 🔴 | |
| `endprotoent` | 🔴 | |
| `endservent` | 🔴 | |
| `freeaddrinfo` | 🟢 | |
| `gai_strerror` | 🟢 | |
| `getaddrinfo` | 🟢 | |
| `gethostent` | 🔴 | |
| `getnameinfo` | 🟢 | |
| `getnetbyaddr` | 🔴 | |
| `getnetbyname` | 🔴 | |
| `getnetent` | 🔴 | |
| `getprotobyname` | 🔴 | |
| `getprotobynumber` | 🔴 | |
| `getprotoent` | 🔴 | |
| `getservbyname` | 🔴 | |
| `getservbyport` | 🔴 | |
| `getservent` | 🔴 | |
| `sethostent` | 🔴 | |
| `setnetent` | 🔴 | |
| `setprotoent` | 🔴 | |
| `setservent` | 🔴 | |

## arpa/inet.h
| Function | Status | Notes |
| - | - | - |
| `htonl` | 🟢 | |
| `htons` | 🟢 | |
| `ntohl` | 🟢 | |
| `ntohs` | 🟢 | |
| `inet_addr` | 🔴 | Obsolete. |
| `inet_ntoa` | 🔴 | Obsolete. |
| `inet_ntop` | 🟢 | |
| `inet_pton` | 🟢 | |

## net/if.h
| Function | Status | Notes |
| - | - | - |
| `if_freenameindex` | 🟢 | |
| `if_indextoname` | 🟢 | |
| `if_nameindex` | 🟢 | |
| `if_nametoindex` | 🟢 | |
