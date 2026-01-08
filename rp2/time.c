// SPDX-FileCopyrightText: 2025 Gregory Neverov
// SPDX-License-Identifier: MIT

#define _POSIX_CPUTIME
#define _POSIX_THREAD_CPUTIME
#include <sys/types.h>

#include <errno.h>
#include <time.h>
#include <sys/times.h>

#include "FreeRTOS.h"
#include "task.h"

#include "hardware/timer.h"
#include "pico/aon_timer.h"


static uint64_t epoch_time_at_boot_us;

int clock_getres(clockid_t clock_id, struct timespec *res) {
    switch (clock_id) {
        case CLOCK_REALTIME:
        case CLOCK_MONOTONIC:
        #if configGENERATE_RUN_TIME_STATS
        case CLOCK_PROCESS_CPUTIME_ID:
        case CLOCK_THREAD_CPUTIME_ID:
        #endif
            if (res) {
                res->tv_sec = 0;
                res->tv_nsec = 1000;
            }
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int clock_gettime(clockid_t clock_id, struct timespec *tp) {
    uint64_t time_us = time_us_64();
    switch (clock_id) {
        case CLOCK_MONOTONIC:
            break;

        case CLOCK_REALTIME:
            time_us += epoch_time_at_boot_us;
            break;

        #if configGENERATE_RUN_TIME_STATS
        case CLOCK_PROCESS_CPUTIME_ID:
            time_us = (configRUN_TIME_COUNTER_TYPE)time_us;
            time_us -= ulTaskGetIdleRunTimeCounter() / configNUMBER_OF_CORES;
            break;

        case CLOCK_THREAD_CPUTIME_ID:
            time_us = ulTaskGetRunTimeCounter(NULL);
            break;
        #endif

        default:
            errno = EINVAL;
            return -1;
    }
    tp->tv_sec = time_us / 1000000;
    tp->tv_nsec = (time_us % 1000000) * 1000;
    return 0;
}

int clock_settime(clockid_t clock_id, const struct timespec *tp) {
    if (clock_id != CLOCK_REALTIME) {
        errno = EINVAL;
        return -1;
    }
    
    uint64_t epoch_time_now_us = (tp->tv_sec * 1000000ll) + (tp->tv_nsec / 1000);
    epoch_time_at_boot_us = epoch_time_now_us - time_us_64();

    if (aon_timer_is_running()) {
        aon_timer_set_time(tp);
    } else {
        aon_timer_start(tp);
    }
    return 0;
}

__attribute__((constructor, visibility("hidden")))
void clock_init(void) {
    if (aon_timer_is_running()) {
        struct timespec tp;
        aon_timer_get_time(&tp);

        uint64_t epoch_time_now_us = (tp.tv_sec * 1000000ll) + (tp.tv_nsec / 1000);
        epoch_time_at_boot_us = epoch_time_now_us - time_us_64();
    }
}

clock_t times(struct tms *tms) {
    uint32_t elapsed = time_us_32();
    uint32_t idle = elapsed;
    #if configGENERATE_RUN_TIME_STATS
    idle = ulTaskGetIdleRunTimeCounter() / configNUMBER_OF_CORES;
    #endif
    tms->tms_utime = 0;
    tms->tms_stime = elapsed - idle;
    tms->tms_cutime = 0;
    tms->tms_cstime = 0;
    return elapsed;
}