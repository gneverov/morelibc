cmake_minimum_required(VERSION 3.13)

add_library(morelib_lwip INTERFACE)

target_sources(morelib_lwip INTERFACE
    dns.c
    arpa_inet.c
    net_if.c
    netdb.c
    ping.c
    raw.c
    socket.c
    socket_api.c
    socket_opt.c
    telnet.c
    tcp.c
    udp.c
)

target_include_directories(morelib_lwip INTERFACE include)
