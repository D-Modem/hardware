/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// Bluetooth on pico-serial W (SPEC-W.md 4): classic SPP and BLE (Nordic UART
// Service plus the pico-serial control service) as host links (link.h).
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool bt_init(void);                      // after core_init/links_init; false: no radio
void bt_service(void);                   // call from the main loop
void bt_pairing_window(uint16_t seconds);   // 0 closes it
void bt_forget_all(void);                // forget every bonded phone
void bt_leds(uint8_t core_leds);         // CORE_LED_RD/SD to the RM2's GPIOs
