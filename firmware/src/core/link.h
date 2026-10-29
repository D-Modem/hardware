/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// Host links (USB CDC, and on the W board BLE and classic Bluetooth SPP) and
// which one owns the RS-232 port (SPEC-W.md 4.6):
//   1. USB, while its port is open (DTR set);
//   2. otherwise the wireless link that connected first;
//   3. otherwise USB, if it's plugged into a host (the wired boards: always).
// Only the owner's lines, line coding, break and data reach the core; the
// others stay connected but their data is dropped. Portable, like core.c.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "core.h"

enum link_id { LINK_USB = 0, LINK_BLE = 1, LINK_SPP = 2, LINK_COUNT = 3, LINK_NONE = 0xFF };

#define LINK_RC_LINE 24u   // longest result-code line looked at

typedef struct {
    bool up;                // connected (USB: mounted)
    bool dtr, rts;
    uint32_t order;         // when it connected (lower = earlier)
    bool has_format;
    uint32_t baud;
    uint8_t stop_cdc, parity_cdc, data_bits;
    // DCD from result codes: the phone is the modem and says CONNECT / NO CARRIER
    bool rc_dcd;
    int8_t dcd;             // -1: DCD follows DTR; 0/1: from result codes
    char line[LINK_RC_LINE];
    uint8_t n;
    bool overflow;
} link_t;

typedef struct {
    link_t l[LINK_COUNT];
    uint8_t owner;          // link_id, LINK_NONE
    uint32_t order;
    uint32_t owner_changes;
} links_t;

extern links_t g_links;

// All links down, no owner; SPP has DCD-from-result-codes on (SPEC-W 4.4).
void links_init(links_t *s, core_t *c);
// Connection events. Wireless links come up with DTR and RTS asserted, so a
// plain terminal app gets DCD and DSR while connected (SPEC-W 4.3).
void link_up(links_t *s, core_t *c, uint8_t id, bool up);
// A link's DTR and RTS changed (USB: SET_CONTROL_LINE_STATE; BLE: Outputs).
// USB's DTR also decides whether it takes the port.
void link_lines(links_t *s, core_t *c, uint8_t id, bool dtr, bool rts);
// CDC-style line coding (stop: 0 = 1, 1 = 1.5, 2 = 2; parity 0 none 1 odd 2 even ...)
void link_format(links_t *s, core_t *c, uint8_t id, uint32_t baud, uint8_t stop_cdc, uint8_t parity_cdc,
                 uint8_t data_bits);
// Send a break (as core_host_break) if this link owns the port; false if not.
bool link_break(links_t *s, core_t *c, uint8_t id, uint16_t ms);
// DCD from CONNECT / NO CARRIER in this link's data (SPEC-W 4.4). Default: on for SPP.
void link_rc_dcd(links_t *s, core_t *c, uint8_t id, bool on);

// Whether this link owns the RS-232 port right now.
static inline bool link_owns(const links_t *s, uint8_t id) { return s->owner == id; }
// Data from the link toward the RS-232 port: returns bytes accepted. A link
// that doesn't own the port has its data dropped (returns len).
uint32_t link_write(links_t *s, core_t *c, uint8_t id, const uint8_t *data, uint32_t len);
// Data from the RS-232 port for the link; nothing unless it owns the port.
uint32_t link_read(links_t *s, core_t *c, uint8_t id, uint8_t *data, uint32_t max);
