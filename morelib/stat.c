// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "morelib/vfs.h"


int fstat(int fd, struct stat *pstat) {
    struct vfs_file *file = vfs_acquire_file(fd, 0);
    if (!file) {
        return -1;
    }
    int ret = vfs_fstat(file, pstat);
    vfs_release_file(file);
    return ret;
}

int futimens(int fd, const struct timespec times[2]) {
    struct vfs_file *file = vfs_acquire_file(fd, 0);
    if (!file) {
        return -1;
    }
    int ret = -1;
    if (file->func->futimens) {
        ret = file->func->futimens(file, times);
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

int utimensat(int fd, const char *file, const struct timespec times[2], int flag) {
    if (fd != AT_FDCWD) {
        errno = EINVAL;
        return -1;
    }
    vfs_path_buffer_t vfs_path;
    struct vfs_mount *vfs = vfs_acquire_mount(file, &vfs_path);
    if (!vfs) {
        return -1;
    }
    int ret = -1;
    if (vfs->func->utimens) {
        ret = vfs->func->utimens(vfs, vfs_path.begin, times);
    } else {
        errno = ENOSYS;
    }
    vfs_release_mount(vfs);
    return ret;
}