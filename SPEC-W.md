<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 D-Modem Authors -->
# pico-serial W — wireless variant (draft 0.1)

pico-serial W is pico-serial with a Bluetooth radio. Thus a phone can be the
modem without a cable:

- **iPhone and iPad through Bluetooth Low Energy (BLE).** Apple
  certification (MFi) is not necessary. All iOS apps can communicate with a
  BLE device. You can use existing "BLE UART" apps immediately for tests.
- **Android through classic Bluetooth serial (SPP).** The D-Modem app
  already connects to the serial port of a paired device. Thus this
  operates with **no change to the app**.
- **USB-C operates as before**, as a wired serial port and for power.

All of [SPEC.md](SPEC.md) still applies: the RP2040, the two RS-232 chips,
the DCE/DTE auto-detect and the DB25 pinout. This document gives only the
items that the radio adds or changes.

---

## 1. Radio: Raspberry Pi Radio Module 2 (RM2)

| | |
|---|---|
| Part | Raspberry Pi **RM2**, order code SC1169 (marked RMC20452T) |
| Chip | Infineon CYW43439, the same radio as Pico W / Pico 2 W |
| Radio | Bluetooth 5.2, **classic and LE**; Wi-Fi 4, 2.4 GHz (not used at first) |
| Size | 14.5 × 16.5 × 2.55 mm, 21 castellated pads, 1.5 mm pitch |
| Antenna | On the module (inverted-F) |
| Supply | Vin 3.0–4.8 V (less than 3.2 V decreases the RF performance); VDDIO 3.3 V |
| Current | Bluetooth only: tens of mA. Wi-Fi transmit: up to 271 mA |
| Approvals | Modular approval in the US (FCC ID 2ABCB-RMC20452T), Canada, EU, UK |
| Lifetime | In production until a minimum of January 2036 |
| Price, stock | $4 RRP; Mouser ~3,900 in stock; DigiKey 2648-SC1169CT-ND. **Not in stock at LCSC** |
| Software | Pico SDK (`pico_cyw43_arch`, BTstack), the same as Pico W |

**Why RM2:**
- It is the module of Raspberry Pi, made for this chip family.
- The Pico SDK supports it without changes when its wires are the same as
  on a Pico W.
- It has the radio approvals.
- Its castellated pads are easy to solder by hand.

**Alternatives that we examined:**
- A bare CYW43439: it needs an RF design and its own approvals.
- An nRF52 or ESP32-C3 module as a second processor: two firmwares and a
  UART link between them.
- An nRF52840 in place of the RP2040: then it is not a Pico design.

**Supply:** RM2 is not in LCSC/JLC stock. Buy it from DigiKey or Mouser.
Then solder it by hand, or send it to JLC as a consigned part.

---

## 2. The antenna decides the form factor

The datasheet of the RM2 (Figure 6) specifies an **RF keep-out of
35.5 × 18.5 mm** around the antenna end of the module. This area must have
no metal of **any** type:
- 13.5 mm beyond the top edge of the module.
- 10.5 mm on each side of the module.
- The top ~5 mm of the module itself.

The full board in a DB25 hood is only about 36 × 29 mm. It is also between
the metal shell of the DB25 and the USB-C connector and cable. **You cannot
get the keep-out in a hood.**

Options:

| | Form | Antenna | Notes |
|---|---|---|---|
| **A (selected)** | A longer **3D-printed hood**, 61 × 56 × 15 mm (§2.2). The DB25 female stays on the front (the same solder-cup mount), and the USB-C is at the back | RM2 at the rear corner, with the antenna end flush with the board edge. The keep-out is in the air in the hood | This agrees with the datasheet. There is space for the LEDs and a pairing pinhole on the top |
| B | Keep the hood size. RM2 at the back, and the antenna extends beyond the board | The keep-out is not correct: the USB-C and the cable are adjacent | Shorter range, and the modular approval does not apply |
| C | Hood board for the wired version, and a separate small radio board on a short cable | Satisfactory | Two boards and a cable between them. Not tidy |

### 2.1 Enclosure: a 3D-printed hood (decided)

The W board has its own **3D-printed DB25 hood**. Its size agrees with the
RM2 and its keep-out. Print services make these hoods in quantity. Because
we set the inside dimensions, it is not necessary to measure a hood from a
third party first.

### 2.2 The printed hood

Model: `hardware/enclosure/w_hood.py` (CadQuery). The script writes the
print files, the board outline and the placement rules. It also does a
check of the fit.

