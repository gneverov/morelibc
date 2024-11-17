// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "hardware/spi.h"


typedef struct rp2_spi {
    spi_inst_t *inst;
    SemaphoreHandle_t mutex;
    TaskHandle_t mutex_holder;
    BaseType_t in_isr;
    StaticSemaphore_t buffer;
} rp2_spi_t;

extern rp2_spi_t rp2_spis[NUM_SPIS];

BaseType_t rp2_spi_take(rp2_spi_t *spi, TickType_t xBlockTime);

BaseType_t rp2_spi_take_to_isr(rp2_spi_t *spi);

void rp2_spi_give_from_isr(rp2_spi_t *spi, BaseType_t *pxHigherPriorityTaskWoken);

BaseType_t rp2_spi_give(rp2_spi_t *spi);
