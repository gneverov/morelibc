// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

/*
 * RP2 SPI Device Driver
 *
 * Linux-compatible SPI device driver (/dev/spidevX.Y) for RP2040/RP2350
 * This implements the device abstraction (one device on the bus), not the
 * controller (the hardware peripheral).
 */

#include <errno.h>
#include <malloc.h>
#include <stdint.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"

#include "linux/spi/spidev.h"
#include "morelib/dev.h"
#include "morelib/vfs.h"
#include "rp2/spi.h"


//=============================================================================
// Configuration
//=============================================================================

// Default speed in Hz
#define SPIDEV_DEFAULT_SPEED_HZ  1000000

// Default bits per word
#define SPIDEV_DEFAULT_BITS_PER_WORD  8

#define SPIDEV_DEFAULT_CS_DELAY_US 10

// Unsupported mode flags that should return EINVAL
#define SPI_MODE_UNSUPPORTED (SPI_CS_HIGH | SPI_LSB_FIRST | SPI_3WIRE | SPI_LOOP | \
    SPI_TX_DUAL | SPI_TX_QUAD | SPI_RX_DUAL | SPI_RX_QUAD | \
    SPI_TX_OCTAL | SPI_RX_OCTAL | SPI_3WIRE_HIZ | SPI_RX_CPHA_FLIP)


//=============================================================================
// Data Structures
//=============================================================================

struct rp2_spidev {
    struct vfs_file base;
    rp2_spi_t *spi;
    uint32_t mode;              // SPI mode flags
    uint speed_hz;              // Default speed
    uint8_t bits_per_word;      // Default: 8
    uint8_t cs_pin;             // Chip select GPIO
};


//=============================================================================
// Transfer Operations
//=============================================================================

static int rp2_spidev_message(struct rp2_spidev *file, const struct spi_ioc_transfer *xfers, size_t num_transfers) {
    // Acquire mutex
    if (!rp2_spi_take(file->spi, portMAX_DELAY)) {
        errno = ENXIO;
        return -1;
    }
    spi_inst_t *spi = file->spi->inst;

    // Apply speed override if specified
    uint baudrate = xfers->speed_hz ? xfers->speed_hz : file->speed_hz;
    spi_set_baudrate(spi, baudrate);

    // Apply bits_per_word override if specified
    uint bits = xfers->bits_per_word ? xfers->bits_per_word : file->bits_per_word;

    // Extract SPI mode (CPOL/CPHA)
    spi_cpol_t cpol = (file->mode & SPI_CPOL) ? SPI_CPOL_1 : SPI_CPOL_0;
    spi_cpha_t cpha = (file->mode & SPI_CPHA) ? SPI_CPHA_1 : SPI_CPHA_0;
    spi_set_format(spi, bits, cpol, cpha, SPI_MSB_FIRST);

    int total_bytes = 0;
    for (size_t i = 0; i < num_transfers; i++) {
        const struct spi_ioc_transfer *xfer = &xfers[i];

        // Assert CS at start of first transfer
        if (!(file->mode & SPI_NO_CS)) {
            gpio_put(file->cs_pin, false);
        }

        // Execute transfer
        if (xfer->tx_buf && xfer->rx_buf) {
            spi_write_read_blocking(spi, xfer->tx_buf, xfer->rx_buf, xfer->len);
        } else if (xfer->tx_buf) {
            spi_write_blocking(spi, xfer->tx_buf, xfer->len);
        } else if (xfer->rx_buf) {
            spi_read_blocking(spi, 0x00, xfer->rx_buf, xfer->len);
        }
        total_bytes += xfer->len;

        // Handle delay_usecs
        if (xfer->delay_usecs) {
            busy_wait_us_32(xfer->delay_usecs);
        }     

        // No cs_change flag and not the last transfer
        // Keep CS asserted
        if (!xfer->cs_change && ((i + 1) < num_transfers)) {
            continue;
        }

        // Deassert CS between transfers, or at the end
        if (!(file->mode & SPI_NO_CS)) {
            gpio_put(file->cs_pin, true);
        }

        // cs_change flag not set and not the last transfer
        // Delay before asserting CS again
        if (xfer->cs_change && ((i + 1) < num_transfers)) {
            busy_wait_us_32(SPIDEV_DEFAULT_CS_DELAY_US);
        }        
    }

    // Release mutex
    rp2_spi_give(file->spi);
    return total_bytes;
}

//=============================================================================
// File Operations
//=============================================================================

static int rp2_spidev_close(void *ctx) {
    struct rp2_spidev *file = ctx;

    // Deinit CS pin
    gpio_deinit(file->cs_pin);

    free(file);
    return 0;
}

static int rp2_spidev_fstat(void *ctx, struct stat *pstat) {
    struct rp2_spidev *file = ctx;

    pstat->st_mode = S_IFCHR;
    pstat->st_rdev = DEV_SPIDEV0 | (spi_get_index(file->spi->inst) << 6) | (file->cs_pin & 0x3f);
    return 0;
}

