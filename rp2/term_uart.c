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

#include "FreeRTOS.h"
#include "semphr.h"
#include "freertos/interrupts.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "rp2/fifo.h"
#include "rp2/term_uart.h"


typedef struct {
    struct poll_file base;
    uart_inst_t *uart;
    uint tx_pin;
    uint rx_pin;
    SemaphoreHandle_t mutex;
    rp2_fifo_t tx_fifo;
    ring_t rx_fifo;
    struct termios termios;
    StaticSemaphore_t xMutexBuffer;
} term_uart_t;

static term_uart_t *term_uarts[NUM_UARTS];

static void term_uart_handler(uint index) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    term_uart_t *file = term_uarts[index];
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
        if (sig) {
            kill_from_isr(0, sig, &xHigherPriorityTaskWoken);
        }
        else {
            buffer[write_count++] = ch;
        }
    }
    if (write_count) {
        UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
        ring_write(&file->rx_fifo, &buffer, write_count);
        poll_file_notify_from_isr(&file->base, 0, POLLIN | POLLRDNORM, &xHigherPriorityTaskWoken);
        taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void term_uart_handler0(void) {
    term_uart_handler(0);
}

static void term_uart_handler1(void) {
    term_uart_handler(1);
}

#define UART_IRQ_HANDLER(uart) (UART_NUM(uart) ? term_uart_handler1 : term_uart_handler0)

static void term_uart_tx_handler(rp2_fifo_t *fifo, const ring_t *ring, BaseType_t *pxHigherPriorityTaskWoken) {
    term_uart_t *file = (void *)fifo - offsetof(term_uart_t, tx_fifo);
    uint events = 0;
    if (ring_write_count(ring) >= (ring->size / 4)) {
        events |= POLLOUT | POLLWRNORM;
    }
    if (!ring_read_count(ring)) {
        events |= POLLDRAIN;
    }
    if (events) {
        poll_file_notify_from_isr(&file->base, 0, events, pxHigherPriorityTaskWoken);
    }
}

static void term_uart_file_deinit(term_uart_t *file) {
    rp2_fifo_deinit(&file->tx_fifo);
    if (file->uart) {
        UBaseType_t save = set_interrupt_core_affinity();
        irq_set_enabled(UART_IRQ_NUM(file->uart), false);
        irq_remove_handler(UART_IRQ_NUM(file->uart), UART_IRQ_HANDLER(file->uart));
        clear_interrupt_core_affinity(save);

        uart_deinit(file->uart);
        // Pull tx pin high to prevent noise for the receiver
        gpio_set_pulls(file->tx_pin, true, false);
        gpio_deinit(file->tx_pin);
        gpio_deinit(file->rx_pin);
        file->uart = NULL;
    }
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
    pstat->st_mode = S_IFCHR;
    pstat->st_rdev = DEV_TTYS0 | uart_get_index(file->uart);
    return 0;
}

static int term_uart_ioctl(void *ctx, unsigned long request, va_list args) {
    term_uart_t *file = ctx;
    int ret = -1;
    xSemaphoreTake(file->mutex, portMAX_DELAY);
    switch (request) {
        case TCFLSH: {
            rp2_fifo_clear(&file->tx_fifo);
            taskENTER_CRITICAL();
            ring_clear(&file->rx_fifo);
            poll_file_notify(&file->base, POLLIN | POLLRDNORM, POLLOUT | POLLWRNORM | POLLDRAIN);
            taskEXIT_CRITICAL();
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
            uint baudrate = MAX(p->c_ispeed, p->c_ospeed);
            baudrate = uart_set_baudrate(file->uart, baudrate);
            file->termios.c_ispeed = file->termios.c_ospeed = baudrate;
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

static int term_uart_read(void *ctx, void *buffer, size_t size) {
    term_uart_t *file = ctx;
    TickType_t xTicksToWait = portMAX_DELAY;
    int ret;
    do {
        taskENTER_CRITICAL();
        ret = ring_read(&file->rx_fifo, buffer, size);
        if (ring_read_count(&file->rx_fifo) == 0) {
            poll_file_notify(&file->base, POLLIN | POLLRDNORM, 0);
        }
        taskEXIT_CRITICAL();
        if (!ret && size) {
            errno = EAGAIN;
            ret = -1;
        }
    }
    while (POLL_CHECK(ret, &file->base, POLLIN, &xTicksToWait));
    return ret;
}

static int term_uart_write(void *ctx, const void *buffer, size_t size) {
    term_uart_t *file = ctx;
    TickType_t xTicksToWait = portMAX_DELAY;
    int ret;
    do {    
        poll_file_notify(&file->base, POLLOUT | POLLWRNORM, 0);
        ret = rp2_fifo_transfer(&file->tx_fifo, (void *)buffer, size);
        if (ret >= size) {
            poll_file_notify(&file->base, 0, POLLOUT | POLLWRNORM);
        }      
        if (!ret && size) {
            errno = EAGAIN;
            ret = -1;
        }
    }
    while (POLL_CHECK(ret, &file->base, POLLOUT, &xTicksToWait));
    return ret;
}

static const struct vfs_file_vtable term_uart_vtable = {
    .close = term_uart_close,
    .fstat = term_uart_fstat,
    .ioctl = term_uart_ioctl,
    .isatty = 1,
    .pollable = 1,
    .read = term_uart_read,
    .write = term_uart_write,
};

static int term_uart_file_init(term_uart_t *file, uint uart_num, uint tx_pin, uint rx_pin, uint baudrate) {
    poll_file_init(&file->base, &term_uart_vtable, O_RDWR, 0);
    uart_inst_t *uart = UART_INSTANCE(uart_num);
    file->tx_pin = tx_pin;
    file->rx_pin = rx_pin;
    file->mutex = xSemaphoreCreateMutexStatic(&file->xMutexBuffer);
    rp2_fifo_init(&file->tx_fifo, true, term_uart_tx_handler);
    termios_init(&file->termios, baudrate);

    if (!rp2_fifo_alloc(&file->tx_fifo, 512, uart_get_dreq(uart, true), DMA_SIZE_8, false, &uart_get_hw(uart)->dr)) {
        return -1;
    }
    if (!ring_alloc(&file->rx_fifo, 9)) {
        return -1;
    }

    file->uart = uart;
    uart_init(uart, baudrate);
    gpio_set_function(rx_pin, GPIO_FUNC_UART);
    gpio_set_function(tx_pin, GPIO_FUNC_UART);
    UBaseType_t save = set_interrupt_core_affinity();
    irq_set_exclusive_handler(UART_IRQ_NUM(uart), UART_IRQ_HANDLER(uart));
    irq_set_enabled(UART_IRQ_NUM(uart), true);
    uart_set_irqs_enabled(uart, true, false);
    clear_interrupt_core_affinity(save);
    return 0;
}

void *term_uart_open(const void *ctx, dev_t dev, int flags) {
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
        poll_file_copy(&file->base);
        goto exit;
    }
    file = calloc(1, sizeof(term_uart_t));
    if (!file) {
        goto exit;
    }
    if (term_uart_file_init(file, uart_num, tx_pin, rx_pin, baudrate) < 0) {
        term_uart_file_deinit(file);
        free(file);
    }
    term_uarts[uart_num] = file;
exit:
    dev_unlock();
    return file;
}

/**
 * Morelibc terminal driver for UARTs.
 */
const struct dev_driver term_uart_drv = {
    .dev = DEV_TTYS0,
    .open = term_uart_open,
};
