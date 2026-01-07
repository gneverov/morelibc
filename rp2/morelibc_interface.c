/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <sys/timespec.h>
#include <unistd.h>

#include "pico/runtime_init.h"


void __weak __assert_func(const char *file, int line, const char *func, const char *failedexpr) {
    printf("assertion \"%s\" failed: file \"%s\", line %d%s%s\n",
           failedexpr, file, line, func ? ", function: " : "",
           func ? func : "");

    _exit(1);
}

int gettimeofday (struct timeval *__restrict tv, __unused void *__restrict tz) {
    if (!tv) {
        return 0;
    }
    struct timespec ts;
    int ret = clock_gettime(CLOCK_REALTIME, &ts);
    TIMESPEC_TO_TIMEVAL(tv, &ts);
    return ret;
}

int settimeofday(const struct timeval *tv, __unused const struct timezone *tz) {
    if (!tv) {
        return 0;
    }
    struct timespec ts;
    TIMEVAL_TO_TIMESPEC(tv, &ts);
    return clock_settime(CLOCK_REALTIME, &ts);
}

void runtime_init(void) {
#ifndef NDEBUG
    if (__get_current_exception()) {
        // crap; started in exception handler
        __breakpoint();
    }
#endif

#if !PICO_RUNTIME_SKIP_INIT_PER_CORE_INSTALL_STACK_GUARD
    // install core0 stack guard
    extern char __StackBottom;
    runtime_init_per_core_install_stack_guard(&__StackBottom);
#endif

    // piolibc __libc_init_array does __preint_array and __init_array
    extern void __libc_init_array(void);
    __libc_init_array();
}

#if !PICO_RUNTIME_NO_INIT_PER_CORE_TLS_SETUP
__weak void runtime_init_pre_core_tls_setup(void) {
    // for now we just set the same global area on both cores
    // note: that this is superfluous with the stock picolibc it seems, since it is itself
    // using a version of __aeabi_read_tp that returns the same pointer on both cores
    extern char __tls_base[];
    extern void _set_tls(void *tls);
    _set_tls(__tls_base);
}
#endif

#if !PICO_RUNTIME_SKIP_INIT_PER_CORE_TLS_SETUP
PICO_RUNTIME_INIT_FUNC_PER_CORE(runtime_init_pre_core_tls_setup, PICO_RUNTIME_INIT_PER_CORE_TLS_SETUP);
#endif

//// naked as it must preserve everything except r0 and lr
//uint32_t __attribute__((naked)) WRAPPER_FUNC(__aeabi_read_tp)() {
//    // note for now we are just returning a shared instance on both cores
//    pico_default_asm_volatile(
//            "ldr r0, =__tls_base\n"
//            "bx lr\n"
//            );
//}
