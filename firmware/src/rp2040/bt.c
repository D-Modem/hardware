/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// Bluetooth on pico-serial W (SPEC-W.md 4). BTstack on the RM2 (CYW43439),
// polled from the main loop (pico_cyw43_arch_poll), so every callback here
// runs in the same context as the USB code and the core.
//
//   classic: an SPP server on RFCOMM ("pico-serial"), for Android and desktops;
//   LE:      the Nordic UART Service for data, and the pico-serial control
//            service (pico_serial_w.gatt) for lines and settings.
//
// Both are host links (link.h): whichever connected first owns the RS-232
// port unless USB has its port open. Pairing only inside a pairing window.
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"
#include "board_config.h"
#include "core.h"
#include "link.h"
#include "bt.h"
#include "pico_serial_w.h"   // GATT database, generated from pico_serial_w.gatt

#define PAIR_WINDOW_S 120u          // SPEC-W 4.5: 2 minutes
#define FORGET_HOLD_MS 10000u       // hold the button this long to forget all phones
#define BUTTON_DEBOUNCE_MS 50u
#define RFCOMM_CHANNEL 1
#define SPP_MAX_FRAME 1000u         // RFCOMM frame size we offer
#define SPP_MAX_CREDITS 3u          // frames the phone may send ahead
#define CREDITS_NOTIFY_STEP 256u    // notify the free space when it moves this much

// handles from the generated GATT database
#define H_NAME ATT_CHARACTERISTIC_GAP_DEVICE_NAME_01_VALUE_HANDLE
#define H_NUS_RX ATT_CHARACTERISTIC_6E400002_B5A3_F393_E0A9_E50E24DCCA9E_01_VALUE_HANDLE
#define H_NUS_TX ATT_CHARACTERISTIC_6E400003_B5A3_F393_E0A9_E50E24DCCA9E_01_VALUE_HANDLE
#define H_NUS_TX_CCC ATT_CHARACTERISTIC_6E400003_B5A3_F393_E0A9_E50E24DCCA9E_01_CLIENT_CONFIGURATION_HANDLE
#define H_CODING ATT_CHARACTERISTIC_50530002_5C6F_4E2B_9A6B_1D2C0E5F7A10_01_VALUE_HANDLE
#define H_OUTPUTS ATT_CHARACTERISTIC_50530003_5C6F_4E2B_9A6B_1D2C0E5F7A10_01_VALUE_HANDLE
#define H_INPUTS ATT_CHARACTERISTIC_50530004_5C6F_4E2B_9A6B_1D2C0E5F7A10_01_VALUE_HANDLE
#define H_INPUTS_CCC ATT_CHARACTERISTIC_50530004_5C6F_4E2B_9A6B_1D2C0E5F7A10_01_CLIENT_CONFIGURATION_HANDLE
#define H_MODE ATT_CHARACTERISTIC_50530005_5C6F_4E2B_9A6B_1D2C0E5F7A10_01_VALUE_HANDLE
#define H_BREAK ATT_CHARACTERISTIC_50530006_5C6F_4E2B_9A6B_1D2C0E5F7A10_01_VALUE_HANDLE
#define H_FLOW ATT_CHARACTERISTIC_50530007_5C6F_4E2B_9A6B_1D2C0E5F7A10_01_VALUE_HANDLE
#define H_CREDITS ATT_CHARACTERISTIC_50530008_5C6F_4E2B_9A6B_1D2C0E5F7A10_01_VALUE_HANDLE
#define H_CREDITS_CCC ATT_CHARACTERISTIC_50530008_5C6F_4E2B_9A6B_1D2C0E5F7A10_01_CLIENT_CONFIGURATION_HANDLE
#define H_INFO ATT_CHARACTERISTIC_50530009_5C6F_4E2B_9A6B_1D2C0E5F7A10_01_VALUE_HANDLE

static bool s_ok;                   // radio up
static bool s_working;              // HCI running
static char s_name[20] = "pico-serial";
static btstack_packet_callback_registration_t s_hci_cb, s_sm_cb;
static uint8_t s_spp_sdp[150];

