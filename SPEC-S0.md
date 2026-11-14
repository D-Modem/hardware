<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 D-Modem Authors -->
# pico-serial S0 — ISDN basic rate stick (draft 0.2)

pico-serial S0 is a USB stick with an **ISDN S/T (S0) port**. It has an
RJ45 socket at one end and USB-C at the other end, in the shape of the Line
stick. It can operate in two directions:
- It connects to an NTBA, or to the internal S0 port of a PBX, as an ISDN
  terminal (TE).
- It is the network (NT) for ISDN terminal adapters, ISDN cards and phones.

As with the Line stick ([SPEC-L.md](SPEC-L.md)), the stick is only the
line interface. Here, also its layer 1 is software: there is **no ISDN
chip**. The PIO of the RP2040 drives and samples the S0 bus through a
transformer pair, a tri-state buffer and two comparators. The second core
runs I.430: the line code, the clock and frame recovery, the access
procedure of the D-channel and HDLC. The stick sends the two B-channels and
the frames of the D-channel through USB.

All functions above this level are **D-Modem on the host** (`dmodemd`,
dmodem-android): LAPD, Q.931, the clear channel modes (X.75, V.110, V.120,
sync PPP), all analogue modulations on a 3.1 kHz call, and CAPI. D-Modem
already has these parts for Linux mISDN cards (`dmodemd --misdn`,
`docs/isdn.md` of D-Modem). This stick brings them to all hosts that
D-Modem runs on, which include Android and Windows, where there is no
mISDN.

All parts are at LCSC, but at this time the S0 transformer is not (§11).
The disadvantage is that we must show I.430 conformance on the bench
(§12). The fallback is the one S/T chip that is still in production, the
XHFC-1SU of Cologne Chip (§13).

This stick is not for these items:
- The U interface (the 2B1Q or 4B3T pair from the exchange to the NTBA:
  see the FPGA board, [SPEC-FX.md](SPEC-FX.md)).
- Primary rate (S2M/E1).
- Power feed to ISDN phones in NT mode on rev A (§4.6).

---

## 1. Requirements

| # | Requirement |
|---|---|
| S1 | USB-C device, Full Speed, bus powered, less than 100 mA |
| S2 | One RJ45 socket, wired as an ISO 8877 TE (pins 3/6 transmit, 4/5 receive) |
| S3 | ITU-T I.430 layer 1, **TE mode** (to an NTBA or the S0 port of a PBX) and **NT mode** (for terminals). The host selects the mode |
| S4 | The two B-channels, 64 kbit/s each, transparent, in each direction; the HDLC frames of the D-channel (16 kbit/s) with the D-channel access of I.430 (echo bits, priority classes) |
| S5 | The clock of the network: in TE mode, the B-channel octets run at the 8000 Hz of the S0 bus, and the USB stream follows it. (On the Line stick, the USB stream follows the codec in the same way) |
| S6 | Galvanic isolation from the S0 bus: the line transformers, ≥ 1500 Vrms |
| S7 | Protection against an incorrect cable: no damage from an analogue phone line (−48 V, ring signal 90 Vrms) or from a powered S0 bus (40 V) on any pins |
| S8 | Switchable 100 Ω terminations (point to point, or the end of a passive bus) |
| S9 | The same RP2040 core, USB-C and firmware structure as the other sticks; drivers on Linux, Windows (WinUSB, no INF) and Android |
| S10 | The discrete circuit and the firmware meet the transmitter requirements of I.430 (pulse mask, amplitude, impedance in the two states) and the receiver requirements (attenuation, jitter, bus configurations). The bench tests show this (§12) |

---

## 2. Block diagram

