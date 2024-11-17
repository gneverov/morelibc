// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <termios.h>


void termios_init(struct termios *termios_p, speed_t speed);
