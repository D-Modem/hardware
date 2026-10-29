/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// The phone-line stick's device logic. See line_dev.h and line_proto.h.
#include <string.h>
#include "line_dev.h"
#include "line_hal.h"
#include "si3056.h"

#ifndef PS_FW_MAJOR
#define PS_FW_MAJOR 0
#define PS_FW_MINOR 0
#define PS_FW_PATCH 0
#endif

#define FLASH_MAX_MS 2000u
#define INT_MASK (SI_INT_RDT | SI_INT_ROV | SI_INT_FDT | SI_INT_DOD | SI_INT_POL)

// Register 5 as the firmware keeps it: the OFHK pin enabled (the hook is the
// pin's, so pulse dialling isn't held up by the serial frames), the monitor
// as asked.
static uint8_t daa1(const line_dev_t *d) { return (uint8_t)(SI_DAA1_OHE | (d->monitor ? SI_DAA1_ONHM : 0)); }

// Whether the line side answers (register 12's FDT); when it does, its IDs
// (registers 11 and 13 describe it only then).
static void check_link(line_dev_t *d) {
    uint8_t v = 0;
    d->t_link = lhal_millis();
    d->link = lhal_daa_read(SI_STATUS, &v) && (v & SI_STATUS_FDT);
    if (d->link) {
        lhal_daa_read(SI_REV, &d->reg11);
        lhal_daa_read(SI_REVB, &d->reg13);
    }
}

bool line_dev_init(line_dev_t *d, uint16_t rate) {
    int src = lp_rate_code(rate);
    if (src < 0) {
        rate = 9600;
        src = lp_rate_code(rate);
    }
    memset(d, 0, sizeof *d);
    d->rate = rate;
    lhal_hook(false);
    d->frames = lhal_daa_start(src);
    if (d->frames) {
        lhal_daa_write(SI_CONTROL2, SI_CONTROL2_INTE | SI_CONTROL2_HBE | SI_CONTROL2_RXE);
        lhal_daa_write(SI_INT_MASK, INT_MASK);
        lhal_daa_write(SI_INT_SOURCE, 0);
        lhal_daa_write(SI_DAA1, daa1(d));
        check_link(d);
    }
    return d->frames;
}

uint8_t line_dev_state(const line_dev_t *d) {
    uint8_t s = 0;
    if (d->link) s |= LP_ST_LINK;
    if (d->off_hook && !d->flash_until) s |= LP_ST_OFF_HOOK;
    if (d->monitor) s |= LP_ST_MONITOR;
    if (lhal_streaming()) s |= LP_ST_STREAMING;
    if (lhal_ring()) s |= LP_ST_RINGING;
    if (d->flash_until) s |= LP_ST_FLASH;
    return s;
}

// A register for a status reply: its value, 0 if it couldn't be read.
static uint8_t reg_or_0(uint8_t reg) {
    uint8_t v = 0;
    if (!lhal_daa_read(reg, &v)) v = 0;
    return v;
}

int line_dev_vendor_in(line_dev_t *d, uint8_t req, uint16_t value, uint8_t *buf, uint16_t max) {
    switch (req) {
    case LP_REQ_GET_INFO: {
        lp_info_t i = {LP_VERSION, PS_FW_MAJOR, PS_FW_MINOR, PS_FW_PATCH, d->rate, LP_MAX_SAMPLES,
                       line_dev_state(d), d->reg11, d->reg13, {0, 0}};
        uint16_t n = max < sizeof i ? max : (uint16_t)sizeof i;
        memcpy(buf, &i, n);
        return n;
    }
    case LP_REQ_GET_STATUS: {
        lp_status_t s;
        memset(&s, 0, sizeof s);
        s.reg12 = reg_or_0(SI_STATUS);
        s.reg28 = reg_or_0(SI_LCS2);
        s.reg29 = reg_or_0(SI_LVS);
        s.reg5 = reg_or_0(SI_DAA1);
        s.int_src = d->int_status;
        d->int_status = 0;
        s.state = line_dev_state(d);
        uint16_t under, over;
        uint32_t in, out;
        lhal_counters(&under, &over, &in, &out);
        s.underruns = under;
        s.overruns = over;
        s.samples_in = in;
        s.samples_out = out;
        s.ring_edges = lhal_ring_edges();
        uint16_t n = max < sizeof s ? max : (uint16_t)sizeof s;
        memcpy(buf, &s, n);
        return n;
    }
    case LP_REQ_DAA_READ: {
        uint8_t v;
        if (value > 127 || max < 1 || !lhal_daa_read((uint8_t)value, &v)) return -1;
        buf[0] = v;
        return 1;
    }
    default:
        return -1;
    }
}

