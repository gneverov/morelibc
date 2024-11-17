cmake_minimum_required(VERSION 3.13)

add_library(morelib_core INTERFACE)

target_sources(morelib_core INTERFACE
    dev.c
    devfs.c
    dirent.c
    dlfcn.c
    event.c
    fcntl.c
    flash_env.c
    flash_heap.c
    ioctl.c
    lock.c
    mem.c
    mman.c
    mtdblk.c
    pipe.c
    poll.c
    random.c
    ring.c
    signal.c
    stat.c
    statvfs.c
    termios.c
    thread.c
    time.c
    tty.c
    unistd.c
    utsname.c
    vfs.c
)

set_source_files_properties(
    flash_env.c
    flash_heap.c
    PROPERTIES COMPILE_OPTIONS -fvisibility=hidden
)

add_library(morelib_headers INTERFACE)
target_include_directories(morelib_headers INTERFACE include)

target_link_libraries(morelib_core INTERFACE
    morelib_headers
    morelib_freertos
)

if(DEFINED FATFS_DIR)
    add_library(morelib_fatfs INTERFACE)
    target_sources(morelib_fatfs INTERFACE
        fatfs.c
        ${FATFS_DIR}/source/ff.c
        ${FATFS_DIR}/source/ffunicode.c
    )
    target_include_directories(morelib_fatfs INTERFACE ${FATFS_DIR}/source)
    target_link_libraries(morelib_fatfs INTERFACE
        morelib_core
    )
endif()

if(DEFINED LITTLEFS_DIR)
    add_library(morelib_littlefs INTERFACE)
    target_sources(morelib_littlefs INTERFACE 
        littlefs.c
        ${LITTLEFS_DIR}/lfs.c
        ${LITTLEFS_DIR}/lfs_util.c        
    )
    target_include_directories(morelib_littlefs INTERFACE ${LITTLEFS_DIR})
    target_compile_definitions(morelib_littlefs INTERFACE
        $<$<NOT:$<CONFIG:DEBUG>>:LFS_NO_ASSERT>
    )
    target_link_libraries(morelib_littlefs INTERFACE
        morelib_core
    )
endif()


# Functions for creating dynamic libraries
# ---
# Modifies a firmware executable to support dynamic linking
# TARGET - the firmware executable to create and modify
function(add_firmware_executable TARGET)
    add_executable(${TARGET} ${ARGN})
    target_link_options(${TARGET} PRIVATE
        LINKER:--gc-keep-exported
    )
    add_custom_command(
        TARGET ${TARGET} POST_BUILD
        COMMAND cp $<TARGET_FILE:${TARGET}> $<TARGET_FILE_BASE_NAME:${TARGET}>.orig$<TARGET_FILE_SUFFIX:${TARGET}>
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/tools/mkfirmware.py $<TARGET_FILE:${TARGET}>
        DEPENDS ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/tools/mkfirmware.py ${TARGET}
        VERBATIM
        COMMAND_EXPAND_LISTS
    )
endfunction()

# Creates an import library for an dynamic library. An import library is used to create dependencies between dynamic libraries.
# TARGET - the import library to create
# EXE - the dynamic library to create the import library from
function(add_import_library TARGET EXE)
    add_library(${TARGET} INTERFACE)
    target_link_options(${TARGET} INTERFACE
        "LINKER:-R$<TARGET_FILE:${EXE}>"
        $<LIST:FILTER,$<TARGET_PROPERTY:${EXE},LINK_OPTIONS>,INCLUDE,LINKER:--wrap>
    )
    target_include_directories(${TARGET} INTERFACE
        $<TARGET_PROPERTY:${EXE},INCLUDE_DIRECTORIES>
    )
    target_compile_definitions(${TARGET} INTERFACE
        $<TARGET_PROPERTY:${EXE},COMPILE_DEFINITIONS>
    )     
    add_dependencies(${TARGET} ${EXE})
endfunction()

# Creates a dynamic library
# TARGET - the dynamic library to create
function(add_dynamic_library TARGET)
    add_executable(${TARGET} ${ARGN})
    target_link_options(${TARGET} PRIVATE
        -nostdlib
        LINKER:--gc-keep-exported
        LINKER:-q
        LINKER:-entry=__dl_main
        LINKER:-init=__dl_init
        LINKER:-fini=__dl_fini
    )
    target_compile_options(${TARGET} PRIVATE
        -fvisibility=hidden
    )
    target_sources(${TARGET} PRIVATE
        ${MICROPY_PORT_DIR}/newlib/dl_init.c
    )
    add_custom_command(
        TARGET ${TARGET} POST_BUILD
        COMMAND cp $<TARGET_FILE:${TARGET}> $<TARGET_FILE_BASE_NAME:${TARGET}>.orig$<TARGET_FILE_SUFFIX:${TARGET}>
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/tools/mkextmod.py $<TARGET_FILE:${TARGET}> $<TARGET_PROPERTY:NEWLIB_DYNLINK_OPTIONS>
        DEPENDS ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/tools/mkextmod.py ${TARGET}
        VERBATIM
        COMMAND_EXPAND_LISTS
    )
    if(NOT PICO_NO_UF2)
        pico_add_uf2_output(${TARGET})
    endif()
endfunction()


# Targets for extracting objects out of libc
# ---
# Execute "ar t" to list objects in libc archive
execute_process(COMMAND ${CMAKE_AR} t ${PICOLIBC_SYSROOT}/lib/${PICOLIBC_BUILDTYPE}/libc.a OUTPUT_VARIABLE LIBC_OBJS )
separate_arguments(LIBC_OBJS UNIX_COMMAND ${LIBC_OBJS})

# Extract object files ("ar x") into output directory
add_custom_target(extract_libc
    COMMAND ${CMAKE_AR} x ${PICOLIBC_SYSROOT}/lib/${PICOLIBC_BUILDTYPE}/libc.a
    BYPRODUCTS ${LIBC_OBJS}
    VERBATIM
)
