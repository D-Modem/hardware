<!-- Made by firmware/tools/protocol_ref.py from the protocol headers: change the headers (or the script), not this page. -->

# Protocol reference

What the firmware's protocol headers define, value by value: the requests, constants and wire structs a host app shares with the boards. [PROTOCOL.md](../firmware/PROTOCOL.md) says how to use them. Every struct is packed and little-endian.

## pico-serial control

The DB25 and W boards' control interface: [`protocol.h`](../firmware/src/core/protocol.h).

pico-serial vendor protocol: control requests on the "pico-serial control" interface. Shared by the firmware and any host app; see firmware/PROTOCOL.md.

| name | value | |
|---|---|---|
| `PS_PROTOCOL_VERSION` | `1` |  |

USB: vendor interface, class 0xFF, subclass/protocol below; requests are vendor-type, recipient interface, wIndex = that interface's number.

| name | value | |
|---|---|---|
| `PS_ITF_SUBCLASS` | `80` (`0x50`) | 'P' |
| `PS_ITF_PROTOCOL` | `1` |  |

### `enum ps_request`

| name | value | |
|---|---|---|
| `PS_REQ_GET_INFO` | `1` | IN ps_info_t |
| `PS_REQ_SET_MODE` | `2` | OUT wValue = ps_mode (saved) |
| `PS_REQ_SET_OUTPUTS` | `3` | OUT wValue = mask &lt;&lt; 8 \| value (PS_LINE_*), not saved |
| `PS_REQ_GET_INPUTS` | `4` | IN ps_inputs_t |
| `PS_REQ_SET_FLOW` | `5` | OUT wValue = ps_flow (saved) |
| `PS_REQ_GET_STATS` | `6` | IN ps_stats_t |
| `PS_REQ_CLEAR_STATS` | `7` | OUT |
| `PS_REQ_SET_BAUD_MODE` | `8` | OUT wValue = ps_baud (saved) |
| `PS_REQ_AUTOBAUD_RESTART` | `9` | OUT: learn the baud rate again now |
| | | *W board only (stall elsewhere):* |
| `PS_REQ_BT_PAIRING` | `10` | OUT wValue = seconds to allow pairing (0 = close the window) |
| `PS_REQ_BT_FORGET` | `11` | OUT: forget every bonded phone |

### `enum ps_mode`

| name | value | |
|---|---|---|
| `PS_MODE_AUTO` | `0` |  |
| `PS_MODE_DCE` | `1` |  |
| `PS_MODE_DTE` | `2` |  |

### `enum ps_role`

| name | value | |
|---|---|---|
| `PS_ROLE_NONE` | `0` |  |
| `PS_ROLE_DCE` | `1` |  |
| `PS_ROLE_DTE` | `2` |  |

### `enum ps_flow`

| name | value | |
|---|---|---|
| `PS_FLOW_AUTO` | `0` |  |
| `PS_FLOW_ON` | `1` |  |
| `PS_FLOW_OFF` | `2` |  |

### `enum ps_baud`

