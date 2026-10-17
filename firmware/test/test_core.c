// Host tests for the serial core: role detection, line mapping, flow control,
// SERIAL_STATE, line coding, break, vendor requests, settings, ring buffer.
// Build and run: make -C firmware/test
#include <stdio.h>
#include <string.h>
#include "../src/core/core.h"
#include "fake_hal.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define TEST(name) static void name(void)
#define RUN(name) do { printf("%s\n", #name); fake_reset(); name(); } while (0)

static core_t c;

static void run_ms(uint32_t ms) {   // advance time, polling every 5 ms
    for (uint32_t t = 0; t < ms; t += 5) { hw.now += 5; core_poll(&c); }
}

static void start_auto(void) {
    core_init(&c);
    CHECK(c.role == PS_ROLE_NONE);
    CHECK(hw.xcvr == PS_ROLE_NONE);
}

TEST(detects_computer_as_dce) {
    start_auto();
    hw.peer_dce = true;
    run_ms(CORE_SETTLE_MS + CORE_DETECT_PERIOD_MS);   // one sample isn't enough
    CHECK(c.role == PS_ROLE_NONE);
    run_ms(CORE_DETECT_PERIOD_MS * CORE_DETECT_CONFIRM);
    CHECK(c.role == PS_ROLE_DCE);
    CHECK(hw.xcvr == PS_ROLE_DCE);
    CHECK(hw.route == PS_ROLE_DCE);
    CHECK(hw.format_calls >= 1);
}

TEST(detects_modem_as_dte) {
    start_auto();
    hw.peer_dte = true;
    run_ms(500);
    CHECK(c.role == PS_ROLE_DTE);
    CHECK(hw.xcvr == PS_ROLE_DTE);
}

TEST(computer_wins_when_both_look_present) {
    start_auto();
    hw.peer_dce = hw.peer_dte = true;
    run_ms(500);
    CHECK(c.role == PS_ROLE_DCE);
}

TEST(glitch_does_not_switch_on) {
    start_auto();
    run_ms(200);
    hw.peer_dce = true; run_ms(CORE_DETECT_PERIOD_MS);   // one sample
    hw.peer_dce = false; run_ms(500);
    CHECK(c.role == PS_ROLE_NONE);
}

TEST(unplug_returns_to_detect) {
    start_auto();
    hw.peer_dce = true; run_ms(500);
    CHECK(c.role == PS_ROLE_DCE);
    hw.peer_dce = false;
    run_ms(CORE_LOST_MS - 50);
    CHECK(c.role == PS_ROLE_DCE);   // brief loss tolerated
    run_ms(100);
    CHECK(c.role == PS_ROLE_NONE);
    CHECK(hw.xcvr == PS_ROLE_NONE);
    CHECK(hw.route == PS_ROLE_NONE);
    hw.peer_dte = true; run_ms(500);   // now a modem
    CHECK(c.role == PS_ROLE_DTE);
}

TEST(short_loss_recovers) {
    start_auto();
    hw.peer_dce = true; run_ms(500);
    hw.peer_dce = false; run_ms(400);
    hw.peer_dce = true; run_ms(5);
    hw.peer_dce = false; run_ms(800);   // a fresh 800 ms: still under the limit
    CHECK(c.role == PS_ROLE_DCE);
}

TEST(forced_mode_ignores_detection) {
    core_settings_t s = {PS_MODE_DTE, PS_FLOW_AUTO};
    uint8_t blob[CORE_SETTINGS_BLOB];
    core_settings_pack(&s, blob);
    memcpy(hw.flash, blob, sizeof blob); hw.flash_valid = true;
    core_init(&c);
    CHECK(c.role == PS_ROLE_DTE);
    hw.peer_dce = true; run_ms(3000);
    CHECK(c.role == PS_ROLE_DTE);
}

