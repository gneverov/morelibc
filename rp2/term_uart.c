// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <malloc.h>
#include <poll.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include "morelib/dev.h"
#include "morelib/poll.h"
#include "morelib/signal.h"
#include "morelib/termios.h"
#include "morelib/vfs.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include "hardware/gpio.h"
#include "rp2/fifo.h"
#include "rp2/term_uart.h"
#include "rp2/uart.h"


typedef struct {
    struct vfs_file base;
    uart_inst_t *uart;
    uint tx_pin;
    uint rx_pin;
    SemaphoreHandle_t mutex;
    pico_fifo_t tx_fifo;
    ring_t rx_fifo;
    struct termios termios;
    uint cooked_state;
    StaticSemaphore_t xMutexBuffer;
} term_uart_t;

static term_uart_t *term_uarts[NUM_UARTS];

static void term_uart_handler(uart_inst_t *uart, void *ctx, BaseType_t *pxHigherPriorityTaskWoken) {
    term_uart_t *file = ctx;
    char buffer[16];
    size_t write_count = 0;
    while (uart_is_readable(file->uart) && (write_count < 16)) {
        uint ch = uart_getc(file->uart);
        int sig = 0;
        if (file->termios.c_lflag & ISIG) {
            switch (ch) {
                case '\003':
                    sig = SIGINT;
                    break;
                case '\032':
                    sig = SIGTSTP;
                    break;
                case '\034':
                    sig = SIGQUIT;
                    break;
            }
        }
        buffer[write_count++] = ch;
        if (sig) {
            kill_from_isr(0, sig, pxHigherPriorityTaskWoken);
        }
    }
    if (write_count) {
        ring_write(&file->rx_fifo, &buffer, write_count);
        poll_notify_from_isr(&file->base, POLLIN | POLLRDNORM, pxHigherPriorityTaskWoken);
    }
}

static void term_uart_tx_handler(pico_fifo_t *fifo, const ring_t *ring, BaseType_t *pxHigherPriorityTaskWoken) {
    term_uart_t *file = (void *)fifo - offsetof(term_uart_t, tx_fifo);
    uint events = 0;
    if (ring_write_count(ring) >= (ring->size / 4)) {
        events |= POLLOUT | POLLWRNORM;
    }
    if (!ring_read_count(ring)) {
        events |= POLLDRAIN;
    }
    if (events) {
        poll_notify_from_isr(&file->base, events, pxHigherPriorityTaskWoken);
    }
}

static void term_uart_file_deinit(term_uart_t *file) {
    if (file->uart) {
        pico_uart_clear_irq(file->uart);
        uart_deinit(file->uart);
        // Pull tx pin high to prevent noise for the receiver
        gpio_set_pulls(file->tx_pin, true, false);
        gpio_deinit(file->tx_pin);
        gpio_deinit(file->rx_pin);
        file->uart = NULL;
    }
    pico_fifo_deinit(&file->tx_fifo);
    ring_free(&file->rx_fifo);
    vSemaphoreDelete(file->mutex);
}

static int term_uart_close(void *ctx) {
    term_uart_t *file = ctx;
    uint index = uart_get_index(file->uart);
    term_uart_file_deinit(file);
    dev_lock();
    assert(term_uarts[index] == file);
    term_uarts[index] = NULL;
    dev_unlock();
    free(file);
    return 0;
}

static int term_uart_fstat(void *ctx, struct stat *pstat) {
    term_uart_t *file = ctx;
    pstat->st_rdev = makedev(major(DEV_TTYS0), uart_get_index(file->uart));
    return 0;
}

