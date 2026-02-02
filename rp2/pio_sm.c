// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <malloc.h>
#include <stdarg.h>
#include "morelib/dev.h"
#include "rp2/pio.h"
#include "rp2/pio_sm.h"

#include "hardware/gpio.h"


//=============================================================================
// PIO Chip
//=============================================================================
static const struct vfs_file_vtable rp2_pio_chip_vtable;

static void *rp2_pio_chip_open(const void *ctx, dev_t dev, int flags) {
    struct vfs_file *chip = calloc(1, sizeof(struct vfs_file));
    if (!chip) {
        return NULL;
    }
    vfs_file_init(chip, &rp2_pio_chip_vtable, flags);
    return chip;
}

static int rp2_pio_sm_open(struct vfs_file *chip, const struct rp2_pio_request *req);

static int rp2_pio_chip_ioctl(void *ctx, unsigned long request, va_list args) {
    struct vfs_file *chip = ctx;
    switch (request) {
        case RP2_PIO_SM_INFO_IOCTL: {
            struct rp2_pio_chip_info *info = va_arg(args, struct rp2_pio_chip_info *);
            info->num_pios = NUM_PIOS;
            info->num_state_machines = NUM_PIO_STATE_MACHINES;
            info->pio_version = PICO_PIO_VERSION;
            info->use_gpio_base = PICO_PIO_USE_GPIO_BASE;
            return 0;
        }
        case RP2_PIO_SM_OPEN_IOCTL: {
            const struct rp2_pio_request *req = va_arg(args, const struct rp2_pio_request *);
            return rp2_pio_sm_open(chip, req);
        }
        default:
            errno = ENOTTY;
            return -1;
    }
}

static const struct vfs_file_vtable rp2_pio_chip_vtable = {
    .ioctl = rp2_pio_chip_ioctl,
};


//=============================================================================
// State Machine
//=============================================================================
static const struct vfs_file_vtable rp2_pio_sm_vtable;

static void rp2_pio_interrupt_handler(PIO pio, enum pio_interrupt_source source, void *context, BaseType_t *pxHigherPriorityTaskWoken) {
    struct rp2_pio_sm *sm = context;
    assert(source == pio_get_rx_fifo_not_empty_interrupt_source(sm->sm));

    pio_set_irq0_source_enabled(sm->pio, pio_get_rx_fifo_not_empty_interrupt_source(sm->sm), false);
    rp2_fifo_set_enabled(&sm->rx_fifo, true);
    poll_file_notify_from_isr(&sm->base, 0, POLLIN | POLLRDNORM, pxHigherPriorityTaskWoken);
    // wait for DMA to start
}

static void rp2_pio_fifo_rx_handler(rp2_fifo_t *fifo, const ring_t *ring, BaseType_t *pxHigherPriorityTaskWoken) {
    struct rp2_pio_sm *sm = (struct rp2_pio_sm *)((uint8_t *)fifo - offsetof(struct rp2_pio_sm, rx_fifo));

    size_t count = ring_read_count(ring);
    poll_file_notify(&sm->base, POLLIN | POLLRDNORM, count ? POLLIN | POLLRDNORM : 0);
    rp2_fifo_set_enabled(&sm->rx_fifo, count);
    pio_set_irq0_source_enabled(sm->pio, pio_get_rx_fifo_not_empty_interrupt_source(sm->sm), !count);
}

static void rp2_pio_fifo_tx_handler(rp2_fifo_t *fifo, const ring_t *ring, BaseType_t *pxHigherPriorityTaskWoken) {
    struct rp2_pio_sm *sm = (struct rp2_pio_sm *)((uint8_t *)fifo - offsetof(struct rp2_pio_sm, tx_fifo));

    uint events = 0;
    if (!ring_read_count(ring)) {
        events |= POLLDRAIN;
    }
    if (ring_write_count(ring) >= sm->threshold) {
        events |= POLLOUT | POLLWRNORM;
    }
    poll_file_notify_from_isr(&sm->base, POLLOUT | POLLWRNORM | POLLDRAIN, events, pxHigherPriorityTaskWoken);
}