| | |
|---|---|
| Outside | **60.6 × 56 × 15 mm**, and the D-shell of the DB25 is 5.8 mm more to the front. A standard hood is about 42 × 54 × 16 |
| Parts | Two halves, divided at the mid-plane of the board: `hood-top` and `hood-bottom` (STEP and STL) |
| DB25 | The flange (53.02 × 12.55 × 0.8) is in a slot, as in all hoods. The front lip has clearance from the mating shell (43 × 11.2) and from the jack posts of the computer (Ø7.2) |
| Attachment to the computer | 2 × **Keystone 2400** knurled thumbscrews, 4-40 × 5/16″, DigiKey 36-2400-ND. They are captive between the halves, in finger pockets at the front corners. They engage the jack posts by **4.5 mm** |
| Closing | 3 × M2 × 10 pan-head self-tapping screws from below, **through the board**, into pilot holes in the top half. They also hold the board. There are also 2 alignment pins at the rear corners |
| Board | 42.2 × 50.6 mm main area, and an 11 mm wide USB-C tongue to the back wall. The board becomes narrower (±18 mm) at the front for the thumbscrew pockets. The outline is in `out/board-outline.dxf` |
| RM2 | Top side, rear left, with the antenna end flush with the board edge at x = 44. The keep-out (35.5 × 18.5) includes the last 5 mm of the module and 13.5 mm of air in the hood |
| USB-C | Rear right, outside the keep-out. The opening in the back wall (13.2 × 7.6) lets the overmoulds of usual plugs go fully in |
| Top face | Five LED windows with light tubes (BT, TR, CD, RD, SD), a **PAIR** pinhole above the pairing button, and debossed labels |
| Inside height | 4.8 mm for parts on each side of the board |

**The checks that the script does before it writes a file:**
- The halves do not overlap.
- The hood does not touch these items: the PCB, the DB25 and its solder
  cups, the RM2, the USB-C, the LEDs, the button, the thumbscrews, the
  screws.
- No metal (DB25, USB-C, screws) is in the RF keep-out.
- The thumbscrews engage by a minimum of 4 mm.

The script finds faults that we put in for a test (a light tube on an LED,
a screw in the keep-out).

**Printing:**
- **MJF nylon PA12** (for example, JLC3DP, black or grey): it is strong
  and it holds self-tapping screws well.
- SLA resin also operates, but it is more brittle at the screw bosses.
- Do not use materials with carbon fill or metal, or metallic paint. They
  change the tuning of the antenna.
- The design keeps walls ≥ 1.0 mm, holes ≥ 1.5 mm and text 2.4 mm tall
  and 0.5 mm deep. These values are in the usual design rules of MJF.

**Assembly:**
1. Solder the cups of the DB25 to the board edge.
2. Put the board into the bottom half, with the flange in its slot.
3. Put in the thumbscrews.
4. Install the top half.
5. Install the 3 screws.

This gives a second board, **pico-serial W**. The wired version does not
change. The same netlist script makes the two boards, with a variant
switch. Only the radio, the outline and some GPIOs are different.

> With a perfect keep-out, the radio is still adjacent to the metal case
> and the cables of a computer. Expect a reliable range across a desk or a
> room, but not through walls. This is sufficient for this function,
> because the phone is usually adjacent to the computer.

---

## 3. Circuit changes

### 3.1 RM2 wiring: identical to Pico W

With this wiring, the Pico W pin defaults of the Pico SDK operate with no
configuration.

| RM2 pin | Function | Connects to |
|---|---|---|
| 1, 4, 7, 11, 15, 21 | Ground | GND (many vias below the module) |
| 2, 19, 20 | No connect | — |
| 3 | gSPI SCLK | **GP29** |
| 5 | gSPI data in | **GP24** |
| 6 | gSPI data out | GP24 through **470 Ω** |
| 10 | nIRQ (host wake) | GP24 through **10 kΩ** |
| 9 | gSPI CS | **GP25** |
| 12, 13 | Wi-Fi on, Bluetooth on | **GP23** (both) |
| 14 | VDDIO | +3V3 |
| 16 | Vin | +3V3, with 10 µF + 100 nF at the pin |
| 8, 17, 18 | Module GPIO 0/1/2 | see 3.3 |

Supply: the AP2112K-3.3 (600 mA) is still sufficient. Add 22 µF bulk
adjacent to the RM2, for Wi-Fi transmit peaks if you use Wi-Fi in the
future.

### 3.2 GPIOs that move

RM2 needs GP23, GP24, GP25 and GP29. The hood board uses these GPIOs for
three optional LEDs and the VBUS sense. On the W board:

| Was (hood board) | Becomes (W board) |
|---|---|
| GP23–25: LED_TR, LED_CD, LED_SD | RM2 power, data, chip select |
| GP29: VBUS ÷2 sense | RM2 SPI clock |
| GP22, 26, 27: LED_PWR, LED_RD, LED_RI | LEDs, now installed: **PWR/BT**, **TR**, **CD** |
| GP28: test pad | **Pairing button** (to GND, internal pull-up) |

