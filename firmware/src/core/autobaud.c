/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// Automatic baud-rate detection. See autobaud.h.
//
// For each standard rate, decode the runs as a UART would. A rate fits if:
//   - a bit is no longer than the shortest run (with 25% to spare);
//   - every level change inside a character lands on a bit boundary
//     (within 5/16 of a bit, counted from the start bit or from the
//     previous change), at most 1 in 8 missing it;
//   - at most 1 in 8 characters has a framing or parity error, and no
//     start bit is a glitch.
// Of the rates that fit, take the slowest whose level changes are, on
// average, within 1/16 bit as close to boundaries as the best one's, and
// decide on it once AB_MIN_FRAMES characters decode at that rate.
//
// A rate too slow fails the first two (a 1-bit run, or changes in the middle
// of its bits); with no 1-bit run to go on, 3/4 of the real rate can fit
// some characters, but lands a quarter-bit off and scores worse. A rate too
// fast decodes a stop bit in the middle of the real character and fails the
// third; if it gets away with that, it scores no better and is the faster.
#include "autobaud.h"
#include <string.h>
#include "core.h"

const uint32_t AB_BAUDS[] = {110, 150, 300, 600, 1200, 2400, 4800, 9600, 14400, 19200,
                             28800, 38400, 57600, 115200, 230400};
const uint32_t AB_NBAUDS = sizeof AB_BAUDS / sizeof AB_BAUDS[0];

#define ALIGN_TOL16 5u       // edge within 5/16 of a bit boundary
#define AB_SCORE_MARGIN 16u  // 1/16 bit, in 1/256 bit

// A run's length in HAL ticks (bits 30..0).
static uint32_t run_len(uint32_t r) { return r & AB_IDLE; }
// A run's level: true for high (bit 31).
static bool run_high(uint32_t r) { return (r >> 31) != 0; }

void ab_reset(ab_t *a) { a->n = 0; }

bool ab_add(ab_t *a, bool high, uint32_t ticks) {
    if (ticks > AB_IDLE) ticks = AB_IDLE;
    if (a->n == 0 && high) return true;   // must start low; nothing to time yet
    if (a->n && run_high(a->run[a->n - 1]) == high) {
        uint32_t l = run_len(a->run[a->n - 1]);
        l = l > AB_IDLE - ticks ? AB_IDLE : l + ticks;
        a->run[a->n - 1] = (high ? 0x80000000u : 0) | l;
        return true;
    }
    if (a->n >= AB_MAX_RUNS) return false;
    a->run[a->n++] = (high ? 0x80000000u : 0) | ticks;
    return true;
}

void ab_drop(ab_t *a, uint32_t pairs) {
    uint32_t k = pairs * 2;
    if (k >= a->n) { a->n = 0; return; }
    memmove(a->run, a->run + k, (a->n - k) * sizeof a->run[0]);
    a->n -= k;
}

// A walk along the runs. Times are ticks from the start of the first run.
typedef struct {
    const ab_t *a;
    uint32_t i;          // current run
    uint64_t s;          // its start
    bool open_high;      // after the last run the line is high (the last run was low)
    // edge checking, while inside a character
    bool in_frame;
    uint64_t t0, prev;   // start bit's edge; the last edge
    uint32_t hz, baud;
    ab_stats_t *st;
} walk_t;

// How far a time span is from a whole number (at least 1) of bits, in 1/16 bit.
static uint32_t off_bits16(const walk_t *w, uint64_t span) {
    uint64_t b16 = (span * w->baud * 16u + w->hz / 2u) / w->hz;
    if (b16 < 16) return (uint32_t)(16 - b16);
    uint32_t frac = (uint32_t)(b16 & 15u);
    return frac < 8 ? frac : 16 - frac;
}

// A level change inside a character: it should be a whole number of bits
// after the start bit's edge, or after the previous change (so a sender a
// few percent off doesn't build up an error across the character).
static void edge(walk_t *w, uint64_t b) {
    if (!w->in_frame || b <= w->t0) return;
    uint32_t abs16 = off_bits16(w, b - w->t0), rel16 = off_bits16(w, b - w->prev);
    uint32_t d = abs16 < rel16 ? abs16 : rel16;
    w->st->edges++;
    w->st->off16 += d;
    if (d > ALIGN_TOL16) w->st->misaligned++;
    w->prev = b;
}

// Level at time t (t never goes backwards): 1 high, 0 low, -1 not known yet.
static int level_at(walk_t *w, uint64_t t) {
    while (w->i < w->a->n && t >= w->s + run_len(w->a->run[w->i])) {
        w->s += run_len(w->a->run[w->i]);
        w->i++;
        if (w->i < w->a->n) edge(w, w->s);
    }
    if (w->i < w->a->n) return run_high(w->a->run[w->i]);
    return w->open_high ? 1 : -1;
}

