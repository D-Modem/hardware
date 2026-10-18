/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// hal.h for the RP2040 boards. GPIO numbers come from the netlist
// (generated/board_pins.h). The RS-232 transceivers invert: logic low on a
// driver input is an asserted (positive) RS-232 line, and an asserted line
// reads low on a receiver output.
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "board_config.h"
#include "core.h"
#include "hal_rp2040.h"
#include "autobaud.pio.h"

#define UART uart0
#define UART_IRQ UART0_IRQ

typedef struct { uint8_t tx, rx, cts, rts; } uart_pins_t;
// [role]: CTS is the far end's flow line (computer's RTS / modem's CTS) into the UART;
// RTS is ours to the far end (computer's CTS / modem's RTS), driven in software.
static const uart_pins_t PINS[3] = {
    {0, 0, 0, 0},
    {PIN_A_TX, PIN_A_RX, PIN_A_CTS, PIN_A_RTS},
    {PIN_B_TX, PIN_B_RX, PIN_B_CTS, PIN_B_RTS},
};
static uint8_t s_route = PS_ROLE_NONE;

static const uint8_t LED_PINS[6] = {LED_PIN_PWR, LED_PIN_TR, LED_PIN_CD, LED_PIN_SD, LED_PIN_RD, LED_PIN_RI};

static void out_pin(uint pin, bool level) {
    gpio_init(pin);
    gpio_put(pin, level);
    gpio_set_dir(pin, GPIO_OUT);
}

static void in_pin(uint pin, bool pull_up) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    if (pull_up) gpio_pull_up(pin);
    else gpio_disable_pulls(pin);
}

// A UART pin set not in use: receivers' outputs go high-impedance when their
// chip is shut down, so pull them up (idle / deasserted).
static void park(const uart_pins_t *p) {
    in_pin(p->tx, true);
    in_pin(p->rx, true);
    in_pin(p->cts, true);
    out_pin(p->rts, true);   // deasserted
}

static void uart_irq(void) {
    uart_hw_t *hw = uart_get_hw(UART);
    while (!(hw->fr & UART_UARTFR_RXFE_BITS)) {
        uint32_t dr = hw->dr;
        uint8_t e = 0;
        if (dr & UART_UARTDR_FE_BITS) e |= RX_ERR_FRAMING;
        if (dr & UART_UARTDR_PE_BITS) e |= RX_ERR_PARITY;
        if (dr & UART_UARTDR_BE_BITS) e |= RX_ERR_BREAK;
        if (dr & UART_UARTDR_OE_BITS) e |= RX_ERR_OVERRUN;
        core_isr_rx(&g_core, (uint8_t)dr, e);
    }
    while (!(hw->fr & UART_UARTFR_TXFF_BITS)) {
        uint8_t b;
        if (!core_isr_tx(&g_core, &b)) {
            hw_clear_bits(&hw->imsc, UART_UARTIMSC_TXIM_BITS);
            break;
        }
        hw->dr = b;
    }
}

// Baud-rate learning: a PIO state machine times the RX line (autobaud.pio).
#define AB_PIO pio0
static uint s_ab_sm, s_ab_offset;
static pio_sm_config s_ab_cfg;

static void ab_irq(void) {
    while (!pio_sm_is_rx_fifo_empty(AB_PIO, s_ab_sm)) {
        uint32_t raw = pio_sm_get(AB_PIO, s_ab_sm);
        core_isr_run(&g_core, (raw >> 31) != 0, ~raw & 0x7FFFFFFFu);
    }
}