```
                       PIO0 SM0 ─► DA, DB, /OE ─► 74LVC2G125 ─ 2 × 100 Ω ─┐
 USB-C ── ESD ── RP2040                                                    TX ══ 1:2 ══╗
  (UFP)          │ core 1: I.430     PIO0 SM1 ◄─ RXP, RXN ◄─ TLV3202 ◄─────RX ══ 1:2 ══╣  TVS, PTC ── RJ45
  LDO 3.3 V      │ core 0: USB, HDLC          PWM ─► RC ─► Vref (threshold)            ║  (3/6 TX, 4/5 RX)
  flash, 12 MHz  ├── PhotoMOS ×2: 100 Ω terminations (line side)     isolation barrier
                 └── LEDs (LINK, B1, B2)            Vmid (1.65 V) ─► both chip-side centre taps
```

---

## 3. I.430 in brief (what the firmware must do)

- **The line:** 192 kbit/s in each direction, each on its own pair. The
  code is **pseudo-ternary**:
  - A binary one is no signal.
  - A binary zero is a full-width pulse (5.21 µs) with alternate polarity,
    **750 mV ±10 % peak into 50 Ω** (the two 100 Ω terminations of the bus
    in parallel).
- **Frames:** 48 bits, 4000 each second (250 µs). Each frame has two octets
  of B1, two octets of B2, 4 D bits, and framing and DC balance bits. From
  NT to TE, a frame also has the 4 E bits (the echo of the D bits that the
  NT received), the activation bit A, and the multiframe bits. A frame
  starts with the framing bit F and its balance bit L. The first zero after
  them breaks the alternation (a **code violation**). This is how a
  receiver finds the frame. A second violation follows in 14 bits or less
  (the FA or N bit).
- **The clock of the TE is the clock of the NT.** A TE recovers the bit
  clock from the frames of the NT. It sends its own frames **two bits
  behind** them.
- **From a TE, binary ones are high impedance.** Many TEs share the pair
  from TE to NT. A zero from one TE pulls the line, but ones from the other
  TEs do not. Thus the D-channel is a wired AND, and the NT sends its echo
  in the E bits.
- **D-channel access (TE):**
  1. Before you send a frame, count the consecutive ones in the E bits: 8
     for priority class 1 (signalling), 10 for class 2. Add one to each
     number after a successful frame.
  2. While you send, compare each D bit that you sent with the E bit that
     is its echo.
  3. If they are different, stop immediately (send ones) and start the
     count again.

  This is the one tight loop (§6.3).
- **Activation:**
  - INFO 0 (no signal).
  - INFO 1 (TE to NT, a request: a fixed pattern, not frame-locked).
  - INFO 2 (NT frames, B, D, E and A zero).
  - INFO 3 (TE frames, synchronised).
  - INFO 4 (NT frames with data, A one).

  TE states F1–F8, NT states G1–G4, with their timers (T3, T1).
- **D-channel frames** are HDLC: flags, zero insertion, the CCITT CRC-16,
  at 16 kbit/s.

---

## 4. The line interface

### 4.1 Transmit

- A **74LVC2G125** dual tri-state buffer (the separate enables are tied
  together: one /OE) drives the two ends of the chip-side winding of the
  transmit transformer, through **100 Ω** each. The centre tap of the
  winding is at Vmid (1.65 V, decoupled). For each bit, the PIO sets one
  of three states:
  - A high and B low (a positive zero).
  - A low and B high (a negative zero).
  - Both off (a binary one: high impedance).
- **Amplitude:** the transformer is 1:2 (line : chip). Thus 750 mV into
  50 Ω on the line is 1.5 V across 200 Ω on the chip side, 7.5 mA. From
  3.3 V, this leaves 1.8 V for the 2 × 100 Ω and the ~10 Ω of the buffer
  on each side: about 240 Ω in total. Thus 2 × 100 Ω is the start value.
  Adjust it on the bench (§12). The height of the pulse then changes with
  the 3.3 V rail (±3 %) and the resistors (1 %). The output resistance of
  the buffer (lower and more stable than an RP2040 pad) changes the total
  by some percent. The result is inside ±10 %.
- **The source impedance** while the buffer sends a zero is about
  240 Ω / 4 = 60 Ω, as the line sees it. In the third state, it is the
  magnetising inductance of the transformer and the off leakage of the
  buffer (high).
