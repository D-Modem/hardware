/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// Host tests for the wideband stick's portable firmware: the codec's
// bring-up (wb_codec.c) against a model of the TLV320AIC3204's registers,
// its clocks re-derived from the registers and checked against TI SLAA557's
// limits, the hybrid's routing against the board's netlist, the samples
// between the I2S frames and the streams (wb_audio.c), and the device logic
// and requests (wb_dev.c) on a fake board.
#include <stdio.h>
#include <string.h>
#include "../src/core/aic3204.h"
#include "../src/core/wb_audio.h"
#include "../src/core/wb_codec.h"
#include "../src/core/wb_dev.h"
#include "../src/core/wb_hal.h"
#include "../src/core/wb_proto.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define TEST(name) static void name(void)
#define RUN(name) do { printf("%s\n", #name); name(); } while (0)

// ---------------------------------------------------------------------------
// A model of the codec's registers (pages 0 and 1, SLAA557 chapter 5): page
// select in register 0, the software reset, the reset values the bring-up
// must change, read-to-clear sticky flags, and the power-up flags following
// what was powered (the headphone drivers' after hp_delay reads).
// ---------------------------------------------------------------------------
#define LOG_MAX 512
typedef struct { uint8_t page, reg, v; uint32_t t; } wr_t;

static struct {
    uint8_t reg[2][128];
    uint8_t page;
    bool answers;              // I2C: the codec acks
    bool hp_never;             // the drivers never reach their gain
    bool pga_never;            // nor the MicPGAs theirs
    int hp_delay, hp_reads;    // reads of P1 R63 before its flags show
    wr_t log[LOG_MAX];
    int nlog;
    int bad_page_writes;       // writes to a page other than 0 and 1
    uint32_t t_reset;          // when RESET last rose (ms)
    int early;                 // accesses within 1 ms of a reset
} aic;

// Reset values (SLAA557 chapter 5) of the registers the tests look at.
static void aic_defaults(void) {
    memset(aic.reg, 0, sizeof aic.reg);
    aic.reg[0][AIC0_PLL_PR] = 0x11;
    aic.reg[0][AIC0_PLL_J] = 0x04;
    aic.reg[0][AIC0_NDAC] = aic.reg[0][AIC0_MDAC] = aic.reg[0][AIC0_NADC] = aic.reg[0][AIC0_MADC] = 0x01;
    aic.reg[0][AIC0_DOSR_LSB] = 0x80;
    aic.reg[0][AIC0_AOSR] = 0x80;
    aic.reg[0][AIC0_BCLK_N] = 0x01;
    aic.reg[0][AIC0_DAC_PRB] = aic.reg[0][AIC0_ADC_PRB] = 0x01;
    aic.reg[0][AIC0_DAC_SETUP1] = 0x14;
    aic.reg[0][AIC0_DAC_SETUP2] = 0x0C;
    aic.reg[0][AIC0_ADC_FINE] = 0x88;
    aic.reg[1][AIC1_LDO] = AIC1_LDO_ANALOG_OFF;
    aic.reg[1][AIC1_HPL_GAIN] = aic.reg[1][AIC1_HPR_GAIN] = 0x40;
    aic.reg[1][AIC1_LPGA_VOL] = aic.reg[1][AIC1_RPGA_VOL] = 0x80;
    aic.reg[1][11] = 0x10;
    aic.page = 0;
    aic.hp_reads = 0;
}

// What the flags registers say now, from what is powered.
static void aic_flags(void) {
    uint8_t out = aic.reg[1][AIC1_OUT_POWER], dac = aic.reg[0][AIC0_DAC_SETUP1], adc = aic.reg[0][AIC0_ADC_SETUP];
    aic.reg[0][AIC0_DAC_FLAGS] = (uint8_t)((dac & 0x80 ? 0x80 : 0) | (dac & 0x40 ? 0x08 : 0) |
                                           (out & AIC1_OUT_HPL ? 0x20 : 0) | (out & AIC1_OUT_HPR ? 0x02 : 0));
    uint8_t pga = aic.pga_never ? 0x44 : 0xCC;
    aic.reg[0][AIC0_ADC_FLAGS] = (uint8_t)(((adc & 0x80 ? 0xC0 : 0) | (adc & 0x40 ? 0x0C : 0)) & pga);
}

static struct {
    uint32_t now;
    bool i2s_on, clock_on;     // the I2S slave running; the codec's clocks running
    uint32_t i2s_rate, frames, frame_frac;
    int starts, stops, resets, reboots;
    bool lost, streaming, intr;
    uint8_t input;
    bool loopback;
    uint16_t peaks[3];
    uint32_t tx_active;
    bool led_link, led_data;
} fb;

uint32_t whal_millis(void) { return fb.now; }

// Time passes: the codec's frames arrive while its clocks and the slave run.
void whal_sleep_ms(uint32_t ms) {
    fb.now += ms;
    if (fb.i2s_on && fb.clock_on) fb.frames += ms * fb.i2s_rate / 1000;
}

void whal_codec_reset(void) {
    fb.resets++;
    aic_defaults();
    aic.t_reset = fb.now;
}

// An access too soon after reset (SLAA557 3.2) is counted.
static void aic_access(void) {
    if (fb.resets && fb.now - aic.t_reset < 1) aic.early++;
}

bool whal_codec_write(uint8_t reg, uint8_t v) {
    if (!aic.answers) return false;
    aic_access();
    if (reg == AIC_PAGE) {
        aic.page = v;
    } else if (aic.page > 1) {
        aic.bad_page_writes++;
    } else if (aic.page == 0 && reg == AIC0_RESET && (v & 1)) {
        aic_defaults();
    } else {
        aic.reg[aic.page][reg & 0x7F] = v;
    }
    if (aic.nlog < LOG_MAX) aic.log[aic.nlog++] = (wr_t){aic.page, reg, v, fb.now};
    if (aic.page == 1 && reg == AIC1_OUT_POWER) aic.hp_reads = 0;
    aic_flags();
    return true;
}

