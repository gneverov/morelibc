// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

/*
 * RP2 SPI Device Driver
 *
 * Linux-compatible SPI device driver (/dev/spidevX.Y) for RP2040/RP2350
 */

#pragma once

#include "morelib/dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SPI device driver interface */
extern const struct dev_driver rp2_spidev_drv;

#ifdef __cplusplus
}
#endif
