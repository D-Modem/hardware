<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 D-Modem Authors -->
# pico-serial FX — FPGA line board for ADSL or ISDN (study, draft 0.0)

One board whose line personality is set by its gateware and the host's
software: an **ADSL / ADSL2+** modem (or the exchange end for vintage
ADSL modems), an **ISDN U** line (2B1Q or 4B3T, either end), an **ISDN
S0** port, or a faster successor to the WB stick's bare-pair mode
([SPEC-WB.md](SPEC-WB.md), whose §9 left ADSL for "a different chip").

The idea is the one behind every D-Modem stick, pushed further: the board
is a **wideband line interface** (a fast ADC and DAC, a line driver, a
transformer) and an FPGA that moves samples to USB 2.0 High Speed and
handles bit timing where it matters. The modem, here a DMT transceiver
or a 2B1Q echo canceller, runs in **D-Modem on the host**, as V.92 does
for the Line stick.

Status: a feasibility study. Nothing here is checked against stock to
the depth of the other specs; part numbers are candidates.

---

## 1. What it would do

| Mode | Port | In the FPGA | On the host (D-Modem) |
|---|---|---|---|
| ADSL, ADSL2+ ATU-R (a customer modem, to a DSLAM) | line | samples at 2.208 / 4.416 MHz, clock tracking | DMT (G.992.1/.3/.5), G.994.1 handshake, training, trellis, Reed-Solomon, interleaving; ATM (AAL5, RFC 2684) or PTM; PPPoE/PPPoA to a network interface |
| ADSL ATU-C (the exchange end, for vintage ADSL modems and routers) | line | the same, more transmit power | the same, the other side of each procedure |
| ISDN U, NT1 or LT side | line | samples (about 1 MHz) | 2B1Q (80 kbaud, ETSI TS 102 080 annex A, ANSI T1.601) or 4B3T (120 kbaud, annex B, Germany); echo canceller; then LAPD and Q.931 as for the S0 stick |
| ISDN S0, TE or NT | S0 | I.430's line code, framing, D-channel access (the S0 stick's soft S0, [SPEC-S0.md](SPEC-S0.md) §3–6, in logic instead of PIO) | as the S0 stick (SPEC-S0 §9) |
| Bare pair, wideband (WB's mode, more tones) | line | samples | D-Modem's WB pump with tones to 1 MHz or more |
| SHDSL (G.991.2) | line | samples | later, if wanted |

**"Depending on the firmware"** comes down to this: one gateware does
every mode (a sample streamer at a set rate, plus the S0 framer), and the
host picks the mode. The gateware updates over USB (DFU into its SPI
flash, with a protected golden image to fall back to), so a mode that
later needs work in logic (a hardware FFT, say) is a gateware update,
not a new board.

### 1.1 ADSL and ISDN U on the same pair

Both are two-wire, echo-cancelled or frequency-split signals on a
telephone pair, below 2.2 MHz. One line front end serves both: the
converters run at whatever rate the mode needs, and the transformer and
hybrid cover 10 kHz to 2.2 MHz. The S0 port is different (four wires,
192 kbit/s pulses, a bus) and gets its own small front end and jack.

### 1.2 Where these lines still are

ISDN and ADSL are being switched off in many countries (Germany's ISDN
was largely gone by 2022; ADSL is following fibre and VDSL out). So the board's
most lasting use is likely **being the exchange**: the ATU-C for an old
ADSL router, the LT for an NTBA (which then gives S0 and power to ISDN
phones, the PS1 problem of SPEC-S0 §4.6), or NT mode for terminal
adapters, all with D-Modem behind it. ATU-R mode still matters where
ADSL is still sold, and that is also the hardest interoperability test
(§6).

---

## 2. Block diagram

```
                      ┌─ ULPI ── USB3320 ── USB-C (High Speed)
                      │
 SPI flash ── FPGA ───┼─ 12-bit ADC/DAC bus ── AD9866 ── TX ─► THS6222 ─┐
 (gateware,  ECP5-25  │  (AFE: ADC, DAC, PGAs)    ◄── RX ── hybrid ◄────┤
  golden)             │                                                 ═══ xDSL transformer ═══ DC block ─ fuse ─ sidactor ─ RJ11 (line)
                      ├─ Si5351 (I2C): sample clock, tuned to track the far end
                      └─ S0 front end: driver + 2 comparators ══ 1:2 transformers ══ RJ45 (S0)
                         5 V ─► boost 12 V (line driver)  ─► 3.3 / 2.5 / 1.1 V (FPGA, AFE)
```

---

## 3. Choosing the FPGA

The FPGA's job is modest in the default gateware: a USB 2.0 HS device
core (ULPI), FIFOs, the converter bus at up to about 35 MHz, an I2C
master, the S0 framer. A few thousand LUTs. What sets the size is
headroom: moving the DMT's FFTs, trellis or Reed-Solomon into logic
later (for a slow host, or Android) wants multipliers and block RAM.

| FPGA | Logic | DSP | Toolchain | Price, availability | |
|---|---|---|---|---|---|
| **Lattice ECP5 LFE5U-25F** (caBGA-256, 0.8 mm) | 24k LUT | 28 18×18 multipliers, 1 Mbit EBR | **open and mature**: Yosys, nextpnr-ecp5, Project Trellis; LiteX; LUNA's USB HS core | about $10–20; LCSC stocks the ECP5 family (the 45F at $17) | **recommended** |
| Cologne Chip GateMate CCGM1A1 (FBGA-324) | 20k LE | multipliers built from logic cells | open: Yosys + nextpnr (himbaechel), Cologne Chip's own tools | DigiKey about $27 one off, $18 in volume; not LCSC | the same company as the XHFC; one supplier for both |
| Lattice iCE40UP5K | 5k LUT | 8 multipliers, 1 Mbit SPRAM | open, mature | cheap, LCSC | enough for streaming and S0, no headroom; a HS ULPI core is tight |
| Gowin GW2A-18 | 20k LUT | 48 multipliers | vendor IDE; open (Apicula) is partial | cheap, LCSC | the toolchain is the catch |

**ECP5-25F:** the open toolchain is the best-trodden of the four, it has
real multipliers for later DSP, JLC assembles BGA-256, and there are
open USB HS cores (LUNA, as on Great Scott Gadgets' Cynthion) and SoC
frameworks (LiteX) to start from. GateMate is the alternative if one
order from Cologne Chip (XHFC and FPGA) suits.

---

## 4. The line front end (ADSL, ISDN U, WB)

### 4.1 Converters: Analog Devices AD9866

A "broadband modem mixed-signal front end": a 12-bit ADC (to 80 MSPS)
with a receive PGA, a 12-bit TxDAC with a transmit PGA, made for
HomePlug and DSL-class modems; the Hermes-Lite 2 SDR uses it, so there
is open gateware for its bus. LCSC lists it (AD9866BCP, C7459112).
**ADI marks it not recommended for new designs**; the pin-compatible
AD9869 is the cheaper sibling (without the AD9866's current output
amplifier). Like the Line stick's Si3056, fine to build on, buy ahead
for a run. The fallback is a separate ADC and DAC (a 12–14 bit, 20–65
MSPS pair) and a PGA: more parts, the same gateware shape.

Rates: ADSL2+ needs 4.416 MHz at the line (512 tones of 4.3125 kHz,
to 2.208 MHz); the converters run at 4× that, 17.664 MHz, with the
FPGA decimating and interpolating, so the analogue filters are gentle.
ADSL is 2.208 MHz at the line. ISDN U and WB run slower.

### 4.2 Clocks: Si5351

ADSL's customer end must run its sample clock locked to the exchange's
(the pilot tone), and an LT must hold the U line's clock; D-Modem's WB
receiver tracks crystals in software. A **Si5351** (or MS5351M, at
LCSC) makes the converters' clock from a 25 MHz crystal, at any rate the
mode needs, and the host pulls it by fractions of a ppm over I2C as its
timing loop asks. (A VCXO tuned by a DAC, as Osmocom's icE1usb does, is
the alternative; the Si5351 covers every mode's rate with one part.)

### 4.3 Line driver: TI THS6222

A differential current-feedback line driver for DSL and power-line
modems, single supply 8–32 V, active at TI. ADSL's transmit power sets
it:

| End | Transmit | |
|---|---|---|
| ATU-R (customer) | about 12.5 dBm, upstream band only (to 138 kHz in annex A, 276 kHz in annex B) | 1.3 Vrms on 100 Ω |
| ATU-C (exchange) | about 20 dBm across 1.1–2.2 MHz | 3.3 Vrms on 100 Ω, DMT peaks about 5× that (17 V): the driver at 12 V, differential, through a 1:2 transformer |
| ISDN U | 2B1Q ±2.5 V pulses (13.5 dBm) | |

A boost converter makes 12 V from VBUS for the driver.

### 4.4 Hybrid, transformer, protection

As the WB stick, scaled up in frequency:
- An **xDSL line transformer** (an ADSL2+ CPE type, 1:1 or 1:2, flat to
  2.2 MHz and down to about 10 kHz for ISDN U; Pulse and Bourns make
  them).
- A resistive **hybrid**: the replica arm subtracts our own transmit
  before the AFE's receive PGA (frequency division does the rest for
  ADSL; 2B1Q needs the host's echo canceller behind it).
- **DC block** capacitors in the line side (≥ 250 V): an ADSL pair can
  carry the phone line's −48 V and ringing, an ISDN U pair the exchange's
  remote feed (up to about 100 V). **No splitter** on the board: ADSL over
  a voice line needs the usual external splitter or microfilter.
- The Line and WB sticks' sidactor and telecom fuse.

The LT side of ISDN U would also need to feed power to the NTBA on rev A
or not at all; most NTBAs run from their own mains supply.

---

## 5. The S0 front end

The S0 stick's circuit (SPEC-S0 §4): the tri-state buffer driving the
transmit transformer's chip winding through resistors, two fast
comparators against a set threshold on the receive transformer, the
1:2 S0 transformer pair for isolation, the same 100 Ω terminations and
protection, with the FPGA in place of the PIO and core 1. In logic,
the frame timing, the two-bit TE offset and the D-channel's echo check
are exact by construction. A board-mounted XHFC-1SU on the FPGA's SPI
instead is the conservative choice; it costs a separate order and
supports only S0, which the S0 stick already does.

---

## 6. USB and the host

- **USB 2.0 High Speed** through a ULPI PHY (Microchip USB3320), the
  device core in gateware; the same Microsoft OS 2.0 descriptors as the
  sticks (WinUSB, no INF) and a pid.codes ID.
- ADSL2+ at the line's 4.416 MHz, 16-bit, is 8.8 MB/s each way, about
  18 MB/s in all: comfortable on HS bulk or high-bandwidth isochronous
  endpoints (about 35–40 MB/s usable). Full Speed (the RP2040's) can't
  carry it, which is why this board is not an RP2040 stick.
- **Host compute:** ADSL2+ is 4000 symbols a second, each a 1024-point
  FFT and IFFT, a time-domain equaliser (tens of taps at 4.4 MHz, about
  150 M multiply-adds a second), and Viterbi and Reed-Solomon at up to
  24 Mbit/s. A few tens of percent of one desktop core with SIMD; harder
  on a phone, which is where moving the FFTs into the ECP5's
  multipliers would pay.
- **dmodemd:** a `--usb` device kind with a mode; ADSL gives a network
  interface (tun/tap with PPPoE or PPPoA) rather than a pty; ISDN U and
  S0 give the same line backend as the S0 stick (LAPD, Q.931, two
  B-channels, CAPI through `dmodem-capid`).

---

## 7. Power

USB-C, 5 V, the board asking for **1.5 A** through its CC pins and
running at reduced transmit power when the host offers only 500 mA.

| | ATU-R, ISDN | ATU-C |
|---|---|---|
| FPGA (ECP5-25, light gateware) | 0.3–0.5 W | 0.3–0.5 W |
| USB3320 | 0.1 W | 0.1 W |
| AD9866 | 0.3–0.5 W | 0.3–0.5 W |
| THS6222 and the boost | 0.3 W | 1–1.5 W |
| **Total** | **about 1–1.5 W** (within 500 mA) | **about 2–2.5 W** (1.5 A USB-C) |

To check against the datasheets before a netlist; these are typical
figures for the class.

---

## 8. Size

Not a stick: two jacks (RJ11 for the line, RJ45 for S0), a BGA FPGA,
the AFE and the driver's boost make a small box, about 70 × 45 mm, four
layers (the BGA's escape and the AFE's ground).

---

## 9. Work and risk

| Part | Size of the job | Risk |
|---|---|---|
| Board (FPGA, USB HS, AFE, line side) | large, but known: SDR boards are built this way | the AFE's NRND status; analogue layout at 2 MHz with 12 V drive |
| Gateware: streamer, USB, clocks, S0 | medium; open cores exist for USB HS and the AD9866 bus | low |
| ISDN U on the host | medium: 2B1Q and its echo canceller are textbook, then the S0 stick's LAPD and Q.931 | low to medium |
| ADSL on the host | **very large**: G.992.1/.3/.5 and G.994.1 (ITU-T publishes them free), training, bit swapping, ATM or PTM | **high**: no open ADSL transceiver to build on that I know of, and DSLAMs (Broadcom, Lantiq, Ikanos chipsets) are strict about handshakes and timing |
| ATU-C for vintage ADSL modems | the same transceiver, the other end | lower than ATU-R: our end sets the rules, old CPE expect G.992.1 |

So the board is feasible, and the hardware is not the hard part. ADSL is
a multi-month software project on the host, on the scale of D-Modem's
V.90/V.92.

---

## 10. Recommendation

1. Keep the **S0 stick** (SPEC-S0) as the ISDN path to build first: one
   cheap board, the soft S0 proven there first, and its host software (LAPD, `s0_line`) is
   what the FX board's ISDN modes reuse.
2. Build the **FX board** as the line lab after it: ECP5-25F, USB3320,
   AD9866, THS6222, Si5351; the line front end for ADSL, ISDN U and WB,
   and a soft S0 port. Bring up ISDN U and WB first (smaller, and they
   test the AFE and clocks), then the ADSL ATU-C against an old ADSL
   router, then the ATU-R against a real DSLAM.

## 11. Open questions

1. The AD9866's stock and life (or design for the AD9869 / a discrete
   ADC and DAC from the start).
2. An xDSL transformer that is also flat enough down to ISDN U's
   band (2B1Q has energy below 10 kHz).
3. ADSL annex A (over POTS) or B (over ISDN) first: the band plan, set
   by where it will be used.
4. Whether the LT side of ISDN U should feed an NTBA (about 90 V remote
   feed): a separate, isolated supply, probably not on this board.
5. ECP5-25F or GateMate (one supplier with the XHFC, or the more
   established toolchain).
