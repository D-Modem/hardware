/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// The phone-line stick behind the sticks' USB device (stick.h): its device
// logic (line_dev.c) and the DAA's sample streams (daa.c).
#include "daa.h"
#include "line_hal.h"
#include "line_stick.h"
#include "stick.h"
#include "usb_stick.h"

line_dev_t g_line;

int stick_vendor_in(uint8_t req, uint16_t value, uint16_t index, uint8_t *buf, uint16_t max) {
    (void)index;
    return line_dev_vendor_in(&g_line, req, value, buf, max);
}

bool stick_vendor_out(uint8_t req, uint16_t value, uint16_t index) {
    (void)index;
    return line_dev_vendor_out(&g_line, req, value);
}

void stick_poll(void) { line_dev_poll(&g_line); }
bool stick_event(stick_event_t *e) { return line_dev_event(&g_line, e); }
void stick_event_again(void) { g_line.sent_valid = false; }
void stick_stream(bool on) { daa_stream(on); }
void stick_from_host(const int16_t *s, uint32_t n) { daa_from_host(s, n); }
uint32_t stick_to_host(int16_t *s, uint32_t max) { return sstream_to_host(&g_stream.s, s, max); }

// The line goes on hook: a modem that lost its host must not hold the line.
void stick_unmounted(void) { line_dev_vendor_out(&g_line, LP_REQ_SET_HOOK, 0); }

// line_hal.h: into the UF2 loader, once this request's status stage has gone.
void lhal_reboot_uf2(void) { usb_stick_reboot_uf2(); }
