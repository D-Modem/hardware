/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The sticks' USB device (firmware/PROTOCOL.md "Line stick", "WB stick"):
// descriptors and the class driver in usb_stick.c, the stick's own part
// behind stick.h.
#pragma once

// Starts the USB device stack.
void usb_stick_init(void);
// Runs TinyUSB, the stick's poll, and sends an event when something
// changed. Call from the main loop; never from an interrupt.
void usb_stick_service(void);
// Into the UF2 loader, 50 ms from now (once the request's status stage has
// gone); usb_stick_service() does it.
void usb_stick_reboot_uf2(void);
