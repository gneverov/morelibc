// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "newlib/thread.h"
#include "newlib/newlib.h"
#include "newlib/vfs.h"

#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t vfs_mutex;

static struct vfs_mount *vfs_table;

static struct vfs_file *vfs_fd_table[VFS_FD_MAX];
static uint16_t vfs_fd_flags[VFS_FD_MAX];

static char *vfs_cwd;

static struct vfs_file *vfs_tty;

__attribute__((constructor, visibility("hidden")))
void vfs_init(void) {
    static StaticSemaphore_t xMutexBuffer;
    vfs_mutex = xSemaphoreCreateMutexStatic(&xMutexBuffer);
}

#ifndef NDEBUG
static bool vfs_is_locked(void) {
    return xSemaphoreGetMutexHolder(vfs_mutex) == xTaskGetCurrentTaskHandle();
}
#endif

static void vfs_lock(void) {
    xSemaphoreTake(vfs_mutex, portMAX_DELAY);
}

static void vfs_unlock(void) {
    assert((xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) || vfs_is_locked());
    xSemaphoreGive(vfs_mutex);
}

void vfs_getcwd(char *buf, size_t size) {
    vfs_lock();
    strncpy(buf, vfs_cwd ? vfs_cwd : "/", size);
    vfs_unlock();
}

__attribute__((visibility("hidden")))
void vfs_setcwd(char *value) {
    size_t len = strlen(value);
    vfs_lock();
    vfs_cwd = realloc(vfs_cwd, len + 1);
    strcpy(vfs_cwd, value);
    vfs_unlock();
}

__attribute__((visibility("hidden")))
struct vfs_file *vfs_gettty(void) {
    vfs_lock();
    struct vfs_file *file = vfs_tty;
    if (file) {
        file->ref_count++;
    }
    vfs_unlock();
    return file;
}

__attribute__((visibility("hidden")))
void vfs_settty(struct vfs_file *file) {
    vfs_lock();
    struct vfs_file *old_file = vfs_tty;
    if (file) {
        file->ref_count++;
    }
    vfs_tty = file;
    vfs_unlock();
    if (old_file) {
        vfs_release_file(old_file);
    }
}

static const struct vfs_filesystem *vfs_lookup_filesystem(const char *type) {
    const struct vfs_filesystem *fs = NULL;
    for (int i = 0; i < vfs_num_fss; i++) {
        if (strcmp(vfs_fss[i]->type, type) == 0) {
            fs = vfs_fss[i];
            break;
        }
    }
    return fs;
}

void vfs_mount_init(struct vfs_mount *vfs, const struct vfs_mount_vtable *func) {
    vfs->func = func;
    vfs->ref_count = 1;
    vfs->path = NULL;
    vfs->path_len = 0;
    vfs->next = NULL;
}

// A valid path must:
// - begin with '/'
// - not end with '/', unless the whole path is "/"

// Compares whether path1 is a prefix of path2.
// Returns NULL if path1 is not a prefix of path2.
// Otherwise return the remainder of path2, after the path1 prefix is removed.
char *vfs_compare_path(const char *path1, const char *path2) {
    size_t len = strlen(path1);
    if (strncmp(path1, path2, len) != 0) {
        return NULL;
    }
    path2 += path1[1] ? len : 0;
    if ((path2[0] == '/') || (path2[0] == '\0')) {
        return (char *)path2;
    }
    return NULL;
}

// Expand path replacing any "." or ".." elements, prepending the CWD, and create a proper absolute path.
int vfs_expand_path(vfs_path_buffer_t *vfs_path, const char *path) {
    const char *limit = vfs_path->buf + sizeof(vfs_path->buf) - 1;
    // The output path does not begin at the beginning of the buffer to allow consumers to prepend their own data.
    char *out = vfs_path->begin = vfs_path->buf + 2;
    *out = '\0';

    if (*path == '/') {
        // absolute path
        path++;
    } else if (*path != '\0') {
        // relative path
        vfs_lock();
        if (vfs_cwd && (strlen(vfs_cwd) > 1)) {
            out = stpcpy(out, vfs_cwd);
        }
        vfs_unlock();
    } else {
        // empty path is invalid
        errno = ENOENT;
        return -1;
    }

    char *next;
    do {
        next = strchrnul(path, '/');
        size_t len = next - path;
        if ((len == 0) || (strncmp(path, ".", len) == 0)) {
        } else if (strncmp(path, "..", len) == 0) {
            char *prev = strrchr(vfs_path->begin, '/');
            out = prev ? prev : vfs_path->begin;
        } else if (out + len < limit) {
            out = stpcpy(out, "/");
            out = stpncpy(out, path, len);
        } else {
            errno = ENAMETOOLONG;
            return -1;
        }
        path = next + 1;
    }
    while (*next);
    if (out == vfs_path->begin) {
        out = stpcpy(out, "/");
    }
    *out = '\0';
    return 0;
}

