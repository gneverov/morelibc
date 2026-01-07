// SPDX-FileCopyrightText: 2025 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/stat.h>
#include "morelib/ioctl.h"
#include "morelib/loop.h"
#include "morelib/mtd.h"


static struct loop_file *loop_files[LOOP_NUM_DEVICES];

static int loop_close(void *ctx) {
    struct loop_file *file = ctx;
    dev_lock();
    assert(loop_files[file->index] == file);
    loop_files[file->index] = NULL;
    dev_unlock();

    if (file->file) {
        vfs_release_file(file->file);
    }
    free(file);
    return 0;
}

static int loop_fsync(void *ctx) {
    struct loop_file *file = ctx;
    return vfs_fsync(file->file);
}

static int loop_size(struct loop_file *file) {
    struct stat stat;
    int ret = vfs_fstat(file->file, &stat);
    if (ret >= 0) {
        return stat.st_size;
    }
    return ret;
}

static int loop_fstat(void *ctx, struct stat *pstat) {
    struct loop_file *file = ctx;
    pstat->st_mode = S_IFBLK;
    pstat->st_rdev = DEV_LOOP0 | file->index;
    return 0;
}

static int loop_ioctl(void *ctx, unsigned long request, va_list args) {
    struct loop_file *file = ctx;
    int ret = -1;
    switch (request) {
        case LOOP_SET_FD: {
            int fd = va_arg(args, int);
            struct vfs_file *target = vfs_acquire_file(fd, FREAD | FWRITE);
            if (!target) {
                break;
            }                
            if (file->file) {
                vfs_release_file(file->file);
            }
            file->file = target;
            ret = 0;
            break;
        }
        case LOOP_CLR_FD: {
            if (file->file) {
                vfs_release_file(file->file);
            }
            file->file = NULL;
            ret = 0;
            break;
        }

        case BLKROSET: {
            const int *ro = va_arg(args, const int *);
            file->ro = *ro;
            ret = 0;
            break;
        }

        case BLKROGET: {
            int *ro = va_arg(args, int *);
            *ro = file->ro;
            ret = 0;
            break;
        }

        case BLKGETSIZE: {
            unsigned long *size = va_arg(args, unsigned long *);
            ret = loop_size(file);
            if (ret >= 0) {
                *size = ret >> 9;
                ret = 0;
            }
            break;
        }

        case BLKFLSBUF: {
            ret = 0;
            break;
        }

        case BLKSSZGET: {
            int *ssize = va_arg(args, int *);
            *ssize = 512;
            ret = 0;
            break;
        }

        case BLKDISCARD: {
            ret = 0;
            break;
        }

        case MEMGETINFO: {
            ret = loop_size(file);
            if (ret >= 0) {
                struct mtd_info *info = va_arg(args, struct mtd_info *);
                info->type = MTD_ABSENT;
                info->flags = MTD_CAP_RAM;
                info->size = ret;
                info->erasesize = 512;
                info->writesize = 512;
                info->oobsize = 0;
                ret = 0;
            }
            break;
        }
        case MEMERASE: {
            ret = 0;
            break;
        }

        case MEMWRITEOOB:
        case MEMREADOOB:
        case MEMLOCK:
        case MEMUNLOCK: {
            errno = ENOTSUP;
            break;
        }

        default: {
            ret = vfs_vioctl(file->file, request, args);
            break;
        }
    }
    return ret;
}

static off_t loop_lseek(void *ctx, off_t pos, int whence) {
    struct loop_file *file = ctx;
    return vfs_lseek(file->file, pos, whence);
}

static void *loop_mmap(void *ctx, void *addr, size_t len, int prot, int flags, off_t off) {
    struct loop_file *file = ctx;
    return vfs_mmap(addr, len, prot, flags, file->file, off);
}

static int loop_pread(void *ctx, void *buffer, size_t size, off_t offset) {
    struct loop_file *file = ctx;
    return vfs_pread(file->file, buffer, size, offset);
}

static int loop_read(void *ctx, void *buffer, size_t size) {
    struct loop_file *file = ctx;
    return vfs_read(file->file, buffer, size);
}

static int loop_pwrite(void *ctx, const void *buffer, size_t size, off_t offset) {
    struct loop_file *file = ctx;
    if (file->ro) {
        errno = EROFS;
        return -1;
    }
    return vfs_pwrite(file->file, buffer, size, offset);
}

static int loop_write(void *ctx, const void *buffer, size_t size) {
    struct loop_file *file = ctx;
    if (file->ro) {
        errno = EROFS;
        return -1;
    }    
    return vfs_write(file->file, buffer, size);
}

static const struct vfs_file_vtable loop_vtable = {
    .close = loop_close,
    .fstat = loop_fstat,
    .fsync = loop_fsync,
    .ioctl = loop_ioctl,
    .lseek = loop_lseek,
    .mmap = loop_mmap,
    .pread = loop_pread,
    .pwrite = loop_pwrite,
    .read = loop_read,
    .write = loop_write,
};

void *loop_open(const void *ctx, dev_t dev, int flags) {
    uint index = minor(dev);
    if (index >= LOOP_NUM_DEVICES) {
        errno = ENODEV;
        return NULL;
    }
    dev_lock();
    struct loop_file *file = loop_files[index];
    if (file) {
        vfs_copy_file(&file->base);
        goto exit;
    }    
    file = calloc(1, sizeof(struct loop_file));
    if (!file) {
        goto exit;
    }
    vfs_file_init(&file->base, &loop_vtable, O_RDWR);
    file->index = index;
    loop_files[index] = file;

exit:
    dev_unlock();
    return file;
}

const struct dev_driver loop_drv = {
    .dev = DEV_LOOP0,
    .open = loop_open,
};
