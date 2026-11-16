<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 D-Modem Authors -->
# pico-serial WB — wideband bare-pair stick (draft 0.1)

pico-serial WB is a sibling of the phone-line stick ([SPEC-L.md](SPEC-L.md))
for **bare copper pairs**: dry pairs, alarm and leased circuits, or your own
pair in a building or between buildings. Two WB sticks communicate with
each other point to point. They sample the pair at **96 kHz**, not at the 8–9.6 kHz of a phone line. Thus they use much more of the bandwidth of the
pair than a voice-band modem can.

As with the Line stick, the stick is only the line interface (a codec, a
transformer, protection). An RP2040 sends samples through USB. The modem is
**D-Modem on the host** (Linux `dmodemd`, the dmodem app of Android). Here
it is a new wideband mode of our own (§6). We make the two ends, thus the
modulation does not have to agree with a standard.

This stick is not for these items:
- Telephone lines. Use the Line stick: the exchange of a phone line lets
  only 300–3400 Hz go through, with all modems.
- ADSL and SHDSL (megahertz bands, standard line codes, equipment of a
  carrier at the far end: see §9).

---

## 1. Requirements

| # | Requirement |
|---|---|
| B1 | USB-C device, Full Speed, bus powered, less than 100 mA |
| B2 | Two-wire connection to the pair: RJ11 (middle pair), as on the Line stick |
| B3 | Samples at 96 000 Hz; 48 000 and 192 000 Hz for tests |
| B4 | Usable band about 4 kHz to 36 kHz (the ADC of the codec passes 37 kHz at 96 kHz, and only 21 kHz at 192 kHz; the transformer and DC block start at about 3 kHz) |
| B5 | Full duplex on two wires by frequency division: one band for each direction |
| B6 | Galvanic isolation (a transformer, ≥ 1500 Vrms) and protection: no damage if you connect it to a live phone line (−48 V, ring signal) and no damage from line surges |
| B7 | Point to point between two WB sticks; no dial tone, ring signal or hook |
| B8 | The same RP2040 core, USB-C and firmware structure as the Line stick; the same stick shape |

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

- **Codec: TI TLV320AIC3204** (LCSC C24109). It has these functions:
  - A stereo ADC and DAC to 192 kHz (but it runs at 96 kHz, B4).
  - Its own PLL. Thus the 12 MHz crystal clock of the RP2040 is its MCLK,
    and the codec is the I2S master. (On the Line stick, the Si3056 is the
    master of the serial port in the same way.)
  - A headphone driver that is sufficiently strong for a line
    transformer.
  - Differential inputs, I2C control, and one 3.3 V supply (internal LDO).
- **Transformer:** a wideband telecom transformer (the ISDN-U / HDSL /
  SHDSL type: some kHz to hundreds of kHz) is the isolation barrier. On the
  Line stick, the capacitors of the Si3056 have this function. Parts: see
  §8.
- **DC block:** capacitors in series with the line side (≥ 250 V). Thus
  the −48 V of a live phone line does not saturate the transformer.
- **Line drive:** the headphone outputs of the codec, differential
  (HPL = s, HPR = −s), through 60.4 Ω in each leg. This gives 120 Ω, about
  the impedance of the pair, and about +3 dBm on the pair at full scale.
  (The guide of TI supports a differential load only with the drivers on
  AVDD, 1.0 Vrms between HPL and HPR.) There is no op-amp.
- **Hybrid:** frequency division keeps the band of the far end separate
  from our band. But our transmit level is much higher than the level that
  we receive. A replica arm at ten times the impedance (604 Ω each leg, and
  1.21 kΩ in series with 10 nF for the cable) makes a copy of the signal
  that we send. The input mixer of the codec subtracts it (P = IN1L + IN1R,
  M = IN2R + IN3R, 10 kΩ each). This removes 10–20 dB of our signal before
  the ADC. The filters of the modem remove the remaining signal.
- **Protection:** the sidactor and telecom fuse of the Line stick on the
  line side, and clamps on the codec side.

The netlist variant is `--variant wb`. The shell is the shell of the Line
stick (`stick.py`), with different LED labels.

---

## 4. Clocks and rates

