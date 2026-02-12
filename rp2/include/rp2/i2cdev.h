// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

/*
 * RP2 I2C Device Driver
 *
 * Linux-compatible I2C device driver (/dev/i2c-X) for RP2040/RP2350
 */

#pragma once

#include "morelib/dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2C device driver interface */
extern const struct dev_driver rp2_i2cdev_drv;

#ifdef __cplusplus
}
#endif
