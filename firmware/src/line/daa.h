/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The line stick's DAA on the RP2040: the Si3056's pins, its clock, its
// serial frames (PIO and an interrupt) and the sample streams. Implements
// line_hal.h.
#pragma once
#include <stdbool.h>
#include "daa_frame.h"

// The line's sample streams (USB reads and writes them; the interrupt runs them).
extern line_stream_t g_stream;

// Pins, LEDs and MCLK (48 MHz on GPIO21, from the USB clock). Leaves the DAA
// in reset, on hook. Call once at startup, before line_dev_init().
void daa_hw_init(void);
// The host opened (alternate setting 1) or closed the stream: the FIFOs emptied.
void daa_stream(bool on);
// The modem's samples from USB OUT, into the stream (overruns counted).
void daa_from_host(const int16_t *s, uint32_t n);
