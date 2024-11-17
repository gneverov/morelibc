// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#define RTLD_LAZY 1                     // Relocations are performed at an implementation-defined time.
#define RTLD_NOW 0                      // Relocations are performed when the object is loaded.
#define RTLD_GLOBAL 2                   // All symbols are available for relocation processing of other modules.
#define RTLD_LOCAL 0                    // All symbols are not made available for relocation processing by other modules.


typedef struct {
    const char *dli_fname;              // Pathname of mapped object file.
    void *dli_fbase;                    // Base of mapped address range.
    const char *dli_sname;              // Symbol name or null pointer.
    void *dli_saddr;                    // Symbol address or null pointer.
} Dl_info_t;

int dladdr(const void *addr, Dl_info_t *dlip);
int dlclose(void *handle);
char *dlerror(void);
void *dlopen(const char *file, int mode);
void *dlsym(void *handle, const char *name);
