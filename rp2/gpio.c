// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "rp2/gpio.h"

#include "freertos/interrupts.h"
#include "semphr.h"

static rp2_gpio_handler_t rp2_gpio_handlers[NUM_BANK0_GPIOS];
static void *rp2_gpio_contexts[NUM_BANK0_GPIOS];

static void rp2_gpio_irq_handler(uint gpio, uint32_t event_mask) {
    assert(rp2_gpio_handlers[gpio]);
    rp2_gpio_handlers[gpio](gpio, event_mask, rp2_gpio_contexts[gpio]);
}

__attribute__((constructor, visibility("hidden")))
void rp2_gpio_init(void) {
    assert(check_interrupt_core_affinity());
    gpio_set_irq_callback(rp2_gpio_irq_handler);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

void rp2_gpio_set_irq_enabled(uint gpio, uint32_t event_mask, bool enabled) {
    UBaseType_t save = set_interrupt_core_affinity();
    assert(rp2_gpio_handlers[gpio]);
    gpio_set_irq_enabled(gpio, event_mask, enabled);
    clear_interrupt_core_affinity(save);
}

bool rp2_gpio_add_handler(uint gpio, rp2_gpio_handler_t handler, void *context) {
    UBaseType_t save = set_interrupt_core_affinity();
    bool ret = false;
    if (!rp2_gpio_handlers[gpio]) {
        gpio_set_irq_enabled(gpio, 0xf, false);
        rp2_gpio_handlers[gpio] = handler;
        rp2_gpio_contexts[gpio] = context;
        ret = true;
    }
    clear_interrupt_core_affinity(save);
    return ret;
}

bool rp2_gpio_remove_handler(uint gpio) {
    UBaseType_t save = set_interrupt_core_affinity();
    bool ret = false;
    if (rp2_gpio_handlers[gpio]) {
        gpio_set_irq_enabled(gpio, 0xf, false);
        rp2_gpio_handlers[gpio] = NULL;
        rp2_gpio_contexts[gpio] = NULL;
        ret = true;
    }
    clear_interrupt_core_affinity(save);
    return ret;
}

#ifndef NDEBUG
#include <stdio.h>

void rp2_gpio_debug(uint gpio) {
    check_gpio_param(gpio);
    io_bank0_irq_ctrl_hw_t *irq_ctrl_base = &io_bank0_hw->proc0_irq_ctrl + INTERRUPT_CORE_NUM;
    printf("gpio %u\n", gpio);
    printf("  function:    %d\n", gpio_get_function(gpio));
    printf("  pulls:       ");
    if (gpio_is_pulled_up(gpio)) {
        printf("up ");
    }
    if (gpio_is_pulled_down(gpio)) {
        printf("down ");
    }
    printf("\n");
    printf("  dir:         %s\n", gpio_is_dir_out(gpio) ? "out" : "in");
    printf("  value:       %d\n", gpio_get(gpio));
    uint32_t event_mask = irq_ctrl_base->inte[gpio / 8];
    event_mask >>= 4 * (gpio % 8);
    printf("  inte:        0x%02lx\n", event_mask & 0xf);
    uint32_t status = irq_ctrl_base->ints[gpio / 8];
    status >>= 4 * (gpio % 8);
    printf("  ints:        0x%02lx\n", status & 0xf);
    printf("  handler:     %p\n", rp2_gpio_handlers[gpio]);
    printf("  context:     %p\n", rp2_gpio_contexts[gpio]);
}
#endif