static int rp2_pio_sm_open(struct vfs_file *chip, const struct rp2_pio_request *req) {
    struct rp2_pio_sm *sm = calloc(1, sizeof(struct rp2_pio_sm));
    if (!sm) {
        return -1;
    }

    poll_file_init(&sm->base, &rp2_pio_sm_vtable, O_RDWR, 0);
    sm->program = req->program;
    sm->sm = -1;
    rp2_fifo_init(&sm->rx_fifo, false, rp2_pio_fifo_rx_handler);
    rp2_fifo_init(&sm->tx_fifo, true, rp2_pio_fifo_tx_handler);

    int ret = -1;
    if (!pio_claim_free_sm_and_add_program(&req->program, &sm->pio, &sm->sm, &sm->loaded_offset)) {
        errno = EIO;
        goto exit;
    }

    if (pio_sm_init(sm->pio, sm->sm, sm->loaded_offset, NULL)) {
        errno = EIO;
        goto exit;
    }
    rp2_pio_set_irq(sm->pio, pio_get_rx_fifo_not_empty_interrupt_source(sm->sm), rp2_pio_interrupt_handler, sm);

    for (uint gpio = 0; gpio < NUM_BANK0_GPIOS; gpio++) {
        if (req->pin_mask & (1u << gpio)) {
            pio_gpio_init(sm->pio, gpio);
            gpio_disable_pulls(gpio);
        }
    }
    sm->pin_mask = req->pin_mask;

    ret = poll_file_fd(&sm->base);

exit:
    poll_file_release(&sm->base);
    return ret;
}

static int rp2_pio_sm_close(void *ctx) {
    struct rp2_pio_sm *sm = ctx;
    if (sm->sm != -1u) {
        pio_sm_set_enabled(sm->pio, sm->sm, false);
        pio_sm_restart(sm->pio, sm->sm);
        rp2_pio_clear_irq(sm->pio, pio_get_rx_fifo_not_empty_interrupt_source(sm->sm));
        rp2_fifo_deinit(&sm->tx_fifo);
        rp2_fifo_deinit(&sm->rx_fifo);
        pio_remove_program_and_unclaim_sm(&sm->program, sm->pio, sm->sm, sm->loaded_offset);
    }
    for (uint gpio = 0; gpio < NUM_BANK0_GPIOS; gpio++) {
        if (sm->pin_mask & (1u << gpio)) {
            gpio_deinit(gpio);
        }
    }
    free(sm);
    return 0;
}

static int rp2_pio_sm_configure(struct rp2_pio_sm *sm, const struct rp2_pio_sm_config *config) {
    pio_sm_config c = config->config;
    sm_config_set_wrap(&c, config->wrap_target + sm->loaded_offset, config->wrap + sm->loaded_offset);

    rp2_fifo_clear(&sm->tx_fifo);
    pio_sm_init(sm->pio, sm->sm, config->initial_pc + sm->loaded_offset, &c);

    rp2_fifo_set_enabled(&sm->rx_fifo, false);
    rp2_fifo_clear(&sm->rx_fifo);    
    return 0;
}

static int rp2_pio_sm_configure_fifo(struct rp2_pio_sm *sm, const struct rp2_pio_fifo_config *config) {
    rp2_fifo_t *fifo = config->tx ? &sm->tx_fifo: &sm->rx_fifo;
    rp2_fifo_deinit(fifo);

    if (config->fifo_size) {
        if (config->tx) {
            sm->threshold = config->threshold;
        }
        PIO pio = sm->pio;
        const volatile void *fifo_addr = config->tx ? &pio->txf[sm->sm] : &pio->rxf[sm->sm];
        return rp2_fifo_alloc(fifo, config->fifo_size, pio_get_dreq(pio, sm->sm, config->tx), config->dma_transfer_size, config->bswap, (volatile void *)fifo_addr) ? 0 : -1;
    }
    return 0;
}

static int rp2_pio_sm_configure_pins(struct rp2_pio_sm *sm, const struct rp2_pio_pin_config *config) {
    for (uint pin = 0; pin < NUM_BANK0_GPIOS; pin++) {
        uint32_t bit = 1u << pin;
        if (config->mask & bit) {
            gpio_set_pulls(pin, config->pull_ups & bit, config->pull_downs & bit);
        }
    }
        
    pio_sm_set_pindirs_with_mask(sm->pio, sm->sm, config->dirs, config->mask & sm->pin_mask);
    pio_sm_set_pins_with_mask(sm->pio, sm->sm, config->values, config->mask & sm->pin_mask);
    return 0;
}

static int rp2_pio_sm_set_enabled(struct rp2_pio_sm *sm, int enabled) {
    pio_sm_set_enabled(sm->pio, sm->sm, enabled);
    return 0;
}

static uint rp2_pio_sm_get_pc(struct rp2_pio_sm *sm) {
    return pio_sm_get_pc(sm->pio, sm->sm) - sm->loaded_offset;
}

static int rp2_pio_sm_exec(struct rp2_pio_sm *sm, uint instr) {
    pio_sm_exec(sm->pio, sm->sm, instr);
    return pio_sm_is_exec_stalled(sm->pio, sm->sm) ? 0 : 1;
}

static int rp2_pio_sm_drain(struct rp2_pio_sm *sm) {
    if (sm->tx_fifo.channel < 0) {
        return 0;
    }

    ring_t ring;
    TickType_t xTicksToWait = portMAX_DELAY;
    int ret;
    do {
        rp2_fifo_exchange(&sm->tx_fifo, &ring, 0);
        if (ring_read_count(&ring)) {
            errno = EAGAIN;
            ret = -1;
        } else {
            ret = 0;
        }
    }
    while (POLL_CHECK(ret, &sm->base, POLLDRAIN, &xTicksToWait));
    return ret;
}