bool vfs_iterate_mount(struct vfs_mount **entry) {
    vfs_lock();
    if (*entry) {
        *entry = (*entry)->next;
    } else {
        *entry = vfs_table;
    }
    if (*entry) {
        (*entry)->ref_count++;
    }
    vfs_unlock();
    return *entry;
}

struct vfs_mount *vfs_acquire_mount(const char *file, vfs_path_buffer_t *vfs_path) {
    if (vfs_expand_path(vfs_path, file)) {
        return NULL;
    }

    vfs_lock();
    struct vfs_mount *entry = vfs_table;
    while (entry) {
        char *next = vfs_compare_path(entry->path, vfs_path->begin);
        if (next != NULL) {
            if (*next == '\0') {
                strcpy(next, "/");
            }
            vfs_path->begin = next;
            entry->ref_count++;
            goto exit;
        }
        entry = entry->next;
    }
    errno = ENOENT;
exit:
    vfs_unlock();
    return entry;
}

void vfs_release_mount(struct vfs_mount *vfs) {
    vfs_lock();
    int ref_count = --vfs->ref_count;
    vfs_unlock();
    if (ref_count == 0) {
        char *path = vfs->path;
        if (vfs->func->umount) {
            vfs->func->umount(vfs);
        }
        free(path);
    }
}

static bool vfs_mount_lookup(const char *path, struct vfs_mount ***vfs) {
    assert((xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) || vfs_is_locked());
    size_t path_len = strlen(path);

    *vfs = &vfs_table;
    while (**vfs) {
        struct vfs_mount *entry = **vfs;
        if (strcmp(entry->path, path) == 0) {
            return true;
        }
        if (entry->path_len < path_len) {
            break;
        }
        *vfs = &entry->next;
    }
    return false;
}

int mkfs(const char *source, const char *filesystemtype, const char *data) {
    const struct vfs_filesystem *fs = vfs_lookup_filesystem(filesystemtype);
    if (!fs) {
        errno = ENODEV;
        return -1;
    }
    if (fs->mkfs) {
        return fs->mkfs(fs, source, data);
    } else {
        errno = ENOSYS;
        return -1;
    }
}

int mount(const char *source, const char *target, const char *filesystemtype, unsigned long mountflags, const char *data) {
    const struct vfs_filesystem *fs = vfs_lookup_filesystem(filesystemtype);
    if (!fs) {
        errno = ENODEV;
        return -1;
    }

    vfs_path_buffer_t vfs_path;
    if (vfs_expand_path(&vfs_path, target) < 0) {
        return -1;
    }
    if (!fs->mount) {
        errno = ENOSYS;
        return -1;
    }
    struct vfs_mount *mount = fs->mount(fs, source, mountflags, data);
    if (!mount) {
        return -1;
    }

    int ret = -1;
    size_t path_len = strlen(vfs_path.begin);
    mount->path = malloc(path_len + 1);
    mount->path_len = path_len;
    if (!mount->path) {
        errno = ENOMEM;
        goto cleanup;
    }
    strcpy(mount->path, vfs_path.begin);

    vfs_lock();
    struct vfs_mount **pentry;
    if (!vfs_mount_lookup(mount->path, &pentry)) {
        // mount point does not exist: insert it
        mount->next = *pentry;
        *pentry = mount;
        mount = NULL;
        ret = 0;
    } else if (mountflags & MS_REMOUNT) {
        // mount point already exists and remounting allowed: replace it
        struct vfs_mount *old_mount = *pentry;
        mount->next = old_mount->next;
        old_mount->next = NULL;
        *pentry = mount;
        // set mount to the old mount so that it is released in cleanup
        mount = old_mount;
        ret = 0;
    } else {
        // mount point already exists and remounting not allowed: error
        errno = EEXIST;
    }
    vfs_unlock();

cleanup:
    if (mount) {
        vfs_release_mount(mount);
    }
    return ret;
}

