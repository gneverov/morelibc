// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "FreeRTOS.h"

#include "hardware/uart.h"


typedef void (*rp2_uart_handler_t)(uart_inst_t *uart, void *context, BaseType_t *pxHigherPriorityTaskWoken);

void rp2_uart_set_irq(uart_inst_t *uart, rp2_uart_handler_t handler, void *context);

void rp2_uart_clear_irq(uart_inst_t *uart);
