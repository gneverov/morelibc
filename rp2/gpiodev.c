// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

/*
 * RP2 GPIO Character Device Driver
 *
 * Linux-compatible GPIO chardev API implementation for RP2040/RP2350
 */

#include <errno.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include "freertos/interrupts.h"

#include "FreeRTOS.h"
#include "task.h"

#include "hardware/gpio.h"
#include "pico/time.h"

#include "morelib/gpio.h"
#include "morelib/poll.h"
#include "morelib/ring.h"
#include "rp2/gpio.h"
#include "rp2/gpiodev.h"


//=============================================================================
// Data Structures
//=============================================================================

// Chip file/state (singleton representing /dev/gpiochip0)
struct rp2_gpiodev_chip {
    struct vfs_file base;
};

struct rp2_gpiodev_line {
    uint gpio;
    alarm_id_t alarm_id;
    uint32_t debounce_time_us;
    uint32_t seqno;
    uint16_t event_mask;
    uint8_t last_value;
};

// Line request file (returned from GPIO_V2_GET_LINE_IOCTL)
struct rp2_gpiodev_req {
    struct poll_file base;
    uint32_t num_lines;

    // Event handling
    ring_t event_buffer;
    uint32_t seqno;

    struct rp2_gpiodev_line lines[];
};

//=============================================================================
// Chip Operations
//=============================================================================

static struct rp2_gpiodev_chip *rp2_gpiodev_chip = NULL;

static int rp2_gpiodev_chip_close(void *ctx) {
    struct rp2_gpiodev_chip *chip = ctx;

    // Last reference - clean up singleton
    dev_lock();
    assert(rp2_gpiodev_chip == chip);
    rp2_gpiodev_chip = NULL;
    dev_unlock();
    free(chip);
    return 0;
}

static int rp2_gpiodev_chip_fstat(void *ctx, struct stat *pstat) {
    (void)ctx;

    pstat->st_mode = S_IFCHR;
    pstat->st_rdev = DEV_GPIOCHIP0;
    return 0;
}

// Get chip information
static int rp2_gpiodev_chip_get_chipinfo(struct rp2_gpiodev_chip *chip, struct gpiochip_info *info) {
    (void)chip;

    strncpy(info->name, "gpiochip0", GPIO_MAX_NAME_SIZE);
    #if PICO_RP2040
    strncpy(info->label, "rp2040", GPIO_MAX_NAME_SIZE);
    #elif PICO_RP2350
    strncpy(info->label, "rp2350", GPIO_MAX_NAME_SIZE);
    #else
    strncpy(info->label, "", GPIO_MAX_NAME_SIZE);
    #endif
    info->lines = NUM_BANK0_GPIOS;
    return 0;
}

// Get line information
static int rp2_gpiodev_chip_get_lineinfo(struct rp2_gpiodev_chip *chip, struct gpio_v2_line_info *info) {
    const uint gpio = info->offset;
    if (gpio >= NUM_BANK0_GPIOS) {
        errno = EINVAL;
        return -1;
    }

    snprintf(info->name, GPIO_MAX_NAME_SIZE, "GPIO%u", (uint)gpio);
    #if PICO_RP2040
    const char *const f_names[] = { "xip", "spi", "uart", "i2c", "pwm", "sio", "pio1", "pio2", "gpck", "usb" };
    #elif PICO_RP2350
    const char *const f_names[] = { "hstx", "spi", "uart", "i2c", "pwm", "sio", "pio1", "pio2", "pio3", "gpck", "usb", "uart_aux" };
    #else
    const char *const f_names[] = {};
    #endif
    uint f = gpio_get_function(gpio);
    snprintf(info->consumer, GPIO_MAX_NAME_SIZE, "%s", (f < (sizeof(f_names) / sizeof(f_names[0]))) ? f_names[f] : "");
    info->flags |= (f != GPIO_FUNC_NULL) ? GPIO_V2_LINE_FLAG_USED : 0;
    info->flags |= gpio_is_dir_out(gpio) ? GPIO_V2_LINE_FLAG_OUTPUT : GPIO_V2_LINE_FLAG_INPUT;
    info->flags |= gpio_is_pulled_up(gpio) ? GPIO_V2_LINE_FLAG_BIAS_PULL_UP : 0;
    info->flags |= gpio_is_pulled_down(gpio) ? GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN : 0;
    uint32_t event_mask = gpio_get_irq_event_mask(gpio);
    info->flags |= (event_mask & GPIO_IRQ_EDGE_RISE) ? GPIO_V2_LINE_EVENT_RISING_EDGE : 0;
    info->flags |= (event_mask & GPIO_IRQ_EDGE_FALL) ? GPIO_V2_LINE_EVENT_FALLING_EDGE : 0;
    return 0;
}

