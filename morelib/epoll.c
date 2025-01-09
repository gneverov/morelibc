// SPDX-FileCopyrightText: 2025 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/epoll.h>
#include "morelib/poll.h"
#include "morelib/thread.h"

#include "FreeRTOS.h"
#include "semphr.h"


struct epoll_waiter {
    struct poll_waiter base;            // protected by critical section
    struct epoll_file *ep_file;         // immutable/no protection needed
    int fd;                             // immutable/no protection needed
    struct poll_file *file;             // protected by epoll_file mutex
    epoll_data_t data;                  // protected by epoll_file mutex
    int edges;                          // protected by critical section
};

static void epoll_waiter_deinit(struct epoll_waiter *desc) {
    if (desc->file) {
        poll_waiter_remove(desc->file, &desc->base);
        poll_file_release(desc->file);
        desc->file = NULL;
        desc->fd = -1;
    }
}

struct epoll_file {
    struct poll_file base;
    SemaphoreHandle_t mutex;
    size_t num_descs;
    struct epoll_waiter *descs;
    StaticSemaphore_t xMutexBuffer;
};

static const struct vfs_file_vtable epoll_file_vtable;

static struct epoll_file *epoll_fdopen(int fd) {
    int flags = 0;
    struct epoll_file *ep_file = (void *)vfs_acquire_file(fd, &flags);
    if (ep_file && (ep_file->base.base.func != &epoll_file_vtable)) {
        poll_file_release(&ep_file->base);
        ep_file = NULL;
        errno = EINVAL;
    }
    return ep_file;
}

static int epoll_close(void *ctx) {
    struct epoll_file *ep_file = ctx;
    for (size_t i = 0; i < ep_file->num_descs; i++) {
        epoll_waiter_deinit(ep_file->descs + i);
    }
    free(ep_file->descs);
    vSemaphoreDelete(ep_file->mutex);
    free(ep_file);
    return 0;
}

static void epoll_notify(const void *ptr, BaseType_t *pxHigherPriorityTaskWoken) {
    struct epoll_waiter *desc = (void *)ptr;
    desc->edges++;
    if (pxHigherPriorityTaskWoken) {
        poll_file_notify_from_isr(&desc->ep_file->base, 0, POLLIN, pxHigherPriorityTaskWoken);
    }
    else {
        poll_file_notify(&desc->ep_file->base, 0, POLLIN);
    }
}

static int epoll_ctl_add(struct epoll_file *ep_file, int fd, const struct epoll_event *event) {
    int ret = -1;
    int flags;
    struct poll_file *file = poll_file_acquire(fd, &flags);
    if (!file) {
        return -1;
    }
    if (file == &ep_file->base) {
        errno = EINVAL;
        goto exit2;
    }
    
    xSemaphoreTake(ep_file->mutex, portMAX_DELAY);
    struct epoll_waiter *desc = NULL;
    for (size_t i = 0; i < ep_file->num_descs; i++) {
        if (!desc && (ep_file->descs[i].fd < 0)) {
            desc = &ep_file->descs[i];
        }
        if (ep_file->descs[i].fd == fd) {
            errno = EEXIST;
            goto exit1;
        }
    }
    if (!desc) {
        errno = ENOSPC;
        goto exit1;
    }
    poll_waiter_init(&desc->base, event->events, epoll_notify);
    desc->ep_file = ep_file;
    desc->fd = fd;
    desc->file = poll_file_copy(file);
    desc->data = event->data;
    poll_waiter_add(file, &desc->base);
    ret = 0;
 
exit1:
    xSemaphoreGive(ep_file->mutex);

exit2:
    poll_file_release(file);
    return ret;
}

