/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
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
#include "sfifo.h"

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
    sfifo_t to_host;               // the line's samples, for USB IN
    sfifo_t from_host;             // the modem's samples, from USB OUT
    volatile bool streaming;       // the host has the stream open (USB alternate setting 1)
    bool playing;                  // from_host has had its head start
    uint32_t prefill;              // samples from_host must hold before playing starts
    volatile uint16_t underruns, overruns;
    volatile uint32_t samples_in, samples_out;
} line_stream_t;

// Fresh frame state: no access in progress; fc_hw as the SB bit is.
void daa_frame_init(daa_frame_t *f, bool fc_hw);
// Queue a control word; false if an access is already under way.
bool daa_frame_queue(daa_frame_t *f, uint16_t word);
// One received word; next_out is the sample for the next frame's primary.
// Fills st with what to send and the FC pin's level.
void daa_frame_word(daa_frame_t *f, uint16_t rx, int16_t next_out, daa_step_t *st);

// Fresh streams, prefill samples of head start for the modem's samples.
void line_stream_init(line_stream_t *ls, uint32_t prefill);
// The frame engine on one received word, with the samples taken from and
// given to the FIFOs (only while streaming; silence otherwise), and the
// under- and overruns counted. From the codec's interrupt.
void line_stream_word(line_stream_t *ls, uint16_t rx, daa_step_t *st);
// The host opened or closed the stream: both FIFOs emptied, playback waits
// for its head start again. Call with the codec's interrupt off.
void line_stream_set(line_stream_t *ls, bool streaming);
// USB OUT: the modem's samples, up to what fits; the rest counted as overruns.
void line_stream_from_host(line_stream_t *ls, const int16_t *s, uint32_t n);
// USB IN: the line's samples waiting, up to max; returns how many.
uint32_t line_stream_to_host(line_stream_t *ls, int16_t *s, uint32_t max);