// Request GPIO lines
static int rp2_gpiodev_line_open(struct rp2_gpiodev_chip *chip, struct gpio_v2_line_request *req);  // Forward declaration

static int rp2_gpiodev_chip_ioctl(void *ctx, unsigned long request, va_list args) {
    struct rp2_gpiodev_chip *file = ctx;

    switch (request) {
        case GPIO_GET_CHIPINFO_IOCTL:
            return rp2_gpiodev_chip_get_chipinfo(file, va_arg(args, struct gpiochip_info *));

        case GPIO_V2_GET_LINEINFO_IOCTL:
            return rp2_gpiodev_chip_get_lineinfo(file, va_arg(args, struct gpio_v2_line_info *));

        case GPIO_V2_GET_LINE_IOCTL:
            return rp2_gpiodev_line_open(file, va_arg(args, struct gpio_v2_line_request *));

        case GPIO_V2_GET_LINEINFO_WATCH_IOCTL:
        case GPIO_GET_LINEINFO_UNWATCH_IOCTL:
            errno = ENOSYS;
            return -1;

        default:
            errno = ENOTTY;
            return -1;
    }
}

static const struct vfs_file_vtable rp2_gpiodev_chip_vtable = {
    .close = rp2_gpiodev_chip_close,
    .fstat = rp2_gpiodev_chip_fstat,
    .ioctl = rp2_gpiodev_chip_ioctl,
};

static void *rp2_gpiodev_chip_open(const void *ctx, dev_t dev, int flags) {
    (void)ctx;
    (void)dev;

    dev_lock();
    struct rp2_gpiodev_chip *file = rp2_gpiodev_chip;
    // Return singleton instance
    if (file) {
        vfs_copy_file(&file->base);
        goto exit;
    }

    // First open - allocate singleton
    file = calloc(1, sizeof(struct rp2_gpiodev_chip));
    if (!file) {
        goto exit;
    }

    vfs_file_init(&file->base, &rp2_gpiodev_chip_vtable, flags);
    rp2_gpiodev_chip = file;

exit:
    dev_unlock();
    return file;
}

const struct dev_driver rp2_gpiodev_drv = {
    .dev = DEV_GPIOCHIP0,
    .open = rp2_gpiodev_chip_open,
};

//=============================================================================
// Line Request File Operations
//=============================================================================

static int rp2_gpiodev_line_close(void *ctx) {
    struct rp2_gpiodev_req *req = ctx;

    // Release lines
    for (uint i = 0; i < req->num_lines; i++) {
        uint gpio = req->lines[i].gpio;
        rp2_gpio_remove_handler(gpio);
        gpio_deinit(gpio);
    }

    // Clean up resources
    ring_free(&req->event_buffer);
    free(req);
    return 0;
}

static int rp2_gpiodev_line_fstat(void *ctx, struct stat *pstat) {
    (void)ctx;
    pstat->st_mode = S_IFCHR;
    return 0;
}

static int rp2_gpiodev_line_read(void *ctx, void *buffer, size_t size) {
    struct rp2_gpiodev_req *req = ctx;

    TickType_t xTicksToWait = portMAX_DELAY;
    int ret = 0;
    while (size >= ret + sizeof(struct gpio_v2_line_event)) {
        taskENTER_CRITICAL();
        bool empty = ring_read(&req->event_buffer, buffer + ret, sizeof(struct gpio_v2_line_event)) < sizeof(struct gpio_v2_line_event);
        if (ring_read_count(&req->event_buffer) == 0) {
            poll_file_notify(&req->base, POLLIN | POLLRDNORM, 0);
        } 
        taskEXIT_CRITICAL();
        if (empty) {
            if (poll_file_wait(&req->base, POLLIN | POLLRDNORM, &xTicksToWait) < 0) {
                break;
            }
        } else {
            ret += sizeof(struct gpio_v2_line_event);
        }
    }
    return (ret > 0) ? ret : -1;
}

