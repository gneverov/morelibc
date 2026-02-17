// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

/*
 * This header provides a Linux-compatible I2C character device API
 * based on the Linux kernel i2c-dev userspace ABI.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I2C message flags */
#define I2C_M_RD            0x0001  /* Read from slave */
#define I2C_M_TEN           0x0010  /* 10-bit address */
#define I2C_M_DMA_SAFE      0x0200  /* DMA-safe buffer */
#define I2C_M_RECV_LEN      0x0400  /* Length in first byte */
#define I2C_M_NO_RD_ACK     0x0800  /* Skip read ACK */
#define I2C_M_IGNORE_NAK    0x1000  /* Treat NAK as ACK */
#define I2C_M_REV_DIR_ADDR  0x2000  /* Toggle R/W bit */
#define I2C_M_NOSTART       0x4000  /* Skip repeated START */
#define I2C_M_STOP          0x8000  /* Force STOP */

/* I2C message structure */
struct i2c_msg {
    uint16_t addr;      /* Slave address (7 or 10 bits) */
    uint16_t flags;     /* Message flags (I2C_M_*) */
    uint16_t len;       /* Data length */
    uint8_t *buf;       /* Data buffer */
};

/* I2C_RDWR ioctl data structure */
struct i2c_rdwr_ioctl_data {
    struct i2c_msg *msgs;   /* Array of messages */
    int nmsgs;              /* Number of messages */
};

/* Adapter functionality flags */
#define I2C_FUNC_I2C                    0x00000001
#define I2C_FUNC_10BIT_ADDR             0x00000002
#define I2C_FUNC_PROTOCOL_MANGLING      0x00000004
#define I2C_FUNC_SMBUS_PEC              0x00000008
#define I2C_FUNC_NOSTART                0x00000010
#define I2C_FUNC_SLAVE                  0x00000020
#define I2C_FUNC_SMBUS_BLOCK_PROC_CALL  0x00008000
#define I2C_FUNC_SMBUS_QUICK            0x00010000
#define I2C_FUNC_SMBUS_READ_BYTE        0x00020000
#define I2C_FUNC_SMBUS_WRITE_BYTE       0x00040000
#define I2C_FUNC_SMBUS_READ_BYTE_DATA   0x00080000
#define I2C_FUNC_SMBUS_WRITE_BYTE_DATA  0x00100000
#define I2C_FUNC_SMBUS_READ_WORD_DATA   0x00200000
#define I2C_FUNC_SMBUS_WRITE_WORD_DATA  0x00400000
#define I2C_FUNC_SMBUS_PROC_CALL        0x00800000
#define I2C_FUNC_SMBUS_READ_BLOCK_DATA  0x01000000
#define I2C_FUNC_SMBUS_WRITE_BLOCK_DATA 0x02000000
#define I2C_FUNC_SMBUS_READ_I2C_BLOCK   0x04000000
#define I2C_FUNC_SMBUS_WRITE_I2C_BLOCK  0x08000000
#define I2C_FUNC_SMBUS_HOST_NOTIFY      0x10000000

/* IOCTL commands */
#define I2C_IOCTL_BASE      0x0700
#define I2C_RETRIES         (I2C_IOCTL_BASE + 1)    /* Set retry count */
#define I2C_TIMEOUT         (I2C_IOCTL_BASE + 2)    /* Set timeout (10ms units) */
#define I2C_SLAVE           (I2C_IOCTL_BASE + 3)    /* Set slave address */
#define I2C_TENBIT          (I2C_IOCTL_BASE + 4)    /* Enable 10-bit mode */
#define I2C_FUNCS           (I2C_IOCTL_BASE + 5)    /* Get functionality */
#define I2C_SLAVE_FORCE     (I2C_IOCTL_BASE + 6)    /* Force slave address */
#define I2C_RDWR            (I2C_IOCTL_BASE + 7)    /* Combined R/W transfer */
#define I2C_PEC             (I2C_IOCTL_BASE + 8)    /* Enable PEC */

#ifdef __cplusplus
}
#endif
