// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include "morelib/vfs.h"


int fcntl(int fd, int cmd, ...) {
    va_list args;
    va_start(args, cmd);
    struct vfs_file *file = vfs_acquire_file(fd, 0);
    if (!file) {
        return -1;
    }

    int ret = -1;
    switch (cmd) {
        case F_GETFL: {
            ret = file->flags;
            break;
        }
        case F_SETFL: {
            int new_flags = va_arg(args, int);
            file->flags = (new_flags & ~O_ACCMODE) | (file->flags & O_ACCMODE);
            ret = 0;
            break;
        }
        default: {
            errno = EINVAL;
            break;
        }
    }
    va_end(args);
    vfs_release_file(file);    
    return ret;
}

int open(const char *path, int flags, ...) {
    vfs_path_buffer_t vfs_path;
    struct vfs_mount *vfs = vfs_acquire_mount(path, &vfs_path);
    if (!vfs) {
        return -1;
    }
    struct vfs_file *file = NULL;
    if (vfs->func->open && !(flags & O_DIRECTORY)) {
        va_list va;
        va_start(va, flags);
        mode_t mode = (flags & O_CREAT) ? va_arg(va, mode_t) : 0;
        file = vfs->func->open(vfs, vfs_path.begin, flags, mode);
        va_end(va);
        if (file || (errno != EISDIR)) {
            goto end;
        }
    } 
    if (vfs->func->opendir) {
        file = vfs->func->opendir(vfs, vfs_path.begin);
    }
    else {
        errno = ENOSYS;
    }

    end:
    vfs_release_mount(vfs);
    int ret = -1;
    if (file) {
        if (file->func->isatty && !(flags & O_NOCTTY)) {
            vfs_settty(file);
            vfs_replace(0, file);
            vfs_replace(1, file);
            vfs_replace(2, file);
        }
        ret = vfs_replace(-1, file);
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
