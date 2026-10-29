/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// TI TLV320AIC3204: the registers and bits the firmware uses. TI SLAA557
// (Application Reference Guide, November 2012), chapter 5 "Register Map";
// AIC0_* are on page 0, AIC1_* on page 1. Register 0 of every page selects
// the page.
#pragma once

#define AIC_I2C_ADDR 0x18        // 7-bit (SLAA557 2.8.1)
#define AIC_PAGE 0               // page select, on every page

// --- page 0 --------------------------------------------------------------
#define AIC0_RESET 1             // D0: software reset (self clearing)
#define AIC0_CLK_MUX 4           // D6 PLL range, D3:2 PLL_CLKIN, D1:0 CODEC_CLKIN
#define AIC0_CLK_MUX_PLL_MCLK 0x00     // PLL_CLKIN = MCLK
#define AIC0_CLK_MUX_CODEC_PLL 0x03    // CODEC_CLKIN = PLL_CLK
#define AIC0_PLL_PR 5            // D7 power, D6:4 P (0 means 8), D3:0 R
#define AIC0_PLL_ON 0x80
#define AIC0_PLL_J 6             // D5:0 J, 4..63
#define AIC0_PLL_D_MSB 7         // D5:0 D[13:8]: write 7, then 8 at once
#define AIC0_PLL_D_LSB 8         // D7:0 D[7:0]
#define AIC0_NDAC 11             // D7 power, D6:0 the divider (0 means 128)
#define AIC0_MDAC 12
#define AIC0_DOSR_MSB 13         // D1:0 DOSR[9:8]: write 13, then 14
#define AIC0_DOSR_LSB 14
#define AIC0_NADC 18
#define AIC0_MADC 19
#define AIC0_AOSR 20             // 32, 64 or 128 (0 means 256)
#define AIC0_DIV_ON 0x80         // NDAC, MDAC, NADC, MADC, BCLK N: the divider powered up
#define AIC0_IFACE1 27           // D7:6 format, D5:4 word length, D3 BCLK out, D2 WCLK out
#define AIC0_IFACE1_I2S 0x00
#define AIC0_IFACE1_32BIT 0x30
#define AIC0_IFACE1_BCLK_OUT 0x08
#define AIC0_IFACE1_WCLK_OUT 0x04
#define AIC0_IFACE3 29           // D5, D4 loopbacks, D3 BCLK inverted, D2 clocks kept, D1:0 BDIV_CLKIN
#define AIC0_IFACE3_ADC_TO_DAC 0x10    // the stereo ADC's output is the DAC's input
#define AIC0_IFACE3_CLKS_ON 0x04       // BCLK and WCLK run even with the codec powered down
#define AIC0_IFACE3_BDIV_DAC_CLK 0x00  // BCLK = DAC_CLK / N
#define AIC0_BCLK_N 30           // D7 power, D6:0 N (0 means 128)
#define AIC0_ADC_FLAGS 36        // D7 left PGA at gain, D6 left ADC up, D3 right PGA, D2 right ADC up
#define AIC0_ADC_FLAGS_UP 0x44
#define AIC0_ADC_FLAGS_PGA 0x88   // both MicPGAs at their programmed gain
#define AIC0_DAC_FLAGS 37        // D7 left DAC up, D5 HPL up, D3 right DAC up, D1 HPR up
#define AIC0_DAC_FLAGS_UP 0xA2   // left DAC, HPL, HPR (the right DAC stays down)
#define AIC0_STICKY1 42          // overflows since the last read: D7/D6 left/right DAC, D3/D2 left/right ADC
#define AIC0_STICKY1_MASK 0xCC
#define AIC0_STICKY2 44          // D7/D6 HPL/HPR over current (and others) since the last read
#define AIC0_STICKY2_OC 0xC0
#define AIC0_INT1 48             // D3 over current, D2 overflow, D0 pulses until the flags are read
#define AIC0_INT1_OC 0x08
#define AIC0_INT1_OVF 0x04
#define AIC0_INT1_PULSES 0x01
#define AIC0_MFP5 52             // GPIO/MFP5: D5:2 function
#define AIC0_MFP5_INT1 0x14      // 0101: the pin is INT1
#define AIC0_DAC_PRB 60          // D4:0 PRB_Px
#define AIC0_ADC_PRB 61          // D4:0 PRB_Rx
#define AIC0_DAC_SETUP1 63       // D7 left DAC up, D6 right DAC up, D5:4 left data, D3:2 right data
#define AIC0_DAC_LEFT_ON 0x80
#define AIC0_DAC_LEFT_DATA 0x10  // left DAC = left channel of the audio interface
#define AIC0_DAC_SETUP2 64       // D3 left mute, D2 right mute
#define AIC0_DAC_MUTE_R 0x04
#define AIC0_DAC_MUTE_L 0x08
#define AIC0_DAC_VOL_L 65        // signed, 0.5 dB steps: -127 (-63.5 dB) .. 48 (+24 dB)
#define AIC0_ADC_SETUP 81        // D7 left ADC up, D6 right ADC up
#define AIC0_ADC_ON 0xC0
#define AIC0_ADC_FINE 82         // D7 left mute, D3 right mute (both muted at reset)
#define AIC0_ADC_VOL_L 83        // signed, 0.5 dB steps: -24 .. 40
#define AIC0_ADC_VOL_R 84

