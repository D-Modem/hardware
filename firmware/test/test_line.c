/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// Host tests for the phone-line stick's portable firmware: the Si3056's
// serial frames (daa_frame.c) against a model of the chip's serial port,
// the sample streams, and the device logic and requests (line_dev.c) on a
// fake board.
#include <stdio.h>
#include <string.h>
#include "../src/core/daa_frame.h"
#include "../src/core/line_dev.h"
#include "../src/core/line_hal.h"
#include "../src/core/si3056.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define TEST(name) static void name(void)
#define RUN(name) do { printf("%s\n", #name); name(); } while (0)

// ---------------------------------------------------------------------------
// A model of the Si3056's serial port (datasheet Figures 27-30): each frame
// it takes a primary word from the RP2040's TX queue (as the PIO would) and
// gives the line's sample; if that primary asked for a secondary (its LSB in
// 15-bit mode, the FC pin in 16-bit mode), it takes the next TX word as a
// control word, carries it out on its registers and answers in the
// secondary word. Writing SB in register 1 switches it to 16-bit mode.
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t reg[128];
    bool fc_pin;
    uint16_t txq[16];          // the PIO's TX FIFO (words in send order)
    int txn;
    uint16_t sent[4096];       // primary words received, in order
    int nsent;
    int16_t line;              // the next sample from the line (counts up)
    int secondaries;
} chip_t;

static chip_t chip;

// The next TX word, as the PIO pulls it: 0 when the FIFO is empty.
static uint16_t chip_pull(void) {
    if (!chip.txn) return 0;
    uint16_t w = chip.txq[0];
    memmove(chip.txq, chip.txq + 1, (size_t)(chip.txn - 1) * sizeof chip.txq[0]);
    chip.txn--;
    return w;
}

// What the RP2040 does with a step: TX words to the PIO's FIFO, the FC pin.
static void apply(const daa_step_t *st) {
    for (int i = 0; i < st->n_tx; i++) chip.txq[chip.txn++] = st->tx[i];
    if (st->fc >= 0) chip.fc_pin = st->fc == 1;
}

// One frame through the model, its words handed to line_stream_word().
static void chip_frame(line_stream_t *ls) {
    daa_step_t st;
    bool sb = chip.reg[SI_CONTROL1] & SI_CONTROL1_SB;
    uint16_t p = chip_pull();
    bool want = sb ? chip.fc_pin : (p & 1u) != 0;
    if (chip.nsent < 4096) chip.sent[chip.nsent++] = p;
    line_stream_word(ls, (uint16_t)chip.line++, &st);
    apply(&st);
    if (!want) return;
    uint16_t c = chip_pull();
    unsigned reg = (c >> 8) & 0x7Fu;
    uint16_t answer = 0;
    if (c & 0x8000u) answer = chip.reg[reg];
    else chip.reg[reg] = (uint8_t)c;
    chip.secondaries++;
    line_stream_word(ls, answer, &st);
    apply(&st);
}

// A fresh chip and streams; the PIO's FIFO primed with one silent word.
static void chip_reset(line_stream_t *ls, uint32_t prefill) {
    memset(&chip, 0, sizeof chip);
    chip.reg[SI_REV] = 0x15;   // Si3018, revision E
    line_stream_init(ls, prefill);
    chip.txq[chip.txn++] = 0;
}

// Queue a control word and run frames until it's done (at most 10).
static bool chip_access(line_stream_t *ls, uint16_t word, uint8_t *result) {
    if (!daa_frame_queue(&ls->fr, word)) return false;
    for (int i = 0; i < 10 && ls->fr.op_state != DAA_OP_DONE; i++) chip_frame(ls);
    if (ls->fr.op_state != DAA_OP_DONE) return false;
    if (result) *result = ls->fr.op_result;
    ls->fr.op_state = DAA_OP_IDLE;
    return true;
}

