// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifndef UTSNAME_SYSNAME
#define UTSNAME_SYSNAME "Picolibc"
#endif
#ifndef UTSNAME_RELEASE
#define UTSNAME_RELEASE __PICOLIBC_VERSION__
#endif
#ifndef UTSNAME_VERSION
#define UTSNAME_VERSION __DATE__
#endif
#ifndef UTSNAME_MACHINE
#if defined(__ARM_ARCH)
#define UTSNAME_MACHINE ("armv" __XSTRING(__ARM_ARCH))
#else
#error unsupported machine
#endif
#endif

int uname(struct utsname *name) {
    strncpy(name->sysname, UTSNAME_SYSNAME, UTSNAME_LENGTH);
    #ifdef UTSNAME_NODENAME
    strncpy(name->nodename, UTSNAME_NODENAME, UTSNAME_LENGTH);
    #else
    if (gethostname(name->nodename, UTSNAME_LENGTH) < 0) {
        strncpy(name->nodename, "", UTSNAME_LENGTH);
    }
    #endif
    strncpy(name->release, UTSNAME_RELEASE, UTSNAME_LENGTH);
    strncpy(name->version, UTSNAME_VERSION, UTSNAME_LENGTH);
    strncpy(name->machine, UTSNAME_MACHINE, UTSNAME_LENGTH);
    return 0;
}
