# pico-serial firmware

RP2040 firmware for the DB25 hood board (SPEC.md §5): a USB CDC serial port
bridged to the DB25. It works out by itself whether a computer (DCE role) or
a modem (DTE role) is attached, and switches on the matching RS-232 chip.
As DCE it also learns the computer's baud rate from its typing.

The same firmware runs pico-serial W (SPEC-W.md), which adds Bluetooth: BLE
for iPhone and classic Bluetooth serial (SPP) for Android. See
[Bluetooth (W board)](#bluetooth-w-board).

pico-serial Line, the phone-line stick (SPEC-L.md), builds from the same
tree into different firmware: a USB front end for its Si3056 DAA, the
modem itself being D-Modem on the host. See [Line stick](#line-stick).

## Build

Needs CMake 3.13+, Ninja or Make, and the Arm GNU toolchain (`arm-none-eabi-gcc`).

```sh
cmake -S firmware -B firmware/build -G Ninja      # -DPS_BOARD=hood (default), w, line or pico
ninja -C firmware/build
# -> firmware/build/pico-serial-hood.uf2
```

- **Dependencies:** CMake fetches Pico SDK **2.3.1** and TinyUSB **0.21.0**
  from GitHub. To use local copies instead, pass `-DPICO_SDK_PATH=…` and
  `-DPICO_TINYUSB_PATH=…`.
- **Why a newer TinyUSB:** the SDK bundles an older TinyUSB, and only
  0.19+ can send CDC serial-state notifications (DCD/DSR/RI to the host).
- **Boards:**
  - `hood`: the real board, with 16 MB flash and the slow-crystal start-up
    margin.
  - `w`: pico-serial W, the hood board's circuit plus the RM2 radio wired as
    on Pico W. Adds BTstack and the radio's firmware (the `.uf2` is about
    900 KB).
  - `line`: the phone-line stick: `src/line` and the portable
    `src/core/daa_frame.c`, `line_dev.c` instead of the RS-232 core (about
    31 KB of code, a 54 KB `.uf2`).
  - `pico`: a bench rig built on a Raspberry Pi Pico wired to the two
    transceivers as SPEC.md §4.5. Same GPIOs. The LEDs move off GP23/24,
    which belong to the Pico.

## Flash

- **First time:** hold BOOTSEL, plug in USB, copy the `.uf2` to the RPI-RP2
  drive.
- **After that:** run `picotool reboot -u -f`, then copy. The board has
  Raspberry Pi's reset interface, so no button press is needed.
- **No 1200-baud reboot:** the "set 1200 baud to reboot" trick is off,
  because 1200 baud is a real modem speed.

## What it does

| | |
|---|---|
| USB | Composite device: CDC serial port, the **pico-serial control** vendor interface ([PROTOCOL.md](PROTOCOL.md)), and the picotool reset interface |
| Role | `auto` (default): samples chip A's STATUS (a computer drives pins 2/4/20) and chip B's INVALID (a modem drives pins 3/5/6/8/22) every 50 ms with both chips off. Three matching samples in a row switch on that chip. The peer missing for 1 s switches it off again. A computer wins if both look present. `dce`/`dte` can also be forced; the setting is saved in flash |
| DCE lines | Host DTR → computer's **DCD and DSR**; host RTS → computer's **CTS**, also held off while the to-host buffer is ¾ full. RI comes only from `SET_OUTPUTS` |
| DTE lines | Host DTR → modem's DTR; host RTS → modem's RTS (same buffer hold-off) |
| To the host | CDC SERIAL_STATE. As DCE: DSR = the computer's DTR, DCD = a computer is attached. As DTE: the modem's DCD, DSR, RI. Framing, parity, overrun and break events also go up |
| Flow control | `auto`: obeys the far end's flow line only once it has asserted it, so 3-wire cables still work. `on` and `off` can be forced |
| Baud rate | `auto` (default): as DCE, **learned** from the computer's characters; as DTE, the host's. `host` and `learn` (both roles) can be set; saved in flash. See below |
| Line coding | 5–8 data bits, none/odd/even parity, 1 or 2 stop bits. Mark/space parity and 1.5 stop bits are mapped to the nearest the UART can do, and flagged in GET_INFO, as are speeds above the transceivers' 250 kbit/s |
| Break | CDC SEND_BREAK, timed or until cleared |
| LEDs | PWR, TR, CD, SD, RD, RI, as on a modem's front panel (optional on the board) |
| Safety | Both transceivers start off (as do the board's pull-downs) and are never on together. Watchdog 1 s |
| USB IDs | `1209:0001`, pid.codes' shared **test** PID. Get a real PID before giving boards to others (`src/rp2040/board_config.h`) |

## Bluetooth (W board)

`src/rp2040/bt.c`, BTstack on the RM2 (CYW43439), polled from the main loop
like the USB code, so nothing runs in an interrupt.

| | |
|---|---|
| Name | `pico-serial-XXXX`, XXXX = the end of the Bluetooth address |
| Android, desktops | Classic **SPP** server on RFCOMM channel 1, "pico-serial". dmodem-android's "paired device" mode works unchanged |
| iPhone, anything BLE | **Nordic UART Service**: write to RX (`6E400002…`), notifications from TX (`6E400003…`). Generic BLE UART apps work |
| Control | The pico-serial control service over BLE: line coding, outputs, inputs (notify), mode, break, flow, credits (notify), info. See [PROTOCOL.md](PROTOCOL.md#bluetooth-w-board) |
| Who owns the port | USB while its port is open (DTR set); otherwise the wireless link that connected first; the others stay connected but read-only (BLE writes are refused, SPP data is dropped). `src/core/link.c` |
| Modem lines | A wireless link counts as DTR and RTS on, so as DCE the computer sees DCD and DSR while a phone is connected. Over SPP, DCD instead follows the phone's `CONNECT` / `NO CARRIER` result codes (Android can't send modem lines) |
| Pairing | Only inside a **pairing window**: 2 minutes after power-up while nothing is bonded, 2 minutes after a press of the PAIR button, or on request over USB (`BT_PAIRING`). Outside it, only bonded phones connect. Up to 4 bonds, kept in flash. Holding PAIR for 10 s forgets them all (also `BT_FORGET` over USB) |
| Security | LE Secure Connections and classic Secure Simple Pairing, "Just Works". Data and control need an encrypted link; a phone that isn't bonded and connects outside the window can't use either |
| Flow control | SPP: RFCOMM credits, given only while the buffer to the DB25 has room for a whole frame. BLE: GATT writes can't be held back, so apps should pace them with the Credits characteristic |
| LEDs | BT: slow blink while advertising, fast blink while the pairing window is open, on while a phone is connected. TR, CD on GPIOs; RD, SD on the RM2's own GPIOs |
| Flash | Settings in the last sector as before; Bluetooth bonds in the two sectors below it |

**Not done yet** (SPEC-W.md §4):
- the BLE L2CAP channel for a dedicated iOS app (§4.2);
- the optional static passkey (§4.5), and the name change over USB (§4.1);
- DTR drop → `ATH` hang-up over SPP (§4.4, off by default anyway);
- asking iOS for the 2M PHY (data length extension is on);
- Wi-Fi.

## Automatic baud rate

As DCE, the computer types at whatever speed its terminal is set to, and
the board works out which. The host's SET_LINE_CODING speed is ignored, but
its data bits, parity and stop bits still apply. As DTE the host's speed is
used, since a modem follows the speed it's spoken to at; `learn` mode turns
learning on there too (for a device that talks first, like a GPS).

**Measuring.** A PIO state machine (`src/rp2040/autobaud.pio`) watches the
RX pin while the UART keeps it: PIO reads every pin's input whatever the
pin is assigned to. From the first falling edge it times each low and high
stretch, in units of 2 system clocks (16 ns at 125 MHz), and an interrupt
passes them to the core.

**Deciding** (`src/core/autobaud.c`). For each standard rate, 110 to
230 400 baud, it decodes the timings as a UART would, with the host's
format. A rate fits if:
- a bit is no longer than the shortest stretch;
- the level changes inside each character fall on bit boundaries;
- at most 1 in 8 characters has a framing or parity error.

Of the rates that fit, it takes the slowest whose changes sit about as
close to bit boundaries as the best one's. That rules out a rate too fast
that happens to decode, and a slower one that fits characters with no
1-bit run. It decides once 2 characters have decoded at that rate. They
are counted only at the chosen rate, because a rate too fast can split one
character into two.

**Then:**
- it waits for the line to go idle for a character's time, so the switch
  never lands in the middle of one;
- the characters it learned from are decoded from the timings and sent to
  the host, so "AT" isn't lost;
- the UART switches to the new speed, and GET_INFO reports it.

**Learning again:** after 3 framing errors within 2 s (the computer
changed speed), on the AUTOBAUD_RESTART request, or when a new peer is
detected. Until the new speed is known, the board keeps sending at the old
one and holds back what it receives.

**Limits:**
- **Two characters** are enough: "AT", or Enter as CR LF, or Enter
  pressed twice on a terminal that sends only CR. One character on its own
  isn't. With so few characters, very noisy lines can rarely fool it (see
  the tests below); a wrong guess shows up as framing errors and is
  re-learned.
- **Standard rates only.** A sender up to about 3% off a standard rate is
  fine.
- **Not the format.** 7E1 and 8N1 characters are the same length, so the
  speed is right either way, but the bytes are only right with the host's
  format.
- **Lost characters:** while it re-learns, characters that came at the old
  speed are lost; so is anything after more than ~40 characters without
  a decision (it then starts over from the newest).

## Layout

```
src/core/       portable: role detection, line mapping, flow control, baud-rate
                learning, settings, which host link owns the port (no SDK)
src/rp2040/     HAL (GPIO, UART + IRQ, PIO line timing, flash), USB descriptors
                and callbacks, Bluetooth (W board: bt.c, the GATT database,
                BTstack config), main
src/line/       the line stick: the Si3056's pins, clock and serial port (PIO),
                its part of the USB device (line_stick.c), main
src/stick/      the sticks' USB device (usb_stick.c: descriptors, the class
                driver, Microsoft OS 2.0) and what it needs from a stick (stick.h)
src/core/       (also) sstream.c: the sticks' sample streams; daa_frame.c,
                line_dev.c: the line stick's frame engine and device logic,
                portable
src/generated/  board_pins.h, board_pins_w.h and board_pins_line.h, written
                by hardware/netlist/build_netlist.py
boards/         SDK board headers for the hood, W and line boards
test/           host tests for the core (fake HAL) and the emulator test
tools/          check_docs.py: every function has a comment (CI runs it)
```

Every function has a comment saying what it does: public ones on their
prototype in the header, static ones above the definition.
`python3 firmware/tools/check_docs.py` lists any that don't, and CI fails
on them.

The GPIO numbers come from the hardware netlist script. Change the board
there, and the pin header follows.

## Tests

```sh
make -C firmware/test run                                   # core, baud detector, host links, line stick; sanitizers on
firmware/test/emu/run.sh firmware/build/pico-serial-hood.bin   # whole firmware in an emulator
```

- **Host link tests** (`test_link.c`): who owns the port as USB, BLE and
  SPP come and go; line coding kept for a link until it owns the port;
  DCD from `CONNECT` / `NO CARRIER`, split across writes, and not from
  text inside longer lines.
- **Host tests:** the core's logic against a fake board (27 tests). The
  tests were checked by breaking the core on purpose: removing DCE
  detection, the buffer hysteresis, the DSR mapping, the one-shot error
  events, holding back data while learning, the re-learn on errors. Each
  break is caught.
- **Baud detector tests** (`test_autobaud.c`): waveforms made from text and
  random bytes at every standard rate, in 8N1, 7E1, 7O1, 8N2 and 8E1, with
  the sender up to 2.5% off and edges moved by up to 10% of a bit. Also:
  glitches, a capture starting mid-character, and characters with no
  single-bit run.
- **Stress run** (not in CI): 20 000 random messages per setting, with
  edges moved by up to 8% of a bit and the sender up to 3% off.
  - Printable text, 2 to 12 characters: no wrong rate.
  - Random bytes: no wrong rate from 5 characters up. With only 2
    characters, 2 in 20 000 were wrong.
  - CR LF, and CR CR: no wrong rate, all decoded.
  - One character alone: waits, except 5–7 in 20 000 at 5% jitter
    (none without jitter).
- **Emulator test:** boots the real `.bin` in
  [rp2040js](https://github.com/wokwi/rp2040js), which plays the USB host,
  the transceivers' detect lines and the far end's modem lines. It checks:
  - enumeration;
  - DCE detection and the chip-enable pins;
  - the DCD/DSR/CTS mapping;
  - data both ways through the UART;
  - SERIAL_STATE notifications;
  - GET_INFO and GET_INPUTS;
  - forcing DTE mode, and its line mapping;
  - getting through a settings save without a hang or watchdog reset;
  - learning the baud rate: "AT\r" driven onto GP1 at 9600 baud (1% fast)
    and later "ATI3\r" at 115200 (2% slow) reach the host, the UART
    switches, and GET_INFO reports it. rp2040js runs PIO on its own timer,
    so the test steps PIO once per CPU cycle to keep its timing true.

## Line stick

`-DPS_BOARD=line`: the firmware of pico-serial Line (SPEC-L.md), a USB
softmodem front end. The host's driver sees one vendor interface with an
isochronous pair for the line's samples and an interrupt endpoint for
events, shaped like Smart Link's SmartUSB56 so D-Modem's driver for that is
the model; the protocol is in [PROTOCOL.md](PROTOCOL.md#line-stick-pico-serial-line).
Microsoft OS 2.0 descriptors bind WinUSB to it on Windows, with no INF.

- **Bring-up:** MCLK is the USB PLL's 48 MHz on GPIO21 (no fractional
  divider: a clean clock). The Si3056 is reset with M0 = M1 = 0 (serial mode
  0, the RGDT pin driven low through reset), its frames start at the reset
  PLL setting, then PLL1 is set (N = 125, M = 256: 98.304 MHz), the rate
  (9600 Hz), 16-bit mode (FC pin requests from then on), and the line side
  powered up. FDT (register 12) says the link across the barrier works; it's
  checked again every second.
- **Frames:** `si3056.pio` is the serial port's slave, following SCLK and
  FSYNC; every word goes through `daa_frame.c` from the PIO's interrupt
  (highest priority): the primary word is the line's sample in and the
  modem's out, and a register access is a secondary word in the same frame.
- **Hook** on the OFHK pin (pulled up: on hook whenever the firmware
  isn't running), **ring detector** on RGDT (its edges counted for the
  host), the DAA's **interrupt** on AOUT/INT (register 4 read out and
  cleared). LEDs: OH while off hook, RI with the ring detector.
- **Tests** (`test_line.c`): the frame engine against a model of the
  Si3056's serial port (Figures 27-30: the LSB flag in 15-bit mode, the FC
  pin in 16-bit mode, register reads and writes), the sample streams
  (order, head start, under- and overruns, samples kept in order across
  register accesses) and the device logic and requests on a fake board.
  Checked by breaking the frame engine on purpose: not clearing the LSB in
  15-bit mode, taking a sample on a secondary word, never dropping FC.
  Each break is caught.