static int rp2_gpiodev_line_get_values(struct rp2_gpiodev_req *req, struct gpio_v2_line_values *pvalues) {
    uint64_t bits = gpio_get_all64();

    pvalues->bits = 0;
    uint32_t mask = pvalues->mask;
    for (uint i = 0; i < req->num_lines; i++) {
        if (mask & 1) {
            uint32_t bit = bits >> req->lines[i].gpio;
            bit &= 1;
            bit <<= i;
            pvalues->bits |= bit;
        }
        mask >>= 1;
    }
    return 0;
}

static int rp2_gpiodev_line_set_values(struct rp2_gpiodev_req *req, const struct gpio_v2_line_values *pvalues) {
    struct gpio_v2_line_values values = *pvalues;

    uint64_t bits = 0;
    uint64_t mask = 0;
    for (uint i = 0; i < req->num_lines; i++) {
        if (values.mask & 1) {
            bits |= (values.bits & 1) << req->lines[i].gpio;
            mask |= 1ull << req->lines[i].gpio;
        }
        values.bits >>= 1;
        values.mask >>= 1;
    }
    gpio_put_masked64(mask, bits);
    return 0;    
}

static uint32_t rp2_gpiodev_config_get_flags(const struct gpio_v2_line_config *config, uint offset, enum gpio_v2_line_attr_id attr_id) {
    uint32_t flags = (attr_id == GPIO_V2_LINE_ATTR_ID_FLAGS) ? config->flags : 0;

    // Check attribute overrides
    for (uint i = 0; i < config->num_attrs; i++) {
        if (config->attrs[i].mask & (1U << offset)) {
            if (config->attrs[i].attr.id == attr_id) {
                flags = config->attrs[i].attr.flags;
            }
        }
    }
    return flags;
}

