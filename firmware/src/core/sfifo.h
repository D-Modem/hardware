/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// Single-producer single-consumer ring of 16-bit samples, for the sticks
// (sstream.h): the codec's interrupt on one side, USB in the main loop on
// the other (single core). As ring.h: each index is written only by its owner.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifndef SFIFO_SIZE
#define SFIFO_SIZE 1024u   // power of two: about 100 ms at 9600 Hz (the WB stick's build sets 4096)
#endif
#define SFIFO_MASK (SFIFO_SIZE - 1u)

typedef struct {
    volatile uint32_t head;   // written by the producer
    volatile uint32_t tail;   // written by the consumer
    int16_t buf[SFIFO_SIZE];
} sfifo_t;

// Empty it (only while neither side is using it).
static inline void sfifo_init(sfifo_t *f) { f->head = f->tail = 0; }
// Samples waiting to be read.
static inline uint32_t sfifo_count(const sfifo_t *f) { return f->head - f->tail; }
// Producer: add a sample; false (dropped) if full.
static inline bool sfifo_put(sfifo_t *f, int16_t s) {
    uint32_t h = f->head;
    if (h - f->tail >= SFIFO_SIZE) return false;
    f->buf[h & SFIFO_MASK] = s;
    f->head = h + 1;
    return true;
}
// Consumer: take the oldest sample; false if empty.
static inline bool sfifo_get(sfifo_t *f, int16_t *s) {
    uint32_t t = f->tail;
    if (f->head == t) return false;
    *s = f->buf[t & SFIFO_MASK];
    f->tail = t + 1;
    return true;
}
// Consumer: take up to n samples; returns how many there were.
static inline uint32_t sfifo_read(sfifo_t *f, int16_t *dst, uint32_t n) {
    uint32_t done = 0;
    while (done < n && sfifo_get(f, &dst[done])) done++;
    return done;
}
// Producer: add up to n samples; returns how many fitted.
static inline uint32_t sfifo_write(sfifo_t *f, const int16_t *src, uint32_t n) {
    uint32_t done = 0;
    while (done < n && sfifo_put(f, src[done])) done++;
    return done;
}
