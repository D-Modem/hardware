/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// What the portable core needs from the hardware. Implemented for the RP2040
// in src/rp2040/hal_rp2040.c and faked for the host tests in test/fake_hal.c.
//
// Line arguments use PS_LINE_* with 1 = asserted; the HAL does the inversion
// the RS-232 transceivers add.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "protocol.h"

typedef struct {
    uint32_t baud;
    uint8_t data_bits;   // 5..8
    uint8_t stop_bits;   // 1 or 2
    uint8_t parity;      // 0 none, 1 odd, 2 even
} uart_format_t;

// Milliseconds since boot; wraps after about 49 days (callers compare differences).
uint32_t hal_millis(void);

// Transceivers: PS_ROLE_NONE = both shut down; DCE = chip A on, B off; DTE = B on, A off.
void hal_xcvr_power(uint8_t role);
// Peer detection, valid even while both chips are shut down.
bool hal_peer_dce(void);   // chip A STATUS: a computer drives pins 2/4/20
bool hal_peer_dte(void);   // chip B INVALID: a modem drives pins 3/5/6/8/22
uint8_t hal_detect_bits(void);   // PS_DETECT_*

// UART: which chip's pins it is on, its format, and flow control.
void hal_uart_route(uint8_t role);
// Set the UART's baud rate, data bits, parity and stop bits.
void hal_uart_format(const uart_format_t *f);
// Obey the far end's flow-control line in hardware: the UART pauses sending
// while it's deasserted.
void hal_uart_hw_cts(bool on);
// Hold the TX line in the break (space) state, or release it.
void hal_uart_break(bool on);
// Data was queued for the UART: start sending if it's idle (it pulls more
// with core_isr_tx from its interrupt).
void hal_uart_kick_tx(void);
// Drop what the UART has received (call with interrupts off).
void hal_uart_rx_flush(void);

// Baud-rate learning: time the RX line of this role's chip (PS_ROLE_NONE
// stops). From the first falling edge after the line is high, report each
// run of one level with core_isr_run(), in ticks of hal_autobaud_hz()
// (0 = too long to count).
void hal_autobaud(uint8_t role);
// The timer rate hal_autobaud reports run lengths in, ticks per second.
uint32_t hal_autobaud_hz(void);

// Modem lines for the active role.
void hal_lines_out(uint8_t role, uint8_t asserted);   // DCE: DCD DSR RI CTS; DTE: DTR RTS
uint8_t hal_lines_in(uint8_t role);                   // DCE: DTR RTS; DTE: DCD DSR RI CTS

// Show the status LEDs: CORE_LED_* bits, 1 = on (boards without LEDs ignore it).
void hal_leds(uint8_t mask);

// Persistent settings (a small blob); load returns false if there is none.
bool hal_settings_load(void *buf, uint32_t len);
// Write the blob to flash (rare: the core batches changes).
void hal_settings_save(const void *buf, uint32_t len);

// Interrupt masking around state shared with the UART interrupt.
uint32_t hal_irq_save(void);
// Re-enable interrupts as they were before the matching hal_irq_save.
void hal_irq_restore(uint32_t state);
