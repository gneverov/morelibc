// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <sys/ioctl.h>
#include "morelib/vfs.h"


__attribute__((visibility("hidden")))
int vioctl(int fd, unsigned long request, va_list args) {
    int flags = 0;
    struct vfs_file *file = vfs_acquire_file(fd, &flags);
    if (!file) {
        return -1;
    }
    int ret = -1;
    if (file->func->ioctl) {
        ret = file->func->ioctl(file, request, args);
    } else {
        errno = ENOTTY;
    }
    vfs_release_file(file);
    return ret;
}

int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    int ret = vioctl(fd, request, args);
    va_end(args);
    return ret;
}
