<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 D-Modem Authors -->
# pico-serial Line — phone-line modem stick (draft 0.1)

pico-serial Line is a USB modem for a real telephone line. Its shape is
the same as the USB Modem of Apple: a stick with about the length of a
disposable lighter. It has an **RJ11 socket** at one end and a **USB-C
receptacle** at the other end. (The modem of Apple had a captive USB-A
cable.)

It is a **softmodem**. The stick is the telephone-line interface (a DAA
and its codec), with an RP2040 between the interface and USB. The modem
itself is the **open DSP of D-Modem**: all modulations from V.21 to V.90,
fax and voice. The DSP runs on the device that the stick connects to:

- **Linux:** `dmodemd` drives the stick through libusb, as it drives the
  SmartUSB56 of Smart Link now (§6). The modem shows as a pty.
- **Windows:** `dmodemd` drives the stick through libusb on WinUSB.
  Windows binds WinUSB automatically (Microsoft OS 2.0 descriptors: no
  INF).
- **Android:** the D-Modem app gets a real telephone line in addition to
  SIP (§6.2).

Thus the stick has no modem firmware of its own that can become out of
date. All parts that make the connection are open, from the line interface
up.

All of [SPEC.md](SPEC.md) about the RP2040 core (power, USB-C, flash,
crystal, buttons, UF2 updates) still applies. There is no RS-232 side.

---

## 1. Requirements

| # | Requirement |
|---|---|
| L1 | USB-C device port, USB 2.0 Full Speed, power from VBUS only, less than 100 mA |
| L2 | One RJ11 socket (6P2C or 6P4C, the middle pair: TIP and RING), loop start lines |
| L3 | V.90 as the analogue modem: the line sampled at **9600 Hz** (the V.90 of D-Modem needs it, hardware.md "Modulations"); also 8000 and 16000 Hz |
| L4 | Hook control that is sufficiently fast for **pulse dialling** (D-Modem sets the time of the pulses) and for hook flash |
| L5 | **Ring detection** and **caller ID** (the line monitored while on hook) |
| L6 | Isolation from the line: ≥ 1500 Vrms, the capacitive barrier of the DAA; line surge protection and a telecom fuse |
| L7 | No driver necessary on Linux, Android and Windows: a vendor-class USB interface for libusb (no kernel driver; WinUSB through Microsoft OS 2.0 descriptors) |
| L8 | Firmware update through the same USB-C cable (UF2) |
| L9 | A stick about **82 mm** long (a disposable lighter), RJ11 at one end, USB-C at the other end |
| L10 | All parts are usually in stock, and you can buy a quantity of 1 |

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

The **Si3056** "Global Serial Interface DAA" of Skyworks (before, Silicon
Labs) is the part that is made for softmodems. It is a codec with a serial
port for a modem DSP, at all sample rates from 7.2 to 16 kHz
(`Fs = FPLL2 / 5120`). Its line-side partner (Si3018 or Si3019) has the
hook switch, the DC and AC termination, the ring detector and the
caller-ID path. You can program it for the line of each country
(impedance, current limit, ring thresholds). The two chips communicate
across two high-voltage capacitors. These capacitors are the isolation
barrier.

These are the reasons that we do not use the alternatives:
- **Si3050** (the voice DAA of Skyworks, the same line side): its PCM
  highway operates at 8 kHz only. The V.90 of D-Modem needs 9600 Hz. (At
  8000 Hz on its own clock, it loses the PCM signal after some seconds.)
- **Si2494/Si3018 ISOmodem**: a complete modem chip. It makes the stick
  plug-and-play, but closed, with no path for D-Modem.
- **A transformer DAA + codec**: more parts, a relay or opto hook switch,
  and the termination for each country must be done by hand.

### 3.1 Circuit

This is the typical application of the datasheet (Si3056 datasheet
rev 1.03, Figure 17, and its bill of materials). The topology does not
change:

