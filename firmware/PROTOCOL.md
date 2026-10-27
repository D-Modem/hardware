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
- **Windows** binds WinUSB to both interfaces by itself (no INF, no
  Zadig): the stick is USB 2.1 with a BOS descriptor whose Microsoft OS 2.0
  platform capability names vendor request `0x4D`. Windows reads the
  descriptor set with it (`bmRequestType` `0xC0`, `wIndex` 7). The set gives
  each interface the compatible ID `WINUSB` and a `DeviceInterfaceGUID`:
  `{0463C853-BB62-4057-9E76-D14B0E3ACAFD}` for the line (D-Modem's, as its
  `dmodem-usb.inf` gives it), the Pico SDK's
  `{bc7398c1-73cd-4cb7-98b8-913a8fca7bf6}` for the reset interface.
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
| `0x4D` | MS_OS_20 | IN | 0 | (device, `wIndex` 7) the Microsoft OS 2.0 descriptor set, for Windows |

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


## WB stick (pico-serial WB)

The wideband bare-pair stick ([SPEC-WB.md](../SPEC-WB.md), `-DPS_BOARD=wb`)
is the Line stick's sibling for bare copper pairs: a TLV320AIC3204 codec
behind a line transformer, streaming the pair's samples at 96 kHz for a
wideband modem on the host (D-Modem's `dmodemd`). Its USB device is the Line
stick's (the same interface layout, endpoints, sample framing and
Microsoft OS 2.0 descriptors), with its own requests: no hook, ring or
caller ID. The definitions are in [`src/core/wb_proto.h`](src/core/wb_proto.h).

### Finding it

- USB `1209:0003` (a pid.codes *test* PID, like the Line stick's
  `1209:0002`, until a real one is assigned), product "pico-serial WB".
  `GET_INFO`'s `board` is 4. No kernel driver claims it: open it with libusb.
- **Windows** binds WinUSB to both interfaces by itself, as for the Line
  stick: the same BOS descriptor, vendor request `0x4D` (`wIndex` 7), and
  D-Modem's `DeviceInterfaceGUID` `{0463C853-BB62-4057-9E76-D14B0E3ACAFD}`
  for the line interface.
