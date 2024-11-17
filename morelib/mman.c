// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "morelib/vfs.h"


void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    int fd_flags = 0;
    struct vfs_file *file = vfs_acquire_file(fd, &fd_flags);
    if (!file) {
        return NULL;
    }
    if (!(fd_flags & FREAD)) {
        errno = EACCES;
        return NULL;
    }
    if (!(fd_flags & FWRITE) && (prot & PROT_WRITE)) {
        errno = EACCES;
        return NULL;
    }
    if (!len || (off < 0)) {
        errno = EINVAL;
        return NULL;
    }
    void *ret = NULL;
    if (file->func->mmap) {
        ret = file->func->mmap(file, addr, len, prot, flags, off);
    } else {
        errno = ENODEV;
    }
    vfs_release_file(file);
    return ret;
}

int munmap(void *addr, size_t len) {
    return 0;
}
