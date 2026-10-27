/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The WB stick's samples between the codec's I2S frames and the sample
// streams (portable: the DMA interrupt calls it with each block of frames;
// host tests do too).
//
// Each I2S frame is two 32-bit words, left then right, MSB first: the
// codec's two ADCs in (left: the hybrid, right: the line), the DACs out.
// One ADC channel goes to the host as S16 (the top 16 bits); the host's
// S16 sample goes out in both slots (the left DAC plays it, the right one
// is powered down). The firmware loopback sends the host's samples back to
// it in place of the line's. The largest |sample| each way is kept for the
// host's level readout.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sstream.h"

enum { WB_PEAK_HYBRID, WB_PEAK_LINE, WB_PEAK_TX, WB_PEAKS };

typedef struct {
    sstream_t s;                   // the samples (sstream.h)
    volatile uint8_t input;        // WP_INPUT_*: the ADC channel the host gets
    volatile bool loopback;        // the host's samples back to it (WP_LOOP_FIRMWARE)
    volatile uint16_t peak[WB_PEAKS];
    volatile uint32_t frames;      // frames received (wraps)
    volatile uint32_t tx_active;   // non-silent samples of the host's played (wraps)
} wb_audio_t;

// Fresh streams (prefill samples of head start for the host's), the left
// ADC for the host, no loopback, the peaks cleared.
void wb_audio_init(wb_audio_t *a, uint32_t prefill);
// A block of received frames (2 words each): the chosen channel to the host
// (unless looping back), the peaks, the frame count.
void wb_audio_rx(wb_audio_t *a, const uint32_t *words, uint32_t frames);
// A block of frames to send (2 words each), from the host's samples (silence
// while there are none); in loopback each also goes back to the host.
void wb_audio_tx(wb_audio_t *a, uint32_t *words, uint32_t frames);
// The peaks since the last call, then cleared. Call with the interrupt off.
void wb_audio_peaks(wb_audio_t *a, uint16_t peak[WB_PEAKS]);
// An I2S word's sample: its top 16 bits.
static inline int16_t wb_word_sample(uint32_t w) { return (int16_t)(uint16_t)(w >> 16); }
// A sample as an I2S word: MSB-aligned, the low bits zero.
static inline uint32_t wb_sample_word(int16_t s) { return (uint32_t)(uint16_t)s << 16; }