bool whal_codec_read(uint8_t reg, uint8_t *v) {
    if (!aic.answers) return false;
    aic_access();
    if (reg == AIC_PAGE) {
        *v = aic.page;
        return true;
    }
    uint8_t p = aic.page > 1 ? 1 : aic.page;
    if (p == 1 && reg == AIC1_DAC_GAIN_FLAGS) {
        uint8_t hp = aic.reg[1][AIC1_OUT_POWER] & (AIC1_OUT_HPL | AIC1_OUT_HPR);
        bool ready = hp && !aic.hp_never && aic.hp_reads++ >= aic.hp_delay;
        aic.reg[1][reg] = ready ? (uint8_t)((hp & AIC1_OUT_HPL ? 0x80 : 0) | (hp & AIC1_OUT_HPR ? 0x40 : 0)) : 0;
    }
    *v = aic.reg[p][reg & 0x7F];
    if (p == 0 && (reg == AIC0_STICKY1 || reg == AIC0_STICKY2)) aic.reg[0][reg] = 0;   // read to clear
    return true;
}

bool whal_codec_int(void) { return fb.intr; }
void whal_i2s_start(uint32_t rate) { fb.i2s_on = true; fb.i2s_rate = rate; fb.starts++; fb.frames = 0; }
void whal_i2s_stop(void) { fb.i2s_on = false; fb.stops++; }

bool whal_i2s_lost(void) {
    bool l = fb.lost;
    fb.lost = false;
    return l;
}

uint32_t whal_frames(void) { return fb.frames; }
bool whal_streaming(void) { return fb.streaming; }
// The fake streams' counters: fixed values, to see them carried through.
void whal_counters(uint16_t *u, uint16_t *o, uint32_t *in, uint32_t *out) { *u = 2; *o = 3; *in = 4000; *out = 3000; }

void whal_peaks(uint16_t peak[3]) {
    memcpy(peak, fb.peaks, sizeof fb.peaks);
    memset(fb.peaks, 0, sizeof fb.peaks);
}

uint32_t whal_tx_active(void) { return fb.tx_active; }
void whal_set_input(uint8_t input) { fb.input = input; }
void whal_set_loopback(bool on) { fb.loopback = on; }
void whal_leds(bool link, bool data) { fb.led_link = link; fb.led_data = data; }
void whal_reboot_uf2(void) { fb.reboots++; }

// A fresh fake board with a codec that answers and runs its clocks.
static void board(void) {
    memset(&fb, 0, sizeof fb);
    memset(&aic, 0, sizeof aic);
    aic_defaults();
    aic.answers = true;
    aic.hp_delay = 5;
    fb.now = 1000;
    fb.clock_on = true;
}

// The index in the write log of the first write to (page, reg) with
// (v & mask) == want, from index `from`; -1 if none.
static int find(int from, uint8_t page, uint8_t reg, uint8_t mask, uint8_t want) {
    for (int i = from; i < aic.nlog; i++)
        if (aic.log[i].page == page && aic.log[i].reg == reg && (aic.log[i].v & mask) == want) return i;
    return -1;
}

// The index of the last write to (page, reg); -1 if none.
static int last(uint8_t page, uint8_t reg) {
    for (int i = aic.nlog - 1; i >= 0; i--)
        if (aic.log[i].page == page && aic.log[i].reg == reg) return i;
    return -1;
}

// A divider's value from its register (D6:0, 0 means 128).
static unsigned div7(uint8_t v) { return (v & 0x7F) ? (v & 0x7Fu) : 128u; }

// SLAA557 Table 2-16 and Table 2-7: the processing blocks' resource class
// and interpolation/decimation filter (A, B, C).
static const unsigned prb_p_rc[26] = {0, 8, 12, 10, 4, 6, 6, 6, 8, 8, 10, 8, 3, 4, 4, 6, 4, 3, 6, 4, 2, 3, 2, 8, 12, 12};
static const char prb_p_filter[26] = "-AAAAAABBBBBBBBBBCCCCCCAAA";
static const unsigned prb_r_rc[19] = {0, 6, 8, 8, 3, 4, 4, 3, 4, 4, 2, 2, 2, 3, 4, 4, 2, 2, 2};
static const char prb_r_filter[19] = "-AAAAAABBBBBBCCCCCC";

