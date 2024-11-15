cmake_minimum_required(VERSION 3.13)

if(NOT TARGET FreeRTOS-Kernel)
    message(FATAL_ERROR "FreeRTOS-Kernel target missing")
endif()

add_library(morelib_freertos INTERFACE)

target_sources(morelib_freertos INTERFACE
    heap_malloc.c
    interrupts.c
    port_hooks.c
)

target_include_directories(morelib_freertos INTERFACE
    include
)

target_link_libraries(morelib_freertos INTERFACE
    FreeRTOS-Kernel
)