- **Pulse shape:** an RC across the line side (the 62 Ω and 1 nF of the
  reference circuit of Cologne Chip as a start) damps the undershoot of the
  transformer. Thus the pulse stays in the pulse mask of I.430.
- The centre tap is at Vmid. Thus, in the third state, while a different
  TE drives the pair, the ends of the winding stay inside the rails
  (1.65 ± 0.75 V). Thus the clamp diodes of the buffer never load the bus.

### 4.2 Receive

- The chip-side centre tap of the receive transformer is also at Vmid.
  Thus a pulse puts one end above Vmid and the other end below it.
- A **TLV3202** dual comparator (40 ns, rail-to-rail inputs, 3.3 V)
  compares each end with one reference, **Vref = Vmid + Vth**. RXP goes
  high on a positive pulse, and RXN goes high on a negative pulse. Each
  input has 1 kΩ in series and BAV99 clamps to the rails. The outputs give
  a small hysteresis.
- **Vth** is an RP2040 PWM output through a two-pole RC filter (about
  1 kHz corner). Thus the firmware sets it to about **half of the peak of
  the received pulse**. To measure the peak, the firmware increases Vth in
  steps until the pulses are not detected (on INFO 2 or 4 in TE mode,
  INFO 1 or 3 in NT mode). Then it tracks the peak. A short bus gives 1.5 V
  on the chip side. A long point-to-point line gives about half of that.
- The outputs of the comparators go directly to two GPIOs for the PIO
  (§6).

### 4.3 Transformer

The transformer is a dual 1:2 module, centre-tapped on the chip side, with
a common-mode choke: **UMEC UT20775-05TS** (1CT:2CT; the UT20795 has split
windings), or Pulse **PE-65795** (dual SMT, 1:2, 2000 Vrms). Both meet the
pulse template of I.430 with the circuits of the ISDN chips. Their 30 mH
line inductance keeps the impedance of the third state high. LCSC does not
have them. They come from DigiKey or Mouser, as consigned parts. A Pulse
part at LCSC would be better (§15).

### 4.4 Wiring, NT mode and terminations

- The RJ45 is wired as a TE: pins 3 (+) and 6 (−) transmit, 4 (+) and
  5 (−) receive.
- **NT mode** swaps the pairs. Rev A does this with a standard ISDN
  crossover cable (3↔4, 6↔5), and the firmware runs as the NT. To swap the
  pairs on the board, two latching DPDT relays are necessary (§15).
- **Terminations:** 100 Ω across each pair, each through a 60 V PhotoMOS
  relay (normally open). Thus the host can turn them on. The host turns
  them on in NT mode, and in TE mode on a point-to-point line with no other
  device at our end. Each relay uses 3 mA of LED current while it is on.

### 4.5 Protection

S0 is inside a building, but a person will connect a stick to an incorrect
socket at some time.

> **CAUTION: DO NOT CONNECT THE S0 STICK TO AN ANALOGUE TELEPHONE LINE.**
> The ring signal of a phone line (≈ 130 V peak on −48 V) is much more
> than the S0 bus voltage. The protection clamps it, but the PTC trips.

On the line side, each pair has these parts:
- A PTC (or a 0.5 A telecom fuse).
- A bidirectional TVS with a stand-off of 58 V (more than the 42 V of PS1).
  It clamps below the isolation of the transformer. It has a low
  capacitance (≤ 50 pF: the bus is a 100 Ω pair at 192 kbit/s).

Thus the TVS clamps the ring signal of a phone line, and the PTC trips. On
the chip side, there are the BAV99 clamps and series resistors of the
comparators, and the outputs of the buffer go through their 100 Ω.

Layout, as for the Line stick: the transformer module is the isolation
barrier. Only the PhotoMOS outputs also go across it. The PhotoMOS has its
own barrier with the correct rating. There is no ground plane below the
line side.