All RS-232 GPIOs (GP0–21) stay exactly as in SPEC.md §4.6.

### 3.3 RM2's own three GPIOs

These GPIOs operate only while the radio has power. On the W board, the
radio always has power:
- **GPIO0 (pin 8):** VBUS present. VBUS through 100 kΩ / 100 kΩ gives
  2.5 V. This is more than the 2.0 V high threshold.
- **GPIO1 (pin 18):** LED **RD** (data from the computer).
- **GPIO2 (pin 17):** LED **SD** (data to the computer).

### 3.4 Power without a USB host

With Bluetooth, no device on the USB-C port communicates with the board.
But the board still needs 5 V from the USB-C. These are possible sources:
- A phone charger or a power bank.
- The USB port of the computer, if the computer has one.
- **The phone itself**: a USB-C iPhone or an Android phone supplies power
  to USB-C accessories. The data side is not used, because the board
  communicates through Bluetooth.

The DB25 cannot supply the power. RS-232 lines give some mA, and the radio
needs tens of mA.

---

## 4. Bluetooth design

### 4.1 One stack, both kinds of Bluetooth

- BTstack, with classic and LE both enabled. They operate at the same time.
- Device name `pico-serial-XXXX`, where XXXX is the end of the Bluetooth
  address. You can change it through USB.

### 4.2 BLE: data service (for iPhone and anything else)

The board uses the **Nordic UART Service (NUS)**, the de facto standard
"serial over BLE". Generic apps (nRF Toolbox of Nordic, Bluefruit Connect
of Adafruit, LightBlue and others) can open it without a custom app:

| Characteristic | UUID | Properties | Direction |
|---|---|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | | |
| RX | `6E400002-…` | write, write without response | phone → RS-232 |
| TX | `6E400003-…` | notify | RS-232 → phone |

**Throughput:**
- The board requests a 247-byte MTU, data-length extension and the 2M
  PHY. iOS keeps connection intervals at 15 ms or longer.
- Expect about 20–100 kB/s. The value changes with the phone.
- 115,200 baud needs about 11.5 kB/s in each direction, thus there is
  sufficient margin. 230,400 is a target, not a promise.

**Option with a higher throughput:**
- The board also opens an **L2CAP connection-oriented channel**. A
  characteristic gives the PSM of this channel. Apple documents this
  pattern for `CBL2CAPChannel`.
- A dedicated iOS app gets a stream with built-in flow control, in place
  of GATT writes.

### 4.3 BLE: control service (custom)

The service uses 128-bit UUIDs. We generated them one time, and they are
fixed in the firmware. All characteristics need an encrypted link.

| Characteristic | Properties | Content |
|---|---|---|
| Line coding | read, write | baud (u32), data bits, parity, stop bits |
| Outputs | read, write | mask + value: DCD, DSR, CTS, RI in DCE mode; DTR, RTS in DTE mode |
| Inputs | read, notify | the lines of the far end: DTR, RTS (DCE) or DCD, DSR, RI, CTS (DTE); "device attached"; framing/parity/overrun; break received |
| Mode | read, write | setting auto/DCE/DTE; current role |
| Break | write | duration in ms |
| Flow | read, write | auto/on/off (SPEC.md §5.5) |
| Credits | notify | free bytes in the RS-232 transmit buffer of the board, for apps that control the rate of GATT writes |
| Info | read | firmware version, board revision |

The default line mapping matches SPEC.md §5.3: in DCE mode, the DTR of the
phone becomes the DCD of the computer. A plain NUS terminal app sets no
lines. Thus it gets **DCD and DSR asserted while it is connected**. A
computer expects this from a modem that is "on".

### 4.4 Classic Bluetooth: SPP (for Android and the D-Modem app)

- An SPP server on RFCOMM, with the name "pico-serial".
- The existing mode of the D-Modem app connects to the serial port of a
  paired device. This mode operates without changes. Pair the board in the
  Bluetooth settings of Android. Then select it in dmodem.
- Data goes directly through.
- **Modem lines:** RFCOMM sends them in its modem-status messages, but the
  API of Android does not make them available. Thus, in DCE mode, the board
  gives these functions:
  - **DCD from result codes** (a setting, on by default for SPP). The board
    monitors the data from the phone for `CONNECT` and `NO CARRIER`. Then
    it raises or drops DCD.
  - **DTR drop → hang-up** (a setting, off by default). When the computer
    drops DTR, the board sends the escape sequence and `ATH` to the phone.
  - The correct solution is in the app: the USB bridge of dmodem already
    has lines. A small vendor framing through SPP, or BLE from Android,
    can also send them.
- Desktop computers (Windows, Linux, macOS) can also use the SPP port.

