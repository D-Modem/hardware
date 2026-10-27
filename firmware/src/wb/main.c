/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// pico-serial WB firmware: the wideband bare-pair stick's USB front end for
// the TLV320AIC3204 codec (SPEC-WB.md). The modem itself is on the host
// (D-Modem).
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "codec.h"
#include "usb_stick.h"
#include "wb_stick.h"

// Brings up the codec at 96 kHz, then USB, then runs both from one loop.
// The watchdog resets the stick if the loop stalls for a second (the codec's
// RESET is pulled down: it goes quiet).
int main(void) {
    codec_hw_init();
    wb_dev_init(&g_wb, 96000);
    usb_stick_init();
    watchdog_enable(1000, true);
    for (;;) {
        usb_stick_service();
        watchdog_update();
    }
}
