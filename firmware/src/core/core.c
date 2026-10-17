// The serial core. See core.h and SPEC.md section 5.
#include "core.h"
#include <string.h>

#ifndef PS_FW_MAJOR
#define PS_FW_MAJOR 0
#define PS_FW_MINOR 1
#define PS_FW_PATCH 0
#endif
#ifndef PS_BOARD_ID
#define PS_BOARD_ID 0
#endif

core_t g_core;

static const uart_format_t DEFAULT_FORMAT = {115200, 8, 1, 0};

// to-host buffer: stop the far end above 3/4 full, let it go again below 1/2
#define ROOM_OFF (RING_SIZE * 3u / 4u)
#define ROOM_ON (RING_SIZE / 2u)

static bool elapsed(uint32_t now, uint32_t since, uint32_t ms) { return (uint32_t)(now - since) >= ms; }

static void set_state(core_t *c, core_state_t s) {
    c->state = s;
    c->t_state = hal_millis();
}

static void apply_flow(core_t *c) {
    bool on = false;
    if (c->role != PS_ROLE_NONE) {
        if (c->settings.flow == PS_FLOW_ON) on = true;
        else if (c->settings.flow == PS_FLOW_AUTO) on = c->cts_latched;
    }
    if (on != c->hw_cts) {
        c->hw_cts = on;
        hal_uart_hw_cts(on);
    }
}

static void activate(core_t *c, uint8_t role) {
    c->role = role;
    c->cts_latched = false;
    c->hw_cts = false;
    c->t_lost = 0;
    hal_uart_hw_cts(false);
    hal_xcvr_power(role);
    hal_uart_route(role);
    hal_uart_format(&c->format);
    c->stats.role_changes++;
    set_state(c, ST_ACTIVE);
}

static void deactivate(core_t *c) {
    if (c->role != PS_ROLE_NONE) hal_lines_out(c->role, 0);
    hal_uart_hw_cts(false);
    c->hw_cts = false;
    hal_uart_route(PS_ROLE_NONE);
    hal_xcvr_power(PS_ROLE_NONE);
    c->role = PS_ROLE_NONE;
    c->outputs = c->inputs = 0;
    c->seen_dce = c->seen_dte = 0;
    set_state(c, ST_SETTLE);
}

// Apply the mode setting from scratch (startup, or after SET_MODE).
static void restart(core_t *c) {
    if (c->role != PS_ROLE_NONE) deactivate(c);
    if (c->settings.mode == PS_MODE_DCE) activate(c, PS_ROLE_DCE);
    else if (c->settings.mode == PS_MODE_DTE) activate(c, PS_ROLE_DTE);
    else {
        hal_xcvr_power(PS_ROLE_NONE);
        hal_uart_route(PS_ROLE_NONE);
        set_state(c, ST_SETTLE);
    }
}

void core_init(core_t *c) {
    memset(c, 0, sizeof *c);
    ring_init(&c->to_host);
    ring_init(&c->to_rs232);
    c->format = DEFAULT_FORMAT;
    c->rx_room = true;
    uint8_t blob[CORE_SETTINGS_BLOB];
    if (!(hal_settings_load(blob, sizeof blob) && core_settings_unpack(&c->settings, blob))) {
        c->settings.mode = PS_MODE_AUTO;
        c->settings.flow = PS_FLOW_AUTO;
    }
    restart(c);
}

// Role detection (auto mode only)
static void detect(core_t *c, uint32_t now) {
    if (c->state == ST_SETTLE) {
        if (!elapsed(now, c->t_state, CORE_SETTLE_MS)) return;
        set_state(c, ST_DETECT);
        c->t_sample = now - CORE_DETECT_PERIOD_MS;
    }
    if (c->state == ST_DETECT) {
        if (!elapsed(now, c->t_sample, CORE_DETECT_PERIOD_MS)) return;
        c->t_sample = now;
        bool dce = hal_peer_dce(), dte = hal_peer_dte();
        c->seen_dce = dce ? (uint8_t)(c->seen_dce + 1) : 0;
        c->seen_dte = dte ? (uint8_t)(c->seen_dte + 1) : 0;
        // a computer on the DB25 wins if both somehow look present
        if (c->seen_dce >= CORE_DETECT_CONFIRM) activate(c, PS_ROLE_DCE);
        else if (c->seen_dte >= CORE_DETECT_CONFIRM) activate(c, PS_ROLE_DTE);
        return;
    }
    // active: is the far end still there? (our own drivers don't feed this chip's detector)
    bool present = c->role == PS_ROLE_DCE ? hal_peer_dce() : hal_peer_dte();
    if (present) {
        c->t_lost = 0;
    } else {
        if (!c->t_lost) c->t_lost = now ? now : 1;
        else if (elapsed(now, c->t_lost, CORE_LOST_MS)) deactivate(c);
    }
}

