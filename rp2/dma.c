// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

/**
 * Module to support multiplexing DMA interrupts.
 * 
 * There are only 2 DMA IRQs, but potentially more than 2 users of DMA. This module allows users to
 * register an IRQ handler for an individual DMA channel, thereby allowing multiple users to share 
 * the same hardware IRQ.
 */
#include "hardware/irq.h"

#include "freertos/interrupts.h"

#include "rp2/dma.h"


static rp2_dma_handler_t rp2_dma_handlers[NUM_DMA_CHANNELS];
static void *rp2_dma_contexts[NUM_DMA_CHANNELS];

static void rp2_dma_irq_handler(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    for (uint channel = 0; channel < NUM_DMA_CHANNELS; channel++) {
        if (dma_channel_get_irq1_status(channel)) {
            assert(rp2_dma_handlers[channel]);
            rp2_dma_handlers[channel](channel, rp2_dma_contexts[channel], &xHigherPriorityTaskWoken);
        }
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

__attribute__((constructor, visibility("hidden")))
void rp2_dma_init(void) {
    assert(check_interrupt_core_affinity());
    irq_add_shared_handler(DMA_IRQ_1, rp2_dma_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);
}

/**
 * Sets the handler for a DMA channel.
 * 
 * Args:
 * channel: DMA channel
 * handler: handler
 * context: context argument of handler
 */
void rp2_dma_set_irq(uint channel, rp2_dma_handler_t handler, void *context) {
    UBaseType_t save = set_interrupt_core_affinity();
    dma_channel_set_irq1_enabled(channel, false);
    rp2_dma_handlers[channel] = handler;
    rp2_dma_contexts[channel] = context;
    dma_channel_set_irq1_enabled(channel, true);
    clear_interrupt_core_affinity(save);
}


/**
 * Clears the handler for a DMA channel.
 * 
 * Args:
 * channel: DMA channel
 */
void rp2_dma_clear_irq(uint channel) {
    UBaseType_t save = set_interrupt_core_affinity();
    dma_channel_set_irq1_enabled(channel, false);
    rp2_dma_handlers[channel] = NULL;
    rp2_dma_contexts[channel] = NULL;
    clear_interrupt_core_affinity(save);
}


/**
 * Acknowledges an IRQ for a DMA channel.
 * 
 * Args:
 * channel: DMA channel
 */
void rp2_dma_acknowledge_irq(uint channel) {
    dma_channel_acknowledge_irq1(channel);
}

#ifndef NDEBUG
#include <stdio.h>

void rp2_dma_debug(uint channel) {
    check_dma_channel_param(channel);
    dma_channel_hw_t *hw = &dma_hw->ch[channel];
    printf("dma channel %u\n", channel);
    printf("  read_addr:   %p\n", (void *)hw->read_addr);
    printf("  write_addr:  %p\n", (void *)hw->write_addr);
    printf("  trans_count: %lu\n", hw->transfer_count);
    printf("  enabled:     %d\n", !!(hw->ctrl_trig & 1));
    printf("  ctrl:        %08lx\n", hw->ctrl_trig);

    dma_debug_channel_hw_t *debug_hw = &dma_debug_hw->ch[channel];
    printf("  ctrdeq:      %lu\n", debug_hw->dbg_ctdreq);
    printf("  tcr:         %lu\n", debug_hw->dbg_tcr);

    uint bit = 1u << channel;
    printf("  inte:        %d\n", !!(dma_hw->inte1 & bit));
    printf("  ints:        %d\n", !!(dma_hw->ints1 & bit));

    printf("  handler:     %p\n", rp2_dma_handlers[channel]);
    printf("  context:     %p\n", rp2_dma_contexts[channel]);
}
#endif
