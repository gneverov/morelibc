// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <time.h>
#include "morelib/thread.h"

#include "FreeRTOS.h"
#include "task.h"


int nanosleep(const struct timespec *rqtp, struct timespec *rmtp) {
    int ret = 0;
    TickType_t xTicksToWait = rqtp->tv_sec * configTICK_RATE_HZ + (rqtp->tv_nsec * configTICK_RATE_HZ + 999999999) / 1000000000;
    TimeOut_t xTimeOut;
    vTaskSetTimeOutState(&xTimeOut);
    while (!xTaskCheckForTimeOut(&xTimeOut, &xTicksToWait)) {
        if (thread_enable_interrupt()) {
            ret = -1;
            break;
        }
        vTaskDelay(xTicksToWait);
        thread_disable_interrupt();
    }
    if (rmtp) {
        rmtp->tv_sec = xTicksToWait / configTICK_RATE_HZ;
        rmtp->tv_nsec = xTicksToWait % configTICK_RATE_HZ * (1000000000 / configTICK_RATE_HZ);
    }
    return ret;
}
