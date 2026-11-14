<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 D-Modem Authors -->
# pico-serial — hardware spec (draft 0.3)

pico-serial is a USB-C to DB25 RS-232 adapter. It uses a bare **RP2040**
(the chip on the Raspberry Pi Pico). The board does not use a Pico module.
The chip, flash, crystal, regulator and all passive parts are on this board.

The board operates in **both serial roles** through one female DB25:

| Role | Operates as | Connects to | Necessary items |
|---|---|---|---|
| **DCE** (default) | a modem | the male DB25 serial port of a computer (PC, Amiga, Atari ST…) | none. It connects directly |
| **DTE** | the serial port of a computer | a modem or other DCE equipment with a female DB25 | a **male–male DB25 gender changer** (straight-through) |

A gender changer has straight-through wires. Thus it changes only the shape
of the plug. The board changes its role electronically. It has one
transceiver for each role, and the two transceivers use the same DB25 pins.
Only one transceiver gets power at a time. The board finds the role
**automatically** from the equipment that you connect. You can also set the
role in the firmware (§5.2).

The main DCE use is a phone that runs **the D-Modem app**, connected to a
vintage computer. For the computer, the board is the same as an external
modem. In DTE mode, the same board is an ordinary USB-to-serial adapter
that communicates with real modems.

**Supply: one LCSC order.**
- JLC assembles all surface-mount parts from LCSC stock.
- The DB25 connector and the hood also come from LCSC (quantity 1 is
  sufficient). You solder and install them by hand.
- The table gives DigiKey options as backups.
- We examined the stock on **2026-10-17**. The parts picker page compares
  all options.

---

## 1. Requirements

| # | Requirement |
|---|---|
| R1 | USB-C device (UFP) port, USB 2.0 Full Speed. Power comes from VBUS only |
| R2 | The board shows as a standard **CDC-ACM** serial port (no driver is necessary on Android, Linux, macOS or Windows 10+) |
| R3 | One female DB25. It operates as **DCE** or **DTE** (DTE through a male–male gender changer) |
| R4 | All 8 modem lines in both roles: TXD, RXD, RTS, CTS, DTR, DSR, DCD, RI, and signal ground |
| R5 | The board finds the role automatically from the attached equipment. You can also set the role in the firmware |
| R6 | 300 baud to a minimum of 230 400 baud; 5–8 data bits, none/even/odd parity, 1 or 2 stop bits, break |
| R7 | Hardware RTS/CTS flow control on the RS-232 side |
| R8 | The board operates with the USB bridge of the D-Modem app as it is now (§6) |
| R9 | Firmware update through the same USB-C cable (UF2) |
| R10 | ±15 kV ESD (HBM) on the RS-232 pins, ESD protection on USB |
| R11 | The board fits in a standard DB25 hood. The USB-C goes out through the cable opening |
| R12 | The board uses less than 100 mA. Thus a phone can supply power to it on OTG |
| R13 | All parts are usually in stock, and you can buy a quantity of 1 |
| — | Status LEDs: **optional**, not a requirement (§4.8) |

---

## 2. Block diagram

```
 USB-C ──┬─ CC1/CC2: 5.1k Rd each ─ GND
 (UFP)   ├─ VBUS ─ PTC 0.5A ─ TVS ─┬─ LDO 3.3V ─── 3V3 rail
         │                         └─ VBUS sense (÷2) → GP29
         └─ D+/D- ─ USBLC6 ─ 27Ω ─ RP2040 USB

                     ┌── UART0 pins set A (GP0–3) + GP4–7 ──┐
 RP2040 ─────────────┤                                      ├── DB25 female
   │                 └── UART0 pins set B (GP12–15) + GP8–11┘     (same 8 pins)
   │                      │                        │
   │               SP3238E (5 TX / 3 RX)    MAX3243E (3 TX / 5 RX)
   │                 "chip A": DCE            "chip B": DTE
   │               SHUTDOWN/R1OUT/STATUS    FORCEOFF/INVALID → GPIO
   ├─ QSPI ── W25Q128JV flash
   ├─ 12 MHz crystal
   ├─ BOOTSEL + RESET buttons, SWD pads
   └─ optional LEDs
```

