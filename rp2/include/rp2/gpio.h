// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "hardware/gpio.h"


typedef void (*rp2_gpio_handler_t)(uint gpio, uint32_t event_mask, void *context);

void rp2_gpio_set_irq_enabled(uint gpio, uint32_t events, bool enabled);

bool rp2_gpio_add_handler(uint gpio, rp2_gpio_handler_t handler, void *context);

bool rp2_gpio_remove_handler(uint gpio);

void rp2_gpio_debug(uint gpio);
