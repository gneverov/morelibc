// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <sys/ioctl.h>
#include "morelib/vfs.h"


__attribute__((visibility("hidden")))
int vfs_vioctl(struct vfs_file *file, unsigned long request, va_list args) {
    if (file->func->ioctl) {
        return file->func->ioctl(file, request, args);
    } else {
        errno = ENOTTY;
        return -1;
    }
}

int vfs_ioctl(struct vfs_file *file, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    int ret = vfs_vioctl(file, request, args);
    va_end(args);
    return ret;
}

int ioctl(int fd, unsigned long request, ...) {
    struct vfs_file *file = vfs_acquire_file(fd, 0);
    if (!file) {
        return -1;
    }
    va_list args;
    va_start(args, request);
    int ret = vfs_vioctl(file, request, args);
    va_end(args);
    vfs_release_file(file);
    return ret;
}