// pairing window
static uint32_t s_pair_until;       // 0 = closed
static uint32_t s_btn_down;         // when the button went down (0 = up)
static bool s_btn_handled, s_btn_forgot;

// BLE
static hci_con_handle_t s_le = HCI_CON_HANDLE_INVALID;
static bool s_le_secure;            // encrypted: counts as a link
static bool s_nus_notify, s_inputs_notify, s_credits_notify;
static bool s_le_pending;           // asked for a "can send now"
static uint8_t s_inputs_sent[sizeof(ps_inputs_t)];
static bool s_inputs_valid;
static uint32_t s_credits_sent;

// SPP
static uint16_t s_spp;              // RFCOMM channel id, 0 = none
static uint16_t s_spp_frame;
static uint8_t s_spp_credits;       // frames the phone may still send
static bool s_spp_pending;

// LEDs
static uint8_t s_wl_leds = 0xFF;    // RD/SD as last written to the RM2

static bool pairing_open(void) { return s_pair_until != 0; }

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

// ---------------------------------------------------------------------------
// Advertising: NUS UUID in the advertisement, the name in the scan response.
// Discoverable flag only while the pairing window is open.
// ---------------------------------------------------------------------------
static uint8_t s_adv[3 + 18];
static uint8_t s_scan[2 + sizeof s_name];

static void advertise(void) {
    if (!s_working) return;
    static const uint8_t nus[16] = {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                                    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E};
    uint8_t n = 0;
    s_adv[n++] = 2;
    s_adv[n++] = BLUETOOTH_DATA_TYPE_FLAGS;
    s_adv[n++] = pairing_open() ? 0x06 : 0x04;   // LE general discoverable (window only); no BR/EDR on LE
    s_adv[n++] = 17;
    s_adv[n++] = BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS;
    memcpy(s_adv + n, nus, sizeof nus);
    n = (uint8_t)(n + sizeof nus);
    gap_advertisements_set_data(n, s_adv);

    uint8_t len = (uint8_t)strlen(s_name);
    s_scan[0] = (uint8_t)(len + 1);
    s_scan[1] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(s_scan + 2, s_name, len);
    gap_scan_response_set_data((uint8_t)(len + 2), s_scan);

    bd_addr_t none = {0};
    gap_advertisements_set_params(0x00A0, 0x0140, 0 /* ADV_IND */, 0, none, 0x07, 0);
    gap_advertisements_enable(1);
}

static void set_window(uint32_t seconds) {
    if (seconds) {
        uint32_t t = now_ms() + seconds * 1000u;
        s_pair_until = t ? t : 1;
    } else {
        s_pair_until = 0;
    }
    if (!s_working) return;
    gap_discoverable_control(pairing_open() ? 1 : 0);
    advertise();
}

void bt_pairing_window(uint16_t seconds) { set_window(seconds); }

static bool have_bonds(void) {
    if (le_device_db_count() > 0) return true;
    btstack_link_key_iterator_t it;
    bool any = false;
    if (gap_link_key_iterator_init(&it)) {
        bd_addr_t a;
        link_key_t k;
        link_key_type_t t;
        any = gap_link_key_iterator_get_next(&it, a, k, &t) != 0;
        gap_link_key_iterator_done(&it);
    }
    return any;
}

void bt_forget_all(void) {
    if (!s_working) return;
    gap_delete_all_link_keys();
    for (int i = 0; i < le_device_db_max_count(); i++) le_device_db_remove(i);
    set_window(PAIR_WINDOW_S);   // nothing bonded now: let a phone pair
}

// ---------------------------------------------------------------------------
// HCI, security manager
// ---------------------------------------------------------------------------
static void le_link(bool up) {
    if (up == s_le_secure) return;
    s_le_secure = up;
    s_inputs_valid = false;
    link_up(&g_links, &g_core, LINK_BLE, up);
}

