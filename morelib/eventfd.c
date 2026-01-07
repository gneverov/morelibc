// SPDX-FileCopyrightText: 2025 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <malloc.h>
#include <sys/eventfd.h>
#include "morelib/poll.h"

#include "FreeRTOS.h"
#include "task.h"


struct eventfd {
    struct poll_file base;
    int flags;
    uint64_t value;
};

static int eventfd_read(void *ctx, void *buffer, size_t size) {
    struct eventfd *file = ctx;
    if (size < sizeof(uint64_t)) {
        errno = EINVAL;
        return -1;
    }

    uint64_t *old_value = buffer;
    TickType_t xTicksToWait = portMAX_DELAY;
    int ret;
    do {
        taskENTER_CRITICAL();
        if (file->value) {
            if (file->flags & EFD_SEMAPHORE) {
                *old_value = 1;
                file->value--;
            }
            else {
                *old_value = file->value;
                file->value = 0;
            }
            poll_file_notify(&file->base, file->value ? 0: POLLIN, POLLOUT);
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
    
    uint64_t new_value = *(uint64_t *)buffer;
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
    struct eventfd *file = malloc(sizeof(struct eventfd));
    if (!file) {
        return -1;
    }

    uint events = (initval ? POLLIN : 0) | (~initval ? POLLOUT : 0);
    poll_file_init(&file->base, &eventfd_vtable, O_RDWR | (flags & EFD_NONBLOCK ? O_NONBLOCK : 0), events);
    file->flags = flags;
    file->value = initval;

    int ret = poll_file_fd(&file->base);
    poll_file_release(&file->base);
    return ret;
}
