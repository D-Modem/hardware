/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The WB stick's codec: clocks, register access by page, bring-up. See
// wb_codec.h; the registers are in aic3204.h.
#include <stddef.h>
#include "aic3204.h"
#include "wb_codec.h"
#include "wb_hal.h"

// The rates (SPEC-WB.md §4). Each keeps DOSR x fs = 6.144 MHz (SLAA557
// 2.4.6: between 2.8 and 6.2 MHz) and MDAC x DOSR / 32 at least the
// processing block's resource class (Tables 2-7 and 2-16):
//   48 kHz:  OSR 128, filter A: PRB_P1 (RC 8), PRB_R1 (RC 6); 4 x 128 / 32 = 16
//   96 kHz:  OSR 64,  filter B: PRB_P7 (RC 6), PRB_R7 (RC 3); 4 x 64 / 32 = 8
//   192 kHz: OSR 32,  filter C: PRB_P17 (RC 3), PRB_R13 (RC 3); 4 x 32 / 32 = 4
// BCLK = 24.576 MHz / N = 64 fs: N = 8, 4, 2.
static const wb_rate_t rates[] = {
    {48000, 128, 1, 1, 8},
    {96000, 64, 7, 7, 4},
    {192000, 32, 17, 13, 2},
};

#define PAGE_UNKNOWN 0xFF
static uint8_t s_page = PAGE_UNKNOWN;

#define HP_TIMEOUT_MS 500        // the drivers' soft-stepping up to their gain
#define POWER_TIMEOUT_MS 200     // the converters' power-up flags

const wb_rate_t *wb_rate(uint32_t hz) {
    for (size_t i = 0; i < sizeof rates / sizeof rates[0]; i++)
        if (rates[i].hz == hz) return &rates[i];
    return NULL;
}

// Select a page, unless it already is (an access that failed forgets it).
static bool page(uint8_t p) {
    if (p == s_page) return true;
    if (!whal_codec_write(AIC_PAGE, p)) {
        s_page = PAGE_UNKNOWN;
        return false;
    }
    s_page = p;
    return true;
}

bool wb_codec_write(uint8_t p, uint8_t reg, uint8_t v) {
    if (!page(p)) return false;
    if (whal_codec_write(reg, v)) return true;
    s_page = PAGE_UNKNOWN;
    return false;
}

bool wb_codec_read(uint8_t p, uint8_t reg, uint8_t *v) {
    if (!page(p)) return false;
    if (whal_codec_read(reg, v)) return true;
    s_page = PAGE_UNKNOWN;
    return false;
}

// Wait until (register & mask) == mask, reading every millisecond; false
// after timeout_ms (or if the codec stops answering).
static bool wait_flags(uint8_t p, uint8_t reg, uint8_t mask, uint32_t timeout_ms) {
    for (uint32_t t = 0;; t++) {
        uint8_t v = 0;
        if (!wb_codec_read(p, reg, &v)) return false;
        if ((v & mask) == mask) return true;
        if (t >= timeout_ms) return false;
        whal_sleep_ms(1);
    }
}

#define W(p, r, v) do { if (!wb_codec_write((p), (r), (uint8_t)(v))) return WB_CODEC_NO_ANSWER; } while (0)

