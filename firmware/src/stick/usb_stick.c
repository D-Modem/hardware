/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// The sticks' USB device (the phone-line stick and the wideband stick): one
// vendor interface shaped like the Smart Link SmartUSB56's (D-Modem's
// dmodemd drives that one): alternate setting 0 with no endpoints, alternate
// setting 1 with an isochronous OUT and IN pair for the samples and an
// interrupt IN for events; plus Raspberry Pi's reset interface (picotool).
// Microsoft OS 2.0 descriptors bind WinUSB to both interfaces on Windows,
// with no INF. Each stick's numbers and requests are its own (stick.h); the
// protocols: firmware/PROTOCOL.md "Line stick" and "WB stick".
#include <string.h>
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "pico/unique_id.h"
#include "pico/usb_reset.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#include "board_config.h"
#include "stick.h"
#include "usb_stick.h"

enum { ITF_LINE, ITF_RESET, ITF_COUNT };
enum { STR_LANG, STR_MANUFACTURER, STR_PRODUCT, STR_SERIAL, STR_LINE, STR_RESET };

#define EVENT_INTERVAL_MS 4

// ---------------------------------------------------------------------------
// Descriptors
// ---------------------------------------------------------------------------
static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,              // 2.1: the BOS descriptor (Microsoft OS 2.0) is there
    .bDeviceClass = 0,             // each interface says
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = PS_USB_VID,
    .idProduct = PS_USB_PID,
    .bcdDevice = (PS_FW_MAJOR << 8) | (PS_FW_MINOR << 4) | PS_FW_PATCH,
    .iManufacturer = STR_MANUFACTURER,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

#define ITF_LEN 9
#define EP_LEN 7
#define LINE_DESC_LEN (ITF_LEN + ITF_LEN + 3 * EP_LEN)
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + LINE_DESC_LEN + TUD_RPI_RESET_DESC_LEN)
// isochronous, asynchronous: the stick's own clock sets the rate
#define ISO_ASYNC (TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS)

static const uint8_t desc_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, 0, 100),   // bus powered, 100 mA
    // alternate setting 0: no endpoints, no bandwidth
    ITF_LEN, TUSB_DESC_INTERFACE, ITF_LINE, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC, STICK_ITF_SUBCLASS, STICK_ITF_PROTOCOL,
    STR_LINE,
    // alternate setting 1: the samples and the events
    ITF_LEN, TUSB_DESC_INTERFACE, ITF_LINE, 1, 3, TUSB_CLASS_VENDOR_SPECIFIC, STICK_ITF_SUBCLASS, STICK_ITF_PROTOCOL,
    STR_LINE,
    EP_LEN, TUSB_DESC_ENDPOINT, STICK_EP_OUT, ISO_ASYNC, U16_TO_U8S_LE(STICK_PACKET_BYTES), 1,
    EP_LEN, TUSB_DESC_ENDPOINT, STICK_EP_IN, ISO_ASYNC, U16_TO_U8S_LE(STICK_PACKET_BYTES), 1,
    EP_LEN, TUSB_DESC_ENDPOINT, STICK_EP_EVENT, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(sizeof(stick_event_t)),
    EVENT_INTERVAL_MS,
    TUD_RPI_RESET_DESCRIPTOR(ITF_RESET, STR_RESET),
};
TU_VERIFY_STATIC(sizeof desc_config == CONFIG_LEN, "config descriptor length");

// TinyUSB, on GET_DESCRIPTOR(DEVICE): the device descriptor above.
const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&desc_device; }
// TinyUSB, on GET_DESCRIPTOR(CONFIGURATION): the one configuration (index is always 0).
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return desc_config; }

// Microsoft OS 2.0: the BOS descriptor names the descriptor set and the
// vendor request Windows reads it with (STICK_REQ_MS_OS_20, wIndex 7). The
// set binds WinUSB to each interface: the line one with D-Modem's interface
// GUID (as its dmodem-usb.inf gives; dmodemd drives both sticks), the reset
// one as the Pico SDK does.
#define MS_OS_20_INDEX 7
#define LINE_MS_OS_20_LEN (0x08 + 0x14 + 0x80)
#define MS_OS_20_LEN (0x0A + LINE_MS_OS_20_LEN + RPI_RESET_MS_OS_20_DESC_LEN)
#define BOS_LEN (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)
#define W16(c) (c), 0

