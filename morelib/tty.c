// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include "morelib/dev.h"
#include "morelib/tty.h"


void *term_mux_open(const void *ctx, dev_t dev, mode_t mode);

static void *tty_open(const void *ctx, dev_t dev, mode_t mode) {
    struct vfs_file *file = NULL;
    switch (dev) {
        case DEV_TTY:
            file = vfs_gettty();
            if (!file) {
                errno = ENODEV;
            }            
            break;
        case DEV_TMUX:
            file = term_mux_open(ctx, dev, mode);
            break;
        default:
            errno = ENODEV;
            break;
    }
    return file;
}

const struct dev_driver tty_drv = {
    .dev = DEV_TTY,
    .open = tty_open,
};