int umount(const char *path) {
    vfs_lock();
    int ret = -1;
    struct vfs_mount **pentry;
    struct vfs_mount *vfs = NULL;
    if (vfs_mount_lookup(path, &pentry)) {
        vfs = *pentry;
        *pentry = vfs->next;
        vfs->next = NULL;
    } else {
        errno = EINVAL;
    }
    vfs_unlock();
    if (vfs) {
        vfs_release_mount(vfs);
        ret = 0;
    }
    return ret;
}

void vfs_file_init(struct vfs_file *file, const struct vfs_file_vtable *func, mode_t mode) {
    file->func = func;
    file->ref_count = 1;
    file->mode = mode;
    file->event = NULL;
}

struct vfs_file *vfs_acquire_file(int fd, int *flags) {
    if ((uint)fd >= VFS_FD_MAX) {
        errno = EBADF;
        return NULL;
    }

    vfs_lock();
    struct vfs_file *file = vfs_fd_table[fd];
    if (file && ((vfs_fd_flags[fd] & *flags) == *flags)) {
        file->ref_count++;
        *flags = vfs_fd_flags[fd];
    } else {
        errno = EBADF;
        file = NULL;
    }
    vfs_unlock();
    return file;
}

struct vfs_file *vfs_copy_file(struct vfs_file *file) {
    vfs_lock();
    file->ref_count++;
    vfs_unlock();
    return file;
}

void vfs_release_file(struct vfs_file *file) {
    vfs_lock();
    int ref_count = --file->ref_count;
    vfs_unlock();
    if (ref_count == 0) {
        assert(file->event == NULL);
        if (file->func->close) {
            file->func->close(file);
        } else {
            free(file);
        }
    }
}

int vfs_set_flags(int fd, int flags) {
    if ((uint)fd >= VFS_FD_MAX) {
        errno = EBADF;
        return -1;
    }

    int ret = -1;
    vfs_lock();
    if (vfs_fd_table[fd]) {
        ret = vfs_fd_flags[fd] = (vfs_fd_flags[fd] & O_ACCMODE) | (flags & ~O_ACCMODE);
    } else {
        errno = EBADF;
    }
    vfs_unlock();
    return ret;
}

static int vfs_fd_next(void) {
    assert((xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) || vfs_is_locked());
    for (int fd = 3; fd < VFS_FD_MAX; fd++) {
        if (!vfs_fd_table[fd]) {
            return fd;
        }
    }
    errno = ENFILE;
    return -1;
}

int vfs_close(int fd) {
    if ((uint)fd >= VFS_FD_MAX) {
        errno = EBADF;
        return -1;
    }

    vfs_lock();
    int ret = -1;
    struct vfs_file *file = vfs_fd_table[fd];
    if (!file) {
        errno = EBADF;
        goto exit;
    }
    vfs_fd_table[fd] = NULL;
exit:
    vfs_unlock();
    if (file) {
        vfs_release_file(file);
        ret = 0;
    }
    return ret;
}

int vfs_replace(int fd, struct vfs_file *file, int flags) {
    if (fd >= VFS_FD_MAX) {
        errno = EBADF;
        return -1;
    }

    vfs_lock();
    struct vfs_file *prev_file = NULL;
    if (fd < 0) {
        fd = vfs_fd_next();
    }
    if (fd < 0) {
        goto exit;
    }
    file->ref_count++;
    prev_file = vfs_fd_table[fd];
    vfs_fd_table[fd] = file;
    vfs_fd_flags[fd] = flags;
exit:
    vfs_unlock();
    if (prev_file) {
        vfs_release_file(prev_file);
    }
    return fd;
}

int dup(int oldfd) {
    int flags = 0;
    struct vfs_file *old_file = vfs_acquire_file(oldfd, &flags);
    if (!old_file) {
        return -1;
    }
    int ret = vfs_replace(-1, old_file, flags);
    vfs_release_file(old_file);
    return ret;
}

int dup2(int oldfd, int newfd) {
    if ((uint)newfd >= VFS_FD_MAX) {
        errno = EBADF;
        return -1;
    }
    int flags = 0;
    struct vfs_file *old_file = vfs_acquire_file(oldfd, &flags);
    if (!old_file) {
        return -1;
    }
    int ret = vfs_replace(newfd, old_file, flags);
    vfs_release_file(old_file);
    return ret;
}

__attribute__((destructor(200)))
void vfs_deinit(void) {
    for (int fd = VFS_FD_MAX - 1; fd >= 0; fd--) {
        fsync(fd);
        vfs_close(fd);
    }
    sync();
}
