/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// The WB stick's device logic. See wb_dev.h and wb_proto.h.
#include <string.h>
#include "aic3204.h"
#include "wb_dev.h"
#include "wb_hal.h"

#ifndef PS_FW_MAJOR
#define PS_FW_MAJOR 0
#define PS_FW_MINOR 0
#define PS_FW_PATCH 0
#endif

#define CLOCK_CHECK_MS 100       // the frames counted this often
#define CLOCK_START_MS 20        // the first frames after a start, at most this late
#define DATA_MS 50               // the DATA LED stays lit this long after a non-silent sample

// Whether the codec is up and clocking.
static bool codec_up(const wb_dev_t *d) { return d->result == WB_CODEC_OK && d->clock; }

// Whether the LINK LED is lit: as the host set it, or (AUTO) while streaming.
static bool carrier(const wb_dev_t *d) {
    return d->carrier == WP_CARRIER_ON || (d->carrier == WP_CARRIER_AUTO && whal_streaming());
}

bool wb_dev_init(wb_dev_t *d, uint32_t rate) {
    memset(d, 0, sizeof *d);
    d->rate = wb_rate(rate);
    if (!d->rate) d->rate = wb_rate(96000);
    d->carrier = WP_CARRIER_AUTO;
    d->input = WP_INPUT_HYBRID;
    d->loopback = WP_LOOP_OFF;
    return wb_dev_start(d);
}

bool wb_dev_start(wb_dev_t *d) {
    whal_i2s_stop();
    wb_codec_cfg_t cfg = {d->rate, d->tx_gain, d->rx_gain, d->loopback == WP_LOOP_CODEC};
    d->result = wb_codec_start(&cfg);
    whal_i2s_start(d->rate->hz);
    whal_set_input(d->input);
    whal_set_loopback(d->loopback == WP_LOOP_FIRMWARE);
    // the codec's clocks: frames arriving within a few ms
    uint32_t f0 = whal_frames();
    d->clock = false;
    for (uint32_t t = 0; t < CLOCK_START_MS && !d->clock; t++) {
        whal_sleep_ms(1);
        d->clock = whal_frames() != f0;
    }
    d->t_clock = whal_millis();
    d->frames_seen = whal_frames();
    return codec_up(d);
}

uint8_t wb_dev_state(const wb_dev_t *d) {
    uint8_t s = 0;
    if (codec_up(d)) s |= WP_ST_CODEC;
    if (carrier(d)) s |= WP_ST_CARRIER;
    if (d->loopback != WP_LOOP_OFF) s |= WP_ST_LOOPBACK;
    if (whal_streaming()) s |= WP_ST_STREAMING;
    return s;
}

// The codec's sticky flags read (which clears them and its INT1), and kept
// for the next event and the next status.
static void read_sticky(wb_dev_t *d) {
    uint8_t v;
    if (d->result != WB_CODEC_OK) return;
    if (wb_codec_read(0, AIC0_STICKY1, &v)) {
        v &= AIC0_STICKY1_MASK;
        d->ovf |= v;
        d->ovf_status |= v;
    }
    if (wb_codec_read(0, AIC0_STICKY2, &v)) {
        v &= AIC0_STICKY2_OC;
        d->oc |= v;
        d->oc_status |= v;
    }
}

// A page 0 register for a status reply: its value, 0 if it couldn't be read.
static uint8_t reg0_or_0(const wb_dev_t *d, uint8_t reg) {
    uint8_t v = 0;
    if (d->result == WB_CODEC_NO_ANSWER || !wb_codec_read(0, reg, &v)) v = 0;
    return v;
}

// Copy a reply into buf, cut to max bytes; its length.
static int reply(uint8_t *buf, uint16_t max, const void *src, uint16_t n) {
    if (n > max) n = max;
    memcpy(buf, src, n);
    return n;
}

