// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "morelib/dev.h"
#include "morelib/devfs.h"
#include "rp2/term_uart.h"


const struct dev_driver *dev_drvs[] = {
    &term_uart_drv,
};

const size_t dev_num_drvs = sizeof(dev_drvs) / sizeof(dev_drvs[0]);


const struct devfs_entry devfs_entries[] = {
    { "/", S_IFDIR, 0 },

    { "/ttyS0", S_IFCHR, DEV_TTYS0 },
    { "/ttyS1", S_IFCHR, DEV_TTYS1 },
};

const size_t devfs_num_entries = sizeof(devfs_entries) / sizeof(devfs_entries[0]);


const struct vfs_filesystem *vfs_fss[] = {
    &devfs_fs,
};

const size_t vfs_num_fss = sizeof(vfs_fss) / sizeof(vfs_fss[0]);