static uint8_t outputs_for(core_t *c) {
    uint8_t dtr = (c->host_lines & PS_LINE_DTR) ? 1 : 0, rts = (c->host_lines & PS_LINE_RTS) ? 1 : 0;
    uint8_t out = 0, flow_line;
    if (c->role == PS_ROLE_DCE) {
        // as a null-modem cable would: the host's DTR is the computer's DCD and DSR
        if (dtr) out |= PS_LINE_DCD | PS_LINE_DSR;
        if (rts) out |= PS_LINE_CTS;
        flow_line = PS_LINE_CTS;
        out = (uint8_t)((out & ~c->override_mask) | (c->override_value & c->override_mask));
        out &= PS_LINE_DCD | PS_LINE_DSR | PS_LINE_RI | PS_LINE_CTS;
    } else if (c->role == PS_ROLE_DTE) {
        if (dtr) out |= PS_LINE_DTR;
        if (rts) out |= PS_LINE_RTS;
        flow_line = PS_LINE_RTS;
        out = (uint8_t)((out & ~c->override_mask) | (c->override_value & c->override_mask));
        out &= PS_LINE_DTR | PS_LINE_RTS;
    } else {
        return 0;
    }
    if (!c->rx_room) out &= (uint8_t)~flow_line;   // our buffer to the host is filling: hold the far end
    return out;
}

static void lines(core_t *c, uint32_t now) {
    uint32_t used = ring_count(&c->to_host);
    if (c->rx_room && used >= ROOM_OFF) c->rx_room = false;
    else if (!c->rx_room && used <= ROOM_ON) c->rx_room = true;

    if (c->state != ST_ACTIVE) return;
    uint8_t out = outputs_for(c);
    if (out != c->outputs) {
        c->outputs = out;
        hal_lines_out(c->role, out);
    }
    c->inputs = hal_lines_in(c->role);

    // FLOW_AUTO: obey the far end's flow line only once it has shown it uses it
    uint8_t peer_flow = c->role == PS_ROLE_DCE ? PS_LINE_RTS : PS_LINE_CTS;
    if (!c->cts_latched && (c->inputs & peer_flow)) c->cts_latched = true;
    apply_flow(c);

    if (c->break_until && (int32_t)(now - c->break_until) >= 0) {
        c->break_until = 0;
        c->break_on = false;
        hal_uart_break(false);
    }
}

static void leds(core_t *c, uint32_t now) {
    uint8_t m = CORE_LED_PWR;
    if (c->role == PS_ROLE_DCE) {
        if (c->inputs & PS_LINE_DTR) m |= CORE_LED_TR;
        if (c->outputs & PS_LINE_DCD) m |= CORE_LED_CD;
        if (c->outputs & PS_LINE_RI) m |= CORE_LED_RI;
    } else if (c->role == PS_ROLE_DTE) {
        if (c->outputs & PS_LINE_DTR) m |= CORE_LED_TR;
        if (c->inputs & PS_LINE_DCD) m |= CORE_LED_CD;
        if (c->inputs & PS_LINE_RI) m |= CORE_LED_RI;
    }
    if (c->t_sd && !elapsed(now, c->t_sd, CORE_LED_PULSE_MS)) m |= CORE_LED_SD;
    if (c->t_rd && !elapsed(now, c->t_rd, CORE_LED_PULSE_MS)) m |= CORE_LED_RD;
    hal_leds(m);
}