---

## 3. Bill of materials (main parts)

| Function | Selected part (LCSC #) | Backups | Pkg | Price | LCSC stock (2026-10-17) |
|---|---|---|---|---|---|
| MCU | Raspberry Pi **RP2040** (C2040) | — | QFN-56 7×7 | $1.07 | 51k |
| Flash 16 MB | Winbond **W25Q128JVSIQ** (C97521) | W25Q128JVPIQ WSON-8, smaller (C190862); W25Q32JVSSIQ 4 MB (C179173) | SOIC-8 208 mil | $2.85 | 66k |
| Crystal | Abracon **ABM8-272-T3** 12 MHz (C20625731) | YXC X322512MSB4SI (C9002), 20 pF / 80 Ω: different capacitors are necessary | 3225 | $0.65 | 8.5k |
| LDO 3.3 V | Diodes **AP2112K-3.3TRG1** (C51118) | ME6211C33M5G-N (C82942); TLV75733PDBVR (C485517) | SOT-23-5 | $0.17 | 7.9k |
| DCE transceiver (5T/3R) | MaxLinear **SP3238EEA-L/TR** (C49669) | TI TRS3238EIPWR from DigiKey (TSSOP-28, different footprint), consigned to JLC | SSOP-28 208 mil | $1.22 | 4.4k (the only 5T/3R with real LCSC stock) |
| DTE transceiver (3T/5R) | TI **MAX3243EIDBR** (C353887) | SP3243EBEA-L/TR (C138704); MAX3243CDBR budget, no IEC rating (C414614) | SSOP-28 208 mil | $1.32 | 2.5k |
| USB-C 16-pin | HRO **TYPE-C-31-M-12** (C165948) | GCT USB4105-GF-A-120 (C5184243), different footprint | SMT | $0.19 | 367k |
| USB ESD | ST **USBLC6-2SC6** (C7519) | — | SOT-23-6 | $0.18 | 35k |
| VBUS TVS | Littelfuse **SMF5.0A** (C151296) | — | SOD-123FL | $0.13 | 22k |
| VBUS PTC | Littelfuse **1206L050YR** (C163512) | PTTC SMD1206P050TF (C2836286) | 1206 | $0.07 | 18k |
| DB25 female, solder cup (hand-soldered) | MH Connectors **MHDB25SS** (C19601171) | Amphenol L77SDB25S (C429843); Amphenol FCI DB25S064TLF from DigiKey | solder cup | $0.82 | 487 |
| Hood (installed by hand) | MH Connectors **MHDPPK25-K** (C22372846), PA66, 4-40 thumbscrews, Ø11.5 mm exit | Amphenol FCI 86303639BLF (C5399712; 15 on LCSC, 3k on DigiKey); Adam Tech DB25-HD-PB-TS from DigiKey | — | $1.56 | 84 (buy spares) |
| Gender changer (DTE use) | any male–male DB25, straight-through | — | — | — | none on LCSC: buy from a different supplier |

The approximate cost of the parts for each board, before the PCB and the
assembly fees of JLC, is **about $12–14**. This includes the passives.

**Parts that we do not use, and the reasons:**
- ADI MAX3238E: very small stock at $8–14 each (LCSC and DigiKey).
- TI MAX3238E: 0 stock or obsolete. TI MAX3238IPWR (not "E"): 49 on LCSC.
- Amphenol L77SDB25SOL2: it shows in DB25 searches, but it is a vertical
  through-hole part, not a solder cup part.
- MH MHDTPPK25-DB25P-K: the hood kit has a male connector.
- ST ST3243EBTR: none until April 2027.
- Plain SP3243E (no "B"): only 120 kbps.
- W25Q16JVSSIQ: the supplier shows it as obsolete.
- Bourns MF-MSMF050-2: the supplier does not recommend it for new designs.
- True straddle-mount female DB25s: none in stock at qty 1 (§7.1).

For some cheap passives and protection parts, LCSC has a minimum order of
5–10. This adds only cents. The minimum for the connector and the hoods
is 1.

The passives are 0402 (0603 where the text says so). Use X7R or better for
all parts on the RS-232 charge pumps.

---

## 4. Circuit detail

### 4.1 Why RP2040 and not RP2350

- The core regulator of the RP2040 is a linear LDO. It needs only two
  capacitors. The regulator of the RP2350 is a switcher. It needs a 3.3 µH
  inductor and a careful layout. This is difficult to fit in a DB25 hood.
- The function needs one UART, fewer than 30 GPIO and full-speed USB.
- The RP2040 costs $0.76, and more than 100k are in stock.

### 4.2 RP2040 support parts (Raspberry Pi's minimal design)

| Pin(s) | Parts |
|---|---|
| IOVDD ×6 | 100 nF each, one for each pin, near the pin |
| USB_VDD | 100 nF |
| ADC_AVDD | 100 nF |
| VREG_VIN | 1 µF |
| VREG_VOUT → DVDD ×2 | 1 µF on VREG_VOUT, 100 nF on each DVDD pin |
| 3V3 rail | 10 µF at the LDO output, 10 µF at its input (0603) |
| XIN / XOUT | 12 MHz crystal, 15 pF to GND on each side, **1 kΩ in series on XOUT** |
| USB_DP / USB_DM | 27 Ω in series on each, near the RP2040 |
| QSPI_SS | to the flash CS; to the BOOTSEL button through 1 kΩ (button to GND) |
| QSPI SD0–3, SCLK | directly to the flash; 100 nF on the flash VCC |
| RUN | RESET button to GND, 10 kΩ pull-up to 3V3 (this adds noise margin to the internal pull-up) |
| TESTEN | to GND |
| SWCLK / SWDIO | test pads (and GND, 3V3) |
| Center pad | GND, paste in a grid, many vias |

Firmware: set `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64` to give the crystal
more start-up margin.

### 4.3 Power

- VBUS → 0.5 A PTC → SMF5.0A → AP2112K-3.3 → 3V3.
- VBUS → 100 kΩ / 100 kΩ → GP29 (ADC3), as on the Pico.
- Budget: RP2040 ~25 mA. The active transceiver ~1 mA, plus 10–20 mA into
  its loads. The idle transceiver ~1 µA in shutdown. LEDs (if installed)
  ~10 mA. **Less than 80 mA.**
- Keep power on the VCC of both transceivers at all times. Use only the
  shutdown pin to put a transceiver in sleep mode. Not all datasheets
  give the off-state leakage when VCC has no power.

### 4.4 USB-C

- **CC1 and CC2 each have a separate 5.1 kΩ to GND.**
- The two D+ pads connect together, and the two D− pads connect together.
- Shell → GND through 1 MΩ ∥ 4.7 nF (a 0 Ω option bonds it directly).
- SBU1/SBU2 are not connected.
- D+/D−: short and matched. 90 Ω differential if the stack-up permits it.

### 4.5 The two transceivers on one connector

**Why two chips (decided):** the pins of an RS-232 chip have fixed
directions. In DCE mode, pins 3/5/6/8/22 are outputs and pins 2/4/20 are
inputs. DTE mode is the exact opposite. A gender changer is
straight-through, and it cannot change the directions. Thus each signal pin
must have both a driver and a receiver: a 5T/3R chip and a 3T/5R chip.
We did not use these alternatives:
- One chip and a mechanical crossover switch: the number of drivers and
  receivers is still incorrect. The switch needs 8 poles, and you cannot
  get access to it in a closed hood.
- One 8T/8R chip: we found none in usual stock.
- ±12 V analog switches: cost and space.
- One DCE chip and a null-modem adapter for DTE use: a standard null modem
  merges DCD and DSR, and usually removes RI. Thus DTE mode loses
  the separate carrier and ring signal sensors.

Each of the 8 signal pins has **one driver and one receiver**. The driver
is on one chip and the receiver is on the other chip.

| DB25 pin | Signal | Chip A, SP3238E (DCE) | Chip B, MAX3243E (DTE) |
|---|---|---|---|
| 2 | TXD | R1IN (in) | DOUT1 (out) |
| 3 | RXD | T1OUT (out) | RIN1 (in) |
| 4 | RTS | R2IN (in) | DOUT2 (out) |
| 5 | CTS | T2OUT (out) | RIN2 (in) |
| 6 | DSR | T3OUT (out) | RIN3 (in) |
| 8 | DCD | T4OUT (out) | RIN4 (in) |
| 20 | DTR | R3IN (in) | DOUT3 (out) |
| 22 | RI | T5OUT (out) | RIN5 (in) |
| 7 | Signal ground | GND | GND |
| 1 | Frame ground | shell, 0 Ω to GND (option to leave open) | |
| others | **not connected** | | |

In DCE mode, chip A is on and chip B stays in shutdown. In DTE mode, chip B
is on and chip A stays in shutdown. The SHUTDOWN pin of chip A and the
FORCEOFF pin of chip B have the same function. Pull-down resistors keep the
two pins low at power-up, thus the two chips start in the off condition.
These are the reasons that this design operates correctly:

- **Idle drivers are high-impedance** in shutdown.
  - MAX3243E: ±25 µA max at ±12 V, also with VCC = 0.
  - SP3238E: its datasheet gives only "High-Z" and gives no value at
    ±12 V. **Measure the leakage on the bench**, or install the TI TRS3238E
    (±25 µA at ±12 V) from DigiKey.
- **The outputs of idle receivers are high-impedance.** The exceptions are
  the outputs that are always active: the STATUS and non-inverting R1OUT
  (pin 16) of the SP3238E, and the INVALID and ROUT2B of the MAX3243E.
  These outputs go to their own GPIOs.
- **If the two chips are on at the same time (a firmware bug), no two of
  our drivers share a pin.** The worst case is a conflict with the driver
  at the far end. RS-232 drivers have protection against this short
  circuit.

Known trade-offs:

1. **More load.** The receiver of the idle chip (3–7 kΩ, 5 kΩ typical, to
   GND) stays on each pin that the active chip drives. Thus a driver sees
   ~2.5 kΩ typical and 1.5 kΩ worst case, not 3–7 kΩ.
   - The datasheets guarantee ±5 V only into 3 kΩ. Into 2.5 kΩ, expect
     about ±5 V.
   - Receivers switch at 2.7 V or less, thus there is still margin. But
     **measure the swing on the bench** at 230 400 baud with load on all
     five DCE drivers.
   - Larger charge-pump capacitors (below) help.
2. **Overvoltage on idle drivers.** The absolute maximum rating of their
   outputs is ±13.2 V. Usual RS-232 equipment with ±12 V rails stays below
   that value under load. A rare driver at the far end that operates from
   ±15 V can be more than that value. There are no footprints for TVS
   diodes by default.

   > **CAUTION: IF A FAR-END DRIVER CAN OPERATE FROM ±15 V, INSTALL
   > BIDIRECTIONAL TVS DIODES ON PINS 2–6, 8, 20, 22.** A voltage of more
   > than ±13.2 V on an idle driver output can cause damage to the
   > transceiver.

**Capacitors, for each chip:**
- C1 (flying capacitor): **0.22 µF**. The specifications of the SP3238E and
  the TRS3238E use 0.22 µF with a 3.3 V ±10 % supply.
- C2–C4 (V+/V− reservoirs): **1 µF**.
- VCC: **1 µF**. TI specifies ≥1 µF here to meet the IEC ESD rating.
- All X7R, ≥16 V on V+/V−. Keep the pump loop of each chip short.

**ONLINE** (SP3238E) and **FORCEON** (MAX3243E) share one GPIO. They go
high together. This disables the automatic power-down. Thus the chip that
its shutdown pin enables stays on.

### 4.6 GPIO map

| GPIO | Function | To |
|---|---|---|
| GP0 | UART0 TX (set A) | A T1IN → pin 3 RXD |
| GP1 | UART0 RX (set A) | A R1OUT (pin 21) ← pin 2 TXD |
| GP2 | UART0 CTS (set A) | A R2OUT ← pin 4 RTS |
| GP3 | UART0 RTS (set A) | A T2IN → pin 5 CTS |
| GP4 | out | A T3IN → pin 6 DSR |
| GP5 | out | A T4IN → pin 8 DCD |
| GP6 | out | A T5IN → pin 22 RI |
| GP7 | in | A R3OUT ← pin 20 DTR |
| GP8 | out | B DIN3 → pin 20 DTR |
| GP9 | in | B ROUT3 ← pin 6 DSR |
| GP10 | in | B ROUT4 ← pin 8 DCD |
| GP11 | in | B ROUT5 ← pin 22 RI |
| GP12 | UART0 TX (set B) | B DIN1 → pin 2 TXD |
| GP13 | UART0 RX (set B) | B ROUT1 ← pin 3 RXD |
| GP14 | UART0 CTS (set B) | B ROUT2 ← pin 5 CTS |
| GP15 | UART0 RTS (set B) | B DIN2 → pin 4 RTS |
| GP16 | out, 100 k pull-down | A SHUTDOWN |
| GP17 | out, 100 k pull-down | B FORCEOFF |
| GP18 | out, 100 k pull-down | A ONLINE + B FORCEON |
| GP19 | in | A R1OUT pin 16, non-inverting, active in shutdown (pin 2 activity) |
| GP20 | in | A STATUS (valid also in shutdown) |
| GP21 | in | B INVALID (valid also in shutdown) |
| GP22–27 | out (optional LEDs) | PWR, TR, CD, SD, RD, RI |
| GP28 | spare | test pad |
| GP29 | ADC3 | VBUS ÷ 2 |

You can route UART0 to GP0–3 or to GP12–15. Thus the firmware gives the
UART to the pins of the active chip and sets the other pins as inputs.
Analog switches are not necessary.

### 4.7 Buttons

BOOTSEL and RESET are on the board, but you cannot get access to them when
the hood is closed. Usual firmware updates do not need them: the USB reset
interface (§5.1) reboots the board into the bootloader. The buttons are for
bring-up and recovery when the hood is removed.

### 4.8 LEDs (optional)

There are six footprints for a 0402 LED and a 1 kΩ resistor on GP22–27.
They are **not installed by default**. They are useful only with a clear
hood or when the hood is removed. If there is not sufficient space for the
layout, remove these footprints first. If no LEDs are installed, GP22–27
are spare pins.

---

## 5. Firmware (outline)

> The firmware is in [`firmware/`](firmware/README.md) (v0.1, wired board).
> These details are decided there:
> - Detection takes a sample each 50 ms and needs 3 samples that agree.
> - The vendor protocol is in [firmware/PROTOCOL.md](firmware/PROTOCOL.md).
> - There is no 1200-baud reboot, because 1200 baud is a real modem speed.
> - The firmware uses TinyUSB 0.21 for SERIAL_STATE.
> - The firmware learns the baud rate from the text that the computer
>   types (§5.7).

### 5.1 USB

- Pico SDK + TinyUSB composite device: CDC-ACM and the reset interface of
  the SDK (thus `picotool` or the app can reboot the board into BOOTSEL).
- `SET_LINE_CODING` → UART format. The RP2040 cannot do mark/space parity
  or 1.5 stop bits. Reject these formats.
- `SEND_BREAK` → UART break. Received errors and breaks → `SERIAL_STATE`.
- USB IDs: a free PID from the PID program of Raspberry Pi (VID 0x2E8A) or
  from pid.codes.

### 5.2 Choosing the role

The mode setting is **auto** (default), **DCE** or **DTE**. The firmware
keeps it in flash. You set it with a vendor request (§5.5).

In auto mode, the two chips start in shutdown. The status output of each
chip continues to operate in shutdown. It goes high when one of **its own
receiver pins** has a valid RS-232 level. The board does a check about each
250 ms:
- **GP20 (A STATUS) high** → a device drives pins 2/4/20, that is, a
  computer is attached → **turn on chip A (DCE)**.
- If not, **GP21 (B INVALID) high** → a device drives pins 3/5/6/8/22,
  that is, a modem is attached → **turn on chip B (DTE)**.
- If not, no device is attached → the two chips stay off.

GP19 (the always-on, non-inverting R1OUT of chip A) shows activity on the
TXD of the computer while chip A is off. Idle mark is **low** on this pin.
This is the opposite of a usual inverting receiver output.

While a role is active, the status output of the active chip can stay low
for about 1 s (because the cable was disconnected or the far end was
switched off). If this occurs, the two chips go back to shutdown and
detection starts again. When the mode changes, the board shows the new mode
in `GET_INFO`. It also sends a `SERIAL_STATE` with all lines off.

### 5.3 DCE mode: line mapping (matches a null-modem cable)

| From USB host | To computer |
|---|---|
| DTR | **DCD** and **DSR** |
| RTS | permits **CTS** (and only while our receive buffer has space) |
| vendor request | RI; DCD, DSR and CTS set separately |

| From computer | To USB host (`SERIAL_STATE`) |
|---|---|
| DTR (pin 20) | DSR bit |
| valid RS-232 levels are present (A STATUS) | DCD bit |
| RTS (pin 4) | used locally: UART CTS, stops the data that we send |

### 5.4 DTE mode: line mapping (an ordinary USB serial adapter)

| From USB host | To modem |
|---|---|
| DTR | DTR (pin 20) |
| RTS | RTS (pin 4), held off while our receive buffer is full |

| From modem | To USB host (`SERIAL_STATE`) |
|---|---|
| DCD (8) | DCD bit |
| DSR (6) | DSR bit |
| RI (22) | RI bit |
| CTS (5) | used locally: UART CTS |

### 5.5 Flow control

An RS-232 input that is not connected reads as "off". If the UART always
obeyed CTS, no data could go out through a 3-wire cable. Thus the UART
ignores its CTS input until the far end asserts that line one time after
the connection. After that, the UART obeys CTS. A vendor request can
set flow control to on or off.

### 5.6 Vendor requests (v1)

- `SET_MODE(auto|dce|dte)`, saved to flash
- `GET_INFO`: firmware version, board revision, current mode, setting
- `SET_MODEM_OUTPUTS(mask, value)`: drive each output line directly
- `GET_MODEM_INPUTS`: read all input lines and the two status outputs. This
  is for hosts with a CDC driver that ignores `SERIAL_STATE`
- `SET_FLOW(auto|on|off)`
- `SET_LEDS` (if LEDs are installed)

### 5.7 Baud rate

As DCE, the board **learns the baud rate of the computer** from the text
that the computer types, as a Hayes modem does. The board ignores the
`SET_LINE_CODING` speed of the host. Thus the phone app does not have to
know the speed of the terminal.

- **Method:** a PIO state machine measures the time of each low and high
  period on the RX pin (GP1 as DCE, GP13 as DTE). The UART keeps the pin
  at the same time. The firmware tries all standard rates from 110 to
  230 400 baud and selects the rate that agrees with the timings
  (firmware/README.md, "Automatic baud rate").
- **No data loss:** the firmware decodes the characters that it learned
  from (for example "AT") from the timings and sends them on. Two
  characters are sufficient. Thus one push of Enter (CR LF) is sufficient,
  or two pushes of Enter on a CR-only terminal.
- **Learn again:** after 3 framing errors in 2 s (the computer changed the
  speed), after a vendor request, or after a new peer connects to the DB25.
- **Data format** (data bits, parity, stop bits) still comes from the host.
- **As DTE**, the board uses the speed of the host by default: a modem uses
  the speed of the data that it receives. A vendor request can enable the
  learning function for DTE too (for a device that sends first, such as a
  GPS). It can also disable the learning function for the two roles.

---

## 6. D-Modem app compatibility

The board operates now with **no change to the app** (DCE mode):
- The app finds all CDC-ACM devices. Its USB filter matches the CDC
  interface class, and usb-serial-for-android 3.9.0 finds CDC-ACM devices
  from their interfaces.
- The default setting of the app, "its DTR is the modem's carrier", becomes
  the DCD of the computer. This is the same as a null-modem cable on an
  ordinary adapter.
- Data and format go through. The board learns the speed from the computer
  (§5.7). Thus the speed setting of the app does not have to match the speed
  of the terminal.

These functions need work in the app:
- **Hang up when the computer drops DTR.** The CDC driver of
  usb-serial-for-android only sets RTS/DTR and does not read input lines.
  Thus the DSR read of the app fails and the app does not hang up. The
  correction in the app: read the CDC notification endpoint
  (`SERIAL_STATE`). This helps all CDC adapters, thus it is better than
  `GET_MODEM_INPUTS`.
- **Ring signal (RI), separate DSR/DCD, and selection of the mode:** a
  "pico-serial board" option in the serial settings of the app, which uses
  the vendor requests.

DTE mode is for computers that use the board to get access to a real modem.
The D-Modem app does not need DTE mode.

---

## 7. Mechanical

### 7.1 DB25 connector: solder cups straddling the board

No true straddle-mount female DB25 is in stock at qty 1. The G17M
straddle series of Amphenol is not in stock at the suppliers that I
examined. The "straddle mount" category of DigiKey has vertical parts with
incorrect labels (for example, Assmann A-DF 25 PP/Z).

Thus use the standard practice of the industry. Put the **1.6 mm board
between the two rows of solder cups**. Solder the cups to pads on the two
sides. The 13-pin row goes on the top and the 12-pin row goes below.
The documents of NorComp say that its solder-cup D-subs accept a 0.063"
board between the rows. KiCad has a matching footprint:
`DSUB-25_Female_EdgeMount_P2.77mm`.

The cup gap is not the same for all brands. Thus **examine a sample with a
1.6 mm scrap board** before you order PCBs.

Select a connector with **plain, unthreaded Ø3.05 flange holes**. The
MHDB25SS has Ø3.1 holes (its drawing confirms this) and a 2.84 mm row
pitch. The 4-40 thumbscrews of the hood go through these holes into the
standoffs of the computer or of the gender changer. Clinch nuts or board
locks would block them.

### 7.2 Hood and board outline

The hood is the MH Connectors **MHDPPK25-K** (LCSC C22372846):
- PA66 plastic, outside 53.5 × 47.2 mm (40 mm across the body), 15 mm
  thick.
- It comes with 4-40 thumbscrews.
- Ø11.5 mm cable exit, which has sufficient space for USB-C
  (8.9 × 3.2 mm).
- The kit has only two strain-relief screws at the cable end. Thus there
  are probably no screws in the inside. It is not yet confirmed if the hood
  closes with a snap or with a hinge.
- No datasheet gives the inside dimensions. With a 15 mm outside thickness,
  expect about 11–12 mm of inside height.

**Backups:**
- **Amphenol FCI 86303639BLF**: the parts snap together, and no part goes
  across the inside. It has the best drawing. It is ~5 mm shorter. 15 on
  LCSC, 3k+ on DigiKey.
- **Adam Tech DB25-HD-PB-TS** (DigiKey): two body screws go across the
  inside. Thus the board needs ~Ø5 mm notches at x = ±15 mm.

**Target outline. We inferred these values, thus confirm them on a sample:**
- About **36–38 mm wide** at the connector end. The board becomes narrower
  toward the back.
- About **28–30 mm usable length** behind the insulator of the connector.
  The front ~3 mm of the board is in the cup field.
- A **USB-C tongue about 10–11 mm wide** that goes into the cable exit.
  The mouth of the receptacle is flush with the opening. Do not install the
  strain relief.
- Height: about 5 mm free on each side of the board. Thus **components go
  on the two sides**. JLC can assemble the two sides, for an extra fee.

### 7.3 PCB

> The layout is in [`hardware/kicad/`](hardware/README.md#the-pcb) (rev A,
> made by `build_pcb.py`): about 37 × 38 mm with the USB-C tongue,
> 4 layers, routed, DRC clean and it matches the schematic. These are the
> differences from the notes below:
> - **Decoupling** is on the bottom, about 2 mm from the pins of the RP2040.
>   It connects through the planes, not within 1 mm. Near placement did not
>   leave space for the pins of the chip to escape. Each power pin has its
>   own via below the package.
> - **Layers:** In1 is a solid GND plane. In2 has signals and also the
>   +3V3 pour.
> - **Test pads** are on the bottom, on a 2.6 mm grid.
> - **No traces below the crystal** on the top layer (keep-outs between its
>   pads). Below that, the In1 GND plane shields it.

- **4 layers, 1.6 mm** (GND / power on the inner layers). The board is about
  36 × 29 mm, with two 28-pin transceivers, a QFN-56 and approximately 50
  passives on the two sides. Two layers can possibly be sufficient. But
  four layers make the RP2040 decoupling and the USB routing much easier.
- Recommended placement:
  - Top: RP2040, crystal, flash, USB-C, LDO, USB ESD.
  - Bottom: the two transceivers and their charge-pump capacitors, near
    the DB25 cups that they drive.
- Put the crystal next to XIN/XOUT, with ground around it and no traces
  below it.
- Put each IOVDD/DVDD capacitor within ~1 mm of its pin.
- Test pads: VBUS, 3V3, 1V1, GND, SWCLK, SWDIO, GP28, chip A and chip B
  TX/RX (logic side).
- Silkscreen: name, revision, "DCE / DTE auto", DB25 pin 1, "male–male
  gender changer for DTE".
- The silkscreen must also have the licence notice: `CERN-OHL-S-2.0` and
  `github.com/D-Modem/hardware`. This is the Source Location that the
  licence requires (LICENSING.md). The hood of the W board also has it
  debossed on the bottom.

---

## 7.4 Wireless variant

[SPEC-W.md](SPEC-W.md) specifies a Bluetooth version (BLE for iPhone,
classic Bluetooth serial for Android). Its antenna needs more space than
a DB25 hood has. Thus it is a separate, slightly larger board.

---

## 8. Open questions

1. **Hood**: confirm the inside space of the MHDPPK25-K and how it closes.
   If it does not fit, use the FCI 86303639BLF.
2. **SP3238E off-state leakage**: measure it with ±12 V on an idle driver.
   If the value is bad, change the DCE chip to the TI TRS3238E from DigiKey.
   That chip is TSSOP-28. Thus decide before the layout, or make the layout
   with a TSSOP footprint and use the MaxLinear SP3238EEY (also TSSOP).
3. **RS-232 TVS footprints** for equipment of the ±15 V era: they use board
   space. Do not include them unless a target machine needs them.
4. **Galvanic isolation** and **phone charging through the board**: these
   are still out of scope.
5. **DE-9 variant**: the same circuit, a different connector, pinout and
   hood.

---

## 9. Next steps

1. **Buy samples from LCSC (~$7)**: 2 × MHDB25SS, 2 × MHDPPK25-K, 1 ×
   86303639BLF. Make sure that a 1.6 mm offcut fits between the cups.
   Measure the inside length and width of each hood and all screw bosses.
   Then make the outline final.
2. **Bench prototype** on a Pico dev board with SP3238E and MAX3243E on
   SSOP-28 adapter boards, with the wires as in §4.5. Prove the role
   detection, the driver swing with the two chips on the bus, and the two
   line mappings with the D-Modem app (DCE) and a real modem (DTE).
3. Netlist, schematic and PCB: done, `hardware/` (generated and examined).
   Next: a person does a review of the layout, and you confirm the outline
   with the sample hood (step 1).
4. Firmware v1 (§5).
5. App: read CDC `SERIAL_STATE`; add the pico-serial options.
6. Order 5 boards. Bring-up: 3V3, 1V1, crystal, enumeration, UF2, loopback
   in the two roles at each speed, each DB25 line in the two roles.
