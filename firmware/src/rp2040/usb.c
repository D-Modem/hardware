/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// USB: descriptors, the CDC serial port, the "pico-serial control" vendor
// interface and Raspberry Pi's reset interface (picotool reboot -u).
#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"
#include "pico/usb_reset.h"
#include "board_config.h"
#include "core.h"
#include "link.h"
#include "usb.h"
#if defined(PS_BOARD_W)
#include "bt.h"
#endif

enum { ITF_CDC_COMM, ITF_CDC_DATA, ITF_CONTROL, ITF_RESET, ITF_COUNT };
enum { STR_LANG, STR_MANUFACTURER, STR_PRODUCT, STR_SERIAL, STR_CDC, STR_CONTROL, STR_RESET };

#define EP_CDC_NOTIF 0x81
#define EP_CDC_OUT 0x02
#define EP_CDC_IN 0x82
#define CONTROL_DESC_LEN 9

// ---------------------------------------------------------------------------
// Descriptors
// ---------------------------------------------------------------------------
static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    // composite with an IAD for the CDC pair
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = PS_USB_VID,
    .idProduct = PS_USB_PID,
    .bcdDevice = (PS_FW_MAJOR << 8) | (PS_FW_MINOR << 4) | PS_FW_PATCH,
    .iManufacturer = STR_MANUFACTURER,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + CONTROL_DESC_LEN + TUD_RPI_RESET_DESC_LEN)

static const uint8_t desc_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, 0, 100),   // bus powered, 100 mA
    TUD_CDC_DESCRIPTOR(ITF_CDC_COMM, STR_CDC, EP_CDC_NOTIF, 16, EP_CDC_OUT, EP_CDC_IN, 64),
    // vendor control interface: requests only, no endpoints (protocol.h)
    CONTROL_DESC_LEN, TUSB_DESC_INTERFACE, ITF_CONTROL, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC,
    PS_ITF_SUBCLASS, PS_ITF_PROTOCOL, STR_CONTROL,
    TUD_RPI_RESET_DESCRIPTOR(ITF_RESET, STR_RESET),
};
TU_VERIFY_STATIC(sizeof desc_config == CONFIG_LEN, "config descriptor length");

const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&desc_device; }
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return desc_config; }

static uint16_t desc_str[33];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    const char *s;
    switch (index) {
    case STR_LANG:
        desc_str[1] = 0x0409;
        desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | 4);
        return desc_str;
    case STR_MANUFACTURER: s = PS_MANUFACTURER; break;
    case STR_PRODUCT: s = PS_PRODUCT; break;
    case STR_SERIAL: pico_get_unique_board_id_string(serial, sizeof serial); s = serial; break;
    case STR_CDC: s = "pico-serial port"; break;
    case STR_CONTROL: s = "pico-serial control"; break;
    case STR_RESET: s = "Reset"; break;
    default: return NULL;
    }
    size_t n = strlen(s);
    if (n > 32) n = 32;
    for (size_t i = 0; i < n; i++) desc_str[1 + i] = (uint8_t)s[i];
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return desc_str;
}

// ---------------------------------------------------------------------------
// "pico-serial control": vendor requests to the core (protocol.h)
// ---------------------------------------------------------------------------
static uint8_t s_control_itf = 0xFF;
static uint8_t s_control_buf[64];

static void psd_init(void) {}
static void psd_reset(uint8_t rhport) { (void)rhport; s_control_itf = 0xFF; }

static uint16_t psd_open(uint8_t rhport, const tusb_desc_interface_t *d, uint16_t max_len) {
    (void)rhport;
    TU_VERIFY(d->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC && d->bInterfaceSubClass == PS_ITF_SUBCLASS &&
              d->bInterfaceProtocol == PS_ITF_PROTOCOL, 0);
    TU_VERIFY(max_len >= sizeof(tusb_desc_interface_t), 0);
    s_control_itf = d->bInterfaceNumber;
    return sizeof(tusb_desc_interface_t);
}

