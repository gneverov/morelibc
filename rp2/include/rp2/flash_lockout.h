// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "FreeRTOS.h"


#if configNUMBER_OF_CORES == 1
static inline void flash_lockout_start(void) {
}

static inline void flash_lockout_end(void) {
}

static inline void flash_lockout_init(void) {
}
#else
void flash_lockout_init(void);

void flash_lockout_start(void);

void flash_lockout_end(void);
#endif