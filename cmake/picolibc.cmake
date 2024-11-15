# Check that the compiler supports TLS, which is required for multicore Picolibc.
execute_process(
    COMMAND ${CMAKE_C_COMPILER} -v
    ERROR_VARIABLE C_COMPILER_VERSION
)
if(C_COMPILER_VERSION MATCHES --disable-tls)
    message(FATAL_ERROR "C compiler '${CMAKE_C_COMPILER}' does not support TLS.")
endif()

# Try to find Picolibc if not already set.
if(NOT DEFINED PICOLIBC_SYSROOT)
    set(PICOLIBC_SYSROOT ${CMAKE_C_COMPILER})
    cmake_path(GET PICOLIBC_SYSROOT PARENT_PATH PICOLIBC_SYSROOT)
    cmake_path(APPEND PICOLIBC_SYSROOT .. picolibc arm-none-eabi)
    cmake_path(NORMAL_PATH PICOLIBC_SYSROOT)
    message("Set Picolibc path relative to compiler at '${CMAKE_C_COMPILER}'.")
endif()

# Check that Picolibc exists
if(NOT EXISTS ${PICOLIBC_SYSROOT})
    message(FATAL_ERROR "Picolibc not found at '${PICOLIBC_SYSROOT}'. Specify path in PICOLIBC_SYSROOT.")
endif()

# Check that build type is defined (e.g., v6-m, v8-m.main+fp)
if(NOT DEFINED PICOLIBC_BUILDTYPE)
    message(FATAL_ERROR "Picolibc build type not defined. Specify in PICOLIBC_BUILDTYPE.")
endif()

# Check that Picolibc supports the build type.
if(NOT EXISTS ${PICOLIBC_SYSROOT}/lib/${PICOLIBC_BUILDTYPE})
    message(FATAL_ERROR "Picolibc build type '${PICOLIBC_BUILDTYPE}' not found in '${PICOLIBC_SYSROOT}'.")
endif()

# Set global compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -isystem ${PICOLIBC_SYSROOT}/include -ftls-model=local-exec")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -nostartfiles -L${PICOLIBC_SYSROOT}/lib/${PICOLIBC_BUILDTYPE}")