// The clocks the registers give, for one rate, against SLAA557's limits
// (sections 2.3.3.3, 2.4.6, 2.7, Tables 2-25 and 2-26).
static void check_clocks(uint32_t hz) {
    const uint8_t *p0 = aic.reg[0];
    // the PLL: from MCLK, the codec's clock; P (0 means 8), R, J.D
    CHECK((p0[AIC0_CLK_MUX] & 0x0C) == 0x00);   // PLL_CLKIN = MCLK
    CHECK((p0[AIC0_CLK_MUX] & 0x03) == 0x03);   // CODEC_CLKIN = PLL_CLK
    CHECK((p0[AIC0_CLK_MUX] & 0x40) == 0);      // the low PLL range
    CHECK(p0[AIC0_PLL_PR] & AIC0_PLL_ON);
    unsigned P = (p0[AIC0_PLL_PR] >> 4) & 7, R = p0[AIC0_PLL_PR] & 0x0F, J = p0[AIC0_PLL_J] & 0x3F;
    unsigned D = (unsigned)(p0[AIC0_PLL_D_MSB] & 0x3F) << 8 | p0[AIC0_PLL_D_LSB];
    if (P == 0) P = 8;
    CHECK(P == 1 && R == 1 && J == 8 && D == 1920);
    CHECK(R >= 1 && R <= 4 && J >= 4 && J <= 63 && D <= 9999);
    uint64_t pll_clkin = WB_MCLK_HZ;
    CHECK(pll_clkin / P >= 10000000 && pll_clkin / P <= 20000000);   // D != 0: 10..20 MHz (eq. 20)
    uint64_t pll = pll_clkin * R * (J * 10000u + D) / (P * 10000u);
    CHECK(pll * P * 10000u == pll_clkin * R * (J * 10000u + D));     // exact
    CHECK(pll == 98304000u);
    CHECK(pll >= 80000000u && pll <= 118000000u);   // Table 2-26: AVDD >= 1.65 V, low range
    // the dividers, powered, and the rate they give
    for (int r = AIC0_NDAC; r <= AIC0_MDAC; r++) CHECK(p0[r] & AIC0_DIV_ON);
    CHECK((p0[AIC0_NADC] & AIC0_DIV_ON) && (p0[AIC0_MADC] & AIC0_DIV_ON) && (p0[AIC0_BCLK_N] & AIC0_DIV_ON));
    unsigned ndac = div7(p0[AIC0_NDAC]), mdac = div7(p0[AIC0_MDAC]), nadc = div7(p0[AIC0_NADC]),
             madc = div7(p0[AIC0_MADC]), bn = div7(p0[AIC0_BCLK_N]);
    unsigned dosr = (unsigned)(p0[AIC0_DOSR_MSB] & 3) << 8 | p0[AIC0_DOSR_LSB], aosr = p0[AIC0_AOSR];
    if (dosr == 0) dosr = 1024;
    if (aosr == 0) aosr = 256;
    CHECK(pll % ((uint64_t)ndac * mdac * dosr) == 0 && pll / ((uint64_t)ndac * mdac * dosr) == hz);   // DAC_FS
    CHECK(pll % ((uint64_t)nadc * madc * aosr) == 0 && pll / ((uint64_t)nadc * madc * aosr) == hz);   // ADC_FS
    CHECK(pll / ndac / bn == (uint64_t)WB_BCLK_PER_FRAME * hz);                                     // BCLK
    // Table 2-25 (DVDD >= 1.65 V)
    CHECK(pll <= (ndac % 2 == 0 && nadc % 2 == 0 ? 137000000u : 110000000u));   // CODEC_CLKIN
    CHECK(pll / ndac <= 55296000u && pll / nadc <= 55296000u);                  // DAC_CLK, ADC_CLK, BDIV_CLKIN
    CHECK(pll / ndac / mdac <= 6758000u && pll / nadc / madc <= 6758000u);      // the modulators' clocks
    CHECK(hz <= 192000u);
    // DOSR x fs between 2.8 and 6.2 MHz (2.4.6), the blocks' resources met
    CHECK((uint64_t)dosr * hz > 2800000u && (uint64_t)dosr * hz < 6200000u);
    unsigned pp = p0[AIC0_DAC_PRB] & 0x1F, pr = p0[AIC0_ADC_PRB] & 0x1F;
    CHECK(pp >= 1 && pp <= 25 && pr >= 1 && pr <= 18);
    if (pp < 1 || pp > 25 || pr < 1 || pr > 18) return;
    CHECK(mdac * dosr / 32 >= prb_p_rc[pp]);
    CHECK(madc * aosr / 32 >= prb_r_rc[pr]);
    // each block's filter suits its OSR (2.4.6: A multiple of 8, B of 4, C of 2;
    // register 20: A 128 or 64, B 64, C 32)
    char fp = prb_p_filter[pp], fr = prb_r_filter[pr];
    CHECK(dosr % (fp == 'A' ? 8u : fp == 'B' ? 4u : 2u) == 0);
    CHECK(fr == 'A' ? (aosr == 128 || aosr == 64) : fr == 'B' ? aosr == 64 : aosr == 32);
    // SLAA557 2.3.3.3 and 2.4.6: filter A for 48 kHz, B for 96, C for 192
    char want = hz <= 48000 ? 'A' : hz <= 96000 ? 'B' : 'C';
    CHECK(fp == want && fr == want);
    // both stereo blocks (the ADC's two channels are both used)
    CHECK(pr == 1 || pr == 2 || pr == 3 || pr == 7 || pr == 8 || pr == 9 || pr == 13 || pr == 14 || pr == 15);
    // I2S, 32-bit words, the codec the master; BCLK from DAC_CLK
    CHECK(p0[AIC0_IFACE1] == (AIC0_IFACE1_I2S | AIC0_IFACE1_32BIT | AIC0_IFACE1_BCLK_OUT | AIC0_IFACE1_WCLK_OUT));
    CHECK((p0[AIC0_IFACE3] & 0x03) == AIC0_IFACE3_BDIV_DAC_CLK);
}

// Each rate's settings give exactly that rate and a 64-bit frame, within
// every clock limit SLAA557 sets; the spec's 96 kHz numbers (SPEC-WB §4).
TEST(codec_clocks) {
    static const uint32_t hz[] = {48000, 96000, 192000};
    for (int i = 0; i < 3; i++) {
        board();
        const wb_rate_t *r = wb_rate(hz[i]);
        CHECK(r != NULL);
        if (!r) continue;
        wb_codec_cfg_t cfg = {r, 0, 0, false};
        CHECK(wb_codec_start(&cfg) == WB_CODEC_OK);
        check_clocks(hz[i]);
    }
    const wb_rate_t *r = wb_rate(96000);
    CHECK(r->osr == 64 && r->prb_p == 7 && r->prb_r == 7 && r->bclk_n == 4);
    r = wb_rate(192000);
    CHECK(r->osr == 32 && r->prb_p == 17 && r->prb_r == 13 && r->bclk_n == 2);
    CHECK(wb_rate(44100) == NULL && wb_rate(8000) == NULL && wb_rate(0) == NULL);
}

// The board's netlist (hardware/netlist/build_netlist.py, the wb variant):
// each codec input's node, as +1/-1 of the line's voltage (XF_A - XF_B) and
// the replica's (REP_A - REP_B), each node half of its pair's voltage.
typedef struct { const char *pin; int line, replica; } node_t;
static const node_t inputs[] = {
    {"IN1L", +1, 0},   // XF_A: line +
    {"IN1R", 0, -1},   // REP_B: replica -
    {"IN2R", -1, 0},   // XF_B: line -
    {"IN3R", 0, +1},   // REP_A: replica +
};

// The input routed into a MicPGA field: its node's line and replica parts
// added to *line and *rep with the terminal's sign, and its resistance.
static void route(uint8_t reg, int shift, int input, int sign, int *line, int *rep, unsigned *r) {
    unsigned f = (reg >> shift) & 3u;
    if (!f) return;
    *line += sign * inputs[input].line;
    *rep += sign * inputs[input].replica;
    *r = f;
}

