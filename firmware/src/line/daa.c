/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The line stick's DAA on the RP2040 (SPEC-L.md §3.2): the Si3056 as the
// serial-port master, the RP2040 its slave in PIO, every received word
// through daa_frame.c from the PIO's interrupt. See daa.h and line_hal.h.
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "board_config.h"
#include "daa.h"
#include "line_hal.h"
#include "si3056.h"
#include "si3056.pio.h"

static_assert(PIN_DAA_SCLK == 2 && PIN_DAA_FSYNC == 3, "si3056.pio waits on GPIO 2 (SCLK) and 3 (FSYNC)");
static_assert(PIN_DAA_MCLK == 21, "MCLK is clk_gpout0, on GPIO21");

#define DAA_PIO pio0
#define DAA_SM 0
#define PREFILL 32              // the modem's samples ahead before playing: ~3 ms
#define ACCESS_TIMEOUT_US 20000 // a register access: a few frames, even at the reset clock

line_stream_t g_stream;
static uint s_offset;
static bool s_loaded;
static volatile uint8_t s_ring_edges;

// The PIO's RX interrupt: every word the Si3056 sent, through the frame
// engine; its TX words to the PIO (at most two, the FIFO has four) and the
// FC pin as it says.
static void __not_in_flash_func(daa_irq)(void) {
    while (!pio_sm_is_rx_fifo_empty(DAA_PIO, DAA_SM)) {
        daa_step_t st;
        uint16_t w = (uint16_t)pio_sm_get(DAA_PIO, DAA_SM);
        line_stream_word(&g_stream, w, &st);
        for (int i = 0; i < st.n_tx; i++)
            if (!pio_sm_is_tx_fifo_full(DAA_PIO, DAA_SM)) pio_sm_put(DAA_PIO, DAA_SM, (uint32_t)st.tx[i] << 16);
        if (st.fc >= 0) gpio_put(PIN_DAA_FC, st.fc);
    }
}

// The ring detector (RGDT, active low) changed: counted for the host.
static void ring_edge(uint gpio, uint32_t events) {
    (void)events;
    if (gpio == PIN_DAA_RGDT_N) s_ring_edges++;
}

// One output pin at a level.
static void out_pin(uint pin, bool level) {
    gpio_init(pin);
    gpio_put(pin, level);
    gpio_set_dir(pin, GPIO_OUT);
}

void daa_hw_init(void) {
    out_pin(PIN_DAA_RESET_N, 0);      // in reset until lhal_daa_start
    out_pin(PIN_DAA_OFHK_N, 1);       // on hook
    out_pin(PIN_DAA_FC, 0);
    out_pin(PIN_DAA_M0, 0);           // serial mode 0 (with M1 = 0)
    out_pin(PIN_DAA_RGDT_N, 0);       // M1 = 0 through reset; the ring detector after
    out_pin(PIN_LED_OH, 0);
    out_pin(PIN_LED_RI, 0);
    gpio_init(PIN_DAA_INT_N);
    gpio_set_dir(PIN_DAA_INT_N, GPIO_IN);
    gpio_pull_up(PIN_DAA_INT_N);
    for (uint p = PIN_DAA_SCLK; p <= PIN_DAA_SDO; p++) {
        gpio_init(p);
        gpio_set_dir(p, GPIO_IN);
    }
    // MCLK: the USB PLL's 48 MHz, undivided (a clean clock: no fractional divider)
    clock_gpio_init(PIN_DAA_MCLK, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_USB, 1);
    line_stream_init(&g_stream, PREFILL);
}

// A register access through the frames: queue the word, wait for its
// secondary frame; false (and the access abandoned) after the timeout.
static bool access(uint16_t word, uint8_t *result) {
    absolute_time_t until = make_timeout_time_us(ACCESS_TIMEOUT_US);
    for (;;) {
        uint32_t irq = save_and_disable_interrupts();
        bool ok = daa_frame_queue(&g_stream.fr, word);
        restore_interrupts(irq);
        if (ok) break;
        if (time_reached(until)) return false;
    }
    while (g_stream.fr.op_state != DAA_OP_DONE)
        if (time_reached(until)) {
            uint32_t irq = save_and_disable_interrupts();
            g_stream.fr.op_state = DAA_OP_IDLE;
            g_stream.fr.expect_secondary = false;
            gpio_put(PIN_DAA_FC, 0);
            restore_interrupts(irq);
            return false;
        }
    if (result) *result = g_stream.fr.op_result;
    g_stream.fr.op_state = DAA_OP_IDLE;
    return true;
}

// Frames running: at least n more primaries within timeout_ms.
static bool frames_run(uint32_t n, uint32_t timeout_ms) {
    uint32_t start = g_stream.fr.primaries;
    absolute_time_t until = make_timeout_time_ms(timeout_ms);
    while (g_stream.fr.primaries - start < n)
        if (time_reached(until)) return false;
    return true;
}

