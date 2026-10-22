/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The Si3056's serial frames and the line's sample streams. See daa_frame.h.
#include "daa_frame.h"

void daa_frame_init(daa_frame_t *f, bool fc_hw) {
    f->fc_hw = fc_hw;
    f->op_state = DAA_OP_IDLE;
    f->op_word = 0;
    f->op_result = 0;
    f->expect_secondary = false;
    f->primaries = f->secondaries = 0;
}

bool daa_frame_queue(daa_frame_t *f, uint16_t word) {
    if (f->op_state != DAA_OP_IDLE && f->op_state != DAA_OP_DONE) return false;
    f->op_word = word;
    f->op_state = DAA_OP_QUEUED;
    return true;
}

void daa_frame_word(daa_frame_t *f, uint16_t rx, int16_t next_out, daa_step_t *st) {
    st->n_tx = 0;
    st->fc = -1;
    st->sample = 0;
    if (f->expect_secondary) {
        // the control word's answer: a read's value in the low byte
        f->expect_secondary = false;
        f->op_result = (uint8_t)rx;
        f->op_state = DAA_OP_DONE;
        f->secondaries++;
        if (f->fc_hw) st->fc = 0;
        st->primary = false;
        return;
    }
    st->primary = true;
    st->sample = (int16_t)rx;
    f->primaries++;
    // the frame whose primary just ended was asked for a secondary: it follows
    if (f->op_state == DAA_OP_ARMED) {
        f->op_state = DAA_OP_SECONDARY;
        f->expect_secondary = true;
    }
    uint16_t s = (uint16_t)next_out;
    if (!f->fc_hw) s &= (uint16_t)~1u;   // 15-bit mode: the LSB is the request flag
    if (f->op_state == DAA_OP_QUEUED && !f->expect_secondary) {
        // ask for a secondary in the next frame, and queue its word right behind
        if (f->fc_hw) st->fc = 1;
        else s |= 1u;
        st->tx[st->n_tx++] = s;
        st->tx[st->n_tx++] = f->op_word;
        f->op_state = DAA_OP_ARMED;
    } else {
        st->tx[st->n_tx++] = s;
    }
}

void line_stream_init(line_stream_t *ls, uint32_t prefill) {
    daa_frame_init(&ls->fr, false);
    sfifo_init(&ls->to_host);
    sfifo_init(&ls->from_host);
    ls->streaming = false;
    ls->playing = false;
    ls->prefill = prefill;
    ls->underruns = ls->overruns = 0;
    ls->samples_in = ls->samples_out = 0;
}

// The sample to send in the next frame: the modem's, once from_host has had
// its head start; silence while it fills, or when it ran dry (an underrun,
// after which it waits for its head start again).
static int16_t next_out(line_stream_t *ls) {
    int16_t s = 0;
    if (!ls->streaming) return 0;
    if (!ls->playing) {
        if (sfifo_count(&ls->from_host) < ls->prefill) return 0;
        ls->playing = true;
    }
    if (sfifo_get(&ls->from_host, &s)) {
        ls->samples_out++;
        return s;
    }
    ls->underruns++;
    ls->playing = false;
    return 0;
}

void line_stream_word(line_stream_t *ls, uint16_t rx, daa_step_t *st) {
    // the next primary's sample is only taken when this word is a primary
    // one: peek at the frame state rather than consume a sample on a secondary
    int16_t out = ls->fr.expect_secondary ? 0 : next_out(ls);
    daa_frame_word(&ls->fr, rx, out, st);
    if (st->primary && ls->streaming) {
        if (sfifo_put(&ls->to_host, st->sample)) ls->samples_in++;
        else ls->overruns++;   // the host isn't taking them
    }
}

void line_stream_set(line_stream_t *ls, bool streaming) {
    sfifo_init(&ls->to_host);
    sfifo_init(&ls->from_host);
    ls->playing = false;
    ls->streaming = streaming;
}

void line_stream_from_host(line_stream_t *ls, const int16_t *s, uint32_t n) {
    uint32_t done = sfifo_write(&ls->from_host, s, n);
    if (done < n) ls->overruns = (uint16_t)(ls->overruns + (n - done));
}

uint32_t line_stream_to_host(line_stream_t *ls, int16_t *s, uint32_t max) {
    return sfifo_read(&ls->to_host, s, max);
}