> **WARNING: KEEP THE CREEPAGE AND CLEARANCE ACROSS THE ISOLATION BARRIER
> AT ≥ 2.5 MM ON ALL LAYERS.** The barrier isolates the USB side from the
> line.

### 4.6 Power on the bus (PS1)

An NT can feed 40 V (34–42 V) to its terminals through the line-side
centre taps of the transformers. **Rev A does not feed this power and does
not use it.** In TE mode, the line-side centre taps are not connected.
Thus the 40 V of an NTBA has no effect. In NT mode, terminals with a local
power supply (TAs, ISDN cards, the TE ports of most PBXs) operate. ISDN
phones that get power from the bus need an external 40 V supply on the
line-side centre taps. §15 describes a variant that feeds power.

---

## 5. Clocks

- **System clock 96 MHz** from the 12 MHz crystal (PLL: VCO 1152 MHz,
  ÷6, ÷2). This gives exactly **500 cycles for each bit**. Thus the bit
  clock of the NT needs no dither. USB keeps its own 48 MHz PLL. (If
  core 1 needs more time, 192 MHz gives 1000 cycles for each bit: VCO
  1152, ÷6, ÷1. This is more than the original 133 MHz rating of the
  RP2040.)
- **NT mode:** the stick is the network clock: 192 000 Hz from the
  crystal, ±30 ppm, inside the ±100 ppm of I.430.
- **TE mode:** the transmit clock follows the clock that the receiver
  recovers from the frames of the NT (§6.2). The transmit program sets the
  length of each bit separately (500 cycles, ±1 to steer). Thus its phase
  follows the phase of the receiver in 10 ns steps.
- The B-channel octets come at the frame rate (4000 frames, each with two
  octets of each B-channel). The USB side counts them (§8.2), as the Line
  stick counts the samples of its codec.

---

## 6. Layer 1 in software

### 6.1 The PIO programs

| SM | Pins | |
|---|---|---|
| PIO0 SM0, transmit | out: DA, DB, /OE | each word from core 1 has some bits: for each bit, a symbol (+, −, one) and the length of the bit in cycles (500 ±1). The program sets the three pins and waits |
| PIO0 SM1, receive | in: RXP, RXN | samples the two comparators at 3.84 MHz (÷25), 20 times in each bit, packs 16 sample pairs into each word, DMA into a ring |
| PIO0 SM2 | — | a 1 MHz tick for timers, if core 1 needs one |

### 6.2 Core 1: receive

- The 20 sample pairs of each bit give the edges of the pulses. A digital
  PLL on the edges of the zeros (ones have no edges) holds the bit phase.
  The slicer takes each bit at its centre: positive zero, negative zero, or
  one.
- **Frame lock:** find the code violation after F and L, and examine the
  second violation. Lock after three good frames in sequence. Lose the lock
  after the time of two frames without a good frame. (These are the rules
  of I.430. They must be confirmed.)
- From each frame:
  - B1 and B2 octets go into the B rings (most significant bit first: the
    first bit on the line).
  - D bits go into the HDLC receiver.
  - E bits go to the access procedure (TE).
  - A and the multiframe bits go to the state machine.
- **NT mode:** the frames of the TEs come two bits after our frames, plus
  the delay of the bus. The PLL of the receiver tracks that phase, as it
  tracks the clock of the NT in TE mode.

### 6.3 Core 1: transmit and the D-channel's tight loop

- Core 1 pushes the transmit words only **some bits ahead**. Thus it can
  still change a D bit after its echo arrives. Core 1 sends a D bit. Its E
  echo comes back in the frame of the NT some bit times later. Then core 1
  decides the next D bit (or a one, on a collision). This takes about
  10 µs: 1000 cycles at 96 MHz.
- Core 1 counts the ones of the E bits for the priority class. It starts
  the bits of the frame when the count is reached, and it stops on a
  difference. Then core 0 puts the frame back at the head of the queue.