| Ref (datasheet) | Value | Function |
|---|---|---|
| C1, C2 | 33 pF, Y2 | the isolation barrier (C1A/C1B, C2A/C2B) |
| C8, C9 | 680 pF, Y2 | TIP and RING to the system ground, EMI (across the barrier) |
| C3 | 10 nF, 250 V | line-side AC path |
| C4 | 1 µF, 50 V | RX/DCT filter |
| C5, C6, C50, C51 | 100 nF | VREG, VREG2, VD, VA |
| C7 | 2.7 nF, 50 V | |
| C10 | 10 nF | |
| D1, D2 | dual diodes, 300 V (or a bridge) | the polarity of the line |
| FB1, FB2 | 600 Ω @ 100 MHz, ≥ 200 mA | EMI, on the protected side of RV1 |
| R15, R16 | 0 Ω (or ferrites) | |
| F (added) | 1.25 A telecom fuse in TIP | UL60950 overcurrent (datasheet appendix, Figure 37) |
| Q1, Q3 | MMBTA42 | hook switch, DC termination |
| Q2 | MMBTA92 | |
| Q4, Q5 | MMBT2484 (or MMBTA06) | |
| RV1 | sidactor, 275 V, 100 A | surge |
| R1 | 1.07 kΩ ½ W | R3: 3.65 kΩ ½ W; R4: 2.49 kΩ ½ W; R11: 73.2 Ω ½ W; R10: 536 Ω ¼ W |
| R2, R5, R6, R9 | 150 Ω, 100 kΩ, 100 kΩ, 1 MΩ | |
| R7, R8 | 20 MΩ | caller ID (the line monitored on hook) |
| R12, R13 | 56.2 Ω | in series with C1, C2 |
| R51–R53 | 4.7 kΩ | pull-ups: RGDT, OFHK, INT |
| Z1 | 43 V zener | |

The part numbers are in the netlist script (`LINE_PARTS` in
`hardware/netlist/build_netlist.py`; `--variant line`) and in its BOM
(`bom-jlc-line.csv`). We compared each part with the stock of JLCPCB on
2026-10-22. The line side is the **Si3018-F-FS** (active). The Si3019 has
the same pins and some more line-level options, at $2.60 more. LCSC does
not have the two Skyworks chips in stock. They come from DigiKey. You
install them by hand, or send them to JLC as consigned parts. This is also
true for the through-hole RJ11.

### 3.2 Connections to the RP2040

| Si3056 pin | RP2040 | |
|---|---|---|
| 1 MCLK | GPIO21 (GPOUT0) | 48 MHz: the USB PLL, divider 1. PLL1: N = 125, M = 256 gives 98.304 MHz; FUP1 = 384 kHz ≥ 144 kHz (§5.25) |
| 2 FSYNC (out) | PIO | frame start, one for each sample |
| 3 SCLK (out) | PIO | 256 × Fs: 2.4576 MHz at 9600 Hz |
| 5 SDO (out) | PIO | 16-bit samples, then control words in secondary frames |
| 6 SDI (in) | PIO | |
| 7 FC | GPIO | requests a secondary frame (16-bit TX mode, SB = 1) |
| 8 RESET | GPIO | |
| 11 AOUT/INT | GPIO | the interrupt of the Si3056 (programmed as INT), with a pull-up |
| 14 M0 | GPIO | serial mode, read at reset |
| 15 RGDT/M1 | GPIO | M1 = 0 driven through reset (master mode), then the ring detector (active low, with a pull-up) |
| 16 OFHK | GPIO | the hook: low = off hook. A pull-up keeps the line free until the firmware runs |

The RP2040 is the slave of the serial port. The Si3056 makes SCLK and FSYNC
from MCLK. Thus the sample clock is the crystal of the board through the
USB PLL, and the USB side follows it (§5).

### 3.3 Layout rules (Skyworks AN67)

- No ground plane below the line side. Put the line-side parts near
  together, inside the barrier.
- The barrier: only C1, C2, C8 and C9 go across it.

  > **WARNING: KEEP THE CREEPAGE AND CLEARANCE ACROSS THE BARRIER AT A
  > MINIMUM OF 2.5 MM ON ALL LAYERS.** The barrier isolates the USB side
  > from the telephone network (L6). A smaller distance can let the line
  > voltage go across the barrier.

  The creepage and clearance across the barrier must be a minimum of
  **2.5 mm** on all layers (UL60950 basic insulation for a telecom network
  at 250 V). Make them wider where the board permits it.
- Put RV1, the fuse and the ferrites immediately at the RJ11. Make TIP and
  RING short and wide.

---

## 4. Rest of the board

The board has the RP2040 core of SPEC.md §4.2 (the chip, W25Q flash,
12 MHz crystal, AP2112K-3.3, USBLC6 ESD, the USB-C with its CC resistors, a
polyfuse). It also has these items:
- Two LEDs: **OH** (off hook) and **RI** (ring signal).
- The BOOTSEL and RESET buttons (inside the shell. The stick also reboots
  to its UF2 loader on a vendor request).
- Test pads for SWD, 3.3 V and the serial port.

The RS-232 chips, the DB25 and their GPIOs are not installed: the `line`
variant of the netlist script does not include them.

---

## 5. USB interface (firmware)

