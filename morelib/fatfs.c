// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include "freertos/timers.h"
#include "morelib/stat.h"
#include "morelib/vfs.h"

#include "FreeRTOS.h"
#include "timers.h"

#include "ff.h"
#include "diskio.h"
#undef DIR


static struct fatfs_volume {
    struct vfs_mount base;
    int id;
    int fd;
    size_t ssize;
    char path[4];
    struct timespec mtime;
    FATFS fs;
    TimerHandle_t flush_timer;
    DRESULT flush_result;
    StaticTimer_t xTimerBuffer;
} *fatfs_drv_map[FF_VOLUMES];


static void fatfs_flush_callback(TimerHandle_t xTimer) {
    struct fatfs_volume *vol = pvTimerGetTimerID(xTimer);
    vol->flush_result = ioctl(vol->fd, BLKFLSBUF) >= 0 ? RES_OK : RES_ERROR;
}

static const struct vfs_mount_vtable fatfs_vtable;

static struct fatfs_volume *fatfs_alloc_volume(const char *device, int flags) {
    struct fatfs_volume *vol = NULL;
    int fd = open(device, flags, 0);
    if (fd < 0) {
        return NULL;
    }
    int ssize;
    if (ioctl(fd, BLKSSZGET, &ssize) < 0) {
        goto cleanup;
    }
    vol = calloc(1, sizeof(struct fatfs_volume));
    if (!vol) {
        goto cleanup;
    }

    int id;
    taskENTER_CRITICAL();
    for (id = 0; id < FF_VOLUMES; id++) {
        if (!fatfs_drv_map[id]) {
            fatfs_drv_map[id] = vol;
            break;
        }
    }
    taskEXIT_CRITICAL();
    if (id == FF_VOLUMES) {
        errno = ENFILE;
        goto cleanup;
    }

    vfs_mount_init(&vol->base, &fatfs_vtable);
    vol->id = id;
    vol->fd = fd;
    vol->ssize = ssize;
    vol->path[0] = id + '0';
    vol->path[1] = ':';
    vol->path[2] = '\0';
    vol->flush_timer = xTimerCreateStatic("fatfs", pdMS_TO_TICKS(500), pdFALSE, vol, fatfs_flush_callback, &vol->xTimerBuffer);
    vol->flush_result = RES_OK;
    return vol;

cleanup:
    if (vol) {
        free(vol);
    }
    close(fd);
    return NULL;
}

static void fatfs_free_volume(struct fatfs_volume *vol) {
    taskENTER_CRITICAL();
    assert(fatfs_drv_map[vol->id] == vol);
    fatfs_drv_map[vol->id] = NULL;
    taskEXIT_CRITICAL();
    xTimerDelete(vol->flush_timer, portMAX_DELAY);
    xTimerSyncTimerDaemon();
    close(vol->fd);
    free(vol);
}

static struct fatfs_volume *fatfs_get_fd(int id) {
    if ((uint)id >= FF_VOLUMES) {
        errno = EBADF;
        return NULL;
    }
    taskENTER_CRITICAL();
    struct fatfs_volume *vol = fatfs_drv_map[id];
    taskEXIT_CRITICAL();
    if (!vol) {
        errno = EBADF;
        return NULL;
    }
    return vol;
}

// this table converts from FRESULT to POSIX errno
static const uint8_t fatfs_err_map[20] = {
    [FR_OK] = 0,
    [FR_DISK_ERR] = EIO,
    [FR_INT_ERR] = EIO,
    [FR_NOT_READY] = EBUSY,
    [FR_NO_FILE] = ENOENT,
    [FR_NO_PATH] = ENOENT,
    [FR_INVALID_NAME] = EINVAL,
    [FR_DENIED] = EACCES,
    [FR_EXIST] = EEXIST,
    [FR_INVALID_OBJECT] = EINVAL,
    [FR_WRITE_PROTECTED] = EROFS,
    [FR_INVALID_DRIVE] = ENODEV,
    [FR_NOT_ENABLED] = ENODEV,
    [FR_NO_FILESYSTEM] = ENODEV,
    [FR_MKFS_ABORTED] = EIO,
    [FR_TIMEOUT] = EIO,
    [FR_LOCKED] = EIO,
    [FR_NOT_ENOUGH_CORE] = ENOMEM,
    [FR_TOO_MANY_OPEN_FILES] = EMFILE,
    [FR_INVALID_PARAMETER] = EINVAL,
};