- In NT mode, core 1 makes the E bits from the D bits that it received in
  the same frame, as I.430 specifies. It sets A from the state machine.

### 6.4 Core 0

Core 0 does these functions:
- USB (TinyUSB, as on the other sticks).
- HDLC in the two directions on the D bits (flags, zero insertion,
  CRC-16).
- The queues of the D frames.
- The timers of the states.
- The termination relays.
- The threshold PWM.
- The LEDs.

### 6.5 Budget

Core 1 processes one bit each 500 cycles: about 1.25 sample words, the PLL
step, one slicer decision, one transmit symbol, and at the end of each
frame, the records of the frame. This is some hundred cycles. Measure it on
the first firmware. 192 MHz is the reserve.

GPIO map (preliminary):

| GPIO | |
|---|---|
| 0, 1, 2 | TX: DA, DB, /OE (PIO0 SM0, consecutive for `out pins, 3`) |
| 3, 4 | RX: RXP, RXN (PIO0 SM1) |
| 5 | Vth (PWM2 B) |
| 10, 11 | termination PhotoMOS: TX pair, RX pair |
| 12–14 | LEDs: LINK (layer 1 active), B1, B2 (a call on it) |
| 24, 25 | test pads (UART0, firmware log) |
| 29 | VBUS sense (ADC3), as on the other sticks |

---

## 7. Rest of the board and power

The board has these items:
- The RP2040 core of SPEC.md §4.2–4.4 (the chip, W25Q flash, 12 MHz
  crystal, AP2112K-3.3, USBLC6 ESD, USB-C with its CC resistors, a 0.5 A
  PTC).
- The BOOTSEL and RESET buttons inside.
- SWD test pads.
- Vmid from a divider on the 3.3 V rail, 10 µF and 100 nF.
- The RC of the threshold.

Budget:
- RP2040 at 96 MHz, with the two cores busy: about 25 mA.
- The transmitter: 7.5 mA while it sends zeros, some mA on average.
- The comparators: about 1 mA.
- PhotoMOS LEDs: 3 mA each.
- LEDs: 2 mA each.

**About 45 mA**, much less than the S1 limit.

---

## 8. USB interface (firmware)

The interface has the same shape as the interface of the Line stick
([firmware/PROTOCOL.md](firmware/PROTOCOL.md), "Line stick"). Thus its
rules for discovery, Windows and isochronous transfers apply here too. The
exact numbers go into PROTOCOL.md and `src/core/s0_proto.h` with the
firmware.

### 8.1 Finding it

- USB `1209:0004` (a pid.codes *test* PID, in addition to the `1209:0002`
  of the Line stick and the `1209:0003` of the WB stick, until a real PID
  is assigned), product "pico-serial S0". The `board` of `GET_INFO` is 5.
- Windows: WinUSB automatically, with the same BOS descriptor and
  Microsoft OS 2.0 set as the Line stick, and the `DeviceInterfaceGUID` of
  D-Modem.
- Interface 0: class `0xFF`, subclass `0x53` ('S'), protocol `0x01`.
  Interface 1: the reset interface of Raspberry Pi, for `picotool`.

### 8.2 Endpoints (alternate setting 1)

| Endpoint | Type | Max packet | |
|---|---|---|---|
| `0x01` OUT | isochronous, asynchronous, 1 ms | 24 bytes | B-channel octets to the line |
| `0x82` IN | isochronous, asynchronous, 1 ms | 24 bytes | B-channel octets from the line |
| `0x83` IN | interrupt, 4 ms | 8 bytes | events (§8.4) |
| `0x04` OUT | bulk | 64 | D-channel frames to the line |
| `0x85` IN | bulk | 64 | D-channel frames from the line |

