<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 Neo-Desktop -->
# pico-serial Line — phone-line modem stick (draft 0.1)

A USB modem for a real telephone line, in the shape of Apple's USB Modem:
a stick about as long as a disposable lighter, with an **RJ11 socket** at
one end and a **USB-C receptacle** at the other (Apple's had a captive
USB-A cable).

It is a **softmodem**: the stick is the telephone-line interface (a DAA
and its codec) with an RP2040 between it and USB. The modem itself, every
modulation from V.21 to V.90, fax and voice, is **D-Modem-RE's open DSP**
running on whatever the stick is plugged into:

- **Linux:** `dmodemd` drives it through libusb, as it drives Smart
  Link's SmartUSB56 today (§6). The modem appears as a pty.
- **Windows:** `dmodemd` drives it through libusb on WinUSB, which
  Windows binds by itself (Microsoft OS 2.0 descriptors: no INF).
- **Android:** dmodem-android gets a real telephone line beside SIP
  (§6.2).

So the stick has no modem firmware of its own to go out of date, and
everything that makes the connection is open, from the line interface up.

Everything in [SPEC.md](SPEC.md) about the RP2040 core (power, USB-C,
flash, crystal, buttons, UF2 updates) still applies. There is no RS-232
side.

---

## 1. Requirements

| # | Requirement |
|---|---|
| L1 | USB-C device port, USB 2.0 Full Speed, powered from VBUS only, under 100 mA |
| L2 | One RJ11 socket (6P2C or 6P4C, the middle pair: TIP and RING), loop start lines |
| L3 | V.90 as the analogue modem: the line sampled at **9600 Hz** (D-Modem's V.90 needs it, hardware.md "Modulations"); 8000 and 16000 Hz too |
| L4 | Hook control fast enough for **pulse dialling** (D-Modem times the pulses) and hook flash |
| L5 | **Ring detection** and **caller ID** (the line heard while on hook) |
| L6 | Isolation from the line: ≥ 1500 Vrms, the DAA's capacitive barrier; line surge protection and a telecom fuse |
| L7 | Driverless on Linux, Android and Windows: a vendor-class USB interface for libusb (no kernel driver; WinUSB by Microsoft OS 2.0 descriptors) |
| L8 | Firmware update over the same USB-C cable (UF2) |
| L9 | A stick about **82 mm** long (a disposable lighter), RJ11 at one end, USB-C at the other |
| L10 | Every part normally stocked and buyable at qty 1 |

---

## 2. Block diagram

```
 USB-C ── ESD ── RP2040 ──────── MCLK 48 MHz (GPOUT0) ──►  Si3056  ═╪═  Si3018/19 ── bridge ── fuse ── RJ11
  (UFP)          │   PIO ◄─ SCLK, FSYNC, SDO ─┤                (system   ║   (line      hook
  LDO 3.3 V      │   PIO ── SDI ─────────────►│                 side)  C1,C2  side)     switch,
  flash, xtal    │   GPIO ── OFHK, RESET, FC ►│                        Y2 caps          ring, CID
                 │   GPIO ◄─ RGDT, INT ───────┤                        ║
                 └── LEDs (OH, RI)                                     isolation barrier
```

---

## 3. The line interface: Si3056 + Si3018/19

Skyworks' (formerly Silicon Labs') **Si3056** "Global Serial Interface
DAA" is the part made for softmodems: a codec with a serial port for a
modem DSP, at any sample rate from 7.2 to 16 kHz (`Fs = FPLL2 / 5120`).
Its line-side partner (Si3018 or Si3019) holds the hook switch, the DC and
AC termination, the ring detector and the caller-ID path, programmable for
every country's line (impedance, current limit, ring thresholds). The two
talk across two high-voltage capacitors: that is the isolation barrier.

Why not the alternatives:
- **Si3050** (Skyworks' voice DAA, same line side): its PCM highway runs
  at 8 kHz only. D-Modem's V.90 needs 9600 Hz (at 8000 Hz on a clock of
  its own it loses the PCM signal every few seconds).
- **Si2494/Si3018 ISOmodem**: a complete modem chip; it would make the
  stick plug-and-play but closed, with no path for D-Modem.
- **A transformer DAA + codec**: more parts, a relay or opto hook switch,
  and every country's termination done by hand.

### 3.1 Circuit

The datasheet's typical application (Si3056 datasheet rev 1.03,
Figure 17, and its bill of materials), unchanged in topology:

