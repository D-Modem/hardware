/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// Automatic baud-rate detection, portable part. The HAL times the RX line
// (how long it stays low, then high, then low...) starting at the first
// falling edge; this works out which standard baud rate those times fit and
// decodes the bytes, so the ones it learned from aren't lost.
//
// The data format (data bits, parity, stop bits) is not detected: it comes
// from the host. See firmware/README.md "Automatic baud rate".
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "hal.h"

#define AB_MAX_RUNS 384u        // runs kept while learning (about 40 characters)
#define AB_SLIDE_RUNS 32u       // past this many without a result, forget the oldest
#define AB_MIN_FRAMES 2u        // characters needed before deciding ("AT")
#define AB_IDLE 0x7FFFFFFFu     // run length meaning "a long time"

// Each run: bit 31 = level (1 high), bits 30..0 = length in HAL ticks.
// The first run is always low (the HAL starts on a falling edge), and levels
// alternate.
typedef struct {
    uint32_t n;
    uint32_t run[AB_MAX_RUNS];
} ab_t;

typedef struct {
    uint32_t frames;       // characters decoded
    uint32_t errors;       // ...with a framing or parity error (or a break)
    uint32_t noise;        // start bits shorter than half a bit
    uint32_t edges;        // level changes inside characters
    uint32_t misaligned;   // ...not on a bit boundary
    uint32_t off16;        // total distance of those from a boundary, 1/16 bit
} ab_stats_t;

// The rates tried, lowest first.
extern const uint32_t AB_BAUDS[];
extern const uint32_t AB_NBAUDS;

void ab_reset(ab_t *a);
// Add a run; merges it into the last if the level didn't change. False if full.
bool ab_add(ab_t *a, bool high, uint32_t ticks);
// Forget the oldest runs (whole low+high pairs), keeping the first run low.
void ab_drop(ab_t *a, uint32_t pairs);

// The baud rate the runs fit, with this format, or 0 if not sure yet.
// hz = HAL ticks per second.
uint32_t ab_detect(const ab_t *a, uint32_t hz, const uart_format_t *f);

// Decode the runs at a baud rate as a UART would, calling fn for each
// character (errors = RX_ERR_* bits). Returns the statistics.
typedef void (*ab_byte_fn)(void *ctx, uint8_t byte, uint8_t errors);
ab_stats_t ab_decode(const ab_t *a, uint32_t hz, uint32_t baud, const uart_format_t *f, ab_byte_fn fn, void *ctx);
