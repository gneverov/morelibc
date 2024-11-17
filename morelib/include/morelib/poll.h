// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <poll.h>
#include "morelib/vfs.h"

#include "FreeRTOS.h"


int poll_ticks(struct pollfd fds[], nfds_t nfds, TickType_t *pxTicksToWait);

int poll_file(struct vfs_file *file, uint events, int timeout);

void poll_notify(struct vfs_file *file, uint events);

void poll_notify_from_isr(struct vfs_file *file, uint events, BaseType_t *pxHigherPriorityTaskWoken);
