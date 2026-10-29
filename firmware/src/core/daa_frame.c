/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
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
    sstream_init(&ls->s, prefill);
}

void line_stream_word(line_stream_t *ls, uint16_t rx, daa_step_t *st) {
    // the next primary's sample is only taken when this word is a primary
    // one: peek at the frame state rather than consume a sample on a secondary
    int16_t out = ls->fr.expect_secondary ? 0 : sstream_out(&ls->s);
    daa_frame_word(&ls->fr, rx, out, st);
    if (st->primary) sstream_in(&ls->s, st->sample);
}
