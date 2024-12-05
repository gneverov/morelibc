// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "FreeRTOS.h"

#include "hardware/pio.h"

#define NUM_PIO_INTERRUPT_SOURCES 12


typedef void (*rp2_pio_handler_t)(PIO pio, enum pio_interrupt_source source, void *context, BaseType_t *pxHigherPriorityTaskWoken);

void rp2_pio_set_irq(PIO pio, enum pio_interrupt_source source, rp2_pio_handler_t handler, void *context);

void rp2_pio_clear_irq(PIO pio, enum pio_interrupt_source source);

void rp2_pio_debug(PIO pio);
