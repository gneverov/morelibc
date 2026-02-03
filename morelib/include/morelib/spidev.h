// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

/*
 * This header provides a Linux-compatible GPIO character device API
 * based on the Linux kernel v2 GPIO userspace ABI.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPI_CPHA            0x0001      /* clock phase */
#define SPI_CPOL            0x0002      /* clock polarity */

#define SPI_MODE_0          (0|0)       /* (original MicroWire) */
#define SPI_MODE_1          (0|SPI_CPHA)
#define SPI_MODE_2          (SPI_CPOL|0)
#define SPI_MODE_3          (SPI_CPOL|SPI_CPHA)
#define SPI_MODE_X_MASK     (SPI_CPOL|SPI_CPHA)

#define SPI_CS_HIGH         0x0004      /* chipselect active high? */
#define SPI_LSB_FIRST       0x0008      /* per-word bits-on-wire */
#define SPI_3WIRE           0x0010      /* SI/SO signals shared */
#define SPI_LOOP            0x0020      /* loopback mode */
#define SPI_NO_CS           0x0040      /* 1 dev/bus, no chipselect */
#define SPI_READY           0x0080      /* slave pulls low to pause */
#define SPI_TX_DUAL         0x0100      /* transmit with 2 wires */
#define SPI_TX_QUAD         0x0200      /* transmit with 4 wires */
#define SPI_RX_DUAL         0x0400      /* receive with 2 wires */
#define SPI_RX_QUAD         0x0800      /* receive with 4 wires */
#define SPI_CS_WORD         0x1000      /* toggle cs after each word */
#define SPI_TX_OCTAL        0x2000      /* transmit with 8 wires */
#define SPI_RX_OCTAL        0x4000      /* receive with 8 wires */
#define SPI_3WIRE_HIZ       0x8000      /* high impedance turnaround */
#define SPI_RX_CPHA_FLIP    0x10000     /* flip CPHA on Rx only xfer */
#define SPI_MOSI_IDLE_LOW   0x20000     /* leave MOSI line low when idle */
#define SPI_MOSI_IDLE_HIGH  0x40000     /* leave MOSI line high when idle */


/* describes a single SPI transfer */
struct spi_ioc_transfer {
    /* Holds pointer to userspace buffer with transmit data, or null. If no data is provided, zeroes are shifted out. */
    const void *tx_buf;

    /* Holds pointer to userspace buffer for receive data, or null. */
    void *rx_buf;

    /* Length of tx and rx buffers, in bytes. */
    size_t len;

    /* Temporary override of the device's bitrate. */
    uint32_t speed_hz;

    /* If nonzero, how long to delay after the last bit transfer before optionally deselecting the device before the 
     * next transfer. */
    uint16_t delay_usecs;

    /* Temporary override of the device's wordsize. */
    uint8_t bits_per_word;

    /* True to deselect device before starting the next transfer. */
    uint8_t cs_change;

    uint8_t tx_nbits;
    uint8_t rx_nbits;

    /* If nonzero, how long to wait between words within one transfer. This property needs explicit support in the SPI 
     * controller, otherwise it is silently ignored. */
    uint8_t word_delay_usecs;
};

#define SPI_IOCTL_BASE 0x6b00

#define SPI_IOC_MESSAGE(N) (SPI_IOCTL_BASE + (N) * sizeof(struct spi_ioc_transfer))

/* Read / Write SPI device word length (1..N) */
/* param: uint8_t * */
#define SPI_IOC_RD_BITS_PER_WORD (SPI_IOCTL_BASE + 6)
#define SPI_IOC_WR_BITS_PER_WORD (SPI_IOCTL_BASE + 7)

/* Read / Write SPI device default max speed hz */
/* param: uint32_t * */
#define SPI_IOC_RD_MAX_SPEED_HZ (SPI_IOCTL_BASE + 8)
#define SPI_IOC_WR_MAX_SPEED_HZ (SPI_IOCTL_BASE + 9)

/* Read of the SPI mode field */
/* param: uint32_t * */
#define SPI_IOC_RD_MODE32 (SPI_IOCTL_BASE + 10)
#define SPI_IOC_WR_MODE32 (SPI_IOCTL_BASE + 11)

#ifdef __cplusplus
}
#endif
