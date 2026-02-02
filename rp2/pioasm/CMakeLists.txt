cmake_minimum_required(VERSION 3.13...3.27)

project(rp2_pioasm C CXX ASM)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_C_STANDARD 11)

# pioasm library for embedded use (INTERFACE library with sources)
add_library(rp2_pioasm INTERFACE)
target_sources(rp2_pioasm INTERFACE
    pioasm.c
    pioasm_lexer.c
    pioasm_parser.c
)

# Test executable (optional)
option(PIOASM_BUILD_TESTS "Build pioasm tests" OFF)
if (PIOASM_BUILD_TESTS)
    if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
        message("Using PICO_SDK_PATH from environment ('${PICO_SDK_PATH}')")
    endif ()

    set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK")

    if (NOT PICO_SDK_PATH)
        message(FATAL_ERROR "SDK location was not specified. Please set PICO_SDK_PATH.")
    endif()

    set(PICO_PLATFORM host)
    include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
    pico_sdk_init()

    add_executable(pioasm_test pioasm_test.c)
    target_link_libraries(pioasm_test rp2_pioasm pico_base_headers)
    target_include_directories(pioasm_test PRIVATE host)
endif()