// The MicPGAs' routing bits (SLAA557 5.3.26-5.3.31) make the left ADC
// the hybrid, line - replica, through 10 kOhm on every input, and the right
// ADC the line alone (inverted) through 20 kOhm; nothing else is routed and
// the unused inputs sit at the common mode.
TEST(codec_hybrid_routing) {
    board();
    wb_codec_cfg_t cfg = {wb_rate(96000), 0, 24, false};
    CHECK(wb_codec_start(&cfg) == WB_CODEC_OK);
    const uint8_t *p1 = aic.reg[1];
    CHECK(p1[AIC1_LPGA_P] == 0x41);   // IN1L 10k (D7:6 = 01), IN1R 10k (D1:0 = 01)
    CHECK(p1[AIC1_LPGA_M] == 0x14);   // IN2R 10k (D5:4 = 01), IN3R 10k (D3:2 = 01), no CM
    CHECK(p1[AIC1_RPGA_P] == 0x20);   // IN2R 20k (D5:4 = 10)
    CHECK(p1[AIC1_RPGA_M] == 0x20);   // IN1L 20k (D5:4 = 10)
    int line = 0, rep = 0, ln = 0, rp = 0;
    unsigned r = 0, rl[4] = {0, 0, 0, 0};
    // left +: IN1L D7:6, IN1R D1:0 (IN2L D5:4 and IN3L D3:2 must be off)
    route(p1[AIC1_LPGA_P], 6, 0, +1, &line, &rep, &rl[0]);
    route(p1[AIC1_LPGA_P], 0, 1, +1, &line, &rep, &rl[1]);
    CHECK(((p1[AIC1_LPGA_P] >> 2) & 0x0F) == 0);
    // left -: IN2R D5:4, IN3R D3:2 (CM1L D7:6, CM2L D1:0 off)
    route(p1[AIC1_LPGA_M], 4, 2, -1, &line, &rep, &rl[2]);
    route(p1[AIC1_LPGA_M], 2, 3, -1, &line, &rep, &rl[3]);
    CHECK((p1[AIC1_LPGA_M] & 0xC3) == 0);
    CHECK(line == 2 && rep == -2);   // (A - B) of the line minus (A - B) of the replica
    for (int i = 0; i < 4; i++) CHECK(rl[i] == AIC_R10K);
    // right +: IN2R D5:4 (IN1R, IN3R, IN2L off); right -: IN1L D5:4 (CMs, IN3L off)
    route(p1[AIC1_RPGA_P], 4, 2, +1, &ln, &rp, &r);
    CHECK(r == AIC_R20K && (p1[AIC1_RPGA_P] & 0xCF) == 0);
    route(p1[AIC1_RPGA_M], 4, 0, -1, &ln, &rp, &r);
    CHECK(r == AIC_R20K && (p1[AIC1_RPGA_M] & 0xCF) == 0);
    CHECK(ln == -2 && rp == 0);
    CHECK(p1[AIC1_FLOAT] == (AIC1_FLOAT_IN2L | AIC1_FLOAT_IN3L));
    CHECK(p1[AIC1_LPGA_VOL] == 24 && p1[AIC1_RPGA_VOL] == 0);   // gains on (D7 clear)
    // the line drive: HPL = left DAC +, HPR = left DAC -, nothing else; CM 0.9 V,
    // the drivers from AVDD at 0 dB, unmuted and on; the A-LDO at 1.77 V
    CHECK(p1[AIC1_HPL_ROUTE] == AIC1_HPL_LDAC && p1[AIC1_HPR_ROUTE] == AIC1_HPR_LDAC_M);
    CHECK(p1[AIC1_CM] == 0x00);
    CHECK(p1[AIC1_HPL_GAIN] == 0x00 && p1[AIC1_HPR_GAIN] == 0x00);
    CHECK(p1[AIC1_OUT_POWER] == (AIC1_OUT_HPL | AIC1_OUT_HPR));
    CHECK(p1[AIC1_POWER] == AIC1_POWER_NO_WEAK_AVDD);
    CHECK(p1[AIC1_LDO] == (AIC1_LDO_AVDD_1V77 | AIC1_LDO_AVDD_ON));
    // the left DAC on and unmuted, the right one off and muted; both ADCs on and unmuted
    CHECK(aic.reg[0][AIC0_DAC_SETUP1] == 0x90 && aic.reg[0][AIC0_DAC_SETUP2] == AIC0_DAC_MUTE_R);
    CHECK(aic.reg[0][AIC0_ADC_SETUP] == AIC0_ADC_ON && aic.reg[0][AIC0_ADC_FINE] == 0x00);
    CHECK(aic.reg[0][AIC0_MFP5] == AIC0_MFP5_INT1);
    CHECK(aic.bad_page_writes == 0);
}