static void hci_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET) return;
    bd_addr_t addr;
    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
        s_working = true;
        gap_local_bd_addr(addr);
        snprintf(s_name, sizeof s_name, "pico-serial-%02X%02X", addr[4], addr[5]);
        gap_set_local_name(s_name);
        set_window(have_bonds() ? 0 : PAIR_WINDOW_S);   // SPEC-W 4.5: open at power-up until bonded
        break;
    case HCI_EVENT_USER_CONFIRMATION_REQUEST:   // classic Secure Simple Pairing, Just Works
        hci_event_user_confirmation_request_get_bd_addr(packet, addr);
        if (pairing_open()) gap_ssp_confirmation_response(addr);
        else gap_ssp_confirmation_negative(addr);
        break;
    case HCI_EVENT_PIN_CODE_REQUEST:            // legacy pairing: never
        hci_event_pin_code_request_get_bd_addr(packet, addr);
        gap_pin_code_negative(addr);
        break;
    case HCI_EVENT_ENCRYPTION_CHANGE:
    case HCI_EVENT_ENCRYPTION_CHANGE_V2:
        if (hci_event_encryption_change_get_connection_handle(packet) == s_le)
            le_link(hci_event_encryption_change_get_status(packet) == 0 &&
                    hci_event_encryption_change_get_encryption_enabled(packet));
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        if (hci_event_disconnection_complete_get_connection_handle(packet) == s_le) {
            s_le = HCI_CON_HANDLE_INVALID;
            s_nus_notify = s_inputs_notify = s_credits_notify = s_le_pending = false;
            le_link(false);
        }
        break;
    default:
        break;
    }
}

static void sm_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET) return;
    hci_con_handle_t h;
    switch (hci_event_packet_get_type(packet)) {
    case SM_EVENT_JUST_WORKS_REQUEST:
        h = sm_event_just_works_request_get_handle(packet);
        if (pairing_open()) sm_just_works_confirm(h);
        else sm_bonding_decline(h);
        break;
    case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
        h = sm_event_numeric_comparison_request_get_handle(packet);
        if (pairing_open()) sm_numeric_comparison_confirm(h);
        else sm_bonding_decline(h);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// SPP (RFCOMM)
// ---------------------------------------------------------------------------
static void spp_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    if (type == RFCOMM_DATA_PACKET) {
        if (channel != s_spp) return;
        if (s_spp_credits) s_spp_credits--;
        link_write(&g_links, &g_core, LINK_SPP, packet, size);   // room was reserved by the credit
        return;
    }
    if (type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
    case RFCOMM_EVENT_INCOMING_CONNECTION: {
        uint16_t cid = rfcomm_event_incoming_connection_get_rfcomm_cid(packet);
        if (s_spp) rfcomm_decline_connection(cid);   // one SPP link at a time
        else rfcomm_accept_connection(cid);
        break;
    }
    case RFCOMM_EVENT_CHANNEL_OPENED:
        if (rfcomm_event_channel_opened_get_status(packet)) break;
        s_spp = rfcomm_event_channel_opened_get_rfcomm_cid(packet);
        s_spp_frame = rfcomm_event_channel_opened_get_max_frame_size(packet);
        s_spp_credits = 1;   // the initial credit given at registration
        s_spp_pending = false;
        link_up(&g_links, &g_core, LINK_SPP, true);
        break;
    case RFCOMM_EVENT_CAN_SEND_NOW: {
        s_spp_pending = false;
        static uint8_t buf[SPP_MAX_FRAME];
        uint16_t max = s_spp_frame < sizeof buf ? s_spp_frame : (uint16_t)sizeof buf;
        uint32_t n = link_read(&g_links, &g_core, LINK_SPP, buf, max);
        if (n) rfcomm_send(s_spp, buf, (uint16_t)n);
        break;
    }
    case RFCOMM_EVENT_CHANNEL_CLOSED:
        if (rfcomm_event_channel_closed_get_rfcomm_cid(packet) != s_spp) break;
        s_spp = 0;
        link_up(&g_links, &g_core, LINK_SPP, false);
        break;
    default:
        break;
    }
}