void core_poll(core_t *c) {
    uint32_t now = hal_millis();
    if (c->settings.mode == PS_MODE_AUTO) detect(c, now);
    lines(c, now);

    if (c->stats.to_rs232 != c->seen_to_rs232) { c->seen_to_rs232 = c->stats.to_rs232; c->t_sd = now ? now : 1; }
    if (c->stats.from_rs232 != c->seen_from_rs232) { c->seen_from_rs232 = c->stats.from_rs232; c->t_rd = now ? now : 1; }
    leds(c, now);

    if (ring_count(&c->to_rs232)) hal_uart_kick_tx();

    if (c->settings_dirty && elapsed(now, c->t_dirty, CORE_SAVE_DELAY_MS)) {
        uint8_t blob[CORE_SETTINGS_BLOB];
        core_settings_pack(&c->settings, blob);
        hal_settings_save(blob, sizeof blob);
        c->settings_dirty = false;
    }
}

void core_host_lines(core_t *c, bool dtr, bool rts) {
    c->host_lines = (uint8_t)((dtr ? PS_LINE_DTR : 0) | (rts ? PS_LINE_RTS : 0));
}

void core_host_format(core_t *c, uint32_t baud, uint8_t stop_cdc, uint8_t parity_cdc, uint8_t data_bits) {
    uart_format_t f = {baud ? baud : c->format.baud, data_bits, 1, 0};
    uint8_t flags = 0;
    if (f.data_bits < 5 || f.data_bits > 8) { f.data_bits = 8; flags |= PS_INFO_CODING_ADJUSTED; }
    // CDC: stop 0 = 1, 1 = 1.5, 2 = 2; parity 0 none, 1 odd, 2 even, 3 mark, 4 space
    if (stop_cdc == 0) f.stop_bits = 1;
    else { f.stop_bits = 2; if (stop_cdc == 1) flags |= PS_INFO_CODING_ADJUSTED; }
    if (parity_cdc <= 2) f.parity = parity_cdc;
    else { f.parity = 0; flags |= PS_INFO_CODING_ADJUSTED; }
    if (f.baud > 250000) flags |= PS_INFO_BAUD_OVER_RATING;
    c->info_flags = (uint8_t)((c->info_flags & ~(PS_INFO_CODING_ADJUSTED | PS_INFO_BAUD_OVER_RATING)) | flags);
    c->format = f;
    if (c->role != PS_ROLE_NONE) hal_uart_format(&c->format);
}

void core_host_break(core_t *c, uint16_t ms) {
    if (ms == 0) {
        c->break_until = 0;
        c->break_on = false;
        hal_uart_break(false);
        return;
    }
    c->break_on = true;
    hal_uart_break(true);
    if (ms == 0xFFFF) c->break_until = 0;   // until the host sends 0
    else {
        uint32_t t = hal_millis() + ms;
        c->break_until = t ? t : 1;
    }
}

uint32_t core_host_write(core_t *c, const uint8_t *data, uint32_t len) {
    uint32_t n = ring_write(&c->to_rs232, data, len);
    if (n) hal_uart_kick_tx();
    return n;
}

uint32_t core_host_read(core_t *c, uint8_t *data, uint32_t max) { return ring_read(&c->to_host, data, max); }

bool core_serial_state(core_t *c, uint16_t *state) {
    uint16_t s = 0;
    if (c->role == PS_ROLE_DCE) {
        if (c->inputs & PS_LINE_DTR) s |= SS_DSR;   // the computer's DTR, as a null modem would show it
        if (c->t_lost == 0) s |= SS_DCD;            // a computer is attached
    } else if (c->role == PS_ROLE_DTE) {
        if (c->inputs & PS_LINE_DCD) s |= SS_DCD;
        if (c->inputs & PS_LINE_DSR) s |= SS_DSR;
        if (c->inputs & PS_LINE_RI) s |= SS_RI;
    }
    uint32_t irq = hal_irq_save();
    uint8_t e = c->rx_errors;
    c->rx_errors = 0;
    hal_irq_restore(irq);
    if (e & RX_ERR_FRAMING) { s |= SS_FRAMING; c->stats.framing++; }
    if (e & RX_ERR_PARITY) { s |= SS_PARITY; c->stats.parity++; }
    if (e & RX_ERR_BREAK) { s |= SS_BREAK; c->stats.breaks++; }
    if (e & RX_ERR_OVERRUN) { s |= SS_OVERRUN; c->stats.overrun++; }
    bool changed = !c->serial_state_valid || (s & SS_EVENTS) || s != c->serial_state_sent;
    c->serial_state_sent = (uint16_t)(s & ~SS_EVENTS);   // events are one-shot
    c->serial_state_valid = true;
    *state = s;
    return changed;
}