static const uint8_t desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_LEN, STICK_REQ_MS_OS_20),
};
TU_VERIFY_STATIC(sizeof desc_bos == BOS_LEN, "BOS descriptor length");

static const uint8_t desc_ms_os_20[] = {
    // set header: length, type, Windows version (8.1), total length
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000),
    U16_TO_U8S_LE(MS_OS_20_LEN),
    // the line interface: function subset header (length, type, first interface, reserved, subset length)
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_LINE, 0,
    U16_TO_U8S_LE(LINE_MS_OS_20_LEN),
    // compatible ID WINUSB, no sub-compatible ID
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'U', 'S', 'B', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    // registry property DeviceInterfaceGUID (REG_SZ, UTF-16): D-Modem's
    U16_TO_U8S_LE(0x0080), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY), U16_TO_U8S_LE(0x0001), U16_TO_U8S_LE(0x0028),
    W16('D'), W16('e'), W16('v'), W16('i'), W16('c'), W16('e'), W16('I'), W16('n'), W16('t'), W16('e'),
    W16('r'), W16('f'), W16('a'), W16('c'), W16('e'), W16('G'), W16('U'), W16('I'), W16('D'), W16(0),
    U16_TO_U8S_LE(0x004E),
    // {0463C853-BB62-4057-9E76-D14B0E3ACAFD}
    W16('{'), W16('0'), W16('4'), W16('6'), W16('3'), W16('C'), W16('8'), W16('5'), W16('3'), W16('-'),
    W16('B'), W16('B'), W16('6'), W16('2'), W16('-'), W16('4'), W16('0'), W16('5'), W16('7'), W16('-'),
    W16('9'), W16('E'), W16('7'), W16('6'), W16('-'), W16('D'), W16('1'), W16('4'), W16('B'), W16('0'),
    W16('E'), W16('3'), W16('A'), W16('C'), W16('A'), W16('F'), W16('D'), W16('}'), W16(0),
    // the reset interface (picotool), the SDK's
    RPI_RESET_MS_OS_20_DESCRIPTOR(ITF_RESET),
};
TU_VERIFY_STATIC(sizeof desc_ms_os_20 == MS_OS_20_LEN, "MS OS 2.0 descriptor set length");

// TinyUSB, on GET_DESCRIPTOR(BOS): the BOS descriptor above.
const uint8_t *tud_descriptor_bos_cb(void) { return desc_bos; }

static uint16_t desc_str[33];

// TinyUSB, on GET_DESCRIPTOR(STRING): string `index` as a USB string
// descriptor (UTF-16, ASCII only, at most 32 characters), valid until the
// next call; the serial number is the flash chip's unique ID. NULL (a STALL)
// for an index we don't have.
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
    case STR_LINE: s = STICK_ITF_NAME; break;
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
// The class driver
// ---------------------------------------------------------------------------
static uint8_t s_itf = 0xFF;
static uint8_t s_alt;
static const tusb_desc_endpoint_t *s_ep_out, *s_ep_in, *s_ep_event;
static int16_t s_out_buf[STICK_MAX_SAMPLES], s_in_buf[STICK_MAX_SAMPLES];
static stick_event_t s_event;
static bool s_event_busy;
static uint8_t s_ctl_buf[64];
static uint32_t s_reboot_at;

// Class driver init, at tusb_init(): nothing to set up.
static void ld_init(void) {}

// Class driver reset, on bus reset or unplug: the stream stops, the
// interface forgotten until opened again.
static void ld_reset(uint8_t rhport) {
    (void)rhport;
    s_itf = 0xFF;
    s_alt = 0;
    s_event_busy = false;
    stick_stream(false);
}