static void spp_service(void) {
    if (!s_spp) return;
    // give the phone credits while there's room for a whole frame per credit
    uint32_t frame = s_spp_frame ? s_spp_frame : 1u;
    while (s_spp_credits < SPP_MAX_CREDITS && ring_free(&g_core.to_rs232) >= (s_spp_credits + 1u) * frame) {
        rfcomm_grant_credits(s_spp, 1);
        s_spp_credits++;
    }
    if (!s_spp_pending && link_owns(&g_links, LINK_SPP) && ring_count(&g_core.to_host)) {
        s_spp_pending = true;
        rfcomm_request_can_send_now_event(s_spp);
    }
}

// ---------------------------------------------------------------------------
// GATT
// ---------------------------------------------------------------------------
static uint16_t blob(const void *p, uint16_t n, uint16_t offset, uint8_t *buf, uint16_t size) {
    return att_read_callback_handle_blob(p, n, offset, buf, size);
}

static uint16_t att_read(hci_con_handle_t con, uint16_t h, uint16_t offset, uint8_t *buf, uint16_t size) {
    (void)con;
    core_t *c = &g_core;
    uint8_t tmp[sizeof(ps_info_t)];
    switch (h) {
    case H_NAME:
        return blob(s_name, (uint16_t)strlen(s_name), offset, buf, size);
    case H_CODING:
        little_endian_store_32(tmp, 0, c->format.baud);
        tmp[4] = c->format.data_bits;
        tmp[5] = c->format.parity;
        tmp[6] = c->format.stop_bits;
        return blob(tmp, 7, offset, buf, size);
    case H_OUTPUTS:
        tmp[0] = c->override_mask;
        tmp[1] = c->outputs;
        return blob(tmp, 2, offset, buf, size);
    case H_INPUTS:
        return blob(tmp, (uint16_t)core_vendor_in(c, PS_REQ_GET_INPUTS, 0, tmp, sizeof tmp), offset, buf, size);
    case H_INFO:
        return blob(tmp, (uint16_t)core_vendor_in(c, PS_REQ_GET_INFO, 0, tmp, sizeof tmp), offset, buf, size);
    case H_MODE:
        tmp[0] = c->settings.mode;
        tmp[1] = c->role;
        return blob(tmp, 2, offset, buf, size);
    case H_FLOW:
        tmp[0] = c->settings.flow;
        return blob(tmp, 1, offset, buf, size);
    case H_CREDITS: {
        uint32_t f = ring_free(&c->to_rs232);
        little_endian_store_16(tmp, 0, (uint16_t)(f > 0xFFFF ? 0xFFFF : f));
        return blob(tmp, 2, offset, buf, size);
    }
    case H_NUS_TX_CCC: case H_INPUTS_CCC: case H_CREDITS_CCC: {
        bool on = h == H_NUS_TX_CCC ? s_nus_notify : h == H_INPUTS_CCC ? s_inputs_notify : s_credits_notify;
        little_endian_store_16(tmp, 0, on ? GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION : 0);
        return blob(tmp, 2, offset, buf, size);
    }
    default:
        return 0;
    }
}

static int att_write(hci_con_handle_t con, uint16_t h, uint16_t mode, uint16_t offset, uint8_t *buf, uint16_t size) {
    (void)mode;
    if (con != s_le) return 0;
    if (offset) return ATT_ERROR_INVALID_OFFSET;
    core_t *c = &g_core;
    bool owner = link_owns(&g_links, LINK_BLE);
    switch (h) {
    case H_NUS_RX:
        // another link owns the port: read-only (SPEC-W 4.6)
        if (!owner) return ATT_ERROR_WRITE_NOT_PERMITTED;
        link_write(&g_links, c, LINK_BLE, buf, size);
        return 0;
    case H_NUS_TX_CCC: case H_INPUTS_CCC: case H_CREDITS_CCC: {
        if (size < 2) return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        bool on = (little_endian_read_16(buf, 0) & GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION) != 0;
        if (h == H_NUS_TX_CCC) s_nus_notify = on;
        else if (h == H_INPUTS_CCC) { s_inputs_notify = on; s_inputs_valid = false; }
        else { s_credits_notify = on; s_credits_sent = 0; }
        return 0;
    }
    case H_CODING:
        if (size != 7) return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        // kept for when this link owns the port; CDC encoding for stop bits (0 = 1, 2 = 2)
        link_format(&g_links, c, LINK_BLE, little_endian_read_32(buf, 0), buf[6] == 2 ? 2 : 0, buf[5], buf[4]);
        return 0;
    case H_OUTPUTS:
        if (size != 2) return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        if (!owner) return ATT_ERROR_WRITE_NOT_PERMITTED;
        core_vendor_out(c, PS_REQ_SET_OUTPUTS, (uint16_t)(buf[0] << 8 | buf[1]));
        return 0;
    case H_MODE:
    case H_FLOW:
        if (size != 1) return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        if (!owner) return ATT_ERROR_WRITE_NOT_PERMITTED;
        if (!core_vendor_out(c, h == H_MODE ? PS_REQ_SET_MODE : PS_REQ_SET_FLOW, buf[0]))
            return ATT_ERROR_VALUE_NOT_ALLOWED;
        return 0;
    case H_BREAK:
        if (size != 2) return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        if (!link_break(&g_links, c, LINK_BLE, little_endian_read_16(buf, 0))) return ATT_ERROR_WRITE_NOT_PERMITTED;
        return 0;
    default:
        return 0;
    }
}