// --- page 1 --------------------------------------------------------------
#define AIC1_POWER 1             // D3: AVDD's weak connection to DVDD off
#define AIC1_POWER_NO_WEAK_AVDD 0x08
#define AIC1_LDO 2               // D7:6 DVDD LDO, D5:4 AVDD LDO, D3 analog off, D0 AVDD LDO on
#define AIC1_LDO_AVDD_1V77 0x20  // 10: 1.77 V (datasheet 7.9, LDOMode 2)
#define AIC1_LDO_ANALOG_OFF 0x08
#define AIC1_LDO_AVDD_ON 0x01
#define AIC1_PLAY1 3             // D4:2 left DAC PowerTune: 000 PTM_P3/P4
#define AIC1_PLAY2 4             // the right DAC's
#define AIC1_OUT_POWER 9         // D5 HPL, D4 HPR, D3 LOL, D2 LOR, D1 MAL, D0 MAR
#define AIC1_OUT_HPL 0x20
#define AIC1_OUT_HPR 0x10
#define AIC1_CM 10               // D6 chip CM (0: 0.9 V), D5:4 HP CM (00: = chip CM), D1 HP from LDOIN
#define AIC1_HPL_ROUTE 12        // D3 left DAC +, D2 IN1L, D1 MAL, D0 MAR
#define AIC1_HPL_LDAC 0x08
#define AIC1_HPR_ROUTE 13        // D4 left DAC -, D3 right DAC +, D2 IN1R, D1 MAR, D0 HPL
#define AIC1_HPR_LDAC_M 0x10
#define AIC1_HPL_GAIN 16         // D6 mute, D5:0 gain in dB, 6-bit two's complement: -6 .. 29
#define AIC1_HPR_GAIN 17
#define AIC1_HP_START 20         // D7:6 soft routing, D5:2 slow charge, D1:0 its resistor
#define AIC1_LPGA_P 52           // left MicPGA +: D7:6 IN1L, D5:4 IN2L, D3:2 IN3L, D1:0 IN1R
#define AIC1_LPGA_M 54           // left MicPGA -: D7:6 CM1L, D5:4 IN2R, D3:2 IN3R, D1:0 CM2L
#define AIC1_RPGA_P 55           // right MicPGA +: D7:6 IN1R, D5:4 IN2R, D3:2 IN3R, D1:0 IN2L
#define AIC1_RPGA_M 57           // right MicPGA -: D7:6 CM1R, D5:4 IN1L, D3:2 IN3L, D1:0 CM2R
#define AIC_R10K 1u              // each routing field: 00 off, 01 10 kOhm, 10 20 kOhm, 11 40 kOhm
#define AIC_R20K 2u
#define AIC_ROUTE(r, shift) ((r) << (shift))
#define AIC1_FLOAT 58            // inputs weakly at CM: D7 IN1L, D6 IN1R, D5 IN2L, D4 IN2R, D3 IN3L, D2 IN3R
#define AIC1_FLOAT_IN2L 0x20
#define AIC1_FLOAT_IN3L 0x08
#define AIC1_LPGA_VOL 59         // D7: 1 = fixed 0 dB; D6:0 gain in 0.5 dB steps, 0..95
#define AIC1_RPGA_VOL 60
#define AIC1_PGA_MAX 95
#define AIC1_PTM_R 61            // 0x00: PTM_R4
#define AIC1_DAC_GAIN_FLAGS 63   // D7 HPL at its gain, D6 HPR at its gain
#define AIC1_DAC_GAIN_FLAGS_HP 0xC0
#define AIC1_IN_CHARGE 71        // analog inputs' power-up time: 0x32 = 3.1 ms
#define AIC1_REF 123             // D2 reference always on, D1:0 fast charge: 01 = 40 ms
