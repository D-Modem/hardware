/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
#pragma once
// Starts the USB device stack (descriptors and callbacks in usb.c). Call
// once, after core_init() and links_init(): callbacks feed the core at once.
void usb_init(void);
// Runs TinyUSB, then moves data both ways between the CDC port and the core
// (only while USB owns the RS-232 port) and sends the host modem-line changes.
// Call from the main loop; never from an interrupt.
void usb_service(void);