// what the phone should hear about next, if anything
static int le_next(uint8_t *data, uint16_t *len, uint16_t max) {
    core_t *c = &g_core;
    if (s_inputs_notify) {
        uint8_t now[sizeof(ps_inputs_t)];
        core_vendor_in(c, PS_REQ_GET_INPUTS, 0, now, sizeof now);
        if (!s_inputs_valid || memcmp(now, s_inputs_sent, sizeof now) != 0) {
            memcpy(data, now, sizeof now);
            *len = sizeof now;
            return H_INPUTS;
        }
    }
    if (s_credits_notify) {
        uint32_t f = ring_free(&c->to_rs232);
        uint32_t d = f > s_credits_sent ? f - s_credits_sent : s_credits_sent - f;
        if (d >= CREDITS_NOTIFY_STEP || (f == RING_SIZE && s_credits_sent != RING_SIZE)) {
            little_endian_store_16(data, 0, (uint16_t)(f > 0xFFFF ? 0xFFFF : f));
            *len = 2;
            return H_CREDITS;
        }
    }
    if (s_nus_notify && link_owns(&g_links, LINK_BLE) && ring_count(&c->to_host)) {
        *len = (uint16_t)ring_count(&c->to_host);
        if (*len > max) *len = max;
        return H_NUS_TX;
    }
    return 0;
}

static void att_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
    case ATT_EVENT_CONNECTED:
        s_le = att_event_connected_get_handle(packet);
        sm_request_pairing(s_le);   // bonded phones re-encrypt at once; others pair (window only)
        break;
    case ATT_EVENT_CAN_SEND_NOW: {
        s_le_pending = false;
        static uint8_t buf[256];
        uint16_t mtu = att_server_get_mtu(s_le), max = (uint16_t)(mtu > 3 ? mtu - 3 : 20);
        if (max > sizeof buf) max = sizeof buf;
        uint16_t len = 0;
        int h = le_next(buf, &len, max);
        if (h == H_NUS_TX) {
            len = (uint16_t)link_read(&g_links, &g_core, LINK_BLE, buf, len);
        } else if (h == H_INPUTS) {
            memcpy(s_inputs_sent, buf, sizeof s_inputs_sent);
            s_inputs_valid = true;
        } else if (h == H_CREDITS) {
            s_credits_sent = little_endian_read_16(buf, 0);
        }
        if (h && len) att_server_notify(s_le, (uint16_t)h, buf, len);
        break;
    }
    default:
        break;
    }
}

static void le_service(void) {
    if (s_le == HCI_CON_HANDLE_INVALID || !s_le_secure || s_le_pending) return;
    uint8_t tmp[sizeof(ps_inputs_t)];
    uint16_t len;
    if (le_next(tmp, &len, sizeof tmp)) {
        s_le_pending = true;
        att_server_request_can_send_now_event(s_le);
    }
}