- **B-channels:** pairs of octets, B1 then B2, one pair each 125 µs:
  8 pairs (16 bytes) in a nominal millisecond.
  - As on the Line stick, each IN packet has the pairs that the line gave
    after the last packet. Thus its length *is* the clock of the network
    (7, 8 or 9 pairs, a maximum of 12 to recover).
  - In each millisecond, the host sends the same number of pairs as the
    last IN packet had.
  - The stick sends them after a 2 ms head start. If it has no data, it
    sends ones (`0xFF`), counts an underrun and waits for its head start
    again.
  - A B-channel that is not switched on (`SET_B`) has ones in the two
    directions. The stick ignores the octets of the host for it.
  - Octets are in line order: the first bit on the line is the most
    significant bit of the octet (G.711 and I.460 order). There is no bit
    reversal on the host, as there is on the cards of mISDN.
- **D-channel:** one frame in each bulk transfer (a short packet, or a
  zero-length packet, ends it), from the address to the last information
  octet. There are no flags and no CRC. IN frames start with one status
  octet: 0 good, 1 bad CRC, 2 aborted, 3 overrun (the octets of the frame
  follow, for logging). A frame has a maximum of 264 octets (the N201 of
  LAPD of 260, plus the header).

### 8.3 Vendor requests

| Request | Dir | |
|---|---|---|
| `GET_INFO` | IN | the version, board 5 |
| `SET_MODE` | OUT | 0 TE, 1 NT; layer 1 deactivates first |
| `ACTIVATE`, `DEACTIVATE` | OUT | layer 1 (PH-ACTIVATE request); in NT mode `DEACTIVATE` sends INFO 0 |
| `GET_STATUS` | IN | the layer 1 state (F1–F8, G1–G4), the threshold and the measured peak, the termination switches, counters: frame slips, lost frame lock, code violations out of place, B underruns and overruns, D frames in each direction, bad CRCs, D collisions |
| `SET_B` | OUT | the B-channels that operate (bit 0 B1, bit 1 B2) |
| `SET_TERM` | OUT | the 100 Ω terminations (bit 0 TX pair, bit 1 RX pair) |
| `SET_THRESHOLD` | OUT | 0 automatic (default), or a fixed Vth in mV, for the bench |
| `LOOP` | OUT | test loops: B-channels back in the firmware, or the transmitter received by the receiver (with a loop plug) |
| `TEST_PATTERN` | OUT | the test signals of I.430 for the bench: continuous zeros (alternate pulses, for the pulse mask and amplitude), INFO 1, high impedance (for the impedance test) |
| `CAPTURE` | IN | the raw comparator samples of the receiver, some ms, for the host to examine |
| `REBOOT_UF2` | OUT | to the UF2 loader, as the other sticks |

### 8.4 Events (`0x83`)

The stick sends eight bytes when one of these items changes:
- The layer 1 state (activated, deactivated, lost frame lock, INFO 2 or
  INFO 4 received).
- A frame slip.
- A B-channel underrun or overrun.
- A D-channel queue overrun.
- A large movement of the threshold.

---

## 9. Host software

### 9.1 dmodemd

`dmodemd --usb` finds an S0 stick in addition to the SmartUSB56, the Line
stick and the WB stick. A new line backend, `dmodemd/s0_line.cpp`, follows
`misdn_line.cpp` (one call; Q.931 from `q931.cpp`; the bearer selects the
clear channel or a 3.1 kHz modem call). It has two differences:

- **LAPD in dmodemd.** On mISDN, the kernel has layer 2. Here, layer 2 is
  `dmodemd/lapd.cpp`: Q.921 on the D-channel frames of the stick, TEI
  assignment (automatic, on TEI 127 / SAPI 63), the multiple frame
  operation of SAPI 0, T200, N200, T203. `link/lapb.cpp` already has the
  extended mode of Q.921 for V.120 (SABME, modulo 128, the timers). The
  LAPD code uses this as its base.
- **No bit reversal**: the stick sends octets in line order (§8.2).

Then, in the sequence of use:
- **Two B-channels**: two calls at the same time (two ptys). `--misdn` does
  not do this at this time.
