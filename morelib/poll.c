// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <malloc.h>
#include "morelib/poll.h"
#include "morelib/thread.h"

#include "FreeRTOS.h"
#include "task.h"


int poll_wait(TickType_t *pxTicksToWait) {
    TimeOut_t xTimeOut;
    vTaskSetTimeOutState(&xTimeOut);    
    if (thread_enable_interrupt()) {
        return -1;
    }
    uint32_t ret = ulTaskNotifyTake(pdTRUE, *pxTicksToWait);
    thread_disable_interrupt();
    BaseType_t timeOut = xTaskCheckForTimeOut(&xTimeOut, pxTicksToWait);
    if (thread_check_interrupted()) {
        return -1;
    }
    if (timeOut) {
        errno = EAGAIN;
        return -1;
    }
    return ret;    
}

void poll_waiter_init(struct poll_waiter *desc, uint events, poll_notification_t notify) {
    desc->next = NULL;
    desc->events = events | POLLCOM;
    desc->notify = notify;
}

void poll_waiter_add(struct poll_file *file, struct poll_waiter *desc) {
    taskENTER_CRITICAL();
    struct poll_waiter **pdesc = &file->waiters;
    desc->next = *pdesc;
    *pdesc = desc;
    if (desc->events & file->events) {
        desc->notify(desc, NULL);
    }    
    taskEXIT_CRITICAL();
}

void poll_waiter_remove(struct poll_file *file, struct poll_waiter *desc) {
    taskENTER_CRITICAL();
    struct poll_waiter **pdesc = &file->waiters;
    while (*pdesc) {
        if (*pdesc == desc) {
            *pdesc = desc->next;
            desc->next = NULL;
        } else {
            pdesc = &(*pdesc)->next;
        }
    }
    taskEXIT_CRITICAL();
}

void poll_file_init(struct poll_file *file, const struct vfs_file_vtable *func, mode_t mode, uint events) {
    assert(func->pollable);
    vfs_file_init(&file->base, func, mode);
    file->waiters = NULL;
    file->events = events;
}

struct poll_file *poll_file_acquire(int fd, int *flags) {
    struct vfs_file *file = vfs_acquire_file(fd, flags);
    if (!file) {
        return NULL;
    }
    if (file->func->pollable) {
        return (void *)file;        
    }
    vfs_release_file(file);
    errno = EPERM;
    return NULL;
}

void poll_file_notify(struct poll_file *file, uint clear_events, uint set_events) {
    taskENTER_CRITICAL();
    uint new_events = ~file->events & set_events;
    file->events &= ~clear_events;
    file->events |= set_events;
    if (new_events) {
        struct poll_waiter *desc = file->waiters;
        while (desc) {
            if (desc->events & new_events) {
                desc->notify(desc, NULL);
            }
            desc = desc->next;
        }
    }
    taskEXIT_CRITICAL();
}

void poll_file_notify_from_isr(struct poll_file *file, uint clear_events, uint set_events, BaseType_t *pxHigherPriorityTaskWoken) {
    UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
    uint new_events = ~file->events & set_events;
    file->events &= ~clear_events;
    file->events |= set_events;
    if (new_events) {
        struct poll_waiter *desc = file->waiters;
        while (desc) {
            if (desc->events & new_events) {
                desc->notify(desc, pxHigherPriorityTaskWoken);
            }
            desc = desc->next;
        }
    }
    taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
}
struct ppoll_waiter {
    struct poll_waiter base;
    TaskHandle_t task;
    struct poll_file *file;
};

static void ppoll_notify(const void *ptr, BaseType_t *pxHigherPriorityTaskWoken) {
    const struct ppoll_waiter *desc = ptr;
    if (pxHigherPriorityTaskWoken) {
        vTaskNotifyGiveFromISR(desc->task, pxHigherPriorityTaskWoken);
    }
    else {
        xTaskNotifyGive(desc->task);
    }
}

uint poll_file_poll(struct poll_file *file) {
    taskENTER_CRITICAL();
    uint revents = file->events;
    taskEXIT_CRITICAL();
    return revents;
}

int poll_file_wait(struct poll_file *file, uint events, TickType_t *pxTicksToWait) {
    struct ppoll_waiter desc;
    poll_waiter_init(&desc.base, events, ppoll_notify);
    desc.task = xTaskGetCurrentTaskHandle();
    desc.file = NULL;
    ulTaskNotifyTake(pdTRUE, 0);
    poll_waiter_add(file, &desc.base);

    int ret = poll_wait(pxTicksToWait);

    poll_waiter_remove(file, &desc.base);
    return ret;
}


int poll(struct pollfd fds[], nfds_t nfds, int timeout) {
    struct ppoll_waiter *descs = calloc(nfds, sizeof(struct ppoll_waiter));
    if (!descs) {
        return -1;
    }

    ulTaskNotifyTake(pdTRUE, 0);
    size_t num_waiters = 0;
    for (nfds_t i = 0; i < nfds; i++) {
        poll_waiter_init(&descs[i].base, fds[i].events, ppoll_notify);
        fds[i].revents = 0;
        if (fds[i].fd < 0) {
            continue;
        }
        int flags = 0;
        struct vfs_file *file = vfs_acquire_file(fds[i].fd, &flags);
        if (!file) {
            fds[i].revents = POLLNVAL;
            continue;
        }
        if (!file->func->pollable) {
            fds[i].revents = POLLFILE;
            vfs_release_file(file);
            continue;
        }
        descs[i].task = xTaskGetCurrentTaskHandle();
        descs[i].file = (void *)file;
        poll_waiter_add(descs[i].file, &descs[i].base);
        num_waiters++;
    }

    TickType_t xTicksToWait = (timeout < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);
    int ret = num_waiters ? poll_wait(&xTicksToWait) : 0;
    int errcode = errno;

    size_t num_fds = 0;
    for (nfds_t i = 0; i < nfds; i++) {
        if (descs[i].file) {
            fds[i].revents = poll_file_poll(descs[i].file) & fds[i].events;
            poll_waiter_remove(descs[i].file, &descs[i].base);
            poll_file_release(descs[i].file);            
        }
        if (fds[i].revents) {
            num_fds++;
        }
    }
    free(descs);

    if ((ret < 0) && (errcode != EAGAIN)) {
        errno = errcode;
        return ret;
    }
    else {
        return num_fds;
    }
}