static int fatfs_result(FRESULT result) {
    if (result == F_OK) {
        return 0;
    }
    errno = fatfs_err_map[result];
    return -1;
}

struct fatfs_file {
    struct vfs_file base;
    FIL fp;
};

struct fatfs_dir {
    struct vfs_file base;
    f_DIR dp;
    FILINFO fno;
    struct dirent dirent;
};

static const struct vfs_mount_vtable fatfs_vtable;

static const struct vfs_file_vtable fatfs_file_vtable;

static const struct vfs_file_vtable fatfs_dir_vtable;

static int fatfs_mkfs(const void *ctx, const char *source, const char *data) {
    int result = -1;

    struct fatfs_volume *vol = fatfs_alloc_volume(source, O_RDWR | O_TRUNC);
    if (!vol) {
        return -1;
    }

    const MKFS_PARM opt = { FM_ANY | FM_SFD, 0, 0, 0, 0 };

    void *work = malloc(FF_MAX_SS);
    if (!work) {
        errno = ENOMEM;
        goto cleanup;
    }

    result = fatfs_result(f_mkfs(vol->path, &opt, work, FF_MAX_SS));

cleanup:
    fatfs_free_volume(vol);
    free(work);
    return result;
}

static void *fatfs_mount(const void *ctx, const char *source, unsigned long mountflags, const char *data) {
    struct fatfs_volume *vol = fatfs_alloc_volume(source, O_RDWR);
    if (!vol) {
        return NULL;
    }
    mountflags &= MS_RDONLY;
    if (ioctl(vol->fd, BLKROSET, &mountflags) < 0) {
        goto cleanup;
    }
    if (fatfs_result(f_mount(&vol->fs, vol->path, 1)) < 0) {
        goto cleanup;
    }
    return vol;

cleanup:
    fatfs_free_volume(vol);
    return NULL;
}

const struct vfs_filesystem fatfs_fs = {
    .type = "fatfs",
    .mkfs = fatfs_mkfs,
    .mount = fatfs_mount,
};

static char *fatfs_path(struct fatfs_volume *vfs, const char *path) {
    size_t len = strlen(vfs->path);
    char *ppath = (char *)path - len;
    return strncpy(ppath, vfs->path, len);
}

static int fatfs_mkdir(void *ctx, const char *path, mode_t mode) {
    struct fatfs_volume *vfs = ctx;
    path = fatfs_path(vfs, path);
    return fatfs_result(f_mkdir(path));
}

static void *fatfs_open(void *ctx, const char *path, int flags, mode_t mode) {
    struct fatfs_volume *vfs = ctx;
    path = fatfs_path(vfs, path);

    BYTE fatfs_mode = 0;
    switch (flags & O_ACCMODE) {
        case O_RDONLY:
            fatfs_mode = FA_READ;
            break;
        case O_WRONLY:
            fatfs_mode = FA_WRITE;
            break;
        case O_RDWR:
            fatfs_mode = FA_READ | FA_WRITE;
            break;
    }
    switch (flags & (O_CREAT | O_APPEND | O_TRUNC | O_EXCL)) {
        case O_CREAT:
            fatfs_mode |= FA_OPEN_ALWAYS;
            break;
        case O_CREAT | O_TRUNC:
            fatfs_mode |= FA_CREATE_ALWAYS;
            break;
        case O_CREAT | O_APPEND:
            fatfs_mode |= FA_OPEN_APPEND;
            break;
        case O_CREAT | O_EXCL:
        case O_CREAT | O_EXCL | O_TRUNC:
        case O_CREAT | O_EXCL | O_APPEND:
        case O_CREAT | O_EXCL | O_TRUNC | O_APPEND:
            fatfs_mode |= FA_CREATE_NEW;
        case 0:
            break;
        default:
            errno = EINVAL;
            return NULL;
    }

    struct fatfs_file *file = calloc(1, sizeof(struct fatfs_file));
    vfs_file_init(&file->base, &fatfs_file_vtable, flags);
    if (fatfs_result(f_open(&file->fp, path, fatfs_mode)) < 0) {
        free(file);
        file = NULL;
    }
    return file;
}

