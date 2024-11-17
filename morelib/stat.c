// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <sys/stat.h>
#include "morelib/vfs.h"


int fstat(int fd, struct stat *pstat) {
    int flags = 0;
    struct vfs_file *file = vfs_acquire_file(fd, &flags);
    if (!file) {
        return -1;
    }
    int ret = -1;
    if (file->func->fstat) {
        memset(pstat, 0, sizeof(struct stat));
        pstat->st_mode = file->mode;
        ret = file->func->fstat(file, pstat);
    } else {
        errno = ENOSYS;
    }
    vfs_release_file(file);
    return ret;
}

int mkdir(const char *path, mode_t mode) {
    vfs_path_buffer_t vfs_path;
    struct vfs_mount *vfs = vfs_acquire_mount(path, &vfs_path);
    if (!vfs) {
        return -1;
    }
    int ret = -1;
    if (vfs->func->mkdir) {
        ret = vfs->func->mkdir(vfs, vfs_path.begin, mode);
    } else {
        errno = ENOSYS;
    }
    vfs_release_mount(vfs);
    return ret;
}

int stat(const char *file, struct stat *pstat) {
    vfs_path_buffer_t vfs_path;
    struct vfs_mount *vfs = vfs_acquire_mount(file, &vfs_path);
    if (!vfs) {
        return -1;
    }
    int ret = -1;
    if (vfs->func->stat) {
        memset(pstat, 0, sizeof(struct stat));
        ret = vfs->func->stat(vfs, vfs_path.begin, pstat);
    } else {
        errno = ENOSYS;
    }
    vfs_release_mount(vfs);
    return ret;
}
