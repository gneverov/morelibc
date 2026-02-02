// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

/*
 * RP2 GPIO Character Device Driver
 *
 * Linux-compatible GPIO chardev API for RP2040/RP2350 platforms
 */

#pragma once

#include "morelib/dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GPIO chardev driver interface */
extern const struct dev_driver rp2_gpiodev_drv;


#ifdef __cplusplus
}
#endif