static int rp2_spidev_ioctl(void *ctx, unsigned long request, va_list args) {
    struct rp2_spidev *file = ctx;

    switch (request) {
        case SPI_IOC_RD_BITS_PER_WORD: {
            uint8_t *bits_per_word = va_arg(args, uint8_t *);
            *bits_per_word = file->bits_per_word;
            return 0;
        }

        case SPI_IOC_WR_BITS_PER_WORD: {
            const uint8_t *bits_per_word = va_arg(args, const uint8_t *);
            if ((*bits_per_word < 4) || (*bits_per_word > 16)) {
                errno = EINVAL;
                return -1;
            }
            file->bits_per_word = *bits_per_word;
            return 0;
        }

        case SPI_IOC_RD_MAX_SPEED_HZ: {
            uint32_t *speed_hz = va_arg(args, uint32_t *);
            *speed_hz = file->speed_hz;
            return 0;
        }

        case SPI_IOC_WR_MAX_SPEED_HZ: {
            const uint32_t *speed_hz = va_arg(args, const uint32_t *);
            if (*speed_hz > clock_get_hz(clk_peri)) {
                errno = EINVAL;
                return -1;
            }
            file->speed_hz = *speed_hz;
            return 0;
        }

        case SPI_IOC_RD_MODE32: {
            uint32_t *mode = va_arg(args, uint32_t *);
            *mode = file->mode;
            return 0;
        }

        case SPI_IOC_WR_MODE32: {
            const uint32_t *mode = va_arg(args, const uint32_t *);
            // Check for unsupported mode flags
            if (*mode & SPI_MODE_UNSUPPORTED) {
                errno = ENOTSUP;
                return -1;
            }
            file->mode = *mode;
            return 0;
        }

        default:
            break;
    }

    // Check for SPI_IOC_MESSAGE(N)
    // SPI_IOC_MESSAGE(N) = SPI_IOCTL_BASE + (N) * sizeof(struct spi_ioc_transfer)
    if (((request & ~0xff) == SPI_IOCTL_BASE) && (((request & 0xff) % sizeof(struct spi_ioc_transfer)) == 0)) {
        size_t num_transfers = (request & 0xff) / sizeof(struct spi_ioc_transfer);
        const struct spi_ioc_transfer *xfers = va_arg(args, const struct spi_ioc_transfer *);
        return rp2_spidev_message(file, xfers, num_transfers);
    }

    errno = ENOTTY;
    return -1;
}

static int rp2_spidev_read(void *ctx, void *buffer, size_t size) {
    struct rp2_spidev *file = ctx;
    struct spi_ioc_transfer xfer = {
        .rx_buf = buffer,
        .len = size,
    };
    return rp2_spidev_message(file, &xfer, 1);
}

static int rp2_spidev_write(void *ctx, const void *buffer, size_t size) {
    struct rp2_spidev *file = ctx;
    struct spi_ioc_transfer xfer = {
        .tx_buf = buffer,
        .len = size,
    };
    return rp2_spidev_message(file, &xfer, 1);
}

static const struct vfs_file_vtable rp2_spidev_vtable = {
    .close = rp2_spidev_close,
    .fstat = rp2_spidev_fstat,
    .ioctl = rp2_spidev_ioctl,
    .read = rp2_spidev_read,
    .write = rp2_spidev_write,
};


//=============================================================================
// Device Open
//=============================================================================

static void *rp2_spidev_open(const void *ctx, dev_t dev, int flags) {
    (void)ctx;

    uint spi_num = minor(dev) >> 6;
    uint cs_pin = minor(dev) & 0x3f;
    if ((spi_num >= NUM_SPIS) || (cs_pin >= NUM_BANK0_GPIOS)) {
        errno = EINVAL;
        return NULL;
    }

    rp2_spi_t *spi = &rp2_spis[spi_num];

    // Allocate new instance
    struct rp2_spidev *file = calloc(1, sizeof(struct rp2_spidev));
    if (!file) {
        return NULL;
    }

    vfs_file_init(&file->base, &rp2_spidev_vtable, flags);
    file->spi = spi;
    file->mode = SPI_MODE_0;
    file->speed_hz = SPIDEV_DEFAULT_SPEED_HZ;
    file->bits_per_word = SPIDEV_DEFAULT_BITS_PER_WORD;
    file->cs_pin = cs_pin;

    // Initialize CS pin as GPIO output, set inactive (high for active-low CS)
    gpio_init(file->cs_pin);
    gpio_put(file->cs_pin, true);
    gpio_set_dir(file->cs_pin, true);

    return file;
}

const struct dev_driver rp2_spidev_drv = {
    .dev = DEV_SPIDEV0,
    .open = rp2_spidev_open,
};