static int rp2_gpiodev_line_set_config(struct rp2_gpiodev_req *req, const struct gpio_v2_line_config *config) {
    for (uint i = 0; i < req->num_lines; i++) {
        uint gpio = req->lines[i].gpio;
        uint32_t flags = rp2_gpiodev_config_get_flags(config, i, GPIO_V2_LINE_ATTR_ID_FLAGS);

        rp2_gpio_set_irq_enabled(gpio, 0xf, false);

        if (flags & GPIO_V2_LINE_FLAG_ACTIVE_LOW) {
            errno = ENXIO;
            return -1;
        }

        switch (flags & (GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_OUTPUT)) {
            case 0:
                if (flags & ~GPIO_V2_LINE_FLAG_ACTIVE_LOW) {
                    errno = EINVAL;
                    return -1;
                }
                break;
            case GPIO_V2_LINE_FLAG_INPUT:
                gpio_set_dir(gpio, false);
                break;
            case GPIO_V2_LINE_FLAG_OUTPUT:
                gpio_set_dir(gpio, true);
                break;
            default:
                errno = EINVAL;
                return -1;
        }

        switch (flags & (GPIO_V2_LINE_FLAG_OPEN_DRAIN | GPIO_V2_LINE_FLAG_OPEN_SOURCE | GPIO_V2_LINE_FLAG_OUTPUT)) {
            case 0:
            case GPIO_V2_LINE_FLAG_OUTPUT:
                break;
            case GPIO_V2_LINE_FLAG_OPEN_DRAIN | GPIO_V2_LINE_FLAG_OUTPUT:
            case GPIO_V2_LINE_FLAG_OPEN_SOURCE | GPIO_V2_LINE_FLAG_OUTPUT:
                errno = ENXIO;
                return -1;
            default:
                errno = EINVAL;
                return -1;
        }

        switch (flags & (GPIO_V2_LINE_FLAG_BIAS_PULL_UP | GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN | GPIO_V2_LINE_FLAG_BIAS_DISABLED)) {
            case 0:
                break;
            case GPIO_V2_LINE_FLAG_BIAS_PULL_UP:
                gpio_pull_up(gpio);
                break;
            case GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN:
                gpio_pull_down(gpio);
                break;
            case GPIO_V2_LINE_FLAG_BIAS_DISABLED:
                gpio_disable_pulls(gpio);
                break;
            default:
                errno = EINVAL;
                return -1;
        }

        uint32_t irq_event_mask;
        switch (flags & (GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING | GPIO_V2_LINE_FLAG_INPUT)) {
            case 0:
            case GPIO_V2_LINE_FLAG_INPUT:
                irq_event_mask = 0;
                break;
            case GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_INPUT:
                irq_event_mask = GPIO_IRQ_EDGE_RISE;
                break;
            case GPIO_V2_LINE_FLAG_EDGE_FALLING | GPIO_V2_LINE_FLAG_INPUT:
                irq_event_mask = GPIO_IRQ_EDGE_FALL;
                break;
            case GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING | GPIO_V2_LINE_FLAG_INPUT:
                irq_event_mask = GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL;
                break;                                
            default:
                errno = EINVAL;
                return -1;
        }

        switch (flags & (GPIO_V2_LINE_FLAG_EVENT_CLOCK_REALTIME | GPIO_V2_LINE_FLAG_EVENT_CLOCK_HTE)) {
            case 0:
                break;
            case GPIO_V2_LINE_FLAG_EVENT_CLOCK_REALTIME:
            case GPIO_V2_LINE_FLAG_EVENT_CLOCK_HTE:
                errno = ENOTSUP;
                return -1;
            default:
                errno = EINVAL;
                return -1;
        }

        req->lines[i].debounce_time_us = rp2_gpiodev_config_get_flags(config, i, GPIO_V2_LINE_ATTR_ID_DEBOUNCE);

        req->lines[i].event_mask = irq_event_mask;
        if (irq_event_mask) {
            if (!req->event_buffer.buffer) {
                if (!ring_alloc(&req->event_buffer, req->event_buffer.size)) {
                    return -1;
                }
            }
            req->lines[i].last_value = gpio_get(gpio);
            rp2_gpio_set_irq_enabled(gpio,  GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
        }
    }

    struct gpio_v2_line_values values = { 0 };
    for (uint i = 0; i < config->num_attrs; i++) {
        if (config->attrs[i].attr.id == GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES) {
            values.bits |= config->attrs[i].attr.values & config->attrs[i].mask & ~values.mask;
            values.mask |= config->attrs[i].mask;
        }
    }
    return rp2_gpiodev_line_set_values(req, &values);
}

static int rp2_gpiodev_line_ioctl(void *ctx, unsigned long request, va_list args) {
    struct rp2_gpiodev_req *req = ctx;

    switch (request) {
        case GPIO_V2_LINE_GET_VALUES_IOCTL: {
            struct gpio_v2_line_values *values = va_arg(args, struct gpio_v2_line_values *);
            return rp2_gpiodev_line_get_values(req, values);
        }
        case GPIO_V2_LINE_SET_VALUES_IOCTL: {
            struct gpio_v2_line_values *values = va_arg(args, struct gpio_v2_line_values *);
            return rp2_gpiodev_line_set_values(req, values);
        }
        case GPIO_V2_LINE_SET_CONFIG_IOCTL: {
            struct gpio_v2_line_config *config = va_arg(args, struct gpio_v2_line_config *);
            return rp2_gpiodev_line_set_config(req, config);
        }
        default:
            errno = ENOTTY;
            return -1;
    }
}

static const struct vfs_file_vtable rp2_gpiodev_line_vtable = {
    .close = rp2_gpiodev_line_close,
    .fstat = rp2_gpiodev_line_fstat,
    .ioctl = rp2_gpiodev_line_ioctl,
    .read = rp2_gpiodev_line_read,
    .pollable = 1,
};

static void rp2_gpiodev_write_event(struct rp2_gpiodev_req *req, uint offset, uint32_t event_mask) {
    // Create event structure
    struct gpio_v2_line_event event = {
        .timestamp_ns = time_us_64() * 1000,
        .offset = offset,
    };
    event_mask &= req->lines[offset].event_mask;

    // Enqueue event (drop if queue full)
    BaseType_t xHigherPriorityTaskTaken = pdFALSE;
    UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
    uint events = 0;
    if (req->event_buffer.buffer) {
        for (; event_mask; event_mask &= event_mask - 1) {
            // Translate event type
            switch (event_mask & (1u << __builtin_ctz(event_mask))) {
                case GPIO_IRQ_EDGE_RISE:
                    event.id = GPIO_V2_LINE_EVENT_RISING_EDGE;
                    break;
                case GPIO_IRQ_EDGE_FALL:
                    event.id = GPIO_V2_LINE_EVENT_FALLING_EDGE;
                    break;
                default:
                    continue;
            }            
            // Ensure there is enough space in the ring for a whole message
            while (ring_write_count(&req->event_buffer) < sizeof(event)) {
                req->event_buffer.read_index += sizeof(event);
            }
            // Finish constructing event and write to ring buffer
            event.seqno = req->seqno++;
            event.line_seqno = req->lines[offset].seqno++;
            ring_write(&req->event_buffer, &event, sizeof(event));
            events |= POLLIN | POLLRDNORM;
        }
        // Wake up poll waiters
        poll_file_notify_from_isr(&req->base, 0, events, &xHigherPriorityTaskTaken);        
    }
    taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
    portYIELD_FROM_ISR(xHigherPriorityTaskTaken);
}

static int64_t rp2_gpiodev_alarm(alarm_id_t id, void *user_data) {
    assert(check_interrupt_core_affinity());
    struct rp2_gpiodev_req *req = user_data;
    uint offset = 0;
    for (; offset < req->num_lines; offset++) {
        if (req->lines[offset].alarm_id == id) {
            break;
        }
    }
    if (offset == req->num_lines) {
        return 0;
    }

    struct rp2_gpiodev_line *line = &req->lines[offset];
    line->alarm_id = -1;
    bool value = gpio_get(line->gpio);
    if (value != line->last_value) {
        line->last_value = value;
        uint32_t event_mask = value ? GPIO_IRQ_EDGE_RISE : GPIO_IRQ_EDGE_FALL;
        rp2_gpiodev_write_event(req, offset, event_mask);
    }
    return 0;
}

static void rp2_gpiodev_irq_handler(uint gpio, uint32_t event_mask, void *ctx) {
    struct rp2_gpiodev_req *req = ctx;
    uint offset = 0;
    for (; offset < req->num_lines; offset++) {
        if (req->lines[offset].gpio == gpio) {
            break;
        }
    }
    if (offset == req->num_lines) {
        return;
    }

    struct rp2_gpiodev_line *line = &req->lines[offset];
    if (line->debounce_time_us) {
        if (line->alarm_id >= 0) {
            cancel_alarm(line->alarm_id);
        }
        line->alarm_id = add_alarm_in_us(line->debounce_time_us, rp2_gpiodev_alarm, req, true);
        return;
    }

    rp2_gpiodev_write_event(req, offset, event_mask);
}

static int rp2_gpiodev_line_open(struct rp2_gpiodev_chip *chip, struct gpio_v2_line_request *req) {
    // Validate request
    if (req->num_lines == 0 || req->num_lines > GPIO_V2_LINES_MAX) {
        errno = EINVAL;
        return -1;
    }
    req->fd = -1;

    // Create line request structure
    struct rp2_gpiodev_req *line_req = calloc(1, sizeof(struct rp2_gpiodev_req) + req->num_lines * sizeof(struct rp2_gpiodev_line));
    if (!line_req) {
        return -1;
    }
    poll_file_init(&line_req->base, &rp2_gpiodev_line_vtable, O_RDWR, 0);

    size_t events_size = req->event_buffer_size;
    events_size = events_size ? events_size : req->num_lines * 16;
    events_size *= sizeof(struct gpio_v2_line_event);
    uint log2_size = -1u;
    while (events_size) {
        log2_size++;
        events_size >>= 1;
    }
    line_req->event_buffer.size = log2_size;

    for (uint i = 0; i < req->num_lines; i++) {
        uint gpio = req->offsets[i];
        if (gpio >= NUM_BANK0_GPIOS) {
            errno = EINVAL;
            return -1;
        }        
        if (gpio_get_function(gpio) != GPIO_FUNC_NULL) {
            errno = EBUSY;
            goto exit;
        }
        line_req->lines[i].gpio = gpio;
        line_req->lines[i].alarm_id = -1; 
        line_req->num_lines++;       
        gpio_init(gpio);
        rp2_gpio_add_handler(gpio, rp2_gpiodev_irq_handler, line_req);
    }

    if (rp2_gpiodev_line_set_config(line_req, &req->config) < 0) {
        goto exit;
    }

    // Get file descriptor
    req->fd = poll_file_fd(&line_req->base);

exit:
    poll_file_release(&line_req->base);
    return (req->fd >= 0) ? 0 : -1;
}
