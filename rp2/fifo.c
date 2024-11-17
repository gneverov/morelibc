// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <malloc.h>
#include <memory.h>

#include "FreeRTOS.h"
#include "task.h"

#include "hardware/timer.h"

#include "rp2/dma.h"
#include "rp2/fifo.h"

static void rp2_fifo_irq_handler(uint channel, void *context, BaseType_t *pxHigherPriorityTaskWoken);

static void rp2_fifo_start_dma(rp2_fifo_t *fifo, bool enabled);

void rp2_fifo_init(rp2_fifo_t *fifo, bool tx, rp2_fifo_handler_t handler) {
    memset(fifo, 0, sizeof(rp2_fifo_t));
    fifo->channel = -1;
    fifo->tx = tx;
    fifo->handler = handler;
}

bool rp2_fifo_alloc(rp2_fifo_t *fifo, uint fifo_size, uint dreq, enum dma_channel_transfer_size dma_transfer_size, bool bswap, volatile void *target_addr) {
    uint log2_size = -1u;
    while (fifo_size) {
        log2_size++;
        fifo_size >>= 1;
    }
    void *buffer = ring_alloc(&fifo->ring, log2_size);
    if (!buffer) {
        return false;
    }

    int channel = dma_claim_unused_channel(false);
    if (channel < 0) {
        errno = EBUSY;
        return false;
    }

    dma_channel_config c = dma_channel_get_default_config(channel);
    channel_config_set_read_increment(&c, fifo->tx);
    channel_config_set_write_increment(&c, !fifo->tx);
    channel_config_set_dreq(&c, dreq);
    channel_config_set_transfer_data_size(&c, dma_transfer_size);
    channel_config_set_bswap(&c, bswap);
    dma_channel_set_config(channel, &c, false);

    if (fifo->tx) {
        dma_channel_set_write_addr(channel, target_addr, false);
    } else {
        dma_channel_set_read_addr(channel, target_addr, false);
    }

    fifo->channel = channel;
    fifo->transfer_size = dma_transfer_size;

    rp2_fifo_start_dma(fifo, true);
    return true;
}

void rp2_fifo_deinit(rp2_fifo_t *fifo) {
    rp2_fifo_start_dma(fifo, false);
    if (fifo->channel >= 0) {
        dma_channel_unclaim(fifo->channel);
        fifo->channel = -1;
    }
    ring_free(&fifo->ring);
}

static void rp2_fifo_irq_handler(uint channel, void *context, BaseType_t *pxHigherPriorityTaskWoken) {
    rp2_fifo_t *fifo = context;
    fifo->int_count++;
    rp2_dma_acknowledge_irq(channel);

    ring_t ring;
    rp2_fifo_exchange_from_isr(fifo, &ring, 0);

    if (fifo->handler) {
        fifo->handler(fifo, &ring, pxHigherPriorityTaskWoken);
    }
}

static void rp2_fifo_do_exchange(rp2_fifo_t *fifo, ring_t *ring, size_t usr_count) {
    size_t dma_count = 0;
    if ((fifo->channel >= 0) && fifo->trans_count) {
        uint trans_count = dma_channel_hw_addr(fifo->channel)->transfer_count;
        dma_count = (fifo->trans_count - trans_count) << fifo->transfer_size;
        fifo->trans_count = trans_count;
    }
    if (fifo->tx) {
        fifo->ring.read_index += dma_count;
        fifo->ring.write_index += usr_count;
    } else {
        fifo->ring.read_index += usr_count;
        fifo->ring.write_index += dma_count;
    }

    *ring = fifo->ring;

    if ((fifo->channel >= 0) && !fifo->trans_count) {
        size_t begin_index = fifo->tx ? ring->read_index : ring->write_index;
        size_t end_index = fifo->tx ? ring->write_index : ring->read_index + ring->size;
        void *ptr = ring_at(ring, begin_index, &dma_count);
        dma_count = MIN(dma_count, end_index - begin_index);
        fifo->trans_count = dma_count >> fifo->transfer_size;
        if (!fifo->trans_count) {
            return;
        }
        if (fifo->tx) {
            dma_channel_transfer_from_buffer_now(fifo->channel, ptr, fifo->trans_count);
        } else {
            dma_channel_transfer_to_buffer_now(fifo->channel, ptr, fifo->trans_count);
        }
    }
}

void rp2_fifo_exchange(rp2_fifo_t *fifo, ring_t *ring, size_t usr_count) {
    taskENTER_CRITICAL();
    rp2_fifo_do_exchange(fifo, ring, usr_count);
    taskEXIT_CRITICAL();
}

void rp2_fifo_exchange_from_isr(rp2_fifo_t *fifo, ring_t *ring, size_t usr_count) {
    UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
    rp2_fifo_do_exchange(fifo, ring, usr_count);
    taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
}

size_t rp2_fifo_transfer(rp2_fifo_t *fifo, void *buffer, size_t size) {
    ring_t ring;
    rp2_fifo_exchange(fifo, &ring, 0);
    size_t count = fifo->tx ? ring_write(&ring, buffer, size) : ring_read(&ring, buffer, size);
    if (count) {
        rp2_fifo_exchange(fifo, &ring, count);
    }
    return count;
}

static void rp2_fifo_start_dma(rp2_fifo_t *fifo, bool enabled) {
    if (fifo->channel < 0) {
        return;
    }
    if (enabled) {
        rp2_dma_set_irq(fifo->channel, rp2_fifo_irq_handler, fifo);
        rp2_fifo_set_enabled(fifo, true);
    } else {
        rp2_dma_set_irq(fifo->channel, NULL, NULL);
        dma_channel_cleanup(fifo->channel);
    }
    ring_t ring;
    rp2_fifo_exchange(fifo, &ring, 0);
}

void rp2_fifo_clear(rp2_fifo_t *fifo) {
    rp2_fifo_start_dma(fifo, false);
    taskENTER_CRITICAL();
    ring_clear(&fifo->ring);
    fifo->trans_count = 0;
    taskEXIT_CRITICAL();
    rp2_fifo_start_dma(fifo, true);
}

void rp2_fifo_set_enabled(rp2_fifo_t *fifo, bool enable) {
    if (fifo->channel < 0) {
        return;
    }
    dma_channel_config c = dma_get_channel_config(fifo->channel);
    channel_config_set_enable(&c, enable);
    dma_channel_set_config(fifo->channel, &c, false);
}

// bool rp2_fifo_get_enabled(rp2_fifo_t *fifo) {
//     dma_channel_config c = dma_get_channel_config(fifo->channel);
//     return channel_config_get_ctrl_value(&c) & DMA_CH0_CTRL_TRIG_EN_BITS;
// }

#ifndef NDEBUG
#include <stdio.h>

void rp2_fifo_debug(const rp2_fifo_t *fifo) {
    taskENTER_CRITICAL();
    ring_t ring = fifo->ring;
    taskEXIT_CRITICAL();

    printf("rp2_fifo %p\n", fifo);
    printf("  tx:          %d\n", fifo->tx);
    printf("  buffer       %p\n", ring.buffer);
    printf("  size:        %u\n", ring.size);
    printf("  next_read:   %u (0x%04x)\n", ring.read_index, ring.read_index % ring.size);
    printf("  next_write:  %u (0x%04x)\n", ring.write_index, ring.write_index % ring.size);
    printf("  trans_count: %u\n", fifo->trans_count);
    printf("  int_count:   %u\n", fifo->int_count);

    if (fifo->channel != -1u) {
        rp2_dma_debug(fifo->channel);
    }
}
#endif
