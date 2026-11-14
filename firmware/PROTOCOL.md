# pico-serial control protocol (version 1)

This protocol is for host apps (the D-Modem app, a desktop tool) that need
more than a plain serial port gives: the role of the board, all modem
lines, and settings. The C definitions are in [`src/core/protocol.h`](src/core/protocol.h). All their values are in the [protocol reference](../docs/protocol-reference.md).

## Finding it

The USB device has a vendor interface:
- Class `0xFF`, subclass `0x50`, protocol `0x01`.
- Name "pico-serial control".
- No endpoints.

Each request is a **vendor** control request with the **interface** as
recipient. `wIndex` is the number of that interface (2 in firmware 0.1).

| bmRequestType | Direction |
|---|---|
| `0xC1` | IN (device → host) |
| `0x41` | OUT (host → device), `wLength = 0` |

An unknown request or a value that is out of range causes a stall.

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
| 9 | AUTOBAUD_RESTART | OUT | 0 | — (no effect if the learning function is not in use) |
| 10 | BT_PAIRING | OUT | seconds (0 closes) | — W board only: opens the Bluetooth pairing window |
| 11 | BT_FORGET | OUT | 0 | — W board only: clears all bonded phones, then opens the window |

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

**As DCE:** the board drives DCD, DSR, RI and CTS. It reads the DTR and
RTS of the computer.

**As DTE:** the board drives DTR and RTS. It reads DCD, DSR, RI and CTS.

**SET_OUTPUTS** overrides the default mapping (SPEC.md §5.3–5.4) for the
lines in `mask`. Example: `wValue = 0x1010` raises RI for a ring signal. RI
has no default source, thus it needs SET_OUTPUTS. If you override the flow
line (CTS as DCE, RTS as DTE), the board still holds it off while its
buffer to the host is ¾ full.

## Baud rate

SET_BAUD_MODE sets the source of the speed of the UART:

| wValue | Mode | As DCE | As DTE |
|---|---|---|---|
| 0 | auto (default) | learned | the host's |
| 1 | host | the host's | the host's |
| 2 | learn | learned | learned |

