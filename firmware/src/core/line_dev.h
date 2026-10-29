/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// The phone-line stick's device logic (portable): the vendor requests of
// line_proto.h, the hook and its flash, the line monitor, the interrupt
// sources and the events for the host. The hardware is behind line_hal.h.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "line_proto.h"

typedef struct {
    uint16_t rate;           // samples a second
    bool frames;             // the Si3056's serial frames run
    bool link;               // ... and its line side answers (FDT)
    uint32_t t_link;         // when FDT was last read (ms)
    bool off_hook;           // as the host asked (a flash puts it on hook for a while)
    bool monitor;            // on-hook line monitor
    uint32_t flash_until;    // ms; 0: no flash
    uint8_t reg11, reg13;    // the chips' IDs, read at init
    uint8_t int_src;         // register 4 bits seen and not yet reported in an event
    uint8_t int_status;      // ... nor in a status
    lp_event_t sent;         // the last event sent
    bool sent_valid;
} line_dev_t;

// Bring the DAA up at rate (Hz) on hook, and read its IDs. False if its
// frames don't run; d->link says whether the line side answers yet
// (line_dev_poll keeps it up to date). The requests work either way.
bool line_dev_init(line_dev_t *d, uint16_t rate);
// Vendor IN request: the reply in buf (at most max bytes); its length, or -1 to stall.
int line_dev_vendor_in(line_dev_t *d, uint8_t req, uint16_t value, uint8_t *buf, uint16_t max);
// Vendor OUT request (no data stage); false to stall.
bool line_dev_vendor_out(line_dev_t *d, uint8_t req, uint16_t value);
// From the main loop: a hook flash's end, the DAA's interrupt, the LEDs.
void line_dev_poll(line_dev_t *d);
// The state now as an event; true if it differs from the last one sent
// (then it counts as sent: its seq is the next one).
bool line_dev_event(line_dev_t *d, lp_event_t *e);
// The LP_ST_* bits now.
uint8_t line_dev_state(const line_dev_t *d);
