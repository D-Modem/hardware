/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// pico-serial firmware: USB CDC serial port (and on the W board, Bluetooth)
// <-> RS-232 DB25, DCE/DTE auto-detect.
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "core.h"
#include "hal_rp2040.h"
#include "link.h"
#include "usb.h"
#if defined(PS_BOARD_W)
#include "bt.h"
#endif

int main(void) {
    hal_init();
    core_init(&g_core);
    links_init(&g_links, &g_core);
#if defined(PS_BOARD_W)
    bt_init();   // loads the radio's firmware (blocking): before USB and the watchdog start
#endif
    usb_init();
    watchdog_enable(1000, true);
    for (;;) {
        usb_service();
#if defined(PS_BOARD_W)
        bt_service();
#endif
        core_poll(&g_core);
        watchdog_update();
    }
}
