/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// Skyworks Si3056 (with the Si3018/19 line side): the registers and bits the
// firmware uses. Datasheet rev 1.03, section 6 "Control Registers".
#pragma once

#define SI_CONTROL1 1            // SR, PWMM, PWME, IDL, SB
#define SI_CONTROL1_SB 0x01      // 16-bit TX data; secondary frames requested by the FC pin
#define SI_CONTROL2 2            // INTE, INTP, WDTEN, RDI, HBE, RXE (reset 0x03)
#define SI_CONTROL2_INTE 0x80    // AOUT/INT is an interrupt pin (active low unless INTP)
#define SI_CONTROL2_HBE 0x02
#define SI_CONTROL2_RXE 0x01
#define SI_INT_MASK 3            // RDTM ROVM FDTM BTDM DODM LCSOM DLCSM POLM
#define SI_INT_SOURCE 4          // the same bits, set when they happen; written 0 to clear
#define SI_INT_RDT 0x80          // ring detected
#define SI_INT_ROV 0x40          // receive overload
#define SI_INT_FDT 0x20          // frame detect (the link) lost
#define SI_INT_DOD 0x08          // drop out: the line went away while off hook
#define SI_INT_POL 0x01          // polarity reversal
#define SI_DAA1 5                // RDTN RDTP OPOL ONHM RDT OHE OH
#define SI_DAA1_ONHM 0x08        // on-hook line monitor (caller ID)
#define SI_DAA1_OHE 0x02         // the OFHK pin works
#define SI_DAA1_OH 0x01
#define SI_DAA2 6                // PDL (bit 4, reset 1: line side powered down), PDN
#define SI_SRC 7                 // sample rate code (line_proto.h lp_rate_code)
#define SI_PLL_N 8               // PLL1 N - 1: write N first, then M
#define SI_PLL_M 9               // PLL1 M - 1
#define SI_REV 11                // LSID[3:0] (Si3018 = 1, Si3019 = 3), REVA[3:0]
#define SI_LSID(r) ((r) >> 4)
#define SI_STATUS 12             // FDT (bit 6), LCS[4:0]
#define SI_STATUS_FDT 0x40
#define SI_REVB 13
#define SI_LCS2 28
#define SI_LVS 29

// MCLK 48 MHz (the RP2040's USB clock): F_BASE = 48 MHz x 256 / 125 = 98.304 MHz.
#define SI_MCLK_N 125
#define SI_MCLK_M 256

// Control words in the secondary frame (Figures 29, 30): bit 15 = read,
// bits 14:8 the register, bits 7:0 the value (a write) or don't care.
static inline unsigned short si_ctl_read(unsigned reg) { return (unsigned short)(0x8000u | (reg & 0x7Fu) << 8); }
// A secondary-frame word writing value to register reg.
static inline unsigned short si_ctl_write(unsigned reg, unsigned value) {
    return (unsigned short)((reg & 0x7Fu) << 8 | (value & 0xFFu));
}
