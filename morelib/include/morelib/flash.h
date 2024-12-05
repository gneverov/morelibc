// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <stddef.h>


int flash_copy(const volatile void *dst, const void *src, size_t len);