wb_codec_result_t wb_codec_start(const wb_codec_cfg_t *cfg) {
    const wb_rate_t *rt = cfg->rate;

    // a known start: hardware reset, then the software reset on page 0
    whal_codec_reset();
    whal_sleep_ms(1);                                  // SLAA557 3.2: 1 ms before any access
    s_page = PAGE_UNKNOWN;
    W(0, AIC0_RESET, 0x01);
    s_page = 0;                                        // a reset leaves page 0 selected
    whal_sleep_ms(1);

    // clocks: PLL from MCLK; P, R, J, D first, then the PLL powered up
    // (10 ms to lock, SLAA557 3.4); CODEC_CLKIN = PLL_CLK
    W(0, AIC0_CLK_MUX, AIC0_CLK_MUX_PLL_MCLK | AIC0_CLK_MUX_CODEC_PLL);
    W(0, AIC0_PLL_PR, (WB_PLL_P & 7) << 4 | WB_PLL_R);
    W(0, AIC0_PLL_J, WB_PLL_J);
    W(0, AIC0_PLL_D_MSB, WB_PLL_D >> 8);              // D: 7 then 8, back to back
    W(0, AIC0_PLL_D_LSB, WB_PLL_D & 0xFF);
    W(0, AIC0_PLL_PR, AIC0_PLL_ON | (WB_PLL_P & 7) << 4 | WB_PLL_R);
    whal_sleep_ms(10);
    // dividers, oversampling (DOSR: 13 then 14), processing blocks
    W(0, AIC0_NDAC, AIC0_DIV_ON | WB_NDAC);
    W(0, AIC0_MDAC, AIC0_DIV_ON | WB_MDAC);
    W(0, AIC0_DOSR_MSB, rt->osr >> 8);
    W(0, AIC0_DOSR_LSB, rt->osr & 0xFF);
    W(0, AIC0_NADC, AIC0_DIV_ON | WB_NADC);
    W(0, AIC0_MADC, AIC0_DIV_ON | WB_MADC);
    W(0, AIC0_AOSR, rt->osr & 0xFF);
    // the audio interface: I2S, 32-bit words (PTM_P4 wants 20 or more), the
    // codec the master: BCLK = DAC_CLK / N, WCLK = DAC_FS
    W(0, AIC0_IFACE1, AIC0_IFACE1_I2S | AIC0_IFACE1_32BIT | AIC0_IFACE1_BCLK_OUT | AIC0_IFACE1_WCLK_OUT);
    W(0, AIC0_IFACE3, AIC0_IFACE3_CLKS_ON | AIC0_IFACE3_BDIV_DAC_CLK |
                          (cfg->loop_codec ? AIC0_IFACE3_ADC_TO_DAC : 0));
    W(0, AIC0_BCLK_N, AIC0_DIV_ON | rt->bclk_n);
    W(0, AIC0_DAC_PRB, rt->prb_p);
    W(0, AIC0_ADC_PRB, rt->prb_r);
    // INT1 on MFP5: overflows and over current, pulsing until the flags are read
    W(0, AIC0_INT1, AIC0_INT1_OC | AIC0_INT1_OVF | AIC0_INT1_PULSES);
    W(0, AIC0_MFP5, AIC0_MFP5_INT1);

    // analog: AVDD from the A-LDO (1.77 V, its highest) with the weak
    // AVDD-DVDD link off first; the analog blocks on
    W(1, AIC1_POWER, AIC1_POWER_NO_WEAK_AVDD);
    W(1, AIC1_LDO, AIC1_LDO_AVDD_1V77 | AIC1_LDO_AVDD_ON);
    // common mode 0.9 V, the headphone drivers' the same and powered from
    // AVDD (a differential load needs that: SLAA557 2.2.3.4)
    W(1, AIC1_CM, 0x00);
    W(1, AIC1_PLAY1, 0x00);                            // PTM_P4 (with 32-bit words)
    W(1, AIC1_PLAY2, 0x00);
    W(1, AIC1_PTM_R, 0x00);                            // PTM_R4
    W(1, AIC1_IN_CHARGE, 0x32);                        // the inputs' power-up time: 3.1 ms
    W(1, AIC1_REF, 0x01);                              // the reference's fast charge: 40 ms
    // no slow charge: the drivers' load is differential with no coupling
    // capacitor (SLAA557 2.2.3: "unnecessary")
    W(1, AIC1_HP_START, 0x00);
    // the line drive: the left DAC's two outputs, HPL = +, HPR = -
    // (SLAA557 2.2.3.2, "Mono Differential DAC to Mono Differential Headphone")
    W(1, AIC1_HPL_ROUTE, AIC1_HPL_LDAC);
    W(1, AIC1_HPR_ROUTE, AIC1_HPR_LDAC_M);
    // the hybrid, in the left MicPGA: + = IN1L (line A) + IN1R (replica B),
    // - = IN2R (line B) + IN3R (replica A), 10 kOhm each: line - replica
    W(1, AIC1_LPGA_P, AIC_ROUTE(AIC_R10K, 6) | AIC_ROUTE(AIC_R10K, 0));
    W(1, AIC1_LPGA_M, AIC_ROUTE(AIC_R10K, 4) | AIC_ROUTE(AIC_R10K, 2));
    // the right MicPGA: the line alone, + = IN2R (line B), - = IN1L (line A),
    // 20 kOhm (6 dB less gain: our own full-scale transmit fits)
    W(1, AIC1_RPGA_P, AIC_ROUTE(AIC_R20K, 4));
    W(1, AIC1_RPGA_M, AIC_ROUTE(AIC_R20K, 4));
    W(1, AIC1_FLOAT, AIC1_FLOAT_IN2L | AIC1_FLOAT_IN3L);   // the unused inputs held at CM
    W(1, AIC1_LPGA_VOL, cfg->rx_gain);                 // D7 clear: the gain applies
    W(1, AIC1_RPGA_VOL, 0x00);
    // the drivers at 0 dB, unmuted, then powered up; wait for their soft-stepping
    W(1, AIC1_HPL_GAIN, 0x00);
    W(1, AIC1_HPR_GAIN, 0x00);
    W(1, AIC1_OUT_POWER, AIC1_OUT_HPL | AIC1_OUT_HPR);
    if (!wait_flags(1, AIC1_DAC_GAIN_FLAGS, AIC1_DAC_GAIN_FLAGS_HP, HP_TIMEOUT_MS)) return WB_CODEC_HP_TIMEOUT;

    // the left DAC up (the left channel's data), unmuted at its volume; the
    // right DAC stays down and muted
    W(0, AIC0_DAC_SETUP1, AIC0_DAC_LEFT_ON | AIC0_DAC_LEFT_DATA);
    W(0, AIC0_DAC_VOL_L, cfg->tx_gain);
    W(0, AIC0_DAC_SETUP2, AIC0_DAC_MUTE_R);
    // both ADCs up, unmuted, 0 dB digital
    W(0, AIC0_ADC_SETUP, AIC0_ADC_ON);
    W(0, AIC0_ADC_FINE, 0x00);
    if (!wait_flags(0, AIC0_DAC_FLAGS, AIC0_DAC_FLAGS_UP, POWER_TIMEOUT_MS) ||
        !wait_flags(0, AIC0_ADC_FLAGS, AIC0_ADC_FLAGS_UP, POWER_TIMEOUT_MS))
        return WB_CODEC_POWER_TIMEOUT;
    return WB_CODEC_OK;
}

bool wb_codec_tx_gain(int16_t half_db) {
    if (half_db < -127 || half_db > 48) return false;
    return wb_codec_write(0, AIC0_DAC_VOL_L, (uint8_t)(int8_t)half_db);
}

bool wb_codec_rx_gain(int16_t half_db) {
    if (half_db < 0 || half_db > AIC1_PGA_MAX) return false;
    return wb_codec_write(1, AIC1_LPGA_VOL, (uint8_t)half_db);
}

bool wb_codec_loop(bool on) {
    return wb_codec_write(0, AIC0_IFACE3, AIC0_IFACE3_CLKS_ON | AIC0_IFACE3_BDIV_DAC_CLK |
                                              (on ? AIC0_IFACE3_ADC_TO_DAC : 0));
}