// First time >= t when the line is at level; UINT64_MAX if not known.
static uint64_t next_level(walk_t *w, uint64_t t, int level) {
    int l = level_at(w, t);
    if (l < 0) return UINT64_MAX;
    if (l == level) return t;
    if (w->i >= w->a->n) return UINT64_MAX;   // high from here on
    return w->s + run_len(w->a->run[w->i]);   // the next run starts at the other level
}

ab_stats_t ab_decode(const ab_t *a, uint32_t hz, uint32_t baud, const uart_format_t *f, ab_byte_fn fn, void *ctx) {
    ab_stats_t st = {0};
    if (!a->n || !baud || !hz) return st;
    walk_t w = {a, 0, 0, !run_high(a->run[a->n - 1]), false, 0, 0, hz, baud, &st};
    uint32_t nbits = 1u + f->data_bits + (f->parity ? 1u : 0u) + f->stop_bits;
    uint64_t pos = 0;
    for (;;) {
        uint64_t t0 = next_level(&w, pos, 0);
        if (t0 == UINT64_MAX) break;
        w.t0 = w.prev = t0;
        w.in_frame = true;
        int bits[13];   // up to 1 + 8 + 1 + 2
        bool incomplete = false, glitch = false;
        uint64_t t = t0;
        for (uint32_t k = 0; k < nbits; k++) {
            t = t0 + ((uint64_t)(2 * k + 1) * hz) / (2u * baud);   // middle of bit k
            int l = level_at(&w, t);
            if (l < 0) { incomplete = true; break; }
            bits[k] = l;
            if (k == 0 && l) { glitch = true; break; }
        }
        w.in_frame = false;
        if (incomplete) break;   // character not finished yet
        if (glitch) {            // the start bit didn't last half a bit
            st.noise++;
            pos = t;
            continue;
        }
        uint8_t byte = 0, ones = 0, err = 0;
        for (uint32_t d = 0; d < f->data_bits; d++)
            if (bits[1 + d]) { byte |= (uint8_t)(1u << d); ones++; }
        uint32_t p = 1u + f->data_bits;
        bool all_low = byte == 0;
        if (f->parity) {
            ones = (uint8_t)(ones + bits[p]);
            if (bits[p]) all_low = false;
            if ((f->parity == 1) != (ones & 1)) err |= RX_ERR_PARITY;   // odd: total ones odd
            p++;
        }
        bool stop_ok = true;
        for (uint32_t s = 0; s < f->stop_bits; s++)
            if (!bits[p + s]) stop_ok = false;
        if (!stop_ok) {
            err |= RX_ERR_FRAMING;
            if (all_low && !bits[p]) err |= RX_ERR_BREAK;
        }
        st.frames++;
        if (err) st.errors++;
        if (fn) fn(ctx, byte, err);
        pos = t + 1;
        if (err & RX_ERR_BREAK) {   // a break: wait for the line to go high again
            pos = next_level(&w, pos, 1);
            if (pos == UINT64_MAX) break;
        }
    }
    return st;
}

uint32_t ab_detect(const ab_t *a, uint32_t hz, const uart_format_t *f) {
    if (a->n < 3) return 0;
    uint32_t minrun = AB_IDLE;
    for (uint32_t i = 0; i < a->n; i++)
        if (run_len(a->run[i]) < minrun) minrun = run_len(a->run[i]);
    uint32_t score[sizeof AB_BAUDS / sizeof AB_BAUDS[0]], frames[sizeof AB_BAUDS / sizeof AB_BAUDS[0]];
    uint32_t best = UINT32_MAX;
    for (uint32_t b = 0; b < AB_NBAUDS; b++) {
        score[b] = UINT32_MAX;
        uint64_t bit = hz / AB_BAUDS[b];
        if (bit * 4u > (uint64_t)minrun * 5u) continue;   // a bit longer than the shortest run
        ab_stats_t st = ab_decode(a, hz, AB_BAUDS[b], f, 0, 0);
        if (!st.frames || st.noise || st.errors * 8u > st.frames ||
            st.misaligned * 8u > st.edges)
            continue;
        score[b] = st.edges ? st.off16 * 16u / st.edges : 0;   // mean distance from a boundary, 1/256 bit
        frames[b] = st.frames;
        if (score[b] < best) best = score[b];
    }
    for (uint32_t b = 0; b < AB_NBAUDS; b++)
        if (score[b] != UINT32_MAX && score[b] <= best + AB_SCORE_MARGIN)
            // enough characters at that rate? (a faster one can split one
            // character in two, so count them only at the chosen rate)
            return frames[b] >= AB_MIN_FRAMES ? AB_BAUDS[b] : 0;
    return 0;
}