// The control words are those of Figures 29 and 30.
TEST(control_words) {
    CHECK(si_ctl_read(11) == 0x8B00);
    CHECK(si_ctl_write(7, 5) == 0x0705);
    CHECK(si_ctl_write(0x85, 0x1FF) == 0x05FF);   // 7-bit address, 8-bit value
}

// 15-bit mode (after reset): a read goes out as the LSB flag on a primary,
// and its answer comes back in that frame's secondary; other primaries keep
// the LSB clear so no secondary is asked for by accident.
TEST(read_in_15_bit_mode) {
    line_stream_t ls;
    chip_reset(&ls, 0);
    for (int i = 0; i < 5; i++) chip_frame(&ls);
    uint8_t v = 0;
    CHECK(chip_access(&ls, si_ctl_read(SI_REV), &v));
    CHECK(v == 0x15);
    CHECK(chip.secondaries == 1);
    for (int i = 0; i < 5; i++) chip_frame(&ls);
    CHECK(chip.secondaries == 1);
    int flagged = 0;
    for (int i = 0; i < chip.nsent; i++) flagged += chip.sent[i] & 1;
    CHECK(flagged == 1);
}

// 15-bit mode with the modem's samples flowing: an odd sample loses its LSB,
// so it never asks for a secondary by accident.
TEST(odd_samples_in_15_bit_mode) {
    line_stream_t ls;
    chip_reset(&ls, 0);
    sstream_set(&ls.s, true);
    int16_t odd[16];
    for (int i = 0; i < 16; i++) odd[i] = (int16_t)(2 * i + 1);
    sstream_from_host(&ls.s, odd, 16);
    for (int i = 0; i < 20; i++) chip_frame(&ls);
    CHECK(chip.secondaries == 0);
    bool saw = false;
    for (int i = 0; i < chip.nsent; i++) saw |= chip.sent[i] == 2;   // 3 sent as 2
    CHECK(saw);
}

// Writing SB switches both sides to 16-bit mode; from then on the FC pin
// asks for secondaries (raised before the primary, dropped after the
// secondary), and the primaries carry all 16 bits.
TEST(switch_to_16_bit_mode) {
    line_stream_t ls;
    chip_reset(&ls, 0);
    chip_frame(&ls);
    CHECK(chip_access(&ls, si_ctl_write(SI_CONTROL1, SI_CONTROL1_SB), NULL));
    CHECK(chip.reg[SI_CONTROL1] == SI_CONTROL1_SB);
    ls.fr.fc_hw = true;
    for (int i = 0; i < 3; i++) chip_frame(&ls);
    CHECK(!chip.fc_pin);
    CHECK(chip_access(&ls, si_ctl_write(SI_SRC, 5), NULL));
    CHECK(chip.reg[SI_SRC] == 5);
    CHECK(!chip.fc_pin);
    uint8_t v = 0;
    CHECK(chip_access(&ls, si_ctl_read(SI_SRC), &v));
    CHECK(v == 5);
    CHECK(chip.secondaries == 3);
    // a modem sample with its LSB set goes out whole
    sstream_set(&ls.s, true);
    int16_t odd[4] = {3, 5, 7, 9};
    sstream_from_host(&ls.s, odd, 4);
    int before = chip.nsent;
    for (int i = 0; i < 6; i++) chip_frame(&ls);
    bool saw = false;
    for (int i = before; i < chip.nsent; i++) saw |= chip.sent[i] == 3;
    CHECK(saw);
    CHECK(chip.secondaries == 3);
}

// Accesses one after another all complete, each in a few frames, and a
// second can't be queued while one is under way.
TEST(back_to_back_accesses) {
    line_stream_t ls;
    chip_reset(&ls, 0);
    ls.fr.fc_hw = true;
    chip.reg[SI_CONTROL1] = SI_CONTROL1_SB;
    for (unsigned r = 20; r < 30; r++) CHECK(chip_access(&ls, si_ctl_write(r, r * 3), NULL));
    for (unsigned r = 20; r < 30; r++) {
        uint8_t v = 0;
        CHECK(chip_access(&ls, si_ctl_read(r), &v));
        CHECK(v == r * 3);
    }
    CHECK(daa_frame_queue(&ls.fr, si_ctl_read(1)));
    CHECK(!daa_frame_queue(&ls.fr, si_ctl_read(2)));
}

