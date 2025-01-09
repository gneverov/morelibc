// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <malloc.h>
#include "morelib/ioctl.h"
#include "morelib/poll.h"
#include "morelib/termios.h"

#include "FreeRTOS.h"
#include "semphr.h"


struct term_mux_waiter {
    struct poll_waiter base;
    struct poll_file *mux_file;
    struct poll_file *term_file;
    size_t remaining;
    struct term_mux_waiter *next;
};

static void term_mux_desc_deinit(struct term_mux_waiter *desc) {
    poll_waiter_remove(desc->term_file, &desc->base);
    poll_file_release(desc->term_file);
    free(desc);
}

typedef struct {
    struct poll_file base;
    SemaphoreHandle_t mutex;
    struct term_mux_waiter *descs;    
    struct termios termios;
    StaticSemaphore_t xMutexBuffer;
} term_mux_t;


static int term_mux_close(void *ctx) {
    term_mux_t *file = ctx;
    struct term_mux_waiter *desc = file->descs;
    while (desc) {
        struct term_mux_waiter *next = desc->next;
        term_mux_desc_deinit(desc);
        desc = next;
    }
    vSemaphoreDelete(file->mutex);
    free(file);
    return 0;
}

static int term_mux_set_termios(term_mux_t *file, const struct termios *p) {
    xSemaphoreTake(file->mutex, portMAX_DELAY);
    file->termios = *p;

    struct term_mux_waiter *desc = file->descs;
    while (desc) {
        struct termios termios;
        vfs_ioctl(&desc->term_file->base, TCGETS, &termios);
        termios.c_lflag = (p->c_lflag & ISIG) | (termios.c_lflag & ~ISIG);
        vfs_ioctl(&desc->term_file->base, TCSETS, &termios);
        desc = desc->next;
    }
    xSemaphoreGive(file->mutex);
    return 0;
}

static void term_mux_notify(const void *ptr, BaseType_t *pxHigherPriorityTaskWoken) {
    const struct term_mux_waiter *desc = ptr;
    if (pxHigherPriorityTaskWoken) {
        poll_file_notify_from_isr(desc->mux_file, 0, POLLIN, pxHigherPriorityTaskWoken);
    }
    else {
        poll_file_notify(desc->mux_file, 0, POLLIN);
    }
}

static int term_mux_add(term_mux_t *file, int fd) {
    int flags = FREAD | FWRITE;
    struct poll_file *term_file = poll_file_acquire(fd, &flags);
    if (!term_file) {
        return -1;
    }
    int ret = -1;
    if (!term_file->base.func->isatty) {
        errno = ENOTTY;
        goto exit2;
    }    
    struct term_mux_waiter **tail = &file->descs;
    xSemaphoreTake(file->mutex, portMAX_DELAY);
    while (*tail) {
        if ((*tail)->term_file == term_file) {
            errno = EEXIST;
            goto exit1;
        }
        tail = &(*tail)->next;
    }
    struct term_mux_waiter *desc = calloc(1, sizeof(struct term_mux_waiter));
    if (!desc) {
        goto exit1;
    }

    poll_waiter_init(&desc->base, POLLIN, term_mux_notify);
    desc->mux_file = &file->base;
    desc->term_file = poll_file_copy(term_file);
    poll_waiter_add(term_file, &desc->base);
    desc->next = NULL;
    *tail = desc;
    ret = 0;

exit1:
    xSemaphoreGive(file->mutex);

exit2:
    poll_file_release(term_file);
    return ret;
}

static int term_mux_remove(term_mux_t *file, struct term_mux_waiter *desc) {
    struct term_mux_waiter **tail = &file->descs;
    int ret = -1;
    // xSemaphoreTake(file->mutex, portMAX_DELAY);
    while (*tail) {
        if (*tail == desc) {
            *tail = desc->next;
            term_mux_desc_deinit(desc);
            ret = 0;
            goto exit;
        }
        tail = &(*tail)->next;
    }
    errno = ENOENT;

exit:
    // xSemaphoreGive(file->mutex);
    return ret;
}

