/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The WB stick's device logic (portable): the vendor requests of
// wb_proto.h, the codec's bring-up at the host's rate and settings, its
// flags and clocks watched, the LEDs, and the events for the host. The
// hardware is behind wb_hal.h.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wb_codec.h"
#include "wb_proto.h"

typedef struct {
    const wb_rate_t *rate;
    wb_codec_result_t result;    // the last bring-up's
    bool clock;                  // the codec's frames arrive
    uint32_t t_clock;            // when they were last checked (ms)
    uint32_t frames_seen;        // ... and the count then
    uint8_t carrier;             // WP_CARRIER_*
    uint8_t input;               // WP_INPUT_*
    uint8_t loopback;            // WP_LOOP_*
    int8_t tx_gain;              // 0.5 dB steps
    uint8_t rx_gain;             // 0.5 dB steps
    uint8_t ovf, oc;             // the codec's sticky flags not yet reported in an event
    uint8_t ovf_status, oc_status;   // ... nor in a status
    uint16_t resyncs;            // the I2S slave lost its place
    uint32_t tx_seen;            // whal_tx_active() at the last poll
    uint32_t t_data;             // when the host's samples last weren't silent (ms)
    bool data;                   // ... within DATA_MS
    wp_event_t sent;             // the last event sent
    bool sent_valid;
} wb_dev_t;

// Defaults (the hybrid to the host, no loopback, 0 dB both ways, LINK while
// streaming), then wb_dev_start() at rate (Hz; 96000 if it isn't one).
bool wb_dev_init(wb_dev_t *d, uint32_t rate);
// The codec brought up again at d's rate and settings, and the I2S slave
// restarted on its clocks. False if the codec didn't come up or its clocks
// don't run; the requests work either way.
bool wb_dev_start(wb_dev_t *d);
// Vendor IN request: the reply in buf (at most max bytes); its length, or -1 to stall.
int wb_dev_vendor_in(wb_dev_t *d, uint8_t req, uint16_t value, uint16_t index, uint8_t *buf, uint16_t max);
// Vendor OUT request (no data stage); false to stall.
bool wb_dev_vendor_out(wb_dev_t *d, uint8_t req, uint16_t value, uint16_t index);
// From the main loop: the codec's interrupt (its sticky flags), the I2S
// slave's place and the clocks, the LEDs.
void wb_dev_poll(wb_dev_t *d);
// The state now as an event; true if it differs from the last one sent
// (then it counts as sent: its seq is the next one).
bool wb_dev_event(wb_dev_t *d, wp_event_t *e);
// The WP_ST_* bits now.
uint8_t wb_dev_state(const wb_dev_t *d);
