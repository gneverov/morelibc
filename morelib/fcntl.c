// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include "morelib/vfs.h"


int fcntl(int fd, int cmd, ...) {
    va_list args;
    va_start(args, cmd);
    int ret = -1;
    switch (cmd) {
        case F_GETFL: {
            int flags = 0;
            struct vfs_file *file = vfs_acquire_file(fd, &flags);
            if (file) {
                ret = (flags & ~O_ACCMODE) | ((flags - 1) & O_ACCMODE);
                vfs_release_file(file);
            }
            break;
        }
        case F_SETFL: {
            int flags = va_arg(args, int);
            ret = vfs_set_flags(fd, flags & ~O_ACCMODE) >= 0 ? 0 : -1;
            break;
        }
        default: {
            errno = EINVAL;
            break;
        }
    }
    va_end(args);
    return ret;
}

int open(const char *path, int flags, ...) {
    vfs_path_buffer_t vfs_path;
    struct vfs_mount *vfs = vfs_acquire_mount(path, &vfs_path);
    if (!vfs) {
        return -1;
    }
    struct vfs_file *file = NULL;
    if (vfs->func->open) {
        va_list va;
        va_start(va, flags);
        mode_t mode = (flags & O_CREAT) ? va_arg(va, mode_t) : 0;
        file = vfs->func->open(vfs, vfs_path.begin, flags, mode);
        va_end(va);
    } else {
        errno = ENOSYS;
    }
    vfs_release_mount(vfs);

    int ret = -1;
    if (file) {
        if (file->func->isatty && !(flags & O_NOCTTY)) {
            vfs_settty(file);
            vfs_replace(0, file, (flags & ~O_ACCMODE) | FREAD);
            vfs_replace(1, file, (flags & ~O_ACCMODE) | FWRITE);
            vfs_replace(2, file, (flags & ~O_ACCMODE) | FWRITE);
        }
        ret = vfs_replace(-1, file, (flags & ~O_ACCMODE) | ((flags + 1) & O_ACCMODE));
        vfs_release_file(file);
    }
    return ret;
}

int rename(const char *old, const char *new) {
    int ret = -1;
    struct vfs_mount *vfs_old = NULL;
    struct vfs_mount *vfs_new = NULL;
    vfs_path_buffer_t vfs_path_old;
    vfs_old = vfs_acquire_mount(old, &vfs_path_old);
    if (!vfs_old) {
        goto exit;
    }
    vfs_path_buffer_t vfs_path_new;
    vfs_new = vfs_acquire_mount(new, &vfs_path_new);
    if (!vfs_new) {
        goto exit;
    }
    if (vfs_old != vfs_new) {
        errno = EXDEV;
        goto exit;
    }
    if (vfs_old->func->rename) {
        ret = vfs_old->func->rename(vfs_old, vfs_path_old.begin, vfs_path_new.begin);
    } else {
        errno = ENOSYS;
    }
exit:
    if (vfs_old) {
        vfs_release_mount(vfs_old);
    }
    if (vfs_new) {
        vfs_release_mount(vfs_new);
    }
    return ret;
}