**Not verified on hardware:** no Si3056 here, and the emulator has none.
The PIO timing (SDI out after SCLK rises, SDO in as it falls; ~50 ns from
FSYNC to the first bit, against a half-period of 200 ns at 9600 Hz), the
reset-time PLL setting, the isochronous endpoints with a real host and the
host's driver are all still to be tried.

## Not yet verified

- **On real hardware:** nothing has run on one yet. The first board or
  bench rig is the real test.
- **Flash write:** the emulator doesn't model flash programming, so a
  saved mode surviving a power cycle is unchecked.
- **UART pin routing:** the emulator doesn't model the UART-to-GPIO
  routing either, so the swap between chip A's and chip B's UART pins is
  unchecked.
- **Baud learning on real lines:** real transceivers make low and high
  stretches slightly unequal (slew). The detector allows for 5/16 of a
  bit, which should cover this at 230 400 baud, but it needs checking on
  the bench.
- **Real host drivers:** Windows' usbser, macOS and Linux cdc_acm, and
  Android's usb-serial-for-android haven't been tried.
- **Bluetooth:** the W firmware builds, but nothing about the radio has
  run: the emulator has no CYW43, and there's no W board or Pico W rig
  yet. A Pico W wired to the transceivers (SPEC-W.md §9) is the first test:
  pairing from an iPhone and from Android, dmodem-android over SPP, the
  ownership rules, and throughput.
- **Not built yet:**
  - the rest of SPEC-W.md §4 listed under [Bluetooth](#bluetooth-w-board);
  - Microsoft OS descriptors, so Windows binds WinUSB to the control
    interface without Zadig.
