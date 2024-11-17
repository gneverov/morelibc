// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include "morelib/dev.h"
#include "morelib/tty.h"


static void *tty_open(const void *ctx, dev_t dev, int flags, mode_t mode) {
    if (dev != DEV_TTY) {
        errno = ENODEV;
        return NULL;
    }
    struct vfs_file *file = vfs_gettty();
    if (!file) {
        errno = ENODEV;
        return NULL;
    }
    return file;
}

const struct dev_driver tty_drv = {
    .dev = DEV_TTY,
    .open = tty_open,
};
