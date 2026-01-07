// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <sys/stat.h>
#include <sys/types.h>

enum {
    DT_UNKNOWN = 0,
    DT_FIFO = (S_IFIFO >> 12),
    DT_CHR = (S_IFCHR >> 12),
    DT_DIR = (S_IFDIR >> 12),
    DT_BLK = (S_IFBLK >> 12),
    DT_REG = (S_IFREG >> 12),
    DT_LNK = (S_IFLNK >> 12),
    DT_SOCK = (S_IFSOCK >> 12),
};

typedef void DIR;

struct dirent {
    ino_t d_ino;
    unsigned char d_type;
    char *d_name;
};