// Not streaming: the line's samples are dropped and silence goes out.
// Streaming: every frame's sample reaches the host in order.
TEST(samples_to_host) {
    line_stream_t ls;
    chip_reset(&ls, 0);
    for (int i = 0; i < 10; i++) chip_frame(&ls);
    CHECK(sfifo_count(&ls.s.to_host) == 0);
    sstream_set(&ls.s, true);
    int16_t first = chip.line;
    for (int i = 0; i < 50; i++) chip_frame(&ls);
    int16_t got[64];
    uint32_t n = sstream_to_host(&ls.s, got, 64);
    CHECK(n == 50);
    bool ordered = true;
    for (uint32_t i = 0; i < n; i++) ordered &= got[i] == (int16_t)(first + (int16_t)i);
    CHECK(ordered);
    CHECK(ls.s.samples_in == 50);
}

// The modem's samples wait for their head start (prefill), then go out in
// order, each exactly once, even with control accesses in between (a
// secondary word doesn't take a sample).
TEST(samples_from_host_with_accesses) {
    line_stream_t ls;
    chip_reset(&ls, 8);
    ls.fr.fc_hw = true;
    chip.reg[SI_CONTROL1] = SI_CONTROL1_SB;
    sstream_set(&ls.s, true);
    int16_t s[40];
    for (int i = 0; i < 40; i++) s[i] = (int16_t)(1000 + i);
    sstream_from_host(&ls.s, s, 4);
    for (int i = 0; i < 3; i++) chip_frame(&ls);
    CHECK(ls.s.samples_out == 0);   // 4 < 8: still filling
    sstream_from_host(&ls.s, s + 4, 36);
    int from = chip.nsent;
    CHECK(chip_access(&ls, si_ctl_write(30, 1), NULL));
    for (int i = 0; i < 10; i++) chip_frame(&ls);
    CHECK(chip_access(&ls, si_ctl_read(30), NULL));
    for (int i = 0; i < 40; i++) chip_frame(&ls);
    // the modem's samples appear once each, in order
    int next = 1000, extra = 0;
    for (int i = from; i < chip.nsent; i++) {
        int16_t w = (int16_t)chip.sent[i];
        if (w == next) next++;
        else if (w != 0) extra++;
    }
    CHECK(next == 1040);
    CHECK(extra == 0);
    CHECK(ls.s.samples_out == 40);
}