"The host's" is the speed in the last CDC SET_LINE_CODING. "Learned" is
the speed that the board measures from the characters of the far end: see
[README.md](README.md#automatic-baud-rate). Data bits, parity and stop
bits always come from the host.

During the learning function, the board holds the received bytes. When the
speed is known (GET_INFO flag `0x08`), the board sends on the characters
that it learned from, and the UART changes to the new speed. The `baud` of
GET_INFO is the speed of the UART, learned or not. AUTOBAUD_RESTART starts
the learning function again.

## Structures

`ps_info_t` (14 bytes):

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | protocol (1) |
| 1 | 3 | firmware major, minor, patch |
| 4 | 1 | board: 0 hood, 1 W, 2 Pico bench rig |
| 5 | 1 | mode setting: 0 auto, 1 DCE, 2 DTE |
| 6 | 1 | current role: 0 none, 1 DCE, 2 DTE |
| 7 | 1 | flow setting |
| 8 | 1 | flags: `0x01` last line coding adjusted, `0x02` baud above 250 kbit/s, `0x04` CTS flow control active, `0x08` baud learned, `0x10` learning function active now |
| 9 | 4 | current baud of the UART |
| 13 | 1 | baud mode setting: 0 auto, 1 host, 2 learn |

Later firmware can add fields at the end without a change to the protocol
version. Request a minimum of the length that you know, and read the data
that the board sends.

`ps_inputs_t` (5 bytes):

| Offset | Field |
|---|---|
| 0 | role |
| 1 | inputs: lines read from the far end |
| 2 | outputs: lines driven now |
| 3 | detect: `0x01` chip A STATUS (computer present), `0x02` chip B INVALID (modem present), `0x04` the always-on receiver of chip A on pin 2 |
| 4 | host lines: DTR/RTS as last set through CDC |

`ps_stats_t` (8 × u32):
- Bytes to RS-232.
- Bytes from RS-232.
- Framing, parity, overrun and break counts.
- Bytes discarded (received while the host did not read and the buffer was
  full).
- Role changes.

## Also on the CDC interface

The CDC serial port sends standard SERIAL_STATE notifications on its
interrupt endpoint. Thus, if the USB stack of an app reads them, the app
does not need this protocol for the usual lines:
- **As DCE:** DSR = the DTR of the computer; DCD = a computer is attached.
- **As DTE:** the DCD, DSR and RI of the modem.
- **The two roles:** break, framing, parity and overrun events.

## Bluetooth (W board)

The W board gives the same controls through BLE, in the **pico-serial
control** service, `50530001-5C6F-4E2B-9A6B-1D2C0E5F7A10`. Its
characteristics use the same UUID with a different second field
(`5053000N`). All characteristics need an encrypted (bonded) link. If this
BLE link does not own the RS-232 port, the board refuses all writes except
line coding (`Write Not Permitted`).

| UUID | Name | Access | Content |
|---|---|---|---|
| `50530002` | Line coding | read, write | baud (u32), data bits, parity (0 none, 1 odd, 2 even), stop bits (1, 2): 7 bytes. Kept for this link and used each time that the link owns the port |
| `50530003` | Outputs | read, write | write: mask, value (as SET_OUTPUTS); read: override mask, lines driven now |
| `50530004` | Inputs | read, notify | `ps_inputs_t`; notified when it changes |
| `50530005` | Mode | read, write | write: the setting (as SET_MODE); read: setting, current role |
| `50530006` | Break | write | u16 ms; 0 ends it, `0xFFFF` until 0 |
| `50530007` | Flow | read, write | as SET_FLOW |
| `50530008` | Credits | read, notify | u16: free bytes in the buffer to the DB25. Notified when it changes by 256 or more, and when the buffer becomes empty |
| `50530009` | Info | read | `ps_info_t` |

Data goes through the Nordic UART Service (`6E400001-B5A3-F393-E0A9-E50E24DCCA9E`):
write to RX `6E400002…`, subscribe to TX `6E400003…`. The board cannot
stop a BLE write. Thus, if an app sends more data than the buffer holds
(4 KB), the app must monitor Credits.

Classic Bluetooth (SPP) sends only data. It has no controls.

## Line stick (pico-serial Line)

The phone-line stick ([SPEC-L.md](../SPEC-L.md), `-DPS_BOARD=line`) is a
different device: the line interface of a softmodem, for a host driver
such as `dmodemd` of D-Modem. It has none of the functions above. Its
definitions are in [`src/core/line_proto.h`](src/core/line_proto.h). This
section is sufficient to write the driver of the host. Its shape is the
same as the SmartUSB56 of Smart Link (the ST7554, which
`frontend/dmodemd/usb_line.cpp` drives). Thus that driver is the model.

### Finding it

- USB `1209:0002` (a pid.codes *test* PID until a real PID is assigned),
  product "pico-serial Line". No kernel driver claims it: open it with
  libusb.
- **Windows** binds WinUSB to the two interfaces automatically (no INF, no
  Zadig). The stick is USB 2.1 with a BOS descriptor. The Microsoft OS 2.0
  platform capability of this descriptor names vendor request `0x4D`.
  Windows reads the descriptor set with this request (`bmRequestType`
  `0xC0`, `wIndex` 7). The set gives each interface the compatible ID
  `WINUSB` and a `DeviceInterfaceGUID`:
  - `{0463C853-BB62-4057-9E76-D14B0E3ACAFD}` for the line (the GUID of
    D-Modem, as its `dmodem-usb.inf` gives it).
  - `{bc7398c1-73cd-4cb7-98b8-913a8fca7bf6}` of the Pico SDK for the reset
    interface.
- Interface 0: class `0xFF`, subclass `0x4C`, protocol `0x01`, "pico-serial
  Line". (Interface 1 is the reset interface of Raspberry Pi, for
  `picotool`.)
- **Alternate setting 0** has no endpoints. **Alternate setting 1** has:

| Endpoint | Type | Max packet | |
|---|---|---|---|
| `0x01` OUT | isochronous, asynchronous, every 1 ms | 40 bytes | the samples of the modem to the line |
| `0x82` IN | isochronous, asynchronous, every 1 ms | 40 bytes | the samples of the line |
| `0x83` IN | interrupt, every 4 ms | 8 bytes | `lp_event_t` when an item changed |

### Samples

- S16, little-endian, mono, at the set rate. After reset, the rate is
  9600 Hz (the rate that the V.90 of D-Modem needs). All rates of the
  Si3056 are possible: 7200, 8000, 8229, 8400, 9000, 9600, 10286, 12000,
  13714, 16000.
