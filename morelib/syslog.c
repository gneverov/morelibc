// SPDX-FileCopyrightText: 2025 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>


static int syslog_maskpri = LOG_UPTO(LOG_DEBUG);

int setlogmask(int maskpri) {
    int old_mask = syslog_maskpri;
    syslog_maskpri = maskpri & LOG_UPTO(LOG_DEBUG);
    return old_mask;
}

void syslog(int priority, const char *message, ... /* arguments */) {
    if ((priority & syslog_maskpri) == 0) {
        return;
    }
    va_list args;
    va_start(args, message);
    vfprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);
}