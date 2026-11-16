<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 D-Modem Authors -->
# pico-serial FX — FPGA line board for ADSL or ISDN (study, draft 0.0)

pico-serial FX is one board. Its gateware and the software on the host set
its line function. It can be one of these items:
- An **ADSL / ADSL2+** modem (or the exchange end for vintage ADSL modems).
- An **ISDN U** line (2B1Q or 4B3T, either end).
- An **ISDN S0** port.
- A faster successor to the bare-pair mode of the WB stick
  ([SPEC-WB.md](SPEC-WB.md). Its §9 left ADSL for "a different chip").

The concept is the same as for all D-Modem sticks, but it goes further. The
board is a **wideband line interface** (a fast ADC and DAC, a line driver, a
transformer) and an FPGA. The FPGA moves samples to USB 2.0 High Speed. It
also controls the bit timing where this is important. The modem (here, a
DMT transceiver or a 2B1Q echo canceller) runs in **D-Modem on the host**,
as V.92 does for the Line stick.

Status: a feasibility study. We did not compare the parts in this document
with the stock to the same depth as in the other specs. The part numbers
are candidates.

---

## 1. What it would do

| Mode | Port | In the FPGA | On the host (D-Modem) |
|---|---|---|---|
| ADSL, ADSL2+ ATU-R (a customer modem, to a DSLAM) | line | samples at 2.208 / 4.416 MHz, clock tracking | DMT (G.992.1/.3/.5), G.994.1 handshake, training, trellis, Reed-Solomon, interleaving; ATM (AAL5, RFC 2684) or PTM; PPPoE/PPPoA to a network interface |
| ADSL ATU-C (the exchange end, for vintage ADSL modems and routers) | line | the same, more transmit power | the same, the other side of each procedure |
| ISDN U, NT1 or LT side | line | samples (about 1 MHz) | 2B1Q (80 kbaud, ETSI TS 102 080 annex A, ANSI T1.601) or 4B3T (120 kbaud, annex B, Germany); echo canceller; then LAPD and Q.931 as for the S0 stick |
| ISDN S0, TE or NT | S0 | the line code, framing and D-channel access of I.430 (the soft S0 of the S0 stick, [SPEC-S0.md](SPEC-S0.md) §3–6, in logic, not in PIO) | as the S0 stick (SPEC-S0 §9) |
| Bare pair, wideband (the mode of WB, more tones) | line | samples | the WB pump of D-Modem with tones to 1 MHz or more |
| SHDSL (G.991.2) | line | samples | later, if necessary |

**"Depending on the firmware"** means this: one gateware does all modes (a
sample streamer at a set rate, and the S0 framer). The host selects the
mode. You can update the gateware through USB (DFU into its SPI flash, with
a protected golden image as a fallback). Thus, if a mode needs work in logic
later (for example, a hardware FFT), you do a gateware update. A new board
is not necessary.

### 1.1 ADSL and ISDN U on the same pair

Both are two-wire signals on a telephone pair, below 2.2 MHz. They use echo
cancellation or a frequency split. One line front end is sufficient for
the two. The converters run at the rate that the mode needs. The
transformer and the hybrid operate from 10 kHz to 2.2 MHz. The S0 port is
different (four wires, 192 kbit/s pulses, a bus). It has its own small
front end and jack.

### 1.2 Where these lines still are

Many countries are switching off ISDN and ADSL. (In Germany, almost all
ISDN was removed by 2022. Fibre and VDSL are now replacing ADSL.) Thus the
most durable use of the board is probably **being the exchange**:
- The ATU-C for an old ADSL router.
- The LT for an NTBA. (The NTBA then gives S0 and power to ISDN phones: the
  PS1 problem of SPEC-S0 §4.6.)
- NT mode for terminal adapters.

All of these modes have D-Modem behind them. ATU-R mode is still important
where ADSL is still sold. It is also the most difficult interoperability
test (§6).

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

In the default gateware, the function of the FPGA is small:
- A USB 2.0 HS device core (ULPI).
- FIFOs.
- The converter bus at up to about 35 MHz.
- An I2C master.
- The S0 framer.

This needs some thousand LUTs. The size is set by the headroom. If we move
the FFTs, trellis or Reed-Solomon of the DMT into logic later (for a slow
host, or for Android), this needs multipliers and block RAM.

| FPGA | Logic | DSP | Toolchain | Price, availability | |
|---|---|---|---|---|---|
| **Lattice ECP5 LFE5U-25F** (caBGA-256, 0.8 mm) | 24k LUT | 28 18×18 multipliers, 1 Mbit EBR | **open and mature**: Yosys, nextpnr-ecp5, Project Trellis; LiteX; the USB HS core of LUNA | about $10–20; LCSC has the ECP5 family in stock (the 45F at $17) | **recommended** |
| Cologne Chip GateMate CCGM1A1 (FBGA-324) | 20k LE | multipliers made from logic cells | open: Yosys + nextpnr (himbaechel), the tools of Cologne Chip | DigiKey about $27 for one, $18 in volume; not LCSC | the same company as the XHFC; one supplier for both |
| Lattice iCE40UP5K | 5k LUT | 8 multipliers, 1 Mbit SPRAM | open, mature | cheap, LCSC | sufficient for streaming and S0, but no headroom; a HS ULPI core is almost too large |
| Gowin GW2A-18 | 20k LUT | 48 multipliers | vendor IDE; the open toolchain (Apicula) is partial | cheap, LCSC | the toolchain is the problem |