// Class driver open, when the host sets the configuration: claims the line
// interface (both alternate settings) and sets aside the isochronous
// endpoints' buffers; returns the descriptor bytes it takes, 0 to pass.
static uint16_t ld_open(uint8_t rhport, const tusb_desc_interface_t *d, uint16_t max_len) {
    TU_VERIFY(d->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC && d->bInterfaceSubClass == STICK_ITF_SUBCLASS &&
              d->bInterfaceProtocol == STICK_ITF_PROTOCOL && d->bAlternateSetting == 0, 0);
    const uint8_t *p = (const uint8_t *)d, *end = p + max_len;
    uint16_t len = 0;
    while (p < end && p + tu_desc_len(p) <= end) {
        if (tu_desc_type(p) == TUSB_DESC_INTERFACE && ((const tusb_desc_interface_t *)p)->bInterfaceNumber !=
                                                          d->bInterfaceNumber)
            break;
        if (tu_desc_type(p) == TUSB_DESC_ENDPOINT) {
            const tusb_desc_endpoint_t *e = (const tusb_desc_endpoint_t *)p;
            if (e->bEndpointAddress == STICK_EP_OUT) s_ep_out = e;
            else if (e->bEndpointAddress == STICK_EP_IN) s_ep_in = e;
            else if (e->bEndpointAddress == STICK_EP_EVENT) s_ep_event = e;
            if (e->bmAttributes.xfer == TUSB_XFER_ISOCHRONOUS)
                TU_ASSERT(usbd_edpt_iso_alloc(rhport, e->bEndpointAddress, tu_edpt_packet_size(e)), 0);
        }
        len = (uint16_t)(len + tu_desc_len(p));
        p = tu_desc_next(p);
    }
    TU_VERIFY(s_ep_out && s_ep_in && s_ep_event, 0);
    s_itf = d->bInterfaceNumber;
    s_alt = 0;
    return len;
}

// The line's samples waiting, up to a packet's worth, sent in the next
// frame: the packet's size is the stick's clock's count, so the host keeps
// the codec's rate (as the SmartUSB56's driver expects).
static void send_in(uint8_t rhport) {
    uint32_t n = stick_to_host(s_in_buf, STICK_MAX_SAMPLES);
    usbd_edpt_xfer(rhport, STICK_EP_IN, (uint8_t *)s_in_buf, (uint16_t)(2 * n), false);
}

// Alternate setting 1 starts the samples both ways and the events;
// alternate setting 0 stops them.
static bool set_alt(uint8_t rhport, uint8_t alt) {
    if (alt > 1) return false;
    if (alt == s_alt) return true;
    if (alt == 1) {
        TU_ASSERT(usbd_edpt_iso_activate(rhport, s_ep_out));
        TU_ASSERT(usbd_edpt_iso_activate(rhport, s_ep_in));
        TU_ASSERT(usbd_edpt_open(rhport, s_ep_event));
        stick_stream(true);
        s_alt = 1;
        s_event_busy = false;
        stick_event_again();   // tell the host the state at once
        usbd_edpt_xfer(rhport, STICK_EP_OUT, (uint8_t *)s_out_buf, sizeof s_out_buf, false);
        send_in(rhport);
    } else {
        s_alt = 0;
        stick_stream(false);
        usbd_edpt_close(rhport, STICK_EP_EVENT);
        s_event_busy = false;
    }
    return true;
}

// The vendor requests (the stick's protocol) to the interface or the
// device, at each control stage: IN ones answer with stick_vendor_in()'s
// data, OUT ones carry only wValue (and wIndex). false stalls: unknown, or a
// bad argument.
static bool vendor_request(uint8_t rhport, uint8_t stage, const tusb_control_request_t *r) {
    if (r->bmRequestType_bit.direction == TUSB_DIR_IN) {
        if (stage != CONTROL_STAGE_SETUP) return true;
        uint16_t max = r->wLength < sizeof s_ctl_buf ? r->wLength : (uint16_t)sizeof s_ctl_buf;
        int n = stick_vendor_in(r->bRequest, r->wValue, r->wIndex, s_ctl_buf, max);
        if (n < 0) return false;
        return tud_control_xfer(rhport, r, s_ctl_buf, (uint16_t)n);
    }
    if (r->wLength != 0) return false;
    if (stage != CONTROL_STAGE_SETUP) return true;
    if (!stick_vendor_out(r->bRequest, r->wValue, r->wIndex)) return false;
    return tud_control_status(rhport, r);
}