The PLL of the codec makes its 96 kHz from the 12 MHz crystal clock of the
RP2040 (GPOUT, no fractional divider):
- P = 1, R = 1, J.D = 8.1920 gives 98.304 MHz.
- NDAC = NADC = 4, MDAC = MADC = 4, DOSR = AOSR = 64 (blocks PRB_P7 and
  PRB_R7).
- BCLK = DAC_CLK / 4 = 6.144 MHz, 64 bits in each frame.
- For 192 kHz (tests): DOSR = AOSR = 32, PRB_P17 and PRB_R13, BCLK / 2.

The codec is the I2S master. The PIO of the RP2040 takes the bit clock and
the word clock of the codec, as the PIO of the Line stick takes the clocks
of the Si3056. Thus the sample clock is the crystal of the stick, and the
USB side follows it.

Two sticks never share a clock. Their crystals can be different by up to
about ±50 ppm. The receiver of the modem tracks the difference (§6).

## 5. USB

The USB interface is the same as on the Line stick
([PROTOCOL.md](firmware/PROTOCOL.md), "Line stick"):
- One vendor interface.
- Isochronous IN and OUT, with 1 ms packets. The clock of the codec sets
  the size of the packets.
- An interrupt endpoint for events.

At 96 kHz S16, that is 192 bytes each millisecond in each direction (384 at
192 kHz). This is much less than the 1023-byte isochronous packets of Full
Speed. A different PID (pid.codes test PID) and a version bit identify the
stick. Vendor requests: info, rate, codec register access, the level of the
line and a loopback test. There is no hook, ring signal or caller ID.

---

## 6. The modem: a wideband mode for D-Modem

This is a new data pump in D-Modem (`dsp/`). The error correction and
compression of D-Modem operate above it (V.42 LAPM, V.44), as for all other
modes.

**Multicarrier (DMT/OFDM), frequency-division duplex.**
- At 96 kHz: a 512-point FFT gives 256 tones, with 187.5 Hz between them.
  A cyclic prefix of 40 samples. About 174 symbols each second. Tones
  about 21 to 192 (4–36 kHz) are usable (B4).
- **The two directions on separate tones:** the end that dialled sends on
  the lower band. The end that answered sends on the upper band. A guard
  band is between them. The split is set at training from the measured
  capacity of the two directions. Thus the two directions get about the
  same rate.
- **Bit loading for each tone:** each receiver measures the
  signal-to-noise ratio of each tone during training. Then it tells the
  sender the number of bits for each tone (0 to 12) and the gain, as ADSL
  does. Long pairs lose their top tones. Short pairs use all tones.
- **Pilot tone and clock tracking:** a pilot in each direction lets the
  receiver follow the difference between the two crystals. (The receiver
  rotates the phase of each tone, and it slips a sample from time to
  time.)
- **Coding:** Reed-Solomon (`dsp/rs.cpp` of D-Modem) with interleaving.
  Possibly trellis coding later, if it gives a sufficient improvement.
- **Start-up:**
  1. The caller sends a probe tone.
  2. The answerer (auto-answer on a leased line, `AT&L1`, or `ATA`)
     replies.
  3. Then the training sequence: sync symbols, the SNR measurement, and the
     exchange of the bit tables on a robust 2 bits-per-tone channel.
  4. Then data.

  If the modem loses sync, it retrains. During a call, the modem adapts
  the bit tables (bit swapping) when the noise of the pair changes.

We measured these rates on a simulated pair (D-Modem `vwb_test`: 24 AWG
cable loss, −15 dB hybrid echo, crystals with 100 ppm of difference, no bit
errors), in each direction at 96 kHz:

| Pair | Rate |
|---|---|
| 0.5 km, quiet | 148.7 kbit/s (all usable tones at 12 bits: the maximum at 96 kHz) |
| 1 km | 125–128 kbit/s |
| 3 km | about 100 kbit/s |
| 5 km | 70–73 kbit/s |

Through V.42 in the two directions at the same time: 137.6 kbit/s of data
in each direction on 0.5 km. We did not try this on a real pair at this
time. The transmit level of the stick (§3, about +3 dBm) and its noise will
set the real values.

