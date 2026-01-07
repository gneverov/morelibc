// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include "morelib/mount.h"

#ifndef VFS_FD_MAX
#define VFS_FD_MAX 64
#endif


struct vfs_filesystem {
    const char *type;
    int (*mkfs)(const void *ctx, const char *source, const char *data);
    void *(*mount)(const void *ctx, const char *source, unsigned long mountflags, const char *data);
};

struct vfs_mount_vtable {
    int (*mkdir)(void *ctx, const char *path, mode_t mode);
    void *(*open)(void *ctx, const char *file, int flags, mode_t mode);
    int (*rename)(void *ctx, const char *old, const char *new);
    int (*stat)(void *ctx, const char *file, struct stat *pstat);
    int (*unlink)(void *ctx, const char *file);
    int (*utimens)(void *ctx, const char *path, const struct timespec times[2]);

    void *(*opendir)(void *ctx, const char *dirname);
    int (*rmdir)(void *ctx, const char *path);

    int (*statvfs)(void *ctx, struct statvfs *buf);
    int (*syncfs)(void *ctx);
    int (*truncate)(void *ctx, const char *path, off_t length);

    int (*umount)(void *ctx);
};

struct vfs_file_vtable {
    int (*close)(void *ctx);
    int (*fstat)(void *ctx, struct stat *pstat);
    int (*futimens)(void *ctx, const struct timespec times[2]);
    int isdir : 1;
    int isatty : 1;
    int pollable : 1;
    off_t (*lseek)(void *ctx, off_t offset, int whence);
    int (*read)(void *ctx, void *buffer, size_t size);
    int (*write)(void *ctx, const void *buffer, size_t size);
    int (*pread)(void *ctx, void *buffer, size_t size, off_t offset);
    int (*pwrite)(void *ctx, const void *buffer, size_t size, off_t offset);

    struct dirent *(*readdir)(void *ctx);
    void (*rewinddir)(void *ctx);

    int (*fstatvfs)(void *ctx, struct statvfs *buf);
    int (*fsync)(void *ctx);
    int (*ftruncate)(void *ctx, off_t length);

    void *(*mmap)(void *ctx, void *addr, size_t len, int prot, int flags, off_t off);
    int (*ioctl)(void *ctx, unsigned long request, va_list args);
};

struct vfs_mount {
    const struct vfs_mount_vtable *func;
    int ref_count;
    char *path;
    size_t path_len;
    struct vfs_mount *next;
};

struct vfs_file {
    const struct vfs_file_vtable *func;
    int ref_count;
    int flags;
};

typedef struct {
    char *begin;
    char buf[256];
} vfs_path_buffer_t;

extern const struct vfs_filesystem *vfs_fss[];
extern const size_t vfs_num_fss;

// path functions
char *vfs_compare_path(const char *path1, const char *path2);
int vfs_expand_path(vfs_path_buffer_t *vfs_path, const char *path);

// mount functions
void vfs_mount_init(struct vfs_mount *vfs, const struct vfs_mount_vtable *func);
struct vfs_mount *vfs_acquire_mount(const char *file, vfs_path_buffer_t *vfs_path);
void vfs_release_mount(struct vfs_mount *vfs);
bool vfs_iterate_mount(struct vfs_mount **entry);
int vfs_mount(struct vfs_mount *vfs, int mountflags);

// file control functions
void vfs_file_init(struct vfs_file *file, const struct vfs_file_vtable *func, int flags);
struct vfs_file *vfs_acquire_file(int fd, int flags);
void vfs_release_file(struct vfs_file *file);
void *vfs_copy_file(struct vfs_file *file);
int vfs_replace(int fd, struct vfs_file *file);

// current working directory functions
void vfs_getcwd(char *buf, size_t size);
void vfs_setcwd(char *value);

// controlling TTY functions
struct vfs_file *vfs_gettty(void);
void vfs_settty(struct vfs_file *file);

// file operations
int vfs_fstat(struct vfs_file *file, struct stat *pstat);
int vfs_fsync(struct vfs_file *file);
int vfs_ftruncate(struct vfs_file *file, off_t length);
int vfs_vioctl(struct vfs_file *file, unsigned long request, va_list args);
int vfs_ioctl(struct vfs_file *file, unsigned long request, ...);
off_t vfs_lseek(struct vfs_file *file, off_t offset, int whence);
void *vfs_mmap(void *addr, size_t len, int prot, int flags, struct vfs_file *file, off_t off);
ssize_t vfs_pread(struct vfs_file *file, void *buffer, size_t size, off_t offset);
ssize_t vfs_pwrite(struct vfs_file *file, const void *buffer, size_t size, off_t offset);
int vfs_read(struct vfs_file *file, void *buffer, size_t size);
int vfs_write(struct vfs_file *file, const void *buffer, size_t size);
