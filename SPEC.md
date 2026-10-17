# pico-serial — hardware spec (draft 0.3)

A USB-C to DB25 RS-232 adapter built on a bare **RP2040** (the chip on the
Raspberry Pi Pico). There is no Pico module: the chip, flash, crystal,
regulator and every passive part are on this board.

It works in **both serial roles** through one female DB25:

| Role | Acts as | Plugs into | What you need |
|---|---|---|---|
| **DCE** (default) | a modem | a computer's male DB25 serial port (PC, Amiga, Atari ST…) | nothing, plugs straight in |
| **DTE** | a computer's serial port | a modem or other DCE kit with a female DB25 | a **male–male DB25 gender changer** (straight-through) |

A gender changer is wired straight through, so it only changes the plug
shape. The board switches roles electronically: it has one transceiver for
each role, both on the same DB25 pins, and only one is powered at a time.
The role is **auto-detected** from whatever is plugged in, or can be fixed
in firmware (§5.2).

The main DCE use is a phone running **dmodem-android** plugged into a
vintage computer: to the computer it looks like an external modem. DTE
mode turns the same board into an ordinary USB-to-serial adapter for
talking to real modems.

**Sourcing: one LCSC order.**
- JLC assembles every surface-mount part from LCSC stock.
- The DB25 connector and hood also come from LCSC (quantity 1 is fine)
  and are soldered and fitted by hand.
- DigiKey options are listed as backups.
- Stock was checked on **2026-10-17**. The parts picker page compares every
  option.

---

## 1. Requirements

| # | Requirement |
|---|---|
| R1 | USB-C device (UFP) port, USB 2.0 Full Speed, powered from VBUS only |
| R2 | Shows up as a standard **CDC-ACM** serial port (no driver on Android, Linux, macOS or Windows 10+) |
| R3 | One female DB25. Works as **DCE** or **DTE** (DTE through a male–male gender changer) |
| R4 | All 8 modem lines in both roles: TXD, RXD, RTS, CTS, DTR, DSR, DCD, RI, plus signal ground |
| R5 | Auto-detects the role from what is attached; can also be fixed in firmware |
| R6 | 300 baud to at least 230 400 baud; 5–8 data bits, none/even/odd parity, 1 or 2 stop bits, break |
| R7 | Hardware RTS/CTS flow control on the RS-232 side |
| R8 | Works with dmodem-android's USB bridge as it is today (§6) |
| R9 | Firmware update over the same USB-C cable (UF2) |
| R10 | ±15 kV ESD (HBM) on the RS-232 pins, ESD protection on USB |
| R11 | Fits inside a standard DB25 hood; USB-C exits through the cable opening |
| R12 | Draws under 100 mA, so a phone can power it on OTG |
| R13 | Every part normally stocked and buyable at qty 1 |
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

