// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "tusb.h"

#ifndef TUSB_CONFIG_CFG_SIZE
/**
 * Amount of space in bytes used to store USB device configuration descriptors.
 */
#define TUSB_CONFIG_CFG_SIZE 512
#endif

#ifndef TUSB_CONFIG_STR_SIZE
/**
 * Amount of space in 2-byte words used to store USB device string descriptors.
 */
#define TUSB_CONFIG_STR_SIZE 256
#endif


/**
 * Struct used to store the USB device config.
 */
typedef struct tusb_config {
    /**
     * Device descriptor
     */
    tusb_desc_device_t device;
    /**
     * Packed configuration descriptors
     */
    uint8_t configs[TUSB_CONFIG_CFG_SIZE];
    /**
     * Packed string descriptors
     */
    uint16_t strings[TUSB_CONFIG_STR_SIZE];
#if CFG_TUD_MSC
    uint8_t msc_vendor_id[8];
    uint8_t msc_product_id[16];
    uint8_t msc_product_rev[4];
#endif
    /**
     * CRC checksum of struct (computed by library)
     */
    uint32_t crc;
} tusb_config_t;

/**
 * Default USB device config (defined by app).
 */
extern const tusb_config_t tusb_default_config;

const tusb_config_t *tusb_config_get(void);

int tusb_config_set(tusb_config_t *config);

int tusb_config_clear(void);

