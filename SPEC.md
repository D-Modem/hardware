# pico-serial — hardware spec (draft 0.1)

A USB-C to DB25 RS-232 adapter built on a bare **RP2040** (the chip on the
Raspberry Pi Pico). There is no Pico module: the chip, flash, crystal,
regulator and every passive part are on this board.

Main use: plug a phone running **dmodem-android** into the back of a vintage
computer. The board is wired as the **modem side (DCE)**, with a **female
DB25**, so it plugs straight into a PC, Amiga or Atari ST serial port with no
null-modem cable or gender changer. To the computer it looks like an
external modem.

---

## 1. Requirements

| # | Requirement |
|---|---|
| R1 | USB-C device (UFP) port, USB 2.0 Full Speed, powered from VBUS only |
| R2 | Shows up as a standard **CDC-ACM** serial port (no driver on Android, Linux, macOS or Windows 10+) |
| R3 | DB25 female, DCE pinout, real RS-232 levels (±5 V or more into 3 kΩ) |
| R4 | All 8 modem lines: TXD, RXD, RTS, CTS, DTR, DSR, DCD, RI, plus signal ground |
| R5 | 300 baud to at least 230 400 baud; 5–8 data bits, none/even/odd parity, 1 or 2 stop bits, break |
| R6 | Hardware RTS/CTS flow control on the RS-232 side |
| R7 | Works with dmodem-android's existing USB bridge as it is today (see §6) |
| R8 | Firmware update over the same USB-C cable (UF2 drag-and-drop) |
| R9 | ±15 kV ESD on the RS-232 pins, ESD protection on USB |
| R10 | Fits inside a standard DB25 hood/backshell; USB-C exits where the cable normally does |
| R11 | Draws under 100 mA, so a phone can power it on OTG |

---

## 2. Block diagram

```
 USB-C ──┬─ CC1/CC2: 5.1k Rd each ─ GND
 (UFP)   ├─ VBUS ─ PTC 500mA ─ TVS ─┬─ LDO 3.3V ──┬─ RP2040 IOVDD/USB/ADC
         │                          │             ├─ W25Q flash
         │                          └─ VBUS sense └─ MAX3238E VCC
         └─ D+/D- ─ USBLC6 ESD ─ 27Ω ─ RP2040 USB

 RP2040 ── UART0 + GPIO ── MAX3238E (5 drivers / 3 receivers) ── DB25 female (DCE)
   │
   ├─ QSPI ── W25Q16/W25Q128 flash
   ├─ XIN/XOUT ── 12 MHz crystal
   ├─ BOOTSEL button, RUN (reset) button
   ├─ SWD pads
   └─ status LEDs (TR, CD, SD, RD, RI, PWR)
```

---

## 3. Key parts

| Function | Part (suggested) | Package | Notes |
|---|---|---|---|
| MCU | RP2040 | QFN-56 7×7 mm | See §4.1 for why not RP2350 |
| Flash | Winbond W25Q16JV (2 MB) or W25Q128JV (16 MB) | SOIC-8 or USON-8 | 2 MB is plenty; 128 matches the Pico |
| Crystal | Abracon ABM8-272-T3, 12 MHz | 3.2×2.5 mm | The part the RP2040 design guide is tuned for |
| Regulator | AP2112K-3.3 or TLV75733P (LDO) | SOT-23-5 | 5 V → 3.3 V; ~80 mA load, ~0.15 W dissipated |
| RS-232 | **MAX3238E** (5 TX / 3 RX, 3.0–5.5 V, ±15 kV ESD, 250 kbps) | SSOP-28 / TSSOP-28 | 5 drivers / 3 receivers is the DCE split. Check stock and second sources before committing |
| USB ESD | USBLC6-2SC6 | SOT-23-6 | On D+/D- with VBUS |
| VBUS TVS | SMF5.0A or similar 5 V TVS | SOD-123F | |
| VBUS fuse | 500 mA hold PTC | 0805/1206 | |
| USB-C | 16-pin USB 2.0 receptacle (e.g. GCT USB4105, HRO TYPE-C-31-M-12) | SMT + through-hole shell tabs | Mid-mount if the hood is tight |
| DB25 | Female, **straddle mount** for a 1.6 mm board | — | Board sits between the two pin rows so it fits the hood. Exact part TBD (§8) |
| Buttons | 2 × small tactile (BOOTSEL, RESET) | SMD 3×4 mm or similar | |
| LEDs | 6 × 0603 | 0603 | Optional; only visible with a clear hood or light pipes |