- Interface 0: class `0xFF`, subclass `0x57` ('W'), protocol `0x01`,
  "pico-serial WB". (Interface 1 is Raspberry Pi's reset interface.)
- **Alternate setting 0** has no endpoints. **Alternate setting 1** has:

| Endpoint | Type | Max packet | |
|---|---|---|---|
| `0x01` OUT | isochronous, asynchronous, every 1 ms | 480 bytes | the modem's samples to the line |
| `0x82` IN | isochronous, asynchronous, every 1 ms | 480 bytes | the line's samples |
| `0x83` IN | interrupt, every 4 ms | 8 bytes | `wp_event_t` when something changed |

### Samples

- S16, little-endian, **mono**, at the rate set: 96 000 Hz after reset;
  48 000 and 192 000 Hz for tests. 192 bytes a millisecond each way at 96
  kHz, 384 at 192 kHz; a packet holds up to 240 samples (192 kHz with 25%
  to catch up).
- **IN** carries one of the codec's two ADC channels (`SET_INPUT`): the
  **hybrid** (the left ADC, default: the line minus the replica of our own
  transmit, so the far end's signal with ours 10–20 dB down) or the **line**
  (the right ADC: the pair's voltage itself, ours included, inverted). The
  other channel's level is in `GET_STATUS`. One channel keeps the packets
  at SPEC-WB's sizes; the second is for measuring the hybrid, which the
  peaks do without streaming it.
- **OUT**: one channel; the stick drives the line differentially from it
  (the left DAC's two outputs: HPL = s, HPR = −s).
- `SET_INTERFACE` to alternate setting 1 starts both directions, back to 0
  stops them; both FIFOs are emptied on each change. As on the Line stick,
  each IN packet carries the samples the codec gave since the last one, so
  its length *is* the stick's clock (the codec's PLL on the stick's 12 MHz
  crystal): count samples, not milliseconds, and send per millisecond as
  many OUT samples as the last IN packet brought. The stick plays them after
  a 3 ms head start; dry, it plays silence, counts an underrun and waits
  for its head start again. More than its 4096-sample FIFO holds is dropped
  and counted as an overrun (as are line samples the host isn't taking).
- The line's delay through the stick: the head start, a third of a
  millisecond of DMA blocks each way, and the codec's filters.

### Requests

Vendor control requests; recipient the **interface** (`wIndex` low byte 0)
or the **device**: `bmRequestType` `0xC1` or `0xC0` IN, `0x41` or `0x40` OUT
(`wLength` 0). An unknown request or a bad value stalls.

| bRequest | Name | Dir | wValue | |
|---|---|---|---|---|
| `0x01` | GET_INFO | IN | 0 | `wp_info_t`, 16 bytes |
| `0x02` | GET_STATUS | IN | 0 | `wp_status_t`, 28 bytes: the line's level (peaks) and the codec's flags since the last status |
| `0x10` | SET_RATE | OUT | kHz: 48, 96 or 192 | the codec reset and brought up at that rate (about 100 ms), the settings below kept |
| `0x11` | SET_CARRIER | OUT | 0 off, 1 on, 2 auto | the LINK LED: the modem's carrier; auto (the default) lights it while the samples stream |
| `0x12` | SET_TX_GAIN | OUT | int16, 0.5 dB steps, −127..48 | the DAC's digital volume (codec page 0 register 65), soft-stepped; 0 after reset |
| `0x13` | SET_RX_GAIN | OUT | 0..95, 0.5 dB steps | the hybrid MicPGA's gain (page 1 register 59); 0 after reset |
| `0x14` | SET_INPUT | OUT | 0 hybrid, 1 line | which ADC channel IN carries |
| `0x15` | SET_LOOPBACK | OUT | 0 off, 1 firmware, 2 codec | see below |
| `0x20` | CODEC_READ | IN | register 0–127; `wIndex` = page << 8 | 1 byte: a codec register |
| `0x21` | CODEC_WRITE | OUT | `register << 8 \| value`; `wIndex` = page << 8 | a codec register, 1–127 (the page select is the firmware's) |
| `0x30` | RESET | OUT | 0 | the codec reset and brought up again, rate and settings kept |
| `0x31` | REBOOT_UF2 | OUT | 0 | into the RP2040's UF2 loader, 50 ms after the status stage |
| `0x4D` | MS_OS_20 | IN | 0 | (device, `wIndex` 7) the Microsoft OS 2.0 descriptor set, for Windows |

**Loopbacks:** *firmware* (1) sends the host's OUT samples straight back
on IN, at the codec's clock, instead of the line's: the USB path and the
host's driver without the line. *Codec* (2) feeds the codec's ADCs to its
DAC (page 0 register 29, D4): the stick repeats what it hears onto the
pair, a remote loopback for the stick at the other end. The hybrid
residue goes round too: with a poorly matched replica and a high
`SET_RX_GAIN` it can ring, so keep the gain low for it.

**Levels:** at 0 dBFS (and `SET_TX_GAIN` 0) each driver swings 0.5 Vrms,
1.0 Vrms between them; through 2 × 60.4 Ω into a 120 Ω pair (a 1:1
transformer) that is 0.5 Vrms on the line, +3.2 dBm for a full-scale sine,
and about +4.8 dBm where the drivers clip (AVDD 1.77 V). A multicarrier
signal backed off ~11 dB for its peaks averages about −8 dBm. The hybrid
input reaches 0 dBFS at 0.5 Vrms (line minus replica, `SET_RX_GAIN` 0:
the PGA's +6 dB for a differential pair on 10 kΩ), the line input at
1.0 Vrms (20 kΩ: our own full-scale transmit reads −6 dBFS there).

### Structures

All little-endian, packed.

`wp_info_t` (16 bytes): `version` (1), `fw_major`, `fw_minor`, `fw_patch`,
`rate` (u32, Hz), `max_samples` (u16, 240), `state`, `board` (4: the WB
stick), `input`, `loopback`, `tx_gain` (i8, 0.5 dB), `rx_gain` (u8, 0.5 dB).

`wp_status_t` (28 bytes): `peak_hybrid`, `peak_line`, `peak_tx` (u16 each:
the largest |sample| since the last status, 32768 = full scale, of the left
ADC, the right ADC and the host's samples played), `ovf` (page 0 register
42 since the last status: `0x80`/`0x40` left/right DAC overflow,
`0x08`/`0x04` left/right ADC overflow), `oc` (register 44's over current:
`0x80` HPL, `0x40` HPR), `adc_flags` (register 36 now: `0x40`/`0x04`
left/right ADC up, `0x80`/`0x08` their PGAs at gain), `dac_flags`
(register 37 now: `0x80` left DAC up, `0x20` HPL, `0x02` HPR), `state`,
1 reserved, `underruns`, `overruns` (u16), `samples_in`, `samples_out`
(u32), `resyncs` (u16: the I2S slave lost its place and started again),
2 reserved.

`wp_event_t` (8 bytes, on `0x83` whenever it changes, and once when the
samples start): `state`, `seq` (+1 each event), `ovf`, `oc` (as in the
status, since the last event: the codec's INT1 on MFP5 brings them),
`underruns`, `overruns` (u16 each).

`state` bits: `0x01` CODEC (it came up and its clocks run), `0x02` CARRIER
(the LINK LED is lit), `0x04` LOOPBACK, `0x08` STREAMING.

### Inside the stick

The codec is the I2S master on the RP2040's 12 MHz crystal (GPOUT0 on
GPIO21, MCLK): its PLL makes 12 × 8.1920 = 98.304 MHz (P = 1, R = 1, J = 8,
D = 1920), NDAC = NADC = MDAC = MADC = 4, and per rate:

| Rate | DOSR = AOSR | Filter | DAC block | ADC block | BCLK = DAC_CLK / N |
|---|---|---|---|---|---|
| 48 kHz | 128 | A | PRB_P1 | PRB_R1 | N = 8: 3.072 MHz |
| 96 kHz | 64 | B | PRB_P7 | PRB_R7 | N = 4: 6.144 MHz |
| 192 kHz | 32 | C | PRB_P17 | PRB_R13 | N = 2: 12.288 MHz |

Every rate keeps DOSR × fs = 6.144 MHz and the modulators at 6.144 MHz
(SLAA557's limits: 2.8–6.2 MHz, 6.758 MHz), MDAC × OSR / 32 at or above
each block's resource class, and 64 BCLKs a frame (I2S, 32-bit words). The
RP2040 is the slave in PIO (`src/wb/i2s.pio`: the codec's bit in as BCLK
rises, ours out just after, a whole BCLK of setup), its words moved by
DMA in blocks of 32 frames and handed to `src/core/wb_audio.c` from the
DMA's interrupt. If WCLK isn't where 32 bits each side put it, the PIO
stops and the firmware starts it over (`resyncs`). The bring-up and the
analog setup (A-LDO 1.77 V, common mode 0.9 V, drivers from AVDD, the
hybrid in the MicPGAs) are in `src/core/wb_codec.c`; registers over I2C1
(address `0x18`).