// Running dry counts an underrun and waits for the head start again; a host
// too far ahead, or one not reading, counts overruns.
TEST(under_and_overruns) {
    line_stream_t ls;
    chip_reset(&ls, 4);
    sstream_set(&ls.s, true);
    int16_t s[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    sstream_from_host(&ls.s, s, 4);
    for (int i = 0; i < 6; i++) chip_frame(&ls);
    CHECK(ls.s.underruns == 1);
    CHECK(!ls.s.playing);
    sstream_from_host(&ls.s, s, 2);
    for (int i = 0; i < 4; i++) chip_frame(&ls);
    CHECK(ls.s.underruns == 1);    // waiting for its head start, not running dry
    static int16_t big[SFIFO_SIZE + 10];
    sstream_set(&ls.s, true);
    sstream_from_host(&ls.s, big, SFIFO_SIZE + 10);
    CHECK(ls.s.overruns == 10);
    for (unsigned i = 0; i < SFIFO_SIZE + 5; i++) chip_frame(&ls);
    CHECK(ls.s.overruns > 10);     // nobody read the line's samples
}

// ---------------------------------------------------------------------------
// The device logic on a fake board
// ---------------------------------------------------------------------------
static struct {
    uint32_t now;
    uint8_t reg[128];
    bool hook, ring, intr, streaming, start_ok;
    int start_src, starts, reboots;
    uint8_t edges;
    bool led_oh, led_ri;
} fb;

uint32_t lhal_millis(void) { return fb.now; }
bool lhal_daa_start(int src) { fb.starts++; fb.start_src = src; fb.reg[SI_SRC] = (uint8_t)src; return fb.start_ok; }
bool lhal_daa_read(uint8_t reg, uint8_t *v) { *v = fb.reg[reg & 0x7F]; return true; }
bool lhal_daa_write(uint8_t reg, uint8_t v) { fb.reg[reg & 0x7F] = v; return true; }
void lhal_hook(bool off) { fb.hook = off; }
bool lhal_ring(void) { return fb.ring; }
uint8_t lhal_ring_edges(void) { return fb.edges; }
bool lhal_int(void) { return fb.intr; }
bool lhal_streaming(void) { return fb.streaming; }
// The fake streams' counters: fixed values, to see them carried through.
void lhal_counters(uint16_t *u, uint16_t *o, uint32_t *in, uint32_t *out) { *u = 2; *o = 3; *in = 400; *out = 300; }
void lhal_leds(bool oh, bool ri) { fb.led_oh = oh; fb.led_ri = ri; }
void lhal_reboot_uf2(void) { fb.reboots++; }

static line_dev_t d;

// A fresh fake board whose DAA comes up, with an Si3018 rev E on it.
static void dev_setup(void) {
    memset(&fb, 0, sizeof fb);
    fb.now = 1000;
    fb.start_ok = true;
    fb.reg[SI_REV] = 0x15;
    fb.reg[SI_REVB] = 0x14;
    fb.reg[SI_STATUS] = SI_STATUS_FDT;
    CHECK(line_dev_init(&d, 9600));
    CHECK(d.link);
}

// Frames running but the line side silent: no link yet, and its IDs unread;
// once FDT sets, the poll a second later finds it and reads the IDs.
TEST(dev_link_comes_up_late) {
    memset(&fb, 0, sizeof fb);
    fb.now = 1000;
    fb.start_ok = true;
    fb.reg[SI_REV] = 0x15;
    CHECK(line_dev_init(&d, 9600));
    CHECK(!d.link && d.reg11 == 0);
    CHECK(fb.reg[SI_DAA1] == SI_DAA1_OHE);   // the system side is set up all the same
    fb.reg[SI_STATUS] = SI_STATUS_FDT;
    fb.now += 500;
    line_dev_poll(&d);
    CHECK(!d.link);
    fb.now += 500;
    line_dev_poll(&d);
    CHECK(d.link && d.reg11 == 0x15);
    CHECK(line_dev_state(&d) & LP_ST_LINK);
}

// Init brings the DAA up at 9600 Hz on hook, turns the interrupt pin on and
// the OFHK pin on, and reports it all in GET_INFO.
TEST(dev_init_and_info) {
    dev_setup();
    CHECK(fb.start_src == 5);
    CHECK(!fb.hook);
    CHECK(fb.reg[SI_CONTROL2] == (SI_CONTROL2_INTE | SI_CONTROL2_HBE | SI_CONTROL2_RXE));
    CHECK(fb.reg[SI_DAA1] == SI_DAA1_OHE);
    CHECK(fb.reg[SI_INT_MASK] & SI_INT_RDT);
    uint8_t buf[64];
    CHECK(line_dev_vendor_in(&d, LP_REQ_GET_INFO, 0, buf, sizeof buf) == (int)sizeof(lp_info_t));
    lp_info_t i;
    memcpy(&i, buf, sizeof i);
    CHECK(i.version == LP_VERSION && i.rate == 9600 && i.max_samples == LP_MAX_SAMPLES);
    CHECK(i.reg11 == 0x15 && i.reg13 == 0x14);
    CHECK(i.state == LP_ST_LINK);
    CHECK(line_dev_vendor_in(&d, LP_REQ_GET_INFO, 0, buf, 4) == 4);   // short wLength: cut
    // a DAA that doesn't come up: no link, but the requests still answer
    fb.start_ok = false;
    CHECK(!line_dev_init(&d, 8000));
    CHECK(line_dev_vendor_in(&d, LP_REQ_GET_INFO, 0, buf, sizeof buf) == (int)sizeof(lp_info_t));
    memcpy(&i, buf, sizeof i);
    CHECK(i.state == 0 && i.rate == 8000);
    CHECK(!line_dev_init(&d, 12345));   // not a rate it has: 9600
    CHECK(d.rate == 9600);
}

// SET_RATE takes the Si3056's rates only, and writes SRC.
TEST(dev_rate) {
    dev_setup();
    CHECK(line_dev_vendor_out(&d, LP_REQ_SET_RATE, 16000));
    CHECK(fb.reg[SI_SRC] == 9 && d.rate == 16000);
    CHECK(line_dev_vendor_out(&d, LP_REQ_SET_RATE, 8000));
    CHECK(fb.reg[SI_SRC] == 1);
    CHECK(!line_dev_vendor_out(&d, LP_REQ_SET_RATE, 44100));
    CHECK(d.rate == 8000);
}

// The hook follows SET_HOOK; a flash goes on hook for its time, then back.
TEST(dev_hook_and_flash) {
    dev_setup();
    CHECK(line_dev_vendor_out(&d, LP_REQ_SET_HOOK, 1));
    CHECK(fb.hook);
    line_dev_poll(&d);
    CHECK(fb.led_oh);
    CHECK(!line_dev_vendor_out(&d, LP_REQ_SET_HOOK, 2));
    CHECK(line_dev_vendor_out(&d, LP_REQ_HOOK_FLASH, 100));
    CHECK(!fb.hook);
    CHECK(line_dev_state(&d) & LP_ST_FLASH);
    fb.now += 99;
    line_dev_poll(&d);
    CHECK(!fb.hook);
    fb.now += 1;
    line_dev_poll(&d);
    CHECK(fb.hook);
    CHECK(line_dev_state(&d) & LP_ST_OFF_HOOK);
    CHECK(!line_dev_vendor_out(&d, LP_REQ_HOOK_FLASH, 0));
    CHECK(!line_dev_vendor_out(&d, LP_REQ_HOOK_FLASH, 5000));
    CHECK(line_dev_vendor_out(&d, LP_REQ_SET_HOOK, 0));
    CHECK(!fb.hook);
    CHECK(line_dev_vendor_out(&d, LP_REQ_HOOK_FLASH, 100));   // on hook: nothing to do
    CHECK(!fb.hook && !(line_dev_state(&d) & LP_ST_FLASH));
}

// The line monitor is ONHM; a raw write of register 5 keeps the OFHK pin
// enabled and moves the monitor with its ONHM bit.
TEST(dev_monitor_and_register_access) {
    dev_setup();
    CHECK(line_dev_vendor_out(&d, LP_REQ_SET_MONITOR, 1));
    CHECK(fb.reg[SI_DAA1] == (SI_DAA1_OHE | SI_DAA1_ONHM));
    CHECK(line_dev_vendor_out(&d, LP_REQ_DAA_WRITE, SI_DAA1 << 8 | 0x00));
    CHECK(fb.reg[SI_DAA1] == SI_DAA1_OHE && !d.monitor);
    CHECK(line_dev_vendor_out(&d, LP_REQ_DAA_WRITE, 16 << 8 | 0x42));
    CHECK(fb.reg[16] == 0x42);
    uint8_t b[4];
    CHECK(line_dev_vendor_in(&d, LP_REQ_DAA_READ, 16, b, sizeof b) == 1 && b[0] == 0x42);
    CHECK(line_dev_vendor_in(&d, LP_REQ_DAA_READ, 200, b, sizeof b) == -1);
    CHECK(!line_dev_vendor_out(&d, LP_REQ_DAA_WRITE, 0x80 << 8));
    CHECK(line_dev_vendor_in(&d, 0x7F, 0, b, sizeof b) == -1);
    CHECK(!line_dev_vendor_out(&d, 0x7F, 0));
    CHECK(line_dev_vendor_out(&d, LP_REQ_REBOOT_UF2, 0) && fb.reboots == 1);
    CHECK(line_dev_vendor_out(&d, LP_REQ_RESET, 0) && fb.starts == 2);
}

// The DAA's interrupt is read out of register 4 and cleared; its bits show
// in the next event and the next status, once each.
TEST(dev_interrupts_and_status) {
    dev_setup();
    fb.reg[SI_INT_SOURCE] = SI_INT_RDT | SI_INT_POL;
    fb.intr = true;
    line_dev_poll(&d);
    fb.intr = false;
    CHECK(fb.reg[SI_INT_SOURCE] == 0);
    lp_event_t e;
    CHECK(line_dev_event(&d, &e));
    CHECK(e.int_src == (SI_INT_RDT | SI_INT_POL));
    CHECK(e.underruns == 2 && e.overruns == 3);
    uint8_t buf[32];
    fb.reg[SI_STATUS] = SI_STATUS_FDT | 0x0A;
    fb.reg[SI_LVS] = 48;
    CHECK(line_dev_vendor_in(&d, LP_REQ_GET_STATUS, 0, buf, sizeof buf) == (int)sizeof(lp_status_t));
    lp_status_t s;
    memcpy(&s, buf, sizeof s);
    CHECK(s.reg12 == (SI_STATUS_FDT | 0x0A) && s.reg29 == 48);
    CHECK(s.int_src == (SI_INT_RDT | SI_INT_POL));
    CHECK(s.samples_in == 400 && s.samples_out == 300);
    CHECK(line_dev_vendor_in(&d, LP_REQ_GET_STATUS, 0, buf, sizeof buf) > 0);
    memcpy(&s, buf, sizeof s);
    CHECK(s.int_src == 0);
}

// Events go out when something changes, each with the next seq, and not
// otherwise: the ring detector's edges, the hook, the stream.
TEST(dev_events) {
    dev_setup();
    lp_event_t e;
    CHECK(line_dev_event(&d, &e));
    uint8_t seq = e.seq;
    CHECK(!line_dev_event(&d, &e));
    fb.ring = true;
    fb.edges = 1;
    CHECK(line_dev_event(&d, &e));
    CHECK(e.seq == (uint8_t)(seq + 1) && e.ring_edges == 1 && (e.state & LP_ST_RINGING));
    line_dev_poll(&d);
    CHECK(fb.led_ri);
    fb.edges = 2;
    CHECK(line_dev_event(&d, &e) && e.ring_edges == 2);
    CHECK(!line_dev_event(&d, &e));
    fb.streaming = true;
    CHECK(line_dev_event(&d, &e) && (e.state & LP_ST_STREAMING));
    CHECK(line_dev_vendor_out(&d, LP_REQ_SET_HOOK, 1));
    CHECK(line_dev_event(&d, &e) && (e.state & LP_ST_OFF_HOOK));
    CHECK(!line_dev_event(&d, &e));
}

// Runs every test; exit status 1 if any check failed.
int main(void) {
    RUN(control_words);
    RUN(read_in_15_bit_mode);
    RUN(odd_samples_in_15_bit_mode);
    RUN(switch_to_16_bit_mode);
    RUN(back_to_back_accesses);
    RUN(samples_to_host);
    RUN(samples_from_host_with_accesses);
    RUN(under_and_overruns);
    RUN(dev_init_and_info);
    RUN(dev_link_comes_up_late);
    RUN(dev_rate);
    RUN(dev_hook_and_flash);
    RUN(dev_monitor_and_register_access);
    RUN(dev_interrupts_and_status);
    RUN(dev_events);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