| Ref (datasheet) | Value | Job |
|---|---|---|
| C1, C2 | 33 pF, Y2 | the isolation barrier (C1A/C1B, C2A/C2B) |
| C8, C9 | 680 pF, Y2 | TIP and RING to the system ground, EMI (across the barrier) |
| C3 | 10 nF, 250 V | line-side AC path |
| C4 | 1 µF, 50 V | RX/DCT filter |
| C5, C6, C50, C51 | 100 nF | VREG, VREG2, VD, VA |
| C7 | 2.7 nF, 50 V | |
| C10 | 10 nF | |
| D1, D2 | dual diodes, 300 V (or a bridge) | the line's polarity |
| FB1, FB2 | 600 Ω @ 100 MHz, ≥ 200 mA | EMI, on the protected side of RV1 |
| R15, R16 | 0 Ω (or ferrites) | |
| F (added) | 1.25 A telecom fuse in TIP | UL60950 overcurrent (datasheet appendix, Figure 37) |
| Q1, Q3 | MMBTA42 | hook switch, DC termination |
| Q2 | MMBTA92 | |
| Q4, Q5 | MMBT2484 (or MMBTA06) | |
| RV1 | sidactor, 275 V, 100 A | surge |
| R1 | 1.07 kΩ ½ W | R3: 3.65 kΩ ½ W; R4: 2.49 kΩ ½ W; R11: 73.2 Ω ½ W; R10: 536 Ω ¼ W |
| R2, R5, R6, R9 | 150 Ω, 100 kΩ, 100 kΩ, 1 MΩ | |
| R7, R8 | 20 MΩ | caller ID (the line heard on hook) |
| R12, R13 | 56.2 Ω | in series with C1, C2 |
| R51–R53 | 4.7 kΩ | pull-ups: RGDT, OFHK, INT |
| Z1 | 43 V zener | |

The part numbers are in the netlist script (`LINE_PARTS` in
`hardware/netlist/build_netlist.py`; `--variant line`) and its BOM
(`bom-jlc-line.csv`), each checked against JLCPCB's stock on 2026-10-22.
The line side is the **Si3018-F-FS** (active; the Si3019 has the same pins
and a few more line-level options, at $2.60 more). Neither Skyworks chip is
stocked at LCSC: they come from DigiKey, and are fitted by hand or sent to
JLC as consigned parts, as is the through-hole RJ11.

### 3.2 Connections to the RP2040

| Si3056 pin | RP2040 | |
|---|---|---|
| 1 MCLK | GPIO21 (GPOUT0) | 48 MHz: the USB PLL, divider 1. PLL1: N = 125, M = 256 gives 98.304 MHz; FUP1 = 384 kHz ≥ 144 kHz (§5.25) |
| 2 FSYNC (out) | PIO | frame start, one per sample |
| 3 SCLK (out) | PIO | 256 × Fs: 2.4576 MHz at 9600 Hz |
| 5 SDO (out) | PIO | 16-bit samples, then control words in secondary frames |
| 6 SDI (in) | PIO | |
| 7 FC | GPIO | requests a secondary frame (16-bit TX mode, SB = 1) |
| 8 RESET | GPIO | |
| 11 AOUT/INT | GPIO | the Si3056's interrupt (programmed as INT), pulled up |
| 14 M0 | GPIO | serial mode, read at reset |
| 15 RGDT/M1 | GPIO | M1 = 0 driven through reset (master mode), then the ring detector (active low, pulled up) |
| 16 OFHK | GPIO | the hook: low = off hook, pulled up so the line stays free until the firmware runs |

The RP2040 is the serial port's slave: the Si3056 makes SCLK and FSYNC
from MCLK, so the sample clock is the board's crystal through the USB
PLL, and the USB side follows it (§5).

### 3.3 Layout rules (Skyworks AN67)

- No ground plane under the line side; the line-side parts close
  together, inside the barrier.
- The barrier: C1, C2, C8, C9 bridge it; nothing else does. Creepage and
  clearance across it at least **2.5 mm** on every layer (UL60950 basic
  insulation for a telecom network at 250 V), wider where the board
  allows.
- RV1, the fuse and the ferrites right at the RJ11; TIP and RING short
  and wide.

---

## 4. Rest of the board

The RP2040 core of SPEC.md §4.2 (the chip, W25Q flash, 12 MHz crystal,
AP2112K-3.3, USBLC6 ESD, the USB-C with its CC resistors, a polyfuse),
plus:
- two LEDs: **OH** (off hook) and **RI** (ringing);
- the BOOTSEL and RESET buttons (inside; the stick also reboots to its
  UF2 loader on a vendor request);
