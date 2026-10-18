/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// pico-serial firmware: USB CDC serial port <-> RS-232 DB25, DCE/DTE auto-detect.
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "core.h"
#include "hal_rp2040.h"
#include "usb.h"

int main(void) {
    hal_init();
    core_init(&g_core);
    usb_init();
    watchdog_enable(1000, true);
    for (;;) {
        usb_service();
        core_poll(&g_core);
        watchdog_update();
    }
}
