// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <fcntl.h>
#include <poll.h>
#include "morelib/vfs.h"

#include "FreeRTOS.h"

#define POLLFILE (POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM)
#define POLLCOM (POLLERR | POLLHUP | POLLNVAL)


typedef void (*poll_notification_t)(const void *ptr, BaseType_t *pxHigherPriorityTaskWoken);

struct poll_waiter {
    struct poll_waiter *next;
    uint events;
    poll_notification_t notify;
};

struct poll_file {
    struct vfs_file base;
    struct poll_waiter *waiters;
    int events;
};

// generic wait for task notification
int poll_wait(TickType_t *pxTicksToWait);

// waiter functions
void poll_waiter_init(struct poll_waiter *desc, uint events, poll_notification_t notify);
void poll_waiter_add(struct poll_file *file, struct poll_waiter *desc);
void poll_waiter_remove(struct poll_file *file, struct poll_waiter *desc);

// file functions
void poll_file_init(struct poll_file *file, const struct vfs_file_vtable *func, int flags, uint events);
struct poll_file *poll_file_acquire(int fd, int flags);
static inline void poll_file_release(struct poll_file *file) {
    vfs_release_file(&file->base);
}
static inline void *poll_file_copy(struct poll_file *file) {
    return vfs_copy_file(&file->base);
}
static inline int poll_file_fd(struct poll_file *file) {
    return vfs_replace(-1, &file->base);
}
void poll_file_notify_from_isr(struct poll_file *file, uint clear, uint set, BaseType_t *pxHigherPriorityTaskWoken);
static inline void poll_file_notify(struct poll_file *file, uint clear, uint set) {
    poll_file_notify_from_isr(file, clear, set, NULL);
}
uint poll_file_poll(struct poll_file *file);
int poll_file_wait(struct poll_file *file, uint events, TickType_t *pxTicksToWait);

// helper macro
#define POLL_CHECK(ret, file, events, pxTicksToWait) \
    (!((file)->base.flags & FNONBLOCK) && (ret < 0) && (errno == EAGAIN) && ((ret = poll_file_wait(file, events, pxTicksToWait)) >= 0))
