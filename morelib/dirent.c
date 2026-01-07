// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <morelib/vfs.h>


int closedir(DIR *dirp) {
    return close((int)dirp);
}

int dirfd(DIR *dirp) {
    return (int)dirp;
}

DIR *fdopendir(int fd) {
    struct vfs_file *file = vfs_acquire_file(fd, 0);
    if (!file) {
        return NULL;
    }
    if (!file->func->isdir) {
        errno = ENOTDIR;
        fd = -1;
    }
    vfs_release_file(file);
    return (fd >= 0) ? (DIR *)fd : NULL;
}

DIR *opendir(const char *dirname) {
    int fd = open(dirname, O_RDONLY | O_DIRECTORY);
    return (fd >= 0) ? (DIR *)fd : NULL;
}

struct dirent *readdir(DIR *dirp) {
    struct vfs_file *file = vfs_acquire_file((int)dirp, 0);
    if (!file) {
        return NULL;
    }
    struct dirent *ret = NULL;
    if (!file->func->isdir) {
        errno = ENOTDIR;
    } else if (file->func->readdir) {
        ret = file->func->readdir(file);
    } else {
        errno = ENOSYS;
    }
    return ret;
}

void rewinddir(DIR *dirp) {
    struct vfs_file *file = vfs_acquire_file((int)dirp, 0);
    if (!file) {
        return;
    }
    if (!file->func->isdir) {
        errno = ENOTDIR;
    }
    else if (file->func->rewinddir) {
        file->func->rewinddir(file);
    } else {
        errno = ENOSYS;
    }
}
