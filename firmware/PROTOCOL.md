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
| 10 | BT_PAIRING | OUT | seconds (0 closes) | — W board only: open the Bluetooth pairing window |
| 11 | BT_FORGET | OUT | 0 | — W board only: forget every bonded phone, then open the window |

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

## Bluetooth (W board)

The W board offers the same controls over BLE, in the **pico-serial
control** service, `50530001-5C6F-4E2B-9A6B-1D2C0E5F7A10`. Its
characteristics share that UUID with the second field changed (`5053000N`).
Every one needs an encrypted (bonded) link. Writes other than line coding
are refused (`Write Not Permitted`) unless this BLE link owns the RS-232
port.

| UUID | Name | Access | Content |
|---|---|---|---|
| `50530002` | Line coding | read, write | baud (u32), data bits, parity (0 none, 1 odd, 2 even), stop bits (1, 2): 7 bytes. Kept for this link and used whenever it owns the port |
| `50530003` | Outputs | read, write | write: mask, value (as SET_OUTPUTS); read: override mask, lines driven now |
| `50530004` | Inputs | read, notify | `ps_inputs_t`; notified when it changes |
| `50530005` | Mode | read, write | write: the setting (as SET_MODE); read: setting, role now |
| `50530006` | Break | write | u16 ms; 0 ends it, `0xFFFF` until 0 |
| `50530007` | Flow | read, write | as SET_FLOW |
| `50530008` | Credits | read, notify | u16: free bytes in the buffer to the DB25. Notified when it moves by 256 or more, and when it empties |
| `50530009` | Info | read | `ps_info_t` |

Data goes through the Nordic UART Service (`6E400001-B5A3-F393-E0A9-E50E24DCCA9E`):
write to RX `6E400002…`, subscribe to TX `6E400003…`. A BLE write can't be
held back, so an app sending more than the buffer holds (4 KB) should
watch Credits.

Classic Bluetooth (SPP) carries data only; there are no controls on it.

## Line stick (pico-serial Line)

The phone-line stick ([SPEC-L.md](../SPEC-L.md), `-DPS_BOARD=line`) is a
different device: a softmodem's line interface, for a host driver such as
D-Modem's `dmodemd`. It has none of the above. Its definitions are in
[`src/core/line_proto.h`](src/core/line_proto.h); this section is enough to
write the host's driver. It is shaped like Smart Link's SmartUSB56 (the
ST7554, which `dmodemd/usb_line.cpp` drives), so that driver is the model.

### Finding it

- USB `1209:0002` (a pid.codes *test* PID until a real one is assigned),
  product "pico-serial Line". No kernel driver claims it: open it with
  libusb.
- Interface 0: class `0xFF`, subclass `0x4C`, protocol `0x01`, "pico-serial
  Line". (Interface 1 is Raspberry Pi's reset interface, for `picotool`.)
- **Alternate setting 0** has no endpoints. **Alternate setting 1** has:

| Endpoint | Type | Max packet | |
|---|---|---|---|
| `0x01` OUT | isochronous, asynchronous, every 1 ms | 40 bytes | the modem's samples to the line |
| `0x82` IN | isochronous, asynchronous, every 1 ms | 40 bytes | the line's samples |
| `0x83` IN | interrupt, every 4 ms | 8 bytes | `lp_event_t` when something changed |

### Samples

- S16, little-endian, mono, at the rate set (9600 Hz after reset: the rate
  D-Modem's V.90 needs; any of the Si3056's: 7200, 8000, 8229, 8400, 9000,
  9600, 10286, 12000, 13714, 16000).
- `SET_INTERFACE` to alternate setting 1 starts both directions; back to 0
  stops them. Both FIFOs are emptied on each change. Samples run on hook
  too, so ring and caller-ID detection can work on them (with the line
  monitor on, `SET_MONITOR`).
- **IN:** each packet carries the samples the codec gave since the last
  one (9 or 10 a millisecond at 9600 Hz; up to 20 if a packet was missed).
  Its length *is* the stick's clock: count samples, not milliseconds.
