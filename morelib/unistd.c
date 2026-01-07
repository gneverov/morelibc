// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>
#include "morelib/dev.h"
#include "morelib/poll.h"
#include "morelib/vfs.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"


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
    // flush uart

    while (1) {
        __breakpoint();
    }
}

static TimerHandle_t alarm_timer;

static void alarm_callback(TimerHandle_t xTimer) {
    if (pvTimerGetTimerID(xTimer)) {
        raise(SIGALRM);
    }
}

unsigned alarm(unsigned seconds) {
    TickType_t xTimerPeriod = pdMS_TO_TICKS(1000u * seconds);
    unsigned result = 0;
    dev_lock();
    if (alarm_timer) {
        vTimerSetTimerID(alarm_timer, NULL);
        TickType_t xExpiryTime = xTimerGetExpiryTime(alarm_timer);
        result = (xExpiryTime - xTaskGetTickCount()) / configTICK_RATE_HZ;
        xTimerDelete(alarm_timer, portMAX_DELAY);
        alarm_timer = NULL;
    }
    if (xTimerPeriod) {
        alarm_timer = xTimerCreate("alarm", xTimerPeriod, pdFALSE, (void *)pdTRUE, alarm_callback);
        xTimerStart(alarm_timer, portMAX_DELAY);
    }
    dev_unlock();
    return result;
}

int chdir(const char *path) {
    vfs_path_buffer_t vfs_path;
    struct vfs_mount *vfs = vfs_acquire_mount(path, &vfs_path);
    if (!vfs) {
        return -1;
    }
    int ret = -1;
    struct stat buf = { .st_mode = S_IFDIR };
    if (strcmp(vfs_path.begin, "/") == 0) {
        // Root is always a valid directory and filesystem may not support stat of root directory.
        ret = 0;
    } else if (vfs->func->stat) {
        ret = vfs->func->stat(vfs, vfs_path.begin, &buf);
    } else {
        errno = ENOSYS;
    }
    vfs_release_mount(vfs);

    if (ret < 0) {
        return -1;
    }
    if (!S_ISDIR(buf.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    // Re-expand path since the call to stat may have modified it.
    if (vfs_expand_path(&vfs_path, path) < 0) {
        return -1;
    }
    vfs_setcwd(vfs_path.begin);
    return 0;
}

char *ctermid(char *s) {
    char *ret = "/dev/tty";
    return s ? strcpy(s, ret) : ret;
}

int dup(int oldfd) {
    struct vfs_file *old_file = vfs_acquire_file(oldfd, 0);
    if (!old_file) {
        return -1;
    }
    int ret = vfs_replace(-1, old_file);
    vfs_release_file(old_file);
    return ret;
}

int dup2(int oldfd, int newfd) {
    if ((uint)newfd >= VFS_FD_MAX) {
        errno = EBADF;
        return -1;
    }
    struct vfs_file *old_file = vfs_acquire_file(oldfd, 0);
    if (!old_file) {
        return -1;
    }
    int ret = vfs_replace(newfd, old_file);
    vfs_release_file(old_file);
    return ret;
}

int fsync(int fd) {
    struct vfs_file *file = vfs_acquire_file(fd, 0);
    if (!file) {
        return -1;
    }
    int ret = vfs_fsync(file);
    vfs_release_file(file);
    return ret;
}

int ftruncate(int fd, off_t length) {
    struct vfs_file *file = vfs_acquire_file(fd, FWRITE);
    if (!file) {
        return -1;
    }
    int ret = vfs_ftruncate(file, length);
    vfs_release_file(file);
    return ret;
}

char *getcwd(char *buf, size_t size) {
    vfs_getcwd(buf, size);
    return buf;
}

int getentropy(void *buffer, size_t length) {
    return getrandom(buffer, length, 0) >= length ? 0 : -1;
}

int gethostname(char *name, size_t namelen) {
    const char *hostname = getenv("HOSTNAME");
    strncpy(name, hostname ? hostname : "", namelen);
    return 0;
}

int getpid(void) {
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    TaskStatus_t task_status;
    vTaskGetInfo(task, &task_status, pdFALSE, eRunning);
    return task_status.xTaskNumber;
}

int isatty(int fd) {
    struct vfs_file *file = vfs_acquire_file(fd, 0);
    if (!file) {
        return -1;
    }
    int ret = file->func->isatty;
    vfs_release_file(file);
    return ret;
}

off_t lseek(int fd, off_t offset, int whence) {
    struct vfs_file *file = vfs_acquire_file(fd, 0);
    if (!file) {
        return -1;
    }
    int ret = vfs_lseek(file, offset, whence);
    vfs_release_file(file);
    return ret;
}

ssize_t pread(int fd, void *buffer, size_t size, off_t offset) {
    struct vfs_file *file = vfs_acquire_file(fd, FREAD);
    if (!file) {
        return -1;
    }
    int ret = vfs_pread(file, buffer, size, offset);
    vfs_release_file(file);
    return ret;
}

ssize_t pwrite(int fd, const void *buffer, size_t size, off_t offset) {
    struct vfs_file *file = vfs_acquire_file(fd, FWRITE);
    if (!file) {
        return -1;
    }
    int ret = vfs_pwrite(file, buffer, size, offset);
    vfs_release_file(file);
    return ret;
}

int read(int fd, void *buffer, size_t size) {
    struct vfs_file *file = vfs_acquire_file(fd, FREAD);
    if (!file) {
        return -1;
    }
    int ret = vfs_read(file, buffer, size);
    vfs_release_file(file);
    return ret;
}

char *realpath(const char *file_name, char *resolved_name) {
    vfs_path_buffer_t vfs_path;
    if (vfs_expand_path(&vfs_path, file_name) < 0) {
        return NULL;
    }
    return resolved_name ? strcpy(resolved_name, vfs_path.begin) : strdup(vfs_path.begin);
}

int rmdir(const char *path) {
    vfs_path_buffer_t vfs_path;
    struct vfs_mount *vfs = vfs_acquire_mount(path, &vfs_path);
    if (!vfs) {
        return -1;
    }
    int ret = -1;
    if (vfs->func->rmdir) {
        ret = vfs->func->rmdir(vfs, vfs_path.begin);
    } else {
        errno = ENOSYS;
    }
    vfs_release_mount(vfs);
    return ret;
}

unsigned sleep(unsigned seconds) {
    struct timespec rqtp = {.tv_sec = seconds, .tv_nsec = 0 };
    struct timespec rmtp;
    nanosleep(&rqtp, &rmtp);
    return rmtp.tv_sec;
}

void sync(void) {
    struct vfs_mount *vfs = NULL;
    while (vfs_iterate_mount(&vfs)) {
        if (vfs->func->syncfs) {
            vfs->func->syncfs(vfs);
        }
        vfs_release_mount(vfs);
    }
}

int truncate(const char *path, off_t length) {
    vfs_path_buffer_t vfs_path;
    struct vfs_mount *vfs = vfs_acquire_mount(path, &vfs_path);
    if (!vfs) {
        return -1;
    }
    int ret = -1;
    if (vfs->func->truncate) {
        ret = vfs->func->truncate(vfs, vfs_path.begin, length);
    } else {
        errno = ENOSYS;
    }
    vfs_release_mount(vfs);
    return ret;
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
    struct vfs_file *file = vfs_acquire_file(fd, FWRITE);
    if (!file) {
        return -1;
    }
    int ret = vfs_write(file, buffer, size);
    vfs_release_file(file);
    return ret;
}
