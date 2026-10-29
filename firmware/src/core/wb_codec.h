/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// The WB stick's codec (TLV320AIC3204, SPEC-WB.md §3-4), portable: its
// clocks for each sample rate, its registers by page, and its bring-up
// (TI SLAA557 2.3.3.3 "ADC Setup", 2.4.6 "DAC Setup"). The hardware is
// behind wb_hal.h.
//
// Clocks: MCLK is the RP2040's 12 MHz crystal (GPOUT0, undivided). The
// codec's PLL makes PLL_CLK = 12 MHz x R x J.D / P = 12 x 8.192 = 98.304 MHz,
// its CODEC_CLKIN. Then DAC_CLK = ADC_CLK = 98.304 / 4 = 24.576 MHz,
// DAC_MOD_CLK = ADC_MOD_CLK = 24.576 / 4 = 6.144 MHz, fs = 6.144 MHz / OSR,
// and BCLK = DAC_CLK / N = 64 fs: 32-bit slots, I2S, the codec the master.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WB_MCLK_HZ 12000000u
#define WB_PLL_P 1
#define WB_PLL_R 1
#define WB_PLL_J 8
#define WB_PLL_D 1920            // J.D = 8.1920
#define WB_NDAC 4
#define WB_MDAC 4
#define WB_NADC 4
#define WB_MADC 4
#define WB_BCLK_PER_FRAME 64     // two 32-bit slots

// One sample rate's settings: the oversampling ratio (DOSR = AOSR), the
// processing blocks (PRB_Px, PRB_Rx: their decimation and interpolation
// filters suit the OSR) and the BCLK divider.
typedef struct {
    uint32_t hz;
    uint16_t osr;
    uint8_t prb_p, prb_r;
    uint8_t bclk_n;
} wb_rate_t;

// What the bring-up sets besides the rate: the host's settings.
typedef struct {
    const wb_rate_t *rate;
    int8_t tx_gain;              // the left DAC's digital volume, 0.5 dB steps (-127..48)
    uint8_t rx_gain;             // the left (hybrid) MicPGA's gain, 0.5 dB steps (0..95)
    bool loop_codec;             // the ADC's output to the DAC (WP_LOOP_CODEC)
} wb_codec_cfg_t;

// How the bring-up went.
typedef enum {
    WB_CODEC_OK,
    WB_CODEC_NO_ANSWER,          // no I2C answer
    WB_CODEC_HP_TIMEOUT,         // the headphone drivers never reached their gain
    WB_CODEC_POWER_TIMEOUT,      // the DAC, the drivers or the ADCs never said they were up
} wb_codec_result_t;

// The settings for a rate (48000, 96000 or 192000 Hz), NULL for another.
const wb_rate_t *wb_rate(uint32_t hz);
// A register on a page (the page selected first if it isn't already).
bool wb_codec_write(uint8_t page, uint8_t reg, uint8_t v);
// Read a register on a page.
bool wb_codec_read(uint8_t page, uint8_t reg, uint8_t *v);
// The codec reset (hardware and software) and brought up as cfg says: PLL
// and dividers, I2S master, LDO and common mode, the hybrid's mixer, the
// differential line drive; the drivers and converters powered up in
// SLAA557's order, waiting on their flags. Takes about 60 ms.
wb_codec_result_t wb_codec_start(const wb_codec_cfg_t *cfg);
// The left DAC's volume (0.5 dB steps, -127..48); false if out of range.
bool wb_codec_tx_gain(int16_t half_db);
// The left MicPGA's gain (0.5 dB steps, 0..95); false if out of range.
bool wb_codec_rx_gain(int16_t half_db);
// The ADC-to-DAC loopback on or off.
bool wb_codec_loop(bool on);
