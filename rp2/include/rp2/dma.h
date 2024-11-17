// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "FreeRTOS.h"

#include "hardware/dma.h"


typedef void (*rp2_dma_handler_t)(uint channel, void *context, BaseType_t *pxHigherPriorityTaskWoken);

void rp2_dma_init(void);

void rp2_dma_set_irq(uint channel, rp2_dma_handler_t handler, void *context);

void rp2_dma_clear_irq(uint channel);

void rp2_dma_acknowledge_irq(uint channel);

void rp2_dma_debug(uint channel);