static void *fatfs_opendir(void *ctx, const char *dirname) {
    struct fatfs_volume *vfs = ctx;
    dirname = fatfs_path(vfs, dirname);
    struct fatfs_dir *dir = malloc(sizeof(struct fatfs_dir));
    vfs_file_init(&dir->base, &fatfs_dir_vtable, O_RDONLY | O_DIRECTORY);
    if (fatfs_result(f_opendir(&dir->dp, dirname)) < 0) {
        free(dir);
        dir = NULL;
    }
    return dir;
}

static int fatfs_rename(void *ctx, const char *old, const char *new) {
    struct fatfs_volume *vfs = ctx;
    old = fatfs_path(vfs, old);
    new = fatfs_path(vfs, new);
    FILINFO fno;
    int ret = f_stat(new, &fno);
    if (ret == FR_OK) {
        ret = f_unlink(new);
    }
    else if (ret == FR_NO_FILE) {
        ret = FR_OK;
    }
    if (ret != FR_OK) {
        return fatfs_result(ret);
    }
    return fatfs_result(f_rename(old, new));
}

static time_t fatfs_init_time(const FILINFO *fno) {
    struct tm tm = {
        .tm_sec = (fno->ftime & 0x1f) << 1,
            .tm_min = (fno->ftime >> 5) & 0x3f,
            .tm_hour = (fno->ftime >> 11) & 0x1f,
            .tm_mday = fno->fdate & 0x1f,
            .tm_mon = ((fno->fdate >> 5) & 0x0f) - 1,
            .tm_year = ((fno->fdate >> 9) & 0x7f) + 80,
            .tm_isdst = 0,
    };
    return mktime(&tm);
}

static void fatfs_init_stat(struct fatfs_volume *vfs, mode_t mode, size_t size, time_t time, struct stat *pstat) {
    pstat->st_mode = mode;
    pstat->st_size = size;
    #if FF_MAX_SS != FF_MIN_SS
    pstat->st_blksize = vfs->fs.ssize;
    #else
    pstat->st_blksize = FF_MAX_SS;
    #endif
    pstat->st_atim.tv_sec = time;
    pstat->st_mtim.tv_sec = time;
    pstat->st_ctim.tv_sec = time;
}

static int fatfs_stat(void *ctx, const char *file, struct stat *pstat) {
    struct fatfs_volume *vfs = ctx;
    FILINFO fno = { .fattrib = AM_DIR };
    if (strcmp(file, "/") != 0) {
        file = fatfs_path(vfs, file);
        if (fatfs_result(f_stat(file, &fno)) < 0) {
            return -1;
        }
    }
    time_t time = fatfs_init_time(&fno);
    fatfs_init_stat(vfs, fno.fattrib & AM_DIR ? S_IFDIR : S_IFREG, fno.fsize, time, pstat);
    return 0;
}

static int fatfs_syncfs(void *ctx) {
    struct fatfs_volume *vfs = ctx;
    xTimerStop(vfs->flush_timer, 0);
    return ioctl(vfs->fd, BLKFLSBUF);
}

