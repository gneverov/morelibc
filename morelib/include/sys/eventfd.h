// SPDX-FileCopyrightText: 2025 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#define EFD_NONBLOCK    0x02 
#define EFD_SEMAPHORE   0x04


int eventfd(unsigned int initval, int flags);