- **OUT:** send, per millisecond, as many samples as the last IN packet
  brought (the SmartUSB56 driver does exactly this: each OUT transfer goes
  with the lengths of an IN transfer just completed). The stick plays them
  at the codec's clock after a 32-sample head start (about 3 ms); if it
  runs dry it plays silence, counts an underrun and waits for its head
  start again. More than its 1024-sample FIFO holds is dropped and counted
  as an overrun (as are line samples the host isn't taking).
- The line's delay through the stick: the head start, plus the Si3056's
  filters (12 samples each way).

### Requests

Vendor control requests; recipient the **interface** (`wIndex` = 0) or the
**device** (as the SmartUSB56's driver sends them): `bmRequestType` `0xC1`
or `0xC0` IN, `0x41` or `0x40` OUT (`wLength` 0). An unknown request or a
bad value stalls.

| bRequest | Name | Dir | wValue | |
|---|---|---|---|---|
| `0x01` | GET_INFO | IN | 0 | `lp_info_t`, 12 bytes |
| `0x02` | GET_STATUS | IN | 0 | `lp_status_t`, 20 bytes (reads four DAA registers: ~1 ms) |
| `0x10` | SET_RATE | OUT | Hz | one of the rates above; set it before starting the samples |
| `0x11` | SET_HOOK | OUT | 1 off hook, 0 on hook | the OFHK pin: takes effect at once |
| `0x12` | HOOK_FLASH | OUT | ms, 1–2000 | on hook for that long, then off hook again (no-op on hook) |
| `0x13` | SET_MONITOR | OUT | 1 on, 0 off | the on-hook line monitor (ONHM), for caller ID |
| `0x20` | DAA_READ | IN | register 0–127 | 1 byte: a Si3056/Si3018 register |
| `0x21` | DAA_WRITE | OUT | `register << 8 \| value` | a register; register 5 keeps OHE set (the hook stays the pin's) and its ONHM bit sets the monitor |
| `0x30` | RESET | OUT | 0 | the DAA reset and brought up again, on hook (~150 ms) |
| `0x31` | REBOOT_UF2 | OUT | 0 | into the RP2040's UF2 loader, 50 ms after the status stage |

**Pulse dialling** is the host's: it sends `SET_HOOK` 0 and 1 for each
break and make, as `dmodemd` does with the SmartUSB56's hook GPIO. Each
takes effect when the request arrives (well within a millisecond of the
control transfer); there is no device-side pulse timer.

**Country settings** are the host's too: after bring-up the line side has
the Si3056's reset values (FCC: 600 Ω, the US's DC termination). Write the
country's registers 16, 26, 30, 31 and the gains (Si3056 datasheet Table 15,
"Country Specific Register Settings") with `DAA_WRITE`.

**Unplugging** (or the host deconfiguring the stick) puts the line on hook.
The watchdog does too if the firmware stalls.

### Structures

All little-endian, packed.

`lp_info_t` (12 bytes): `version` (1), `fw_major`, `fw_minor`, `fw_patch`,
`rate` (u16, Hz), `max_samples` (20, per packet), `state` (below),
`reg11` (line-side ID in bits 7:4: 1 = Si3018, 3 = Si3019; system-side
revision in 3:0), `reg13` (line-side revision in bits 5:2), 2 reserved.

`lp_status_t` (20 bytes): `reg12` (FDT bit 6: the link across the barrier
works; LCS[4:0], the loop current), `reg28` (LCS2), `reg29` (LVS, the line
voltage), `reg5` (RDTN, RDTP, ONHM, RDT, OHE, OH), `int_src` (register 4's
bits since the last status), `state`, `underruns` (u16), `overruns` (u16),
`samples_in` (u32: to the host), `samples_out` (u32: played),
`ring_edges`, 1 reserved.

`lp_event_t` (8 bytes, on `0x83` whenever it changes, and once when the
samples start): `state`, `ring_edges` (the ring detector's changes, mod
256: each one is a pulse for the modem's ring cadence check, as the
SmartUSB56's ring GPIO changes are), `int_src` (register 4 since the last
event: `0x80` ring, `0x40` receive overload, `0x20` the link lost, `0x08`
the line dropped out, `0x01` polarity reversal), `seq` (+1 each event),
`underruns`, `overruns` (u16 each).

`state` bits: `0x01` LINK (FDT), `0x02` OFF_HOOK, `0x04` MONITOR, `0x08`
STREAMING, `0x10` RINGING (the ring detector active now), `0x20` FLASH.

### Inside the stick

The Si3056 is the serial port's master (serial mode 0) on a 48 MHz MCLK
from the RP2040's USB PLL (PLL1 N = 125, M = 256 → 98.304 MHz); the RP2040
is its slave in PIO (`src/line/si3056.pio`), every 16-bit word through
`src/core/daa_frame.c` from the PIO's interrupt. Register accesses ride in
secondary frames, asked for by the FC pin (16-bit mode, SB set at
bring-up). The hook is the OFHK pin (OHE set), the ring detector the RGDT
pin, the DAA's interrupt the AOUT/INT pin.
