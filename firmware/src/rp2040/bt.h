/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// Bluetooth on pico-serial W (SPEC-W.md 4): classic SPP and BLE (Nordic UART
// Service plus the pico-serial control service) as host links (link.h).
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Starts the radio and BTstack: SPP server, GATT server, pairing rules.
// Call once, after core_init() and links_init(), and before USB and the
// watchdog start: loading the radio's firmware blocks for a while. Returns
// false if the radio didn't start; the board then works over USB only.
bool bt_init(void);
// Polls the radio and runs every Bluetooth callback, the pairing button and
// the BT LED. Call from the main loop, never from an interrupt.
void bt_service(void);
// Opens the pairing window for `seconds` (0 closes it). From the USB
// request PS_REQ_BT_PAIRING; the button opens it for 2 minutes.
void bt_pairing_window(uint16_t seconds);
// Forgets every bonded phone (classic and LE) and opens the pairing window.
// From the USB request PS_REQ_BT_FORGET, or holding the button 10 s.
void bt_forget_all(void);
// Shows the core's RD and SD LED bits (CORE_LED_*) on the LEDs wired to the
// RM2's own GPIOs; the other LED bits are ignored. Called from hal_leds().
void bt_leds(uint8_t core_leds);
