// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "FreeRTOS.h"
#include "semphr.h"

#include "hardware/i2c.h"


typedef struct rp2_i2c {
    i2c_inst_t *inst;
    SemaphoreHandle_t mutex;
    StaticSemaphore_t buffer;
} rp2_i2c_t;

extern rp2_i2c_t rp2_i2cs[NUM_I2CS];

BaseType_t rp2_i2c_take(rp2_i2c_t *i2c, TickType_t xBlockTime);

BaseType_t rp2_i2c_give(rp2_i2c_t *i2c);