// The bring-up keeps SLAA557's order: reset first and nothing for 1 ms
// after it; P, R, J, D before the PLL is powered, D's two registers back to
// back (and DOSR's); the dividers once the PLL runs; the weak AVDD link off
// before the LDO; the drivers set up before they are powered; the DAC only
// once the drivers' flags say they're up; the ADCs after their routing.
TEST(codec_sequence) {
    board();
    wb_codec_cfg_t cfg = {wb_rate(96000), -12, 0, false};
    uint32_t t0 = fb.now;
    CHECK(wb_codec_start(&cfg) == WB_CODEC_OK);
    CHECK(fb.resets == 1 && aic.early == 0);
    CHECK(fb.now - t0 >= 12 && fb.now - t0 < 100);   // 1 + 1 + 10 ms and the flags
    int rst = find(0, 0, AIC0_RESET, 0x01, 0x01);
    CHECK(rst == 1 && aic.log[0].reg == AIC_PAGE && aic.log[0].v == 0);   // page 0, then the reset
    int j = find(0, 0, AIC0_PLL_J, 0, 0), dm = find(0, 0, AIC0_PLL_D_MSB, 0, 0), dl = find(0, 0, AIC0_PLL_D_LSB, 0, 0);
    int pll_on = find(0, 0, AIC0_PLL_PR, AIC0_PLL_ON, AIC0_PLL_ON), pr = find(0, 0, AIC0_PLL_PR, AIC0_PLL_ON, 0);
    CHECK(pr > rst && j > rst && dl == dm + 1 && pll_on > dl && pll_on > j && pll_on > pr);
    CHECK(aic.log[find(0, 0, AIC0_NDAC, 0, 0)].t >= aic.log[pll_on].t + 10);   // the PLL's 10 ms
    CHECK(find(0, 0, AIC0_DOSR_LSB, 0, 0) == find(0, 0, AIC0_DOSR_MSB, 0, 0) + 1);
    int weak = find(0, 1, AIC1_POWER, 0, 0), ldo = find(0, 1, AIC1_LDO, AIC1_LDO_AVDD_ON, AIC1_LDO_AVDD_ON);
    CHECK(weak > 0 && ldo > weak);
    int hp_on = find(0, 1, AIC1_OUT_POWER, 0x30, 0x30);
    CHECK(hp_on > 0);
    static const uint8_t before_hp[] = {AIC1_CM, AIC1_HP_START, AIC1_HPL_ROUTE, AIC1_HPR_ROUTE, AIC1_HPL_GAIN,
                                        AIC1_HPR_GAIN, AIC1_REF, AIC1_PLAY1};
    for (unsigned i = 0; i < sizeof before_hp; i++) {
        int w = find(0, 1, before_hp[i], 0, 0);
        CHECK(w > ldo && w < hp_on);
    }
    int dac_on = find(0, 0, AIC0_DAC_SETUP1, 0x80, 0x80), adc_on = find(0, 0, AIC0_ADC_SETUP, 0xC0, 0xC0);
    CHECK(dac_on > hp_on && aic.hp_reads > aic.hp_delay);   // after the drivers' flags were seen
    CHECK(adc_on > last(1, AIC1_LPGA_M) && adc_on > last(1, AIC1_RPGA_M) && adc_on > last(1, AIC1_LPGA_VOL));
    CHECK(dac_on > find(0, 0, AIC0_DAC_PRB, 0, 0) && adc_on > find(0, 0, AIC0_ADC_PRB, 0, 0));
    CHECK(find(0, 0, AIC0_DAC_VOL_L, 0, 0) > 0 && aic.reg[0][AIC0_DAC_VOL_L] == 0xF4);   // -12 x 0.5 dB
    CHECK(aic.reg[0][AIC0_IFACE3] == AIC0_IFACE3_CLKS_ON);
    // the codec loopback is register 29's ADC-to-DAC bit
    board();
    cfg.loop_codec = true;
    CHECK(wb_codec_start(&cfg) == WB_CODEC_OK);
    CHECK(aic.reg[0][AIC0_IFACE3] == (AIC0_IFACE3_CLKS_ON | AIC0_IFACE3_ADC_TO_DAC));
    CHECK(wb_codec_loop(false) && aic.reg[0][AIC0_IFACE3] == AIC0_IFACE3_CLKS_ON);
}

// A codec that doesn't answer, or whose drivers or MicPGAs never come up: the bring-up
// says so (within its timeouts), and the device's requests still work.
TEST(codec_failures) {
    board();
    aic.answers = false;
    wb_codec_cfg_t cfg = {wb_rate(96000), 0, 0, false};
    CHECK(wb_codec_start(&cfg) == WB_CODEC_NO_ANSWER);
    board();
    aic.hp_never = true;
    uint32_t t0 = fb.now;
    CHECK(wb_codec_start(&cfg) == WB_CODEC_HP_TIMEOUT);
    CHECK(fb.now - t0 >= 500 && fb.now - t0 < 600);
    CHECK((aic.reg[0][AIC0_DAC_SETUP1] & 0x80) == 0);   // no DAC without its drivers
    board();
    aic.pga_never = true;
    t0 = fb.now;
    CHECK(wb_codec_start(&cfg) == WB_CODEC_POWER_TIMEOUT);
    CHECK(fb.now - t0 >= 200 && fb.now - t0 < 300);
    // the device: no codec, no CODEC state; GET_INFO still answers
    board();
    aic.answers = false;
    fb.clock_on = false;
    wb_dev_t d;
    CHECK(!wb_dev_init(&d, 96000));
    CHECK(!(wb_dev_state(&d) & WP_ST_CODEC));
    uint8_t buf[32];
    CHECK(wb_dev_vendor_in(&d, WP_REQ_GET_INFO, 0, 0, buf, sizeof buf) == (int)sizeof(wp_info_t));
    CHECK(wb_dev_vendor_out(&d, WP_REQ_SET_TX_GAIN, 4, 0));   // kept for when it answers
    CHECK(d.tx_gain == 4);
    CHECK(wb_dev_vendor_in(&d, WP_REQ_CODEC_READ, 5, 0, buf, sizeof buf) == -1);
    // answering but no clocks: up, but not CODEC
    board();
    fb.clock_on = false;
    CHECK(!wb_dev_init(&d, 96000));
    CHECK(d.result == WB_CODEC_OK && !d.clock && !(wb_dev_state(&d) & WP_ST_CODEC));
}

// Packets are sized by the codec's clock: a millisecond's samples, mono S16,
// with room to catch up, within a full-speed isochronous packet.
TEST(packet_sizing) {
    CHECK(wp_samples_per_ms(96000) * 2 == 192);    // SPEC-WB §5
    CHECK(wp_samples_per_ms(192000) * 2 == 384);
    CHECK(wp_samples_per_ms(48000) * 2 == 96);
    CHECK(WP_MAX_SAMPLES * 4 >= wp_samples_per_ms(192000) * 5);   // 25% to catch up at the top rate
    CHECK(WP_PACKET_BYTES == 2 * WP_MAX_SAMPLES && WP_PACKET_BYTES <= 1023);
    CHECK(sizeof(wp_info_t) == 16 && sizeof(wp_status_t) == 28 && sizeof(wp_event_t) == 8);
    CHECK(SFIFO_SIZE >= 3u * wp_samples_per_ms(192000) + 2u * WP_MAX_SAMPLES);   // head start and two packets
}