// Control requests to the line interface: SET_INTERFACE and GET_INTERFACE
// (the alternate settings), and the vendor requests.
static bool ld_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *r) {
    if (r->bmRequestType_bit.recipient != TUSB_REQ_RCPT_INTERFACE || (r->wIndex & 0xFF) != s_itf) return false;
    if (r->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR) return vendor_request(rhport, stage, r);
    if (r->bmRequestType_bit.type != TUSB_REQ_TYPE_STANDARD) return false;
    switch (r->bRequest) {
    case TUSB_REQ_SET_INTERFACE:
        if (stage != CONTROL_STAGE_SETUP) return true;
        if (!set_alt(rhport, (uint8_t)r->wValue)) return false;
        return tud_control_status(rhport, r);
    case TUSB_REQ_GET_INTERFACE:
        if (stage != CONTROL_STAGE_SETUP) return true;
        return tud_control_xfer(rhport, r, &s_alt, 1);
    default:
        return false;
    }
}

// TinyUSB hands vendor requests to the device (not an interface) here: the
// same requests, as the SmartUSB56's driver sends them.
// Windows' request for the Microsoft OS 2.0 descriptor set comes here too.
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *r) {
    if (r->bmRequestType_bit.recipient == TUSB_REQ_RCPT_INTERFACE) return ld_control_xfer_cb(rhport, stage, r);
    if (r->bmRequestType_bit.recipient != TUSB_REQ_RCPT_DEVICE) return false;
    if (r->bRequest == STICK_REQ_MS_OS_20 && r->wIndex == MS_OS_20_INDEX &&
        r->bmRequestType_bit.direction == TUSB_DIR_IN) {
        if (stage != CONTROL_STAGE_SETUP) return true;
        return tud_control_xfer(rhport, r, (void *)(uintptr_t)desc_ms_os_20, sizeof desc_ms_os_20);
    }
    return vendor_request(rhport, stage, r);
}

// A transfer done: the modem's samples into the stream (then the next OUT
// packet), the next IN packet with what the line has given, or the event
// endpoint free again. Nothing re-armed once the stream has stopped.
static bool ld_xfer_cb(uint8_t rhport, uint8_t ep, xfer_result_t res, uint32_t n) {
    if (ep == STICK_EP_EVENT) {
        s_event_busy = false;
        return true;
    }
    if (s_alt != 1) return true;
    if (ep == STICK_EP_OUT) {
        if (res == XFER_RESULT_SUCCESS) stick_from_host(s_out_buf, n / 2);
        usbd_edpt_xfer(rhport, STICK_EP_OUT, (uint8_t *)s_out_buf, sizeof s_out_buf, false);
    } else if (ep == STICK_EP_IN) {
        send_in(rhport);
    }
    return true;
}

static const usbd_class_driver_t stick_driver = {
#if CFG_TUSB_DEBUG >= 2
    .name = STICK_DRIVER_NAME,
#endif
    .init = ld_init,
    .reset = ld_reset,
    .open = ld_open,
    .control_xfer_cb = ld_control_xfer_cb,
    .xfer_cb = ld_xfer_cb,
};

static usbd_class_driver_t s_drivers[2];

// TinyUSB, at init: the application's class drivers, tried before the
// built-in ones (the line interface and Raspberry Pi's reset interface).
const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *count) {
    s_drivers[0] = stick_driver;
    s_drivers[1] = usb_reset_interface_driver;
    *count = 2;
    return s_drivers;
}

// TinyUSB: unplugged or deconfigured: the stick's own reaction (the line
// stick goes on hook: a modem that lost its host must not hold the line).
void tud_umount_cb(void) { stick_unmounted(); }

// Milliseconds since boot (wraps; compared by difference).
static uint32_t millis(void) { return to_ms_since_boot(get_absolute_time()); }

void usb_stick_reboot_uf2(void) {
    uint32_t t = millis() + 50;
    s_reboot_at = t ? t : 1;
}

// See usb_stick.h. Full-speed device on the RP2040's only USB port.
void usb_stick_init(void) {
    const tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    tusb_init(0, &init);
}

void usb_stick_service(void) {
    tud_task();
    stick_poll();
    if (s_reboot_at && (int32_t)(millis() - s_reboot_at) >= 0) reset_usb_boot(0, 0);
    if (s_alt == 1 && !s_event_busy && tud_ready() && stick_event(&s_event)) {
        s_event_busy = usbd_edpt_xfer(0, STICK_EP_EVENT, (uint8_t *)&s_event, sizeof s_event, false);
        if (!s_event_busy) stick_event_again();   // try again next time
    }
}
