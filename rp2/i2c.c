// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

/**
 * Module to synchronize access to an I2C bus.
 *
 * Only one thread can perform operations on an I2C instance at the same time. A regular mutex is
 * used to synchronize this access. Unlike the SPI module, I2C does not need ISR ownership transfer
 * as the RP2040/RP2350 I2C hardware uses blocking transfers.
 */

#include "rp2/i2c.h"


/**
 * Array of mutex-protected I2C instances.
 */
rp2_i2c_t rp2_i2cs[NUM_I2CS] = {
    { .inst = i2c0, 0 },
    { .inst = i2c1, 0 },
};

__attribute__((constructor, visibility("hidden")))
void rp2_i2c_init(void) {
    for (int i = 0; i < NUM_I2CS; i++) {
        rp2_i2cs[i].mutex = xSemaphoreCreateMutexStatic(&rp2_i2cs[i].buffer);
    }
}

/**
 * Take ownership of an I2C bus.
 *
 * Args:
 * i2c: I2C bus to take
 * xBlockTime: time to wait to acquire bus
 *
 * Returns:
 * true if bus was acquired, otherwise false
 */
BaseType_t rp2_i2c_take(rp2_i2c_t *i2c, TickType_t xBlockTime) {
    return xSemaphoreTake(i2c->mutex, xBlockTime);
}

/**
 * Give up ownership of an I2C bus.
 *
 * The calling thread must have already acquired ownership by calling rp2_i2c_take. After calling
 * this function, the calling thread no longer owns the bus.
 *
 * Args:
 * i2c: the I2C bus to give
 *
 * Returns:
 * true
 */
BaseType_t rp2_i2c_give(rp2_i2c_t *i2c) {
    return xSemaphoreGive(i2c->mutex);
}