// ---------------------------------------------------------------------------
// The samples between the frames and the streams
// ---------------------------------------------------------------------------

// A frame's words: left, right samples MSB-aligned, junk in the low bits.
static void frame(uint32_t *w, int16_t l, int16_t r) {
    w[0] = (uint32_t)(uint16_t)l << 16 | 0x1234u;
    w[1] = (uint32_t)(uint16_t)r << 16 | 0xABCDu;
}

// Received frames: the chosen channel's top 16 bits reach the host in order,
// only while streaming; the peaks follow both channels.
TEST(audio_rx) {
    static wb_audio_t a;
    wb_audio_init(&a, 0);
    uint32_t w[2 * 8];
    for (int i = 0; i < 8; i++) frame(&w[2 * i], (int16_t)(100 * i), (int16_t)(-1000 - i));
    wb_audio_rx(&a, w, 8);
    CHECK(sfifo_count(&a.s.to_host) == 0 && a.frames == 8);   // not streaming
    sstream_set(&a.s, true);
    wb_audio_rx(&a, w, 8);
    int16_t got[16];
    CHECK(sstream_to_host(&a.s, got, 16) == 8);
    bool ok = true;
    for (int i = 0; i < 8; i++) ok &= got[i] == (int16_t)(100 * i);
    CHECK(ok);
    a.input = WP_INPUT_LINE;
    wb_audio_rx(&a, w, 8);
    CHECK(sstream_to_host(&a.s, got, 16) == 8 && got[0] == -1000 && got[7] == -1007);
    uint16_t pk[WB_PEAKS];
    wb_audio_peaks(&a, pk);
    CHECK(pk[WB_PEAK_HYBRID] == 700 && pk[WB_PEAK_LINE] == 1007 && pk[WB_PEAK_TX] == 0);
    wb_audio_peaks(&a, pk);
    CHECK(pk[WB_PEAK_HYBRID] == 0 && pk[WB_PEAK_LINE] == 0);
    frame(w, -32768, 32767);
    wb_audio_rx(&a, w, 1);
    wb_audio_peaks(&a, pk);
    CHECK(pk[WB_PEAK_HYBRID] == 32768 && pk[WB_PEAK_LINE] == 32767);
    CHECK(wb_word_sample(0x80000000u) == -32768 && wb_sample_word(-1) == 0xFFFF0000u);
}

// Frames to send: the host's samples after their head start, in both slots
// with the low bits clear; silence before and after; the DATA count.
TEST(audio_tx) {
    static wb_audio_t a;
    wb_audio_init(&a, 4);
    uint32_t w[2 * 8];
    wb_audio_tx(&a, w, 8);
    bool silent = true;
    for (int i = 0; i < 16; i++) silent &= w[i] == 0;
    CHECK(silent && a.s.underruns == 0);
    sstream_set(&a.s, true);
    int16_t s[6] = {1, -2, 3, -4, 5, -32768};
    sstream_from_host(&a.s, s, 3);
    wb_audio_tx(&a, w, 2);
    CHECK(w[0] == 0 && a.s.samples_out == 0);   // 3 < 4: still filling
    sstream_from_host(&a.s, s + 3, 3);
    wb_audio_tx(&a, w, 8);
    CHECK(w[0] == 0x00010000u && w[1] == 0x00010000u && w[2] == 0xFFFE0000u && w[3] == 0xFFFE0000u);
    CHECK(w[10] == 0x80000000u && w[12] == 0);
    CHECK(a.s.samples_out == 6 && a.tx_active == 6 && a.s.underruns == 1);
    uint16_t pk[WB_PEAKS];
    wb_audio_peaks(&a, pk);
    CHECK(pk[WB_PEAK_TX] == 32768);
}

// Firmware loopback: the host's samples come back to it, one per frame, and
// the line's don't; off again, the line's do.
TEST(audio_loopback) {
    static wb_audio_t a;
    wb_audio_init(&a, 0);
    sstream_set(&a.s, true);
    a.loopback = true;
    int16_t s[5] = {11, 22, 33, 44, 55};
    sstream_from_host(&a.s, s, 5);
    uint32_t w[2 * 5];
    for (int i = 0; i < 5; i++) frame(&w[2 * i], 999, 999);
    wb_audio_tx(&a, w, 5);
    wb_audio_rx(&a, w, 5);
    int16_t got[10];
    CHECK(sstream_to_host(&a.s, got, 10) == 5);
    CHECK(got[0] == 11 && got[4] == 55);
    a.loopback = false;
    for (int i = 0; i < 5; i++) frame(&w[2 * i], 999, 999);
    wb_audio_rx(&a, w, 5);
    CHECK(sstream_to_host(&a.s, got, 10) == 5 && got[0] == 999);
}

// ---------------------------------------------------------------------------
// The device logic and requests
// ---------------------------------------------------------------------------
static wb_dev_t d;

// A fresh board and device, up at 96 kHz.
static void dev_setup(void) {
    board();
    CHECK(wb_dev_init(&d, 96000));
}

// Init: the codec up at 96 kHz, the I2S slave started at that rate, the
// defaults, and all of it in GET_INFO.
TEST(dev_init_and_info) {
    dev_setup();
    CHECK(fb.i2s_on && fb.i2s_rate == 96000 && fb.starts == 1);
    CHECK(d.clock && d.result == WB_CODEC_OK);
    CHECK(fb.input == WP_INPUT_HYBRID && !fb.loopback);
    uint8_t buf[64];
    CHECK(wb_dev_vendor_in(&d, WP_REQ_GET_INFO, 0, 0, buf, sizeof buf) == (int)sizeof(wp_info_t));
    wp_info_t i;
    memcpy(&i, buf, sizeof i);
    CHECK(i.version == WP_VERSION && i.rate == 96000 && i.max_samples == WP_MAX_SAMPLES && i.board == WP_BOARD);
    CHECK(i.state == WP_ST_CODEC && i.input == WP_INPUT_HYBRID && i.loopback == WP_LOOP_OFF);
    CHECK(i.tx_gain == 0 && i.rx_gain == 0);
    CHECK(wb_dev_vendor_in(&d, WP_REQ_GET_INFO, 0, 0, buf, 4) == 4);   // short wLength: cut
    board();
    CHECK(wb_dev_init(&d, 12345));   // not a rate it has: 96 kHz
    CHECK(d.rate->hz == 96000);
}

