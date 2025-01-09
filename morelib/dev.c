// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include "morelib/dev.h"
#include "morelib/vfs.h"

#include "FreeRTOS.h"
#include "semphr.h"


// dev_mutex is held when a device is being opened
static SemaphoreHandle_t dev_mutex;

__attribute__((constructor, visibility("hidden")))
void dev_init(void) {
    static StaticSemaphore_t xMutexBuffer;
    dev_mutex = xSemaphoreCreateRecursiveMutexStatic(&xMutexBuffer);
}

static const struct dev_driver *finddev(dev_t dev) {
    for (size_t i = 0; i < dev_num_drvs; i++) {
        if (major(dev_drvs[i]->dev) == major(dev)) {
            return dev_drvs[i];
        }
    }
    return NULL;
}

struct vfs_file *opendev(dev_t dev, mode_t mode) {
    const struct dev_driver *drv = finddev(dev);
    if (!drv) {
        errno = ENODEV;
        return NULL;
    }
    if (!drv->open) {
        errno = ENOSYS;
        return NULL;
    }
    return drv->open(drv, dev, mode & ~S_IFMT);
}

int statdev(dev_t dev, struct stat *pstat) {
    const struct dev_driver *drv = finddev(dev);
    if (!drv) {
        errno = ENODEV;
        return -1;
    }
    if (!drv->stat) {
        errno = ENOSYS;
        return -1;
    }
    return drv->stat(drv, dev, pstat);
}

__attribute__((visibility("hidden")))
void dev_lock(void) {
    xSemaphoreTakeRecursive(dev_mutex, portMAX_DELAY);
}

__attribute__((visibility("hidden")))
void dev_unlock(void) {
    xSemaphoreGiveRecursive(dev_mutex);
}
