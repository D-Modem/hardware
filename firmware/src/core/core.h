// The serial core: which role the board is in, what the modem lines do,
// flow control, and the bytes between the host link and the RS-232 port.
// Portable: hardware access goes through hal.h. See SPEC.md section 5.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "hal.h"
#include "protocol.h"
#include "ring.h"

#define CORE_DETECT_PERIOD_MS 50u    // sample the detect lines this often
#define CORE_DETECT_CONFIRM 3u       // ... this many times in a row before switching on
#define CORE_LOST_MS 1000u           // peer gone this long: back to detecting
#define CORE_SETTLE_MS 100u          // after switching off, before detecting again
#define CORE_SAVE_DELAY_MS 500u      // settle before writing settings to flash
#define CORE_LED_PULSE_MS 30u

// LEDs, in the order of the front panel
#define CORE_LED_PWR 0x01
#define CORE_LED_TR 0x02
#define CORE_LED_CD 0x04
#define CORE_LED_SD 0x08
#define CORE_LED_RD 0x10
#define CORE_LED_RI 0x20

// CDC SERIAL_STATE bits (USB CDC PSTN 6.5.4)
#define SS_DCD 0x01
#define SS_DSR 0x02
#define SS_BREAK 0x04
#define SS_RI 0x08
#define SS_FRAMING 0x10
#define SS_PARITY 0x20
#define SS_OVERRUN 0x40
#define SS_EVENTS (SS_BREAK | SS_FRAMING | SS_PARITY | SS_OVERRUN)

// UART receive error flags, from the interrupt
#define RX_ERR_FRAMING 0x01
#define RX_ERR_PARITY 0x02
#define RX_ERR_BREAK 0x04
#define RX_ERR_OVERRUN 0x08

typedef struct {
    uint8_t mode;   // ps_mode
    uint8_t flow;   // ps_flow
} core_settings_t;

typedef enum { ST_DETECT, ST_ACTIVE, ST_SETTLE } core_state_t;

typedef struct {
    core_settings_t settings;
    core_state_t state;
    uint8_t role;
    uint32_t t_state;       // when the current state began
    uint32_t t_sample;      // last detect sample
    uint8_t seen_dce, seen_dte;
    uint32_t t_lost;        // peer missing since (0 = present)

    uint8_t host_lines;     // PS_LINE_DTR / RTS from the host
    uint8_t override_mask, override_value;
    uint8_t outputs, inputs;
    bool rx_room;           // hysteresis on the to-host buffer
    bool cts_latched;       // FLOW_AUTO: the far end has used its flow line
    bool hw_cts;

    uart_format_t format;
    uint8_t info_flags;
    uint32_t break_until;   // 0 = no timed break
    bool break_on;

    uint16_t serial_state_sent;
    bool serial_state_valid;
    volatile uint8_t rx_errors;   // set by the UART interrupt

    bool settings_dirty;
    uint32_t t_dirty;

    uint32_t t_sd, t_rd;    // LED pulses
    uint32_t seen_to_rs232, seen_from_rs232;
    ps_stats_t stats;

    ring_t to_host;         // RS-232 -> host
    ring_t to_rs232;        // host -> RS-232
} core_t;

extern core_t g_core;

void core_init(core_t *c);
// Call often from the main loop.
void core_poll(core_t *c);

// Host link events
void core_host_lines(core_t *c, bool dtr, bool rts);
void core_host_format(core_t *c, uint32_t baud, uint8_t stop_bits_cdc, uint8_t parity_cdc, uint8_t data_bits);
void core_host_break(core_t *c, uint16_t duration_ms);
uint32_t core_host_write(core_t *c, const uint8_t *data, uint32_t len);   // returns bytes accepted
uint32_t core_host_read(core_t *c, uint8_t *data, uint32_t max);
// SERIAL_STATE to send to the host; true if it changed (or events are pending).
bool core_serial_state(core_t *c, uint16_t *state);

// Vendor requests. IN: returns length written to buf, or -1 to stall.
int core_vendor_in(core_t *c, uint8_t req, uint16_t value, uint8_t *buf, uint16_t max);
// OUT: false to stall.
bool core_vendor_out(core_t *c, uint8_t req, uint16_t value);

// From the UART interrupt
static inline void core_isr_rx(core_t *c, uint8_t byte, uint8_t errors) {
    if (errors) c->rx_errors |= errors;
    if (!(errors & RX_ERR_BREAK)) {
        if (!ring_put(&c->to_host, byte)) c->stats.dropped++;
        else c->stats.from_rs232++;
    }
}
static inline bool core_isr_tx(core_t *c, uint8_t *byte) {
    if (!ring_get(&c->to_rs232, byte)) return false;
    c->stats.to_rs232++;
    return true;
}

// Settings blob (for flash), with a checksum.
#define CORE_SETTINGS_BLOB 16u
void core_settings_pack(const core_settings_t *s, uint8_t out[CORE_SETTINGS_BLOB]);
bool core_settings_unpack(core_settings_t *s, const uint8_t in[CORE_SETTINGS_BLOB]);