// SET_RATE takes 48, 96 and 192 (kHz): the codec brought up again at that
// rate (its registers say so) and the slave restarted; others stall.
TEST(dev_rate) {
    dev_setup();
    CHECK(wb_dev_vendor_out(&d, WP_REQ_SET_RATE, 192, 0));
    CHECK(d.rate->hz == 192000 && fb.i2s_rate == 192000 && fb.starts == 2 && fb.resets == 2);
    check_clocks(192000);
    CHECK(wb_dev_vendor_out(&d, WP_REQ_SET_RATE, 48, 0));
    check_clocks(48000);
    CHECK(!wb_dev_vendor_out(&d, WP_REQ_SET_RATE, 44, 0));
    CHECK(!wb_dev_vendor_out(&d, WP_REQ_SET_RATE, 96000 & 0xFFFF, 0));
    CHECK(d.rate->hz == 48000);
    uint8_t buf[16];
    wp_info_t i;
    CHECK(wb_dev_vendor_in(&d, WP_REQ_GET_INFO, 0, 0, buf, sizeof buf) == 16);
    memcpy(&i, buf, sizeof i);
    CHECK(i.rate == 48000);
}

// The gains, the input and the loopbacks: in range they reach the codec
// (or the samples' path) and survive a reset; out of range they stall.
TEST(dev_settings) {
    dev_setup();
    CHECK(wb_dev_vendor_out(&d, WP_REQ_SET_TX_GAIN, (uint16_t)-20, 0));
    CHECK(aic.reg[0][AIC0_DAC_VOL_L] == (uint8_t)-20 && d.tx_gain == -20);
    CHECK(!wb_dev_vendor_out(&d, WP_REQ_SET_TX_GAIN, (uint16_t)-128, 0));
    CHECK(!wb_dev_vendor_out(&d, WP_REQ_SET_TX_GAIN, 49, 0));
    CHECK(wb_dev_vendor_out(&d, WP_REQ_SET_TX_GAIN, 48, 0) && aic.reg[0][AIC0_DAC_VOL_L] == 48);
    CHECK(wb_dev_vendor_out(&d, WP_REQ_SET_RX_GAIN, 40, 0) && aic.reg[1][AIC1_LPGA_VOL] == 40);
    CHECK(!wb_dev_vendor_out(&d, WP_REQ_SET_RX_GAIN, 96, 0));
    CHECK(wb_dev_vendor_out(&d, WP_REQ_SET_INPUT, WP_INPUT_LINE, 0) && fb.input == WP_INPUT_LINE);
    CHECK(!wb_dev_vendor_out(&d, WP_REQ_SET_INPUT, 2, 0));
    CHECK(wb_dev_vendor_out(&d, WP_REQ_SET_LOOPBACK, WP_LOOP_FIRMWARE, 0) && fb.loopback);
    CHECK(aic.reg[0][AIC0_IFACE3] == AIC0_IFACE3_CLKS_ON);
    CHECK(wb_dev_state(&d) & WP_ST_LOOPBACK);
    CHECK(wb_dev_vendor_out(&d, WP_REQ_SET_LOOPBACK, WP_LOOP_CODEC, 0) && !fb.loopback);
    CHECK(aic.reg[0][AIC0_IFACE3] & AIC0_IFACE3_ADC_TO_DAC);
    CHECK(!wb_dev_vendor_out(&d, WP_REQ_SET_LOOPBACK, 3, 0));
    // a reset keeps them all
    CHECK(wb_dev_vendor_out(&d, WP_REQ_RESET, 0, 0) && fb.resets == 2);
    CHECK(aic.reg[0][AIC0_DAC_VOL_L] == 48 && aic.reg[1][AIC1_LPGA_VOL] == 40);
    CHECK((aic.reg[0][AIC0_IFACE3] & AIC0_IFACE3_ADC_TO_DAC) && fb.input == WP_INPUT_LINE);
    CHECK(wb_dev_vendor_out(&d, WP_REQ_SET_LOOPBACK, WP_LOOP_OFF, 0));
    CHECK(!(aic.reg[0][AIC0_IFACE3] & AIC0_IFACE3_ADC_TO_DAC) && !(wb_dev_state(&d) & WP_ST_LOOPBACK));
}

// Raw register access with the page in wIndex's high byte; the page select
// itself is the firmware's; unknown requests stall.
TEST(dev_register_access) {
    dev_setup();
    CHECK(wb_dev_vendor_out(&d, WP_REQ_CODEC_WRITE, AIC1_HPL_GAIN << 8 | 0x05, 1 << 8));
    CHECK(aic.reg[1][AIC1_HPL_GAIN] == 0x05);
    CHECK(wb_dev_vendor_out(&d, WP_REQ_CODEC_WRITE, 100 << 8 | 0x42, 0));
    CHECK(aic.reg[0][100] == 0x42);
    uint8_t b[4];
    CHECK(wb_dev_vendor_in(&d, WP_REQ_CODEC_READ, AIC1_HPL_GAIN, 1 << 8, b, sizeof b) == 1 && b[0] == 0x05);
    CHECK(wb_dev_vendor_in(&d, WP_REQ_CODEC_READ, 100, 0, b, sizeof b) == 1 && b[0] == 0x42);
    CHECK(wb_dev_vendor_in(&d, WP_REQ_CODEC_READ, 128, 0, b, sizeof b) == -1);
    CHECK(!wb_dev_vendor_out(&d, WP_REQ_CODEC_WRITE, AIC_PAGE << 8 | 1, 0));
    CHECK(!wb_dev_vendor_out(&d, WP_REQ_CODEC_WRITE, 0x80 << 8, 0));
    CHECK(wb_dev_vendor_in(&d, 0x7F, 0, 0, b, sizeof b) == -1);
    CHECK(!wb_dev_vendor_out(&d, 0x7F, 0, 0));
    CHECK(wb_dev_vendor_in(&d, WP_REQ_SET_RATE, 96, 0, b, sizeof b) == -1);   // an OUT request asked IN
    CHECK(wb_dev_vendor_out(&d, WP_REQ_REBOOT_UF2, 0, 0) && fb.reboots == 1);
}

