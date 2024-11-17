// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <stdarg.h>
#include <sys/ioctl.h>


int vioctl(int fd, unsigned long request, va_list args);
