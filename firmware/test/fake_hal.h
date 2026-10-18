/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
#pragma once
#include "../src/core/hal.h"

typedef struct {
    uint32_t now;
    uint8_t xcvr, route;
    int xcvr_calls;
    bool peer_dce, peer_dte;
    uart_format_t format;
    int format_calls;
    bool hw_cts, brk;
    int kicks;
    uint8_t out_role, out, in;
    uint8_t leds;
    uint8_t flash[64];
    bool flash_valid;
    int saves;
} fake_hw_t;

extern fake_hw_t hw;
void fake_reset(void);
