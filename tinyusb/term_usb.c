// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "tusb.h"
#if CFG_TUD_CDC
#include <errno.h>
#include <malloc.h>
#include <signal.h>
#include <sys/ioctl.h>
#include "morelib/dev.h"
#include "morelib/poll.h"
#include "morelib/termios.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include "tinyusb/cdc_device_cb.h"
#include "tinyusb/term_usb.h"


typedef struct {
    struct poll_file base;
    uint8_t usb_itf;
    SemaphoreHandle_t mutex;
    struct termios termios;
    StaticSemaphore_t xMutexBuffer;
} term_usb_t;

static term_usb_t *terminal_usbs[CFG_TUD_CDC];

static void term_usb_update_line_coding(term_usb_t *file, const cdc_line_coding_t *line_coding) {
    file->termios.c_ispeed = line_coding->bit_rate;
    file->termios.c_ospeed = file->termios.c_ispeed;
}

static void term_usb_tud_cdc_device_cb(void *context, tud_cdc_cb_type_t cb_type, tud_cdc_cb_args_t *cb_args) {
    term_usb_t *file = context;
    uint events = 0;
    xSemaphoreTake(file->mutex, portMAX_DELAY);
    switch (cb_type) {
        case TUD_CDC_RX: {
            if (tud_cdc_n_available(file->usb_itf)) {
                events |= POLLIN | POLLRDNORM;
            }
            break;
        }
        case TUD_CDC_RX_WANTED: {
            if (file->termios.c_lflag & ISIG) {
                kill(0, SIGINT);
            }
            break;
        }
        case TUD_CDC_TX_COMPLETE: {
            if (tud_cdc_n_write_available(file->usb_itf) >= (CFG_TUD_CDC_TX_BUFSIZE / 4)) {
                events |= POLLOUT | POLLWRNORM;
            }
            if (tud_cdc_n_write_available(file->usb_itf) == CFG_TUD_CDC_TX_BUFSIZE) {
                events |= POLLDRAIN;
            }
            break;
        }
        case TUD_CDC_LINE_STATE: {
            if (!tud_cdc_n_connected(file->usb_itf)) {
                tud_cdc_n_write_clear(file->usb_itf);
                events |= POLLOUT | POLLWRNORM | POLLDRAIN;
            }
            break;
        }
        case TUD_CDC_LINE_CODING: {
            term_usb_update_line_coding(file, cb_args->line_coding.p_line_coding);
            break;
        }
        default: {
            break;
        }
    }
    poll_file_notify(&file->base, 0, events);
    xSemaphoreGive(file->mutex);
}

static int term_usb_close(void *ctx) {
    term_usb_t *file = ctx;
    tud_cdc_clear_cb(file->usb_itf);
    vSemaphoreDelete(file->mutex);
    dev_lock();
    assert(terminal_usbs[file->usb_itf] == file);
    terminal_usbs[file->usb_itf] = NULL;
    dev_unlock();
    free(file);
    return 0;
}

static int term_usb_fstat(void *ctx, struct stat *pstat) {
    term_usb_t *file = ctx;
    pstat->st_mode = S_IFCHR;
    pstat->st_rdev = DEV_TTYUSB0 | file->usb_itf;
    return 0;
}

static int term_usb_ioctl(void *ctx, unsigned long request, va_list args) {
    term_usb_t *file = ctx;
    int ret = -1;
    xSemaphoreTake(file->mutex, portMAX_DELAY);
    switch (request) {
        case TCFLSH: {
            tud_cdc_n_write_clear(file->usb_itf);
            tud_cdc_n_read_flush(file->usb_itf);
            poll_file_notify(&file->base, POLLIN | POLLRDNORM, POLLOUT | POLLWRNORM | POLLDRAIN);
            ret = 0;
            break;
        }
        case TCGETS: {
            struct termios *p = va_arg(args, struct termios *);
            *p = file->termios;
            ret = 0;
            break;
        }
        case TCSETS: {
            const struct termios *p = va_arg(args, const struct termios *);
            file->termios = *p;
            ret = 0;
            break;
        }
        default: {
            errno = EINVAL;
            break;
        }
    }
    xSemaphoreGive(file->mutex);
    return ret;
}

static int term_usb_read(void *ctx, void *buffer, size_t size) {
    term_usb_t *file = ctx;
    TickType_t xTicksToWait = portMAX_DELAY;
    int ret;
    do {
        xSemaphoreTake(file->mutex, portMAX_DELAY);
        ret = tud_cdc_n_read(file->usb_itf, buffer, size);
        if (!ret && size) {
            errno = EAGAIN;
            ret = -1;
            poll_file_notify(&file->base, POLLIN | POLLRDNORM, 0);
        }
        xSemaphoreGive(file->mutex);
    }
    while (POLL_CHECK(ret, &file->base, POLLIN, &xTicksToWait));
    return ret;
}

static int term_usb_write(void *ctx, const void *buffer, size_t size) {
    term_usb_t *file = ctx;
    TickType_t xTicksToWait = portMAX_DELAY;
    int ret;
    do {
        xSemaphoreTake(file->mutex, portMAX_DELAY);
        if (!tud_cdc_n_connected(file->usb_itf)) {
            ret = size;
        } else {
            ret = tud_cdc_n_write(file->usb_itf, buffer, size);
            if (!ret && size) {
                errno = EAGAIN;
                ret = -1;
                poll_file_notify(&file->base, POLLOUT | POLLWRNORM, 0);
            }
            else {
                tud_cdc_n_write_flush(file->usb_itf);
            }
        }
        xSemaphoreGive(file->mutex);
    }
    while (POLL_CHECK(ret, &file->base, POLLOUT, &xTicksToWait));    
    return ret;
}

static const struct vfs_file_vtable term_usb_vtable = {
    .close = term_usb_close,
    .fstat = term_usb_fstat,
    .ioctl = term_usb_ioctl,
    .isatty = 1,
    .pollable = 1,
    .read = term_usb_read,
    .write = term_usb_write,
};

static void *term_usb_open(const void *ctx, dev_t dev, int flags) {
    uint usb_itf = minor(dev);
    if (usb_itf >= CFG_TUD_CDC) {
        errno = ENODEV;
        return NULL;
    }

    dev_lock();
    term_usb_t *file = terminal_usbs[usb_itf];
    if (file) {
        poll_file_copy(&file->base);
        goto exit;
    }
    file = calloc(1, sizeof(term_usb_t));
    if (!file) {
        goto exit;
    }
    poll_file_init(&file->base, &term_usb_vtable, O_RDWR, 0);
    file->usb_itf = usb_itf;
    file->mutex = xSemaphoreCreateMutexStatic(&file->xMutexBuffer);
    termios_init(&file->termios, 0);
    cdc_line_coding_t coding;
    tud_cdc_n_get_line_coding(usb_itf, &coding);
    term_usb_update_line_coding(file, &coding);
    if (tud_cdc_n_ready(usb_itf)) {
        tud_cdc_n_read_flush(usb_itf);
    }
    tud_cdc_n_write_clear(usb_itf);
    tud_cdc_n_set_wanted_char(usb_itf, 3);
    tud_cdc_set_cb(usb_itf, term_usb_tud_cdc_device_cb, file);
    terminal_usbs[usb_itf] = file;

exit:
    dev_unlock();
    return file;
}

/**
 * Morelibc terminal driver for USB CDC interfaces.
 */
const struct dev_driver term_usb_drv = {
    .dev = DEV_TTYUSB0,
    .open = term_usb_open,
};
#endif
