<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 Neo-Desktop -->
# pico-serial WB — wideband bare-pair stick (draft 0.1)

A sibling of the phone-line stick ([SPEC-L.md](SPEC-L.md)) for **bare
copper pairs**: dry pairs, alarm and leased circuits, a pair of your own
in a building or between buildings. Two WB sticks talk to each other
point to point, sampling the pair at **96 kHz** instead of a phone line's 8–9.6 kHz, so they use far more of the pair's
bandwidth than any voice-band modem can.

Same as the Line stick: the stick is only the line interface (a codec, a
transformer, protection) with an RP2040 streaming samples over USB, and
the modem is **D-Modem-RE on the host** (Linux `dmodemd`, Android's
dmodem app), here a new wideband mode of our own (§6). Both ends are
ours, so the modulation needn't follow any standard.

Not covered: telephone lines (use the Line stick: a phone line's
exchange passes only 300–3400 Hz, whatever the modem), ADSL and SHDSL
(megahertz bands, standard line codes, a carrier's equipment at the far
end: see §9).

---

## 1. Requirements

| # | Requirement |
|---|---|
| B1 | USB-C device, Full Speed, bus powered, under 100 mA |
| B2 | Two-wire connection to the pair: RJ11 (middle pair), like the Line stick |
| B3 | Samples at 96 000 Hz; 48 000 and 192 000 Hz for tests |
| B4 | Usable band about 4 kHz to 36 kHz (the codec's ADC passes 37 kHz at 96 kHz, and only 21 kHz at 192 kHz; the transformer and DC block start at about 3 kHz) |
| B5 | Full duplex on two wires by frequency division: one band each way |
| B6 | Galvanic isolation (a transformer, ≥ 1500 Vrms) and protection: survives being plugged into a live phone line (−48 V, ringing) and line surges |
| B7 | Point to point between two WB sticks; no dial tone, ringing or hook |
| B8 | The same RP2040 core, USB-C and firmware structure as the Line stick; same stick shape |

---

## 2. Block diagram

```
 USB-C ── ESD ── RP2040 ── I2S (PIO) ──► TLV320AIC3204 ── line out ─► R ─┐
  (UFP)          │  MCLK 12 MHz (GPOUT) ►  codec, PLL      ◄─ line in ◄─ hybrid
  LDO 3.3 V      │  I2C ───────────────►   96/192 kHz                     │
  flash, xtal    └── LEDs (LINK, DATA)                         ═══ transformer ═══ DC block ─ fuse ─ RJ11
                                                                isolation barrier   caps    sidactor
```

---

## 3. The line interface

- **Codec: TI TLV320AIC3204** (LCSC C24109): stereo ADC and DAC to 192
  kHz (but run at 96 kHz, B4), its own PLL (so the RP2040's 12 MHz crystal clock is its MCLK and
  it is the I2S master, as the Si3056 is the serial port's master on the
  Line stick), a headphone driver strong enough for a line transformer,
  differential inputs, I2C control, one 3.3 V supply (internal LDO).
- **Transformer:** a wideband telecom transformer (the ISDN-U / HDSL /
  SHDSL kind: a few kHz to hundreds of kHz) is the isolation barrier, as
  the Si3056's capacitors are on the Line stick. Parts: see §8.
- **DC block:** capacitors in series with the line side (≥ 250 V), so a
  live phone line's −48 V doesn't saturate the transformer.
- **Line drive:** the codec's headphone outputs, differential (HPL = s,
  HPR = −s), through 60.4 Ω in each leg: 120 Ω, about the pair's
  impedance; about +8 dBm at most. No op-amp.
- **Hybrid:** frequency division keeps the far end's band apart from our
  own, but our own transmit level is far above what we receive. A
  replica arm at ten times the impedance (604 Ω each leg, and 1.21 kΩ in
  series with 12 nF for the cable) makes a copy of what we send; the
  codec's input mixer subtracts it (P = IN1L + IN1R, M = IN2R + IN3R, 10
  kΩ each), taking 10–20 dB of our own signal out before the ADC. The
  modem's filters take the rest.
- **Protection:** the Line stick's sidactor and telecom fuse on the line
  side, clamps on the codec side.

The netlist variant is `--variant wb`; the shell is the Line stick's
(`stick.py`), with the LEDs relabelled.

---

## 4. Clocks and rates

The codec's PLL makes its 96 kHz from the RP2040's 12 MHz crystal
clock (GPOUT, no fractional divider): P = 1, R = 1, J.D = 8.1920 gives
98.304 MHz; NDAC = NADC = 4, MDAC = MADC = 4, DOSR = AOSR = 64 (blocks
PRB_P7 and PRB_R7); BCLK = DAC_CLK / 4 = 6.144 MHz, 64 bits a frame. For
192 kHz (tests): DOSR = AOSR = 32, PRB_P17 and PRB_R13, BCLK / 2. The codec is the I2S master: the
RP2040's PIO takes its bit clock and word clock as the Line stick's PIO
takes the Si3056's. So the sample clock is the stick's crystal, and the
USB side follows it.

Two sticks never share a clock: their crystals differ by up to about
±50 ppm, and the modem's receiver tracks the difference (§6).

## 5. USB

As the Line stick ([PROTOCOL.md](firmware/PROTOCOL.md), "Line stick"):
one vendor interface, isochronous IN and OUT, 1 ms packets sized by the
codec's clock, an interrupt endpoint for events. At 96 kHz S16 that is
192 bytes a millisecond each way (384 at 192 kHz), well within Full
Speed's 1023-byte isochronous packets. A distinct PID (pid.codes test
PID) and a version bit say which stick it is. Vendor requests: info,
rate, codec register access, the line's level and a loopback test; no
hook, ring or caller ID.