| Function | Chosen part (LCSC #) | Backups | Pkg | Price | LCSC stock (2026-10-17) |
|---|---|---|---|---|---|
| MCU | Raspberry Pi **RP2040** (C2040) | — | QFN-56 7×7 | $1.07 | 51k |
| Flash 16 MB | Winbond **W25Q128JVSIQ** (C97521) | W25Q128JVPIQ WSON-8, smaller (C190862); W25Q32JVSSIQ 4 MB (C179173) | SOIC-8 208 mil | $2.85 | 66k |
| Crystal | Abracon **ABM8-272-T3** 12 MHz (C20625731) | YXC X322512MSB4SI (C9002), 20 pF / 80 Ω: needs different caps | 3225 | $0.65 | 8.5k |
| LDO 3.3 V | Diodes **AP2112K-3.3TRG1** (C51118) | ME6211C33M5G-N (C82942); TLV75733PDBVR (C485517) | SOT-23-5 | $0.17 | 7.9k |
| DCE transceiver (5T/3R) | MaxLinear **SP3238EEA-L/TR** (C49669) | TI TRS3238EIPWR from DigiKey (TSSOP-28, different footprint), consigned to JLC | SSOP-28 208 mil | $1.22 | 4.4k (the only 5T/3R with real LCSC stock) |
| DTE transceiver (3T/5R) | TI **MAX3243EIDBR** (C353887) | SP3243EBEA-L/TR (C138704); MAX3243CDBR budget, no IEC rating (C414614) | SSOP-28 208 mil | $1.32 | 2.5k |
| USB-C 16-pin | HRO **TYPE-C-31-M-12** (C165948) | GCT USB4105-GF-A-120 (C5184243), different footprint | SMT | $0.19 | 367k |
| USB ESD | ST **USBLC6-2SC6** (C7519) | — | SOT-23-6 | $0.18 | 35k |
| VBUS TVS | Littelfuse **SMF5.0A** (C151296) | — | SOD-123FL | $0.13 | 22k |
| VBUS PTC | Littelfuse **1206L050YR** (C163512) | PTTC SMD1206P050TF (C2836286) | 1206 | $0.07 | 18k |
| DB25 female, solder cup (hand-soldered) | MH Connectors **MHDB25SS** (C19601171) | Amphenol L77SDB25S (C429843); Amphenol FCI DB25S064TLF from DigiKey | solder cup | $0.82 | 487 |
| Hood (hand-fitted) | MH Connectors **MHDPPK25-K** (C22372846), PA66, 4-40 thumbscrews, Ø11.5 mm exit | Amphenol FCI 86303639BLF (C5399712; 15 on LCSC, 3k on DigiKey); Adam Tech DB25-HD-PB-TS from DigiKey | — | $1.56 | 84 (buy spares) |
| Gender changer (DTE use) | any male–male DB25, straight-through | — | — | — | none on LCSC: buy elsewhere |

Rough parts cost per board, before the PCB and JLC's assembly fees:
**about $12–14** including passives.

**Parts avoided and why:**
- ADI MAX3238E: tiny stock at $8–14 each (LCSC and DigiKey).
- TI MAX3238E: 0 stock or obsolete. TI MAX3238IPWR (not "E"): 49 on LCSC.
- Amphenol L77SDB25SOL2: shows up in DB25 searches but is a vertical
  through-hole part, not solder cup.
- MH MHDTPPK25-DB25P-K: the hood kit comes with a male connector.
- ST ST3243EBTR: none until April 2027.
- Plain SP3243E (no "B"): only 120 kbps.
- W25Q16JVSSIQ: marked obsolete.
- Bourns MF-MSMF050-2: not recommended for new designs.
- True straddle-mount female DB25s: none stocked at qty 1 (§7.1).

LCSC asks for 5–10 of some cheap passives and protection parts; that adds
only cents. The connector and hoods have a minimum of 1.

Passives are 0402 (0603 where noted). X7R or better for anything on the
RS-232 charge pumps.

---

## 4. Circuit detail

### 4.1 Why RP2040 and not RP2350

- The RP2040's core regulator is a linear LDO that needs only two capacitors.
  The RP2350's is a switcher that needs a 3.3 µH inductor and careful layout,
  which is hard to fit in a DB25 hood.
- The job needs one UART, under 30 GPIO and full-speed USB.
- The RP2040 is $0.76 with 100k+ in stock.

### 4.2 RP2040 support parts (Raspberry Pi's minimal design)

| Pin(s) | Parts |
|---|---|
| IOVDD ×6 | 100 nF each, one per pin, placed close |
| USB_VDD | 100 nF |
| ADC_AVDD | 100 nF |
| VREG_VIN | 1 µF |
| VREG_VOUT → DVDD ×2 | 1 µF on VREG_VOUT, 100 nF on each DVDD pin |
| 3V3 rail | 10 µF at the LDO output, 10 µF at its input (0603) |
| XIN / XOUT | 12 MHz crystal, 15 pF to GND each side, **1 kΩ in series on XOUT** |
| USB_DP / USB_DM | 27 Ω series each, close to the RP2040 |
| QSPI_SS | to flash CS; to the BOOTSEL button through 1 kΩ (button to GND) |
| QSPI SD0–3, SCLK | straight to flash; 100 nF on flash VCC |
| RUN | RESET button to GND (RUN has an internal pull-up) |
| TESTEN | to GND |
| SWCLK / SWDIO | test pads (plus GND, 3V3) |
| Center pad | GND, paste in a grid, many vias |

Firmware: `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64` for crystal start-up
margin.

### 4.3 Power

- VBUS → 0.5 A PTC → SMF5.0A → AP2112K-3.3 → 3V3.
- VBUS → 100 kΩ / 100 kΩ → GP29 (ADC3), as on the Pico.
- Budget: RP2040 ~25 mA, the active transceiver ~1 mA plus 10–20 mA into
  its loads, the idle one ~1 µA in shutdown, LEDs (if fitted) ~10 mA.
  **Under 80 mA.**
- Keep both transceivers' VCC on all the time; only the shutdown pin puts
  one to sleep. Not every part's datasheet specifies off-state leakage with
  VCC unpowered.

### 4.4 USB-C

- **CC1 and CC2 each get their own 5.1 kΩ to GND.**
- Both D+ pads tied together, both D− pads tied together.
- Shell → GND through 1 MΩ ∥ 4.7 nF (0 Ω option to bond directly).
- SBU1/SBU2 not connected.
- D+/D−: short and matched. 90 Ω differential if the stack-up allows.

### 4.5 The two transceivers on one connector

Each of the 8 signal pins has **one driver and one receiver**: the driver
from one chip and the receiver from the other.

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

In DCE mode chip A is on and chip B is held in shutdown; in DTE mode it's
the other way round. Chip A's SHUTDOWN pin and chip B's FORCEOFF pin do the
same job. Both are pulled low at power-up, so both chips start off. Why this
works:

- **Idle drivers are high-impedance** in shutdown.
  - MAX3243E: ±25 µA max at ±12 V, even with VCC = 0.
  - SP3238E: its datasheet says only "High-Z" and gives no figure at
    ±12 V. **Check the leakage on the bench**, or fit the TI TRS3238E (±25 µA
    at ±12 V) from DigiKey.
- **Idle receivers' outputs are high-impedance.** The exceptions are the
  always-active outputs: SP3238E's STATUS and non-inverting R1OUT (pin 16),
  and MAX3243E's INVALID and ROUT2B. Those go to their own GPIOs.
- **Even with both chips on (a firmware bug), no two of our drivers share a
  pin.** The worst case is fighting the far end's driver, which RS-232
  drivers are short-circuit protected against.

Known trade-offs:

1. **Extra load.** The idle chip's receiver (3–7 kΩ, 5 kΩ typical, to GND)
   stays on every pin the active chip drives. A driver therefore sees
   ~2.5 kΩ typical and 1.5 kΩ worst case instead of 3–7 kΩ.
   - The datasheets guarantee ±5 V only into 3 kΩ. Into 2.5 kΩ, expect
     about ±5 V.
   - Receivers switch at 2.7 V or less, so there is still margin, but
     **check the swing on the bench** at 230 400 baud with all five DCE
     drivers loaded.
   - Larger charge-pump capacitors (below) help.
2. **Overvoltage on idle drivers.** Their outputs are rated to ±13.2 V
   absolute maximum. Normal ±12 V-rail RS-232 kit stays below that under
   load. A rare far-end driver running from ±15 V could exceed it. If that
   matters, fit bidirectional TVS diodes on pins 2–6, 8, 20, 22; there are
   no footprints for them by default.

**Capacitors, per chip:**
- C1 (flying cap): **0.22 µF**. Both SP3238E and TRS3238E are specified
  with 0.22 µF across a 3.3 V ±10 % supply.
- C2–C4 (V+/V− reservoirs): **1 µF**.
- VCC: **1 µF**. TI asks for ≥1 µF here to meet the IEC ESD rating.
- All X7R, ≥16 V on V+/V−. Keep each chip's pump loop short.

**ONLINE** (SP3238E) and **FORCEON** (MAX3243E) share one GPIO and go
high together. That turns automatic power-down off, so whichever chip its
shutdown pin enables stays on.

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
| GP20 | in | A STATUS (valid even in shutdown) |
| GP21 | in | B INVALID (valid even in shutdown) |
| GP22–27 | out (optional LEDs) | PWR, TR, CD, SD, RD, RI |
| GP28 | spare | test pad |
| GP29 | ADC3 | VBUS ÷ 2 |

UART0 can be routed to GP0–3 or GP12–15, so firmware gives the UART to
the active chip's pins and leaves the other set as inputs. No analog
switches are needed.

### 4.7 Buttons

BOOTSEL and RESET are on the board but can't be reached with the hood
closed. Normal firmware updates don't need them: the USB reset interface
(§5.1) reboots the board into the bootloader. The buttons are there for
bring-up and recovery with the hood off.

### 4.8 LEDs (optional)

There are six 0402 LED + 1 kΩ footprints on GP22–27, **not fitted by
default**. They're only useful with a clear hood or with the hood off.
They are the first thing to drop if layout space runs out. With no LEDs
fitted, GP22–27 become spare pins.

---

## 5. Firmware (outline)

### 5.1 USB

- Pico SDK + TinyUSB composite device: CDC-ACM + the SDK's reset
  interface (so `picotool` or the app can reboot it into BOOTSEL).
- `SET_LINE_CODING` → UART format. RP2040 doesn't do mark/space parity or
  1.5 stop bits; reject those.
- `SEND_BREAK` → UART break. Errors and breaks received → `SERIAL_STATE`.
- USB IDs: a free PID from Raspberry Pi's PID program (VID 0x2E8A) or from
  pid.codes.

### 5.2 Choosing the role

The mode setting is **auto** (default), **DCE** or **DTE**. It is stored in
flash and set with a vendor request (§5.5).

In auto mode, both chips start in shutdown. Each chip's status output keeps
working in shutdown and goes high when any of **its own receiver pins**
carries a valid RS-232 level. The board checks about every 250 ms:
- **GP20 (A STATUS) high** → something drives pins 2/4/20, i.e. a
  computer is attached → **turn on chip A (DCE)**.
- otherwise **GP21 (B INVALID) high** → something drives pins
  3/5/6/8/22, i.e. a modem is attached → **turn on chip B (DTE)**.
- otherwise nothing is attached → stay off.

GP19 (chip A's always-on, non-inverting R1OUT) shows activity on the
computer's TXD while chip A is off. Idle mark reads **low** on this pin,
the opposite of a normal inverting receiver output.

While a role is active, if the active chip's status output stays low for
about 1 s (the cable was pulled or the far end switched off), both chips go
back to shutdown and detection starts again. When the mode changes, the board
reports it in `GET_INFO` and sends a `SERIAL_STATE` with all lines off.

### 5.3 DCE mode: line mapping (matches a null-modem cable)

| From USB host | To computer |
|---|---|
| DTR | **DCD** and **DSR** |
| RTS | allows **CTS** (and only while our receive buffer has room) |
| vendor request | RI; DCD, DSR and CTS set separately |

| From computer | To USB host (`SERIAL_STATE`) |
|---|---|
| DTR (pin 20) | DSR bit |
| valid RS-232 levels present (A STATUS) | DCD bit |
| RTS (pin 4) | used locally: UART CTS, stops our sending |

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

An RS-232 input that isn't connected reads as "off". If the UART always
obeyed CTS, a 3-wire cable would never let any data out. So the UART's CTS
input is ignored until the far end has asserted that line at least once
since it was connected, and obeyed from then on. A vendor request can force
flow control on or off.

### 5.6 Vendor requests (v1)

- `SET_MODE(auto|dce|dte)`, saved to flash
- `GET_INFO`: firmware version, board revision, current mode, setting
- `SET_MODEM_OUTPUTS(mask, value)`: drive each output line directly
- `GET_MODEM_INPUTS`: read every input line plus both status outputs, for hosts
  whose CDC driver ignores `SERIAL_STATE`
- `SET_FLOW(auto|on|off)`
- `SET_LEDS` (if LEDs are fitted)

---

## 6. dmodem-android compatibility

Works today with **no app change** (DCE mode):
- The app finds any CDC-ACM device (its USB filter matches the CDC
  interface class, and usb-serial-for-android 3.9.0 detects CDC-ACM by its
  interfaces).
- The app's default "its DTR is the modem's carrier" setting becomes the
  computer's DCD, the same as a null-modem cable on an ordinary adapter.
- Data, speed and format pass through.

Needs app work:
- **Hanging up when the computer drops DTR.** usb-serial-for-android's CDC
  driver only sets RTS/DTR and never reads input lines, so the app's DSR
  read fails and it skips the hang-up. Fix in the app: read the CDC
  notification endpoint (`SERIAL_STATE`). That helps every CDC adapter, so
  it's better than relying on `GET_MODEM_INPUTS`.
- **Ring (RI), separate DSR/DCD, and choosing the mode:** a
  "pico-serial board" option in the app's serial settings, using the vendor
  requests.

DTE mode is for computers using the board to reach a real modem;
dmodem-android doesn't need it.

---

## 7. Mechanical

### 7.1 DB25 connector: solder cups straddling the board

No true straddle-mount female DB25 is stocked at qty 1. Amphenol's G17M
straddle series isn't stocked anywhere I checked, and DigiKey's "straddle
mount" category contains mislabelled vertical parts (e.g. Assmann A-DF 25
PP/Z).

Instead, use the standard industry practice: slide the **1.6 mm board
between the two rows of solder cups** and solder the cups to pads on both
sides. The 13-pin row goes on top and the 12-pin row underneath. NorComp's
own material says its solder-cup D-subs take a 0.063" board between the
rows. KiCad has a matching footprint: `DSUB-25_Female_EdgeMount_P2.77mm`.

The cup gap isn't standardised across brands, so **check a sample with a
1.6 mm scrap board** before ordering PCBs.

Pick a connector with **plain, unthreaded Ø3.05 flange holes**. The
MHDB25SS has Ø3.1 holes, confirmed from its drawing, and a 2.84 mm row
pitch. The hood's 4-40 thumbscrews pass through those holes
into the computer's or gender changer's standoffs. Clinch nuts or board
locks would get in the way.

### 7.2 Hood and board outline

The hood is the MH Connectors **MHDPPK25-K** (LCSC C22372846):
- PA66 plastic, outside 53.5 × 47.2 mm (40 mm across the body), 15 mm
  thick;
- comes with 4-40 thumbscrews;
- Ø11.5 mm cable exit, which leaves room for USB-C (8.9 × 3.2 mm);
- the kit includes only two strain-relief screws at the cable end, so the
  inside looks clear of screws. Not yet confirmed whether the hood snaps
  or hinges shut;
- no datasheet gives inside dimensions. With a 15 mm outside thickness,
  expect about 11–12 mm of inside height.

**Backups:**
- **Amphenol FCI 86303639BLF**: snaps together with nothing crossing the
  inside, best drawing, ~5 mm shorter. 15 on LCSC, 3k+ on DigiKey.
- **Adam Tech DB25-HD-PB-TS** (DigiKey): two body screws cross the inside,
  so the board needs ~Ø5 mm notches at x = ±15 mm.

**Target outline. Inferred, so confirm on a sample:**
- About **36–38 mm wide** at the connector end, narrowing toward the
  back.
- About **28–30 mm usable length** behind the connector's insulator. The
  front ~3 mm of board is inside the cup field.
- A **USB-C tongue about 10–11 mm wide** reaching into the cable exit.
  The receptacle's mouth sits flush with the opening. Leave out the
  strain relief.
- Height: about 5 mm free on each side of the board, so **components go
  on both sides**. JLC can assemble both sides, for an extra fee.

### 7.3 PCB

- **4 layers, 1.6 mm** (GND / power on the inner layers). The board is about
  36 × 29 mm with two 28-pin transceivers, a QFN-56 and around 50
  passives on both sides. Two layers might fit, but four makes the RP2040
  decoupling and USB routing much easier.
- Suggested placement:
  - top: RP2040, crystal, flash, USB-C, LDO, USB ESD;
  - bottom: both transceivers and their charge-pump capacitors, close to
    the DB25 cups they drive.
- Crystal next to XIN/XOUT, with ground around it and no traces
  underneath.
- Every IOVDD/DVDD capacitor within ~1 mm of its pin.
- Test pads: VBUS, 3V3, 1V1, GND, SWCLK, SWDIO, GP28, chip A and chip B
  TX/RX (logic side).
- Silkscreen: name, revision, "DCE / DTE auto", DB25 pin 1, "male–male
  gender changer for DTE".

---

## 8. Open questions

1. **Hood**: confirm the MHDPPK25-K's inside space and how it closes. If it
   doesn't fit, use the FCI 86303639BLF.
2. **SP3238E off-state leakage**: measure it with ±12 V on an idle driver.
   If it's poor, switch the DCE chip to the TI TRS3238E from DigiKey. That
   chip is TSSOP-28, so decide before layout, or lay out a TSSOP footprint
   and use the MaxLinear SP3238EEY (also TSSOP).
3. **RS-232 TVS footprints** for ±15 V-era kit: they cost board space;
   leave them out unless a target machine needs them.
4. **Galvanic isolation** and **phone charging through the board**: still
   out of scope.
5. **DE-9 variant**: same circuit, different connector, pinout and hood.

---

## 9. Next steps

1. **Buy samples from LCSC (~$7)**: 2 × MHDB25SS, 2 × MHDPPK25-K, 1 ×
   86303639BLF. Check that a 1.6 mm offcut fits between the cups. Measure
   each hood's inside length and width and any screw bosses. Then lock the
   outline.
2. **Bench prototype** on a Pico dev board with SP3238E and MAX3243E on
   SSOP-28 adapter boards, wired as §4.5. Prove the role detection, the
   driver swing with both chips on the bus, and both line mappings with
   dmodem-android (DCE) and a real modem (DTE).
3. Schematic and 4-layer layout in KiCad.
4. Firmware v1 (§5).
5. App: read CDC `SERIAL_STATE`; add the pico-serial options.
6. Order 5 boards. Bring-up: 3V3, 1V1, crystal, enumeration, UF2, loopback
   in both roles at each speed, every DB25 line in both roles.