- `SET_INTERFACE` to alternate setting 1 starts the two directions. A
  change back to 0 stops them. The stick empties the two FIFOs on each
  change. Samples also run on hook. Thus ring and caller-ID detection can
  operate on them (with the line monitor on, `SET_MONITOR`).
- **IN:** each packet has the samples that the codec gave after the last
  packet (9 or 10 each millisecond at 9600 Hz; up to 20 if a packet was
  missed). Its length *is* the clock of the stick: count samples, not
  milliseconds.
- **OUT:** in each millisecond, send the same number of samples as the last
  IN packet had. (The SmartUSB56 driver does exactly this: each OUT
  transfer uses the lengths of an IN transfer that just completed.)
  - The stick plays the samples at the clock of the codec, after a
    32-sample head start (about 3 ms).
  - If it has no samples, it plays silence, counts an underrun and waits
    for its head start again.
  - If the samples are more than its 1024-sample FIFO holds, the stick
    discards them and counts an overrun. (It does the same for line
    samples that the host does not take.)
- The delay of the line through the stick: the head start, plus the
  filters of the Si3056 (12 samples in each direction).

### Requests

Vendor control requests. The recipient is the **interface** (`wIndex` = 0)
or the **device** (as the driver of the SmartUSB56 sends them):
`bmRequestType` `0xC1` or `0xC0` IN, `0x41` or `0x40` OUT (`wLength` 0).
An unknown request or a bad value causes a stall.

| bRequest | Name | Dir | wValue | |
|---|---|---|---|---|
| `0x01` | GET_INFO | IN | 0 | `lp_info_t`, 12 bytes |
| `0x02` | GET_STATUS | IN | 0 | `lp_status_t`, 20 bytes (reads four DAA registers: ~1 ms) |
| `0x10` | SET_RATE | OUT | Hz | one of the rates above; set it before you start the samples |
| `0x11` | SET_HOOK | OUT | 1 off hook, 0 on hook | the OFHK pin: has an effect immediately |
| `0x12` | HOOK_FLASH | OUT | ms, 1–2000 | on hook for that time, then off hook again (no effect on hook) |
| `0x13` | SET_MONITOR | OUT | 1 on, 0 off | the on-hook line monitor (ONHM), for caller ID |
| `0x20` | DAA_READ | IN | register 0–127 | 1 byte: a Si3056/Si3018 register |
| `0x21` | DAA_WRITE | OUT | `register << 8 \| value` | a register; register 5 keeps OHE set (the pin keeps control of the hook) and its ONHM bit sets the monitor |
| `0x30` | RESET | OUT | 0 | the DAA is reset and brought up again, on hook (~150 ms) |
| `0x31` | REBOOT_UF2 | OUT | 0 | into the UF2 loader of the RP2040, 50 ms after the status stage |
| `0x4D` | MS_OS_20 | IN | 0 | (device, `wIndex` 7) the Microsoft OS 2.0 descriptor set, for Windows |

**Pulse dialling** is a function of the host. The host sends `SET_HOOK` 0
and 1 for each break and make, as `dmodemd` does with the hook GPIO of the
SmartUSB56. Each request has an effect when it arrives (much less than a
millisecond after the control transfer). There is no pulse timer in the
device.

