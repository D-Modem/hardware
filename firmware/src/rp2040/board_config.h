// Per-board choices the netlist doesn't decide: LEDs, names, IDs.
#pragma once
#include "generated/board_pins.h"

// USB IDs. 1209:0001 is pid.codes' shared *test* PID: fine on the bench,
// not for boards given to other people. Apply for a PID (Raspberry Pi's
// program under VID 2E8A, or pid.codes) and set it here before release.
#ifndef PS_USB_VID
#define PS_USB_VID 0x1209
#define PS_USB_PID 0x0001
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