TEST(dce_line_mapping) {
    start_auto();
    hw.peer_dce = true; run_ms(500);
    run_ms(5);
    CHECK(hw.out == 0);
    core_host_lines(&c, true, false); run_ms(5);
    CHECK(hw.out == (PS_LINE_DCD | PS_LINE_DSR));     // host DTR = carrier
    core_host_lines(&c, true, true); run_ms(5);
    CHECK(hw.out == (PS_LINE_DCD | PS_LINE_DSR | PS_LINE_CTS));
    core_host_lines(&c, false, true); run_ms(5);
    CHECK(hw.out == PS_LINE_CTS);
}

TEST(dte_line_mapping) {
    start_auto();
    hw.peer_dte = true; run_ms(500);
    core_host_lines(&c, true, true); run_ms(5);
    CHECK(hw.out == (PS_LINE_DTR | PS_LINE_RTS));
    CHECK(hw.out_role == PS_ROLE_DTE);
}

TEST(overrides) {
    start_auto();
    hw.peer_dce = true; run_ms(500);
    core_host_lines(&c, true, true);
    CHECK(core_vendor_out(&c, PS_REQ_SET_OUTPUTS, (uint16_t)((PS_LINE_RI | PS_LINE_DSR) << 8 | PS_LINE_RI)));
    run_ms(5);
    CHECK(hw.out == (PS_LINE_DCD | PS_LINE_RI | PS_LINE_CTS));   // RI on, DSR forced off
    CHECK(core_vendor_out(&c, PS_REQ_SET_OUTPUTS, 0)); run_ms(5);
    CHECK(hw.out == (PS_LINE_DCD | PS_LINE_DSR | PS_LINE_CTS));
}

TEST(full_buffer_holds_the_far_end) {
    start_auto();
    hw.peer_dce = true; run_ms(500);
    core_host_lines(&c, false, true); run_ms(5);
    CHECK(hw.out & PS_LINE_CTS);
    for (uint32_t i = 0; i < RING_SIZE * 3 / 4; i++) core_isr_rx(&c, 'x', 0);
    run_ms(5);
    CHECK(!(hw.out & PS_LINE_CTS));
    uint8_t buf[256];
    while (ring_count(&c.to_host) > RING_SIZE / 2 + 10) core_host_read(&c, buf, sizeof buf < 10 ? 1 : 10);
    run_ms(5);
    CHECK(!(hw.out & PS_LINE_CTS));   // hysteresis
    core_host_read(&c, buf, 20); run_ms(5);
    CHECK(hw.out & PS_LINE_CTS);
}

TEST(flow_auto_latches) {
    start_auto();
    hw.peer_dce = true; run_ms(500);
    CHECK(!hw.hw_cts);                 // 3-wire cable: never obey an unused line
    hw.in = PS_LINE_RTS; run_ms(5);    // the computer uses RTS
    CHECK(hw.hw_cts);
    hw.in = 0; run_ms(5);
    CHECK(hw.hw_cts);                  // ...and it stays obeyed
    hw.peer_dce = false; run_ms(CORE_LOST_MS + 200);
    CHECK(!hw.hw_cts);                 // forgotten when the peer goes
}

TEST(flow_settings) {
    start_auto();
    hw.peer_dte = true; run_ms(500);
    CHECK(core_vendor_out(&c, PS_REQ_SET_FLOW, PS_FLOW_ON)); run_ms(5);
    CHECK(hw.hw_cts);
    CHECK(core_vendor_out(&c, PS_REQ_SET_FLOW, PS_FLOW_OFF));
    hw.in = PS_LINE_CTS; run_ms(5);
    CHECK(!hw.hw_cts);
    CHECK(!core_vendor_out(&c, PS_REQ_SET_FLOW, 7));
}

TEST(serial_state_dce) {
    start_auto();
    uint16_t s;
    CHECK(core_serial_state(&c, &s) && s == 0);
    CHECK(!core_serial_state(&c, &s));            // unchanged: nothing to send
    hw.peer_dce = true; run_ms(500);
    CHECK(core_serial_state(&c, &s) && s == SS_DCD);
    hw.in = PS_LINE_DTR; run_ms(5);
    CHECK(core_serial_state(&c, &s) && s == (SS_DCD | SS_DSR));   // computer's DTR shows as DSR
}

