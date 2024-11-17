// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "morelib/dev.h"
#include "morelib/mtd.h"
#include "morelib/vfs.h"

#include "FreeRTOS.h"
#include "semphr.h"

#ifndef MTD_NUM_DEVICES
#define MTD_NUM_DEVICES 2
#endif

#ifndef MTD_NUM_PARTITIONS
#define MTD_NUM_PARTITIONS 4
#endif


struct mtd_device {
    struct mtd_info info;
    uintptr_t mmap_addr;
    uintptr_t rw_addr;
};

struct mtd_partition {
    struct mtd_device *device;
    uint32_t offset;
    size_t size;
};

struct mtd_file {
    struct vfs_file base;
    int index;
    SemaphoreHandle_t mutex;
    off_t pos;
    struct timespec mtime;
    StaticSemaphore_t xMutexBuffer;
};

extern const struct dev_driver mtd_drv;
