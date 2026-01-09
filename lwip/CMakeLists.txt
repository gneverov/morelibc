cmake_minimum_required(VERSION 3.13)

add_library(morelib_lwip INTERFACE)

target_sources(morelib_lwip INTERFACE
    arpa_inet.c
    dns.c
    net_if.c
    netdb.c
    ping.c
    raw.c
    socket.c
    socket_opt.c
    telnet.c
    tcp.c
    tls_context.c
    tls_socket.c
    udp.c
)

target_include_directories(morelib_lwip INTERFACE include)

target_compile_definitions(morelib_lwip INTERFACE MORELIB_LWIP=1)
