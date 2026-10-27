/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The wideband stick's device state, behind the sticks' USB device
// (stick.h, wb_stick.c).
#pragma once
#include "wb_dev.h"

// The device state the requests act on (main.c brings it up).
extern wb_dev_t g_wb;
