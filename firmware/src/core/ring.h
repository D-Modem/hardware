/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// Single-producer single-consumer byte ring. One side may run in an interrupt
// handler and the other in the main loop (single core): indices are only
// written by their owner, and each is read once per call.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define RING_SIZE 4096u   // power of two
#define RING_MASK (RING_SIZE - 1u)

typedef struct {
    volatile uint32_t head;   // written by the producer
    volatile uint32_t tail;   // written by the consumer
    uint8_t buf[RING_SIZE];
} ring_t;

// Empty the ring (only while neither side is using it).
static inline void ring_init(ring_t *r) { r->head = r->tail = 0; }
// Bytes waiting to be read.
static inline uint32_t ring_count(const ring_t *r) { return r->head - r->tail; }
// Bytes that can be written before it's full.
static inline uint32_t ring_free(const ring_t *r) { return RING_SIZE - ring_count(r); }

// Producer: add one byte; false (byte dropped) if the ring is full.
static inline bool ring_put(ring_t *r, uint8_t b) {
    uint32_t h = r->head;
    if (h - r->tail >= RING_SIZE) return false;
    r->buf[h & RING_MASK] = b;
    r->head = h + 1;
    return true;
}

// Consumer: take the oldest byte; false if the ring is empty.
static inline bool ring_get(ring_t *r, uint8_t *b) {
    uint32_t t = r->tail;
    if (r->head == t) return false;
    *b = r->buf[t & RING_MASK];
    r->tail = t + 1;
    return true;
}

// Producer: add up to n bytes; returns how many fitted.
static inline uint32_t ring_write(ring_t *r, const uint8_t *src, uint32_t n) {
    uint32_t done = 0;
    while (done < n && ring_put(r, src[done])) done++;
    return done;
}

// Consumer: take up to n bytes; returns how many there were.
static inline uint32_t ring_read(ring_t *r, uint8_t *dst, uint32_t n) {
    uint32_t done = 0;
    while (done < n && ring_get(r, &dst[done])) done++;
    return done;
}
