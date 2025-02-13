// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <morelib/crc.h>

#define FLASH_ENV_MAX 64


struct flash_env {
    const char *env[FLASH_ENV_MAX];
    char buffer[2048];
    uint32_t crc;    
};

extern char **environ;

__attribute__((section(".flash_env")))
static const volatile struct flash_env flash_env;

__attribute__((constructor(101), visibility("hidden")))
void env_init(void) {
    if (crc32(CRC32_INITIAL, (const void *)&flash_env, sizeof(struct flash_env)) == CRC32_CHECK) {
        environ = (char **)flash_env.env;
    }
}

__attribute__((destructor, visibility("hidden")))
int env_fini(void) {
    if (environ == (char **)flash_env.env) {
        return 0;
    }

    int fd = open("/dev/firmware", O_RDWR);
    if (fd < 0) {
        return -1;
    }
    int ret = -1;
    uintptr_t off = ((uintptr_t)&flash_env) & 0xffffff;
    void *addr = mmap(0, sizeof(struct flash_env), PROT_READ, MAP_SHARED, fd, off);
    if (addr != &flash_env) {
        goto exit;
    }

    off_t pos = offsetof(struct flash_env, buffer);
    char **e = environ;
    for (int i = 0; i < FLASH_ENV_MAX; i++) {
        uintptr_t ptr = *e ? pos + (uintptr_t)&flash_env : 0;
        if (pwrite(fd, &ptr, sizeof(ptr), off + offsetof(struct flash_env, env[i])) < 0) {
            goto exit;
        }
        if (*e) {
            size_t len = strlen(*e) + 1;
            if (pwrite(fd, *e, len, off + pos) < 0) {
                goto exit;
            }
            pos += len;
            e++;
        }
    }
    uint32_t crc = CRC32_INITIAL;
    for (size_t i = 0; i < offsetof(struct flash_env, crc); i += 4) {
        uint8_t buf[4];
        if (pread(fd, buf, 4, off + i) < 0) {
            goto exit;
        }
        crc = crc32(crc, buf, 4);
    }
    if (pwrite(fd, &crc, 4, off + offsetof(struct flash_env, crc)) < 0) {
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

static int env_copy(void) {
    if (environ != (char **)flash_env.env) {
        return 0;
    }    
    environ = calloc(FLASH_ENV_MAX + 1, sizeof(char *));
    if (!environ) {
        return -1;
    }
    for (int i = 0; i < FLASH_ENV_MAX; i++) {
        if (flash_env.env[i]) {
            environ[i] = strdup(flash_env.env[i]);
        }
    }
    return 1;
}

int __wrap_setenv(const char *name, const char *value, int rewrite) {
    int __real_setenv(const char *name, const char *value, int rewrite);
    if (env_copy() < 0) {
        return -1;
    }
    return __real_setenv(name, value, rewrite);
}

int __wrap_unsetenv(const char *name) {
    int __real_unsetenv(const char *name);
    if (env_copy() < 0) {
        return -1;
    }
    return __real_unsetenv(name);
}