// ---------------------------------------------------------------------------
// Button, LEDs
// ---------------------------------------------------------------------------
static void button(uint32_t now) {
    bool down = !gpio_get(PIN_PAIR_BTN);
    if (!down) {
        s_btn_down = 0;
        s_btn_handled = s_btn_forgot = false;
        return;
    }
    if (!s_btn_down) { s_btn_down = now ? now : 1; return; }
    uint32_t held = now - s_btn_down;
    if (!s_btn_handled && held >= BUTTON_DEBOUNCE_MS) {
        s_btn_handled = true;
        set_window(PAIR_WINDOW_S);
    }
    if (!s_btn_forgot && held >= FORGET_HOLD_MS) {
        s_btn_forgot = true;
        bt_forget_all();
    }
}

// PWR/BT: slow blink advertising, fast blink pairing window, on while connected
static void bt_led(uint32_t now) {
    bool on;
    if (!s_ok) on = true;   // no radio: just power
    else if (g_links.l[LINK_BLE].up || g_links.l[LINK_SPP].up) on = true;
    else if (s_btn_forgot) on = (now / 50u) & 1u;   // forgetting: very fast
    else if (pairing_open()) on = (now / 125u) & 1u;
    else on = (now % 1000u) < 100u;
    gpio_put(PIN_LED_BT, on);
}

void bt_leds(uint8_t core_leds) {
    if (!s_ok) return;
    uint8_t m = core_leds & (CORE_LED_RD | CORE_LED_SD);
    if (m == s_wl_leds) return;
    uint8_t changed = (uint8_t)(m ^ s_wl_leds);
    if (changed & CORE_LED_RD) cyw43_arch_gpio_put(WLGPIO_LED_RD, (m & CORE_LED_RD) != 0);
    if (changed & CORE_LED_SD) cyw43_arch_gpio_put(WLGPIO_LED_SD, (m & CORE_LED_SD) != 0);
    s_wl_leds = m;
}

// ---------------------------------------------------------------------------
bool bt_init(void) {
    gpio_init(PIN_LED_BT);
    gpio_set_dir(PIN_LED_BT, GPIO_OUT);
    gpio_put(PIN_LED_BT, 1);
    gpio_init(PIN_PAIR_BTN);
    gpio_set_dir(PIN_PAIR_BTN, GPIO_IN);
    gpio_pull_up(PIN_PAIR_BTN);

    if (cyw43_arch_init()) return false;   // also starts BTstack, with bonds kept in flash
    s_ok = true;
    s_wl_leds = 0xFF;
    bt_leds(0);

    l2cap_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING);
    s_sm_cb.callback = sm_handler;
    sm_add_event_handler(&s_sm_cb);

    // classic: SPP, Secure Simple Pairing (Just Works), encryption required
    rfcomm_init();
    rfcomm_register_service_with_initial_credits(spp_handler, RFCOMM_CHANNEL, SPP_MAX_FRAME, 1);
    sdp_init();
    memset(s_spp_sdp, 0, sizeof s_spp_sdp);
    spp_create_sdp_record(s_spp_sdp, sdp_create_service_record_handle(), RFCOMM_CHANNEL, "pico-serial");
    sdp_register_service(s_spp_sdp);
    gap_set_class_of_device(0x001F00);   // uncategorised
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_authentication_requirement(SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_GENERAL_BONDING);
    gap_ssp_set_auto_accept(0);
    gap_set_security_level(LEVEL_2);
    gap_discoverable_control(0);
    gap_connectable_control(1);
    gap_set_local_name(s_name);

    // LE: GATT server
    att_server_init(profile_data, att_read, att_write);
    att_server_register_packet_handler(att_handler);

    s_hci_cb.callback = hci_handler;
    hci_add_event_handler(&s_hci_cb);
    hci_power_control(HCI_POWER_ON);
    return true;
}

void bt_service(void) {
    uint32_t now = now_ms();
    if (s_ok) {
        cyw43_arch_poll();
        if (s_pair_until && (int32_t)(now - s_pair_until) >= 0) set_window(0);
        button(now);
        spp_service();
        le_service();
    }
    bt_led(now);
}