Passives are 0402/0603 throughout unless noted.

---

## 4. Circuit detail

### 4.1 Why RP2040 and not RP2350

Both work. RP2040 is chosen because:

- its core regulator is a linear LDO that needs only two capacitors. The
  RP2350's core regulator is a switcher that needs a 3.3 µH inductor and a
  careful layout;
- this job needs very little: one UART, about 15 GPIO and full-speed USB;
- Raspberry Pi's "minimal design" reference covers every part used here.

Switching to an RP2350A later changes only §4.2 and §4.3; the rest of the
board stays the same.

### 4.2 RP2040 support parts (from the RP2040 hardware design guide's minimal design)

| Pin(s) | Parts |
|---|---|
| IOVDD ×6 | 100 nF each, one per pin, placed close |
| USB_VDD | 100 nF |
| ADC_AVDD | 100 nF (via a small RC filter if the ADC is used; it isn't for serial) |
| VREG_VIN | 1 µF |
| VREG_VOUT → DVDD ×2 | 1 µF on VREG_VOUT, 100 nF on each DVDD pin |
| 3V3 rail | 10 µF bulk near the LDO output, 10 µF at the LDO input |
| XIN / XOUT | 12 MHz crystal, 15 pF to GND on each side, **1 kΩ in series on XOUT** |
| USB_DP / USB_DM | 27 Ω series each, close to the RP2040 |
| QSPI_SS | to flash CS; also to the BOOTSEL button through 1 kΩ (button to GND). Optional 10 kΩ pull-up to 3V3, not fitted by default |
| QSPI_SD0–3, SCLK | straight to flash, short traces; 100 nF on flash VCC |
| RUN | RESET button to GND; optional 10 kΩ pull-up + 100 nF (RUN has an internal pull-up) |
| TESTEN | tie to GND |
| SWCLK / SWDIO | to test pads or a 3-pin 1.27 mm header (with GND) |
| GND pad | the QFN's center pad is GND: full paste window split into a grid, many vias |

Firmware note: build with `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64` (or more)
for margin with whatever crystal is fitted.

### 4.3 Power

- VBUS → 500 mA PTC → 5 V TVS → LDO → 3V3.
- VBUS → 100 kΩ / 100 kΩ divider → GPIO29 (ADC3), the same as the Pico, to
  read the supply.
- Budget: RP2040 ~25 mA, MAX3238E ~1 mA plus ~10–20 mA driving five lines
  into 3 kΩ, LEDs ~10 mA. **Under 80 mA total**, well inside what a phone
  gives on OTG.
- The board does not pass power through to the phone and does not charge
  it (see §8).

### 4.4 USB-C

- **CC1 and CC2 each get their own 5.1 kΩ to GND.** Do not share one
  resistor; some hosts won't turn on VBUS if you do.
- Connect both D+ pads together and both D− pads together (both plug
  orientations).
- Shell → GND through 1 MΩ in parallel with 4.7 nF (fit a 0 Ω instead to
  bond it directly).
- SBU1/SBU2 not connected.
- D+/D−: short, matched, 90 Ω differential if the stack-up allows. At
  12 Mbit/s a short 2-layer route works fine.

### 4.5 RS-232 (MAX3238E, wired as DCE)

The computer is the DTE. Its outputs (TXD, RTS, DTR) are our inputs, and
its inputs (RXD, CTS, DSR, DCD, RI) are our outputs. That is 3 receivers
and 5 drivers, which is exactly what the MAX3238E has.

- Charge-pump and VCC capacitors: values from the datasheet table for
  3.0–3.6 V (100 nF in the usual case). Use X7R rated ≥ 16 V on V+ and V−.
- FORCEON and FORCEOFF go to GPIOs so firmware can choose always-on or
  auto-shutdown. Default: always on while USB is configured.
- INVALID → GPIO: high when a powered computer port is connected
  (valid RS-232 levels on the receivers). Firmware can report "computer
  connected".
- R1OUTB (the receiver output that stays active in shutdown) → GPIO, for
  waking on traffic from the computer.

### 4.6 DB25 female pinout (DCE)

| DB25 pin | Signal | Direction (from the board) | MAX3238E | RP2040 |
|---|---|---|---|---|
| 1 | Protective / frame ground | — | — | Shell, to GND through 0 Ω (option to leave open) |
| 2 | TXD (computer sends) | in | R1 | GP1 — UART0 RX |
| 3 | RXD (computer receives) | out | T1 | GP0 — UART0 TX |
| 4 | RTS (computer's) | in | R2 | GP2 — UART0 CTS |
| 5 | CTS (to computer) | out | T2 | GP3 — UART0 RTS (or plain GPIO) |
| 6 | DSR (to computer) | out | T3 | GP4 |
| 7 | Signal ground | — | — | GND |
| 8 | DCD (to computer) | out | T4 | GP5 |
| 20 | DTR (computer's) | in | R3 | GP7 |
| 22 | RI (to computer) | out | T5 | GP6 |
| all others | **not connected** | | | |

Leave every other pin unconnected. Some machines put power or other
signals on non-standard DB25 pins, so nothing else should connect.

Flow control works directly with the RP2040 UART: the computer's RTS goes
to the UART's CTS input (stop sending when the computer is full), and the
UART's RTS output drives the computer's CTS (the computer stops when our
buffer is full).

### 4.7 Other GPIO

| GPIO | Use |
|---|---|
| GP8 | MAX3238E INVALID (in) |
| GP9 | MAX3238E FORCEOFF (out) |
| GP10 | MAX3238E FORCEON (out) |
| GP11 | MAX3238E R1OUTB (in) |
| GP16–21 | LEDs, each with 1 kΩ to GND: PWR, TR (computer's DTR), CD, SD (to computer), RD (from computer), RI |
| GP29 | VBUS sense (ADC3) |
| rest | test pads where routing allows |

The LED names copy the classic external-modem front panel that
dmodem-android's "Modem lights" screen shows.

---

## 5. Firmware (outline)

- Pico SDK + TinyUSB. USB composite device:
  1. CDC-ACM (the serial port);
  2. the Pico SDK's "reset" vendor interface, so `picotool` or the app can
     reboot the board into BOOTSEL without pressing the button.
- **Line coding**: `SET_LINE_CODING` sets the UART's baud, data bits,
  parity and stop bits. RP2040 doesn't support mark/space parity or 1.5
  stop bits; reject or round those.
- **Break**: `SEND_BREAK` → UART break.
- **Lines from the USB host to the computer (default "null-modem" mapping)**:
  - host DTR → **DCD** and **DSR** to the computer
  - host RTS → allows **CTS** to the computer (ANDed with "buffer has room")
  - RI → idle until the vendor request below sets it
- **Lines from the computer to the USB host**: CDC `SERIAL_STATE`
  notifications. The computer's DTR is reported as **DSR**, and the board
  also sets DCD in the notification while INVALID shows a computer is
  connected. Errors (framing, parity, overrun) and received breaks are
  reported in the same message.
- **Vendor control requests (optional, v2)**:
  - `SET_MODEM_OUTPUTS(mask, value)`: drive DCD, DSR, RI and CTS
    separately, overriding the default mapping;
  - `GET_MODEM_INPUTS`: read DTR, RTS, INVALID directly, for hosts whose
    CDC driver ignores `SERIAL_STATE`;
  - `SET_LEDS`, `GET_INFO` (firmware version, board revision).
- Buffers: at least 4 KB each way in RAM.
- USB IDs: get a free PID from Raspberry Pi's RP2040/RP2350 PID program
  (under VID 0x2E8A), or from pid.codes for an open-source project.

---

## 6. dmodem-android compatibility

What works today with **no app change**:

- The app finds any CDC-ACM device (its USB filter matches the CDC
  interface class, and usb-serial-for-android 3.9.0 detects CDC-ACM by its
  interfaces, not by a VID/PID list).
- The app's default "its DTR is the modem's carrier" setting drives host
  DTR. With the default mapping in §5, that becomes the computer's DCD,
  exactly what a null-modem cable did for an ordinary adapter.
- The app sets RTS on, which lets the board assert CTS.
- Data, baud rate and format all pass through.

What **does not work today**, and the app change it needs:

- **Hanging up when the computer drops DTR.** The app reads the
  adapter's DSR for this, but usb-serial-for-android's CDC driver doesn't
  read input lines (it only supports setting RTS and DTR), so the read
  fails and the app skips it. Two ways to fix it:
  1. the app reads the CDC notification endpoint (`SERIAL_STATE`) itself;
  2. the app polls the board's `GET_MODEM_INPUTS` vendor request.

  (1) also helps every other CDC adapter, so it's the better choice.
- **RI (ringing) and separate DSR/DCD control** need `SET_MODEM_OUTPUTS`
  in the app, ideally as a "pico-serial board" option in its serial
  settings.

---

## 7. PCB and mechanical

- 2-layer, 1.6 mm FR-4 (1.6 mm also suits the straddle-mount DB25). Use
  4-layer only if routing around the QFN-56 gets too tight.
- Outline set by the DB25 hood. Target about **38 × 50 mm**; measure the
  chosen hood first. DB25 straddles one short edge, USB-C sits on the
  opposite edge and exits through the hood's cable opening.
- One solid GND pour on the bottom layer. Keep the MAX3238E's charge-pump
  loop short and away from the crystal.
- Crystal close to XIN/XOUT, guarded with GND, no traces underneath.
- Every IOVDD/DVDD capacitor within ~1 mm of its pin.
- Test pads: 3V3, 1V1, VBUS, GND, SWCLK, SWDIO, UART TX/RX (TTL side).
- Silkscreen: board name, revision, "DCE", DB25 pin 1 marker.
- Assembly: all SMT on the top side, plus the through-hole DB25 and the
  USB-C shell tabs, so it suits JLCPCB-style assembly.

---

## 8. Open questions

1. **DCE only, or a DTE build too?** A DTE build (male DB25, MAX3243E,
   3 TX / 5 RX) would make it a general USB-to-serial cable for talking to
   a real modem or other DCE kit. It needs a different transceiver and
   pinout. It could be a second layout or a build option on the same board.
2. **Exact DB25 straddle-mount part and hood**: these set the board
   outline, so choose them first.
3. **LED visibility**: clear hood, light pipes, or drop the LEDs?
4. **Galvanic isolation** (an isolator between USB and RS-232 grounds):
   left out for cost and size. Worth adding only if ground loops show up
   when a phone charger and the computer are both plugged into mains.
5. **Phone charging through the board** (USB-C power delivery passing
   through): left out because it adds a PD controller and a second USB-C
   connector.
6. **DE-9 variant** for AT-style PCs: same circuit, different connector and
   pinout.

---

## 9. Next steps

1. Pick the DB25 part and hood → fix the board outline.
2. Schematic in KiCad (RP2040 reference symbols and footprints exist).
3. Firmware on a Pico dev board plus a MAX3238E breakout first, to prove
   the signal mapping with dmodem-android before ordering PCBs.
4. App: read CDC `SERIAL_STATE` (fixes DTR hang-up for all CDC adapters).
5. Layout, order 5 boards, bring-up checklist (3V3, 1V1, crystal, BOOTSEL
   enumeration, UF2 flash, loopback at each baud rate, every DB25 line).
