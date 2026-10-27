/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The wideband stick's USB protocol (SPEC-WB.md §5, firmware/PROTOCOL.md
// "WB stick"): what the host's driver (D-Modem's dmodemd) and the firmware
// share. Shaped as the Line stick's (line_proto.h): the same interface
// layout and endpoints, other requests. Little-endian, packed.
#pragma once
#include <stdint.h>

#define WP_VERSION 1
#define WP_BOARD 4             // wp_info_t.board: the WB stick (board_config.h PS_BOARD_ID)

// Interface 0: vendor class, this subclass and protocol. Alternate setting
// 0 has no endpoints (no bandwidth); alternate setting 1 has the three below,
// and selecting it starts the samples both ways.
#define WP_ITF_SUBCLASS 0x57   // 'W'
#define WP_ITF_PROTOCOL 0x01
#define WP_EP_OUT 0x01         // isochronous, 1 ms: the modem's samples to the line
#define WP_EP_IN 0x82          // isochronous, 1 ms: the line's samples
#define WP_EP_EVENT 0x83       // interrupt, 4 ms: wp_event_t when something changes
#define WP_MAX_SAMPLES 240     // per packet: 192 kHz (192 a ms) with room to catch up
#define WP_PACKET_BYTES (2 * WP_MAX_SAMPLES)

// Vendor requests (recipient: the interface or the device; wIndex's low byte
// the interface, 0)
enum {
    WP_REQ_GET_INFO = 0x01,     // IN: wp_info_t
    WP_REQ_GET_STATUS = 0x02,   // IN: wp_status_t (the peaks and the codec's flags since the last one)
    WP_REQ_SET_RATE = 0x10,     // OUT: wValue = the sample rate in kHz: 48, 96 or 192
    WP_REQ_SET_CARRIER = 0x11,  // OUT: wValue = WP_CARRIER_* (the LINK LED)
    WP_REQ_SET_TX_GAIN = 0x12,  // OUT: wValue = int16, 0.5 dB steps, -127..48: the DAC's volume
    WP_REQ_SET_RX_GAIN = 0x13,  // OUT: wValue = 0..95, 0.5 dB steps: the hybrid's MicPGA
    WP_REQ_SET_INPUT = 0x14,    // OUT: wValue = WP_INPUT_*: which ADC channel the host gets
    WP_REQ_SET_LOOPBACK = 0x15, // OUT: wValue = WP_LOOP_*
    WP_REQ_CODEC_READ = 0x20,   // IN: wValue = register (1..127), wIndex = page << 8; 1 byte
    WP_REQ_CODEC_WRITE = 0x21,  // OUT: wValue = register << 8 | value, wIndex = page << 8
    WP_REQ_RESET = 0x30,        // OUT: the codec reset and brought up again (rate and settings kept)
    WP_REQ_REBOOT_UF2 = 0x31,   // OUT: into the RP2040's UF2 loader
    WP_REQ_MS_OS_20 = 0x4D,     // IN, wIndex 7: the Microsoft OS 2.0 descriptor set (Windows)
};

enum { WP_CARRIER_OFF, WP_CARRIER_ON, WP_CARRIER_AUTO };   // AUTO (the default): lit while streaming
enum { WP_INPUT_HYBRID, WP_INPUT_LINE };                    // left ADC (default), right ADC
enum { WP_LOOP_OFF, WP_LOOP_FIRMWARE, WP_LOOP_CODEC };

// State bits (wp_info_t.state, wp_status_t.state, wp_event_t.state)
#define WP_ST_CODEC 0x01        // the codec came up and its clocks run
#define WP_ST_CARRIER 0x02      // the LINK LED is lit
#define WP_ST_LOOPBACK 0x04     // a loopback is on
#define WP_ST_STREAMING 0x08    // alternate setting 1

typedef struct __attribute__((packed)) {
    uint8_t version;            // WP_VERSION
    uint8_t fw_major, fw_minor, fw_patch;
    uint32_t rate;              // samples a second
    uint16_t max_samples;       // WP_MAX_SAMPLES
    uint8_t state;              // WP_ST_*
    uint8_t board;              // WP_BOARD
    uint8_t input;              // WP_INPUT_*
    uint8_t loopback;           // WP_LOOP_*
    int8_t tx_gain;             // the DAC's volume, 0.5 dB steps
    uint8_t rx_gain;            // the hybrid MicPGA's gain, 0.5 dB steps
} wp_info_t;

typedef struct __attribute__((packed)) {
    uint16_t peak_hybrid;       // the largest |sample| since the last status: left ADC (32768 = full scale)
    uint16_t peak_line;         // ... right ADC
    uint16_t peak_tx;           // ... the host's samples played
    uint8_t ovf;                // codec page 0 register 42 bits since the last status: DAC/ADC overflows
    uint8_t oc;                 // register 44's over-current bits (HPL 0x80, HPR 0x40) since the last status
    uint8_t adc_flags;          // register 36 now: PGAs at gain, ADCs powered
    uint8_t dac_flags;          // register 37 now: DACs and drivers powered
    uint8_t state;              // WP_ST_*
    uint8_t reserved;
    uint16_t underruns;         // OUT samples the line wanted and the host hadn't sent
    uint16_t overruns;          // samples dropped: OUT too far ahead, or IN not taken
    uint32_t samples_in;        // the line's samples handed to the host
    uint32_t samples_out;       // the host's samples played
    uint16_t resyncs;           // the I2S slave lost its place and started again
    uint16_t reserved2;
} wp_status_t;

typedef struct __attribute__((packed)) {
    uint8_t state;              // WP_ST_*
    uint8_t seq;                // +1 each event
    uint8_t ovf;                // register 42 bits since the last event
    uint8_t oc;                 // register 44's over-current bits since the last event
    uint16_t underruns, overruns;
} wp_event_t;

_Static_assert(sizeof(wp_info_t) == 16, "wp_info_t");
_Static_assert(sizeof(wp_status_t) == 28, "wp_status_t");
_Static_assert(sizeof(wp_event_t) == 8, "wp_event_t");
_Static_assert(WP_PACKET_BYTES <= 1023, "a full-speed isochronous packet");

// Samples a millisecond at a rate (Hz): the IN packets' usual size.
static inline unsigned wp_samples_per_ms(unsigned hz) { return hz / 1000u; }
