// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "FreeRTOS.h"
#include "semphr.h"

#include "device/usbd_pvt.h"
#include "tinyusb/tusb_lock.h"


static SemaphoreHandle_t tud_mutex;

void tud_callback(tusb_cb_func_t func, void *arg) {
    usbd_defer_func(func, arg, false);
}

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