static int term_uart_ioctl(void *ctx, unsigned long request, va_list args) {
    term_uart_t *file = ctx;
    int ret = -1;
    xSemaphoreTake(file->mutex, portMAX_DELAY);
    switch (request) {
        case TCFLSH: {
            pico_fifo_clear(&file->tx_fifo);
            file->rx_fifo.read_index = file->rx_fifo.write_index;
            ret = 0;
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

static uint term_uart_poll(void *ctx) {
    term_uart_t *file = ctx;
    uint events = 0;
    if (ring_read_count(&file->rx_fifo)) {
        events |= POLLIN | POLLRDNORM;
    }
    ring_t ring;
    pico_fifo_exchange(&file->tx_fifo, &ring, 0);
    if (ring_write_count(&ring) >= (ring.size / 4)) {
        events |= POLLOUT | POLLWRNORM;
    }
    ;
    if (ring_read_count(&ring)) {
        events |= POLLDRAIN;
    }
    return events;
}

static int term_uart_read(void *ctx, void *buffer, size_t size) {
    term_uart_t *file = ctx;
    xSemaphoreTake(file->mutex, portMAX_DELAY);
    int ret = ring_read(&file->rx_fifo, buffer, size);
    xSemaphoreGive(file->mutex);
    if (!ret) {
        errno = EAGAIN;
        return -1;
    }
    return ret;
}

static int term_uart_write(void *ctx, const void *buffer, size_t size) {
    term_uart_t *file = ctx;
    xSemaphoreTake(file->mutex, portMAX_DELAY);
    int ret = pico_fifo_transfer(&file->tx_fifo, (void *)buffer, size);
    if (!ret) {
        errno = EAGAIN;
        ret = -1;
    }
    xSemaphoreGive(file->mutex);
    return ret;
}

static const struct vfs_file_vtable term_uart_vtable = {
    .close = term_uart_close,
    .fstat = term_uart_fstat,
    .ioctl = term_uart_ioctl,
    .isatty = 1,
    .poll = term_uart_poll,
    .read = term_uart_read,
    .write = term_uart_write,
};

static int term_uart_file_init(term_uart_t *file, uint uart_num, uint tx_pin, uint rx_pin, uint baudrate, int flags, mode_t mode) {
    vfs_file_init(&file->base, &term_uart_vtable, mode | S_IFCHR);
    uart_inst_t *uart = UART_INSTANCE(uart_num);
    file->tx_pin = tx_pin;
    file->rx_pin = rx_pin;
    file->mutex = xSemaphoreCreateMutexStatic(&file->xMutexBuffer);
    pico_fifo_init(&file->tx_fifo, true, term_uart_tx_handler);
    termios_init(&file->termios, baudrate);

    if (!pico_fifo_alloc(&file->tx_fifo, 512, uart_get_dreq(uart, true), DMA_SIZE_8, false, &uart_get_hw(uart)->dr)) {
        return -1;
    }
    if (!ring_alloc(&file->rx_fifo, 9)) {
        return -1;
    }

    file->uart = uart;
    uart_init(uart, baudrate);
    gpio_set_function(rx_pin, GPIO_FUNC_UART);
    gpio_set_function(tx_pin, GPIO_FUNC_UART);
    pico_uart_set_irq(uart, term_uart_handler, file);
    uart_set_irqs_enabled(uart, true, false);
    return 0;
}

void *term_uart_open(const void *ctx, dev_t dev, int flags, mode_t mode) {
    uint uart_num = minor(dev - DEV_TTYS0);
    if (uart_num >= NUM_UARTS) {
        errno = ENODEV;
        return NULL;
    }
    uint tx_pin = PICO_DEFAULT_UART_TX_PIN + 4 * uart_num;
    uint rx_pin = PICO_DEFAULT_UART_RX_PIN + 4 * uart_num;
    uint baudrate = PICO_DEFAULT_UART_BAUD_RATE;

    dev_lock();
    term_uart_t *file = term_uarts[uart_num];
    if (file) {
        vfs_copy_file(&file->base);
        goto exit;
    }
    file = calloc(1, sizeof(term_uart_t));
    if (!file) {
        goto exit;
    }
    if (term_uart_file_init(file, uart_num, tx_pin, rx_pin, baudrate, flags, mode) < 0) {
        term_uart_file_deinit(file);
        free(file);
    }
    term_uarts[uart_num] = file;
exit:
    dev_unlock();
    return file;
}

const struct dev_driver term_uart_drv = {
    .dev = DEV_TTYS0,
    .open = term_uart_open,
};
