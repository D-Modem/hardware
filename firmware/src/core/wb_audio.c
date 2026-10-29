/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// The WB stick's samples between the I2S frames and the streams. See wb_audio.h.
#include "wb_audio.h"
#include "wb_proto.h"

void wb_audio_init(wb_audio_t *a, uint32_t prefill) {
    sstream_init(&a->s, prefill);
    a->input = WP_INPUT_HYBRID;
    a->loopback = false;
    for (int i = 0; i < WB_PEAKS; i++) a->peak[i] = 0;
    a->frames = 0;
    a->tx_active = 0;
}

// |s| (32768 for -32768).
static uint16_t mag(int16_t s) { return (uint16_t)(s < 0 ? -(int32_t)s : s); }

// peak = max(peak, |s|).
static void track(volatile uint16_t *peak, int16_t s) {
    uint16_t m = mag(s);
    if (m > *peak) *peak = m;
}

void wb_audio_rx(wb_audio_t *a, const uint32_t *words, uint32_t frames) {
    uint8_t input = a->input;
    bool loop = a->loopback;
    for (uint32_t i = 0; i < frames; i++) {
        int16_t l = wb_word_sample(words[2 * i]), r = wb_word_sample(words[2 * i + 1]);
        track(&a->peak[WB_PEAK_HYBRID], l);
        track(&a->peak[WB_PEAK_LINE], r);
        if (!loop) sstream_in(&a->s, input == WP_INPUT_LINE ? r : l);
    }
    a->frames += frames;
}

void wb_audio_tx(wb_audio_t *a, uint32_t *words, uint32_t frames) {
    bool loop = a->loopback;
    for (uint32_t i = 0; i < frames; i++) {
        int16_t s = sstream_out(&a->s);
        if (s) a->tx_active++;
        track(&a->peak[WB_PEAK_TX], s);
        words[2 * i] = words[2 * i + 1] = wb_sample_word(s);
        if (loop) sstream_in(&a->s, s);
    }
}

void wb_audio_peaks(wb_audio_t *a, uint16_t peak[WB_PEAKS]) {
    for (int i = 0; i < WB_PEAKS; i++) {
        peak[i] = a->peak[i];
        a->peak[i] = 0;
    }
}
