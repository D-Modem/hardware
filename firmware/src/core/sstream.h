/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// A stick's two sample streams (portable; both sticks use them): the line's
// samples to the host (USB IN) and the modem's samples from it (USB OUT),
// each a FIFO between the codec's interrupt and USB in the main loop. The
// modem's samples get a head start before they play; running dry and
// overflowing are counted. The codec's side runs one sample each way per
// sample period, at the codec's clock.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sfifo.h"

typedef struct {
    sfifo_t to_host;               // the line's samples, for USB IN
    sfifo_t from_host;             // the modem's samples, from USB OUT
    volatile bool streaming;       // the host has the stream open (USB alternate setting 1)
    bool playing;                  // from_host has had its head start
    uint32_t prefill;              // samples from_host must hold before playing starts
    volatile uint16_t underruns, overruns;
    volatile uint32_t samples_in, samples_out;
} sstream_t;

// Fresh streams, prefill samples of head start for the modem's samples.
void sstream_init(sstream_t *ss, uint32_t prefill);
// Codec side: the sample to play now. The modem's, once from_host has had its
// head start; silence while it fills, while not streaming, or when it ran dry
// (an underrun, after which it waits for its head start again).
int16_t sstream_out(sstream_t *ss);
// Codec side: a sample from the line, for the host (only while streaming;
// dropped otherwise). One the host isn't taking counts as an overrun.
void sstream_in(sstream_t *ss, int16_t sample);
// The host opened or closed the stream: both FIFOs emptied, playback waits
// for its head start again. Call with the codec's interrupt off.
void sstream_set(sstream_t *ss, bool streaming);
// USB OUT: the modem's samples, up to what fits; the rest counted as overruns.
void sstream_from_host(sstream_t *ss, const int16_t *s, uint32_t n);
// USB IN: the line's samples waiting, up to max; returns how many.
uint32_t sstream_to_host(sstream_t *ss, int16_t *s, uint32_t max);