**Country settings** are also a function of the host. After bring-up, the
line side has the reset values of the Si3056 (FCC: 600 Ω, the DC
termination of the US). Write the registers 16, 26, 30, 31 and the gains of
the country (Si3056 datasheet Table 15, "Country Specific Register
Settings") with `DAA_WRITE`.

**Disconnection** (or deconfiguration of the stick by the host) puts the
line on hook. The watchdog also does this if the firmware stops.

### Structures

All are little-endian and packed.

`lp_info_t` (12 bytes): `version` (1), `fw_major`, `fw_minor`, `fw_patch`,
`rate` (u16, Hz), `max_samples` (20, for each packet), `state` (below),
`reg11` (line-side ID in bits 7:4: 1 = Si3018, 3 = Si3019; system-side
revision in 3:0), `reg13` (line-side revision in bits 5:2), 2 reserved.

`lp_status_t` (20 bytes): `reg12` (FDT bit 6: the link across the barrier
operates; LCS[4:0], the loop current), `reg28` (LCS2), `reg29` (LVS, the
line voltage), `reg5` (RDTN, RDTP, ONHM, RDT, OHE, OH), `int_src` (the bits
of register 4 since the last status), `state`, `underruns` (u16),
`overruns` (u16), `samples_in` (u32: to the host), `samples_out` (u32:
played), `ring_edges`, 1 reserved.

`lp_event_t` (8 bytes, on `0x83` each time that it changes, and one time
when the samples start):
- `state`.
- `ring_edges` (the changes of the ring detector, mod 256: each one is a
  pulse for the ring cadence check of the modem, as are the changes of the
  ring GPIO of the SmartUSB56).
- `int_src` (register 4 since the last event: `0x80` ring, `0x40` receive
  overload, `0x20` the link lost, `0x08` the line dropped out, `0x01`
  polarity reversal).
- `seq` (+1 each event).
- `underruns`, `overruns` (u16 each).

`state` bits: `0x01` LINK (FDT), `0x02` OFF_HOOK, `0x04` MONITOR, `0x08`
STREAMING, `0x10` RINGING (the ring detector is active now), `0x20` FLASH.

### Inside the stick

The Si3056 is the master of the serial port (serial mode 0) on a 48 MHz
MCLK from the USB PLL of the RP2040 (PLL1 N = 125, M = 256 → 98.304 MHz).
The RP2040 is its slave in PIO (`src/line/si3056.pio`). Each 16-bit word
goes through `src/core/daa_frame.c` from the interrupt of the PIO. Register
accesses go in secondary frames, which the FC pin requests (16-bit mode, SB
set at bring-up). The hook is the OFHK pin (OHE set), the ring detector is
the RGDT pin, and the interrupt of the DAA is the AOUT/INT pin.


## WB stick (pico-serial WB)

The wideband bare-pair stick ([SPEC-WB.md](../SPEC-WB.md), `-DPS_BOARD=wb`)
is the sibling of the Line stick for bare copper pairs. It has a
TLV320AIC3204 codec behind a line transformer. It sends the samples of the
pair at 96 kHz to a wideband modem on the host (`dmodemd` of D-Modem). Its
USB device is the USB device of the Line stick (the same interface layout,
endpoints, sample framing and Microsoft OS 2.0 descriptors), with its own
requests: no hook, ring signal or caller ID. The definitions are in [`src/core/wb_proto.h`](src/core/wb_proto.h).

### Finding it

- USB `1209:0003` (a pid.codes *test* PID, as the `1209:0002` of the Line
  stick, until a real PID is assigned), product "pico-serial WB". The
  `board` of `GET_INFO` is 4. No kernel driver claims it: open it with
  libusb.
- **Windows** binds WinUSB to the two interfaces automatically, as for the
  Line stick: the same BOS descriptor, vendor request `0x4D` (`wIndex` 7),
  and the `DeviceInterfaceGUID` of D-Modem
  `{0463C853-BB62-4057-9E76-D14B0E3ACAFD}` for the line interface.
- Interface 0: class `0xFF`, subclass `0x57` ('W'), protocol `0x01`,
  "pico-serial WB". (Interface 1 is the reset interface of Raspberry Pi.)
- **Alternate setting 0** has no endpoints. **Alternate setting 1** has:

| Endpoint | Type | Max packet | |
|---|---|---|---|
| `0x01` OUT | isochronous, asynchronous, every 1 ms | 480 bytes | the samples of the modem to the line |
| `0x82` IN | isochronous, asynchronous, every 1 ms | 480 bytes | the samples of the line |
| `0x83` IN | interrupt, every 4 ms | 8 bytes | `wp_event_t` when an item changed |

### Samples

- S16, little-endian, **mono**, at the set rate: 96 000 Hz after reset;
  48 000 and 192 000 Hz for tests. This is 192 bytes each millisecond in
  each direction at 96 kHz, and 384 at 192 kHz. A packet holds up to 240
  samples (192 kHz with 25% to recover).
- **IN** has one of the two ADC channels of the codec (`SET_INPUT`):
  - The **hybrid** (the left ADC, the default): the line minus the replica
    of our transmit signal. Thus it is the signal of the far end, with our
    signal 10–20 dB down.
  - The **line** (the right ADC): the voltage of the pair itself, with our
    signal, inverted.

  The level of the other channel is in `GET_STATUS`. One channel keeps the
  packets at the sizes of SPEC-WB. The second channel is for the
  measurement of the hybrid. The peaks give this measurement without a
  stream of the second channel.
- **OUT**: one channel. The stick drives the line differentially from it
  (the two outputs of the left DAC: HPL = s, HPR = −s).
- `SET_INTERFACE` to alternate setting 1 starts the two directions. A
  change back to 0 stops them. The stick empties the two FIFOs on each
  change.
  - As on the Line stick, each IN packet has the samples that the codec
    gave after the last packet. Thus its length *is* the clock of the stick
    (the PLL of the codec on the 12 MHz crystal of the stick). Count
    samples, not milliseconds. In each millisecond, send the same number of
    OUT samples as the last IN packet had.
  - The stick plays them after a 3 ms head start. If it has no samples, it
    plays silence, counts an underrun and waits for its head start again.
  - If the samples are more than its 4096-sample FIFO holds, the stick
    discards them and counts an overrun. (It does the same for line
    samples that the host does not take.)
- The delay of the line through the stick: the head start, a third of a
  millisecond of DMA blocks in each direction, and the filters of the
  codec.

### Requests

Vendor control requests. The recipient is the **interface** (`wIndex` low
byte 0) or the **device**: `bmRequestType` `0xC1` or `0xC0` IN, `0x41` or
`0x40` OUT (`wLength` 0). An unknown request or a bad value causes a stall.

| bRequest | Name | Dir | wValue | |
|---|---|---|---|---|
| `0x01` | GET_INFO | IN | 0 | `wp_info_t`, 16 bytes |
| `0x02` | GET_STATUS | IN | 0 | `wp_status_t`, 28 bytes: the level of the line (peaks) and the flags of the codec since the last status |
| `0x10` | SET_RATE | OUT | kHz: 48, 96 or 192 | the codec is reset and brought up at that rate (about 100 ms); the settings below are kept |
| `0x11` | SET_CARRIER | OUT | 0 off, 1 on, 2 auto | the LINK LED: the carrier of the modem; auto (the default) lights it while the samples stream |
| `0x12` | SET_TX_GAIN | OUT | int16, 0.5 dB steps, −127..48 | the digital volume of the DAC (codec page 0 register 65), soft-stepped; 0 after reset |
| `0x13` | SET_RX_GAIN | OUT | 0..95, 0.5 dB steps | the gain of the hybrid MicPGA (page 1 register 59); 0 after reset |
| `0x14` | SET_INPUT | OUT | 0 hybrid, 1 line | the ADC channel that IN has |
| `0x15` | SET_LOOPBACK | OUT | 0 off, 1 firmware, 2 codec | see below |
| `0x20` | CODEC_READ | IN | register 0–127; `wIndex` = page << 8 | 1 byte: a codec register |
| `0x21` | CODEC_WRITE | OUT | `register << 8 \| value`; `wIndex` = page << 8 | a codec register, 1–127 (the firmware controls the page select) |
| `0x30` | RESET | OUT | 0 | the codec is reset and brought up again; rate and settings are kept |
| `0x31` | REBOOT_UF2 | OUT | 0 | into the UF2 loader of the RP2040, 50 ms after the status stage |
| `0x4D` | MS_OS_20 | IN | 0 | (device, `wIndex` 7) the Microsoft OS 2.0 descriptor set, for Windows |

**Loopbacks:**
- *Firmware* (1) sends the OUT samples of the host directly back on IN, at
  the clock of the codec, in place of the samples of the line. This tests
  the USB path and the driver of the host without the line.
- *Codec* (2) sends the ADCs of the codec to its DAC (page 0 register 29,
  D4). The stick sends the signal that it receives back onto the pair. This
  is a remote loopback for the stick at the other end. The hybrid residue
  also goes through the loop.

  > **CAUTION: KEEP `SET_RX_GAIN` LOW WHEN YOU USE THE CODEC LOOPBACK.**
  > With a replica that does not match well and a high `SET_RX_GAIN`, the
  > loop can ring.

**Levels:** at 0 dBFS (and `SET_TX_GAIN` 0), each driver swings 0.5 Vrms,
and 1.0 Vrms between them. Through 2 × 60.4 Ω into a 120 Ω pair (a 1:1
transformer), that is 0.5 Vrms on the line. This gives +3.2 dBm for a
full-scale sine, and about +4.8 dBm where the drivers clip (AVDD 1.77 V). A
multicarrier signal with a ~11 dB back-off for its peaks has an average of
about −8 dBm. The hybrid input gets to 0 dBFS at 0.5 Vrms (line minus
replica, `SET_RX_GAIN` 0: the +6 dB of the PGA for a differential pair on
10 kΩ). The line input gets to 0 dBFS at 1.0 Vrms (20 kΩ: our full-scale
transmit signal reads −6 dBFS there).

### Structures

All are little-endian and packed.

`wp_info_t` (16 bytes): `version` (1), `fw_major`, `fw_minor`, `fw_patch`,
`rate` (u32, Hz), `max_samples` (u16, 240), `state`, `board` (4: the WB
stick), `input`, `loopback`, `tx_gain` (i8, 0.5 dB), `rx_gain` (u8, 0.5 dB).

`wp_status_t` (28 bytes):
- `peak_hybrid`, `peak_line`, `peak_tx` (u16 each: the largest |sample|
  since the last status, 32768 = full scale, of the left ADC, the right ADC
  and the samples of the host that the stick played).
- `ovf` (page 0 register 42 since the last status: `0x80`/`0x40`
  left/right DAC overflow, `0x08`/`0x04` left/right ADC overflow).
- `oc` (the over current of register 44: `0x80` HPL, `0x40` HPR).
- `adc_flags` (register 36 now: `0x40`/`0x04` left/right ADC up,
  `0x80`/`0x08` their PGAs at gain).
- `dac_flags` (register 37 now: `0x80` left DAC up, `0x20` HPL, `0x02`
  HPR).
- `state`, 1 reserved.
- `underruns`, `overruns` (u16), `samples_in`, `samples_out` (u32).
- `resyncs` (u16: the I2S slave lost its position and started again).
- 2 reserved.

`wp_event_t` (8 bytes, on `0x83` each time that it changes, and one time
when the samples start): `state`, `seq` (+1 each event), `ovf`, `oc` (as in
the status, since the last event: the INT1 of the codec on MFP5 brings
them), `underruns`, `overruns` (u16 each).

`state` bits: `0x01` CODEC (it came up and its clocks run), `0x02` CARRIER
(the LINK LED is on), `0x04` LOOPBACK, `0x08` STREAMING.

### Inside the stick

The codec is the I2S master on the 12 MHz crystal of the RP2040 (GPOUT0 on
GPIO21, MCLK). Its PLL makes 12 × 8.1920 = 98.304 MHz (P = 1, R = 1, J = 8,
D = 1920), NDAC = NADC = MDAC = MADC = 4, and for each rate:

| Rate | DOSR = AOSR | Filter | DAC block | ADC block | BCLK = DAC_CLK / N |
|---|---|---|---|---|---|
| 48 kHz | 128 | A | PRB_P1 | PRB_R1 | N = 8: 3.072 MHz |
| 96 kHz | 64 | B | PRB_P7 | PRB_R7 | N = 4: 6.144 MHz |
| 192 kHz | 32 | C | PRB_P17 | PRB_R13 | N = 2: 12.288 MHz |

Each rate keeps these values:
- DOSR × fs = 6.144 MHz, and the modulators at 6.144 MHz (the limits of
  SLAA557: 2.8–6.2 MHz, 6.758 MHz).
- MDAC × OSR / 32 at or above the resource class of each block.
- 64 BCLKs in each frame (I2S, 32-bit words).

The RP2040 is the slave in PIO (`src/wb/i2s.pio`: the bit of the codec
comes in as BCLK rises, our bit goes out immediately after, with a full
BCLK of setup). DMA moves its words in blocks of 32 frames and gives them
to `src/core/wb_audio.c` from the interrupt of the DMA. If WCLK is not
where 32 bits on each side put it, the PIO stops and the firmware starts it
again (`resyncs`). The bring-up and the analog setup (A-LDO 1.77 V, common
mode 0.9 V, drivers from AVDD, the hybrid in the MicPGAs) are in
`src/core/wb_codec.c`. The registers are accessed through I2C1 (address
`0x18`).
