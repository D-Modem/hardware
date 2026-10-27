/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// The WB stick's codec on the RP2040 (SPEC-WB.md §3-4): the TLV320AIC3204
// as the I2S master on the RP2040's 12 MHz crystal clock, the RP2040 its
// slave in PIO, the frames moved by DMA in blocks and handed to wb_audio.c
// from the DMA's interrupt; its registers over I2C1. See codec.h and wb_hal.h.
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "board_config.h"
#include "aic3204.h"
#include "codec.h"
#include "wb_hal.h"
#include "i2s.pio.h"

static_assert(PIN_CODEC_BCLK == 2 && PIN_CODEC_WCLK == 3, "i2s.pio waits on GPIO 2 (BCLK) and 3 (WCLK)");
static_assert(PIN_CODEC_MCLK == 21, "MCLK is clk_gpout0, on GPIO21");
static_assert(PIN_CODEC_SDA == 6 && PIN_CODEC_SCL == 7, "I2C1 on GPIO 6 (SDA) and 7 (SCL)");

#define I2S_PIO pio0
#define I2S_SM 0
#define I2C_PORT i2c1
#define I2C_HZ 400000            // fast mode (datasheet 7.17)
#define I2C_TIMEOUT_US 2000
#define BLOCK 32                 // frames a DMA block: 1/3 ms at 96 kHz
#define PREFILL_MS 3             // the modem's samples ahead before playing

wb_audio_t g_audio;
static uint s_offset;
static int s_rx[2], s_tx[2];     // DMA channels: two each way, each chained to the other
static uint32_t s_rx_buf[2][2 * BLOCK], s_tx_buf[2][2 * BLOCK];
static bool s_running;

// The DMA's interrupt: a received block to wb_audio_rx() and its channel
// pointed at its buffer again; a sent block's buffer refilled from
// wb_audio_tx(). Each channel has the other's whole block to wait for this.
static void __not_in_flash_func(dma_irq)(void) {
    uint32_t pending = dma_hw->ints0;
    for (int k = 0; k < 2; k++) {
        if (pending & (1u << s_rx[k])) {
            dma_hw->ints0 = 1u << s_rx[k];
            wb_audio_rx(&g_audio, s_rx_buf[k], BLOCK);
            dma_channel_set_write_addr((uint)s_rx[k], s_rx_buf[k], false);
        }
        if (pending & (1u << s_tx[k])) {
            dma_hw->ints0 = 1u << s_tx[k];
            wb_audio_tx(&g_audio, s_tx_buf[k], BLOCK);
            dma_channel_set_read_addr((uint)s_tx[k], s_tx_buf[k], false);
        }
    }
}

// One output pin at a level.
static void out_pin(uint pin, bool level) {
    gpio_init(pin);
    gpio_put(pin, level);
    gpio_set_dir(pin, GPIO_OUT);
}

void codec_hw_init(void) {
    out_pin(PIN_CODEC_RESET_N, 0);   // in reset until whal_codec_reset (pulled down too)
    out_pin(PIN_LED_LINK, 0);
    out_pin(PIN_LED_DATA, 0);
    gpio_init(PIN_CODEC_MFP5);       // INT1 once the codec is set up; until then nothing drives it
    gpio_set_dir(PIN_CODEC_MFP5, GPIO_IN);
    gpio_pull_down(PIN_CODEC_MFP5);
    gpio_init(PIN_CODEC_BCLK);
    gpio_set_dir(PIN_CODEC_BCLK, GPIO_IN);
    gpio_init(PIN_CODEC_WCLK);
    gpio_set_dir(PIN_CODEC_WCLK, GPIO_IN);
    gpio_init(PIN_CODEC_DOUT);
    gpio_set_dir(PIN_CODEC_DOUT, GPIO_IN);
    // MCLK: the crystal's 12 MHz, undivided (no fractional divider: a clean clock)
    clock_gpio_init_int_frac8(PIN_CODEC_MCLK, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_XOSC_CLKSRC, 1, 0);
    // I2C1 (4.7 kOhm pull-ups on the board)
    i2c_init(I2C_PORT, I2C_HZ);
    gpio_set_function(PIN_CODEC_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_CODEC_SCL, GPIO_FUNC_I2C);
    // the I2S slave and its DMA
    s_offset = pio_add_program(I2S_PIO, &i2s_slave_program);
    pio_sm_claim(I2S_PIO, I2S_SM);
    for (int k = 0; k < 2; k++) {
        s_rx[k] = dma_claim_unused_channel(true);
        s_tx[k] = dma_claim_unused_channel(true);
    }
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq);
    irq_set_priority(DMA_IRQ_0, 0);  // highest: a block must be back before its turn comes again
    wb_audio_init(&g_audio, 0);
}

// One DMA channel of a pair: words between the PIO's FIFO and buf, a block
// at a time, chained to its partner, an interrupt at each block's end.
static void dma_setup(int ch, int partner, bool rx, uint32_t *buf, bool start) {
    dma_channel_config c = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, !rx);
    channel_config_set_write_increment(&c, rx);
    channel_config_set_dreq(&c, pio_get_dreq(I2S_PIO, I2S_SM, !rx));
    channel_config_set_chain_to(&c, (uint)partner);
    if (rx)
        dma_channel_configure((uint)ch, &c, buf, &I2S_PIO->rxf[I2S_SM], 2 * BLOCK, start);
    else
        dma_channel_configure((uint)ch, &c, &I2S_PIO->txf[I2S_SM], buf, 2 * BLOCK, start);
    dma_channel_set_irq0_enabled((uint)ch, true);
}