- **NT mode**: the network side of Q.931 and the TEI assigner of LAPD.
  Thus an ISDN terminal adapter, an ISDN card in a vintage PC, or the ISDN
  hardware of a BBS can dial into D-Modem: the stick is the exchange. The
  firmware can switch a call between two terminals on the bus from
  B-channel to B-channel, or it can route the call to SIP.
- **CAPI**: `dmodem-capid` (phase 4 of the ISDN work) drives a D-Modem
  through its DTE with AT commands, above all line backends. Thus the stick
  is a CAPI controller on all hosts where dmodemd runs. The stick and
  `s0_line.cpp` need nothing for this.

### 9.2 dmodem-android

Through the USB host API of Android and libusb, as the Line stick. Android
has no ISDN stack. With this stick, a phone can dial ISDN data calls.

### 9.3 mISDN (later, optional)

A small Linux kernel driver for the interface of §8 can make the stick an
mISDN device. This is for the other users of mISDN (chan_misdn of
Asterisk, LCR, isdn utilities) and for `dmodemd --misdn`. D-Modem does not
need it: the userspace path operates on all hosts.

### 9.4 Testing without hardware

A `fake_s0` device for the tests of dmodemd (as `fake_st7554` and
`fake_misdn`):
1. Two dmodemd instances, one in TE mode and one in NT mode, through the
   D- and B-channels of the fake device.
2. Then one stick in each mode, back to back with a crossover cable.

---

## 10. Mechanical

The stick uses the shell of the Line stick (`hardware/enclosure/stick.py`),
with an RJ45 in place of the RJ11. The RJ45 is about 16 mm wide and
13–14 mm tall. Thus the stick is about 2 mm wider than the Line stick, and
it has the same thickness. A mid-mount jack can make it thinner. There are
light pipes for the three LEDs.

---

## 11. Parts (to check against stock before the netlist)

| Role | Part | Source | Package | Notes |
|---|---|---|---|---|
| Transmit buffer | TI SN74LVC2G125 | LCSC | VSSOP-8 / SOT-23-8 | the two /OE tied; 3.3 V |
| Transmit resistors | 100 Ω 1 % ×2 | LCSC | 0603 | adjusted on the bench |
| Comparator | TI TLV3202 (dual, 40 ns, rail to rail) | LCSC, to confirm | VSSOP-8 | or a dual of the same class |
| Threshold filter | 2 × (10 kΩ, 100 nF) | LCSC | 0603 | PWM to Vref |
| Clamps | BAV99 ×2 | LCSC C2500 | SOT-23 | comparator inputs |
| Transformer | UMEC UT20775-05TS, or Pulse PE-65795 | DigiKey / Mouser | dual SMT | 1:2, chip-side centre tap, common-mode choke |
| Line TVS ×2 | bidirectional, 58 V stand-off, low capacitance | LCSC, to select | SMB or smaller | |
| PTC or fuse ×2 | telecom rated | LCSC, to select | | |
| Terminations | 100 Ω ×2 through 60 V PhotoMOS ×2 | LCSC, to select (AQY212 or TLP172A class) | SOP-4 | |
| RJ45 | unshielded, no magnetics, 8P8C | LCSC, to select | THT | latch toward the top |
| RP2040 core | as SPEC.md §3 | LCSC | | |

---

## 12. Showing it meets I.430 (the bench)

The `TEST_PATTERN` and `CAPTURE` requests of the firmware (§8.3) are for
these tests. Do the tests in this sequence:

1. **Transmitter:** send continuous zeros into a 50 Ω load. On a scope,
   measure the amplitude (750 mV ±10 %) and compare the pulse with the mask
   of I.430. Adjust the resistors and the RC until the two pass with
   margin, at 3.2 and 3.4 V.
2. **Impedance:** measure the impedance of the transmitter in the high
   impedance state and of the receiver. Compare them with the templates of
   I.430. Use a network analyser or a bridge across the band. Do this for
   the two TX states.