static int fatfs_statvfs(void *ctx, struct statvfs *buf) {
    struct fatfs_volume *vfs = ctx;
    DWORD nclst;
    FATFS *fatfs;
    if (fatfs_result(f_getfree(vfs->path, &nclst, &fatfs)) < 0) {
        return -1;
    }

    #if FF_MAX_SS != FF_MIN_SS
    buf->f_bsize = fatfs->csize * fatfs->ssize;
    #else
    buf->f_bsize = fatfs->csize * FF_MAX_SS;
    #endif
    buf->f_frsize = buf->f_bsize;
    buf->f_blocks = fatfs->n_fatent - 2;
    buf->f_bfree = nclst;
    buf->f_bavail = buf->f_bfree;
    buf->f_files = 0;
    buf->f_ffree = 0;
    buf->f_favail = 0;
    buf->f_fsid = 0;
    buf->f_flag = 0;
    buf->f_namemax = FF_MAX_LFN;
    return 0;
}

static int fatfs_truncate(void *ctx, const char *path, off_t length) {
    struct fatfs_volume *vfs = ctx;
    path = fatfs_path(vfs, path);
    FIL fp;
    if (fatfs_result(f_open(&fp, path, FA_WRITE)) < 0) {
        return -1;
    }
    int ret = -1;
    if (fatfs_result(f_lseek(&fp, length)) >= 0) {
        ret = fatfs_result(f_truncate(&fp));
    }
    f_close(&fp);
    return ret;
}

static int fatfs_umount(void *ctx) {
    struct fatfs_volume *vfs = ctx;
    int result = fatfs_result(f_unmount(vfs->path));
    fatfs_free_volume(vfs);
    return result;
}

static int fatfs_unlink(void *ctx, const char *file) {
    struct fatfs_volume *vfs = ctx;
    file = fatfs_path(vfs, file);
    return fatfs_result(f_unlink(file));
}

static void fatfs_init_FILINFO(const time_t *tv_sec, FILINFO *fno) {
    struct tm *tm = localtime(tv_sec);
    if (tm) {
        fno->ftime = (tm->tm_sec >> 1) & 0x1f;
        fno->ftime |= (tm->tm_min & 0x3f) << 5;
        fno->ftime |= (tm->tm_hour & 0x1f) << 11;
        fno->fdate = tm->tm_mday & 0x1f;
        fno->fdate |= ((tm->tm_mon + 1) & 0x0f) << 5;
        fno->fdate |= ((tm->tm_year - 80) & 0x7f) << 9;
    }
    else {
        fno->ftime = 0;
        fno->fdate = 0;
    }
}

static int fatfs_utimens(void *ctx, const char *file, const struct timespec times[2]) {
    struct fatfs_volume *vfs = ctx;
    file = fatfs_path(vfs, file);
    if (times[1].tv_nsec == UTIME_OMIT) {
        return 0;
    }
    time_t mtime = (times[1].tv_nsec != UTIME_NOW) ? times[1].tv_sec : time(NULL);
    FILINFO fno;
    fatfs_init_FILINFO(&mtime, &fno);
    return fatfs_result(f_utime(file, &fno));
}

static const struct vfs_mount_vtable fatfs_vtable = {
    .mkdir = fatfs_mkdir,
    .open = fatfs_open,
    .rename = fatfs_rename,
    .stat = fatfs_stat,
    .umount = fatfs_umount,
    .unlink = fatfs_unlink,
    .utimens = fatfs_utimens,

    .opendir = fatfs_opendir,
    .rmdir = fatfs_unlink,

    .statvfs = fatfs_statvfs,
    .syncfs = fatfs_syncfs,
    .truncate = fatfs_truncate,
};


static int fatfs_close(void *ctx) {
    struct fatfs_file *file = ctx;
    int result = fatfs_result(f_close(&file->fp));
    free(file);
    return result;
}

static int fatfs_closedir(void *ctx) {
    struct fatfs_dir *dir = ctx;
    int result = fatfs_result(f_closedir(&dir->dp));
    free(dir);
    return result;
}

