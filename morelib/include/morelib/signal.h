// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <signal.h>

#include "FreeRTOS.h"


void kill_from_isr(int pid, int sig, BaseType_t *pxHigherPriorityTaskWoken);
