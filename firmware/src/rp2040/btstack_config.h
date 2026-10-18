/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 Neo-Desktop
 */
// BTstack configuration for pico-serial W: classic (SPP) and LE (GATT) at once.
#pragma once

// Roles and features (pico_btstack_classic / _ble also define the first two)
#ifndef ENABLE_CLASSIC
#define ENABLE_CLASSIC
#endif
#ifndef ENABLE_BLE
#define ENABLE_BLE
#endif
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_SECURE_CONNECTIONS
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS
#define ENABLE_SOFTWARE_AES128
#define ENABLE_LE_DATA_LENGTH_EXTENSION
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP   // the SDK always builds BTstack's stdout HCI dump

// Buffers
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4
#define MAX_NR_AVDTP_CONNECTIONS 0
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 0
#define MAX_NR_BNEP_CHANNELS 0
#define MAX_NR_BNEP_SERVICES 0
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 4
#define MAX_NR_GATT_CLIENTS 0
#define MAX_NR_HCI_CONNECTIONS 3
#define MAX_NR_HID_HOST_CONNECTIONS 0
#define MAX_NR_HIDS_CLIENTS 0
#define MAX_NR_HFP_CONNECTIONS 0
#define MAX_NR_L2CAP_CHANNELS 6
#define MAX_NR_L2CAP_SERVICES 4
#define MAX_NR_RFCOMM_CHANNELS 1
#define MAX_NR_RFCOMM_MULTIPLEXERS 1
#define MAX_NR_RFCOMM_SERVICES 1
#define MAX_NR_SERVICE_RECORD_ITEMS 2
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_WHITELIST_ENTRIES 4
#define MAX_NR_LE_DEVICE_DB_ENTRIES 4
#define MAX_ATT_DB_SIZE 32   // runtime GATT builder: unused (the database is compiled), but built by the SDK

// Bonds kept in flash (TLV), SPEC-W.md 4.5: up to 4 phones
#define NVM_NUM_DEVICE_DB_ENTRIES 4
#define NVM_NUM_LINK_KEYS 4

// The CYW43 controller's own buffers
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN (255 + 4)
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

// Platform
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT
#define HCI_RESET_RESEND_TIMEOUT_MS 1000
