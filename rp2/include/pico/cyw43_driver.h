/*
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

void cyw43_driver_init(void);

void cyw43_driver_deinit(void);

// PICO_CONFIG: CYW43_PIO_CLOCK_DIV_DYNAMIC, Enable runtime configuration of the clock divider for communication with the wireless chip, type=bool, default=0, group=pico_cyw43_driver
#ifndef CYW43_PIO_CLOCK_DIV_DYNAMIC
#define CYW43_PIO_CLOCK_DIV_DYNAMIC 0
#endif

// PICO_CONFIG: CYW43_PIO_CLOCK_DIV_INT, Integer part of the clock divider for communication with the wireless chip, type=bool, default=2, group=pico_cyw43_driver
#ifndef CYW43_PIO_CLOCK_DIV_INT
// backwards compatibility using old define
#ifdef CYW43_PIO_CLOCK_DIV
#define CYW43_PIO_CLOCK_DIV_INT CYW43_PIO_CLOCK_DIV
#else
#define CYW43_PIO_CLOCK_DIV_INT 2
#endif
#endif

// PICO_CONFIG: CYW43_PIO_CLOCK_DIV_FRAC8, Fractional part of the clock divider for communication with the wireless chip 0-255, type=int, min=0, max=255, default=0, group=pico_cyw43_driver
#ifndef CYW43_PIO_CLOCK_DIV_FRAC8
#ifdef CYW43_PIO_CLOCK_DIV_FRAC
#define CYW43_PIO_CLOCK_DIV_FRAC8 CYW43_PIO_CLOCK_DIV_FRAC
#else
#define CYW43_PIO_CLOCK_DIV_FRAC8 0
#endif
#endif

#if CYW43_PIO_CLOCK_DIV_DYNAMIC
void cyw43_set_pio_clkdiv_int_frac8(uint32_t clock_div_int, uint8_t clock_div_frac8);

// backwards compatibility
static inline void cyw43_set_pio_clock_divisor(uint16_t clock_div_int, uint8_t clock_div_frac8) {
    return cyw43_set_pio_clkdiv_int_frac8(clock_div_int, clock_div_frac8);
}
#endif

#if CYW43_PIN_WL_DYNAMIC
int cyw43_set_pins_wl(uint pins[CYW43_PIN_INDEX_WL_COUNT]);
#endif
