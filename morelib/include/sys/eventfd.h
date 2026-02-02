// SPDX-FileCopyrightText: 2025 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <fcntl.h>

#define EFD_CLOEXEC     O_CLOEXEC
#define EFD_NONBLOCK    O_NONBLOCK 
#define EFD_SEMAPHORE   0x80000000


int eventfd(unsigned int initval, int flags);