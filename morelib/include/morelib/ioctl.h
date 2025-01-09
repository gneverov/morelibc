// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <sys/ioctl.h>


// Terminal multiplexer device ioctls
// ---
#define TMUX_BASE 0x8100

#define TMUX_ADD (TMUX_BASE + 0)
#define TMUX_REMOVE (TMUX_BASE + 1)