**ECP5-25F:** of the four, it has the open toolchain with the most use. It
has real multipliers for DSP in the future. JLC assembles BGA-256. There
are open USB HS cores (LUNA, as on the Cynthion of Great Scott Gadgets) and
SoC frameworks (LiteX) to start from. GateMate is the alternative if one
order from Cologne Chip (XHFC and FPGA) is better.

---

## 4. The line front end (ADSL, ISDN U, WB)

### 4.1 Converters: Analog Devices AD9866

The AD9866 is a "broadband modem mixed-signal front end". It has a 12-bit
ADC (to 80 MSPS) with a receive PGA, and a 12-bit TxDAC with a transmit
PGA. It is made for HomePlug and DSL-class modems. The Hermes-Lite 2 SDR
uses it, thus there is open gateware for its bus. LCSC lists it
(AD9866BCP, C7459112).

**ADI marks it as not recommended for new designs.** The pin-compatible
AD9869 is the cheaper sibling (without the current output amplifier of the
AD9866). As with the Si3056 of the Line stick, it is satisfactory for a
build, but buy the parts early for a production run. The fallback is a
separate ADC and DAC (a 12–14 bit, 20–65 MSPS pair) and a PGA: more parts,
but the same gateware shape.

Rates: ADSL2+ needs 4.416 MHz at the line (512 tones of 4.3125 kHz, to
2.208 MHz). The converters run at 4× that rate, 17.664 MHz. The FPGA does
the decimation and the interpolation, thus the analogue filters can be
gentle. ADSL is 2.208 MHz at the line. ISDN U and WB run slower.

### 4.2 Clocks: Si5351

These modes need an accurate clock:
- The customer end of ADSL must lock its sample clock to the clock of the
  exchange (the pilot tone).
- An LT must hold the clock of the U line.
- The WB receiver of D-Modem tracks crystals in software.

A **Si5351** (or MS5351M, at LCSC) makes the clock of the converters from a
25 MHz crystal, at the rate that the mode needs. The host changes it by
fractions of a ppm through I2C, as its timing loop requests. (A VCXO tuned
by a DAC, as on the icE1usb of Osmocom, is the alternative. The Si5351 gives
the rate of all modes with one part.)

### 4.3 Line driver: TI THS6222

The THS6222 is a differential current-feedback line driver for DSL and
power-line modems. It has a single supply of 8–32 V, and it is active at
TI. The transmit power of ADSL sets the driver:

| End | Transmit | |
|---|---|---|
| ATU-R (customer) | about 12.5 dBm, upstream band only (to 138 kHz in annex A, 276 kHz in annex B) | 1.3 Vrms on 100 Ω |
| ATU-C (exchange) | about 20 dBm across 1.1–2.2 MHz | 3.3 Vrms on 100 Ω, DMT peaks about 5× that (17 V): the driver at 12 V, differential, through a 1:2 transformer |
| ISDN U | 2B1Q ±2.5 V pulses (13.5 dBm) | |

A boost converter makes 12 V from VBUS for the driver.

### 4.4 Hybrid, transformer, protection

The design is the same as on the WB stick, but for higher frequencies:
- An **xDSL line transformer** (an ADSL2+ CPE type, 1:1 or 1:2, flat to
  2.2 MHz and down to about 10 kHz for ISDN U. Pulse and Bourns make
  them).
- A resistive **hybrid**: the replica arm subtracts our transmit signal
  before the receive PGA of the AFE. (For ADSL, frequency division removes
  the remaining signal. 2B1Q needs the echo canceller of the host behind
  it.)
- **DC block** capacitors in the line side (≥ 250 V).

  > **CAUTION: USE DC BLOCK CAPACITORS WITH A RATING OF 250 V OR MORE.**
  > An ADSL pair can have the −48 V and the ring signal of the phone line.
  > An ISDN U pair can have the remote feed of the exchange (up to about
  > 100 V).

  There is **no splitter** on the board. ADSL on a voice line needs the
  usual external splitter or microfilter.
- The sidactor and telecom fuse of the Line and WB sticks.

On the LT side of ISDN U, rev A must also feed power to the NTBA, or not
feed power at all. Most NTBAs operate from their own mains supply.

---

## 5. The S0 front end

This is the circuit of the S0 stick (SPEC-S0 §4), with the FPGA in place
of the PIO and core 1:
- The tri-state buffer drives the chip winding of the transmit transformer
  through resistors.
- Two fast comparators compare the receive transformer with a set
  threshold.
- The 1:2 S0 transformer pair gives isolation.
- The same 100 Ω terminations and protection.