static int epoll_ctl_del(struct epoll_file *ep_file, int fd) {
    int ret = -1;
    xSemaphoreTake(ep_file->mutex, portMAX_DELAY);
    for (size_t i = 0; i < ep_file->num_descs; i++) {
        struct epoll_waiter *desc = &ep_file->descs[i];
        if (desc->fd == fd) {
            epoll_waiter_deinit(desc);
            ret = 0;
            goto exit;
        }
    }
    errno = ENOENT;

exit:
    xSemaphoreGive(ep_file->mutex);
    return ret;    
}

static int epoll_ctl_mod(struct epoll_file *ep_file, int fd, const struct epoll_event *event) {
    int ret = -1;
    xSemaphoreTake(ep_file->mutex, portMAX_DELAY);
    for (size_t i = 0; i < ep_file->num_descs; i++) {
        struct epoll_waiter *desc = &ep_file->descs[i];
        if (desc->fd == fd) {
            taskENTER_CRITICAL();
            desc->base.events = event->events | POLLCOM;
            taskEXIT_CRITICAL();
            desc->data = event->data;
            ret = 0;
            goto exit;
        }
    }
    errno = ENOENT;

exit:
    xSemaphoreGive(ep_file->mutex);
    return ret;   
}

static const struct vfs_file_vtable epoll_file_vtable = {
    .close = epoll_close,
    .pollable = 1,
};

int epoll_create(int size) {
    struct epoll_file *ep_file = calloc(1, sizeof(struct epoll_file));
    if (!ep_file) {
        return -1;
    }
    int fd = -1;
    poll_file_init(&ep_file->base, &epoll_file_vtable, 0, 0);
    ep_file->mutex = xSemaphoreCreateMutexStatic(&ep_file->xMutexBuffer);
    ep_file->descs = calloc(size, sizeof(struct epoll_waiter));
    if (!ep_file->descs) {
        goto exit;
    }
    ep_file->num_descs = size;
    fd = poll_file_fd(&ep_file->base, FREAD | FWRITE);

exit:
    poll_file_release(&ep_file->base);
    return fd;
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    struct epoll_file *ep_file = epoll_fdopen(epfd);
    if (!ep_file) {
        return -1;
    }
    int ret = -1;
    switch (op) {
        case EPOLL_CTL_ADD: 
            ret = epoll_ctl_add(ep_file, fd, event);
        case EPOLL_CTL_DEL:
            ret = epoll_ctl_del(ep_file, fd);
        case EPOLL_CTL_MOD:
            ret = epoll_ctl_mod(ep_file, fd, event);
        default:
            errno = EINVAL;
    }

    poll_file_release(&ep_file->base);
    return ret;    
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    struct epoll_file *ep_file = epoll_fdopen(epfd);
    if (!ep_file) {
        return -1;
    }

    TickType_t xTicksToWait = (timeout < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);
    int ret;
    do {
        ret = 0;
        poll_file_notify(&ep_file->base, POLLIN, 0);
        xSemaphoreTake(ep_file->mutex, portMAX_DELAY);
        for (size_t i = 0; (i < ep_file->num_descs) && (ret < maxevents); i++) {
            struct epoll_waiter *desc = &ep_file->descs[i];
            if (!desc->file) {
                continue;
            }
            taskENTER_CRITICAL();
            uint revents = poll_file_poll(desc->file) & desc->base.events;
            int triggered = (desc->base.events & EPOLLET) ? desc->edges : revents;
            desc->edges = 0;
            if (triggered && (desc->base.events & EPOLLONESHOT)) {
                desc->base.events = 0;
            }
            taskEXIT_CRITICAL();
            if (triggered) {
                events[ret].events = revents;
                events[ret].data = desc->data;
                ret++;
            }   
        }
        if (ret == 0) {
            errno = EAGAIN;
            ret = -1;
        }
        else {
            poll_file_notify(&ep_file->base, 0, POLLIN);
        }
        xSemaphoreGive(ep_file->mutex);        
    }
    while (POLL_CHECK(0, ret, &ep_file->base, POLLIN, &xTicksToWait));

    poll_file_release(&ep_file->base);
    return ret;
}
