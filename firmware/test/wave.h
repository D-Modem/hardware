/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// Test waveforms: the line timings a capture would see for some characters.
#pragma once
#include <stddef.h>
#include "../src/core/hal.h"

#define WAVE_MAX 8192
typedef struct {
    uint32_t n;
    bool high[WAVE_MAX];
    uint32_t ticks[WAVE_MAX];
} wave_t;

// Characters sent at baud * (1 + err), each edge moved by up to +-jitter of
// a bit, gap_bits of idle between characters. Starts at the first start bit;
// the run still going at the end (the final stop bit and idle) isn't included.
void wave_make(wave_t *w, uint32_t hz, uint32_t baud, double err, const uart_format_t *f,
               const uint8_t *msg, size_t len, double gap_bits, double jitter);
double wave_rnd(void);   // 0..1, repeatable