bool lhal_daa_start(int src) {
    // stop the frames; reset with M0 = M1 = 0 (serial mode 0), MCLK running
    irq_set_enabled(PIO0_IRQ_0, false);
    if (s_loaded) pio_sm_set_enabled(DAA_PIO, DAA_SM, false);
    gpio_set_irq_enabled(PIN_DAA_RGDT_N, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, false);
    gpio_put(PIN_DAA_FC, 0);
    out_pin(PIN_DAA_RGDT_N, 0);
    gpio_put(PIN_DAA_RESET_N, 0);
    sleep_ms(2);
    gpio_put(PIN_DAA_RESET_N, 1);    // M0, M1 latched here
    sleep_ms(1);
    gpio_set_dir(PIN_DAA_RGDT_N, GPIO_IN);   // now the ring detector (pulled up on the board)
    gpio_pull_up(PIN_DAA_RGDT_N);
    gpio_set_irq_enabled_with_callback(PIN_DAA_RGDT_N, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, ring_edge);

    // the frames: 15-bit mode until SB is set
    bool streaming = g_stream.s.streaming;
    line_stream_init(&g_stream, PREFILL);
    g_stream.s.streaming = streaming;
    if (!s_loaded) {
        s_offset = pio_add_program(DAA_PIO, &si3056_program);
        pio_sm_claim(DAA_PIO, DAA_SM);
        pio_gpio_init(DAA_PIO, PIN_DAA_SDI);
        s_loaded = true;
    }
    si3056_program_init(DAA_PIO, DAA_SM, s_offset, PIN_DAA_SDO, PIN_DAA_SDI);
    pio_sm_clear_fifos(DAA_PIO, DAA_SM);
    pio_sm_put(DAA_PIO, DAA_SM, 0);   // the first primary's word
    pio_set_irq0_source_enabled(DAA_PIO, pis_sm0_rx_fifo_not_empty, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, daa_irq);
    irq_set_priority(PIO0_IRQ_0, 0);  // highest: a word missed loses the frame's place
    irq_set_enabled(PIO0_IRQ_0, true);
    pio_sm_set_enabled(DAA_PIO, DAA_SM, true);

    // at reset PLL1 runs at MCLK x 1 / 1: slower frames, but frames
    if (!frames_run(4, 100)) return false;
    // PLL1 for MCLK = 48 MHz: N first, then M (registers hold value - 1)
    if (!access(si_ctl_write(SI_PLL_N, SI_MCLK_N - 1), NULL)) return false;
    if (!access(si_ctl_write(SI_PLL_M, SI_MCLK_M - 1), NULL)) return false;
    sleep_ms(2);                      // lock: < 1 ms
    if (!access(si_ctl_write(SI_SRC, (uint8_t)src), NULL)) return false;
    if (!frames_run(4, 50)) return false;
    // 16-bit mode: secondaries by the FC pin from now on
    if (!access(si_ctl_write(SI_CONTROL1, SI_CONTROL1_SB), NULL)) return false;
    uint32_t irq = save_and_disable_interrupts();
    g_stream.fr.fc_hw = true;
    restore_interrupts(irq);
    // the line side powered up; it finds the link within 10 ms (FDT)
    if (!access(si_ctl_write(SI_DAA2, 0x00), NULL)) return false;
    absolute_time_t until = make_timeout_time_ms(100);
    uint8_t st = 0;
    while (!time_reached(until) && access(si_ctl_read(SI_STATUS), &st) && !(st & SI_STATUS_FDT)) sleep_ms(2);
    return true;
}

bool lhal_daa_read(uint8_t reg, uint8_t *v) { return access(si_ctl_read(reg), v); }
bool lhal_daa_write(uint8_t reg, uint8_t v) { return access(si_ctl_write(reg, v), NULL); }

void daa_stream(bool on) {
    uint32_t irq = save_and_disable_interrupts();
    sstream_set(&g_stream.s, on);
    restore_interrupts(irq);
}

void daa_from_host(const int16_t *s, uint32_t n) {
    // the overrun count is shared with the interrupt's: count with it held off
    uint32_t irq = save_and_disable_interrupts();
    sstream_from_host(&g_stream.s, s, n);
    restore_interrupts(irq);
}

uint32_t lhal_millis(void) { return to_ms_since_boot(get_absolute_time()); }
void lhal_hook(bool off) { gpio_put(PIN_DAA_OFHK_N, !off); }
bool lhal_ring(void) { return !gpio_get(PIN_DAA_RGDT_N); }
uint8_t lhal_ring_edges(void) { return s_ring_edges; }
bool lhal_int(void) { return !gpio_get(PIN_DAA_INT_N); }
bool lhal_streaming(void) { return g_stream.s.streaming; }

void lhal_counters(uint16_t *underruns, uint16_t *overruns, uint32_t *in, uint32_t *out) {
    *underruns = g_stream.s.underruns;
    *overruns = g_stream.s.overruns;
    *in = g_stream.s.samples_in;
    *out = g_stream.s.samples_out;
}

void lhal_leds(bool off_hook, bool ringing) {
    gpio_put(PIN_LED_OH, off_hook);
    gpio_put(PIN_LED_RI, ringing);
}