TEST(serial_state_dte_and_events) {
    start_auto();
    hw.peer_dte = true; run_ms(500);
    hw.in = PS_LINE_DCD | PS_LINE_DSR | PS_LINE_RI; run_ms(5);
    uint16_t s;
    CHECK(core_serial_state(&c, &s) && s == (SS_DCD | SS_DSR | SS_RI));
    core_isr_rx(&c, 0, RX_ERR_FRAMING | RX_ERR_OVERRUN);
    CHECK(core_serial_state(&c, &s) && s == (SS_DCD | SS_DSR | SS_RI | SS_FRAMING | SS_OVERRUN));
    CHECK(!core_serial_state(&c, &s));            // events are one-shot
    core_isr_rx(&c, 0, RX_ERR_BREAK);
    CHECK(core_serial_state(&c, &s) && (s & SS_BREAK));
    CHECK(c.stats.breaks == 1 && c.stats.framing == 1 && c.stats.overrun == 1);
}

TEST(line_coding) {
    start_auto();
    hw.peer_dce = true; run_ms(500);
    core_host_format(&c, 9600, 0, 2, 7);
    CHECK(hw.format.baud == 9600 && hw.format.parity == 2 && hw.format.data_bits == 7 && hw.format.stop_bits == 1);
    CHECK(!(c.info_flags & PS_INFO_CODING_ADJUSTED));
    core_host_format(&c, 2400, 1, 3, 8);   // 1.5 stop, mark parity: the UART can't
    CHECK(hw.format.stop_bits == 2 && hw.format.parity == 0);
    CHECK(c.info_flags & PS_INFO_CODING_ADJUSTED);
    core_host_format(&c, 460800, 0, 0, 8);
    CHECK(c.info_flags & PS_INFO_BAUD_OVER_RATING);
    CHECK(!(c.info_flags & PS_INFO_CODING_ADJUSTED));
}

TEST(break_timing) {
    start_auto();
    hw.peer_dce = true; run_ms(500);
    core_host_break(&c, 250);
    CHECK(hw.brk);
    run_ms(200); CHECK(hw.brk);
    run_ms(60); CHECK(!hw.brk);
    core_host_break(&c, 0xFFFF); run_ms(2000);
    CHECK(hw.brk);
    core_host_break(&c, 0);
    CHECK(!hw.brk);
}

TEST(data_both_ways) {
    start_auto();
    hw.peer_dce = true; run_ms(500);
    const uint8_t msg[] = "ATDT5551234\r";
    CHECK(core_host_write(&c, msg, sizeof msg - 1) == sizeof msg - 1);
    CHECK(hw.kicks > 0);
    uint8_t b, out[32]; int n = 0;
    while (core_isr_tx(&c, &b)) out[n++] = b;
    CHECK(n == (int)sizeof msg - 1 && memcmp(out, msg, sizeof msg - 1) == 0);
    core_isr_rx(&c, 'O', 0); core_isr_rx(&c, 'K', 0);
    CHECK(core_host_read(&c, out, sizeof out) == 2 && out[0] == 'O' && out[1] == 'K');
    CHECK(c.stats.to_rs232 == sizeof msg - 1 && c.stats.from_rs232 == 2);
}

