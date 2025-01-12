set(PICO_CLIB morelibc)
set(PICO_STDIO_UART OFF)
message("before PICO_CLIB ${PICO_CLIB}")
pico_sdk_init()

# Sometimes pico-sdk doesn't get these
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS FALSE)
set(CMAKE_EXECUTABLE_SUFFIX .elf)

# Helper function to remove a list items from a target property.
function(target_remove_property_value TARGET PROP)
    get_target_property(props ${TARGET} ${PROP})
    list(REMOVE_ITEM props ${ARGN})
    set_target_properties(${TARGET} PROPERTIES ${PROP} "${props}")
endfunction()

# Helper function to compile a project with hidden visibility.
function(set_target_visibility_hidden TARGET)
    get_target_property(SRCS ${TARGET} INTERFACE_SOURCES)
    set_source_files_properties(${SRCS} PROPERTIES COMPILE_OPTIONS -fvisibility=hidden)
endfunction()

# Break non-essential dependency from pico_stdlib to pico_stdio.
# We do not want to use pico_stdio.
target_remove_property_value(pico_stdlib INTERFACE_LINK_LIBRARIES pico_stdio)
target_remove_property_value(pico_stdlib_headers INTERFACE_LINK_LIBRARIES pico_stdio_headers)
target_include_directories(pico_stdlib_headers INTERFACE ${PICO_SDK_PATH}/src/rp2_common/pico_stdio/include)

# Break non-essential dependency from pico_runtime to pico_printf and pico_malloc.
# We do not want to use pico_printf or pico_malloc.
target_remove_property_value(pico_runtime INTERFACE_LINK_LIBRARIES pico_printf pico_malloc)
target_remove_property_value(pico_runtime_headers INTERFACE_LINK_LIBRARIES pico_printf_headers pico_malloc_headers)

# Remove GCC specs option. We do no use specs with Picolibc.
target_remove_property_value(pico_runtime INTERFACE_LINK_OPTIONS --specs=nosys.specs)

# We use the the standard FreeRTOS mode of TinyUSB, not the specialized Pico mode.
target_remove_property_value(tinyusb_common_base INTERFACE_COMPILE_DEFINITIONS CFG_TUSB_OS=OPT_OS_PICO)
target_compile_definitions(tinyusb_common_base INTERFACE CFG_TUSB_OS=OPT_OS_FREERTOS)

# Make certain projects hidden as it does not make sense to export them.
set_target_visibility_hidden(pico_crt0)
set_target_visibility_hidden(pico_runtime)
set_target_visibility_hidden(pico_runtime_init)

# Check FreeRTOS path is defined so we can modify it from RP2.
if(NOT DEFINED FREERTOS_DIR)
    message(FATAL_ERROR "FreeRTOS path not specifed. Set FREERTOS_DIR.")
endif()

# Set platform specific variables.
if(${PICO_PLATFORM} STREQUAL rp2040)
    set(FREERTOS_DIR ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040)
    set(PICOLIBC_BUILDTYPE thumb/v6-m/nofp)
elseif(${PICO_PLATFORM} STREQUAL rp2350-arm-s)
    set(FREERTOS_DIR ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2350_ARM_NTZ)
    set(PICOLIBC_BUILDTYPE thumb/v8-m.main+fp/softfp)
else()
    message(FATAL_ERROR "Pico platform ${PICO_PLATFORM} not supported.")
endif()

# Set linker script files
set(RP2_EXE_LD_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_exe_${PICO_PLATFORM}.ld)
set(RP2_LIB_LD_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_lib_${PICO_PLATFORM}.ld)
