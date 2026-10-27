/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The wideband stick behind the sticks' USB device (stick.h): its device
// logic (wb_dev.c) and the codec's sample streams (codec.c).
#include "board_config.h"
#include "codec.h"
#include "stick.h"
#include "usb_stick.h"
#include "wb_hal.h"
#include "wb_stick.h"

_Static_assert(PS_BOARD_ID == WP_BOARD, "wp_info_t.board is the board's ID");

wb_dev_t g_wb;

int stick_vendor_in(uint8_t req, uint16_t value, uint16_t index, uint8_t *buf, uint16_t max) {
    return wb_dev_vendor_in(&g_wb, req, value, index, buf, max);
}

bool stick_vendor_out(uint8_t req, uint16_t value, uint16_t index) {
    return wb_dev_vendor_out(&g_wb, req, value, index);
}

void stick_poll(void) { wb_dev_poll(&g_wb); }
bool stick_event(stick_event_t *e) { return wb_dev_event(&g_wb, e); }
void stick_event_again(void) { g_wb.sent_valid = false; }
void stick_stream(bool on) { codec_stream(on); }
void stick_from_host(const int16_t *s, uint32_t n) { codec_from_host(s, n); }
uint32_t stick_to_host(int16_t *s, uint32_t max) { return sstream_to_host(&g_audio.s, s, max); }

// Nothing to let go of (no hook): the stream has stopped with the bus; the
// LINK LED follows the host's next say, AUTO until then.
void stick_unmounted(void) { g_wb.carrier = WP_CARRIER_AUTO; }

// wb_hal.h: into the UF2 loader, once this request's status stage has gone.
void whal_reboot_uf2(void) { usb_stick_reboot_uf2(); }
