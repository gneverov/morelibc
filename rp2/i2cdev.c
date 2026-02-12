// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

/*
 * RP2 I2C Device Driver
 *
 * Linux-compatible I2C device driver (/dev/i2c-X) for RP2040/RP2350
 * This implements the device abstraction for userspace I2C access.
 */

#include <errno.h>
#include <malloc.h>
#include <stdint.h>

#include "hardware/i2c.h"

#include "morelib/dev.h"
#include "morelib/i2cdev.h"
#include "morelib/vfs.h"
#include "rp2/i2c.h"


//=============================================================================
// Configuration
//=============================================================================

#define I2CDEV_DEFAULT_TIMEOUT_MS   1000
#define I2CDEV_DEFAULT_RETRIES      3
#define I2CDEV_DEFAULT_BAUDRATE     100000  /* 100 kHz standard mode */

/* Supported functionality */
#define I2CDEV_FUNCS (I2C_FUNC_I2C | I2C_FUNC_NOSTART)


//=============================================================================
// Data Structures
//=============================================================================

struct rp2_i2cdev {
    struct vfs_file base;
    rp2_i2c_t *i2c;         /* Pointer to rp2_i2cs[bus_num] */
    uint16_t addr;          /* Current slave address */
    uint32_t timeout_ms;    /* Timeout in milliseconds */
    uint8_t retries;        /* Retry count */
};


//=============================================================================
// Transfer Operations
//=============================================================================

static int rp2_i2cdev_rdwr(struct rp2_i2cdev *file, const struct i2c_rdwr_ioctl_data *data) {
    /* Acquire mutex */
    if (!rp2_i2c_take(file->i2c, portMAX_DELAY)) {
        errno = EBUSY;
        return -1;
    }

    int i = 0;
    for (; i < data->nmsgs; i++) {
        const struct i2c_msg *msg = &data->msgs[i];

        /* Validate address */
        if (msg->addr > 0x7F) {
            errno = EINVAL;
            break;
        }

        /* Determine if we should send STOP after this message */
        bool nostop = (i + 1 < data->nmsgs) && !(msg->flags & I2C_M_STOP);
        absolute_time_t t = make_timeout_time_ms(file->timeout_ms);
        i2c_inst_t *i2c = file->i2c->inst;
        int ret;
        if (msg->flags & I2C_M_RD){
            ret = i2c_read_blocking_until(i2c, msg->addr, msg->buf, msg->len, nostop, t);
        } else {
            ret = i2c_write_blocking_until(i2c, msg->addr, msg->buf, msg->len, nostop, t);
        }
        if (ret == PICO_ERROR_GENERIC) {
            errno = EIO;
            break;
        }
        if (ret == PICO_ERROR_TIMEOUT) {
            errno = ETIMEDOUT;
            break;
        }
    }

    /* Release mutex */
    rp2_i2c_give(file->i2c);
    return (i < data->nmsgs) ? -1 : i;
}


//=============================================================================
// File Operations
//=============================================================================

static int rp2_i2cdev_close(void *ctx) {
    struct rp2_i2cdev *file = ctx;
    free(file);
    return 0;
}

static int rp2_i2cdev_fstat(void *ctx, struct stat *pstat) {
    struct rp2_i2cdev *file = ctx;
    pstat->st_mode = S_IFCHR;
    pstat->st_rdev = DEV_I2C0 | i2c_get_index(file->i2c->inst);
    return 0;
}

static int rp2_i2cdev_ioctl(void *ctx, unsigned long request, va_list args) {
    struct rp2_i2cdev *file = ctx;

    switch (request) {
        case I2C_SLAVE:
        case I2C_SLAVE_FORCE: {
            unsigned long addr = va_arg(args, unsigned long);
            if (addr > 0x7F) {
                errno = EINVAL;
                return -1;
            }
            file->addr = addr;
            return 0;
        }

        case I2C_TENBIT: {
            unsigned long tenbit = va_arg(args, unsigned long);
            if (tenbit) {
                errno = ENOTSUP;
                return -1;
            }
            return 0;
        }

        case I2C_FUNCS: {
            unsigned long *funcs = va_arg(args, unsigned long *);
            *funcs = I2CDEV_FUNCS;
            return 0;
        }

        case I2C_RDWR: {
            const struct i2c_rdwr_ioctl_data *data = va_arg(args, const struct i2c_rdwr_ioctl_data *);
            return rp2_i2cdev_rdwr(file, data);
        }

        case I2C_TIMEOUT: {
            unsigned long timeout = va_arg(args, unsigned long);
            /* Linux uses 10ms units */
            file->timeout_ms = timeout * 10;
            return 0;
        }

        case I2C_RETRIES: {
            unsigned long retries = va_arg(args, unsigned long);
            file->retries = retries;
            return 0;
        }

        case I2C_PEC:
            /* PEC not supported by RP2 hardware */
            errno = ENOTSUP;
            return -1;

        default:
            errno = ENOTTY;
            return -1;
    }
}

static int rp2_i2cdev_read_write(struct rp2_i2cdev *file, void *buffer, size_t size, uint flags) {
    if (file->addr == 0) {
        errno = EINVAL;
        return -1;
    }
    struct i2c_msg msg = {
        .addr = file->addr,
        .flags = flags,
        .len = size,
        .buf = buffer,
    };
    struct i2c_rdwr_ioctl_data data = {
        .msgs = &msg,
        .nmsgs = 1,
    };
    int ret = rp2_i2cdev_rdwr(file, &data);
    return (ret > 0) ? size : ret;
}

static int rp2_i2cdev_read(void *ctx, void *buffer, size_t size) {
    struct rp2_i2cdev *file = ctx;
    return rp2_i2cdev_read_write(file, buffer, size, I2C_M_RD);
}

static int rp2_i2cdev_write(void *ctx, const void *buffer, size_t size) {
    struct rp2_i2cdev *file = ctx;
    return rp2_i2cdev_read_write(file, (void *)buffer, size, 0);
}

static const struct vfs_file_vtable rp2_i2cdev_vtable = {
    .close = rp2_i2cdev_close,
    .fstat = rp2_i2cdev_fstat,
    .ioctl = rp2_i2cdev_ioctl,
    .read = rp2_i2cdev_read,
    .write = rp2_i2cdev_write,
};


//=============================================================================
// Device Open
//=============================================================================

static void *rp2_i2cdev_open(const void *ctx, dev_t dev, int flags) {
    (void)ctx;

    uint i2c_num = minor(dev);
    if (i2c_num >= NUM_I2CS) {
        errno = EINVAL;
        return NULL;
    }

    /* Allocate new instance */
    struct rp2_i2cdev *file = calloc(1, sizeof(struct rp2_i2cdev));
    if (!file) {
        return NULL;
    }

    vfs_file_init(&file->base, &rp2_i2cdev_vtable, flags);
    file->i2c = &rp2_i2cs[i2c_num];
    file->addr = 0;
    file->timeout_ms = I2CDEV_DEFAULT_TIMEOUT_MS;
    file->retries = I2CDEV_DEFAULT_RETRIES;

    return file;
}

const struct dev_driver rp2_i2cdev_drv = {
    .dev = DEV_I2C0,
    .open = rp2_i2cdev_open,
};