**Leased-line behaviour (Hayes `&L1`):** no dial tone, ring signal or hook.
`ATD` (any number) starts the modem as the caller. `ATA`, or `ATS0=1`,
waits for the probe of a caller. If the carrier is lost, the modem sends
`NO CARRIER`. With `&L1`, the modem tries again automatically.

**Test plan:**
1. Test the two pumps against each other through a simulated pair. Include
   frequency-dependent loss (a cable model), noise (white and narrowband),
   the leakage of the hybrid, and crystals with 100 ppm of difference.
2. Then test two sticks on a real pair (a drum of telephone cable).

---

## 7. Host software

- `dmodemd --usb` finds a WB stick in addition to the SmartUSB56 and the
  Line stick. `-r 96000` is the default for it.
- The core runs at the rate of the line. The pumps of D-Modem already take
  their sample rate from the core (`modem_dp` `srate`). The 8000/9600-only
  checks of the line drivers and of the options become wider for this
  device.
- dmodem-app: the same, through the USB host API of Android and libusb.

## 8. Parts

We compared these parts with the JLCPCB/LCSC stock on 2026-10-27.

| Role | Part | LCSC | Package | Notes |
|---|---|---|---|---|
| Codec | TI TLV320AIC3204IRHBR | C24109 | QFN-32 5×5 | I2C 0x18; LDOIN 3.3 V, internal LDOs (LDO_SELECT high) |
| Line transformer | Pulse PE-65848NLT | C7261159 | SMD-8 12×12, 8.6 mm high | 1500 Vrms, Lp 3.5–6.5 mH, split line winding; 250 in stock |
| Transformer, second source | Pulse PE-65812NLT | C7498702 | SMD-8 12×12 | 2000 V, 2.5 mH, low corner ~3.8 kHz |
| Sidactor | Littelfuse P3100SCLRP | C151191 | DO-214AA | the same as the Line stick; 70 pF, no effect at 100 kHz |
| Fuse | Littelfuse 04611.25ER | C99554 | SMD | the same as the Line stick |
| Ferrites ×2 | Murata BLM18AG601SN1D | C19330 | 0603 | less than 1 Ω at 100 kHz |
| DC block ×2, in series | FS43X105K251EGG 1 µF 250 V X7R | C381468 | 1812 | 0.5 µF, 500 V together (a live line has peaks of ~260 V); in the split winding of the transformer (TIP–8, 7–C–6, 5–RING) |
| Codec-side TVS | Yangjie ESDSLC5V0LB | C2762104 | DFN1006-2 | across the codec winding, 0.8 pF |
| Codec-side clamps ×2 | Nexperia BAV99 | C2500 | SOT-23 | each line node to 3.3 V and ground |
| Drive and hybrid | 60.4 Ω, 604 Ω, 1.21 kΩ 1 %; 10 nF C0G (no 12 nF C0G in stock) | C2933247, C32183, C22835, C2971995 | 0603, 0805 | §3 |

> **CAUTION: CONNECT THE WB STICK ONLY TO A BARE PAIR. DO NOT CONNECT IT
> TO A LIVE TELEPHONE LINE.** A live line has −48 V and peaks of about
> 260 V when it rings.

If you connect the stick to a live phone line by mistake, there is no DC
path. Thus the exchange sees the stick as on-hook. The ring signal drives
about 6 mA through it.

Examine these items on the bench:
- The datasheet of the transformer gives no bandwidth curve or saturation
  rating. Sweep one transformer from 1 to 200 kHz.
- The hybrid values are only a start point, from typical cable data.

## 9. Not this stick: ADSL and SHDSL

ADSL uses 25 kHz–1.1 MHz (ADSL2+ 2.2 MHz). This is about 4.4 million
samples each second in each direction. That is more than the Full Speed USB
of the RP2040 can send. ADSL also needs an ADSL analogue front end. To get
to an ISP, it needs the full G.992 standard (DMT, its training, ATM)
against a DSLAM. SHDSL has the same problems. If necessary, this can be a
study on a different chip in the future.

## 10. Next steps

1. Parts (§8), then the netlist, schematic and PCB (`--variant wb`).
2. Firmware: the codec through I2C, I2S in PIO, the USB of the Line stick
   at 96/192 kHz.
3. The modem in D-Modem. Develop it first with the simulated pair.
4. Two sticks on a real pair.
