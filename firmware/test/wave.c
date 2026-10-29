/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
#include "wave.h"
#include <math.h>

static uint32_t rng = 12345;
double wave_rnd(void) {
    rng = rng * 1103515245u + 12345u;
    return (double)((rng >> 8) & 0xFFFFFF) / (double)0x1000000;
}

void wave_make(wave_t *w, uint32_t hz, uint32_t baud, double err, const uart_format_t *f,
               const uint8_t *msg, size_t len, double gap_bits, double jitter) {
    static bool lv[WAVE_MAX * 2];
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        int ones = 0;
        lv[n++] = false;
        for (int d = 0; d < f->data_bits; d++) { bool b = (msg[i] >> d) & 1; ones += b; lv[n++] = b; }
        if (f->parity) lv[n++] = f->parity == 1 ? !(ones & 1) : (ones & 1);
        for (int s = 0; s < f->stop_bits; s++) lv[n++] = true;
        for (int g = 0; g < (int)gap_bits; g++) lv[n++] = true;
    }
    double bit = (double)hz / (baud * (1.0 + err));
    double last = (wave_rnd() * 2 - 1) * jitter * bit;   // the first start bit's falling edge
    w->n = 0;
    for (size_t k = 1; k < n && w->n < WAVE_MAX; k++) {
        if (lv[k] == lv[k - 1]) continue;
        double e = (double)k * bit + (wave_rnd() * 2 - 1) * jitter * bit;
        w->high[w->n] = lv[k - 1];
        w->ticks[w->n] = (uint32_t)llround(e - last);
        w->n++;
        last = e;
    }
}
