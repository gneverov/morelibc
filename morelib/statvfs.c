// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <sys/statvfs.h>
#include "morelib/vfs.h"


int fstatvfs(int fd, struct statvfs *buf) {
    int flags = 0;
    struct vfs_file *file = vfs_acquire_file(fd, &flags);
    if (!file) {
        return -1;
    }
    int ret = -1;
    if (file->func->fstatvfs) {
        memset(buf, 0, sizeof(struct statvfs));
        ret = file->func->fstatvfs(file, buf);
    } else {
        errno = ENOSYS;
    }
    vfs_release_file(file);
    return ret;
}

int statvfs(const char *path, struct statvfs *buf) {
    vfs_path_buffer_t vfs_path;
    struct vfs_mount *vfs = vfs_acquire_mount(path, &vfs_path);
    if (!vfs) {
        return -1;
    }
    int ret = -1;
    if (vfs->func->statvfs) {
        memset(buf, 0, sizeof(struct statvfs));
        ret = vfs->func->statvfs(vfs, buf);
    } else {
        errno = ENOSYS;
    }
    vfs_release_mount(vfs);
    return ret;
}
