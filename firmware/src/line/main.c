/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// pico-serial Line firmware: the phone-line stick's USB front end for the
// Si3056 DAA (SPEC-L.md). The modem itself is on the host (D-Modem).
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "daa.h"
#include "line_stick.h"
#include "usb_stick.h"

// Brings up the DAA at 9600 Hz on hook (V.90 needs it), then USB, then runs
// both from one loop. The watchdog resets the stick if the loop stalls for a
// second (the DAA then goes on hook: the OFHK pin is pulled up).
int main(void) {
    daa_hw_init();
    line_dev_init(&g_line, 9600);
    usb_stick_init();
    watchdog_enable(1000, true);
    for (;;) {
        usb_stick_service();
        watchdog_update();
    }
}