int core_vendor_in(core_t *c, uint8_t req, uint16_t value, uint8_t *buf, uint16_t max) {
    (void)value;
    switch (req) {
    case PS_REQ_GET_INFO: {
        ps_info_t i = {PS_PROTOCOL_VERSION, PS_FW_MAJOR, PS_FW_MINOR, PS_FW_PATCH, PS_BOARD_ID,
                       c->settings.mode, c->role, c->settings.flow,
                       (uint8_t)(c->info_flags | (c->hw_cts ? PS_INFO_FLOW_ACTIVE : 0)), c->format.baud};
        uint16_t n = max < sizeof i ? max : (uint16_t)sizeof i;
        memcpy(buf, &i, n);
        return n;
    }
    case PS_REQ_GET_INPUTS: {
        ps_inputs_t i = {c->role, c->inputs, c->outputs, hal_detect_bits(), c->host_lines};
        uint16_t n = max < sizeof i ? max : (uint16_t)sizeof i;
        memcpy(buf, &i, n);
        return n;
    }
    case PS_REQ_GET_STATS: {
        uint16_t n = max < sizeof c->stats ? max : (uint16_t)sizeof c->stats;
        memcpy(buf, &c->stats, n);
        return n;
    }
    default:
        return -1;
    }
}

static void mark_dirty(core_t *c) {
    c->settings_dirty = true;
    c->t_dirty = hal_millis();
}

bool core_vendor_out(core_t *c, uint8_t req, uint16_t value) {
    switch (req) {
    case PS_REQ_SET_MODE:
        if (value > PS_MODE_DTE) return false;
        if (value != c->settings.mode) {
            c->settings.mode = (uint8_t)value;
            mark_dirty(c);
            restart(c);
        }
        return true;
    case PS_REQ_SET_FLOW:
        if (value > PS_FLOW_OFF) return false;
        if (value != c->settings.flow) {
            c->settings.flow = (uint8_t)value;
            mark_dirty(c);
            apply_flow(c);
        }
        return true;
    case PS_REQ_SET_OUTPUTS:
        c->override_mask = (uint8_t)(value >> 8);
        c->override_value = (uint8_t)value;
        return true;
    case PS_REQ_CLEAR_STATS: {
        uint32_t irq = hal_irq_save();
        memset(&c->stats, 0, sizeof c->stats);
        hal_irq_restore(irq);
        return true;
    }
    default:
        return false;
    }
}

// Settings blob: magic, version, fields, CRC-32 over the rest.
static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
    }
    return ~crc;
}

void core_settings_pack(const core_settings_t *s, uint8_t out[CORE_SETTINGS_BLOB]) {
    memset(out, 0, CORE_SETTINGS_BLOB);
    memcpy(out, "PSER", 4);
    out[4] = 1;   // version
    out[5] = s->mode;
    out[6] = s->flow;
    uint32_t crc = crc32(out, CORE_SETTINGS_BLOB - 4);
    memcpy(out + CORE_SETTINGS_BLOB - 4, &crc, 4);
}

bool core_settings_unpack(core_settings_t *s, const uint8_t in[CORE_SETTINGS_BLOB]) {
    uint32_t crc;
    memcpy(&crc, in + CORE_SETTINGS_BLOB - 4, 4);
    if (memcmp(in, "PSER", 4) != 0 || in[4] != 1 || crc != crc32(in, CORE_SETTINGS_BLOB - 4)) return false;
    if (in[5] > PS_MODE_DTE || in[6] > PS_FLOW_OFF) return false;
    s->mode = in[5];
    s->flow = in[6];
    return true;
}
