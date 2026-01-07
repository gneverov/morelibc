// SPDX-FileCopyrightText: 2025 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "morelib/dev.h"
#include "morelib/vfs.h"

#ifndef LOOP_NUM_DEVICES
#define LOOP_NUM_DEVICES 4
#endif

// IOCTL commands for loop device
#define LOOP_BASE 0x1000
#define LOOP_SET_FD (LOOP_BASE + 0)
#define LOOP_CLR_FD (LOOP_BASE + 1)


struct loop_file {
    struct vfs_file base;
    int index;
    struct vfs_file *file;
    int ro;
};

extern const struct dev_driver loop_drv;
