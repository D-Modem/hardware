/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The line stick's USB device (firmware/PROTOCOL.md "Line stick").
#pragma once
#include "line_dev.h"

// The device state the requests act on (main.c brings it up).
extern line_dev_t g_line;

// Starts the USB device stack (descriptors and the class driver in usb_line.c).
void usb_line_init(void);
// Runs TinyUSB, the device logic's poll, and sends an event when something
// changed. Call from the main loop; never from an interrupt.
void usb_line_service(void);