bool line_dev_vendor_out(line_dev_t *d, uint8_t req, uint16_t value) {
    switch (req) {
    case LP_REQ_SET_RATE: {
        int src = lp_rate_code(value);
        if (src < 0 || !lhal_daa_write(SI_SRC, (uint8_t)src)) return false;
        d->rate = value;
        return true;
    }
    case LP_REQ_SET_HOOK:
        if (value > 1) return false;
        d->off_hook = value == 1;
        d->flash_until = 0;
        lhal_hook(d->off_hook);
        return true;
    case LP_REQ_HOOK_FLASH: {
        if (value == 0 || value > FLASH_MAX_MS) return false;
        if (!d->off_hook) return true;   // already on hook: nothing to flash
        uint32_t t = lhal_millis() + value;
        d->flash_until = t ? t : 1;
        lhal_hook(false);
        return true;
    }
    case LP_REQ_SET_MONITOR:
        if (value > 1) return false;
        d->monitor = value == 1;
        return lhal_daa_write(SI_DAA1, daa1(d));
    case LP_REQ_DAA_WRITE: {
        uint8_t reg = (uint8_t)(value >> 8), v = (uint8_t)value;
        if (reg > 127) return false;
        if (reg == SI_DAA1) {   // the hook stays the pin's; the monitor follows the bit
            d->monitor = (v & SI_DAA1_ONHM) != 0;
            v = daa1(d);
        }
        return lhal_daa_write(reg, v);
    }
    case LP_REQ_RESET:
        line_dev_init(d, d->rate);
        return true;
    case LP_REQ_REBOOT_UF2:
        lhal_reboot_uf2();
        return true;
    default:
        return false;
    }
}

void line_dev_poll(line_dev_t *d) {
    if (d->flash_until && (int32_t)(lhal_millis() - d->flash_until) >= 0) {
        d->flash_until = 0;
        lhal_hook(d->off_hook);
    }
    if (d->frames && (uint32_t)(lhal_millis() - d->t_link) >= 1000) check_link(d);
    if (d->frames && lhal_int()) {
        uint8_t v;
        if (lhal_daa_read(SI_INT_SOURCE, &v)) {
            d->int_src |= v;
            d->int_status |= v;
            lhal_daa_write(SI_INT_SOURCE, 0);
        }
    }
    lhal_leds(d->off_hook && !d->flash_until, lhal_ring());
}

bool line_dev_event(line_dev_t *d, lp_event_t *e) {
    uint16_t under, over;
    uint32_t in, out;
    lhal_counters(&under, &over, &in, &out);
    memset(e, 0, sizeof *e);
    e->state = line_dev_state(d);
    e->ring_edges = lhal_ring_edges();
    e->int_src = d->int_src;
    e->underruns = under;
    e->overruns = over;
    if (d->sent_valid && e->state == d->sent.state && e->ring_edges == d->sent.ring_edges && e->int_src == 0 &&
        e->underruns == d->sent.underruns && e->overruns == d->sent.overruns)
        return false;
    e->seq = (uint8_t)(d->sent.seq + 1);
    d->sent = *e;
    d->sent_valid = true;
    d->int_src = 0;
    return true;
}
