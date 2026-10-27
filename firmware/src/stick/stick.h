/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// What the sticks' USB device (usb_stick.c) needs from the stick it serves:
// its protocol's numbers, and its device logic and sample streams. The
// phone-line stick's are in src/line/line_stick.c (line_proto.h), the
// wideband stick's in src/wb/wb_stick.c (wb_proto.h).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#if defined(PS_BOARD_WB)
#include "wb_proto.h"
#define STICK_ITF_SUBCLASS WP_ITF_SUBCLASS
#define STICK_ITF_PROTOCOL WP_ITF_PROTOCOL
#define STICK_EP_OUT WP_EP_OUT
#define STICK_EP_IN WP_EP_IN
#define STICK_EP_EVENT WP_EP_EVENT
#define STICK_MAX_SAMPLES WP_MAX_SAMPLES
#define STICK_PACKET_BYTES WP_PACKET_BYTES
#define STICK_REQ_MS_OS_20 WP_REQ_MS_OS_20
#define STICK_ITF_NAME "pico-serial WB"
#define STICK_DRIVER_NAME "PS-WB"
typedef wp_event_t stick_event_t;
#else
#include "line_proto.h"
#define STICK_ITF_SUBCLASS LP_ITF_SUBCLASS
#define STICK_ITF_PROTOCOL LP_ITF_PROTOCOL
#define STICK_EP_OUT LP_EP_OUT
#define STICK_EP_IN LP_EP_IN
#define STICK_EP_EVENT LP_EP_EVENT
#define STICK_MAX_SAMPLES LP_MAX_SAMPLES
#define STICK_PACKET_BYTES LP_PACKET_BYTES
#define STICK_REQ_MS_OS_20 LP_REQ_MS_OS_20
#define STICK_ITF_NAME "pico-serial Line"
#define STICK_DRIVER_NAME "PS-LINE"
typedef lp_event_t stick_event_t;
#endif

// Vendor IN request (wIndex as the host sent it): the reply in buf (at most
// max bytes); its length, or -1 to stall.
int stick_vendor_in(uint8_t req, uint16_t value, uint16_t index, uint8_t *buf, uint16_t max);
// Vendor OUT request (no data stage); false to stall.
bool stick_vendor_out(uint8_t req, uint16_t value, uint16_t index);
// From the main loop, after TinyUSB: the device logic's poll.
void stick_poll(void);
// The state as an event, if it changed since the last one sent (then it
// counts as sent).
bool stick_event(stick_event_t *e);
// The last event didn't go out (or the stream just started): send the state
// at the next chance even if it hasn't changed.
void stick_event_again(void);
// The host opened (alternate setting 1) or closed the stream; also on bus reset.
void stick_stream(bool on);
// The modem's samples from USB OUT, into the stream.
void stick_from_host(const int16_t *s, uint32_t n);
// The line's samples for USB IN: up to max; returns how many.
uint32_t stick_to_host(int16_t *s, uint32_t max);
// Unplugged, or the host deconfigured the stick.
void stick_unmounted(void);
