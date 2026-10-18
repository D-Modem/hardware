# pico-serial control protocol (version 1)

For host apps (dmodem-android, a desktop tool) that want more than a plain
serial port gives: the board's role, every modem line, and settings. The
C definitions are in [`src/core/protocol.h`](src/core/protocol.h).

## Finding it

The USB device has a vendor interface:
- class `0xFF`, subclass `0x50`, protocol `0x01`;
- named "pico-serial control";
- with no endpoints.

Every request is a **vendor** control request with the **interface** as
recipient, and `wIndex` set to that interface's number (2 in firmware 0.1).

| bmRequestType | Direction |
|---|---|
| `0xC1` | IN (device → host) |
| `0x41` | OUT (host → device), `wLength = 0` |

An unknown request or an out-of-range value stalls.

## Requests

| bRequest | Name | Dir | wValue | Data |
|---|---|---|---|---|
| 1 | GET_INFO | IN | 0 | `ps_info_t`, 14 bytes |
| 2 | SET_MODE | OUT | 0 auto, 1 DCE, 2 DTE | — (saved in flash) |
| 3 | SET_OUTPUTS | OUT | `mask << 8 \| value` | — (not saved) |
| 4 | GET_INPUTS | IN | 0 | `ps_inputs_t`, 5 bytes |
| 5 | SET_FLOW | OUT | 0 auto, 1 on, 2 off | — (saved) |
| 6 | GET_STATS | IN | 0 | `ps_stats_t`, 32 bytes |
| 7 | CLEAR_STATS | OUT | 0 | — |
| 8 | SET_BAUD_MODE | OUT | 0 auto, 1 host, 2 learn | — (saved) |
| 9 | AUTOBAUD_RESTART | OUT | 0 | — (no effect unless learning is in use) |

All multi-byte fields are little-endian.

## Modem line bits

| Bit | Line |
|---|---|
| `0x01` | DTR |
| `0x02` | RTS |
| `0x04` | DCD |
| `0x08` | DSR |
| `0x10` | RI |
| `0x20` | CTS |

1 means asserted (ON).

**As DCE:** the board drives DCD, DSR, RI and CTS, and reads the
computer's DTR and RTS.

**As DTE:** it drives DTR and RTS, and reads DCD, DSR, RI and CTS.

**SET_OUTPUTS** overrides the default mapping (SPEC.md §5.3–5.4) for the
lines in `mask`. Example: `wValue = 0x1010` raises RI for a ring. RI has no
default source, so it needs SET_OUTPUTS. An override of the flow line (CTS
as DCE, RTS as DTE) is still held off while the board's buffer to the host
is ¾ full.

## Baud rate

Where the UART's speed comes from (SET_BAUD_MODE):

| wValue | Mode | As DCE | As DTE |
|---|---|---|---|
| 0 | auto (default) | learned | the host's |
| 1 | host | the host's | the host's |
| 2 | learn | learned | learned |

"The host's" is the speed in the last CDC SET_LINE_CODING. "Learned" is
measured from the far end's characters: see
[README.md](README.md#automatic-baud-rate). Data bits, parity and stop
bits are always the host's.

While learning, received bytes are held back. Once the speed is known
(GET_INFO flag `0x08`), the characters it was learned from are sent on, and
the UART switches to it. GET_INFO's `baud` is the speed the UART runs at,
learned or not. AUTOBAUD_RESTART starts learning again.

## Structures

`ps_info_t` (14 bytes):

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | protocol (1) |
| 1 | 3 | firmware major, minor, patch |
| 4 | 1 | board: 0 hood, 1 W, 2 Pico bench rig |
| 5 | 1 | mode setting: 0 auto, 1 DCE, 2 DTE |
| 6 | 1 | role now: 0 none, 1 DCE, 2 DTE |
| 7 | 1 | flow setting |
| 8 | 1 | flags: `0x01` last line coding adjusted, `0x02` baud above 250 kbit/s, `0x04` CTS flow control active, `0x08` baud learned, `0x10` learning the baud now |
| 9 | 4 | baud the UART runs at now |
| 13 | 1 | baud mode setting: 0 auto, 1 host, 2 learn |

Fields may be added at the end in later firmware without changing the
protocol version. Ask for at least the length you understand and read
what comes back.

`ps_inputs_t` (5 bytes):

| Offset | Field |
|---|---|
| 0 | role |
| 1 | inputs: lines read from the far end |
| 2 | outputs: lines driven now |
| 3 | detect: `0x01` chip A STATUS (computer present), `0x02` chip B INVALID (modem present), `0x04` chip A's always-on receiver on pin 2 |
| 4 | host lines: DTR/RTS as last set over CDC |

`ps_stats_t` (8 × u32):
- bytes to RS-232;
- bytes from RS-232;
- framing, parity, overrun and break counts;
- bytes dropped (received while the host wasn't reading and the buffer was full);
- role changes.

## Also on the CDC interface

The CDC serial port sends standard SERIAL_STATE notifications on its
interrupt endpoint, so apps whose USB stack reads them need nothing from
this protocol for the common lines:
- **as DCE:** DSR = the computer's DTR; DCD = a computer is attached;
- **as DTE:** the modem's DCD, DSR and RI;
- **both roles:** break, framing, parity and overrun events.
