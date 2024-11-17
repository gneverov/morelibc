// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <dirent.h>
#include <errno.h>
#include <morelib/vfs.h>


int closedir(DIR *dirp) {
    struct vfs_file *file = dirp;
    if (!file) {
        return -1;
    }
    if (S_ISDIR(file->mode)) {
        vfs_release_file(file);
        return 0;

    } else {
        errno = ENOTDIR;
        return -1;
    }
}

DIR *fdopendir(int fd) {
    int flags = 0;
    struct vfs_file *file = vfs_acquire_file(fd, &flags);
    if (!file) {
        return NULL;
    }
    if (S_ISDIR(file->mode)) {
        return (DIR *)file;
    } else {
        vfs_release_file(file);
        errno = ENOTDIR;
        return NULL;
    }
}

DIR *opendir(const char *dirname) {
    vfs_path_buffer_t vfs_dirname;
    struct vfs_mount *vfs = vfs_acquire_mount(dirname, &vfs_dirname);
    if (!vfs) {
        return NULL;
    }
    struct vfs_file *file = NULL;
    if (vfs->func->opendir) {
        file = vfs->func->opendir(vfs, vfs_dirname.begin);
    } else {
        errno = ENOSYS;
    }
    vfs_release_mount(vfs);

    return file;
}

struct dirent *readdir(DIR *dirp) {
    struct vfs_file *file = dirp;
    if (!file) {
        return NULL;
    }
    struct dirent *ret = NULL;
    if (!S_ISDIR(file->mode)) {
        errno = ENOTDIR;
    } else if (file->func->readdir) {
        ret = file->func->readdir(file);
    } else {
        errno = ENOSYS;
    }
    return ret;
}

void rewinddir(DIR *dirp) {
    struct vfs_file *file = dirp;
    if (!file) {
        return;
    }
    if (!S_ISDIR(file->mode)) {
        errno = ENOTDIR;
    }
    if (file->func->rewinddir) {
        file->func->rewinddir(dirp);
    } else {
        errno = ENOSYS;
    }
}
