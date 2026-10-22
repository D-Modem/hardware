/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
#pragma once

#define CFG_TUD_ENABLED 1
#ifndef CFG_TUSB_RHPORT0_MODE
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#endif
#define CFG_TUSB_OS OPT_OS_PICO
#define CFG_TUD_ENDPOINT0_SIZE 64

#if defined(PS_BOARD_LINE)
#define CFG_TUD_CDC 0                 // the line stick: one vendor interface (src/line/usb_line.c)
#else
#define CFG_TUD_CDC 1
#endif
#define CFG_TUD_CDC_NOTIFY 1          // SERIAL_STATE: DCD, DSR, RI, errors (TinyUSB >= 0.19)
#define CFG_TUD_CDC_RX_BUFSIZE 512
#define CFG_TUD_CDC_TX_BUFSIZE 512
#define CFG_TUD_CDC_EP_BUFSIZE 64

#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0              // our vendor interfaces are app drivers (usb.c)