// The PIO program from its entry (it finds a frame's start) and the four
// DMA channels, the samples' state as it is. Silence until wb_audio_tx's
// first block.
static void i2s_go(void) {
    memset(s_tx_buf, 0, sizeof s_tx_buf);
    i2s_slave_program_init(I2S_PIO, I2S_SM, s_offset, PIN_CODEC_DOUT, PIN_CODEC_DIN, PIN_CODEC_WCLK);
    pio_sm_clear_fifos(I2S_PIO, I2S_SM);
    pio_interrupt_clear(I2S_PIO, 0);
    for (int k = 0; k < 2; k++) {
        dma_setup(s_rx[k], s_rx[k ^ 1], true, s_rx_buf[k], k == 0);
        dma_setup(s_tx[k], s_tx[k ^ 1], false, s_tx_buf[k], k == 0);
    }
    irq_set_enabled(DMA_IRQ_0, true);
    pio_sm_set_enabled(I2S_PIO, I2S_SM, true);
    s_running = true;
}

// The PIO stopped and the DMA channels aborted. Each channel is disabled
// before any is aborted, so none can trigger its partner (RP2040-E13).
static void i2s_halt(void) {
    if (!s_running) return;
    pio_sm_set_enabled(I2S_PIO, I2S_SM, false);
    irq_set_enabled(DMA_IRQ_0, false);
    uint32_t mask = 0;
    for (int k = 0; k < 2; k++) {
        int ch[2] = {s_rx[k], s_tx[k]};
        for (int i = 0; i < 2; i++) {
            dma_channel_set_irq0_enabled((uint)ch[i], false);
            hw_clear_bits(&dma_hw->ch[ch[i]].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
            mask |= 1u << ch[i];
        }
    }
    for (int k = 0; k < 2; k++) {
        dma_channel_abort((uint)s_rx[k]);
        dma_channel_abort((uint)s_tx[k]);
    }
    dma_hw->ints0 = mask;
    s_running = false;
}

void whal_i2s_start(uint32_t rate) {
    i2s_halt();
    bool streaming = g_audio.s.streaming;
    wb_audio_init(&g_audio, PREFILL_MS * rate / 1000);
    g_audio.s.streaming = streaming;
    i2s_go();
}

void whal_i2s_stop(void) { i2s_halt(); }

bool whal_i2s_lost(void) {
    if (!s_running || !pio_interrupt_get(I2S_PIO, 0)) return false;
    i2s_halt();
    i2s_go();   // the streams carry on; only the frames start over
    return true;
}

void codec_stream(bool on) {
    uint32_t irq = save_and_disable_interrupts();
    sstream_set(&g_audio.s, on);
    restore_interrupts(irq);
}

void codec_from_host(const int16_t *s, uint32_t n) {
    // the overrun count is shared with the interrupt's: count with it held off
    uint32_t irq = save_and_disable_interrupts();
    sstream_from_host(&g_audio.s, s, n);
    restore_interrupts(irq);
}

uint32_t whal_millis(void) { return to_ms_since_boot(get_absolute_time()); }

void whal_sleep_ms(uint32_t ms) {
    while (ms--) {
        sleep_ms(1);
        watchdog_update();
    }
}

void whal_codec_reset(void) {
    gpio_put(PIN_CODEC_RESET_N, 0);
    sleep_ms(1);                     // at least 10 ns (SLAA557 3.1)
    gpio_put(PIN_CODEC_RESET_N, 1);
}

bool whal_codec_write(uint8_t reg, uint8_t v) {
    uint8_t b[2] = {reg, v};
    return i2c_write_timeout_us(I2C_PORT, AIC_I2C_ADDR, b, 2, false, I2C_TIMEOUT_US) == 2;
}

bool whal_codec_read(uint8_t reg, uint8_t *v) {
    if (i2c_write_timeout_us(I2C_PORT, AIC_I2C_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1) return false;
    return i2c_read_timeout_us(I2C_PORT, AIC_I2C_ADDR, v, 1, false, I2C_TIMEOUT_US) == 1;
}

bool whal_codec_int(void) { return gpio_get(PIN_CODEC_MFP5); }
uint32_t whal_frames(void) { return g_audio.frames; }
bool whal_streaming(void) { return g_audio.s.streaming; }
uint32_t whal_tx_active(void) { return g_audio.tx_active; }
void whal_set_input(uint8_t input) { g_audio.input = input; }
void whal_set_loopback(bool on) { g_audio.loopback = on; }

void whal_counters(uint16_t *underruns, uint16_t *overruns, uint32_t *in, uint32_t *out) {
    *underruns = g_audio.s.underruns;
    *overruns = g_audio.s.overruns;
    *in = g_audio.s.samples_in;
    *out = g_audio.s.samples_out;
}

void whal_peaks(uint16_t peak[3]) {
    uint32_t irq = save_and_disable_interrupts();
    wb_audio_peaks(&g_audio, peak);
    restore_interrupts(irq);
}

void whal_leds(bool link, bool data) {
    gpio_put(PIN_LED_LINK, link);
    gpio_put(PIN_LED_DATA, data);
}
