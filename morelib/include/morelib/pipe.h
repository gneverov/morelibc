// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "morelib/poll.h"


int pipe_pair(struct poll_file *pipes[2]);