/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The phone-line stick's USB protocol (SPEC-L.md §5, firmware/PROTOCOL.md
// "Line stick"): what the host's driver (D-Modem's dmodemd) and the
// firmware share. Little-endian, packed.
#pragma once
#include <stdint.h>

#define LP_VERSION 1

// Interface 0: vendor class, this subclass and protocol. Alternate setting
// 0 has no endpoints (no bandwidth); alternate setting 1 has the three below,
// and selecting it starts the samples both ways.
#define LP_ITF_SUBCLASS 0x4C   // 'L'
#define LP_ITF_PROTOCOL 0x01
#define LP_EP_OUT 0x01         // isochronous, 1 ms: the modem's samples to the line
#define LP_EP_IN 0x82          // isochronous, 1 ms: the line's samples
#define LP_EP_EVENT 0x83       // interrupt, 1 ms: lp_event_t when something changes
#define LP_MAX_SAMPLES 20      // per packet: 16 kHz (16 a ms) with room to catch up
#define LP_PACKET_BYTES (2 * LP_MAX_SAMPLES)

// Vendor requests (recipient: the interface or the device, wIndex 0)
enum {
    LP_REQ_GET_INFO = 0x01,     // IN: lp_info_t
    LP_REQ_GET_STATUS = 0x02,   // IN: lp_status_t
    LP_REQ_SET_RATE = 0x10,     // OUT: wValue = sample rate in Hz (lp_rate_code)
    LP_REQ_SET_HOOK = 0x11,     // OUT: wValue 1 = off hook, 0 = on hook
    LP_REQ_HOOK_FLASH = 0x12,   // OUT: wValue = ms on hook (1..2000), then off hook again
    LP_REQ_SET_MONITOR = 0x13,  // OUT: wValue 1 = hear the line on hook (caller ID), 0 = not
    LP_REQ_DAA_READ = 0x20,     // IN: wValue = register (0..127); 1 byte
    LP_REQ_DAA_WRITE = 0x21,    // OUT: wValue = register << 8 | value
    LP_REQ_RESET = 0x30,        // OUT: the DAA reset and brought up again, on hook
    LP_REQ_REBOOT_UF2 = 0x31,   // OUT: into the RP2040's UF2 loader
};

// State bits (lp_info_t.state, lp_status_t.state, lp_event_t.state)
#define LP_ST_LINK 0x01         // the Si3056 and the line side talk (FDT)
#define LP_ST_OFF_HOOK 0x02
#define LP_ST_MONITOR 0x04      // on-hook line monitor on
#define LP_ST_STREAMING 0x08    // alternate setting 1
#define LP_ST_RINGING 0x10      // the ring detector is active now
#define LP_ST_FLASH 0x20        // a hook flash is in progress

typedef struct __attribute__((packed)) {
    uint8_t version;            // LP_VERSION
    uint8_t fw_major, fw_minor, fw_patch;
    uint16_t rate;              // samples a second
    uint8_t max_samples;        // LP_MAX_SAMPLES
    uint8_t state;              // LP_ST_*
    uint8_t reg11;              // Si3056 register 11: line-side ID (bits 7:4) and system-side revision
    uint8_t reg13;              // register 13: line-side revision (bits 5:2)
    uint8_t reserved[2];
} lp_info_t;

typedef struct __attribute__((packed)) {
    uint8_t reg12;              // FDT (bit 6), loop current LCS[4:0]
    uint8_t reg28;              // loop current LCS2
    uint8_t reg29;              // line voltage LVS
    uint8_t reg5;               // DAA control 1: RDTN, RDTP, ONHM, RDT, OHE, OH
    uint8_t int_src;            // register 4 bits seen since the last event or status
    uint8_t state;              // LP_ST_*
    uint16_t underruns;         // OUT samples the line wanted and the host hadn't sent
    uint16_t overruns;          // samples dropped: OUT too far ahead, or IN not taken
    uint32_t samples_in;        // the line's samples handed to the host
    uint32_t samples_out;       // the host's samples played
    uint8_t ring_edges;         // ring detector changes, mod 256
    uint8_t reserved;
} lp_status_t;

typedef struct __attribute__((packed)) {
    uint8_t state;              // LP_ST_*
    uint8_t ring_edges;         // ring detector changes, mod 256: each one is a pulse for the modem's ring()
    uint8_t int_src;            // register 4 bits since the last event (ring, overload, polarity...)
    uint8_t seq;                // +1 each event
    uint16_t underruns, overruns;
} lp_event_t;

_Static_assert(sizeof(lp_info_t) == 12, "lp_info_t");
_Static_assert(sizeof(lp_status_t) == 20, "lp_status_t");
_Static_assert(sizeof(lp_event_t) == 8, "lp_event_t");

// The Si3056's SRC code (register 7) for a rate in Hz, -1 if it has none.
static inline int lp_rate_code(unsigned hz) {
    static const uint16_t rates[] = {7200, 8000, 8229, 8400, 9000, 9600, 10286, 12000, 13714, 16000};
    for (int i = 0; i < (int)(sizeof rates / sizeof rates[0]); i++)
        if (rates[i] == hz) return i;
    return -1;
}
