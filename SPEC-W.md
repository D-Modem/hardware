# pico-serial W — wireless variant (draft 0.1)

pico-serial with a Bluetooth radio, so a phone can be the modem without a
cable:

- **iPhone and iPad over Bluetooth Low Energy (BLE).** No Apple
  certification (MFi) is needed. Any iOS app can talk to a BLE device, and
  existing "BLE UART" apps work for testing straight away.
- **Android over classic Bluetooth serial (SPP).** dmodem-android already
  connects out to a paired device's serial port, so this works with **no
  app change**.
- **USB-C works as before**, as a wired serial port and for power.

Everything in [SPEC.md](SPEC.md) still applies: the RP2040, both RS-232
chips, the DCE/DTE auto-detect and the DB25 pinout. This document covers
only what the radio adds or changes.

---

## 1. Radio: Raspberry Pi Radio Module 2 (RM2)

| | |
|---|---|
| Part | Raspberry Pi **RM2**, order code SC1169 (marked RMC20452T) |
| Chip | Infineon CYW43439, the same radio as Pico W / Pico 2 W |
| Radio | Bluetooth 5.2, **classic and LE**; Wi-Fi 4, 2.4 GHz (unused at first) |
| Size | 14.5 × 16.5 × 2.55 mm, 21 castellated pads, 1.5 mm pitch |
| Antenna | On the module (inverted-F) |
| Supply | Vin 3.0–4.8 V (below 3.2 V reduces RF performance); VDDIO 3.3 V |
| Current | Bluetooth only: tens of mA. Wi-Fi transmit: up to 271 mA |
| Approvals | Modular approval in the US (FCC ID 2ABCB-RMC20452T), Canada, EU, UK |
| Lifetime | In production until at least January 2036 |
| Price, stock | $4 RRP; Mouser ~3,900 in stock; DigiKey 2648-SC1169CT-ND. **Not stocked at LCSC** |
| Software | Pico SDK (`pico_cyw43_arch`, BTstack), same as Pico W |

**Why RM2:**
- It's Raspberry Pi's own module, made for this chip family.
- The Pico SDK supports it without changes when it's wired like a Pico W.
- It carries the radio approvals.
- Its castellated pads are easy to solder by hand.

**Alternatives considered:**
- A bare CYW43439: needs RF design and its own approvals.
- An nRF52 or ESP32-C3 module as a second processor: two firmwares and a
  UART link between them.
- Swapping the RP2040 for an nRF52840: no longer a Pico design.

**Sourcing:** RM2 is not in LCSC/JLC stock. Buy it from DigiKey or Mouser
and either solder it by hand or send it to JLC as a consigned part.

---

## 2. The antenna decides the form factor

RM2's datasheet (Figure 6) asks for an **RF keep-out of 35.5 × 18.5 mm**
around the antenna end of the module, free of **any** metal:
- 13.5 mm beyond the module's top edge;
- 10.5 mm either side of it;
- the top ~5 mm of the module itself.

The whole board inside a DB25 hood is only about 36 × 29 mm. It also sits
between the DB25's metal shell and the USB-C connector and cable. **The
keep-out can't be met inside a hood.**

Options:

| | Form | Antenna | Notes |
|---|---|---|---|
| **A (recommended)** | Board in a small plastic case, about 40 × 65 mm. The DB25 female stays on one end (same solder-cup mount), USB-C at the other | RM2 on a long side edge, antenna end flush with the board edge; the keep-out falls in air inside the case | Meets the datasheet. Room for the LEDs and a pairing button on the case |
| B | Keep the hood size; RM2 at the back with the antenna overhanging the board | Keep-out not met: USB-C and the cable are right there | Shorter range, and the modular approval no longer applies |
| C | Hood board for the wired version; a separate small radio board on a short cable | Fine | Two boards and an inter-board cable; messy |

Option A gives a second board, **pico-serial W**. The hood version stays
as it is. Both come from the same netlist script with a variant switch.
Only the radio, the outline and a few GPIOs differ.

> Even with a perfect keep-out, the radio will sit next to a computer's
> metal case and cables. Expect reliable range across a desk or a room,
> not through walls. That suits this job, since the phone is usually next
> to the computer.

---

## 3. Circuit changes

### 3.1 RM2 wiring: identical to Pico W

With this wiring, the Pico SDK's Pico W pin defaults work with no
configuration.

| RM2 pin | Function | Connects to |
|---|---|---|
| 1, 4, 7, 11, 15, 21 | Ground | GND (many vias under the module) |
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

