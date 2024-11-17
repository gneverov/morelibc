// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "freertos/interrupts.h"

#include "hardware/gpio.h"
#include "rp2/uart.h"


static rp2_uart_handler_t rp2_uart_handlers[NUM_UARTS];
static void *rp2_uart_contexts[NUM_UARTS];

static void rp2_uart_irq_handler(uint index) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    rp2_uart_handlers[index](UART_INSTANCE(index), rp2_uart_contexts[index], &xHigherPriorityTaskWoken);
}

static void rp2_uart0_irq_handler(void) {
    rp2_uart_irq_handler(0);
}

static void rp2_uart1_irq_handler(void) {
    rp2_uart_irq_handler(1);
}

__attribute__((constructor, visibility("hidden")))
void rp2_uart_init(void) {
    assert(check_interrupt_core_affinity());
    irq_set_exclusive_handler(UART0_IRQ, rp2_uart0_irq_handler);
    irq_set_exclusive_handler(UART1_IRQ, rp2_uart1_irq_handler);
}

void rp2_uart_set_irq(uart_inst_t *uart, rp2_uart_handler_t handler, void *context) {
    uint index = UART_NUM(uart);
    UBaseType_t save = set_interrupt_core_affinity();
    irq_set_enabled(UART_IRQ_NUM(uart), false);
    rp2_uart_handlers[index] = handler;
    rp2_uart_contexts[index] = context;
    irq_set_enabled(UART_IRQ_NUM(uart), true);
    clear_interrupt_core_affinity(save);
}

void rp2_uart_clear_irq(uart_inst_t *uart) {
    uint index = UART_NUM(uart);
    UBaseType_t save = set_interrupt_core_affinity();
    irq_set_enabled(UART_IRQ_NUM(uart), false);
    rp2_uart_handlers[index] = NULL;
    rp2_uart_contexts[index] = NULL;
    clear_interrupt_core_affinity(save);
}
