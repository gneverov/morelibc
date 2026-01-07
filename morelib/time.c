// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include "morelib/thread.h"

#include "FreeRTOS.h"
#include "task.h"


int clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *rqtp, struct timespec *rmtp) {
    struct timespec now;
    if (flags & TIMER_ABSTIME) {
        if (clock_gettime(clock_id, &now) < 0) {
            return -1;
        }
        timespecsub(rqtp, &now, &now);
        rqtp = &now;
    }

    int ret = 0;
    const long ns_per_tick = 1000000000 / configTICK_RATE_HZ;
    TickType_t xTicksToWait = (rqtp->tv_sec >= 0) ? rqtp->tv_sec * configTICK_RATE_HZ + (rqtp->tv_nsec + ns_per_tick - 1) / ns_per_tick : 0;
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
    if (rmtp && !flags) {
        rmtp->tv_sec = xTicksToWait / configTICK_RATE_HZ;
        rmtp->tv_nsec = xTicksToWait % configTICK_RATE_HZ * ns_per_tick;
    }
    return ret;
}

int nanosleep(const struct timespec *rqtp, struct timespec *rmtp) {
    return clock_nanosleep(CLOCK_REALTIME, 0, rqtp, rmtp);
}

int timespec_get(struct timespec *ts, int base) {
    if (base == TIME_UTC) {
        return clock_gettime(CLOCK_REALTIME, ts);
    }
    errno = EINVAL;
    return -1;
}
