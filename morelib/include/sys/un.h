// SPDX-FileCopyrightText: 2025 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <sys/socket.h>

// #define UNIX_PATH_MAX	16

struct sockaddr_un {
	sa_family_t     sun_family;     /* Address family */
	char            sun_path[];     /* Socket pathname */
};