- test pads for SWD, 3.3 V and the serial port.

The RS-232 chips, the DB25 and their GPIOs are not fitted: the netlist
script's `line` variant leaves them out.

---

## 5. USB interface (firmware)

One **vendor-class** interface (class 0xFF), USB 2.0 Full Speed, VID
`0x1209` (pid.codes) with a test PID until a real one is assigned. It is
shaped like the SmartUSB56's, so D-Modem's driver for that is the model
for this one:

| Endpoint | Type | |
|---|---|---|
| EP1 IN | isochronous, 1 ms | the line's samples: S16 little-endian, mono, at Fs. Each packet carries the samples the codec gave in that millisecond (9 or 10 at 9600 Hz): the host's clock is the stick's |
| EP1 OUT | isochronous, 1 ms | the modem's samples, the same format; the stick plays them at its codec's clock, a small buffer absorbing USB's timing |
| EP2 IN | interrupt, 8 ms | events: ring detector changes, line voltage/current alerts, overload, a polarity reversal, buffer underrun/overrun |

Vendor control requests (EP0): the stick's info and version; the sample
rate; the hook (off, on, a timed flash or pulse); the line monitor (on
hook, for caller ID); a Si3056/Si3018 register read and write (the host
sets the country's line from D-Modem's table); reset; reboot to UF2. The
exact numbers go in `firmware/PROTOCOL.md` with the firmware.

Samples run both ways from the moment the host starts them, on hook too,
so ring and caller-ID detection can run on the samples as they do on
ALSA codecs.

---

## 6. Host software

### 6.1 D-Modem-RE (Linux)

A second device kind beside the SmartUSB56 in `dmodemd/usb_line.cpp`:
`dmodemd --usb=auto` finds either. The differences from the ST7554:
- samples at 9600 Hz by default (V.90);
- the hook and the line monitor by vendor request, not GPIO registers;
- caller ID (the ST7554's codec hears nothing on hook; this one does);
- the country's line settings written to the line side's registers.

### 6.2 dmodem-android

The app gets the stick through Android's USB host API (a `UsbDeviceConnection`'s
descriptor handed to libusb), so the same line code runs on the phone:
the modem on a real telephone line, beside SIP and the line to another
phone.

---

## 7. Mechanical

- **Length about 82 mm**, the length of a disposable lighter (a BIC
  classic is 81 mm), with a cross-section set by the RJ11 socket.
- RJ11 at one end, its latch toward the top; USB-C at the other.
- A 3D-printed shell in two halves (as the W hood: MJF PA12), the board
  held between them. Light pipes or thin windows for the two LEDs.
- `hardware/enclosure/stick.py` makes the shell, the board outline and
  the placement file, and checks the fit.

---

## 8. Compliance (only if you sell it)

- Connecting equipment to the telephone network is regulated: in the US,
  **FCC Part 68 / TIA-968** registration (the ACTA database) and a
  ringer equivalence number; in Europe the radio and low-voltage rules
  with EN 62368-1 and the telecom network interface standards; elsewhere
  each country's own. The Si3056 is designed to meet them all
  ("globally compliant"), but the product still needs testing.
- Safety: UL/EN 62368-1 (formerly 60950) for the barrier and the
  overvoltage tests; the datasheet's appendix lists what passes.
- None of this applies to building your own, but use it on your own line
  and at your own risk.

---

## 9. Open questions

1. **The Si3056 is "not for new designs"** at Skyworks (October 2026:
   DigiKey about 1,400 Si3056-D-FS and 2,600 on reel; Si3056-FS
   obsolete). Fine for building these; for a long run, buy ahead. The
   fallback is the Si3050 (active, same line side), at 8 kHz only: V.34
   and below, no V.90.
2. The RJ11 (Ckmtw R-RJ11R06P-A800, LCSC C189750) is 13 mm tall, so the
   stick is about 18-19 mm thick, against a lighter's 15. A mid-mount
   jack would save 3-4 mm.
3. Whether to run the samples isochronous (as the SmartUSB56) or bulk:
   isochronous keeps D-Modem's driver as it is; bulk is simpler on
   Android without libusb.

## 10. Next steps

1. Netlist and schematic (`--variant line`).
2. The shell (`stick.py`), then the PCB.
3. Firmware: the Si3056's serial port in PIO, the USB interface.
4. D-Modem-RE's driver, against a fake device first (as `fake_st7554`).