Supply: AP2112K-3.3 (600 mA) still covers it. Add 22 µF bulk next to the
RM2, for Wi-Fi transmit peaks if Wi-Fi is ever used.

### 3.2 GPIOs that move

RM2 needs GP23, GP24, GP25 and GP29. The hood board uses them for three
optional LEDs and the VBUS sense. In the W board:

| Was (hood board) | Becomes (W board) |
|---|---|
| GP23–25: LED_TR, LED_CD, LED_SD | RM2 power, data, chip select |
| GP29: VBUS ÷2 sense | RM2 SPI clock |
| GP22, 26, 27: LED_PWR, LED_RD, LED_RI | LEDs, now fitted: **PWR/BT**, **TR**, **CD** |
| GP28: test pad | **Pairing button** (to GND, internal pull-up) |

Every RS-232 GPIO (GP0–21) stays exactly as in SPEC.md §4.6.

### 3.3 RM2's own three GPIOs

These work only while the radio is powered, which it always is in the W
board:
- **GPIO0 (pin 8):** VBUS present. VBUS through 100 kΩ / 100 kΩ, giving
  2.5 V, above the 2.0 V high threshold.
- **GPIO1 (pin 18):** LED **RD** (data from the computer).
- **GPIO2 (pin 17):** LED **SD** (data to the computer).

### 3.4 Power without a USB host

Over Bluetooth, nothing on the USB-C port talks to the board, but it still
needs 5 V from the USB-C:
- a phone charger or power bank;
- the computer's own USB port, if it has one;
- **the phone itself**: a USB-C iPhone or an Android phone supplies power
  to USB-C accessories. The data side goes unused, because the board talks
  over Bluetooth.

The DB25 can't power it: RS-232 lines give a few mA and the radio needs
tens.

---

## 4. Bluetooth design

### 4.1 One stack, both kinds of Bluetooth

- BTstack with classic and LE both enabled, running at the same time.
- Device name `pico-serial-XXXX`, where XXXX is the end of the Bluetooth
  address. It can be changed over USB.

### 4.2 BLE: data service (for iPhone and anything else)

The **Nordic UART Service (NUS)**, the de facto standard "serial over
BLE". Generic apps (Nordic's nRF Toolbox, Adafruit's Bluefruit Connect,
LightBlue and others) can open it with no custom app:

| Characteristic | UUID | Properties | Direction |
|---|---|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | | |
| RX | `6E400002-…` | write, write without response | phone → RS-232 |
| TX | `6E400003-…` | notify | RS-232 → phone |

**Throughput:**
- The board asks for a 247-byte MTU, data-length extension and the 2M
  PHY. iOS keeps connection intervals at 15 ms or longer.
- Expect roughly 20–100 kB/s depending on the phone.
- 115,200 baud needs about 11.5 kB/s each way, so it's comfortable.
  230,400 is a target, not a promise.

**Higher-throughput option:**
- The board also opens an **L2CAP connection-oriented channel**, whose
  PSM is published in a characteristic. That's the pattern Apple
  documents for `CBL2CAPChannel`.
- A dedicated iOS app gets a stream with built-in flow control instead of
  GATT writes.

### 4.3 BLE: control service (custom)

128-bit UUIDs, generated once and fixed in the firmware. Every
characteristic needs an encrypted link.

| Characteristic | Properties | Content |
|---|---|---|
| Line coding | read, write | baud (u32), data bits, parity, stop bits |
| Outputs | read, write | mask + value: DCD, DSR, CTS, RI in DCE mode; DTR, RTS in DTE mode |
| Inputs | read, notify | the far end's lines: DTR, RTS (DCE) or DCD, DSR, RI, CTS (DTE); "device attached"; framing/parity/overrun; break received |
| Mode | read, write | setting auto/DCE/DTE; current role |
| Break | write | duration in ms |
| Flow | read, write | auto/on/off (SPEC.md §5.5) |
| Credits | notify | free bytes in the board's RS-232 transmit buffer, for apps that pace GATT writes |
| Info | read | firmware version, board revision |

The default line mapping matches SPEC.md §5.3: in DCE mode the phone's DTR
becomes the computer's DCD. A plain NUS terminal app, which sets no lines,
therefore gets **DCD and DSR asserted while connected**. That's what a
computer expects from a modem that's "on".

### 4.4 Classic Bluetooth: SPP (for Android and dmodem-android)

- An SPP server on RFCOMM, named "pico-serial".
- dmodem-android's existing mode, which connects to a paired device's
  serial port, works unchanged. Pair the board in Android's Bluetooth
  settings, then pick it in dmodem.