The stick has one **vendor-class** interface (class 0xFF), USB 2.0 Full
Speed, VID `0x1209` (pid.codes). It uses a test PID until it gets a real
PID. Its shape is the same as the interface of the SmartUSB56. Thus the
driver of D-Modem for the SmartUSB56 is the model for this driver:

| Endpoint | Type | |
|---|---|---|
| EP1 IN | isochronous, 1 ms | the samples of the line: S16 little-endian, mono, at Fs. Each packet has the samples that the codec gave in that millisecond (9 or 10 at 9600 Hz): the clock of the host is the clock of the stick |
| EP1 OUT | isochronous, 1 ms | the samples of the modem, in the same format. The stick plays them at the clock of its codec. A small buffer absorbs the timing of USB |
| EP2 IN | interrupt, 8 ms | events: ring detector changes, line voltage/current alerts, overload, a polarity reversal, buffer underrun/overrun |

Vendor control requests (EP0):
- The info and version of the stick.
- The sample rate.
- The hook (off, on, a timed flash or pulse).
- The line monitor (on hook, for caller ID).
- A Si3056/Si3018 register read and write (the host sets the line of the
  country from the table of D-Modem).
- Reset.
- Reboot to UF2.

The exact numbers go in `firmware/PROTOCOL.md` with the firmware.

Samples go in the two directions from the time that the host starts them,
also on hook. Thus the ring and caller-ID detection can run on the samples,
as on ALSA codecs.

---

## 6. Host software

### 6.1 D-Modem (Linux)

A second device type in addition to the SmartUSB56 in `frontend/dmodemd/usb_line.cpp`:
`dmodemd --usb=auto` finds the two types. These are the differences from
the ST7554:
- The default sample rate is 9600 Hz (V.90).
- The hook and the line monitor operate through vendor requests, not GPIO
  registers.
- Caller ID (the codec of the ST7554 receives nothing on hook; this codec
  receives the line).
- The firmware writes the line settings of the country to the registers of
  the line side.

### 6.2 The D-Modem app (Android)

The app gets access to the stick through the USB host API of Android (the
descriptor of a `UsbDeviceConnection` goes to libusb). Thus the same line
code runs on the phone: the modem on a real telephone line, in addition to
SIP and the line to a different phone.

---

## 7. Mechanical

- **Length about 82 mm**, the length of a disposable lighter (a BIC
  classic is 81 mm). The RJ11 socket sets the cross-section.
- The RJ11 is at one end, with its latch toward the top. The USB-C is at
  the other end.
- A 3D-printed shell in two halves (the same as the W hood: MJF PA12).
  The board is held between the halves. Light pipes or thin windows for the
  two LEDs.
- `hardware/enclosure/stick.py` makes the shell, the board outline and
  the placement file. It also does a check of the fit.

---

## 8. Compliance (only if you sell it)

- There are regulations for equipment that connects to the telephone
  network:
  - In the US: **FCC Part 68 / TIA-968** registration (the ACTA database)
    and a ringer equivalence number.
  - In Europe: the radio and low-voltage rules with EN 62368-1 and the
    standards for the telecom network interface.
  - In other countries: the rules of each country.

  The design of the Si3056 agrees with all of them ("globally compliant").
  But the product must still have tests.
- Safety: UL/EN 62368-1 (before, 60950) for the barrier and the
  overvoltage tests. The appendix of the datasheet gives the items that
  pass.
- These items do not apply if you build a stick for yourself. But use it
  on your own line and at your own risk.

---

## 9. Open questions

1. **The Si3056 is "not for new designs"** at Skyworks (October 2026:
   DigiKey about 1,400 Si3056-D-FS and 2,600 on reel; Si3056-FS
   obsolete). This is satisfactory to build these sticks. For a long
   production run, buy the parts early. The fallback is the Si3050
   (active, the same line side), at 8 kHz only: V.34 and below, no V.90.
2. The RJ11 (Ckmtw R-RJ11R06P-A800, LCSC C189750) is 13 mm tall. Thus the
   stick is about 18-19 mm thick, but a lighter is 15 mm thick. A mid-mount
   jack can decrease this by 3-4 mm.
3. Send the samples as isochronous (as the SmartUSB56) or as bulk?
   Isochronous keeps the driver of D-Modem as it is. Bulk is simpler on
   Android without libusb.

## 10. Next steps

1. Netlist and schematic (`--variant line`).
2. The shell (`stick.py`), then the PCB.
3. Firmware: the serial port of the Si3056 in PIO, the USB interface.
4. The driver of D-Modem, first with a fake device (as `fake_st7554`).