TEST(vendor_info_and_mode) {
    start_auto();
    uint8_t buf[64];
    int n = core_vendor_in(&c, PS_REQ_GET_INFO, 0, buf, sizeof buf);
    CHECK(n == (int)sizeof(ps_info_t));
    ps_info_t i; memcpy(&i, buf, sizeof i);
    CHECK(i.protocol == PS_PROTOCOL_VERSION && i.mode == PS_MODE_AUTO && i.role == PS_ROLE_NONE && i.baud == 115200);
    CHECK(core_vendor_in(&c, 0x7F, 0, buf, sizeof buf) == -1);
    CHECK(!core_vendor_out(&c, PS_REQ_SET_MODE, 3));
    CHECK(core_vendor_out(&c, PS_REQ_SET_MODE, PS_MODE_DCE));
    CHECK(c.role == PS_ROLE_DCE && hw.xcvr == PS_ROLE_DCE);   // switches at once
    run_ms(CORE_SAVE_DELAY_MS + 20);
    CHECK(hw.saves == 1);
    core_settings_t s;
    CHECK(core_settings_unpack(&s, hw.flash) && s.mode == PS_MODE_DCE);
    run_ms(2000);
    CHECK(hw.saves == 1);   // not rewritten
    CHECK(core_vendor_out(&c, PS_REQ_SET_MODE, PS_MODE_AUTO));
    CHECK(c.role == PS_ROLE_NONE);
    n = core_vendor_in(&c, PS_REQ_GET_INPUTS, 0, buf, sizeof buf);
    CHECK(n == (int)sizeof(ps_inputs_t));
}

TEST(settings_blob) {
    core_settings_t a = {PS_MODE_DTE, PS_FLOW_OFF}, b = {0, 0};
    uint8_t blob[CORE_SETTINGS_BLOB];
    core_settings_pack(&a, blob);
    CHECK(core_settings_unpack(&b, blob) && b.mode == PS_MODE_DTE && b.flow == PS_FLOW_OFF);
    blob[5] ^= 1;
    CHECK(!core_settings_unpack(&b, blob));       // checksum catches it
    memset(blob, 0xFF, sizeof blob);              // erased flash
    CHECK(!core_settings_unpack(&b, blob));
    hw.flash_valid = true; memset(hw.flash, 0xFF, sizeof hw.flash);
    core_init(&c);
    CHECK(c.settings.mode == PS_MODE_AUTO && c.settings.flow == PS_FLOW_AUTO);   // defaults
}

TEST(ring_wraps) {
    static ring_t r;
    ring_init(&r);
    uint8_t b;
    for (uint32_t round = 0; round < 3; round++) {
        for (uint32_t i = 0; i < RING_SIZE; i++) CHECK(ring_put(&r, (uint8_t)i));
        CHECK(!ring_put(&r, 0));
        for (uint32_t i = 0; i < RING_SIZE; i++) CHECK(ring_get(&r, &b) && b == (uint8_t)i);
        CHECK(!ring_get(&r, &b));
    }
}

TEST(leds) {
    start_auto();
    run_ms(5);
    CHECK(hw.leds == CORE_LED_PWR);
    hw.peer_dce = true; run_ms(500);
    hw.in = PS_LINE_DTR; core_host_lines(&c, true, false);
    core_isr_rx(&c, 'a', 0);
    run_ms(5);
    CHECK(hw.leds == (CORE_LED_PWR | CORE_LED_TR | CORE_LED_CD | CORE_LED_RD));
    run_ms(CORE_LED_PULSE_MS + 10);
    CHECK(!(hw.leds & CORE_LED_RD));
}

int main(void) {
    RUN(detects_computer_as_dce);
    RUN(detects_modem_as_dte);
    RUN(computer_wins_when_both_look_present);
    RUN(glitch_does_not_switch_on);
    RUN(unplug_returns_to_detect);
    RUN(short_loss_recovers);
    RUN(forced_mode_ignores_detection);
    RUN(dce_line_mapping);
    RUN(dte_line_mapping);
    RUN(overrides);
    RUN(full_buffer_holds_the_far_end);
    RUN(flow_auto_latches);
    RUN(flow_settings);
    RUN(serial_state_dce);
    RUN(serial_state_dte_and_events);
    RUN(line_coding);
    RUN(break_timing);
    RUN(data_both_ways);
    RUN(vendor_info_and_mode);
    RUN(settings_blob);
    RUN(ring_wraps);
    RUN(leds);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
