// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "morelib/poll.h"
#include "morelib/ring.h"

#include "FreeRTOS.h"

#include "hardware/dma.h"


typedef struct rp2_fifo rp2_fifo_t;

typedef void (*rp2_fifo_handler_t)(rp2_fifo_t *fifo, const ring_t *ring, BaseType_t *pxHigherPriorityTaskWoken);

struct rp2_fifo {
    ring_t ring;
    int channel;
    uint trans_count;
    enum dma_channel_transfer_size transfer_size;
    bool tx;
    rp2_fifo_handler_t handler;
    uint int_count;
};

void rp2_fifo_init(rp2_fifo_t *fifo, bool tx, rp2_fifo_handler_t handler);

bool rp2_fifo_alloc(rp2_fifo_t *fifo, uint fifo_size, uint dreq, enum dma_channel_transfer_size dma_transfer_size, bool bswap, volatile void *target_addr);

void rp2_fifo_deinit(rp2_fifo_t *fifo);

void rp2_fifo_exchange(rp2_fifo_t *fifo, ring_t *ring, size_t usr_count);

void rp2_fifo_exchange_from_isr(rp2_fifo_t *fifo, ring_t *ring, size_t usr_count, BaseType_t *pxHigherPriorityTaskWoken);

size_t rp2_fifo_transfer(rp2_fifo_t *fifo, void *buffer, size_t size);

int rp2_fifo_read(struct poll_file *file, rp2_fifo_t *fifo, void *buffer, size_t size, TickType_t *pxTicksToWait);

int rp2_fifo_write(struct poll_file *file, rp2_fifo_t *fifo, const void *buffer, size_t size, TickType_t *pxTicksToWait);

void rp2_fifo_clear(rp2_fifo_t *fifo);

void rp2_fifo_set_enabled(rp2_fifo_t *fifo, bool enable);

// bool rp2_fifo_get_enabled(rp2_fifo_t *fifo);

#ifndef NDEBUG
void rp2_fifo_debug(const rp2_fifo_t *fifo);
#endif
