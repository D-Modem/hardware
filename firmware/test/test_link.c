/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// Host tests for the host links: who owns the RS-232 port (SPEC-W.md 4.6)
// and DCD from result codes (SPEC-W.md 4.4).
#include <stdio.h>
#include <string.h>
#include "../src/core/link.h"
#include "fake_hal.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define TEST(name) static void name(void)
#define RUN(name) do { printf("%s\n", #name); fake_reset(); setup(); name(); } while (0)

static core_t c;
static links_t s;

static void run_ms(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 5) { hw.now += 5; core_poll(&c); }
}

static void setup(void) {
    core_init(&c);
    links_init(&s, &c);
    CHECK(core_vendor_out(&c, PS_REQ_SET_BAUD_MODE, PS_BAUD_HOST));
    hw.peer_dce = true;
    run_ms(500);
    CHECK(c.role == PS_ROLE_DCE);
}

static uint32_t wr(uint8_t id, const char *t) { return link_write(&s, &c, id, (const uint8_t *)t, (uint32_t)strlen(t)); }

TEST(usb_alone_is_like_before) {
    CHECK(s.owner == LINK_NONE);
    link_up(&s, &c, LINK_USB, true);
    CHECK(s.owner == LINK_USB);          // mounted, port not yet open: still the owner
    CHECK(c.host_lines == 0);
    link_lines(&s, &c, LINK_USB, true, true);
    run_ms(10);
    CHECK(c.outputs == (PS_LINE_DCD | PS_LINE_DSR | PS_LINE_CTS));
    CHECK(wr(LINK_USB, "hi") == 2);
    CHECK(ring_count(&c.to_rs232) == 2);
    link_up(&s, &c, LINK_USB, false);
    CHECK(s.owner == LINK_NONE && c.host_lines == 0);
}

TEST(first_wireless_link_owns) {
    link_up(&s, &c, LINK_USB, true);     // plugged in for power only, port closed
    link_up(&s, &c, LINK_BLE, true);
    CHECK(s.owner == LINK_BLE);
    run_ms(10);
    CHECK(c.outputs & PS_LINE_DCD);      // a plain NUS app gets DCD and DSR
    CHECK(c.outputs & PS_LINE_DSR);
    link_up(&s, &c, LINK_SPP, true);
    CHECK(s.owner == LINK_BLE);          // the second one is read-only
    CHECK(wr(LINK_SPP, "xyz") == 3);     // accepted and dropped
    CHECK(ring_count(&c.to_rs232) == 0);
    CHECK(wr(LINK_BLE, "ab") == 2);
    CHECK(ring_count(&c.to_rs232) == 2);
    uint8_t buf[8];
    ring_put(&c.to_host, 'Q');
    CHECK(link_read(&s, &c, LINK_SPP, buf, sizeof buf) == 0);
    CHECK(link_read(&s, &c, LINK_BLE, buf, sizeof buf) == 1 && buf[0] == 'Q');
    link_up(&s, &c, LINK_BLE, false);
    CHECK(s.owner == LINK_SPP);          // passes to the one still connected
    link_up(&s, &c, LINK_SPP, false);
    CHECK(s.owner == LINK_USB);
}

TEST(usb_port_open_takes_over) {
    link_up(&s, &c, LINK_USB, true);
    link_up(&s, &c, LINK_BLE, true);
    link_format(&s, &c, LINK_BLE, 9600, 0, 0, 8);
    CHECK(c.format.baud == 9600);
    link_format(&s, &c, LINK_USB, 19200, 0, 2, 7);
    CHECK(c.format.baud == 9600);        // not the owner yet: kept for later
    link_lines(&s, &c, LINK_USB, true, false);
    CHECK(s.owner == LINK_USB);
    CHECK(c.format.baud == 19200 && c.format.parity == 2 && c.format.data_bits == 7);
    CHECK(!link_break(&s, &c, LINK_BLE, 100));
    CHECK(link_break(&s, &c, LINK_USB, 100) && hw.brk);
    link_lines(&s, &c, LINK_USB, false, false);   // port closed: back to the wireless link
    CHECK(s.owner == LINK_BLE);
    CHECK(!hw.brk);                      // the old owner's break ended
    CHECK(c.format.baud == 9600);
}

TEST(spp_dcd_from_result_codes) {
    link_up(&s, &c, LINK_SPP, true);
    run_ms(10);
    CHECK(!(c.outputs & PS_LINE_DCD));   // no call yet
    CHECK(c.outputs & PS_LINE_DSR);
    wr(LINK_SPP, "ATDT5551234\r");
    wr(LINK_SPP, "\r\nCONN");            // split across writes
    wr(LINK_SPP, "ECT 33600\r\n");
    run_ms(10);
    CHECK(c.outputs & PS_LINE_DCD);
    wr(LINK_SPP, "data with NO CARRIER inside\r\n");   // not a whole line
    run_ms(10);
    CHECK(c.outputs & PS_LINE_DCD);
    wr(LINK_SPP, "\r\nNO CARRIER\r\n");
    run_ms(10);
    CHECK(!(c.outputs & PS_LINE_DCD));
    link_rc_dcd(&s, &c, LINK_SPP, false);   // off: DCD follows "DTR" (connected)
    run_ms(10);
    CHECK(c.outputs & PS_LINE_DCD);
}

TEST(long_lines_are_not_result_codes) {
    link_up(&s, &c, LINK_SPP, true);
    char big[64];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = 0;
    wr(LINK_SPP, big);
    wr(LINK_SPP, "CONNECT\r\n");         // same line as the x's
    run_ms(10);
    CHECK(!(c.outputs & PS_LINE_DCD));
    wr(LINK_SPP, "CONNECT\r");
    run_ms(10);
    CHECK(c.outputs & PS_LINE_DCD);
}

int main(void) {
    RUN(usb_alone_is_like_before);
    RUN(first_wireless_link_owns);
    RUN(usb_port_open_takes_over);
    RUN(spp_dcd_from_result_codes);
    RUN(long_lines_are_not_result_codes);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