// GET_STATUS: the peaks and the codec's sticky flags since the last one,
// its power flags, the streams' counters.
TEST(dev_status) {
    dev_setup();
    fb.peaks[0] = 1000;
    fb.peaks[1] = 20000;
    fb.peaks[2] = 30000;
    aic.reg[0][AIC0_STICKY1] = 0x08;   // left ADC overflow
    aic.reg[0][AIC0_STICKY2] = 0x80;   // HPL over current
    uint8_t buf[64];
    CHECK(wb_dev_vendor_in(&d, WP_REQ_GET_STATUS, 0, 0, buf, sizeof buf) == (int)sizeof(wp_status_t));
    wp_status_t s;
    memcpy(&s, buf, sizeof s);
    CHECK(s.peak_hybrid == 1000 && s.peak_line == 20000 && s.peak_tx == 30000);
    CHECK(s.ovf == 0x08 && s.oc == 0x80);
    CHECK((s.adc_flags & AIC0_ADC_FLAGS_UP) == AIC0_ADC_FLAGS_UP && (s.dac_flags & AIC0_DAC_FLAGS_UP) == AIC0_DAC_FLAGS_UP);
    CHECK(s.underruns == 2 && s.overruns == 3 && s.samples_in == 4000 && s.samples_out == 3000);
    CHECK(s.state == WP_ST_CODEC);
    CHECK(wb_dev_vendor_in(&d, WP_REQ_GET_STATUS, 0, 0, buf, sizeof buf) > 0);
    memcpy(&s, buf, sizeof s);
    CHECK(s.peak_hybrid == 0 && s.ovf == 0 && s.oc == 0);
}

// The LEDs: LINK as the host sets it (AUTO: while streaming); DATA while
// the host's samples aren't silent, and 50 ms after.
TEST(dev_leds) {
    dev_setup();
    wb_dev_poll(&d);
    CHECK(!fb.led_link && !fb.led_data);
    fb.streaming = true;
    wb_dev_poll(&d);
    CHECK(fb.led_link && (wb_dev_state(&d) & WP_ST_CARRIER));
    CHECK(wb_dev_vendor_out(&d, WP_REQ_SET_CARRIER, WP_CARRIER_OFF, 0));
    wb_dev_poll(&d);
    CHECK(!fb.led_link);
    fb.streaming = false;
    CHECK(wb_dev_vendor_out(&d, WP_REQ_SET_CARRIER, WP_CARRIER_ON, 0));
    wb_dev_poll(&d);
    CHECK(fb.led_link);
    CHECK(!wb_dev_vendor_out(&d, WP_REQ_SET_CARRIER, 3, 0));
    fb.tx_active = 10;
    wb_dev_poll(&d);
    CHECK(fb.led_data);
    fb.now += 49;
    wb_dev_poll(&d);
    CHECK(fb.led_data);
    fb.now += 1;
    wb_dev_poll(&d);
    CHECK(!fb.led_data);
}

// Events: when the state changes, the codec's INT1 brings its sticky flags
// (read and cleared), the clocks stop or the slave loses its place (counted).
TEST(dev_events) {
    dev_setup();
    wp_event_t e;
    CHECK(wb_dev_event(&d, &e));
    uint8_t seq = e.seq;
    CHECK(e.state == WP_ST_CODEC && e.underruns == 2 && e.overruns == 3);
    CHECK(!wb_dev_event(&d, &e));
    fb.streaming = true;
    CHECK(wb_dev_event(&d, &e) && e.seq == (uint8_t)(seq + 1));
    CHECK(e.state == (WP_ST_CODEC | WP_ST_STREAMING | WP_ST_CARRIER));
    // INT1: the overflow read out of register 42 and cleared, once in an event
    aic.reg[0][AIC0_STICKY1] = 0x04;
    fb.intr = true;
    wb_dev_poll(&d);
    fb.intr = false;
    CHECK(aic.reg[0][AIC0_STICKY1] == 0);
    CHECK(wb_dev_event(&d, &e) && e.ovf == 0x04);
    CHECK(!wb_dev_event(&d, &e));
    // the codec's clocks stop: within 100 ms, no CODEC
    fb.clock_on = false;
    fb.now += 100;
    wb_dev_poll(&d);
    fb.now += 100;
    wb_dev_poll(&d);
    CHECK(!d.clock && wb_dev_event(&d, &e) && !(e.state & WP_ST_CODEC));
    fb.clock_on = true;
    whal_sleep_ms(100);
    wb_dev_poll(&d);
    CHECK(d.clock && wb_dev_event(&d, &e) && (e.state & WP_ST_CODEC));
    // the slave lost its place: counted for the status
    fb.lost = true;
    wb_dev_poll(&d);
    CHECK(d.resyncs == 1);
    uint8_t buf[32];
    wp_status_t s;
    CHECK(wb_dev_vendor_in(&d, WP_REQ_GET_STATUS, 0, 0, buf, sizeof buf) > 0);
    memcpy(&s, buf, sizeof s);
    CHECK(s.resyncs == 1);
}

// Runs every test; exit status 1 if any check failed.
int main(void) {
    RUN(codec_clocks);
    RUN(codec_hybrid_routing);
    RUN(codec_sequence);
    RUN(codec_failures);
    RUN(packet_sizing);
    RUN(audio_rx);
    RUN(audio_tx);
    RUN(audio_loopback);
    RUN(dev_init_and_info);
    RUN(dev_rate);
    RUN(dev_settings);
    RUN(dev_register_access);
    RUN(dev_status);
    RUN(dev_leds);
    RUN(dev_events);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