3. **Receiver:** use a second stick in NT mode as the source, through a
   cable simulator (or real cable on a drum). Test a short passive bus with
   8 TE loads, an extended passive bus, and a point-to-point line at the
   maximum length of I.430. Measure jitter and frame lock.
4. **D-channel access:** connect two sticks as TEs on one bus to a third
   stick as NT. Let the two TEs send at the same time. The collisions must
   be resolved, and no frame must be lost.
5. **The real equipment:**
   - An NTBA on an ISDN line, where one still exists.
   - Then the S0 ports of PBXs (Auerswald, the FRITZ!Box models of AVM with
     an internal S0).
   - In NT mode: ISDN TAs, an AVM FRITZ!Card and an HFC-based card with
     mISDN.

   An HFC card on mISDN is also the reference: it can be the NT or the TE
   opposite the stick.

---

## 13. The fallback: an XHFC-1SU

If the soft S0 fails the bench tests, a chip must do the line function.
The **XHFC-1SU** (LQFP-64) of Cologne Chip is the one S/T controller that
is still in production ("recommended for new projects", sold through
Cologne Chip, not LCSC). It does TE and NT to I.430 and TBR 3. It has an
HDLC controller that does the CRC and the access procedure of the
D-channel, a PCM bus and SPI. With this chip:

- The line interface becomes the reference circuit of Cologne Chip
  (XHFC-2SU datasheet §5.2.8): the same 1:2 transformers, the receive
  divider network, and a BC848 pair that makes V_AMP for the transmitter.
- The B-channels go on its PCM bus (PCM30, slots 1 and 2) into a PIO
  program. The frames of the D-channel go through SPI. A 24.576 MHz
  oscillator is necessary.
- The USB interface (§8) does not change. Thus the host does not see a
  difference.

The [xhfc-breakout](https://gitea.osmocom.org/electronics/osmo-small-hardware)
of Osmocom (the two-port XHFC-2SU) is a reference design that operates.

Other S/T chips (SBCX and IPAC-X of Infineon, TP3420A of TI, MC145574 of
Motorola, W6691 of Winbond, HFC-S USB of Cologne Chip) are end of life, or
only brokers have them.

---

## 14. Compliance (only if you sell it)

Terminal equipment on a public ISDN must meet these rules:
- In Europe: the Radio Equipment and Low Voltage rules with EN 62368-1, and
  TBR 3 conformance for the S/T interface. (A soft S0 must have this test
  from the start.)
- In other countries: the rules of each country.

NT mode makes only a private bus. If you build a stick for yourself, the
risk is yours. Use it on your own NTBA or PBX.

---

## 15. Open questions

1. **A transformer at LCSC:** a Pulse ISDN S module of the T50xx family
   with 1:2 and a chip-side centre tap. Then JLC can assemble the full
   board.
2. **The real cycle count of core 1** (§6.5): 96 MHz or 192 MHz.
3. **The receiver on long lines:** is a sliced threshold sufficient at the
   far end of a point-to-point line? Or does the slicer need the shape of
   the received pulse (an equaliser on the samples), as the adaptive
   receivers of the ISDN chips do?
4. **NT on the board in place of a crossover cable:** two latching DPDT
   signal relays (pulsed: no standing current) on the line side swap the
   pairs, at 2 × 9 × 14 mm. This is useful if the stick is mostly used as
   an NT.
5. **Power feed (PS1) in NT mode:** a 5 V → 40 V boost (about 1 W for one
   phone), only on a host that gives 500 mA or more.

## 16. Next steps

1. Firmware first, on a Pico on a breadboard with the transformer, the
   buffer and the comparators. Connect two Picos, TE and NT, back to back.
   Then test against an HFC card on mISDN.
2. Do the bench tests of §12, 1–3, on that rig.
3. Netlist, schematic and PCB (`--variant s0`), and the shell (`stick.py`
   with an RJ45).
4. D-Modem: `fake_s0`, `lapd.cpp`, `s0_line.cpp`. Then test a stick on an
   NTBA or on the S0 port of a PBX.
