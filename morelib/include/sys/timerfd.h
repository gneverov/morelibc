// SPDX-FileCopyrightText: 2025 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <fcntl.h>
#include <time.h>

#define TFD_CLOEXEC     O_CLOEXEC
#define TFD_NONBLOCK    O_NONBLOCK 

#define TFD_TIMER_ABSTIME  1
#define TFD_TIMER_CANCEL_ON_SET 2


int timerfd_create(int clockid, int flags);

int timerfd_settime(int fd, int flags, const struct itimerspec *new_value, struct itimerspec *old_value);

int timerfd_gettime(int fd, struct itimerspec *curr_value);