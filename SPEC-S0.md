<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 D-Modem Authors -->
# pico-serial S0 — ISDN basic rate stick (draft 0.1)

A USB stick with an **ISDN S/T (S0) port**: an RJ45 socket at one end,
USB-C at the other, the Line stick's shape. It plugs into an NTBA or a
PBX's internal S0 port as an ISDN terminal (TE), or, the other way round,
it is the network (NT) for ISDN terminal adapters, ISDN cards and phones.

As with the Line stick ([SPEC-L.md](SPEC-L.md)), the stick is only the
line interface. It carries the S0 bus's layer 1 and HDLC framing, and
streams both B-channels and the D-channel's frames over USB. Everything
above, LAPD, Q.931, the clear channel modes (X.75, V.110, V.120, sync
PPP), every analogue modulation on a 3.1 kHz call, and CAPI, is
**D-Modem on the host** (`dmodemd`, dmodem-android). D-Modem already has
those parts for Linux mISDN cards (`dmodemd --misdn`, D-Modem's
`docs/isdn.md`); this stick brings them to every host D-Modem runs on,
Android and Windows included, where there is no mISDN.

Not covered: the U interface (the exchange's 2B1Q pair to the NTBA; the
chip could do Up0/UpN, see §14), primary rate (S2M/E1: a different
chip, as Osmocom's icE1usb), and power feeding ISDN phones in NT mode on
rev A (§4.5).

---

## 1. Requirements

| # | Requirement |
|---|---|
| S1 | USB-C device, Full Speed, bus powered, under 100 mA |
| S2 | One RJ45 socket, wired as an ISO 8877 TE (pins 3/6 transmit, 4/5 receive) |
| S3 | ITU-T I.430 layer 1, **TE mode** (to an NTBA or a PBX's S0 port) and **NT mode** (for terminals), chosen by the host |
| S4 | Both B-channels, 64 kbit/s each, transparent, each way; the D-channel's HDLC frames (16 kbit/s) with I.430's D-channel access (echo bits, priority classes) |
| S5 | The network's clock: in TE mode the B-channel octets run at the S0 bus's 8000 Hz, and the USB stream follows it, as the Line stick's follows its codec |
| S6 | Galvanic isolation from the S0 bus: the line transformers, ≥ 1500 Vrms |
| S7 | Protection against a wrong cable: survives an analogue phone line (−48 V, ringing 90 Vrms) or a powered S0 bus (40 V) on any pins |
| S8 | Switchable 100 Ω terminations (point to point, or the end of a passive bus) |
| S9 | The same RP2040 core, USB-C and firmware structure as the other sticks; drivers on Linux, Windows (WinUSB, no INF) and Android |

---

## 2. Block diagram

```
                     SPI (25 MHz max) ──────────┐
 USB-C ── ESD ── RP2040 ── PCM30 (PIO) ──── XHFC-1SU ── TX ══ transformer ══┐
  (UFP)          │   IRQ, RESET ──────────────┘  │  24.576 MHz     1:2 CT    ├─ TVS, PTC ── RJ45
  LDO 3.3 V      │                               └─ RX ══ transformer ══════┘   (3/6 TX, 4/5 RX)
  flash, 12 MHz  ├── PhotoMOS ×2: 100 Ω terminations (line side)     isolation barrier
                 └── LEDs (LINK, B1, B2)
```

---

## 3. Choosing the S/T chip

ISDN chips are mostly gone. What is left:

| Part | Maker | What it is | Status (October 2026) |
|---|---|---|---|
| **XHFC-1SU** | Cologne Chip | one S/T or Up0 port, HDLC controller, FIFOs, PCM bus, SPI or 8-bit bus; TE and NT; LQFP-64, 3.3 V | **in mass production, "recommended for new projects"** ([colognechip.com](https://colognechip.com/xhfc-1su-overview/)); sold through Cologne Chip's own distribution, not LCSC, Mouser or DigiKey |
| XHFC-2SU | Cologne Chip | the same with two ports (Osmocom's [xhfc-breakout](https://gitea.osmocom.org/electronics/osmo-small-hardware) uses it) | "contact factory" |
| HFC-S USB | Cologne Chip | a complete USB ISDN adapter in one chip (mISDN's `hfcsusb`) | datasheet from 2002; no current availability found. It would also tie the stick to Cologne Chip's USB IDs and protocol |
| SBCX PEB 3081, IPAC-X PSB 21150 | Infineon / Lantiq (Intel) | S/T transceiver, S/T + HDLC controller | broker stock only, end of life |
| TP3420A | TI | S/T transceiver | NRND, no successor |
| MC145574 | Motorola / NXP | S/T transceiver | discontinued |
| W6691 | Winbond | PCI S/T controller | discontinued, and PCI |
| **No chip:** S0 in the RP2040's PIO | ours | a discrete transmitter and two comparators, the line code, framing and D-channel access in software (§12) | every part at LCSC, but nobody has done it, and I.430 conformance is ours to prove |

**Rev A uses the XHFC-1SU.** It is the one S/T chip still made, it does
TE and NT to I.430 and TBR 3, its HDLC controller does the D-channel's
framing, CRC and access procedure (the echo channel, priority classes,
automatic repetition of a frame that lost a collision), and its external
circuit is a few resistors, two transistors and a transformer pair. The
cost is sourcing: it is bought from Cologne Chip (a quote), and fitted by
hand or sent to JLC as a consigned part, as the Line stick's Si3056 is.
LQFP-64 at 0.5 mm pitch hand-solders.

The software S0 (§12) stays the fallback, and an experiment worth doing
on a later board: it would make the stick all-LCSC.

---

## 4. The line interface

Cologne Chip's XHFC-2SU datasheet (June 2012, §5.2.8, the same circuit
for the 1SU): one transformer for each direction, both 1:2 (line : chip)
with a centre tap on the chip side.

### 4.1 Receive

The line pair through its transformer to the chip's four receive pins
(R_A, L_A, L_B, R_B) through the datasheet's divider network (100 kΩ,
33 kΩ, 3.9 kΩ, 22 pF at the pins, BAV99 clamps on the transformer
side, the centre tap biased from 3.3 V by 330 kΩ / 3.9 kΩ). The
receiver detects polarity by itself, so the pair may be either way round.

### 4.2 Transmit

T_A and T_B through 3.3 Ω to the transmit transformer's ends, its centre
tap at **V_AMP**, about 2.8–3.1 V from a BC848 pair (the datasheet's
Figure 5.7), and VDD_SU from the same reference. V_AMP sets the pulse
amplitude: it is trimmed (a resistor choice, set once on the bench) for
I.430's 750 mV into 50 Ω. 62 Ω and 1 nF across the line side damp the
undershoot (values depend on the transformer). BAV99 clamps each end to
the rails.

### 4.3 Transformer

A dual 1:2 module, centre-tapped on the chip side, with a common-mode
choke: **UMEC UT20795-05TS** (Osmocom's xhfc-breakout uses the -5MTS),
or Pulse **PE-65795** (dual SMT, 1:2, 2000 Vrms). Both meet I.430's pulse
template with Cologne Chip's circuit. Neither is at LCSC: from DigiKey or
Mouser, consigned. A Pulse part at LCSC would be better; see §14.

### 4.4 Wiring, NT mode and terminations

- The RJ45 is wired as a TE: pins 3 (+) and 6 (−) transmit, 4 (+) and
  5 (−) receive. Cologne Chip's polarity: T_A to the jack's minus pin,
  T_B to its plus.
- **NT mode** swaps the pairs: rev A does it with a standard ISDN
  crossover cable (3↔4, 6↔5), and the firmware switches the chip to NT.
  Swapping on the board needs two latching DPDT relays (§14).
- **Terminations:** 100 Ω across each pair, each through a 60 V
  PhotoMOS relay (normally open), so the host turns them on: in NT
  mode, and in TE mode on a point-to-point line with nothing else at our
  end. 3 mA of LED current each while on.

### 4.5 Power on the bus (PS1)

An NT may feed 40 V (34–42 V) to its terminals through the transformers'
centre taps on the line side. **Rev A neither feeds nor draws it.** In TE
mode the line side's centre taps are left free, so an NTBA's 40 V does
nothing. In NT mode, locally powered terminals (TAs, ISDN cards, most
PBXs' TE ports) work; ISDN phones powered by the bus need an external
40 V supply put on the line side's centre taps. A feeding variant (a
5 V → 40 V boost, about 1 W, past USB's 100 mA unless the host offers
more through USB-C current advertisement) is §14.

### 4.6 Protection

S0 is inside a building, but a stick will meet the wrong socket. On the
line side, per pair: a PTC (or the transformer's own winding resistance
and a 0.5 A telecom fuse) and a bidirectional TVS that stands off 58 V
(above PS1's 42 V) and clamps below the transformer's isolation, low
capacitance (≤ 50 pF: the bus is a 100 Ω pair at 192 kbit/s). A phone
line's ringing (≈ 130 V peak on −48 V) is then clamped and the PTC
trips; nothing passes the transformer that the chip-side BAV99s cannot
take. The chip side gets the datasheet's BAV99 clamps.

Layout, as for the Line stick: the isolation barrier is the transformer
module; nothing else crosses it except the PhotoMOS outputs, whose own
barrier is rated for it. Creepage and clearance across it ≥ 2.5 mm on
every layer; no ground plane under the line side.

---

## 5. Clocks

- **XHFC-1SU:** a 24.576 MHz 3.3 V oscillator into OSC_IN (e.g. SCTF
  SX3V24.576B, LCSC C5179997; or the datasheet's Pierce circuit with a
  crystal). Its PLL makes the line interface's 12.288 MHz and the PCM
  side's clocks.
- **RP2040:** its own 12 MHz crystal, as on every stick.
- **TE mode:** the XHFC locks to the NT's frames; its PCM bus (F0IO,
  C4IO), which it masters, runs at the network's 8000 frames a second. So
  the B-channel octets come at the network's rate, and the USB side counts
  them (§8.2), as the Line stick counts its codec's samples. Slips are
  counted and reported.
- **NT mode:** the stick is the network clock: the 24.576 MHz oscillator
  (±50 ppm). TEs lock to it.

---

## 6. The XHFC-1SU and the RP2040

| XHFC-1SU | RP2040 | |
|---|---|---|
| SPI_CLK, D6/SPI_RX, D7/SPI_TX, /CS | SPI0 (SCK, TX, RX, CSn) | registers and the D-channel FIFOs; mode 0, up to 25 MHz (run at 12.5 MHz from clk_peri) |
| A0, D0–D5 | GND | SPI mode (A0/SPI_GND and the unused data pins tied low, as the datasheet's SPI configuration) |
| SPI_INV | GND | SPI clock not inverted |
| MODE0, MODE1 | strapped | the SPI processor interface (the datasheet's mode table, to confirm) |
| F0IO | PIO | the PCM frame sync, 8 kHz, the XHFC master |
| C4IO | PIO | the PCM bit clock (×2): PCM30, 2.048 Mbit/s, 32 slots |
| STIO1 | PIO in | the XHFC's PCM output: B1 in slot 1, B2 in slot 2 |
| STIO2 | PIO out | to the XHFC: B1, B2 |
| /INT | GPIO (IRQ) | open drain, pulled up |
| /RES | GPIO | held low until the oscillator runs |
| WAKEUP | — | not used |

- **B-channels over PCM, not SPI:** the XHFC's data flow connects each
  B-channel straight to a PCM time slot (transparent, no HDLC), and a PIO
  program shifts the slots into the RP2040's DMA. This gives the octets
  exactly at the network's clock, with no FIFO polling, the same shape as
  the Line stick's codec port. The PIO shifts most significant bit first,
  so octets reach USB in G.711 and I.460 order: the first bit on the line
  is the octet's top bit (no bit reversal on the host, unlike mISDN's
  cards, D-Modem `docs/isdn.md`).
- **D-channel over SPI:** FIFO pair 2 in HDLC mode. The XHFC adds and
  checks the CRC, does the access procedure in TE mode (it watches the
  echo bits, backs off on a collision and repeats the frame by itself),
  and gives a status byte after each received frame. 16 kbit/s is a few
  hundred SPI transactions a second.
- **Layer 1 states** (I.430's F3–F7 in TE mode, G1–G4 in NT mode): the
  XHFC's state machine, read in its interrupt and reported (§8.4). The
  host asks for activation; the XHFC does the INFO 1–4 exchange.
- The **E-channel** (the NT's echo of the D bits) is the chip's business;
  it is not sent to the host.

GPIO map (preliminary):

| GPIO | |
|---|---|
| 0–3 | SPI0: RX (MISO), CSn, SCK, TX (MOSI) |
| 4–7 | PCM: F0IO, C4IO, STIO1 (in), STIO2 (out); PIO0 |
| 8 | XHFC /INT |
| 9 | XHFC /RES |
| 10, 11 | termination PhotoMOS: TX pair, RX pair |
| 12–14 | LEDs: LINK (layer 1 active), B1, B2 (a call on it) |
| 24, 25 | test pads (UART0, firmware log) |
| 29 | VBUS sense (ADC3), as on the other sticks |

---

## 7. Rest of the board and power

The RP2040 core of SPEC.md §4.2–4.4 (the chip, W25Q flash, 12 MHz
crystal, AP2112K-3.3, USBLC6 ESD, USB-C with its CC resistors, a 0.5 A
PTC), the BOOTSEL and RESET buttons inside, SWD test pads.

Budget, to check on the bench: RP2040 about 25 mA; the XHFC-1SU and its
oscillator, the datasheet's DC figures to be confirmed (Cologne Chip's
3.3 V parts run at tens of mA); the transmitter about 15 mA peak while
sending zeros into 50 Ω, a few mA on average; PhotoMOS LEDs 3 mA each;
LEDs 2 mA each. **Expected under 80 mA**, inside S1.

---

## 8. USB interface (firmware)

Shaped like the Line stick's ([firmware/PROTOCOL.md](firmware/PROTOCOL.md),
"Line stick"), so its finding, Windows and isochronous rules carry over.
The exact numbers move into PROTOCOL.md and `src/core/s0_proto.h` with
the firmware.

### 8.1 Finding it

- USB `1209:0004` (a pid.codes *test* PID, beside the Line stick's
  `1209:0002` and the WB stick's `1209:0003`, until a real one is
  assigned), product "pico-serial S0". `GET_INFO`'s `board` is 5.
- Windows: WinUSB by itself, the same BOS descriptor and Microsoft OS 2.0
  set as the Line stick, D-Modem's `DeviceInterfaceGUID`.
- Interface 0: class `0xFF`, subclass `0x53` ('S'), protocol `0x01`.
  Interface 1: Raspberry Pi's reset interface, for `picotool`.

### 8.2 Endpoints (alternate setting 1)

| Endpoint | Type | Max packet | |
|---|---|---|---|
| `0x01` OUT | isochronous, asynchronous, 1 ms | 24 bytes | B-channel octets to the line |
| `0x82` IN | isochronous, asynchronous, 1 ms | 24 bytes | B-channel octets from the line |
| `0x83` IN | interrupt, 4 ms | 8 bytes | events (§8.4) |
| `0x04` OUT | bulk | 64 | D-channel frames to the line |
| `0x85` IN | bulk | 64 | D-channel frames from the line |

- **B-channels:** pairs of octets, B1 then B2, one pair each 125 µs:
  8 pairs (16 bytes) a nominal millisecond. As on the Line stick, each IN
  packet carries the pairs the line gave since the last one, so its
  length *is* the network's clock (7, 8 or 9 pairs, 12 at most to catch
  up); the host sends per millisecond as many pairs as the last IN packet
  brought. The stick plays them after a 2 ms head start; dry, it sends
  the idle octet (`0xFF`), counts an underrun and waits for its head
  start again. A B-channel not switched on (`SET_B`) carries `0xFF`
  both ways and the host's octets for it are ignored.
- **D-channel:** one frame per bulk transfer (ended by a short packet, or
  a zero-length one), address to the last information octet: no flags,
  no CRC. IN frames start with one status octet: 0 good, 1 bad CRC,
  2 aborted, 3 overrun (the frame's octets follow for logging). Up to
  264 octets (LAPD's N201 of 260 plus the header).

### 8.3 Vendor requests

| Request | Dir | |
|---|---|---|
| `GET_INFO` | IN | the version, board 5, the XHFC's chip ID and revision |
| `SET_MODE` | OUT | 0 TE, 1 NT; layer 1 deactivates first |
| `ACTIVATE`, `DEACTIVATE` | OUT | layer 1 (PH-ACTIVATE request); in NT mode `DEACTIVATE` sends INFO 0 |
| `GET_STATUS` | IN | the layer 1 state (F1–F8, G1–G4), the termination switches, counters: frame slips, B underruns and overruns, D frames each way, bad CRCs, D collisions |
| `SET_B` | OUT | which B-channels flow (bit 0 B1, bit 1 B2) |
| `SET_TERM` | OUT | the 100 Ω terminations (bit 0 TX pair, bit 1 RX pair) |
| `LOOP` | OUT | test loops: B-channels back inside the XHFC, or at the line side (I.430's loops 2 and 3 for the bench) |
| `REG_READ`, `REG_WRITE` | IN, OUT | the XHFC's registers, for debugging |
| `REBOOT_UF2` | OUT | to the UF2 loader, as the other sticks |

### 8.4 Events (`0x83`)

Eight bytes when something changes: the layer 1 state (activated,
deactivated, lost frame sync, the NT's INFO 2 or INFO 4 seen), a frame
slip, a B-channel underrun or overrun, a D-channel FIFO overrun.

---

## 9. Host software

### 9.1 dmodemd

`dmodemd --usb` finds an S0 stick beside the SmartUSB56, the Line and
the WB stick. A new line backend, `dmodemd/s0_line.cpp`, follows
`misdn_line.cpp` (one call; Q.931 from `q931.cpp`; the bearer picks the
clear channel or a 3.1 kHz modem call) with two differences:

- **LAPD in dmodemd.** On mISDN, layer 2 is the kernel's. Here it is
  `dmodemd/lapd.cpp`: Q.921 on the stick's D-channel frames, TEI
  assignment (automatic, on TEI 127 / SAPI 63), SAPI 0's multiple frame
  operation, T200, N200, T203. `link/lapb.cpp` already has Q.921's
  extended mode for V.120 (SABME, modulo 128, the timers), which the LAPD
  code builds on.
- **No bit reversal**: the stick sends octets in line order (§6).

Then, in order of use:
- **Two B-channels**: two calls at once (two ptys), which `--misdn`
  doesn't do yet.
- **NT mode**: Q.931's network side and LAPD's TEI assigner, so an ISDN
  terminal adapter, an ISDN card in a vintage PC, or a BBS's ISDN
  hardware dials into D-Modem: the stick is the exchange. A call between
  two terminals on the bus can be switched B-channel to B-channel in the
  XHFC, or routed to SIP.
- **CAPI**: `dmodem-capid` (the ISDN work's phase 4) drives a D-Modem
  through its DTE with AT commands, above every line backend, so the
  stick is a CAPI controller wherever dmodemd runs, with nothing in the
  stick or in `s0_line.cpp` for it.

### 9.2 dmodem-android

Through Android's USB host API and libusb, as the Line stick. Android
has no ISDN stack at all; with this stick a phone dials ISDN data calls.

### 9.3 mISDN (later, optional)

A small Linux kernel driver against §8 would make the stick an mISDN
device, for mISDN's other users (Asterisk's chan_misdn, LCR, isdn
utilities) and for `dmodemd --misdn`. It is not needed for D-Modem: the
userspace path works on every host.

### 9.4 Testing without hardware

A `fake_s0` device for dmodemd's tests (as `fake_st7554` and
`fake_misdn`): two dmodemd instances, one in TE and one in NT mode,
through the fake's D- and B-channels, then one stick in each mode back to
back with a crossover cable.

---

## 10. Mechanical

The Line stick's shell (`hardware/enclosure/stick.py`), with an RJ45 in
place of the RJ11: about 16 mm wide and 13–14 mm tall, so the stick is
about 2 mm wider than the Line stick and as thick. A mid-mount jack would
make it thinner. Light pipes for the three LEDs.

---

## 11. Parts (to check against stock before the netlist)

| Role | Part | Source | Package | Notes |
|---|---|---|---|---|
| S/T controller | Cologne Chip XHFC-1SU | Cologne Chip (quote) | LQFP-64 0.5 mm | consigned or hand-fitted |
| Oscillator | 24.576 MHz, 3.3 V | LCSC C5179997 (SCTF SX3V24.576B) | 3225 | or a crystal and the Pierce circuit |
| Transformer | UMEC UT20795-05TS, or Pulse PE-65795 | DigiKey / Mouser | dual SMT | 1:2, chip-side centre tap, common-mode choke |
| Transmit reference | BC848C ×2 (or a BC848 dual) | LCSC | SOT-23 | V_AMP, VDD_SU |
| Clamps | BAV99 | LCSC C2500 | SOT-23 | chip side, transmit and receive |
| Line TVS ×2 | bidirectional, 58 V stand-off, low capacitance | LCSC, to choose | SMB or smaller | |
| PTC or fuse ×2 | telecom rated | LCSC, to choose | | |
| Terminations | 100 Ω ×2 through 60 V PhotoMOS ×2 | LCSC, to choose (AQY212 or TLP172A class) | SOP-4 | |
| RJ45 | unshielded, no magnetics, 8P8C | LCSC, to choose | THT | latch toward the top |
| RP2040 core | as SPEC.md §3 | LCSC | | |

---

## 12. The alternative: S0 in the RP2040's PIO

What the XHFC does on the line is within the RP2040's reach:

- I.430's line is 192 kbit/s, pseudo-ternary: a binary one is no pulse
  (the TE's transmitter at high impedance), a zero a pulse of alternating
  polarity, 750 mV into 50 Ω. 48 bit frames, 4000 a second: each frame
  carries two octets of B1, two of B2 and 4 D bits, and (NT to TE) the E bits
  that echo the D bits the NT heard.
- **Transmit:** two GPIOs each drive one end of the transmit transformer's
  chip winding through a resistor, the third state a binary one. The
  pulse amplitude and the TE's output impedance (I.430's templates) come
  from the resistors and the transformer.
- **Receive:** two comparators (a fast dual, at LCSC) at about ±50 % of
  the nominal pulse, into two GPIOs, oversampled 16 times in PIO
  (3.072 MHz).
- **Software:** a PIO program for each direction; core 1 recovers the
  clock (a digital PLL on the received zeros), finds the frame (the F bit's
  code violation), sends its frames two bits behind the NT's (I.430's
  offset), runs the D-channel access (compare each D bit sent with the E
  bit back, stop at once on a difference; the priority classes' counts of
  ones), the activation states (INFO 0–4), and HDLC on the D-channel. At
  5.2 µs a bit and 125 MHz that is 650 cycles a bit for one core: room to
  spare.

Why not on rev A: the work is all ours (the DPLL and the comparator
thresholds must hold on a passive bus with other terminals, and on a long
point-to-point line), and I.430's pulse mask, impedance and jitter
figures have to be shown on the bench before the stick goes on someone's
NTBA. The XHFC has them. A soft-S0 board (`--variant s0soft`) would be
cheap to try once rev A gives a reference to test against.

---

## 13. Compliance (only if you sell it)

Terminal equipment on a public ISDN: in Europe the Radio Equipment and
Low Voltage rules with EN 62368-1, TBR 3 conformance for the S/T
interface (the XHFC is designed to pass TBR 3); elsewhere
each country's own. NT mode makes a private bus only. Building your own:
at your own risk, on your own NTBA or PBX.

---

## 14. Open questions

1. **Buying XHFC-1SU:** price, minimum order and lead time from Cologne
   Chip, and whether a distributor (or JLC's global sourcing) can supply
   it, so the board can be assembled whole.
2. **A transformer at LCSC:** a Pulse ISDN S module of the T50xx family
   with 1:2 and a chip-side centre tap, so the line side is assembled too.
3. **NT on the board instead of a crossover cable:** two latching DPDT
   signal relays (pulsed: no standing current) on the line side swap the
   pairs, at 2 × 9 × 14 mm. Worth it if the stick is mostly used as an NT.
4. **Power feeding (PS1) in NT mode:** a 5 V → 40 V boost (about 1 W for
   one phone), only on a host that offers 500 mA or more.
5. **The Up0 port:** the XHFC-1SU's same port can run Up0/UpN, the
   two-wire interface of many PBXs' system phones. A second footprint
   choice, not on rev A.
6. **XHFC-1SU's supply current:** from the datasheet's DC section and
   the bench, for §7.

## 15. Next steps

1. Parts (§11, §14 1–2): the XHFC from Cologne Chip, a transformer.
2. Netlist, schematic and PCB (`--variant s0`), the shell (`stick.py`
   with an RJ45).
3. Firmware: the XHFC over SPI, its PCM bus in PIO, the USB interface
   (§8), against the XHFC's own loops first.
4. D-Modem: `fake_s0`, `lapd.cpp`, `s0_line.cpp`; then a stick on an NTBA,
   and two sticks back to back (TE and NT).