static void ab_init(void) {
    s_ab_sm = (uint)pio_claim_unused_sm(AB_PIO, true);
    s_ab_offset = pio_add_program(AB_PIO, &autobaud_program);
    s_ab_cfg = autobaud_program_get_default_config(s_ab_offset);
    sm_config_set_in_shift(&s_ab_cfg, true, false, 32);   // right: "in x, 31; in y, 1" puts y in bit 31
    sm_config_set_fifo_join(&s_ab_cfg, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv_int_frac(&s_ab_cfg, 1, 0);
    irq_set_exclusive_handler(PIO0_IRQ_0, ab_irq);
    pio_set_irq0_source_enabled(AB_PIO, (enum pio_interrupt_source)(pis_sm0_rx_fifo_not_empty + s_ab_sm), true);
    irq_set_enabled(PIO0_IRQ_0, true);
}

void hal_autobaud(uint8_t role) {
    pio_sm_set_enabled(AB_PIO, s_ab_sm, false);
    pio_sm_clear_fifos(AB_PIO, s_ab_sm);
    if (role != PS_ROLE_DCE && role != PS_ROLE_DTE) return;
    // the pin stays the UART's: PIO reads every pin's input
    uint pin = PINS[role].rx;
    sm_config_set_in_pins(&s_ab_cfg, pin);
    sm_config_set_jmp_pin(&s_ab_cfg, pin);
    pio_sm_init(AB_PIO, s_ab_sm, s_ab_offset, &s_ab_cfg);
    pio_sm_set_enabled(AB_PIO, s_ab_sm, true);
}

uint32_t hal_autobaud_hz(void) { return clock_get_hz(clk_sys) / 2u; }

void hal_init(void) {
    // transceivers off until the core decides (the board also pulls these low)
    out_pin(PIN_A_SHDN_N, false);
    out_pin(PIN_B_FORCEOFF_N, false);
    out_pin(PIN_RS_FORCEON, false);
    in_pin(PIN_A_STATUS, false);     // driven by the chips, even in shutdown
    in_pin(PIN_B_INVALID, false);
    in_pin(PIN_A_R1OUTB, false);

    // chip A (DCE) modem lines
    out_pin(PIN_A_DSR, true);
    out_pin(PIN_A_DCD, true);
    out_pin(PIN_A_RI, true);
    in_pin(PIN_A_DTR, true);
    // chip B (DTE) modem lines
    out_pin(PIN_B_DTR, true);
    in_pin(PIN_B_DSR, true);
    in_pin(PIN_B_DCD, true);
    in_pin(PIN_B_RI, true);

    park(&PINS[PS_ROLE_DCE]);
    park(&PINS[PS_ROLE_DTE]);

    for (unsigned i = 0; i < 6; i++)
        if (LED_PINS[i] != LED_NONE) out_pin(LED_PINS[i], false);

    uart_init(UART, 115200);
    uart_set_fifo_enabled(UART, true);
    irq_set_exclusive_handler(UART_IRQ, uart_irq);
    irq_set_enabled(UART_IRQ, true);
    uart_set_irq_enables(UART, true, false);   // RX (and RX timeout); TX when there's data

    ab_init();
}

uint32_t hal_millis(void) { return to_ms_since_boot(get_absolute_time()); }

void hal_xcvr_power(uint8_t role) {
    // never both on: switch the other one off first
    if (role != PS_ROLE_DCE) gpio_put(PIN_A_SHDN_N, 0);
    if (role != PS_ROLE_DTE) gpio_put(PIN_B_FORCEOFF_N, 0);
    gpio_put(PIN_RS_FORCEON, role != PS_ROLE_NONE);   // no auto power-down while in use
    if (role == PS_ROLE_DCE) gpio_put(PIN_A_SHDN_N, 1);
    if (role == PS_ROLE_DTE) gpio_put(PIN_B_FORCEOFF_N, 1);
}

bool hal_peer_dce(void) { return gpio_get(PIN_A_STATUS); }
bool hal_peer_dte(void) { return gpio_get(PIN_B_INVALID); }

uint8_t hal_detect_bits(void) {
    return (uint8_t)((gpio_get(PIN_A_STATUS) ? PS_DETECT_A_STATUS : 0) |
                     (gpio_get(PIN_B_INVALID) ? PS_DETECT_B_INVALID : 0) |
                     (gpio_get(PIN_A_R1OUTB) ? PS_DETECT_A_R1OUTB : 0));
}

void hal_uart_route(uint8_t role) {
    if (role == s_route) return;
    uint32_t irq = save_and_disable_interrupts();
    if (s_route != PS_ROLE_NONE) park(&PINS[s_route]);
    if (role != PS_ROLE_NONE) {
        const uart_pins_t *p = &PINS[role];
        gpio_set_function(p->tx, GPIO_FUNC_UART);
        gpio_set_function(p->rx, GPIO_FUNC_UART);
        gpio_pull_up(p->rx);
        gpio_set_function(p->cts, GPIO_FUNC_UART);   // still readable with gpio_get()
        gpio_pull_up(p->cts);
        out_pin(p->rts, true);
    }
    s_route = role;
    restore_interrupts(irq);
}

void hal_uart_format(const uart_format_t *f) {
    uart_set_baudrate(UART, f->baud);
    uart_parity_t par = f->parity == 1 ? UART_PARITY_ODD : f->parity == 2 ? UART_PARITY_EVEN : UART_PARITY_NONE;
    uart_set_format(UART, f->data_bits, f->stop_bits, par);
}

void hal_uart_rx_flush(void) {
    uart_hw_t *hw = uart_get_hw(UART);
    while (!(hw->fr & UART_UARTFR_RXFE_BITS)) (void)hw->dr;
}

void hal_uart_hw_cts(bool on) { uart_set_hw_flow(UART, on, false); }
void hal_uart_break(bool on) { uart_set_break(UART, on); }

void hal_uart_kick_tx(void) {
    uint32_t irq = save_and_disable_interrupts();
    uart_hw_t *hw = uart_get_hw(UART);
    // fill the FIFO now; the TX interrupt (on draining below its level) does the rest
    bool more = true;
    while (!(hw->fr & UART_UARTFR_TXFF_BITS)) {
        uint8_t b;
        if (!core_isr_tx(&g_core, &b)) { more = false; break; }
        hw->dr = b;
    }
    if (more) hw_set_bits(&hw->imsc, UART_UARTIMSC_TXIM_BITS);
    restore_interrupts(irq);
}

void hal_lines_out(uint8_t role, uint8_t a) {
    if (role == PS_ROLE_DCE) {
        gpio_put(PIN_A_DCD, !(a & PS_LINE_DCD));
        gpio_put(PIN_A_DSR, !(a & PS_LINE_DSR));
        gpio_put(PIN_A_RI, !(a & PS_LINE_RI));
        gpio_put(PIN_A_RTS, !(a & PS_LINE_CTS));    // our "clear to send" to the computer
    } else if (role == PS_ROLE_DTE) {
        gpio_put(PIN_B_DTR, !(a & PS_LINE_DTR));
        gpio_put(PIN_B_RTS, !(a & PS_LINE_RTS));
    }
}

uint8_t hal_lines_in(uint8_t role) {
    uint8_t r = 0;
    if (role == PS_ROLE_DCE) {
        if (!gpio_get(PIN_A_DTR)) r |= PS_LINE_DTR;
        if (!gpio_get(PIN_A_CTS)) r |= PS_LINE_RTS;   // the computer's RTS arrives on the UART's CTS pin
    } else if (role == PS_ROLE_DTE) {
        if (!gpio_get(PIN_B_DCD)) r |= PS_LINE_DCD;
        if (!gpio_get(PIN_B_DSR)) r |= PS_LINE_DSR;
        if (!gpio_get(PIN_B_RI)) r |= PS_LINE_RI;
        if (!gpio_get(PIN_B_CTS)) r |= PS_LINE_CTS;
    }
    return r;
}

void hal_leds(uint8_t mask) {
    for (unsigned i = 0; i < 6; i++)
        if (LED_PINS[i] != LED_NONE) gpio_put(LED_PINS[i], (mask >> i) & 1);
}

// Settings live in the last flash sector.
#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

bool hal_settings_load(void *buf, uint32_t len) {
    memcpy(buf, (const void *)(XIP_BASE + SETTINGS_OFFSET), len);
    return true;   // the core checks the magic and checksum
}

void hal_settings_save(const void *buf, uint32_t len) {
    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof page);
    memcpy(page, buf, len < sizeof page ? len : sizeof page);
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(irq);
}

uint32_t hal_irq_save(void) { return save_and_disable_interrupts(); }
void hal_irq_restore(uint32_t s) { restore_interrupts(s); }
