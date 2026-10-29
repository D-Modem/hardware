/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// What wb_codec.c and wb_dev.c need from the WB stick's hardware.
// Implemented for the RP2040 in src/wb/codec.c and faked for the host tests
// in test/test_wb.c.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Milliseconds since boot (wraps; callers compare differences).
uint32_t whal_millis(void);
// Wait ms milliseconds (the watchdog kept fed).
void whal_sleep_ms(uint32_t ms);
// The codec's RESET pin low for a while, then high: a hardware reset with
// MCLK (12 MHz) running. Its registers can be written 1 ms later.
void whal_codec_reset(void);
// A register of the codec's, on the page selected last (I2C); false if the
// codec didn't answer.
bool whal_codec_write(uint8_t reg, uint8_t v);
// Read a register on the page selected last; false if the codec didn't answer.
bool whal_codec_read(uint8_t reg, uint8_t *v);
// The codec's INT1 (on MFP5) is asserted: a sticky flag wants reading.
bool whal_codec_int(void);
// Start the I2S slave (PIO and DMA) on the codec's clocks, the streams'
// head start set for the rate; the streams stay open if they were.
void whal_i2s_start(uint32_t rate);
// Stop the I2S slave (the codec is about to be reset or reclocked).
void whal_i2s_stop(void);
// The I2S slave lost its place in the frames since the last call (it has
// been started again).
bool whal_i2s_lost(void);
// Frames received since boot (wraps): the codec's clocks run while it rises.
uint32_t whal_frames(void);
// Whether the host has the sample stream open (USB alternate setting 1).
bool whal_streaming(void);
// The sample streams' counters (sstream_t).
void whal_counters(uint16_t *underruns, uint16_t *overruns, uint32_t *in, uint32_t *out);
// The largest |sample| of the left ADC, the right ADC and the host's samples
// since the last call (wb_audio_t.peak); then cleared.
void whal_peaks(uint16_t peak[3]);
// Non-silent samples of the host's played so far (wraps).
uint32_t whal_tx_active(void);
// Which ADC channel goes to the host (WP_INPUT_*).
void whal_set_input(uint8_t input);
// Firmware loopback: the host's samples come back to it instead of the line's.
void whal_set_loopback(bool on);
// The status LEDs.
void whal_leds(bool link, bool data);
// Into the UF2 loader (doesn't return on the board).
void whal_reboot_uf2(void);
