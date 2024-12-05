cmake_minimum_required(VERSION 3.13)

add_library(morelib_tinyusb INTERFACE)

target_sources(morelib_tinyusb INTERFACE
    cdc_device_cb.c
    msc_device.c
    # net_device_lwip.c
    term_usb.c
    tusb_config.c
    tusb_lock.c
    # usbd_cb.c
)

target_include_directories(morelib_tinyusb INTERFACE include)

# target_link_libraries(morelib_tinyusb INTERFACE
#     tinyusb_common
#     tinyusb_device
# )