static int fatfs_fstat(void *ctx, struct stat *pstat) {
    struct fatfs_file *file = ctx;
    // No-op that ensures file->fp is valid
    if (fatfs_result(f_lseek(&file->fp, f_tell(&file->fp))) < 0) {
        return -1;
    }
    struct fatfs_volume *vfs = (struct fatfs_volume *)((char *)file->fp.obj.fs - offsetof(struct fatfs_volume, fs));
    fatfs_init_stat(vfs, S_IFREG, f_size(&file->fp), 0, pstat);
    return 0;
}

static int fatfs_ftruncate(void *ctx, off_t length) {
    struct fatfs_file *file = ctx;
    off_t original = f_tell(&file->fp);
    if (fatfs_result(f_lseek(&file->fp, length)) < 0) {
        return -1;
    }
    int ret = fatfs_result(f_truncate(&file->fp));
    if ((original < length) && (fatfs_result(f_lseek(&file->fp, original)) < 0)) {
        return -1;
    }
    return ret;
}

static off_t fatfs_lseek(void *ctx, off_t offset, int whence) {
    struct fatfs_file *file = ctx;
    switch (whence) {
        case SEEK_SET:
            break;
        case SEEK_CUR:
            offset += f_tell(&file->fp);
            break;
        case SEEK_END:
            offset += f_size(&file->fp);
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    if (fatfs_result(f_lseek(&file->fp, offset)) < 0) {
        return -1;
    }
    return offset;
}

static int fatfs_pread(void *ctx, void *buffer, size_t size, off_t offset) {
    struct fatfs_file *file = ctx;
    off_t original = f_tell(&file->fp);
    if (fatfs_result(f_lseek(&file->fp, offset)) < 0) {
        return -1;
    }
    UINT br;
    int ret = fatfs_result(f_read(&file->fp, buffer, size, &br));
    if (fatfs_result(f_lseek(&file->fp, original)) < 0) {
        return -1;
    }
    return (ret < 0) ? -1 : br;
}

static int fatfs_read(void *ctx, void *buffer, size_t size) {
    struct fatfs_file *file = ctx;
    UINT br;
    if (fatfs_result(f_read(&file->fp, buffer, size, &br)) < 0) {
        return -1;
    }
    return br;
}

static struct dirent *fatfs_readdir(void *ctx) {
    struct fatfs_dir *dir = ctx;
    int orig_errno = errno;
    if (fatfs_result(f_readdir(&dir->dp, &dir->fno))) {
        return NULL;
    }
    errno = orig_errno;
    dir->dirent.d_ino = 0;
    dir->dirent.d_type = dir->fno.fattrib & AM_DIR ? DT_DIR : DT_REG;
    dir->dirent.d_name = dir->fno.fname;
    return strlen(dir->dirent.d_name) ? &dir->dirent : NULL;
}

static void fatfs_rewinddir(void *ctx) {
    struct fatfs_dir *dir = ctx;
    fatfs_result(f_rewinddir(&dir->dp));
}

static int fatfs_pwrite(void *ctx, const void *buffer, size_t size, off_t offset) {
    struct fatfs_file *file = ctx;
    off_t original = f_tell(&file->fp);
    if (fatfs_result(f_lseek(&file->fp, offset)) < 0) {
        return -1;
    }
    UINT bw;
    int ret = fatfs_result(f_write(&file->fp, buffer, size, &bw));
    if (fatfs_result(f_lseek(&file->fp, original)) < 0) {
        return -1;
    }
    return (ret < 0) ? -1 : bw;
}

static int fatfs_write(void *ctx, const void *buffer, size_t size) {
    struct fatfs_file *file = ctx;
    UINT bw;
    if (fatfs_result(f_write(&file->fp, buffer, size, &bw)) < 0) {
        return -1;
    }
    return bw;
}

static const struct vfs_file_vtable fatfs_file_vtable = {
    .close = fatfs_close,
    .fstat = fatfs_fstat,
    .ftruncate = fatfs_ftruncate,
    .lseek = fatfs_lseek,
    .pread = fatfs_pread,
    .pwrite = fatfs_pwrite,
    .read = fatfs_read,
    .write = fatfs_write,
};

static const struct vfs_file_vtable fatfs_dir_vtable = {
    .close = fatfs_closedir,
    .isdir = 1,
    .readdir = fatfs_readdir,
    .rewinddir = fatfs_rewinddir,
};

#include "diskio.h"

__attribute__((visibility("hidden")))
DSTATUS disk_initialize(BYTE pdrv) {
    return disk_status(pdrv);
}

__attribute__((visibility("hidden")))
DSTATUS disk_status(BYTE pdrv) {
    struct fatfs_volume *vol = fatfs_get_fd(pdrv);
    if (!vol) {
        return STA_NOINIT;
    }

    struct stat buf;
    if (fstat(vol->fd, &buf) >= 0) {
        if (timespeccmp(&buf.st_mtim, &vol->mtime, >)) {
            // if someone else modified the disk, invalidate the fat disk cache
            vol->mtime = buf.st_mtim;
            vol->fs.winsect = -1;
        }
    }

    int ro = 0;
    ioctl(vol->fd, BLKROGET, &ro);
    return ro ? STA_PROTECT : 0;
}

__attribute__((visibility("hidden")))
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    struct fatfs_volume *vol = fatfs_get_fd(pdrv);
    if (!vol) {
        return RES_PARERR;
    }

    lseek(vol->fd, sector * vol->ssize, SEEK_SET);
    if (read(vol->fd, buff, count * vol->ssize) < 0) {
        return RES_ERROR;
    }
    return RES_OK;
}

