// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "FreeRTOS.h"
#include "semphr.h"

#include "tinyusb/tusb_lock.h"


static SemaphoreHandle_t tud_mutex;

__attribute__((constructor, visibility("hidden")))
void tud_lock_init(void) {
    static StaticSemaphore_t xMutexBuffer;
    tud_mutex = xSemaphoreCreateMutexStatic(&xMutexBuffer);
}

void tud_lock(void) {
    xSemaphoreTake(tud_mutex, portMAX_DELAY);
}

void tud_unlock(void) {
    xSemaphoreGive(tud_mutex);
}
