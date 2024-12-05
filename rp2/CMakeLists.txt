cmake_minimum_required(VERSION 3.13)

add_library(morelib_rp2 INTERFACE)

target_sources(morelib_rp2 INTERFACE
    dma.c
    fifo.c
    flash.c
	flash_lockout.c
    gpio.c
    irq.c
    mtd.c
    pio.c
    pwm.c
    sdcard.c
    spi.c
    term_uart.c
)

target_include_directories(morelib_rp2 INTERFACE include)

target_link_libraries(morelib_rp2 INTERFACE
	morelib_core
	hardware_dma
	hardware_flash
	hardware_pio
	hardware_pwm
	hardware_spi
	hardware_uart
	pico_runtime
)

add_library(pico_morelibc_interface INTERFACE)

target_sources(pico_morelibc_interface INTERFACE morelibc_interface.c)

target_link_libraries(pico_morelibc_interface INTERFACE 
    pico_aon_timer
    pico_runtime_init
    pico_time
)

if(PICO_CYW43_SUPPORTED)
    add_library(cyw43_helper INTERFACE)

    target_sources(cyw43_helper INTERFACE
        cyw43_driver.c
        cyw43_lwip.c
    )

    target_compile_definitions(cyw43_helper INTERFACE
        CYW43_LWIP=1
    )

    target_include_directories(cyw43_helper INTERFACE include)
endif()
