/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// A stick's sample streams. See sstream.h.
#include "sstream.h"

void sstream_init(sstream_t *ss, uint32_t prefill) {
    sfifo_init(&ss->to_host);
    sfifo_init(&ss->from_host);
    ss->streaming = false;
    ss->playing = false;
    ss->prefill = prefill;
    ss->underruns = ss->overruns = 0;
    ss->samples_in = ss->samples_out = 0;
}

int16_t sstream_out(sstream_t *ss) {
    int16_t s = 0;
    if (!ss->streaming) return 0;
    if (!ss->playing) {
        if (sfifo_count(&ss->from_host) < ss->prefill) return 0;
        ss->playing = true;
    }
    if (sfifo_get(&ss->from_host, &s)) {
        ss->samples_out++;
        return s;
    }
    ss->underruns++;
    ss->playing = false;
    return 0;
}

void sstream_in(sstream_t *ss, int16_t sample) {
    if (!ss->streaming) return;
    if (sfifo_put(&ss->to_host, sample)) ss->samples_in++;
    else ss->overruns++;   // the host isn't taking them
}

void sstream_set(sstream_t *ss, bool streaming) {
    sfifo_init(&ss->to_host);
    sfifo_init(&ss->from_host);
    ss->playing = false;
    ss->streaming = streaming;
}

void sstream_from_host(sstream_t *ss, const int16_t *s, uint32_t n) {
    uint32_t done = sfifo_write(&ss->from_host, s, n);
    if (done < n) ss->overruns = (uint16_t)(ss->overruns + (n - done));
}

uint32_t sstream_to_host(sstream_t *ss, int16_t *s, uint32_t max) {
    return sfifo_read(&ss->to_host, s, max);
}
