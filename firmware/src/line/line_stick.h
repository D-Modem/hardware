/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// The line stick's device state, behind the sticks' USB device (stick.h,
// line_stick.c).
#pragma once
#include "line_dev.h"

// The device state the requests act on (main.c brings it up).
extern line_dev_t g_line;
