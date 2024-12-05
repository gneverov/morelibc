// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>


/**
 * Rewrites a const global variable stored in flash.
 * 
 * Args:
 * dst: address of a const global variable
 * src: pointer to the new value of the global variable
 * len: length of the global variable
 * 
 * Returns:
 * 0 on success, -1 on failure and sets errno
 */
int flash_copy(const volatile void *dst, const void *src, size_t len) {
    int fd = open("/dev/firmware", O_RDWR);
    if (fd < 0) {
        return -1;
    }
    int ret = -1;
    uintptr_t off = ((uintptr_t)dst) & 0xffffff;
    void *addr = mmap(0, len, PROT_READ, MAP_SHARED, fd, off);
    if (addr != dst) {
        goto exit;
    }
    if (pwrite(fd, src, len, off) < 0) {
        goto exit;
    }
    if (fsync(fd) < 0) {
        goto exit;
    }
    ret = 0;
exit:
    close(fd);
    return ret;
}
