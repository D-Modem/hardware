/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// pico-serial vendor protocol: control requests on the "pico-serial control"
// interface. Shared by the firmware and any host app; see firmware/PROTOCOL.md.
#pragma once
#include <stdint.h>

#define PS_PROTOCOL_VERSION 1

// USB: vendor interface, class 0xFF, subclass/protocol below; requests are
// vendor-type, recipient interface, wIndex = that interface's number.
#define PS_ITF_SUBCLASS 0x50   // 'P'
#define PS_ITF_PROTOCOL 0x01

enum ps_request {
    PS_REQ_GET_INFO = 0x01,          // IN  ps_info_t
    PS_REQ_SET_MODE = 0x02,          // OUT wValue = ps_mode (saved)
    PS_REQ_SET_OUTPUTS = 0x03,       // OUT wValue = mask << 8 | value (PS_LINE_*), not saved
    PS_REQ_GET_INPUTS = 0x04,        // IN  ps_inputs_t
    PS_REQ_SET_FLOW = 0x05,          // OUT wValue = ps_flow (saved)
    PS_REQ_GET_STATS = 0x06,         // IN  ps_stats_t
    PS_REQ_CLEAR_STATS = 0x07,       // OUT
    PS_REQ_SET_BAUD_MODE = 0x08,     // OUT wValue = ps_baud (saved)
    PS_REQ_AUTOBAUD_RESTART = 0x09,  // OUT: learn the baud rate again now
    // W board only (stall elsewhere):
    PS_REQ_BT_PAIRING = 0x0A,        // OUT wValue = seconds to allow pairing (0 = close the window)
    PS_REQ_BT_FORGET = 0x0B,         // OUT: forget every bonded phone
};

enum ps_mode { PS_MODE_AUTO = 0, PS_MODE_DCE = 1, PS_MODE_DTE = 2 };
enum ps_role { PS_ROLE_NONE = 0, PS_ROLE_DCE = 1, PS_ROLE_DTE = 2 };
enum ps_flow { PS_FLOW_AUTO = 0, PS_FLOW_ON = 1, PS_FLOW_OFF = 2 };
// Where the UART's baud rate comes from. AUTO: learned from the far end as
// DCE (a computer typing at its own speed), the host's as DTE (a modem
// follows the speed it's spoken to at). HOST: always the host's (SET_LINE_CODING).
// LEARN: always learned. Data bits, parity and stop bits are always the host's.
enum ps_baud { PS_BAUD_AUTO = 0, PS_BAUD_HOST = 1, PS_BAUD_LEARN = 2 };

// Modem lines, 1 = asserted (ON). Which are outputs and which inputs depends
// on the role: as DCE the board drives DCD DSR RI CTS and reads DTR RTS; as
// DTE it drives DTR RTS and reads DCD DSR RI CTS.
#define PS_LINE_DTR 0x01
#define PS_LINE_RTS 0x02
#define PS_LINE_DCD 0x04
#define PS_LINE_DSR 0x08
#define PS_LINE_RI  0x10
#define PS_LINE_CTS 0x20

// ps_info_t.flags
#define PS_INFO_CODING_ADJUSTED 0x01   // last SET_LINE_CODING asked for something the UART can't do
#define PS_INFO_BAUD_OVER_RATING 0x02  // baud above the transceivers' 250 kbit/s rating
#define PS_INFO_FLOW_ACTIVE 0x04       // hardware CTS flow control currently obeyed
#define PS_INFO_BAUD_LOCKED 0x08       // the baud rate was learned from the far end
#define PS_INFO_BAUD_LEARNING 0x10     // learning it now (received data waits until it's known)

typedef struct __attribute__((packed)) {
    uint8_t protocol;      // PS_PROTOCOL_VERSION
    uint8_t fw_major, fw_minor, fw_patch;
    uint8_t board;         // 0 = DB25 hood board, 1 = W (wireless), 2 = Pico bench rig
    uint8_t mode;          // ps_mode setting
    uint8_t role;          // ps_role now
    uint8_t flow;          // ps_flow setting
    uint8_t flags;         // PS_INFO_*
    uint32_t baud;         // UART baud now (learned, or the host's)
    uint8_t baud_mode;     // ps_baud setting
} ps_info_t;

// ps_inputs_t.detect
#define PS_DETECT_A_STATUS 0x01   // chip A sees valid RS-232 levels on pins 2/4/20 (a computer)
#define PS_DETECT_B_INVALID 0x02  // chip B sees valid RS-232 levels on pins 3/5/6/8/22 (a modem)
#define PS_DETECT_A_R1OUTB 0x04   // chip A's always-on receiver on pin 2 (non-inverting)

typedef struct __attribute__((packed)) {
    uint8_t role;
    uint8_t inputs;        // PS_LINE_* read from the far end in this role
    uint8_t outputs;       // PS_LINE_* the board is driving now
    uint8_t detect;        // PS_DETECT_*
    uint8_t host;          // PS_LINE_DTR/RTS as the host last set them
} ps_inputs_t;

typedef struct __attribute__((packed)) {
    uint32_t to_rs232;     // bytes sent out of the DB25
    uint32_t from_rs232;   // bytes received on the DB25
    uint32_t framing, parity, overrun, breaks;
    uint32_t dropped;      // received bytes lost because the host wasn't reading
    uint32_t role_changes;
} ps_stats_t;

_Static_assert(sizeof(ps_info_t) == 14, "ps_info_t is 14 bytes on the wire");
_Static_assert(sizeof(ps_inputs_t) == 5, "ps_inputs_t is 5 bytes on the wire");
_Static_assert(sizeof(ps_stats_t) == 32, "ps_stats_t is 32 bytes on the wire");
