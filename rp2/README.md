# RP2 support for Morelibc
Support library for using the Raspberry Pi [Pico SDK](https://github.com/raspberrypi/pico-sdk) with Morelibc, which includes the RP2040 and RP2350 microprocessors. Support provided includes:

- A Morelibc MTD driver for the chip's XIP flash memory and PSRAM.
- A Morelibc terminal driver for the chip's UART peripherals.
- An implementation of `getrandom`.
- LD scripts for linking Morelibc.
- Helper functions to interact with the Morelibc interrupt model.
- Helper functions to multiplex multiple users on a single interrupt vector.
