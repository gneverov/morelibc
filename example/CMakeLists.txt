cmake_minimum_required(VERSION 3.13)

include(FetchContent)

FetchContent_Declare(
    pico-sdk
    GIT_REPOSITORY https://github.com/gneverov/pico-sdk
    GIT_TAG efe2103f9b28458a1615ff096054479743ade236
)

FetchContent_Declare(
    freertos
    GIT_REPOSITORY https://github.com/gneverov/FreeRTOS-Kernel.git
    GIT_TAG ad0cff000985a06733542ce486278b5032db2d1e
)

FetchContent_Declare(
    fatfs
    URL http://elm-chan.org/fsw/ff/arc/ff15.zip
)

FetchContent_Populate(pico-sdk)
include(${pico-sdk_SOURCE_DIR}/pico_sdk_init.cmake)

project(morelib_example)

FetchContent_Populate(freertos)
set(FREERTOS_DIR ${freertos_SOURCE_DIR})

get_filename_component(MORELIBC_DIR ".." ABSOLUTE)
include(${MORELIBC_DIR}/rp2/pico-sdk.cmake)

include(${MORELIBC_DIR}/cmake/picolibc.cmake)

add_subdirectory(${FREERTOS_DIR} freertos)

add_subdirectory(${MORELIBC_DIR} morelib)

add_executable(example 
    main.c
    morelib_cfg.c
)

target_link_libraries(example
    morelib_rp2
)

pico_set_linker_script(example ${RP2_EXE_LD_SCRIPT})
pico_add_uf2_output(example)
