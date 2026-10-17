// A pretend board for the host tests: records what the core asks of the
// hardware and lets a test set what the hardware reports.
#include "fake_hal.h"
#include <string.h>

fake_hw_t hw;

void fake_reset(void) {
    memset(&hw, 0, sizeof hw);
    hw.now = 1000;
    hw.xcvr = 0xFF;    // "never set"
    hw.route = 0xFF;
}

uint32_t hal_millis(void) { return hw.now; }
void hal_xcvr_power(uint8_t role) { hw.xcvr = role; hw.xcvr_calls++; }
bool hal_peer_dce(void) { return hw.peer_dce; }
bool hal_peer_dte(void) { return hw.peer_dte; }
uint8_t hal_detect_bits(void) {
    return (uint8_t)((hw.peer_dce ? PS_DETECT_A_STATUS : 0) | (hw.peer_dte ? PS_DETECT_B_INVALID : 0));
}
void hal_uart_route(uint8_t role) { hw.route = role; }
void hal_uart_format(const uart_format_t *f) { hw.format = *f; hw.format_calls++; }
void hal_uart_hw_cts(bool on) { hw.hw_cts = on; }
void hal_uart_break(bool on) { hw.brk = on; }
void hal_uart_kick_tx(void) { hw.kicks++; }
void hal_lines_out(uint8_t role, uint8_t asserted) { hw.out_role = role; hw.out = asserted; }
uint8_t hal_lines_in(uint8_t role) { (void)role; return hw.in; }
void hal_leds(uint8_t mask) { hw.leds = mask; }
bool hal_settings_load(void *buf, uint32_t len) {
    if (!hw.flash_valid || len > sizeof hw.flash) return false;
    memcpy(buf, hw.flash, len);
    return true;
}
void hal_settings_save(const void *buf, uint32_t len) {
    memcpy(hw.flash, buf, len);
    hw.flash_valid = true;
    hw.saves++;
}
uint32_t hal_irq_save(void) { return 0; }
void hal_irq_restore(uint32_t s) { (void)s; }
