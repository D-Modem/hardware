/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// A pretend board for the host tests: records what the core asks of the
// hardware and lets a test set what the hardware reports.
#include "fake_hal.h"
#include <string.h>

fake_hw_t hw;

// Clears every record and setting, then starts the clock at 1000 ms with the
// transceiver power and UART routing marked "never set" (0xFF).
void fake_reset(void) {
    memset(&hw, 0, sizeof hw);
    hw.now = 1000;
    hw.xcvr = 0xFF;    // "never set"
    hw.route = 0xFF;
}

// The test's clock: hw.now, which tests advance by hand.
uint32_t hal_millis(void) { return hw.now; }
// Records which transceiver was powered, and counts the calls.
void hal_xcvr_power(uint8_t role) { hw.xcvr = role; hw.xcvr_calls++; }
// Reports a computer on the line when the test set hw.peer_dce.
bool hal_peer_dce(void) { return hw.peer_dce; }
// Reports a modem on the line when the test set hw.peer_dte.
bool hal_peer_dte(void) { return hw.peer_dte; }
// The raw detect bits the real board would report for hw.peer_dce / peer_dte.
uint8_t hal_detect_bits(void) {
    return (uint8_t)((hw.peer_dce ? PS_DETECT_A_STATUS : 0) | (hw.peer_dte ? PS_DETECT_B_INVALID : 0));
}
// Records which chip the UART was routed to.
void hal_uart_route(uint8_t role) { hw.route = role; }
// Records the UART format last set, and counts the calls.
void hal_uart_format(const uart_format_t *f) { hw.format = *f; hw.format_calls++; }
// Records whether hardware CTS flow control is on.
void hal_uart_hw_cts(bool on) { hw.hw_cts = on; }
// Records whether a break is being sent.
void hal_uart_break(bool on) { hw.brk = on; }
// Counts the times the core said data was queued for the UART.
void hal_uart_kick_tx(void) { hw.kicks++; }
// Counts the times the core dropped the UART's received data.
void hal_uart_rx_flush(void) { hw.rx_flushes++; }
// Records whose RX line baud learning is timing, and counts the calls.
void hal_autobaud(uint8_t role) { hw.ab_role = role; hw.ab_calls++; }
// The capture clock the tests' waveforms use: FAKE_AB_HZ (the RP2040's 62.5 MHz).
uint32_t hal_autobaud_hz(void) { return FAKE_AB_HZ; }
// Records the modem lines driven, and for which role.
void hal_lines_out(uint8_t role, uint8_t asserted) { hw.out_role = role; hw.out = asserted; }
// Returns the far end's modem lines as the test set them in hw.in.
uint8_t hal_lines_in(uint8_t role) { (void)role; return hw.in; }
// Records the LED mask.
void hal_leds(uint8_t mask) { hw.leds = mask; }
// Copies out the fake flash, if something was saved (or a test put it there).
bool hal_settings_load(void *buf, uint32_t len) {
    if (!hw.flash_valid || len > sizeof hw.flash) return false;
    memcpy(buf, hw.flash, len);
    return true;
}
// Copies the settings into the fake flash and counts the saves.
void hal_settings_save(const void *buf, uint32_t len) {
    memcpy(hw.flash, buf, len);
    hw.flash_valid = true;
    hw.saves++;
}
// No interrupts on the host: nothing to mask.
uint32_t hal_irq_save(void) { return 0; }
// No interrupts on the host: nothing to restore.
void hal_irq_restore(uint32_t s) { (void)s; }
