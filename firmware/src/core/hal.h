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

uint32_t hal_millis(void);

// Transceivers: PS_ROLE_NONE = both shut down; DCE = chip A on, B off; DTE = B on, A off.
void hal_xcvr_power(uint8_t role);
// Peer detection, valid even while both chips are shut down.
bool hal_peer_dce(void);   // chip A STATUS: a computer drives pins 2/4/20
bool hal_peer_dte(void);   // chip B INVALID: a modem drives pins 3/5/6/8/22
uint8_t hal_detect_bits(void);   // PS_DETECT_*

// UART: which chip's pins it is on, its format, and flow control.
void hal_uart_route(uint8_t role);
void hal_uart_format(const uart_format_t *f);
void hal_uart_hw_cts(bool on);          // obey the far end's flow-control line
void hal_uart_break(bool on);
void hal_uart_kick_tx(void);            // data was queued for the UART

// Modem lines for the active role.
void hal_lines_out(uint8_t role, uint8_t asserted);   // DCE: DCD DSR RI CTS; DTE: DTR RTS
uint8_t hal_lines_in(uint8_t role);                   // DCE: DTR RTS; DTE: DCD DSR RI CTS

void hal_leds(uint8_t mask);            // CORE_LED_* (boards without LEDs ignore it)

// Persistent settings (a small blob); load returns false if there is none.
bool hal_settings_load(void *buf, uint32_t len);
void hal_settings_save(const void *buf, uint32_t len);

// Interrupt masking around state shared with the UART interrupt.
uint32_t hal_irq_save(void);
void hal_irq_restore(uint32_t state);
