// SPDX-FileCopyrightText: 2025 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <malloc.h>
#include <sys/eventfd.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include "freertos/timers.h"
#include "morelib/poll.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"


struct eventfd {
    struct poll_file base;
    uint64_t value;
};

static int eventfd_read(void *ctx, void *buffer, size_t size) {
    struct eventfd *file = ctx;
    if (size < sizeof(uint64_t)) {
        errno = EINVAL;
        return -1;
    }

    uint64_t old_value;
    TickType_t xTicksToWait = portMAX_DELAY;
    int ret;
    do {
        taskENTER_CRITICAL();
        if (file->value) {
            if (file->base.base.flags & EFD_SEMAPHORE) {
                old_value = 1;
                file->value--;
            }
            else {
                old_value = file->value;
                file->value = 0;
            }
            poll_file_notify(&file->base, file->value ? 0: POLLIN, POLLOUT);
            memcpy(buffer, &old_value, sizeof(uint64_t));
            ret = sizeof(uint64_t);
        }
        else {
            errno = EAGAIN;
            ret = -1;
        }
        taskEXIT_CRITICAL();
    }
    while (POLL_CHECK(ret, &file->base, POLLIN, &xTicksToWait));
    return ret;
}

static int eventfd_write(void *ctx, const void *buffer, size_t size) {
    struct eventfd *file = ctx;
    if (size < sizeof(uint64_t)) {
        errno = EINVAL;
        return -1;
    }
    
    uint64_t new_value;
    memcpy(&new_value, buffer, sizeof(uint64_t));
    TickType_t xTicksToWait = portMAX_DELAY;
    int ret;
    do {
        taskENTER_CRITICAL();
        if (~file->value > new_value) {
            file->value += new_value;
            poll_file_notify(&file->base, ~file->value ? 0 : POLLOUT, POLLIN);
            ret = sizeof(uint64_t);
        }
        else {
            errno = EAGAIN;
            ret = -1;
        }
        taskEXIT_CRITICAL();
    }
    while (POLL_CHECK(ret, &file->base, POLLOUT, &xTicksToWait));
    return ret;
}

static const struct vfs_file_vtable eventfd_vtable = {
    .pollable = 1,
    .read = eventfd_read,
    .write = eventfd_write,
};

int eventfd(unsigned int initval, int flags) {
    struct eventfd *file = calloc(1, sizeof(struct eventfd));
    if (!file) {
        return -1;
    }

    uint events = (initval ? POLLIN : 0) | (~initval ? POLLOUT : 0);
    poll_file_init(&file->base, &eventfd_vtable, O_RDWR | (flags & ~O_ACCMODE), events);
    file->value = initval;

    int ret = poll_file_fd(&file->base);
    poll_file_release(&file->base);
    return ret;
}


struct timerfd {
    struct eventfd base;
    int clockid;
    TickType_t value;
    TickType_t interval;
    TimerHandle_t timer;
    StaticTimer_t xTimerBuffer;
};

int timerfd_close(void *ctx) {
    struct timerfd *file = ctx;
    xTimerDelete(file->timer, portMAX_DELAY);
    xTimerSyncTimerDaemon();
    free(file);
    return 0;
}

static const struct vfs_file_vtable timerfd_vtable = {
    .pollable = 1,
    .close = timerfd_close,
    .read = eventfd_read,
};

static void timerfd_callback(TimerHandle_t xTimer) {
    struct timerfd *file = pvTimerGetTimerID(xTimer);
    assert(file->timer == xTimer);
    taskENTER_CRITICAL();
    file->base.value++;
    poll_file_notify(&file->base.base, 0, POLLIN);
    taskEXIT_CRITICAL();

    if (file->value) {
        if (file->interval) {
            xTimerChangePeriod(xTimer, file->interval, portMAX_DELAY);
        } else {
            xTimerStop(xTimer, portMAX_DELAY);
        }
        file->value = 0;
    }
}

int timerfd_create(int clockid, int flags) {
    struct timespec now;
    if (clock_gettime(clockid, &now) < 0) {
        return -1;
    }

    struct timerfd *file = calloc(1, sizeof(struct timerfd));
    if (!file) {
        return -1;
    }

    poll_file_init(&file->base.base, &timerfd_vtable, O_RDWR | (flags & ~O_ACCMODE), 0);
    file->timer = xTimerCreateStatic("timerfd", portMAX_DELAY, pdFALSE, file, timerfd_callback, &file->xTimerBuffer);
    file->clockid = clockid;

    int ret = poll_file_fd(&file->base.base);
    poll_file_release(&file->base.base);
    return ret;
}

static struct timerfd *timerfd_from_fd(int fd) {
    struct timerfd *file = (struct timerfd *)poll_file_acquire(fd, FREAD | FWRITE);
    if (!file) {
        return NULL;
    }
    if (file->base.base.base.func != &timerfd_vtable) {
        errno = EINVAL;
        poll_file_release(&file->base.base);
        return NULL;
    }
    return file;
}

static TickType_t timespec_to_ticks(const struct timespec *ts) {
    const long ns_per_tick = 1000000000 / configTICK_RATE_HZ;
    return (ts->tv_sec >= 0) ? ts->tv_sec * configTICK_RATE_HZ + (ts->tv_nsec + ns_per_tick - 1) / ns_per_tick : 0;
}

static void ticks_to_timespec(TickType_t ticks, struct timespec *ts) {
    const long ns_per_tick = 1000000000 / configTICK_RATE_HZ;
    ts->tv_sec = ticks / configTICK_RATE_HZ;
    ts->tv_nsec = (ticks % configTICK_RATE_HZ) * ns_per_tick;
}

static void timerfd_gettime_internal(struct timerfd *file, struct itimerspec *value) {
    memset(value, 0, sizeof(*value));
    if (file->timer && xTimerIsTimerActive(file->timer)) {
        TickType_t ticks = xTimerGetExpiryTime(file->timer) - xTaskGetTickCount();
        ticks_to_timespec(ticks, &value->it_value);
        ticks_to_timespec(xTimerGetPeriod(file->timer), &value->it_interval);
    }
}

int timerfd_settime(int fd, int flags, const struct itimerspec *new_value, struct itimerspec *old_value) {
    struct timerfd *file = timerfd_from_fd(fd);
    if (!file) {
        return -1;
    }

    struct timespec ts = new_value->it_value;
    if (flags & TFD_TIMER_ABSTIME) {
        if (clock_gettime(file->clockid, &ts) < 0) {
            return -1;
        }
        timespecsub(&new_value->it_value, &ts, &ts);
    }

    if (old_value) {
        timerfd_gettime_internal(file, old_value);
    }

    xTimerStop(file->timer, portMAX_DELAY);
    xTimerSyncTimerDaemon();

    file->value = timespec_to_ticks(&ts);
    file->interval = timespec_to_ticks(&new_value->it_interval);
    if (file->value || file->interval) {
        file->timer = xTimerCreateStatic("timerfd", file->value ? file->value : file->interval, file->interval ? pdTRUE : pdFALSE, file, timerfd_callback, &file->xTimerBuffer);
        xTimerStart(file->timer, portMAX_DELAY);
    }

    return 0;
}

int timerfd_gettime(int fd, struct itimerspec *curr_value) {
    struct timerfd *file = timerfd_from_fd(fd);
    if (!file) {
        return -1;
    }
    timerfd_gettime_internal(file, curr_value);
    return 0;
}