int wb_dev_vendor_in(wb_dev_t *d, uint8_t req, uint16_t value, uint16_t index, uint8_t *buf, uint16_t max) {
    switch (req) {
    case WP_REQ_GET_INFO: {
        wp_info_t i = {WP_VERSION, PS_FW_MAJOR, PS_FW_MINOR, PS_FW_PATCH, d->rate->hz, WP_MAX_SAMPLES,
                       wb_dev_state(d), WP_BOARD, d->input, d->loopback, d->tx_gain, d->rx_gain};
        return reply(buf, max, &i, sizeof i);
    }
    case WP_REQ_GET_STATUS: {
        wp_status_t s;
        memset(&s, 0, sizeof s);
        uint16_t peak[3];
        whal_peaks(peak);
        s.peak_hybrid = peak[0];
        s.peak_line = peak[1];
        s.peak_tx = peak[2];
        read_sticky(d);
        s.ovf = d->ovf_status;
        s.oc = d->oc_status;
        d->ovf_status = d->oc_status = 0;
        s.adc_flags = reg0_or_0(d, AIC0_ADC_FLAGS);
        s.dac_flags = reg0_or_0(d, AIC0_DAC_FLAGS);
        s.state = wb_dev_state(d);
        uint16_t under, over;
        uint32_t in, out;
        whal_counters(&under, &over, &in, &out);
        s.underruns = under;
        s.overruns = over;
        s.samples_in = in;
        s.samples_out = out;
        s.resyncs = d->resyncs;
        return reply(buf, max, &s, sizeof s);
    }
    case WP_REQ_CODEC_READ: {
        uint8_t v;
        if (value > 127 || max < 1 || !wb_codec_read((uint8_t)(index >> 8), (uint8_t)value, &v)) return -1;
        buf[0] = v;
        return 1;
    }
    default:
        return -1;
    }
}

bool wb_dev_vendor_out(wb_dev_t *d, uint8_t req, uint16_t value, uint16_t index) {
    switch (req) {
    case WP_REQ_SET_RATE: {
        const wb_rate_t *r = wb_rate(1000u * value);
        if (!r) return false;
        d->rate = r;
        wb_dev_start(d);
        return true;
    }
    case WP_REQ_SET_CARRIER:
        if (value > WP_CARRIER_AUTO) return false;
        d->carrier = (uint8_t)value;
        return true;
    case WP_REQ_SET_TX_GAIN: {
        int16_t g = (int16_t)value;
        if (g < -127 || g > 48) return false;
        d->tx_gain = (int8_t)g;
        return d->result == WB_CODEC_NO_ANSWER || wb_codec_tx_gain(g);
    }
    case WP_REQ_SET_RX_GAIN:
        if (value > AIC1_PGA_MAX) return false;
        d->rx_gain = (uint8_t)value;
        return d->result == WB_CODEC_NO_ANSWER || wb_codec_rx_gain((int16_t)value);
    case WP_REQ_SET_INPUT:
        if (value > WP_INPUT_LINE) return false;
        d->input = (uint8_t)value;
        whal_set_input(d->input);
        return true;
    case WP_REQ_SET_LOOPBACK:
        if (value > WP_LOOP_CODEC) return false;
        d->loopback = (uint8_t)value;
        whal_set_loopback(value == WP_LOOP_FIRMWARE);
        return d->result == WB_CODEC_NO_ANSWER || wb_codec_loop(value == WP_LOOP_CODEC);
    case WP_REQ_CODEC_WRITE: {
        uint8_t reg = (uint8_t)(value >> 8);
        if (reg == AIC_PAGE || reg > 127) return false;   // the page is the firmware's to select
        return wb_codec_write((uint8_t)(index >> 8), reg, (uint8_t)value);
    }
    case WP_REQ_RESET:
        wb_dev_start(d);
        return true;
    case WP_REQ_REBOOT_UF2:
        whal_reboot_uf2();
        return true;
    default:
        return false;
    }
}

void wb_dev_poll(wb_dev_t *d) {
    uint32_t now = whal_millis();
    if (whal_i2s_lost()) d->resyncs++;
    if ((uint32_t)(now - d->t_clock) >= CLOCK_CHECK_MS) {
        uint32_t f = whal_frames();
        d->clock = f != d->frames_seen;
        d->frames_seen = f;
        d->t_clock = now;
    }
    if (d->result == WB_CODEC_OK && whal_codec_int()) read_sticky(d);
    uint32_t tx = whal_tx_active();
    if (tx != d->tx_seen) {
        d->tx_seen = tx;
        d->t_data = now;
        d->data = true;
    } else if (d->data && (uint32_t)(now - d->t_data) >= DATA_MS) {
        d->data = false;
    }
    whal_leds(carrier(d), d->data);
}

bool wb_dev_event(wb_dev_t *d, wp_event_t *e) {
    uint16_t under, over;
    uint32_t in, out;
    whal_counters(&under, &over, &in, &out);
    memset(e, 0, sizeof *e);
    e->state = wb_dev_state(d);
    e->ovf = d->ovf;
    e->oc = d->oc;
    e->underruns = under;
    e->overruns = over;
    if (d->sent_valid && e->state == d->sent.state && e->ovf == 0 && e->oc == 0 &&
        e->underruns == d->sent.underruns && e->overruns == d->sent.overruns)
        return false;
    e->seq = (uint8_t)(d->sent.seq + 1);
    d->sent = *e;
    d->sent_valid = true;
    d->ovf = d->oc = 0;
    return true;
}