### 4.5 Pairing and security

The board has no screen or keypad. Thus some rules prevent unknown persons
from typing into your computer:
- **Pairing is possible only in a pairing window:**
  - For 2 minutes after power-up, while no device is bonded.
  - For 2 minutes after you push the pairing button (GP28).
  - When you open the window through USB.
- Outside the window, only bonded devices can connect. The flash keeps a
  maximum of 4 bonds. To clear them, hold the button for 10 s.
- **LE Secure Connections** for BLE and **Secure Simple Pairing** for
  classic.
- The default is "Just Works". An optional 6-digit **static passkey**
  enables passkey entry. You set it through USB and print it on the case
  label.
- Data and control characteristics need an encrypted link.

### 4.6 Who owns the serial port

USB, BLE and SPP can all be connected at the same time. But only one link
owns the RS-232 port at a time:
1. **USB** owns the port while the CDC port is open (host DTR set). USB
   takes the port also from a wireless link.
2. If USB does not own it, the **first wireless link** that connects owns
   it.
3. The other links stay connected, but they are read-only. BLE write
   attempts get an error. The board discards SPP data and shows a notice in
   the Info/status.

The PWR/BT LED shows the state: slow flash = advertising, fast flash =
pairing window open, steady = connected.

---

## 5. Firmware changes

- Board definition: a custom board header with the CYW43 pins of Pico W,
  and `PICO_CYW43_SUPPORTED=1`.
- CYW43 firmware: about 230 KB. It is part of the SDK, and it is in the
  16 MB flash. BTstack and buffers need about 60–100 KB of the 264 KB RAM
  of the RP2040.
- The three links (§4.6) share one serial core. Thus the line control, the
  flow control and the role detection (SPEC.md §5) are written one time.
- The firmware does not use Wi-Fi at first. A possible future option is a
  TCP serial server (telnet/RFC 2217) for computers or apps on the same
  network.

---

## 6. Phone software

- **iPhone/iPad:** there is no app at this time.
  - For tests, use a generic BLE UART app (§4.2).
  - For real use, an iOS app must do the same function as the Android app:
    the phone is the modem.
  - That is a separate project. It does not need MFi: BLE apps can go on
    the App Store.
- **Android:**
  - The D-Modem app operates now through SPP (§4.4).
  - If dmodem gets BLE, it can also send the modem lines through the
    control service.

---

## 7. Compliance notes (only if you sell it)

- The modular approval of the RM2 applies to the US, Canada, EU and UK
  **if** you obey its integration rules: the keep-out (§2) and the power
  conditions. Then the product needs an outside label "Contains FCC ID:
  2ABCB-RMC20452T" (and the IC equivalent).
- A product that you sell with Bluetooth in it also needs a Bluetooth SIG
  listing. This listing costs money.
- These items do not apply if you build a board for yourself.

---

## 8. Open questions

1. **Enclosure:** decided: the printed hood (§2.2). Open: print one test
   pair before you order a quantity. Examine the flange fit, the
   thumbscrew reach on a real computer, and the fit of the USB-C plug.
2. **Wired version:** keep the hood from a supplier, or give it a printed
   hood from the same script (shorter, no RM2)?
3. **Wi-Fi:** keep it off, or plan the TCP serial server?
4. **RP2350 in place of RP2040** for the W board? It is equivalent to a
   Pico 2 W: more RAM (520 KB) and faster. It needs the external inductor
   for its core regulator and a new layout. BLE does not need it.
5. **iOS app:** a separate project, or out of scope?

## 9. Next steps

1. Buy a Pico W. Order one test print of the hood (`hardware/enclosure/out/*.stl`).
2. **Bench prototype with a Pico W from a supplier.** It has the same radio
   and the same pins. Connect it to the RS-232 test rig from SPEC.md §9.
   Then prove BLE NUS with an iPhone, SPP with the D-Modem app, pairing,
   and throughput at 115,200 and 230,400. **Also test the radio in a DB25
   hood adjacent to the back panel of a computer**: this decides the
   enclosure (§2.1). These tests do not need a new board.
3. ~~Add the W variant to `hardware/netlist/build_netlist.py`~~: done
   (`--variant w`): the RM2, the moved GPIOs, the button and the installed
   LEDs; schematic `hardware/kicad/pico-serial-w.kicad_sch`.
4. ~~W board layout~~: done, `hardware/kicad/pico-serial-w.kicad_pcb`
   (see hardware/README.md, "pico-serial W"). Autorouted. A person must do
   a review before you order it.
5. Firmware: `-DPS_BOARD=w` builds BLE NUS + control service, SPP and
   pairing (firmware/README.md). Built, but not yet run on a radio. First,
   prove it on the Pico W bench rig (step 2).