static bool psd_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *r) {
    if (r->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR || r->bmRequestType_bit.recipient != TUSB_REQ_RCPT_INTERFACE ||
        (r->wIndex & 0xFF) != s_control_itf)
        return false;
    if (stage != CONTROL_STAGE_SETUP) return true;
    if (r->bmRequestType_bit.direction == TUSB_DIR_IN) {
        uint16_t max = r->wLength < sizeof s_control_buf ? r->wLength : (uint16_t)sizeof s_control_buf;
        int n = core_vendor_in(&g_core, r->bRequest, r->wValue, s_control_buf, max);
        if (n < 0) return false;
        return tud_control_xfer(rhport, r, s_control_buf, (uint16_t)n);
    }
    if (r->wLength != 0) return false;
#if defined(PS_BOARD_W)
    if (r->bRequest == PS_REQ_BT_PAIRING) { bt_pairing_window(r->wValue); return tud_control_status(rhport, r); }
    if (r->bRequest == PS_REQ_BT_FORGET) { bt_forget_all(); return tud_control_status(rhport, r); }
#endif
    if (!core_vendor_out(&g_core, r->bRequest, r->wValue)) return false;
    return tud_control_status(rhport, r);
}

// TinyUSB hands every vendor-type request to this one callback, whatever the
// recipient; ours are the ones addressed to the control interface.
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *r) {
    return psd_control_xfer_cb(rhport, stage, r);
}

static bool psd_xfer_cb(uint8_t rhport, uint8_t ep, xfer_result_t res, uint32_t n) {
    (void)rhport; (void)ep; (void)res; (void)n;
    return true;
}

static const usbd_class_driver_t ps_control_driver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "PS-CONTROL",
#endif
    .init = psd_init,
    .reset = psd_reset,
    .open = psd_open,
    .control_xfer_cb = psd_control_xfer_cb,
    .xfer_cb = psd_xfer_cb,
};

static usbd_class_driver_t s_drivers[2];

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *count) {
    s_drivers[0] = ps_control_driver;
    s_drivers[1] = usb_reset_interface_driver;
    *count = 2;
    return s_drivers;
}

// ---------------------------------------------------------------------------
// CDC serial port
// ---------------------------------------------------------------------------
// Everything goes through the links layer, which decides whether USB owns
// the RS-232 port (link.h): on the wired boards it always does.
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) { (void)itf; link_lines(&g_links, &g_core, LINK_USB, dtr, rts); }

void tud_cdc_line_coding_cb(uint8_t itf, const cdc_line_coding_t *lc) {
    (void)itf;
    link_format(&g_links, &g_core, LINK_USB, lc->bit_rate, lc->stop_bits, lc->parity, lc->data_bits);
}

void tud_cdc_send_break_cb(uint8_t itf, uint16_t ms) { (void)itf; link_break(&g_links, &g_core, LINK_USB, ms); }

void tud_mount_cb(void) {
    g_core.serial_state_valid = false;   // re-announce the lines
    link_up(&g_links, &g_core, LINK_USB, true);
}
void tud_umount_cb(void) { link_up(&g_links, &g_core, LINK_USB, false); }
void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    link_lines(&g_links, &g_core, LINK_USB, false, false);
}

static uint16_t s_pending_state;
static bool s_pending;

void usb_init(void) {
    const tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    tusb_init(0, &init);
}

void usb_service(void) {
    tud_task();
    if (!tud_mounted()) return;

    // Not the owner (a wireless link has the port and ours isn't open): drop
    // what the host sends, send it nothing.
    static bool was_owner;
    bool owner = link_owns(&g_links, LINK_USB);
    if (owner != was_owner) {
        was_owner = owner;
        g_core.serial_state_valid = false;
    }
    uint8_t buf[64];
    if (!owner) {
        while (tud_cdc_available()) tud_cdc_read(buf, sizeof buf);
        return;
    }

    // host -> RS-232: take only what fits, so the host is held back (NAK) when we're full
    uint32_t room = ring_free(&g_core.to_rs232);
    while (room && tud_cdc_available()) {
        uint32_t n = tud_cdc_read(buf, room < sizeof buf ? room : sizeof buf);
        if (!n) break;
        link_write(&g_links, &g_core, LINK_USB, buf, n);
        room -= n;
    }

    // RS-232 -> host
    uint32_t space = tud_cdc_write_available();
    if (space && ring_count(&g_core.to_host)) {
        uint32_t n = link_read(&g_links, &g_core, LINK_USB, buf, space < sizeof buf ? space : sizeof buf);
        tud_cdc_write(buf, n);
        tud_cdc_write_flush();
    }

    // modem lines and errors -> host (SERIAL_STATE); keep events until sent
    uint16_t s;
    if (core_serial_state(&g_core, &s)) {
        s_pending_state = (uint16_t)(s | (s_pending ? (s_pending_state & SS_EVENTS) : 0));
        s_pending = true;
    }
    if (s_pending) {
        cdc_notify_uart_state_t st = {.value = s_pending_state};
        if (tud_cdc_n_notify_uart_state(0, &st)) s_pending = false;
    }
}