static int term_mux_ioctl(void *ctx, unsigned long request, va_list args) {
    term_mux_t *file = ctx;
    int ret = -1;
    switch (request) {
        case TCGETS: {
            struct termios *p = va_arg(args, struct termios *);
            xSemaphoreTake(file->mutex, portMAX_DELAY);
            *p = file->termios;
            xSemaphoreGive(file->mutex);
            ret = 0;
            break;
        }
        case TCSETS: {
            const struct termios *p = va_arg(args, const struct termios *);
            ret = term_mux_set_termios(file, p);
            break;
        }
        case TMUX_ADD: {
            int fd = va_arg(args, int);
            ret = term_mux_add(file, fd);
            break;
        }
        // case TMUX_REMOVE: {
        //     int fd = va_arg(args, int);
        //     ret = term_mux_remove(file, fd);
        //     break;
        // }
        default: {
            errno = EINVAL;
            break;
        }
    }
    return ret;
}

static int term_mux_read(void *ctx, void *buffer, size_t size, int flags) {
    term_mux_t *file = ctx;
    TickType_t xTicksToWait = portMAX_DELAY;
    int ret;
    do {
        ret = -1;
        poll_file_notify(&file->base, POLLIN, 0);
        xSemaphoreTake(file->mutex, portMAX_DELAY);
        struct term_mux_waiter *desc = file->descs;
        while (desc) {
            struct term_mux_waiter *next = desc->next;
            ret = vfs_read(&desc->term_file->base, buffer, size, flags | FNONBLOCK);
            if ((ret > 0) || (ret == size)) {
                break;
            }
            if (!ret || (errno != EAGAIN)) {
                term_mux_remove(file, desc);
                ret = -1;
            }
            desc = next;
        }
        xSemaphoreGive(file->mutex);
        if (ret < 0) {
            errno = EAGAIN;
        }
        else {
            poll_file_notify(&file->base, 0, POLLIN);
        }
    }
    while (POLL_CHECK(flags, ret, &file->base, POLLIN, &xTicksToWait));
    return ret;
}

static int term_mux_write_one(term_mux_t *file, struct term_mux_waiter *desc, const void *buffer, size_t size, int flags) {
    int ret = vfs_write(&desc->term_file->base, buffer, size, flags);
    if (ret < 0) {
        term_mux_remove(file, desc);
    }
    return ret;
}

static int term_mux_write(void *ctx, const void *buffer, size_t size, int flags) {
    term_mux_t *file = ctx;
    xSemaphoreTake(file->mutex, portMAX_DELAY);
    // Iterate through terminals until one write succeeds. The amount written to this first 
    // terminal shall be the amount that must be written to the remaining terminals, even if they 
    // initially don't accept all the data.
    struct term_mux_waiter *desc = file->descs;
    while (desc) {
        desc->remaining = size;
        struct term_mux_waiter *next = desc->next;
        if (desc == file->descs) {
            int ret = term_mux_write_one(file, desc, buffer, size, flags);
            if (ret >= 0) {
                size = ret;
                desc->remaining = 0;
            }        
        }
        desc = next;
    }

    // Keep iterating through the terminals until all the data has been written to all the 
    // terminals. Note that "size" has been modified to the size written to the first terminal.
    size_t count;
    do {
        count = 0;
        desc = file->descs;
        while (desc) {
            struct term_mux_waiter *next = desc->next;
            if (desc->remaining) {
                int ret = term_mux_write_one(file, desc, buffer + size - desc->remaining, desc->remaining, flags);
                if (ret >= 0) {
                    desc->remaining -= ret;
                    if (desc->remaining) {
                        count++;
                    }                 
                }
            }
            desc = next;
        }
    }
    while (count);

    xSemaphoreGive(file->mutex);
    return size;
}

static const struct vfs_file_vtable term_mux_vtable = {
    .close = term_mux_close,
    .ioctl = term_mux_ioctl,
    .isatty = 1,
    .pollable = 1,
    .read = term_mux_read,
    .write = term_mux_write,
};

__attribute__((visibility("hidden")))
void *term_mux_open(const void *ctx, dev_t dev, mode_t mode) {
    term_mux_t *file = calloc(1, sizeof(term_mux_t));
    if (!file) {
        return NULL;
    }
    poll_file_init(&file->base, &term_mux_vtable, mode | S_IFCHR, 0);
    file->mutex = xSemaphoreCreateMutexStatic(&file->xMutexBuffer);
    termios_init(&file->termios, 0);
    return file;
}
