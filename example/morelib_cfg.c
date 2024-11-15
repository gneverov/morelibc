// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "newlib/dev.h"
#include "newlib/devfs.h"
// #include "newlib/dhara.h"
// #include "newlib/fatfs.h"
// #include "newlib/littlefs.h"
// #include "newlib/mem.h"
// #include "newlib/mtdblk.h"
// #include "newlib/newlib.h"
// #include "newlib/tty.h"
#include "rp2/mtd.h"
#include "rp2/term_uart.h"
// #include "tinyusb/terminal.h"


const struct dev_driver *dev_drvs[] = {
    // &mem_drv,
    // &mtd_drv,
    // &mtdblk_drv,
    &uart_drv,
    // &usb_drv,
};
const size_t dev_num_drvs = sizeof(dev_drvs) / sizeof(dev_drvs[0]);


const struct devfs_entry devfs_entries[] = {
    { "/", S_IFDIR, 0 },

    // { "/mem", S_IFCHR, DEV_MEM },
    // { "/null", S_IFCHR, DEV_NULL },
    // { "/zero", S_IFCHR, DEV_ZERO },
    // { "/full", S_IFCHR, DEV_FULL },

    // { "/mtd0", S_IFCHR, DEV_MTD0 },
    // { "/mtd1", S_IFCHR, DEV_MTD1 },

    // { "/firmware", S_IFBLK, DEV_MTDBLK0 },
    // { "/storage", S_IFBLK, DEV_MTDBLK1 },

    { "/ttyS0", S_IFCHR, DEV_TTYS0 },
    { "/ttyS1", S_IFCHR, DEV_TTYS1 },

    // { "/ttyUSB0", S_IFCHR, DEV_TTYUSB0 },
};

const size_t devfs_num_entries = sizeof(devfs_entries) / sizeof(devfs_entries[0]);


const struct vfs_filesystem *vfs_fss[] = {
    &devfs_fs,
    // &fatfs_fs,
    // &littlefs_fs,
};

const size_t vfs_num_fss = sizeof(vfs_fss) / sizeof(vfs_fss[0]);
