// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <memory.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <termios.h>

#include "newlib/dlfcn.h"
#include "newlib/ioctl.h"
#include "newlib/newlib.h"
#include "newlib/vfs.h"
#include "newlib/thread.h"
#include "timers.h"


int close(int fd) {
    return vfs_close(fd);
}

void __attribute__((noreturn, weak)) _exit(int status) {
    // Picolibc's raise function calls _exit(128 + signum) for the default signal handler.
    if (status >= 128) {
        psignal(status - 128, "signal");
        fflush(stderr);
        status = 2;
    }

    vTaskSuspendAll();
    // switch to MSP stack
    // clear current task

    while (1) {
        __breakpoint();
    }
}

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

int fstat(int fd, struct stat *pstat) {
    int flags = 0;
    struct vfs_file *file = vfs_acquire_file(fd, &flags);
    if (!file) {
        return -1;
    }
    int ret = -1;
    if (file->func->fstat) {
        memset(pstat, 0, sizeof(struct stat));
        pstat->st_mode = file->mode;
        ret = file->func->fstat(file, pstat);
    } else {
        errno = ENOSYS;
    }
    vfs_release_file(file);
    return ret;
}

int getpid(void) {
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    TaskStatus_t task_status;
    vTaskGetInfo(task, &task_status, pdFALSE, eRunning);
    return task_status.xTaskNumber;
}

int isatty(int fd) {
    int flags = 0;
    struct vfs_file *file = vfs_acquire_file(fd, &flags);
    if (!file) {
        return -1;
    }
    int ret = file->func->isatty;
    vfs_release_file(file);
    return ret;
}

int kill(int pid, int sig) {
    if (pid != 0) {
        errno = EINVAL;
        return -1;
    }
    return raise(sig);
}

static void pending_kill_from_isr(void *pvParameter1, uint32_t ulParameter2) {
    int pid = (intptr_t)pvParameter1;
    int sig = ulParameter2;
    kill(pid, sig);
}

void kill_from_isr(int pid, int sig, BaseType_t *pxHigherPriorityTaskWoken) {
    BaseType_t ret = xTimerPendFunctionCallFromISR(
        pending_kill_from_isr,
        (void *)pid,
        sig,
        pxHigherPriorityTaskWoken);
    if (ret != pdPASS) {
        assert(0);
    }
}

off_t lseek(int fd, off_t offset, int whence) {
    int flags = 0;
    struct vfs_file *file = vfs_acquire_file(fd, &flags);
    if (!file) {
        return -1;
    }
    int ret = -1;
    if (file->func->lseek) {
        ret = file->func->lseek(file, offset, whence);
    } else {
        errno = S_ISCHR(file->mode) ? ESPIPE : ENOSYS;
    }
    vfs_release_file(file);
    return ret;
}

int mkdir(const char *path, mode_t mode) {
    vfs_path_buffer_t vfs_path;
    struct vfs_mount *vfs = vfs_acquire_mount(path, &vfs_path);
    if (!vfs) {
        return -1;
    }
    int ret = -1;
    if (vfs->func->mkdir) {
        ret = vfs->func->mkdir(vfs, vfs_path.begin, mode);
    } else {
        errno = ENOSYS;
    }
    vfs_release_mount(vfs);
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

int read(int fd, void *buffer, size_t size, off_t offset) {
    int flags = FREAD;
    struct vfs_file *file = vfs_acquire_file(fd, &flags);
    if (!file) {
        return -1;
    }
    int ret = -1;
    if (file->func->read) {
        do {
            ret = file->func->read(file, buffer, size);
        }
        while (!(flags & FNONBLOCK) && (ret < 0) && (errno == EAGAIN) && (poll_file(file, POLLIN, -1) >= 0));
    } else {
        errno = S_ISDIR(file->mode) ? EISDIR : ENOSYS;
    }
    vfs_release_file(file);
    return ret;
}

ssize_t pread(int fd, void *buffer, size_t size, off_t offset) {
    if (lseek(fd, 0, SEEK_CUR) < 0) {
        return -1;
    }
    if (offset < 0) {
        errno = EINVAL;
        return -1;
    }
    int flags = FREAD;
    struct vfs_file *file = vfs_acquire_file(fd, &flags);
    if (!file) {
        return -1;
    }
    int ret = -1;
    if (file->func->read) {
        do {
            ret = file->func->pread(file, buffer, size, offset);
        }
        while (!(flags & FNONBLOCK) && (ret < 0) && (errno == EAGAIN) && (poll_file(file, POLLIN, -1) >= 0));
    } else {
        errno = S_ISDIR(file->mode) ? EISDIR : ENOSYS;
    }
    vfs_release_file(file);
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

int stat(const char *file, struct stat *pstat) {
    vfs_path_buffer_t vfs_path;
    struct vfs_mount *vfs = vfs_acquire_mount(file, &vfs_path);
    if (!vfs) {
        return -1;
    }
    int ret = -1;
    if (vfs->func->stat) {
        memset(pstat, 0, sizeof(struct stat));
        ret = vfs->func->stat(vfs, vfs_path.begin, pstat);
    } else {
        errno = ENOSYS;
    }
    vfs_release_mount(vfs);
    return ret;
}

__attribute__((noinline))
int *tls_errno(void) {
    return &errno;
}

int unlink(const char *file) {
    vfs_path_buffer_t vfs_path;
    struct vfs_mount *vfs = vfs_acquire_mount(file, &vfs_path);
    if (!vfs) {
        return -1;
    }
    int ret = -1;
    if (vfs->func->unlink) {
        ret = vfs->func->unlink(vfs, vfs_path.begin);
    } else {
        errno = ENOSYS;
    }
    vfs_release_mount(vfs);
    return ret;
}

int write(int fd, const void *buffer, size_t size) {
    int flags = FWRITE;
    struct vfs_file *file = vfs_acquire_file(fd, &flags);
    if (!file) {
        return -1;
    }
    int ret = -1;
    if (file->func->write) {
        do {
            ret = file->func->write(file, buffer, size);
        }
        while (!(flags & FNONBLOCK) && (ret < 0) && (errno == EAGAIN) && (poll_file(file, POLLOUT, -1) >= 0));
    } else {
        errno = S_ISDIR(file->mode) ? EISDIR : ENOSYS;
    }
    vfs_release_file(file);
    return ret;
}

ssize_t pwrite(int fd, const void *buffer, size_t size, off_t offset) {
    if (lseek(fd, 0, SEEK_CUR) < 0) {
        return -1;
    }
    if (offset < 0) {
        errno = EINVAL;
        return -1;
    }
    int flags = FWRITE;
    struct vfs_file *file = vfs_acquire_file(fd, &flags);
    if (!file) {
        return -1;
    }
    int ret = -1;
    if (file->func->write) {
        do {
            ret = file->func->pwrite(file, buffer, size, offset);
        }
        while (!(flags & FNONBLOCK) && (ret < 0) && (errno == EAGAIN) && (poll_file(file, POLLOUT, -1) >= 0));
    } else {
        errno = S_ISDIR(file->mode) ? EISDIR : ENOSYS;
    }
    vfs_release_file(file);
    return ret;
}