In logic, the frame timing, the two-bit TE offset and the echo check of the
D-channel are exact by design. The conservative alternative is a
board-mounted XHFC-1SU on the SPI of the FPGA. But it needs a separate
order, and it supports only S0. The S0 stick already does S0.

---

## 6. USB and the host

- **USB 2.0 High Speed** through a ULPI PHY (Microchip USB3320), with the
  device core in gateware. The board uses the same Microsoft OS 2.0
  descriptors as the sticks (WinUSB, no INF) and a pid.codes ID.
- ADSL2+ at the 4.416 MHz of the line, 16-bit, is 8.8 MB/s in each
  direction, about 18 MB/s in total. This is easy on HS bulk or
  high-bandwidth isochronous endpoints (about 35–40 MB/s usable). Full
  Speed (the USB of the RP2040) cannot send this rate. This is why this
  board is not an RP2040 stick.
- **Host compute:** ADSL2+ is 4000 symbols each second. Each symbol needs:
  - A 1024-point FFT and IFFT.
  - A time-domain equaliser (tens of taps at 4.4 MHz, about 150 M
    multiply-adds each second).
  - Viterbi and Reed-Solomon at up to 24 Mbit/s.

  This is some tens of percent of one desktop core with SIMD. It is more
  difficult on a phone. On a phone, it can be better to move the FFTs into
  the multipliers of the ECP5.
- **dmodemd:** a `--usb` device type with a mode. ADSL gives a network
  interface (tun/tap with PPPoE or PPPoA), not a pty. ISDN U and S0 give
  the same line backend as the S0 stick (LAPD, Q.931, two B-channels, CAPI
  through `dmodem-capid`).

---

## 7. Power

The power comes from USB-C, 5 V. The board requests **1.5 A** through its
CC pins. When the host gives only 500 mA, the board operates at a lower
transmit power.

| | ATU-R, ISDN | ATU-C |
|---|---|---|
| FPGA (ECP5-25, light gateware) | 0.3–0.5 W | 0.3–0.5 W |
| USB3320 | 0.1 W | 0.1 W |
| AD9866 | 0.3–0.5 W | 0.3–0.5 W |
| THS6222 and the boost | 0.3 W | 1–1.5 W |
| **Total** | **about 1–1.5 W** (within 500 mA) | **about 2–2.5 W** (1.5 A USB-C) |

These are typical values for this class of parts. Compare them with the
datasheets before you make a netlist.

---

## 8. Size

This board is not a stick. It has two jacks (RJ11 for the line, RJ45 for
S0), a BGA FPGA, the AFE and the boost of the driver. Thus it is a small
box, about 70 × 45 mm, with four layers (for the escape of the BGA and the
ground of the AFE).

---

## 9. Work and risk

| Part | Size of the work | Risk |
|---|---|---|
| Board (FPGA, USB HS, AFE, line side) | large, but known: SDR boards use this design | the NRND status of the AFE; analogue layout at 2 MHz with 12 V drive |
| Gateware: streamer, USB, clocks, S0 | medium; open cores exist for USB HS and the AD9866 bus | low |
| ISDN U on the host | medium: 2B1Q and its echo canceller are well known, then the LAPD and Q.931 of the S0 stick | low to medium |
| ADSL on the host | **very large**: G.992.1/.3/.5 and G.994.1 (ITU-T publishes them free of charge), training, bit swapping, ATM or PTM | **high**: I know of no open ADSL transceiver to start from. DSLAMs (Broadcom, Lantiq, Ikanos chipsets) are strict about handshakes and timing |
| ATU-C for vintage ADSL modems | the same transceiver, the other end | lower than ATU-R: our end sets the rules, and old CPE expect G.992.1 |

Thus the board is feasible, and the hardware is not the difficult part.
ADSL is a software project on the host that takes many months, of the same
size as the V.90/V.92 of D-Modem.

---

## 10. Recommendation

1. Keep the **S0 stick** (SPEC-S0) as the first ISDN path to build. It is
   one cheap board, it proves the soft S0 first, and the ISDN modes of the
   FX board use its host software again (LAPD, `s0_line`).
2. After that, build the **FX board** as the line lab: ECP5-25F, USB3320,
   AD9866, THS6222, Si5351; the line front end for ADSL, ISDN U and WB,
   and a soft S0 port. Do the bring-up in this sequence:
   1. ISDN U and WB (they are smaller, and they test the AFE and the
      clocks).
   2. The ADSL ATU-C against an old ADSL router.
   3. The ATU-R against a real DSLAM.

## 11. Open questions

1. The stock and life of the AD9866 (or design for the AD9869 / a discrete
   ADC and DAC from the start).
2. An xDSL transformer that is also sufficiently flat down to the band of
   ISDN U (2B1Q has energy below 10 kHz).
3. ADSL annex A (over POTS) or B (over ISDN) first: the location of use
   sets the band plan.
4. Must the LT side of ISDN U feed an NTBA (about 90 V remote feed)? This
   needs a separate, isolated supply, probably not on this board.
5. ECP5-25F or GateMate (one supplier with the XHFC, or the toolchain with
   more use)?
