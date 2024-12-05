// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "morelib/crc.h"


static const uint32_t crctab[] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
};

/**
 * Computes CRC-32 checksum based on the IEEE 802.3 definition.
 * 
 * Right-shifting polynomial: 0xEDB88320
 * Initial CRC: 0xFFFFFFFF
 * Final XOR value: 0xFFFFFFFF
 * Verify value: 0x2144DF1C
 * 
 * The output of this function can be passed as the input to a subsequent call to incrementally 
 * compute the checksum of a stream.
 */
uint32_t crc32(uint32_t crc, const uint8_t *buffer, size_t size) {
    crc = ~crc;
    for (size_t i = 0; i < size; i++) {
        uint32_t x = buffer[i];
        crc = (crc >> 4) ^ crctab[(crc ^ x) & 0xf];
        x >>= 4;
        crc = (crc >> 4) ^ crctab[(crc ^ x) & 0xf];
    }
    return ~crc;
}
