/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// Host tests for the baud-rate detector: waveforms made from text and random
// bytes at every standard rate, with timing jitter, a sender running fast or
// slow, other data formats, glitches, and a capture starting mid-character.
// Build and run: make -C firmware/test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/autobaud.h"
#include "../src/core/core.h"
#include "wave.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static ab_t a;
static wave_t wv;
#define rnd wave_rnd

static void synth(uint32_t hz, uint32_t baud, double err, const uart_format_t *f,
                  const uint8_t *msg, size_t len, double gap_bits, double jitter) {
    wave_make(&wv, hz, baud, err, f, msg, len, gap_bits, jitter);
    ab_reset(&a);
    for (uint32_t i = 0; i < wv.n; i++) ab_add(&a, wv.high[i], wv.ticks[i]);
}

typedef struct { uint8_t buf[512]; uint32_t n; uint32_t errs; } sink_t;
static void sink(void *ctx, uint8_t b, uint8_t e) {
    sink_t *s = ctx;
    if (s->n < sizeof s->buf) s->buf[s->n++] = b;
    if (e) s->errs++;
}

static bool decodes_to(uint32_t hz, uint32_t baud, const uart_format_t *f, const uint8_t *msg, size_t len) {
    sink_t s = {{0}, 0, 0};
    uint8_t mask = (uint8_t)((1u << f->data_bits) - 1);
    ab_decode(&a, hz, baud, f, sink, &s);
    if (s.n != len || s.errs) return false;
    for (size_t i = 0; i < len; i++)
        if (s.buf[i] != (msg[i] & mask)) return false;
    return true;
}

static const uart_format_t F8N1 = {0, 8, 1, 0}, F7E1 = {0, 7, 1, 2}, F8N2 = {0, 8, 2, 0},
                           F7O1 = {0, 7, 1, 1}, F8E1 = {0, 8, 1, 2};

static void every_rate(const char *name, const uart_format_t *f, const uint8_t *msg, size_t len,
                       double err, double jitter, double gap, uint32_t hz) {
    int bad = 0;
    for (uint32_t b = 0; b < AB_NBAUDS; b++) {
        synth(hz, AB_BAUDS[b], err, f, msg, len, gap, jitter);
        uint32_t got = ab_detect(&a, hz, f);
        bool ok = got == AB_BAUDS[b] && decodes_to(hz, got, f, msg, len);
        CHECK(ok);
        if (!ok) { bad++; printf("    %s at %u: got %u\n", name, AB_BAUDS[b], got); }
    }
    printf("%-44s %s\n", name, bad ? "FAIL" : "ok");
}

