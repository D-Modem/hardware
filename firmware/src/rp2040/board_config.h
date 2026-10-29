/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// Per-board choices the netlist doesn't decide: LEDs, names, IDs.
#pragma once
#if defined(PS_BOARD_W)
#include "generated/board_pins_w.h"
#elif defined(PS_BOARD_LINE)
#include "generated/board_pins_line.h"
#elif defined(PS_BOARD_WB)
#include "generated/board_pins_wb.h"
#else
#include "generated/board_pins.h"
#endif

// USB IDs. 1209:0001, 1209:0002 and 1209:0003 are pid.codes' shared *test*
// PIDs: fine on the bench, not for boards given to other people. Apply for a
// PID (Raspberry Pi's program under VID 2E8A, or pid.codes) and set it here
// before release. The line stick and the wideband stick each have their own,
// being other kinds of device (no CDC port; their driver finds them by
// VID:PID, as dmodemd does the SmartUSB56).
#ifndef PS_USB_VID
#define PS_USB_VID 0x1209
#if defined(PS_BOARD_LINE)
#define PS_USB_PID 0x0002
#elif defined(PS_BOARD_WB)
#define PS_USB_PID 0x0003
#else
#define PS_USB_PID 0x0001
#endif
#endif

#define LED_NONE 0xFF

#if defined(PS_BOARD_PICO)
// Bench rig: a Raspberry Pi Pico wired to the transceivers as SPEC.md 4.5.
// GP23 (SMPS mode) and GP24 (VBUS sense) belong to the Pico and stay untouched.
#define PS_BOARD_ID 2
#define PS_PRODUCT "pico-serial bench (Pico)"
#define LED_PIN_PWR 25   // the Pico's own LED
#define LED_PIN_TR 22
#define LED_PIN_CD 26
#define LED_PIN_SD 27
#define LED_PIN_RD 28
#define LED_PIN_RI LED_NONE
#elif defined(PS_BOARD_LINE)
// pico-serial Line, the phone-line stick (SPEC-L.md): two LEDs, OH and RI
// (src/line/daa.c drives them); no RS-232.
#define PS_BOARD_ID 3
#define PS_PRODUCT "pico-serial Line"
#elif defined(PS_BOARD_WB)
// pico-serial WB, the wideband bare-pair stick (SPEC-WB.md): two LEDs, LINK
// and DATA (src/wb/codec.c drives them); no RS-232.
#define PS_BOARD_ID 4
#define PS_PRODUCT "pico-serial WB"
#elif defined(PS_BOARD_W)
// pico-serial W (SPEC-W.md). LEDs fitted. PWR/BT shows the Bluetooth state
// (bt.c); RD and SD are on the RM2's own GPIOs (WLGPIO_LED_*), driven
// through the radio; there's no RI LED.
#define PS_BOARD_ID 1
#define PS_PRODUCT "pico-serial W"
#define LED_PIN_PWR LED_NONE
#define LED_PIN_TR PIN_LED_TR
#define LED_PIN_CD PIN_LED_CD
#define LED_PIN_SD LED_NONE
#define LED_PIN_RD LED_NONE
#define LED_PIN_RI LED_NONE
#else
// DB25 hood board. LEDs are optional (not fitted by default); driving the
// pins does no harm either way.
#define PS_BOARD_ID 0
#define PS_PRODUCT "pico-serial DB25"
#define LED_PIN_PWR PIN_LED_PWR
#define LED_PIN_TR PIN_LED_TR
#define LED_PIN_CD PIN_LED_CD
#define LED_PIN_SD PIN_LED_SD
#define LED_PIN_RD PIN_LED_RD
#define LED_PIN_RI PIN_LED_RI
#endif

#define PS_MANUFACTURER "pico-serial"