---

## 6. The modem: a wideband mode for D-Modem

A new data pump in D-Modem-RE (`dsp/`), with D-Modem's error correction
and compression above it (V.42 LAPM, V.44) as for every other mode.

**Multicarrier (DMT/OFDM), frequency-division duplex.**
- At 96 kHz: a 512-point FFT gives 256 tones 187.5 Hz apart; a cyclic
  prefix of 40 samples; about 174 symbols a second. Tones about 21 to 192
  (4–36 kHz) are usable (B4).
- **The two directions on separate tones:** the end that dialled sends on
  the lower band, the end that answered on the upper, with a guard band
  between. The split is set at training from both directions' measured
  capacity, so the two directions get about the same rate.
- **Per-tone bit loading:** each receiver measures every tone's
  signal-to-noise ratio during training and tells the sender how many bits
  each tone carries (0 to 12) and at what gain, as ADSL does. Long pairs
  lose their top tones; short ones use them all.
- **Pilot tone and clock tracking:** a pilot in each direction lets the
  receiver follow the two crystals' difference (rotating each tone's phase
  and slipping a sample now and then).
- **Coding:** Reed-Solomon (D-Modem's `dsp/rs.cpp`) with interleaving;
  trellis coding later if it pays.
- **Start-up:** the caller sends a probe tone; the answerer (auto-answer
  on a leased line, `AT&L1`, or `ATA`) replies; then the training
  sequence (sync symbols, the SNR measurement, the bit tables exchanged on
  a robust 2 bits-per-tone channel), then data. Retrain on loss of
  sync, and adapt the bit tables in a call (bit swapping) as the pair's
  noise changes.

Expected rates (to be measured): about 100–150 kbit/s each way on a short
pair (about 85 tones each way); on long pairs (km of telephone cable) the top
tones fade and the rate drops with them.

**Leased-line behaviour (Hayes `&L1`):** no dial tone, ringing or hook.
`ATD` (any number) starts as the caller; `ATA`, or `ATS0=1`, waits for a
caller's probe. Carrier lost: `NO CARRIER`, and with `&L1` the modem
tries again by itself.

**Test plan:** both pumps against each other over a simulated pair:
frequency-dependent loss (a cable model), noise (white and narrowband),
the hybrid's leakage, crystals 100 ppm apart; then two sticks on a real
pair (a drum of telephone cable).

---

## 7. Host software

- `dmodemd --usb` finds a WB stick beside the SmartUSB56 and the Line
  stick; `-r 96000` (the default for it).
- The core runs at the line's rate: D-Modem's pumps already take their
  sample rate from the core (`modem_dp` `srate`); the line drivers' and
  options' 8000/9600-only checks widen for this device.
- dmodem-app: the same, through Android's USB host API and libusb.

## 8. Parts

Checked against JLCPCB/LCSC stock on 2026-10-27.

| Role | Part | LCSC | Package | Notes |
|---|---|---|---|---|
| Codec | TI TLV320AIC3204IRHBR | C24109 | QFN-32 5×5 | I2C 0x18; LDOIN 3.3 V, internal LDOs (LDO_SELECT high) |
| Line transformer | Pulse PE-65848NLT | C7261159 | SMD-8 12×12, 8.6 mm high | 1500 Vrms, Lp 3.5–6.5 mH, split line winding; 250 in stock |
| Transformer, second source | Pulse PE-65812NLT | C7498702 | SMD-8 12×12 | 2000 V, 2.5 mH, low corner ~3.8 kHz |
| Sidactor | Littelfuse P3100SCLRP | C151191 | DO-214AA | as the Line stick; 70 pF, no effect at 100 kHz |
| Fuse | Littelfuse 04611.25ER | C99554 | SMD | as the Line stick |
| Ferrites ×2 | Murata BLM18AG601SN1D | C19330 | 0603 | under 1 Ω at 100 kHz |
| DC block ×2, in series | FS43X105K251EGG 1 µF 250 V X7R | C381468 | 1812 | 0.5 µF, 500 V together (a live line peaks at ~260 V); in the transformer's split winding (TIP–8, 7–C–6, 5–RING) |
| Codec-side TVS | Yangjie ESDSLC5V0LB | C2762104 | DFN1006-2 | across the codec winding, 0.8 pF |
| Codec-side clamps ×2 | Nexperia BAV99 | C2500 | SOT-23 | each line node to 3.3 V and ground |
| Drive and hybrid | 60.4 Ω, 604 Ω, 1.21 kΩ 1 %; 12 nF C0G | basic | 0603 | §3 |

Plugged into a live phone line by mistake: no DC path, so the exchange
sees it on-hook; ringing drives about 6 mA through it.

To check on the bench: the transformer's datasheet gives no bandwidth
curve or saturation rating (sweep one from 1 to 200 kHz), and the hybrid
values are a starting point from typical cable data.

## 9. Not this stick: ADSL and SHDSL

ADSL uses 25 kHz–1.1 MHz (ADSL2+ 2.2 MHz): about 4.4 million samples a
second each way, past the RP2040's Full Speed USB, and it would need an
ADSL analogue front end and, to reach an ISP, the whole G.992 standard
(DMT, its training, ATM) against a DSLAM. SHDSL likewise. A study, on a
different chip, if wanted later.

## 10. Next steps

1. Parts (§8), then the netlist, schematic and PCB (`--variant wb`).
2. Firmware: the codec over I2C, I2S in PIO, the Line stick's USB at
   96/192 kHz.
3. The modem in D-Modem-RE, developed against the simulated pair first.
4. Two sticks on a real pair.
