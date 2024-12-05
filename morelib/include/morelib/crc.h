// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <stddef.h>
#include <stdint.h>

// The CRC value to pass to the first call of crc32.
#define CRC32_INITIAL 0x00000000u

#define CRC32_CHECK 0x2144DF1Cu


uint32_t crc32(uint32_t crc, const uint8_t *buffer, size_t size);
