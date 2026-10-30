<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 D-Modem Authors -->
# pico-serial S0 — ISDN basic rate stick (draft 0.2)

A USB stick with an **ISDN S/T (S0) port**: an RJ45 socket at one end,
USB-C at the other, the Line stick's shape. It plugs into an NTBA or a
PBX's internal S0 port as an ISDN terminal (TE), or, the other way round,
it is the network (NT) for ISDN terminal adapters, ISDN cards and phones.

As with the Line stick ([SPEC-L.md](SPEC-L.md)), the stick is only the
line interface, and here even its layer 1 is software: there is **no
ISDN chip**. The RP2040's PIO drives and samples the S0 bus through a
transformer pair, a tri-state buffer and two comparators, and its second
core runs I.430: the line code, the clock and frame recovery, the
D-channel's access procedure and HDLC. The stick streams both B-channels
and the D-channel's frames over USB. Everything above, LAPD, Q.931, the
clear channel modes (X.75, V.110, V.120, sync PPP), every analogue
modulation on a 3.1 kHz call, and CAPI, is **D-Modem on the host**
(`dmodemd`, dmodem-android). D-Modem already has those parts for Linux
mISDN cards (`dmodemd --misdn`, D-Modem's `docs/isdn.md`); this stick
brings them to every host D-Modem runs on, Android and Windows included,
where there is no mISDN.

Every part is at LCSC except, so far, the S0 transformer (§11). The
cost is that I.430 conformance is ours to show on the bench (§12). The
one S/T chip still made, Cologne Chip's XHFC-1SU, is the fallback
(§13).

Not covered: the U interface (the exchange's 2B1Q or 4B3T pair to the
NTBA: the FPGA board, [SPEC-FX.md](SPEC-FX.md)), primary rate (S2M/E1),
and power feeding ISDN phones in NT mode on rev A (§4.6).

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
| S10 | I.430's transmitter (pulse mask, amplitude, impedance in both states) and receiver (attenuation, jitter, bus configurations) met by the discrete circuit and the firmware, and shown on the bench (§12) |

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

- **The line:** 192 kbit/s each way on its own pair, **pseudo-ternary**:
  a binary one is no signal, a binary zero a full-width pulse (5.21 µs)
  of alternating polarity, **750 mV ±10 % peak into 50 Ω** (the bus's two
  100 Ω terminations in parallel).
- **Frames:** 48 bits, 4000 a second (250 µs). Each carries two octets of
  B1, two of B2, 4 D bits, and framing and DC balance bits; NT to TE also
  the 4 E bits (the echo of the D bits the NT received), the activation
  bit A, and the multiframe bits. A frame starts with the framing bit F
  and its balance bit L, and the first zero after them breaks the
  alternation (a **code violation**); that is how a receiver finds the
  frame. A second violation follows within 14 bits (the FA or N bit).
- **The TE's clock is the NT's.** A TE recovers the bit clock from the
  NT's frames and sends its own frames **two bits behind** them.
- **Binary ones are high impedance from a TE.** Several TEs share the TE
  to NT pair; a zero from any of them pulls the line, ones from the
  others don't. So the D-channel is a wired AND, and the NT echoes it in
  the E bits.
- **D-channel access (TE):** before sending a frame, count consecutive
  ones in the E bits: 8 for priority class 1 (signalling), 10 for class 2,
  each one more after a successful frame. While sending, compare each D
  bit sent with the E bit that echoes it; on a difference, stop at once
  (send ones) and start counting again. This is the one tight loop
  (§6.3).
- **Activation:** INFO 0 (no signal), INFO 1 (TE to NT, asking: a fixed
  pattern, not frame-locked), INFO 2 (NT frames, B, D, E and A zero),
  INFO 3 (TE frames, synchronised), INFO 4 (NT frames with data, A one).
  TE states F1–F8, NT states G1–G4, with their timers (T3, T1).
- **D-channel frames** are HDLC: flags, zero insertion, the CCITT CRC-16,
  at 16 kbit/s.

---

## 4. The line interface

### 4.1 Transmit

- A **74LVC2G125** dual tri-state buffer (separate enables tied
  together: one /OE) drives the two ends of the transmit transformer's
  chip-side winding through **100 Ω** each; the winding's centre tap sits
  at Vmid (1.65 V, decoupled). The PIO sets three states each bit: A high
  and B low (a positive zero), A low and B high (a negative zero), or
  both off (a binary one: high impedance).
- **Amplitude:** the transformer is 1:2 (line : chip), so 750 mV into
  50 Ω on the line is 1.5 V across 200 Ω on the chip side, 7.5 mA. From
  3.3 V that leaves 1.8 V for the 2 × 100 Ω and the buffer's own ~10 Ω
  each: about 240 Ω in all, so 2 × 100 Ω is the starting value, to be
  trimmed on the bench (§12). The pulse's height then depends on the 3.3 V
  rail (±3 %) and the resistors (1 %); the buffer's output resistance
  (lower and steadier than an RP2040 pad's) varies by a few percent of the
  total. Inside ±10 %.
- **The source impedance** while sending a zero is about 240 Ω / 4 = 60 Ω
  as the line sees it; in the third state it is the transformer's
  magnetising inductance and the buffer's off leakage (high).
- **Pulse shape:** an RC across the line side (the 62 Ω and 1 nF of
  Cologne Chip's reference circuit as a start) damps the transformer's
  undershoot into I.430's pulse mask.
- With the centre tap at Vmid, the winding's ends stay inside the rails
  in the third state while another TE drives the pair (1.65 ± 0.75 V), so
  the buffer's clamp diodes never load the bus.

### 4.2 Receive

- The receive transformer's chip-side centre tap is also at Vmid, so a
  pulse puts one end above Vmid and the other below it.
- A **TLV3202** dual comparator (40 ns, rail-to-rail inputs, 3.3 V)
  compares each end with one reference, **Vref = Vmid + Vth**: RXP goes
  high on a positive pulse, RXN on a negative one. 1 kΩ in series and
  BAV99 clamps to the rails at each input; a little hysteresis from the
  outputs.
- **Vth** is an RP2040 PWM output through a two-pole RC filter (about
  1 kHz corner), so the firmware sets it: about **half the received
  pulse's peak**, which it measures by stepping Vth up until pulses drop
  out (on INFO 2 or 4 in TE mode, INFO 1 or 3 in NT mode) and then
  tracks. A short bus brings 1.5 V on the chip side, a long point-to-point
  line about half that.
- The comparators' outputs go straight to two GPIOs for the PIO (§6).

### 4.3 Transformer

A dual 1:2 module, centre-tapped on the chip side, with a common-mode
choke: **UMEC UT20775-05TS** (1CT:2CT; the UT20795 has split windings),
or Pulse **PE-65795** (dual SMT, 1:2, 2000 Vrms). Both meet I.430's pulse
template with the ISDN chips' circuits, and their 30 mH line inductance
keeps the third state's impedance high. Neither is at LCSC: from DigiKey
or Mouser, consigned. A Pulse part at LCSC would be better (§15).

### 4.4 Wiring, NT mode and terminations

- The RJ45 is wired as a TE: pins 3 (+) and 6 (−) transmit, 4 (+) and
  5 (−) receive.
- **NT mode** swaps the pairs: rev A does it with a standard ISDN
  crossover cable (3↔4, 6↔5), and the firmware runs as the NT. Swapping
  on the board needs two latching DPDT relays (§15).
- **Terminations:** 100 Ω across each pair, each through a 60 V
  PhotoMOS relay (normally open), so the host turns them on: in NT
  mode, and in TE mode on a point-to-point line with nothing else at our
  end. 3 mA of LED current each while on.

### 4.5 Protection

S0 is inside a building, but a stick will meet the wrong socket. On the
line side, per pair: a PTC (or a 0.5 A telecom fuse) and a bidirectional
TVS that stands off 58 V (above PS1's 42 V) and clamps below the
transformer's isolation, low capacitance (≤ 50 pF: the bus is a 100 Ω
pair at 192 kbit/s). A phone line's ringing (≈ 130 V peak on −48 V) is
then clamped and the PTC trips. On the chip side, the comparators' BAV99
clamps and series resistors; the buffer's outputs through their 100 Ω.

Layout, as for the Line stick: the isolation barrier is the transformer
module; nothing else crosses it except the PhotoMOS outputs, whose own
barrier is rated for it. Creepage and clearance across it ≥ 2.5 mm on
every layer; no ground plane under the line side.

### 4.6 Power on the bus (PS1)

An NT may feed 40 V (34–42 V) to its terminals through the transformers'
line-side centre taps. **Rev A neither feeds nor draws it.** In TE mode
the line-side centre taps are left free, so an NTBA's 40 V does nothing.
In NT mode, locally powered terminals (TAs, ISDN cards, most PBXs' TE
ports) work; ISDN phones powered by the bus need an external 40 V supply
on the line-side centre taps. A feeding variant is §15.

---

## 5. Clocks

- **System clock 96 MHz** from the 12 MHz crystal (PLL: VCO 1152 MHz,
  ÷6, ÷2): **500 cycles a bit**, exactly, so the NT's bit clock needs no
  dither. USB keeps its own 48 MHz PLL. (192 MHz gives 1000 cycles a bit
  if core 1 needs more room: VCO 1152, ÷6, ÷1, past the RP2040's
  original 133 MHz rating.)
- **NT mode:** the stick is the network clock: 192 000 Hz from the
  crystal, ±30 ppm, inside I.430's ±100 ppm.
- **TE mode:** the transmit clock follows the clock recovered from the
  NT's frames (§6.2). The transmit program's bit length is set bit by bit
  (500 cycles, ±1 to steer), so its phase follows the receiver's in 10 ns
  steps.
- The B-channel octets come at the frame rate (4000 frames, two octets
  of each B-channel each), and the USB side counts them (§8.2), as the
  Line stick counts its codec's samples.

---

## 6. Layer 1 in software

### 6.1 The PIO programs

| SM | Pins | |
|---|---|---|
| PIO0 SM0, transmit | out: DA, DB, /OE | each word from core 1 holds a few bits: per bit, a symbol (+, −, one) and the bit's length in cycles (500 ±1). The program sets the three pins and waits |
| PIO0 SM1, receive | in: RXP, RXN | samples both comparators at 3.84 MHz (÷25), 20 times a bit, packs 16 sample pairs into each word, DMA into a ring |
| PIO0 SM2 | — | a 1 MHz tick for timers, if core 1 wants one |

### 6.2 Core 1: receive

- Each bit's 20 sample pairs give the pulses' edges. A digital PLL on the
  edges of the zeros (ones carry none) holds the bit phase, and the
  slicer takes each bit at its centre: positive zero, negative zero, or
  one.
- **Frame lock:** find the code violation after F and L, check the
  second violation, then lock after three good frames in a row and lose
  lock after two frames' time without one (I.430's rules, to check).
- Out of each frame: B1 and B2 octets into the B rings (most significant
  bit first: the first bit on the line), D bits into the HDLC receiver,
  E bits to the access procedure (TE), A and the multiframe bits to the
  state machine.
- **NT mode:** the TEs' frames come two bits after ours, plus the bus's
  delay; the receiver's PLL tracks that phase as it tracks the NT's clock
  in TE mode.

### 6.3 Core 1: transmit and the D-channel's tight loop

- The transmit words are pushed **a few bits ahead** only, so that a D
  bit can still be changed after its echo arrives: a D bit is sent, its E
  echo comes back in the NT's frame a few bit times later, and the next D
  bit (or a one, on a collision) is decided then. About 10 µs: 1000 cycles
  at 96 MHz.
- Core 1 counts the E bits' ones for the priority class, starts the
  frame's bits when the count is reached, and stops on a difference;
  core 0 then puts the frame back at the head of the queue.
- In NT mode, core 1 builds the E bits from the D bits it received in the
  same frame, as I.430 lays them out, and sets A from the state machine.

### 6.4 Core 0

USB (TinyUSB, as on the other sticks), HDLC in both directions on the D
bits (flags, zero insertion, CRC-16), the D frames' queues, the states'
timers, the termination relays, the threshold PWM, the LEDs.

### 6.5 Budget

Core 1 handles one bit every 500 cycles: about 1.25 sample words, the
PLL step, one slicer decision, one transmit symbol, and at frame ends the
frame's bookkeeping. A few hundred cycles; to measure on the first
firmware, with 192 MHz as the reserve.

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

The RP2040 core of SPEC.md §4.2–4.4 (the chip, W25Q flash, 12 MHz
crystal, AP2112K-3.3, USBLC6 ESD, USB-C with its CC resistors, a 0.5 A
PTC), the BOOTSEL and RESET buttons inside, SWD test pads. Vmid from a
divider on the 3.3 V rail, 10 µF and 100 nF; the threshold's RC.

Budget: RP2040 at 96 MHz, both cores busy, about 25 mA; the transmitter
7.5 mA while sending zeros, a few mA on average; the comparators about
1 mA; PhotoMOS LEDs 3 mA each; LEDs 2 mA each. **About 45 mA**, well
inside S1.

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
  brought. The stick sends them after a 2 ms head start; dry, it sends
  ones (`0xFF`), counts an underrun and waits for its head start again.
  A B-channel not switched on (`SET_B`) carries ones both ways and the
  host's octets for it are ignored. Octets are in line order: the first
  bit on the line is the octet's most significant (G.711 and I.460
  order; no bit reversal on the host, unlike mISDN's cards).
- **D-channel:** one frame per bulk transfer (ended by a short packet, or
  a zero-length one), address to the last information octet: no flags,
  no CRC. IN frames start with one status octet: 0 good, 1 bad CRC,
  2 aborted, 3 overrun (the frame's octets follow for logging). Up to
  264 octets (LAPD's N201 of 260 plus the header).

### 8.3 Vendor requests

| Request | Dir | |
|---|---|---|
| `GET_INFO` | IN | the version, board 5 |
| `SET_MODE` | OUT | 0 TE, 1 NT; layer 1 deactivates first |
| `ACTIVATE`, `DEACTIVATE` | OUT | layer 1 (PH-ACTIVATE request); in NT mode `DEACTIVATE` sends INFO 0 |
| `GET_STATUS` | IN | the layer 1 state (F1–F8, G1–G4), the threshold and the measured peak, the termination switches, counters: frame slips, lost frame lock, code violations out of place, B underruns and overruns, D frames each way, bad CRCs, D collisions |
| `SET_B` | OUT | which B-channels flow (bit 0 B1, bit 1 B2) |
| `SET_TERM` | OUT | the 100 Ω terminations (bit 0 TX pair, bit 1 RX pair) |
| `SET_THRESHOLD` | OUT | 0 automatic (default), or a fixed Vth in mV, for the bench |
| `LOOP` | OUT | test loops: B-channels back in the firmware, or the transmitter heard by the receiver (with a loop plug) |
| `TEST_PATTERN` | OUT | I.430's test signals for the bench: continuous zeros (alternating pulses, for the pulse mask and amplitude), INFO 1, high impedance (for the impedance test) |
| `CAPTURE` | IN | the receiver's raw comparator samples, a few ms, for the host to look at |
| `REBOOT_UF2` | OUT | to the UF2 loader, as the other sticks |

### 8.4 Events (`0x83`)

Eight bytes when something changes: the layer 1 state (activated,
deactivated, lost frame lock, INFO 2 or INFO 4 seen), a frame slip, a
B-channel underrun or overrun, a D-channel queue overrun, the threshold
moved far.

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
- **No bit reversal**: the stick sends octets in line order (§8.2).

Then, in order of use:
- **Two B-channels**: two calls at once (two ptys), which `--misdn`
  doesn't do yet.
- **NT mode**: Q.931's network side and LAPD's TEI assigner, so an ISDN
  terminal adapter, an ISDN card in a vintage PC, or a BBS's ISDN
  hardware dials into D-Modem: the stick is the exchange. A call between
  two terminals on the bus can be switched B-channel to B-channel in the
  firmware, or routed to SIP.
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
| Transmit buffer | TI SN74LVC2G125 | LCSC | VSSOP-8 / SOT-23-8 | both /OE tied; 3.3 V |
| Transmit resistors | 100 Ω 1 % ×2 | LCSC | 0603 | trimmed on the bench |
| Comparator | TI TLV3202 (dual, 40 ns, rail to rail) | LCSC, to confirm | VSSOP-8 | or a dual of the same class |
| Threshold filter | 2 × (10 kΩ, 100 nF) | LCSC | 0603 | PWM to Vref |
| Clamps | BAV99 ×2 | LCSC C2500 | SOT-23 | comparator inputs |
| Transformer | UMEC UT20775-05TS, or Pulse PE-65795 | DigiKey / Mouser | dual SMT | 1:2, chip-side centre tap, common-mode choke |
| Line TVS ×2 | bidirectional, 58 V stand-off, low capacitance | LCSC, to choose | SMB or smaller | |
| PTC or fuse ×2 | telecom rated | LCSC, to choose | | |
| Terminations | 100 Ω ×2 through 60 V PhotoMOS ×2 | LCSC, to choose (AQY212 or TLP172A class) | SOP-4 | |
| RJ45 | unshielded, no magnetics, 8P8C | LCSC, to choose | THT | latch toward the top |
| RP2040 core | as SPEC.md §3 | LCSC | | |

---

## 12. Showing it meets I.430 (the bench)

The firmware's `TEST_PATTERN` and `CAPTURE` (§8.3) are for this. In
order:

1. **Transmitter:** continuous zeros into a 50 Ω load: the amplitude
   (750 mV ±10 %) and the pulse against I.430's mask, on a scope; the
   resistors and the RC trimmed until both pass with margin, at 3.2 and
   3.4 V.
2. **Impedance:** the transmitter's in the high impedance state and the
   receiver's, against I.430's templates, with a network analyser or a
   bridge across the band; both TX states.
3. **Receiver:** a second stick in NT mode as the source, through a cable
   simulator (or real cable on a drum): a short passive bus with 8 TE
   loads, an extended passive bus, a point-to-point line to I.430's
   longest; jitter and frame lock.
4. **D-channel access:** two sticks as TEs on one bus to a third as NT,
   both sending at once: collisions resolved, no frame lost.
5. **The real thing:** an NTBA on an ISDN line where one still exists,
   then the S0 ports of PBXs (Auerswald, AVM's FRITZ!Box models with an
   internal S0), and, in NT mode, ISDN TAs, an AVM FRITZ!Card and an
   HFC-based card with mISDN. An HFC card on mISDN is also the
   reference: it can be the NT or the TE across from the stick.

---

## 13. The fallback: an XHFC-1SU

If the soft S0 fails the bench, the line goes to a chip. Cologne
Chip's **XHFC-1SU** (LQFP-64) is the one S/T controller still in
production ("recommended for new projects", sold through Cologne Chip,
not LCSC): TE and NT to I.430 and TBR 3, an HDLC controller doing the
D-channel's CRC and access procedure, a PCM bus and SPI. With it:

- the line interface becomes Cologne Chip's reference circuit (XHFC-2SU
  datasheet §5.2.8): the same 1:2 transformers, the receive divider
  network, a BC848 pair making V_AMP for the transmitter;
- B-channels on its PCM bus (PCM30, slots 1 and 2) into a PIO program, the
  D-channel's frames over SPI; a 24.576 MHz oscillator;
- the USB interface (§8) unchanged, so the host never knows.

Osmocom's [xhfc-breakout](https://gitea.osmocom.org/electronics/osmo-small-hardware)
(the two-port XHFC-2SU) is a working reference.

Other S/T chips (Infineon's SBCX and IPAC-X, TI's TP3420A, Motorola's
MC145574, Winbond's W6691, Cologne Chip's HFC-S USB) are end of life or
found only at brokers.

---

## 14. Compliance (only if you sell it)

Terminal equipment on a public ISDN: in Europe the Radio Equipment and
Low Voltage rules with EN 62368-1, TBR 3 conformance for the S/T
interface (a soft S0 has to be tested for it from scratch); elsewhere
each country's own. NT mode makes a private bus only. Building your own:
at your own risk, on your own NTBA or PBX.

---

## 15. Open questions

1. **A transformer at LCSC:** a Pulse ISDN S module of the T50xx family
   with 1:2 and a chip-side centre tap, so the whole board is assembled.
2. **Core 1's real cycle count** (§6.5): 96 MHz or 192 MHz.
3. **The receiver on long lines:** whether a sliced threshold is enough
   at the far end of a point-to-point line, or the slicer wants the
   received pulse's shape (an equaliser on the samples) as the ISDN
   chips' adaptive receivers do.
4. **NT on the board instead of a crossover cable:** two latching DPDT
   signal relays (pulsed: no standing current) on the line side swap the
   pairs, at 2 × 9 × 14 mm. Worth it if the stick is mostly used as an NT.
5. **Power feeding (PS1) in NT mode:** a 5 V → 40 V boost (about 1 W for
   one phone), only on a host that offers 500 mA or more.

## 16. Next steps

1. Firmware first, on a Pico on a breadboard with the transformer, the
   buffer and the comparators: two Picos, TE and NT, back to back;
   then against an HFC card on mISDN.
2. The bench tests of §12, 1–3, on that rig.
3. Netlist, schematic and PCB (`--variant s0`), the shell (`stick.py`
   with an RJ45).
4. D-Modem: `fake_s0`, `lapd.cpp`, `s0_line.cpp`; then a stick on an NTBA
   or a PBX's S0 port.
