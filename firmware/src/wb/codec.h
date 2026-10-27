/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The WB stick's codec on the RP2040: its pins, MCLK, I2C, and its I2S
// frames (PIO and DMA) with the samples through wb_audio.c. Implements
// wb_hal.h.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wb_audio.h"

// The samples (USB reads and writes their streams; the DMA interrupt runs them).
extern wb_audio_t g_audio;

// Pins, LEDs, MCLK (the 12 MHz crystal on GPIO21), I2C1, the PIO program
// and the DMA channels. Leaves the codec in reset. Call once at startup,
// before wb_dev_init().
void codec_hw_init(void);
// The host opened (alternate setting 1) or closed the stream: the FIFOs emptied.
void codec_stream(bool on);
// The modem's samples from USB OUT, into the stream (overruns counted).
void codec_from_host(const int16_t *s, uint32_t n);