Where the UART's baud rate comes from. AUTO: learned from the far end as DCE (a computer typing at its own speed), the host's as DTE (a modem follows the speed it's spoken to at). HOST: always the host's (SET_LINE_CODING). LEARN: always learned. Data bits, parity and stop bits are always the host's.

| name | value | |
|---|---|---|
| `PS_BAUD_AUTO` | `0` |  |
| `PS_BAUD_HOST` | `1` |  |
| `PS_BAUD_LEARN` | `2` |  |

Modem lines, 1 = asserted (ON). Which are outputs and which inputs depends on the role: as DCE the board drives DCD DSR RI CTS and reads DTR RTS; as DTE it drives DTR RTS and reads DCD DSR RI CTS.

| name | value | |
|---|---|---|
| `PS_LINE_DTR` | `1` |  |
| `PS_LINE_RTS` | `2` |  |
| `PS_LINE_DCD` | `4` |  |
| `PS_LINE_DSR` | `8` |  |
| `PS_LINE_RI` | `16` (`0x10`) |  |
| `PS_LINE_CTS` | `32` (`0x20`) |  |

ps_info_t.flags.

| name | value | |
|---|---|---|
| `PS_INFO_CODING_ADJUSTED` | `1` | last SET_LINE_CODING asked for something the UART can't do |
| `PS_INFO_BAUD_OVER_RATING` | `2` | baud above the transceivers' 250 kbit/s rating |
| `PS_INFO_FLOW_ACTIVE` | `4` | hardware CTS flow control currently obeyed |
| `PS_INFO_BAUD_LOCKED` | `8` | the baud rate was learned from the far end |
| `PS_INFO_BAUD_LEARNING` | `16` (`0x10`) | learning it now (received data waits until it's known) |

### `ps_info_t`

14 bytes.

| offset | size | type | field | |
|---|---|---|---|---|
| 0 | 1 | `uint8_t` | `protocol` | PS_PROTOCOL_VERSION |
| 1 | 1 | `uint8_t` | `fw_major` |  |
| 2 | 1 | `uint8_t` | `fw_minor` |  |
| 3 | 1 | `uint8_t` | `fw_patch` |  |
| 4 | 1 | `uint8_t` | `board` | 0 = DB25 hood board, 1 = W (wireless), 2 = Pico bench rig |
| 5 | 1 | `uint8_t` | `mode` | ps_mode setting |
| 6 | 1 | `uint8_t` | `role` | ps_role now |
| 7 | 1 | `uint8_t` | `flow` | ps_flow setting |
| 8 | 1 | `uint8_t` | `flags` | PS_INFO_* |
| 9 | 4 | `uint32_t` | `baud` | UART baud now (learned, or the host's) |
| 13 | 1 | `uint8_t` | `baud_mode` | ps_baud setting |

ps_inputs_t.detect.

| name | value | |
|---|---|---|
| `PS_DETECT_A_STATUS` | `1` | chip A sees valid RS-232 levels on pins 2/4/20 (a computer) |
| `PS_DETECT_B_INVALID` | `2` | chip B sees valid RS-232 levels on pins 3/5/6/8/22 (a modem) |
| `PS_DETECT_A_R1OUTB` | `4` | chip A's always-on receiver on pin 2 (non-inverting) |

### `ps_inputs_t`

5 bytes.

| offset | size | type | field | |
|---|---|---|---|---|
| 0 | 1 | `uint8_t` | `role` |  |
| 1 | 1 | `uint8_t` | `inputs` | PS_LINE_* read from the far end in this role |
| 2 | 1 | `uint8_t` | `outputs` | PS_LINE_* the board is driving now |
| 3 | 1 | `uint8_t` | `detect` | PS_DETECT_* |
| 4 | 1 | `uint8_t` | `host` | PS_LINE_DTR/RTS as the host last set them |

### `ps_stats_t`

32 bytes.

| offset | size | type | field | |
|---|---|---|---|---|
| 0 | 4 | `uint32_t` | `to_rs232` | bytes sent out of the DB25 |
| 4 | 4 | `uint32_t` | `from_rs232` | bytes received on the DB25 |
| 8 | 4 | `uint32_t` | `framing` |  |
| 12 | 4 | `uint32_t` | `parity` |  |
| 16 | 4 | `uint32_t` | `overrun` |  |
| 20 | 4 | `uint32_t` | `breaks` |  |
| 24 | 4 | `uint32_t` | `dropped` | received bytes lost because the host wasn't reading |
| 28 | 4 | `uint32_t` | `role_changes` |  |

## Line stick

pico-serial Line, the phone-line stick: [`line_proto.h`](../firmware/src/core/line_proto.h).

The phone-line stick's USB protocol (SPEC-L.md §5, firmware/PROTOCOL.md "Line stick"): what the host's driver (D-Modem's dmodemd) and the firmware share. Little-endian, packed.

| name | value | |
|---|---|---|
| `LP_VERSION` | `1` |  |

Interface 0: vendor class, this subclass and protocol. Alternate setting 0 has no endpoints (no bandwidth); alternate setting 1 has the three below, and selecting it starts the samples both ways.

| name | value | |
|---|---|---|
| `LP_ITF_SUBCLASS` | `76` (`0x4C`) | 'L' |
| `LP_ITF_PROTOCOL` | `1` |  |
| `LP_EP_OUT` | `1` | isochronous, 1 ms: the modem's samples to the line |
| `LP_EP_IN` | `130` (`0x82`) | isochronous, 1 ms: the line's samples |
| `LP_EP_EVENT` | `131` (`0x83`) | interrupt, 1 ms: lp_event_t when something changes |
| `LP_MAX_SAMPLES` | `20` (`0x14`) | per packet: 16 kHz (16 a ms) with room to catch up |
| `LP_PACKET_BYTES` | `(2 * LP_MAX_SAMPLES)` |  |

### `LP_REQ_*`

Vendor requests (recipient: the interface or the device, wIndex 0)

| name | value | |
|---|---|---|
| `LP_REQ_GET_INFO` | `1` | IN: lp_info_t |
| `LP_REQ_GET_STATUS` | `2` | IN: lp_status_t |
| `LP_REQ_SET_RATE` | `16` (`0x10`) | OUT: wValue = sample rate in Hz (lp_rate_code) |
| `LP_REQ_SET_HOOK` | `17` (`0x11`) | OUT: wValue 1 = off hook, 0 = on hook |
| `LP_REQ_HOOK_FLASH` | `18` (`0x12`) | OUT: wValue = ms on hook (1..2000), then off hook again |
| `LP_REQ_SET_MONITOR` | `19` (`0x13`) | OUT: wValue 1 = hear the line on hook (caller ID), 0 = not |
| `LP_REQ_DAA_READ` | `32` (`0x20`) | IN: wValue = register (0..127); 1 byte |
| `LP_REQ_DAA_WRITE` | `33` (`0x21`) | OUT: wValue = register &lt;&lt; 8 \| value |
| `LP_REQ_RESET` | `48` (`0x30`) | OUT: the DAA reset and brought up again, on hook |
| `LP_REQ_REBOOT_UF2` | `49` (`0x31`) | OUT: into the RP2040's UF2 loader |
| `LP_REQ_MS_OS_20` | `77` (`0x4D`) | IN, wIndex 7: the Microsoft OS 2.0 descriptor set (Windows) |

State bits (lp_info_t.state, lp_status_t.state, lp_event_t.state)

| name | value | |
|---|---|---|
| `LP_ST_LINK` | `1` | the Si3056 and the line side talk (FDT) |
| `LP_ST_OFF_HOOK` | `2` |  |
| `LP_ST_MONITOR` | `4` | on-hook line monitor on |
| `LP_ST_STREAMING` | `8` | alternate setting 1 |
| `LP_ST_RINGING` | `16` (`0x10`) | the ring detector is active now |
| `LP_ST_FLASH` | `32` (`0x20`) | a hook flash is in progress |

### `lp_info_t`

12 bytes.

| offset | size | type | field | |
|---|---|---|---|---|
| 0 | 1 | `uint8_t` | `version` | LP_VERSION |
| 1 | 1 | `uint8_t` | `fw_major` |  |
| 2 | 1 | `uint8_t` | `fw_minor` |  |
| 3 | 1 | `uint8_t` | `fw_patch` |  |
| 4 | 2 | `uint16_t` | `rate` | samples a second |
| 6 | 1 | `uint8_t` | `max_samples` | LP_MAX_SAMPLES |
| 7 | 1 | `uint8_t` | `state` | LP_ST_* |
| 8 | 1 | `uint8_t` | `reg11` | Si3056 register 11: line-side ID (bits 7:4) and system-side revision |
| 9 | 1 | `uint8_t` | `reg13` | register 13: line-side revision (bits 5:2) |
| 10 | 2 | `uint8_t[2]` | `reserved` |  |

### `lp_status_t`

20 bytes.

| offset | size | type | field | |
|---|---|---|---|---|
| 0 | 1 | `uint8_t` | `reg12` | FDT (bit 6), loop current LCS[4:0] |
| 1 | 1 | `uint8_t` | `reg28` | loop current LCS2 |
| 2 | 1 | `uint8_t` | `reg29` | line voltage LVS |
| 3 | 1 | `uint8_t` | `reg5` | DAA control 1: RDTN, RDTP, ONHM, RDT, OHE, OH |
| 4 | 1 | `uint8_t` | `int_src` | register 4 bits seen since the last event or status |
| 5 | 1 | `uint8_t` | `state` | LP_ST_* |
| 6 | 2 | `uint16_t` | `underruns` | OUT samples the line wanted and the host hadn't sent |
| 8 | 2 | `uint16_t` | `overruns` | samples dropped: OUT too far ahead, or IN not taken |
| 10 | 4 | `uint32_t` | `samples_in` | the line's samples handed to the host |
| 14 | 4 | `uint32_t` | `samples_out` | the host's samples played |
| 18 | 1 | `uint8_t` | `ring_edges` | ring detector changes, mod 256 |
| 19 | 1 | `uint8_t` | `reserved` |  |

### `lp_event_t`

8 bytes.

| offset | size | type | field | |
|---|---|---|---|---|
| 0 | 1 | `uint8_t` | `state` | LP_ST_* |
| 1 | 1 | `uint8_t` | `ring_edges` | ring detector changes, mod 256: each one is a pulse for the modem's ring() |
| 2 | 1 | `uint8_t` | `int_src` | register 4 bits since the last event (ring, overload, polarity...) |
| 3 | 1 | `uint8_t` | `seq` | +1 each event |
| 4 | 2 | `uint16_t` | `underruns` |  |
| 6 | 2 | `uint16_t` | `overruns` |  |

## WB stick

pico-serial WB, the wideband stick: [`wb_proto.h`](../firmware/src/core/wb_proto.h).

The wideband stick's USB protocol (SPEC-WB.md §5, firmware/PROTOCOL.md "WB stick"): what the host's driver (D-Modem's dmodemd) and the firmware share. Shaped as the Line stick's (line_proto.h): the same interface layout and endpoints, other requests. Little-endian, packed.

| name | value | |
|---|---|---|
| `WP_VERSION` | `1` |  |
| `WP_BOARD` | `4` | wp_info_t.board: the WB stick (board_config.h PS_BOARD_ID) |

Interface 0: vendor class, this subclass and protocol. Alternate setting 0 has no endpoints (no bandwidth); alternate setting 1 has the three below, and selecting it starts the samples both ways.

| name | value | |
|---|---|---|
| `WP_ITF_SUBCLASS` | `87` (`0x57`) | 'W' |
| `WP_ITF_PROTOCOL` | `1` |  |
| `WP_EP_OUT` | `1` | isochronous, 1 ms: the modem's samples to the line |
| `WP_EP_IN` | `130` (`0x82`) | isochronous, 1 ms: the line's samples |
| `WP_EP_EVENT` | `131` (`0x83`) | interrupt, 4 ms: wp_event_t when something changes |
| `WP_MAX_SAMPLES` | `240` (`0xF0`) | per packet: 192 kHz (192 a ms) with room to catch up |
| `WP_PACKET_BYTES` | `(2 * WP_MAX_SAMPLES)` |  |

### `WP_REQ_*`

Vendor requests (recipient: the interface or the device; wIndex's low byte the interface, 0)

| name | value | |
|---|---|---|
| `WP_REQ_GET_INFO` | `1` | IN: wp_info_t |
| `WP_REQ_GET_STATUS` | `2` | IN: wp_status_t (the peaks and the codec's flags since the last one) |
| `WP_REQ_SET_RATE` | `16` (`0x10`) | OUT: wValue = the sample rate in kHz: 48, 96 or 192 |
| `WP_REQ_SET_CARRIER` | `17` (`0x11`) | OUT: wValue = WP_CARRIER_* (the LINK LED) |
| `WP_REQ_SET_TX_GAIN` | `18` (`0x12`) | OUT: wValue = int16, 0.5 dB steps, -127..48: the DAC's volume |
| `WP_REQ_SET_RX_GAIN` | `19` (`0x13`) | OUT: wValue = 0..95, 0.5 dB steps: the hybrid's MicPGA |
| `WP_REQ_SET_INPUT` | `20` (`0x14`) | OUT: wValue = WP_INPUT_*: which ADC channel the host gets |
| `WP_REQ_SET_LOOPBACK` | `21` (`0x15`) | OUT: wValue = WP_LOOP_* |
| `WP_REQ_CODEC_READ` | `32` (`0x20`) | IN: wValue = register (1..127), wIndex = page &lt;&lt; 8; 1 byte |
| `WP_REQ_CODEC_WRITE` | `33` (`0x21`) | OUT: wValue = register &lt;&lt; 8 \| value, wIndex = page &lt;&lt; 8 |
| `WP_REQ_RESET` | `48` (`0x30`) | OUT: the codec reset and brought up again (rate and settings kept) |
| `WP_REQ_REBOOT_UF2` | `49` (`0x31`) | OUT: into the RP2040's UF2 loader |
| `WP_REQ_MS_OS_20` | `77` (`0x4D`) | IN, wIndex 7: the Microsoft OS 2.0 descriptor set (Windows) |

### `WP_CARRIER_*`

AUTO (the default): lit while streaming.

| name | value | |
|---|---|---|
| `WP_CARRIER_OFF` | `0` |  |
| `WP_CARRIER_ON` | `1` |  |
| `WP_CARRIER_AUTO` | `2` |  |

### `WP_INPUT_*`

Left ADC (default), right ADC.

| name | value | |
|---|---|---|
| `WP_INPUT_HYBRID` | `0` |  |
| `WP_INPUT_LINE` | `1` |  |

### `WP_LOOP_*`

| name | value | |
|---|---|---|
| `WP_LOOP_OFF` | `0` |  |
| `WP_LOOP_FIRMWARE` | `1` |  |
| `WP_LOOP_CODEC` | `2` |  |

State bits (wp_info_t.state, wp_status_t.state, wp_event_t.state)

| name | value | |
|---|---|---|
| `WP_ST_CODEC` | `1` | the codec came up and its clocks run |
| `WP_ST_CARRIER` | `2` | the LINK LED is lit |
| `WP_ST_LOOPBACK` | `4` | a loopback is on |
| `WP_ST_STREAMING` | `8` | alternate setting 1 |

### `wp_info_t`

16 bytes.

| offset | size | type | field | |
|---|---|---|---|---|
| 0 | 1 | `uint8_t` | `version` | WP_VERSION |
| 1 | 1 | `uint8_t` | `fw_major` |  |
| 2 | 1 | `uint8_t` | `fw_minor` |  |
| 3 | 1 | `uint8_t` | `fw_patch` |  |
| 4 | 4 | `uint32_t` | `rate` | samples a second |
| 8 | 2 | `uint16_t` | `max_samples` | WP_MAX_SAMPLES |
| 10 | 1 | `uint8_t` | `state` | WP_ST_* |
| 11 | 1 | `uint8_t` | `board` | WP_BOARD |
| 12 | 1 | `uint8_t` | `input` | WP_INPUT_* |
| 13 | 1 | `uint8_t` | `loopback` | WP_LOOP_* |
| 14 | 1 | `int8_t` | `tx_gain` | the DAC's volume, 0.5 dB steps |
| 15 | 1 | `uint8_t` | `rx_gain` | the hybrid MicPGA's gain, 0.5 dB steps |

### `wp_status_t`

28 bytes.

| offset | size | type | field | |
|---|---|---|---|---|
| 0 | 2 | `uint16_t` | `peak_hybrid` | the largest \|sample\| since the last status: left ADC (32768 = full scale) |
| 2 | 2 | `uint16_t` | `peak_line` | ... right ADC |
| 4 | 2 | `uint16_t` | `peak_tx` | ... the host's samples played |
| 6 | 1 | `uint8_t` | `ovf` | codec page 0 register 42 bits since the last status: DAC/ADC overflows |
| 7 | 1 | `uint8_t` | `oc` | register 44's over-current bits (HPL 0x80, HPR 0x40) since the last status |
| 8 | 1 | `uint8_t` | `adc_flags` | register 36 now: PGAs at gain, ADCs powered |
| 9 | 1 | `uint8_t` | `dac_flags` | register 37 now: DACs and drivers powered |
| 10 | 1 | `uint8_t` | `state` | WP_ST_* |
| 11 | 1 | `uint8_t` | `reserved` |  |
| 12 | 2 | `uint16_t` | `underruns` | OUT samples the line wanted and the host hadn't sent |
| 14 | 2 | `uint16_t` | `overruns` | samples dropped: OUT too far ahead, or IN not taken |
| 16 | 4 | `uint32_t` | `samples_in` | the line's samples handed to the host |
| 20 | 4 | `uint32_t` | `samples_out` | the host's samples played |
| 24 | 2 | `uint16_t` | `resyncs` | the I2S slave lost its place and started again |
| 26 | 2 | `uint16_t` | `reserved2` |  |

### `wp_event_t`

8 bytes.

| offset | size | type | field | |
|---|---|---|---|---|
| 0 | 1 | `uint8_t` | `state` | WP_ST_* |
| 1 | 1 | `uint8_t` | `seq` | +1 each event |
| 2 | 1 | `uint8_t` | `ovf` | register 42 bits since the last event |
| 3 | 1 | `uint8_t` | `oc` | register 44's over-current bits since the last event |
| 4 | 2 | `uint16_t` | `underruns` |  |
| 6 | 2 | `uint16_t` | `overruns` |  |