- Data passes straight through.
- **Modem lines:** RFCOMM carries them in its modem-status messages, but
  Android's API doesn't expose them. The board therefore offers, in DCE
  mode:
  - **DCD from result codes** (setting, on by default for SPP). The board
    watches the data from the phone for `CONNECT` and `NO CARRIER` and
    raises or drops DCD.
  - **DTR drop → hang-up** (setting, off by default). When the computer
    drops DTR, the board sends the phone the escape sequence and `ATH`.
  - The proper fix is in the app: dmodem's USB bridge already has lines.
    A small vendor framing over SPP, or using BLE from Android, would
    carry them too.
- Desktop computers (Windows, Linux, macOS) can use the SPP port too.

### 4.5 Pairing and security

The board has no screen or keypad, so a few rules keep strangers from
typing into your computer:
- **Pairing is allowed only in a pairing window:**
  - for 2 minutes after power-up while no device is bonded;
  - for 2 minutes after the pairing button (GP28) is pressed;
  - or when the window is opened over USB.
- Outside the window, only bonded devices may connect. Up to 4 bonds are
  kept in flash. Holding the button for 10 s clears them.
- **LE Secure Connections** for BLE and **Secure Simple Pairing** for
  classic.
- Default is "Just Works". An optional 6-digit **static passkey**, set
  over USB and printed on the case label, turns on passkey entry.
- Data and control characteristics need an encrypted link.

### 4.6 Who owns the serial port

USB, BLE and SPP can all be connected, but only one owns the RS-232 port
at a time:
1. **USB** owns it while the CDC port is open (host DTR set), and takes
   it over even from a wireless link.
2. Otherwise the **first wireless link** to connect owns it.
3. The others stay connected but read-only. BLE write attempts get an
   error, and SPP data is dropped with a notice in the Info/status.

The PWR/BT LED shows the state: slow blink = advertising, fast blink =
pairing window open, steady = connected.

---

## 5. Firmware changes

- Board definition: a custom board header with Pico W's CYW43 pins, plus
  `PICO_CYW43_SUPPORTED=1`.
- CYW43 firmware: about 230 KB, part of the SDK, stored in the 16 MB
  flash. BTstack and buffers need about 60–100 KB of the RP2040's 264 KB
  RAM.
- One serial core shared by all three links (§4.6), so line handling,
  flow control and role detection (SPEC.md §5) are written once.
- Wi-Fi is not used at first. A later option is a TCP serial server
  (telnet/RFC 2217) for computers or apps on the same network.

---

## 6. Phone software

- **iPhone/iPad:** there is no app yet.
  - For testing, use a generic BLE UART app (§4.2).
  - For real use, an iOS app has to do what dmodem-android does: the
    phone as the modem.
  - That's a separate project and needs no MFi: BLE apps can go on the
    App Store.
- **Android:**
  - dmodem-android works today over SPP (§4.4).
  - Adding BLE to dmodem would also carry the modem lines through the
    control service.

---

## 7. Compliance notes (only if you sell it)

- The RM2's modular approval covers the US, Canada, EU and UK **if** its
  integration rules are followed: the keep-out (§2) and the power
  conditions. The product then needs an outside label "Contains FCC ID:
  2ABCB-RMC20452T" (and the IC equivalent).
- A product sold with Bluetooth in it also needs a Bluetooth SIG listing,
  which costs money.
- Neither matters for building your own.

---

## 8. Open questions

1. **Form factor:** option A (small case, recommended), B (hood size,
   compromised antenna) or C (two boards)?
2. **Case:** 3D-printed, or an off-the-shelf small plastic box? It
   decides the outline, the button and where the LEDs show.
3. **Wi-Fi:** leave it off, or plan the TCP serial server?
4. **RP2350 instead of RP2040** for the W board? It's a Pico 2 W
   equivalent: more RAM (520 KB) and faster. It needs the external
   inductor for its core regulator and a new layout. Not needed for BLE.
5. **iOS app:** separate project, or out of scope?

## 9. Next steps

1. Pick the form factor and case (§8).
2. **Bench prototype with an off-the-shelf Pico W.** It has the same radio
   and the same pins. Wire it to the RS-232 test rig from SPEC.md §9, then
   prove BLE NUS with an iPhone, SPP with dmodem-android, pairing, and
   throughput at 115,200 and 230,400. This needs no new board.
3. Add the W variant to `hardware/netlist/build_netlist.py`: the RM2, the
   moved GPIOs, the button and fitted LEDs.
4. W board layout following §2 option A.
