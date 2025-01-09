// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <stdio.h>
#include "morelib/vfs.h"


int vfs_fsync(struct vfs_file *file) {
    if (file->func->fsync) {
        return file->func->fsync(file);
    }
    else {
        return 0;
    }
}

int vfs_ftruncate(struct vfs_file *file, off_t length) {
    if (length < 0) {
        errno = EINVAL;
        return -1;
    }    
    if (file->func->ftruncate) {
        return file->func->ftruncate(file, length);
    } else {
        errno = ENOSYS;
        return -1;
    }
}

off_t vfs_lseek(struct vfs_file *file, off_t offset, int whence) {
    if (file->func->lseek) {
        return file->func->lseek(file, offset, whence);
    } else {
        errno = S_ISDIR(file->mode) ? EISDIR : ESPIPE;
        return -1;
    }
}

ssize_t vfs_pread(struct vfs_file *file, void *buffer, size_t size, off_t offset) {
    if (vfs_lseek(file, 0, SEEK_CUR) < 0) {
        return -1;
    }
    if (offset < 0) {
        errno = EINVAL;
        return -1;
    }
    if (file->func->pread) {
        return file->func->pread(file, buffer, size, offset);
    } else {
        errno = S_ISDIR(file->mode) ? EISDIR : ENOSYS;
        return -1;
    }
}

ssize_t vfs_pwrite(struct vfs_file *file, const void *buffer, size_t size, off_t offset) {
    if (vfs_lseek(file, 0, SEEK_CUR) < 0) {
        return -1;
    }
    if (offset < 0) {
        errno = EINVAL;
        return -1;
    }
    if (file->func->pwrite) {
        return file->func->pwrite(file, buffer, size, offset);
    } else {
        errno = S_ISDIR(file->mode) ? EISDIR : ENOSYS;
        return -1;
    }
}

int vfs_read(struct vfs_file *file, void *buffer, size_t size, int flags) {
    if (file->func->read) {
        return file->func->read(file, buffer, size, flags);
    } else {
        errno = S_ISDIR(file->mode) ? EISDIR : ENOSYS;
        return -1;
    }
}

int vfs_write(struct vfs_file *file, const void *buffer, size_t size, int flags) {
    if (file->func->write) {
        return file->func->write(file, buffer, size, flags);
    } else {
        errno = S_ISDIR(file->mode) ? EISDIR : ENOSYS;
        return -1;
    }
}
