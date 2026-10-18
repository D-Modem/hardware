/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// Host links and port ownership. See link.h and SPEC-W.md 4.4, 4.6.
#include "link.h"
#include <string.h>

links_t g_links;

// Who should own the RS-232 port now (link.h's three rules): USB with DTR
// set, else the earliest-connected wireless link, else USB if plugged in.
static uint8_t pick_owner(const links_t *s) {
    const link_t *usb = &s->l[LINK_USB];
    if (usb->up && usb->dtr) return LINK_USB;
    uint8_t best = LINK_NONE;
    for (uint8_t i = LINK_BLE; i < LINK_COUNT; i++)
        if (s->l[i].up && (best == LINK_NONE || s->l[i].order < s->l[best].order)) best = i;
    if (best != LINK_NONE) return best;
    return usb->up ? LINK_USB : LINK_NONE;
}

// Hand the core the owner's lines and DCD, and on a change of owner its coding.
static void apply(links_t *s, core_t *c, bool coding) {
    if (s->owner == LINK_NONE) {
        core_host_lines(c, false, false);
        core_host_dcd(c, -1);
        return;
    }
    const link_t *l = &s->l[s->owner];
    core_host_lines(c, l->dtr, l->rts);
    core_host_dcd(c, l->rc_dcd ? l->dcd : -1);
    if (coding && l->has_format) core_host_format(c, l->baud, l->stop_cdc, l->parity_cdc, l->data_bits);
}

// After any link event: re-pick the owner, count a change (ending the old
// owner's break), and pass the owner's state to the core.
static void update(links_t *s, core_t *c) {
    uint8_t o = pick_owner(s);
    bool changed = o != s->owner;
    if (changed) {
        s->owner = o;
        s->owner_changes++;
        if (c->break_on) core_host_break(c, 0);   // the old owner's break ends with it
    }
    apply(s, c, changed);
}

void links_init(links_t *s, core_t *c) {
    memset(s, 0, sizeof *s);
    s->owner = LINK_NONE;
    for (uint8_t i = 0; i < LINK_COUNT; i++) s->l[i].dcd = -1;
    s->l[LINK_SPP].rc_dcd = true;
    apply(s, c, false);
}

void link_up(links_t *s, core_t *c, uint8_t id, bool up) {
    link_t *l = &s->l[id];
    if (l->up == up) return;
    l->up = up;
    l->n = 0;
    l->overflow = false;
    l->dcd = l->rc_dcd ? 0 : -1;   // no call yet
    if (up) {
        l->order = ++s->order;
        bool wireless = id != LINK_USB;
        l->dtr = l->rts = wireless;
        l->has_format = false;
    } else {
        l->dtr = l->rts = false;
    }
    update(s, c);
}

void link_lines(links_t *s, core_t *c, uint8_t id, bool dtr, bool rts) {
    s->l[id].dtr = dtr;
    s->l[id].rts = rts;
    update(s, c);
}

void link_format(links_t *s, core_t *c, uint8_t id, uint32_t baud, uint8_t stop_cdc, uint8_t parity_cdc,
                 uint8_t data_bits) {
    link_t *l = &s->l[id];
    l->has_format = true;
    l->baud = baud;
    l->stop_cdc = stop_cdc;
    l->parity_cdc = parity_cdc;
    l->data_bits = data_bits;
    if (s->owner == id) core_host_format(c, baud, stop_cdc, parity_cdc, data_bits);
}

bool link_break(links_t *s, core_t *c, uint8_t id, uint16_t ms) {
    if (s->owner != id) return false;
    core_host_break(c, ms);
    return true;
}

void link_rc_dcd(links_t *s, core_t *c, uint8_t id, bool on) {
    link_t *l = &s->l[id];
    l->rc_dcd = on;
    l->dcd = on ? 0 : -1;
    l->n = 0;
    if (s->owner == id) apply(s, c, false);
}

// A complete line from the phone: is it a result code that changes DCD?
static void rc_line(links_t *s, core_t *c, uint8_t id, link_t *l) {
    int8_t dcd = l->dcd;
    if (l->n >= 7 && memcmp(l->line, "CONNECT", 7) == 0 && (l->n == 7 || l->line[7] == ' ' || l->line[7] == '/'))
        dcd = 1;
    else if (l->n == 10 && memcmp(l->line, "NO CARRIER", 10) == 0)
        dcd = 0;
    if (dcd != l->dcd) {
        l->dcd = dcd;
        if (s->owner == id) core_host_dcd(c, dcd);
    }
}

// Split the link's outgoing data into lines for rc_line(); lines too long
// to be a result code are skipped. Keeps a partial line between calls.
static void rc_scan(links_t *s, core_t *c, uint8_t id, const uint8_t *p, uint32_t n) {
    link_t *l = &s->l[id];
    for (uint32_t i = 0; i < n; i++) {
        uint8_t b = p[i];
        if (b == '\r' || b == '\n') {
            if (l->n && !l->overflow) rc_line(s, c, id, l);
            l->n = 0;
            l->overflow = false;
        } else if (l->n < sizeof l->line) {
            l->line[l->n++] = (char)b;
        } else {
            l->overflow = true;   // too long to be a result code
        }
    }
}

uint32_t link_write(links_t *s, core_t *c, uint8_t id, const uint8_t *data, uint32_t len) {
    if (s->owner != id) return len;
    uint32_t n = core_host_write(c, data, len);
    if (s->l[id].rc_dcd) rc_scan(s, c, id, data, n);
    return n;
}

uint32_t link_read(links_t *s, core_t *c, uint8_t id, uint8_t *data, uint32_t max) {
    if (s->owner != id) return 0;
    return core_host_read(c, data, max);
}
