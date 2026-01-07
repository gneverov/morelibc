// SPDX-FileCopyrightText: 2025 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "FreeRTOS.h"
#include "semphr.h"
#include "timers.h"

#include "freertos/timers.h"


#if configUSE_TIMERS
static void xTimerSyncTimerDaemon_cb(void *arg1, uint32_t arg2) {
    SemaphoreHandle_t sem = arg1;
    xSemaphoreGive(sem);
}

/**
 * Sends a message to the timer daemon and waits for it to be processed.
 * 
 * This ensures that all previously sent messages (e.g., stop, delete) have also been processed.
 */
void xTimerSyncTimerDaemon(void) {
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    xTimerPendFunctionCall(xTimerSyncTimerDaemon_cb, sem, 0, portMAX_DELAY);
    xSemaphoreTake(sem, portMAX_DELAY);
    vSemaphoreDelete(sem);
}
#endif