int main(void) {
    const uint32_t HZ = 62500000;   // 125 MHz / 2
    const uint8_t at[] = "AT\r", at_lc[] = "at", dial[] = "ATDT5551212\r", ok[] = "\r\nOK\r\n";
    const uint8_t at2[] = "AT";

    every_rate("AT\\r, 8N1, exact", &F8N1, at, 3, 0, 0, 0, HZ);
    every_rate("AT (just 2 characters), 8N1", &F8N1, at2, 2, 0, 0, 0, HZ);
    every_rate("at, 8N1, sender 2% fast, jitter 0.08 bit", &F8N1, at_lc, 2, 0.02, 0.08, 0, HZ);
    every_rate("AT\\r, 8N1, sender 2.5% slow, jitter 0.1", &F8N1, at, 3, -0.025, 0.1, 0, HZ);
    every_rate("ATDT5551212\\r, 8N1, gaps of 3 bits", &F8N1, dial, 12, 0.01, 0.05, 3, HZ);
    every_rate("\\r\\nOK\\r\\n, 8N1, back to back", &F8N1, ok, 6, -0.01, 0.05, 0, HZ);
    every_rate("ATDT5551212\\r, 7E1", &F7E1, dial, 12, 0.015, 0.05, 0, HZ);
    every_rate("ATDT5551212\\r, 7O1", &F7O1, dial, 12, -0.015, 0.05, 1, HZ);
    every_rate("ATDT5551212\\r, 8N2", &F8N2, dial, 12, 0.0, 0.05, 0, HZ);
    every_rate("ATDT5551212\\r, 8E1", &F8E1, dial, 12, 0.0, 0.05, 0, HZ);
    every_rate("AT\\r, 8N1, 100 MHz ticks (200 MHz clock)", &F8N1, at, 3, 0.01, 0.05, 0, 100000000);

    uint8_t bin[40];
    int bad = 0;
    for (int trial = 0; trial < 200; trial++) {
        for (size_t i = 0; i < sizeof bin; i++) bin[i] = (uint8_t)(rnd() * 256);
        uint32_t baud = AB_BAUDS[trial % AB_NBAUDS];
        synth(HZ, baud, (rnd() - 0.5) * 0.04, &F8N1, bin, 16, (double)(trial % 3), 0.06);
        uint32_t got = ab_detect(&a, HZ, &F8N1);
        if (got != baud) { bad++; printf("    random bytes at %u: got %u\n", baud, got); }
        CHECK(got == baud);
    }
    printf("%-44s %s (%d/200 wrong)\n", "200 x 16 random bytes, random rates", bad ? "FAIL" : "ok", bad);

    // characters with no 1-bit runs: half the rate is ruled out by the
    // bit-boundary check, 3/4 of it by the score
    {
        const char *tricky[] = {"0>", "<0", "<8", "><", "0>0>", "<0<8", "<8<8<8", "><><><"};
        int wrong = 0, undecided = 0;
        for (int t = 0; t < 8; t++)
            for (uint32_t b = 1; b < AB_NBAUDS; b++)
                for (int gap = 0; gap < 4; gap++) {
                    size_t len = strlen(tricky[t]);
                    synth(HZ, AB_BAUDS[b], 0, &F8N1, (const uint8_t *)tricky[t], len, gap, 0.03);
                    uint32_t got = ab_detect(&a, HZ, &F8N1);
                    if (!got) undecided++;
                    else if (got != AB_BAUDS[b]) {
                        wrong++;
                        printf("    \"%s\" gap %d at %u: got %u\n", tricky[t], gap, AB_BAUDS[b], got);
                    }
                }
        CHECK(wrong == 0);
        CHECK(undecided == 0);
        printf("%-44s %s\n", "no 1-bit runs (\"<0\" etc.)", wrong ? "FAIL" : "ok");
    }

    // one character isn't enough to decide, even one a faster rate would
    // split in two (CR, as Enter sends on many terminals)
    {
        const char *one[] = {"A", "\r", "\n", "a", "~", "\x55", "\xff", "0"};
        int decided = 0;
        for (int t = 0; t < 8; t++)
            for (uint32_t b = 0; b < AB_NBAUDS; b++) {
                synth(HZ, AB_BAUDS[b], 0.01, &F8N1, (const uint8_t *)one[t], 1, 0, 0.03);
                if (ab_detect(&a, HZ, &F8N1)) decided++;
            }
        CHECK(decided == 0);
        printf("%-44s %s\n", "one character: no decision yet", decided ? "FAIL" : "ok");
    }
    every_rate("CR LF (Enter), 8N1", &F8N1, (const uint8_t *)"\r\n", 2, 0.02, 0.05, 0, HZ);
    every_rate("CR CR (Enter twice, CR only), 7E1", &F7E1, (const uint8_t *)"\r\r", 2, -0.02, 0.05, 2, HZ);

    // a glitch before the data stops a decision until it's dropped
    synth(HZ, 9600, 0, &F8N1, at, 3, 0, 0);
    {
        ab_t g;
        ab_reset(&g);
        ab_add(&g, false, 300);       // 5 us low glitch
        ab_add(&g, true, HZ / 100);   // 10 ms idle
        for (uint32_t i = 0; i < a.n; i++) ab_add(&g, a.run[i] >> 31, a.run[i] & AB_IDLE);
        CHECK(ab_detect(&g, HZ, &F8N1) == 0);
        ab_drop(&g, 1);
        CHECK(ab_detect(&g, HZ, &F8N1) == 9600);
        printf("%-44s ok\n", "glitch first: undecided until dropped");
    }

    // capture starting in the middle of a character: drop runs until it fits
    {
        synth(HZ, 19200, 0, &F8N1, dial, 12, 0, 0.03);
        ab_drop(&a, 2);   // starts mid-"A"/"T"
        uint32_t got = 0;
        for (int k = 0; k < 6 && !(got = ab_detect(&a, HZ, &F8N1)); k++) ab_drop(&a, 1);
        CHECK(got == 19200);
        printf("%-44s %s\n", "capture started mid-character", got == 19200 ? "ok" : "FAIL");
    }

    // add merges same-level runs and the first run is low
    ab_reset(&a);
    CHECK(ab_add(&a, true, 100) && a.n == 0);
    ab_add(&a, false, 10); ab_add(&a, false, 5); ab_add(&a, true, 7);
    CHECK(a.n == 2 && (a.run[0] & AB_IDLE) == 15 && (a.run[1] >> 31));
    ab_add(&a, true, AB_IDLE);
    CHECK((a.run[1] & AB_IDLE) == AB_IDLE);
    ab_reset(&a);
    for (uint32_t i = 0; i < AB_MAX_RUNS; i++) CHECK(ab_add(&a, i & 1, 100));
    CHECK(!ab_add(&a, AB_MAX_RUNS & 1, 100));

    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
