/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// The Si3056's serial frames, from the RP2040's side (portable: the PIO
// interrupt calls it with each 16-bit word received; host tests do too).
//
// Every frame (one sample period, 256 SCLKs) starts with a primary word:
// the line's sample in, the modem's sample out. A control access (a register
// read or write) is a secondary word in the same frame, 128 SCLKs later,
// which the RP2040 requests while that frame's primary is going out: in
// 15-bit mode (after reset) by the LSB of the primary word it sends, in
// 16-bit mode (SB set) by the FC pin. One access is on the wire at a time.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sstream.h"

enum { DAA_OP_IDLE, DAA_OP_QUEUED, DAA_OP_ARMED, DAA_OP_SECONDARY, DAA_OP_DONE };

typedef struct {
    bool fc_hw;                    // 16-bit mode: secondary frames requested by the FC pin
    volatile uint8_t op_state;     // DAA_OP_*
    uint16_t op_word;              // the control word (si_ctl_read / si_ctl_write)
    volatile uint8_t op_result;    // a read's value
    bool expect_secondary;         // the next word is a secondary one
    volatile uint32_t primaries, secondaries;
} daa_frame_t;

// What to do after a word: push these TX words (in order) for the PIO,
// set the FC pin (-1: leave it), and whether the word was a primary one.
typedef struct {
    uint8_t n_tx;
    uint16_t tx[2];
    int8_t fc;
    bool primary;
    int16_t sample;                // a primary word's sample
} daa_step_t;

// The line's two sample streams and the frames they ride on.
typedef struct {
    daa_frame_t fr;
    sstream_t s;                   // the samples (sstream.h)
} line_stream_t;

// Fresh frame state: no access in progress; fc_hw as the SB bit is.
void daa_frame_init(daa_frame_t *f, bool fc_hw);
// Queue a control word; false if an access is already under way.
bool daa_frame_queue(daa_frame_t *f, uint16_t word);
// One received word; next_out is the sample for the next frame's primary.
// Fills st with what to send and the FC pin's level.
void daa_frame_word(daa_frame_t *f, uint16_t rx, int16_t next_out, daa_step_t *st);

// Fresh frames and streams, prefill samples of head start for the modem's samples.
void line_stream_init(line_stream_t *ls, uint32_t prefill);
// The frame engine on one received word, with the samples taken from and
// given to the streams (only while streaming; silence otherwise), and the
// under- and overruns counted. From the codec's interrupt. The streams
// themselves (opening, USB's side) are sstream.h's, on ls->s.
void line_stream_word(line_stream_t *ls, uint16_t rx, daa_step_t *st);
