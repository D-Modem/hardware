/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// What line_dev.c needs from the stick's hardware. Implemented for the
// RP2040 in src/line/daa.c and faked for the host tests in test/test_line.c.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Milliseconds since boot (wraps; callers compare differences).
uint32_t lhal_millis(void);
// Reset the Si3056, start its serial frames, set the PLL, the rate (SRC
// code), 16-bit mode, and power up the line side; false if the frames
// didn't run (no Si3056, no clock). Whether the line side answers is
// register 12's FDT bit, which line_dev.c reads.
bool lhal_daa_start(int src);
// A register of the DAA's, through a secondary frame (waits a few frames);
// false if the access didn't complete.
bool lhal_daa_read(uint8_t reg, uint8_t *v);
// Write a register of the DAA's, through a secondary frame; false on a timeout.
bool lhal_daa_write(uint8_t reg, uint8_t v);
// The hook: the OFHK pin (true = off hook).
void lhal_hook(bool off);
// The ring detector now (RGDT active).
bool lhal_ring(void);
// The ring detector's changes so far, mod 256.
uint8_t lhal_ring_edges(void);
// The DAA's interrupt pin is asserted.
bool lhal_int(void);
// Whether the host has the sample stream open (USB alternate setting 1).
bool lhal_streaming(void);
// The sample streams' counters (sstream_t).
void lhal_counters(uint16_t *underruns, uint16_t *overruns, uint32_t *in, uint32_t *out);
// The status LEDs: off hook, ringing.
void lhal_leds(bool off_hook, bool ringing);
// Into the UF2 loader (doesn't return on the board).
void lhal_reboot_uf2(void);