__attribute__((visibility("hidden")))
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    struct fatfs_volume *vol = fatfs_get_fd(pdrv);
    if (!vol) {
        return RES_PARERR;
    }

    lseek(vol->fd, sector * vol->ssize, SEEK_SET);
    if (write(vol->fd, buff, count * vol->ssize) < 0) {
        return (errno == EROFS) ? RES_WRPRT : RES_ERROR;
    }

    struct stat buf;
    if (fstat(vol->fd, &buf) >= 0) {
        vol->mtime = buf.st_mtim;
    }
    return RES_OK;
}

__attribute__((visibility("hidden")))
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    struct fatfs_volume *vol = fatfs_get_fd(pdrv);
    if (!vol) {
        return RES_PARERR;
    }
    switch (cmd) {
        case CTRL_SYNC: {
            if (xTimerReset(vol->flush_timer, 0)) {
                return vol->flush_result;
            }
            else {
                return ioctl(vol->fd, BLKFLSBUF) >= 0 ? RES_OK : RES_ERROR;
            }
        }
        case GET_SECTOR_COUNT: {
            unsigned long size;
            if (ioctl(vol->fd, BLKGETSIZE, &size) < 0) {
                return RES_ERROR;
            }
            *(LBA_t *)buff = (size << 9) / vol->ssize;
            return RES_OK;
        }
        #if FF_MAX_SS != FF_MIN_SS
        case GET_SECTOR_SIZE: {
            *(WORD *)buff = vol->ssize;
            return RES_OK;
        }
        #endif
        case GET_BLOCK_SIZE: {
            *(DWORD *)buff = 1;
            return RES_OK;
        }
        #if FF_USE_TRIM
        case CTRL_TRIM: {
            LBA_t *lba = buff;
            uint64_t range[] = { lba[0], lba[1] - lba[0] };
            if (ioctl(vol->fd, BLKDISCARD, range) < 0) {
                return RES_ERROR;
            }
            return RES_OK;
        }
        #endif
        default: {
            return RES_PARERR;
        }
    }
}

__attribute__((visibility("hidden")))
DWORD get_fattime(void) {
    time_t t = time(0);
    struct tm *stm = localtime(&t);
    return (DWORD)(stm->tm_year - 80) << 25 |
                   (DWORD)(stm->tm_mon + 1) << 21 |
                   (DWORD)stm->tm_mday << 16 |
                   (DWORD)stm->tm_hour << 11 |
                   (DWORD)stm->tm_min << 5 |
                   (DWORD)stm->tm_sec >> 1;
}
