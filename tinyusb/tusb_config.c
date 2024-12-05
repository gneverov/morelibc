// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

/**
 * Module to support TinyUSB callbacks for device descriptors.
 * 
 * The application must provide the symbol `tusb_default_config` which contains the default USB 
 * device configuration. If the application does not need to dynamically change the USB device 
 * configuration, then it need not do anything more. Otherwise the application may change the USB
 * device configuration by calling functions in this module.
 * 
 * Calling `tusb_config_set` sets a new USB device configuration defined by the application. The 
 * configuration is additionally persisted in flash memory in the "flash_env" section and will 
 * be used instead of the default configuration at reset. Calling `tusb_config_clear` will restore
 * the default configuration.
 * 
 * This module implements the TinyUSB callbacks for device descriptors based on the current USB 
 * device configuration set by this module.
 */

#include "morelib/crc.h"
#include "morelib/flash.h"
#include "tinyusb/tusb_config.h"


// Storage location of the dynamic USB device config in the "flash env" section.
__attribute__((section(".flash_env.tinyusb")))
static const volatile tusb_config_t tusb_flash_config;

static const tusb_config_t *tusb_config = &tusb_default_config;

/**
 * Gets the current USB device config.
 */
__attribute__((visibility("hidden")))
const tusb_config_t *tusb_config_get(void) {
    return tusb_config;
}

/**
 * Sets the current USB device config and stores it in flash.
 */
__attribute__((visibility("hidden")))
int tusb_config_set(tusb_config_t *config) {
    bool connected = tud_connected();
    tud_disconnect();

    const tusb_config_t *flash_config = (const tusb_config_t *)&tusb_flash_config;
    config->crc = crc32(CRC32_INITIAL, (void *)config, offsetof(tusb_config_t, crc));
    int ret = flash_copy(&tusb_flash_config, config, sizeof(tusb_config_t));
    if (ret >= 0) {
        tusb_config = flash_config;
    }

    if (connected) {
        tud_connect();
    }
    return ret;
}

/**
 * Restores the current USB device config and from flash.
 */
__attribute__((constructor, visibility("hidden")))
void tusb_config_restore(void) {
    assert(!tud_connected());

    const tusb_config_t *flash_config = (const tusb_config_t *)&tusb_flash_config;
    uint32_t crc = crc32(CRC32_INITIAL, (const void *)flash_config, sizeof(tusb_config_t));
    if (crc == CRC32_CHECK) {
        tusb_config = flash_config;
    }
}

/**
 * Clears the USB device config in flash and resets the current config to the default.
 */
__attribute__((visibility("hidden")))
int tusb_config_clear(void) {
    bool connected = tud_connected();
    tud_disconnect();

    tusb_config = &tusb_default_config;
    uint32_t crc = 0;
    int ret = flash_copy(&tusb_flash_config.crc, &crc, sizeof(uint32_t));

    if (connected) {
        tud_connect();
    }
    return ret;
}


// TinyUSB static callback functions
__attribute__((visibility("hidden")))
const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&tusb_config->device;
}

__attribute__((visibility("hidden")))
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    const uint8_t *cfg = tusb_config->configs;
    while (index) {
        cfg += ((tusb_desc_configuration_t *)cfg)->wTotalLength;
        index--;
    }
    return cfg;
}

__attribute__((visibility("hidden")))
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    const uint16_t *str = tusb_config->strings;
    while (index) {
        str += ((tusb_desc_string_t *)str)->bLength / sizeof(uint16_t);
        index--;
    }
    return str;
}