static int rp2_pio_sm_ioctl(void *ctx, unsigned long request, va_list args) {
    struct rp2_pio_sm *sm = ctx;
    switch (request) {
        case RP2_PIO_SM_CONFIGURE_IOCTL: {
            struct rp2_pio_sm_config *config = va_arg(args, struct rp2_pio_sm_config *);
            return rp2_pio_sm_configure(sm, config);
        }
        case RP2_PIO_SM_CONFIGURE_FIFO_IOCTL: {
            struct rp2_pio_fifo_config *config = va_arg(args, struct rp2_pio_fifo_config *);
            return rp2_pio_sm_configure_fifo(sm, config);
        }
        case RP2_PIO_SM_CONFIGURE_PINS_IOCTL: {
            struct rp2_pio_pin_config *config = va_arg(args, struct rp2_pio_pin_config *);
            return rp2_pio_sm_configure_pins(sm, config);
        }
        case RP2_PIO_SM_SET_ENABLED_IOCTL: {
            int enabled = va_arg(args, int);
            return rp2_pio_sm_set_enabled(sm, enabled);
        }
        case RP2_PIO_SM_GET_PC_IOCTL: {
            uint *pc = va_arg(args, uint *);
            *pc = rp2_pio_sm_get_pc(sm);
            return 0;
        }
        case RP2_PIO_SM_EXEC_IOCTL: {
            uint instr = va_arg(args, uint);
            return rp2_pio_sm_exec(sm, instr);
        }
        case RP2_PIO_SM_DRAIN_IOCTL: {
            return rp2_pio_sm_drain(sm);
        }        
        default: {
            errno = ENOTTY;
            return -1;
        }
    }
}

static int rp2_pio_sm_read(void *ctx, void *buffer, size_t size) {
    struct rp2_pio_sm *sm = ctx;
    TickType_t xTicksToWait = portMAX_DELAY;
    return rp2_fifo_read(&sm->base, &sm->rx_fifo, buffer, size, &xTicksToWait);
}

static int rp2_pio_sm_write(void *ctx, const void *buffer, size_t size) {
    struct rp2_pio_sm *sm = ctx;
    TickType_t xTicksToWait = portMAX_DELAY;
    return rp2_fifo_write(&sm->base, &sm->tx_fifo, buffer, size, &xTicksToWait);

}

static const struct vfs_file_vtable rp2_pio_sm_vtable = {
    .close = rp2_pio_sm_close,
    // .fstat = rp2_pio_sm_fstat,
    .ioctl = rp2_pio_sm_ioctl,
    .read = rp2_pio_sm_read,
    .write = rp2_pio_sm_write,
    .pollable = 1,
};



//=============================================================================
// Driver
//=============================================================================
const struct dev_driver rp2_pio_drv = {
    .dev = 0xf100,
    .open = rp2_pio_chip_open,
};


#ifndef NDEBUG
#include <stdio.h>

void rp2_pio_sm_debug(int fd) {
    struct rp2_pio_sm *sm = (struct rp2_pio_sm *)poll_file_acquire(fd, 0);
    if (sm->base.base.func != &rp2_pio_sm_vtable) {
        printf("%s\n", strerror(EBADF));
        goto exit;
    }

    PIO pio = sm->pio;
    uint num = sm->sm;
    printf("sm %u on pio %u at %p\n", num, pio_get_index(pio), sm);
    printf("  enabled:   %d\n", !!(pio->ctrl & (1u << num)));
    printf("  clkdiv:    0x%08lx\n", pio->sm[num].clkdiv);
    printf("  execctrl:  0x%08lx\n", pio->sm[num].execctrl);
    printf("  shiftctrl: 0x%08lx\n", pio->sm[num].shiftctrl);
    printf("  pinctrl:   0x%08lx\n", pio->sm[num].pinctrl);

    printf("  pc:        %u\n", pio_sm_get_pc(pio, num));
    printf("  rx_fifo:   %u", pio_sm_get_rx_fifo_level(pio, num));
    if (pio_sm_is_rx_fifo_full(pio, num)) {
        printf(" full");
    }
    printf("\n");
    printf("  tx_fifo:   %u", pio_sm_get_tx_fifo_level(pio, num));
    if (pio_sm_is_tx_fifo_full(pio, num)) {
        printf(" full");
    }
    printf("\n");

    rp2_fifo_debug(&sm->rx_fifo);
    rp2_fifo_debug(&sm->tx_fifo);

exit:
    poll_file_release(&sm->base);
}
#endif
