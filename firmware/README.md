# pico-serial firmware

This is the RP2040 firmware for the DB25 hood board (SPEC.md §5): a USB CDC
serial port with a bridge to the DB25. The firmware finds automatically if
a computer (DCE role) or a modem (DTE role) is attached. Then it switches on
the matching RS-232 chip. As DCE, it also learns the baud rate of the
computer from the characters that the computer types.

The same firmware runs pico-serial W (SPEC-W.md), which adds Bluetooth: BLE
for iPhone and classic Bluetooth serial (SPP) for Android. See
[Bluetooth (W board)](#bluetooth-w-board).

The same source tree also builds a different firmware for pico-serial Line,
the phone-line stick (SPEC-L.md). This firmware is a USB front end for its
Si3056 DAA. The modem itself is D-Modem on the host. See
[Line stick](#line-stick). Its sibling for bare copper pairs, pico-serial WB
(SPEC-WB.md), does the same for a TLV320AIC3204 codec at 96 kHz. See
[WB stick](#wb-stick).

## Build

You need CMake 3.13+, Ninja or Make, and the Arm GNU toolchain (`arm-none-eabi-gcc`).

```sh
cmake -S firmware -B firmware/build -G Ninja      # -DPS_BOARD=hood (default), w, line, wb or pico
ninja -C firmware/build
# -> firmware/build/pico-serial-hood.uf2
```

- **Dependencies:** CMake downloads Pico SDK **2.3.1** and TinyUSB
  **0.21.0** from GitHub. To use local copies, give `-DPICO_SDK_PATH=…` and
  `-DPICO_TINYUSB_PATH=…`.
- **Why a newer TinyUSB:** the SDK includes an older TinyUSB. Only 0.19+
  can send CDC serial-state notifications (DCD/DSR/RI to the host).
- **Boards:**
  - `hood`: the real board, with 16 MB flash and the start-up margin for a
    slow crystal.
  - `w`: pico-serial W, the circuit of the hood board and the RM2 radio,
    wired as on Pico W. It adds BTstack and the firmware of the radio (the
    `.uf2` is about 900 KB).
  - `line`: the phone-line stick: `src/line` and the portable
    `src/core/daa_frame.c`, `line_dev.c`, in place of the RS-232 core
    (about 31 KB of code, a 54 KB `.uf2`).
  - `wb`: the wideband bare-pair stick: `src/wb` and the portable
    `src/core/wb_codec.c`, `wb_audio.c`, `wb_dev.c`, with the USB device
    (`src/stick`) and sample streams of the Line stick (about 33 KB of code,
    a 58 KB `.uf2`).
  - `pico`: a bench rig with a Raspberry Pi Pico, wired to the two
    transceivers as in SPEC.md §4.5. The GPIOs are the same. The LEDs move
    from GP23/24, because the Pico uses these pins.

## Flash

- **First time:** hold BOOTSEL and connect USB. Then copy the `.uf2` to the
  RPI-RP2 drive.
- **After that:** run `picotool reboot -u -f`, then copy the file. The board
  has the reset interface of Raspberry Pi, thus it is not necessary to push
  a button.
- **No 1200-baud reboot:** the "set 1200 baud to reboot" function is
  disabled, because 1200 baud is a real modem speed.

## What it does

| | |
|---|---|
| USB | Composite device: CDC serial port, the **pico-serial control** vendor interface ([PROTOCOL.md](PROTOCOL.md)), and the picotool reset interface |
| Role | `auto` (default): with the two chips off, the firmware samples the STATUS of chip A (a computer drives pins 2/4/20) and the INVALID of chip B (a modem drives pins 3/5/6/8/22) each 50 ms. Three matching samples in sequence switch on that chip. If the peer is missing for 1 s, the firmware switches the chip off again. If the two appear present, the computer has priority. You can also force `dce`/`dte`. The flash keeps the setting |
| DCE lines | Host DTR → **DCD and DSR** of the computer; host RTS → **CTS** of the computer. CTS is also held off while the buffer to the host is ¾ full. RI comes only from `SET_OUTPUTS` |
| DTE lines | Host DTR → DTR of the modem; host RTS → RTS of the modem (the same buffer hold-off) |
| To the host | CDC SERIAL_STATE. As DCE: DSR = the DTR of the computer, DCD = a computer is attached. As DTE: the DCD, DSR, RI of the modem. Framing, parity, overrun and break events also go to the host |
| Flow control | `auto`: obeys the flow line of the far end only after the far end asserts it. Thus 3-wire cables still operate. You can force `on` and `off` |
| Baud rate | `auto` (default): as DCE, the firmware **learns** the rate from the characters of the computer. As DTE, it uses the rate of the host. You can set `host` and `learn` (for the two roles). The flash keeps the setting. See below |
| Line coding | 5–8 data bits, none/odd/even parity, 1 or 2 stop bits. The firmware maps mark/space parity and 1.5 stop bits to the nearest format that the UART can do, and shows a flag in GET_INFO. It does the same for speeds above the 250 kbit/s of the transceivers |
| Break | CDC SEND_BREAK, timed or until cleared |
| LEDs | PWR, TR, CD, SD, RD, RI, as on the front panel of a modem (optional on the board) |
| Safety | The two transceivers start in the off condition (as the pull-downs of the board also make sure). They are never on at the same time. Watchdog 1 s |
| USB IDs | `1209:0001`, the shared **test** PID of pid.codes. Get a real PID before you give boards to other persons (`src/rp2040/board_config.h`) |

## Bluetooth (W board)

`src/rp2040/bt.c` is BTstack on the RM2 (CYW43439). The main loop polls it,
as it polls the USB code. Thus no Bluetooth code runs in an interrupt.

| | |
|---|---|
| Name | `pico-serial-XXXX`, XXXX = the end of the Bluetooth address |
| Android, desktops | Classic **SPP** server on RFCOMM channel 1, "pico-serial". The "paired device" mode of the D-Modem app operates without changes |
| iPhone, all BLE devices | **Nordic UART Service**: write to RX (`6E400002…`), notifications from TX (`6E400003…`). Generic BLE UART apps operate |
| Control | The pico-serial control service through BLE: line coding, outputs, inputs (notify), mode, break, flow, credits (notify), info. See [PROTOCOL.md](PROTOCOL.md#bluetooth-w-board) |
| Who owns the port | USB while its port is open (DTR set). If not, the wireless link that connected first. The other links stay connected, but they are read-only (the firmware refuses BLE writes and discards SPP data). `src/core/link.c` |
| Modem lines | A wireless link counts as DTR and RTS on. Thus, as DCE, the computer sees DCD and DSR while a phone is connected. Through SPP, DCD follows the `CONNECT` / `NO CARRIER` result codes of the phone (Android cannot send modem lines) |
| Pairing | Only in a **pairing window**: 2 minutes after power-up while no device is bonded, 2 minutes after you push the PAIR button, or on a request through USB (`BT_PAIRING`). Outside the window, only bonded phones can connect. The flash keeps a maximum of 4 bonds. To clear all bonds, hold PAIR for 10 s (or use `BT_FORGET` through USB) |
| Security | LE Secure Connections and classic Secure Simple Pairing, "Just Works". Data and control need an encrypted link. A phone that is not bonded and connects outside the window cannot use them |
| Flow control | SPP: the firmware gives RFCOMM credits only while the buffer to the DB25 has space for a full frame. BLE: the firmware cannot stop GATT writes. Thus apps must control their rate with the Credits characteristic |
| LEDs | BT: slow flash while advertising, fast flash while the pairing window is open, on while a phone is connected. TR, CD on GPIOs; RD, SD on the own GPIOs of the RM2 |
| Flash | Settings in the last sector, as before; Bluetooth bonds in the two sectors below it |

**Not done at this time** (SPEC-W.md §4):
- The BLE L2CAP channel for a dedicated iOS app (§4.2).
- The optional static passkey (§4.5), and the name change through USB
  (§4.1).
- DTR drop → `ATH` hang-up through SPP (§4.4, off by default).
- The request to iOS for the 2M PHY (data length extension is on).
- Wi-Fi.

## Automatic baud rate

As DCE, the computer types at the speed of its terminal, and the board
finds that speed. The board ignores the SET_LINE_CODING speed of the host,
but it uses the data bits, parity and stop bits of the host. As DTE, the
board uses the speed of the host, because a modem uses the speed of the
data that it receives. `learn` mode enables the learning function for DTE
too (for a device that sends first, such as a GPS).

**Measurement.** A PIO state machine (`src/rp2040/autobaud.pio`) monitors
the RX pin while the UART keeps the pin. The PIO can read the input of all
pins, also when a pin has a different function. From the first falling
edge, it measures the time of each low and high period, in units of 2
system clocks (16 ns at 125 MHz). An interrupt sends these times to the
core.

**Decision** (`src/core/autobaud.c`). For each standard rate, 110 to
230 400 baud, the core decodes the timings as a UART does, with the format
of the host. A rate fits if these conditions are true:
- A bit is not longer than the shortest period.
- The level changes in each character occur on bit boundaries.
- A maximum of 1 in 8 characters has a framing or parity error.

From the rates that fit, the core selects the slowest rate whose changes
are approximately as near to bit boundaries as those of the best rate.
This prevents a rate that is too fast but decodes by chance. It also
prevents a slower rate that fits characters with no 1-bit run. The core
decides when 2 characters decode at that rate. The core counts the
characters only at the selected rate, because a rate that is too fast can
divide one character into two.

**Then:**
- The core waits until the line is idle for the time of one character.
  Thus the change never occurs in the middle of a character.
- The core decodes the characters that it learned from the timings and
  sends them to the host. Thus "AT" is not lost.
- The UART changes to the new speed, and GET_INFO shows it.

**Learn again:** after 3 framing errors in 2 s (the computer changed the
speed), on the AUTOBAUD_RESTART request, or when the firmware detects a
new peer. Until the firmware knows the new speed, the board continues to
send at the old speed and holds the data that it receives.

**Limits:**
- **Two characters** are sufficient: "AT", or Enter as CR LF, or two pushes
  of Enter on a terminal that sends only CR. One character alone is not
  sufficient. With so few characters, very noisy lines can cause an
  incorrect result on rare occasions (see the tests below). An incorrect
  result causes framing errors, and then the firmware learns the rate
  again.
- **Standard rates only.** A sender that is up to about 3% different from a
  standard rate is satisfactory.
- **Not the format.** 7E1 and 8N1 characters have the same length. Thus
  the speed is correct for the two formats, but the bytes are correct only
  with the format of the host.
- **Lost characters:** while the firmware learns again, the characters that
  came at the old speed are lost. Characters are also lost if there is no
  decision after more than ~40 characters (the firmware then starts again
  from the newest character).

## Layout

```
src/core/       portable: role detection, line mapping, flow control, baud-rate
                learning, settings, which host link owns the port (no SDK)
src/rp2040/     HAL (GPIO, UART + IRQ, PIO line timing, flash), USB descriptors
                and callbacks, Bluetooth (W board: bt.c, the GATT database,
                BTstack config), main
src/line/       the line stick: the Si3056's pins, clock and serial port (PIO),
                its part of the USB device (line_stick.c), main
src/wb/         the WB stick: the codec's pins, MCLK, I2C, its I2S frames (PIO
                and DMA), its part of the USB device (wb_stick.c), main
src/stick/      the sticks' USB device (usb_stick.c: descriptors, the class
                driver, Microsoft OS 2.0) and what it needs from a stick (stick.h)
src/core/       (also) sstream.c: the sticks' sample streams; daa_frame.c,
                line_dev.c: the line stick's frame engine and device logic;
                wb_codec.c, wb_audio.c, wb_dev.c: the WB stick's codec
                bring-up, samples and device logic; portable
src/generated/  board_pins.h, board_pins_w.h, board_pins_line.h and
                board_pins_wb.h, written by hardware/netlist/build_netlist.py
boards/         SDK board headers for the hood, W, line and WB boards
test/           host tests for the core (fake HAL) and the emulator test
tools/          check_docs.py: every function has a comment (CI runs it)
```

Each function has a comment that tells what it does. For public functions,
the comment is on the prototype in the header. For static functions, the
comment is above the definition. `python3 firmware/tools/check_docs.py`
shows the functions that do not have a comment, and CI fails on them.

The GPIO numbers come from the hardware netlist script. If you change the
board in the script, the pin header also changes.

## Tests

```sh
make -C firmware/test run                                   # core, baud detector, host links, the sticks; sanitizers on
firmware/test/emu/run.sh firmware/build/pico-serial-hood.bin   # whole firmware in an emulator
```

- **Host link tests** (`test_link.c`): the owner of the port when USB, BLE
  and SPP connect and disconnect; the line coding of a link is kept until
  the link owns the port; DCD from `CONNECT` / `NO CARRIER`, also when a
  code is divided across writes, but not from text in longer lines.
- **Host tests:** the logic of the core against a fake board (27 tests).
  We examined the tests: we broke the core intentionally and the tests
  found each break. These were the breaks: no DCE detection, no buffer
  hysteresis, no DSR mapping, no one-shot error events, no hold of data
  during learning, no new learning after errors.
- **Baud detector tests** (`test_autobaud.c`): waveforms made from text and
  random bytes at all standard rates, in 8N1, 7E1, 7O1, 8N2 and 8E1. The
  sender is up to 2.5% off, and the edges move by up to 10% of a bit. Also:
  glitches, a capture that starts in the middle of a character, and
  characters with no single-bit run.
- **Stress run** (not in CI): 20 000 random messages for each setting.
  The edges move by up to 8% of a bit, and the sender is up to 3% off.
  - Printable text, 2 to 12 characters: no incorrect rate.
  - Random bytes: no incorrect rate from 5 characters up. With only 2
    characters, 2 in 20 000 were incorrect.
  - CR LF, and CR CR: no incorrect rate, all decoded.
  - One character alone: the firmware waits, except 5–7 in 20 000 at 5%
    jitter (none without jitter).
- **Emulator test:** the test boots the real `.bin` in
  [rp2040js](https://github.com/wokwi/rp2040js). The emulator is the USB
  host, the detect lines of the transceivers and the modem lines of the far
  end. The test examines these items:
  - Enumeration.
  - DCE detection and the chip-enable pins.
  - The DCD/DSR/CTS mapping.
  - Data in the two directions through the UART.
  - SERIAL_STATE notifications.
  - GET_INFO and GET_INPUTS.
  - DTE mode when it is forced, and its line mapping.
  - A settings save without a hang or a watchdog reset.
  - The learning of the baud rate: the test sends "AT\r" on GP1 at 9600
    baud (1% fast) and later "ATI3\r" at 115200 (2% slow). The text gets
    to the host, the UART changes speed, and GET_INFO shows the speed.
    rp2040js runs PIO on its own timer. Thus the test moves PIO by one step
    for each CPU cycle to keep its timing correct.

## Line stick

`-DPS_BOARD=line`: the firmware of pico-serial Line (SPEC-L.md), a USB
softmodem front end. The driver of the host sees one vendor interface. It
has an isochronous pair for the samples of the line and an interrupt
endpoint for events. Its shape is the same as the SmartUSB56 of Smart Link.
Thus the driver of D-Modem for the SmartUSB56 is the model. The protocol is
in [PROTOCOL.md](PROTOCOL.md#line-stick-pico-serial-line). On Windows,
Microsoft OS 2.0 descriptors bind WinUSB to it, with no INF.

- **Bring-up:**
  1. MCLK is the 48 MHz of the USB PLL on GPIO21 (no fractional divider:
     a clean clock).
  2. The firmware resets the Si3056 with M0 = M1 = 0 (serial mode 0, the
     RGDT pin driven low through reset). Its frames start at the PLL
     setting of the reset.
  3. Then the firmware sets PLL1 (N = 125, M = 256: 98.304 MHz), the rate
     (9600 Hz), 16-bit mode (FC pin requests from then on), and powers up
     the line side.
  4. FDT (register 12) shows that the link across the barrier operates.
     The firmware examines it again each second.
- **Frames:** `si3056.pio` is the slave of the serial port, and it follows
  SCLK and FSYNC. Each word goes through `daa_frame.c` from the interrupt
  of the PIO (highest priority). The primary word is the sample from the
  line and the sample from the modem. A register access is a secondary word
  in the same frame.
- **Hook** on the OFHK pin (with a pull-up: on hook when the firmware does
  not run). **Ring detector** on RGDT (the firmware counts its edges for
  the host). The **interrupt** of the DAA on AOUT/INT (the firmware reads
  register 4 and clears it). LEDs: OH while off hook, RI with the ring
  detector.
- **Tests** (`test_line.c`):
  - The frame engine against a model of the serial port of the Si3056
    (Figures 27-30: the LSB flag in 15-bit mode, the FC pin in 16-bit
    mode, register reads and writes).
  - The sample streams (sequence, head start, under- and overruns, samples
    kept in sequence across register accesses).
  - The device logic and requests on a fake board.

  We examined the tests: we broke the frame engine intentionally, and the
  tests found each break. These were the breaks: the LSB not cleared in
  15-bit mode, a sample taken on a secondary word, FC never dropped.

**Not verified on hardware:** we have no Si3056, and the emulator has none.
These items are not yet tried: the PIO timing (SDI out after SCLK rises,
SDO in as it falls; ~50 ns from FSYNC to the first bit, against a
half-period of 200 ns at 9600 Hz), the PLL setting at reset, the
isochronous endpoints with a real host, and the driver of the host.

## WB stick

`-DPS_BOARD=wb`: the firmware of pico-serial WB (SPEC-WB.md), the wideband
bare-pair stick. It uses the USB device of the Line stick (`src/stick`, the
same interface, endpoints and Microsoft OS 2.0 set; USB `1209:0003`) in
front of a TLV320AIC3204 codec at 96 kHz (48 and 192 kHz for tests). The
protocol is in [PROTOCOL.md](PROTOCOL.md#wb-stick-pico-serial-wb).

- **Clocks:** MCLK is the 12 MHz crystal itself on GPIO21 (GPOUT0, not
  divided). The PLL of the codec makes 98.304 MHz (P = 1, R = 1, J.D =
  8.1920); NDAC = NADC = MDAC = MADC = 4; DOSR = AOSR = 64 with PRB_P7 and
  PRB_R7 at 96 kHz (32, PRB_P17, PRB_R13 at 192; 128, PRB_P1, PRB_R1 at
  48); BCLK = DAC_CLK / 4 = 6.144 MHz, 64 in each frame. The codec is the
  I2S master. Thus the samples run on the crystal of the stick and USB
  follows them, as on the Line stick.
- **Bring-up** (`wb_codec.c`, in the sequence of SLAA557):
  1. Reset (pin, then software).
  2. PLL.
  3. Dividers, OSR, processing blocks.
  4. I2S master with 32-bit words.
  5. INT1 (overflows, over current) on MFP5.
  6. The weak AVDD link off, and the A-LDO on at 1.77 V.
  7. Common mode 0.9 V.
  8. The routing of the drivers (the + of the left DAC to HPL, its − to
     HPR: one DAC, a differential line drive), 0 dB, powered up. Then the
     firmware waits for their gain flags (a maximum of 500 ms).
  9. Then the left DAC and the two ADCs are powered and unmuted. The
     firmware waits for their power flags and the gain flags of the
     MicPGAs (a maximum of 200 ms).
- **The hybrid** is in the left MicPGA: + is IN1L (line A) and IN1R
  (replica B), − is IN2R (line B) and IN3R (replica A), 10 kΩ each (page 1
  registers 52 = `0x41`, 54 = `0x14`). Thus the left ADC receives the line
  minus the replica of our signal. The right MicPGA receives only the line
  (IN2R − IN1L, 20 kΩ: registers 55 = `0x20`, 57 = `0x20`). It is not
  possible to route only the replica differentially (its two nodes connect
  only to the + side of the right MicPGA). The host gets one channel (the
  hybrid by default), and the peaks of the two channels.
- **Frames:** `i2s.pio` is the I2S slave, full duplex, and it follows BCLK
  and WCLK. DMA moves the words in the two directions in blocks of 32
  frames, ping-pong. `wb_audio.c` runs from the interrupt of the DMA
  (highest priority). If a frame is out of step (WCLK is not where 32 bits
  on each side put it), the PIO stops. The main loop starts it again and
  counts the event.
- **LEDs:** LINK as the host sets it (`SET_CARRIER`; by default while the
  samples stream). DATA while the samples of the host are not silent (and
  for 50 ms after).
- **Tests** (`test_wb.c`):
  - The bring-up against a model of the registers of the codec.
  - The clocks of each rate, calculated again from the registers that the
    firmware wrote, and compared with the limits of SLAA557 (PLL input and
    output ranges, the clock of each divider, DOSR × fs, the resource
    classes and filters of the blocks).
  - The MicPGA routing bits, compared with the netlist of the board
    (left = line − replica, right = the line).
  - The sequence of the bring-up, failures and timeouts.
  - Packet sizes, the samples to and from the frames, the peaks, the
    firmware loopback.
  - The device logic and requests on a fake board.

  We examined the tests: we broke the code intentionally, and the tests
  found each break. These were the breaks: PLL D off by one, an incorrect
  block or BCLK divider, a MicPGA input on an incorrect pin or resistance,
  HPR on the right DAC, no wait for the flags of the drivers or the PLL,
  the weak AVDD link left on, one TX slot only, the channels swapped,
  SET_RATE in Hz, the page select writable, overflow flags never cleared.

**Levels:** at 0 dBFS, the drivers put 1.0 Vrms between HPL and HPR, and
0.5 Vrms on a 120 Ω pair through the 2 × 60.4 Ω. This gives +3.2 dBm for a
full-scale sine, and about +4.8 dBm where the drivers clip on AVDD
(1.77 V). The "about +8 dBm at most" of SPEC-WB needs the drivers on LDOIN
(3.3 V, common mode 1.65 V). SLAA557 permits this only single-ended
("Differential Loading: supported only when using the headphone-output
stage powered from the AVDD supply"). Also, SLAA557 specifies AVDD at
1.8 V or more for the 0.9 V common mode, but the A-LDO gives a maximum of
1.77 V. We keep 0.9 V for the swing. If the bench shows distortion near
full scale, 0.75 V (page 1 register 10 = `0x40`) gives 2.5 dB less swing
for more headroom.

**Not verified on hardware:** there is no WB board at this time, and the
emulator has no codec. These items are not yet tried:
- The PIO timing (the codec takes the bit ~25 ns after BCLK rises, before
  it changes ≥ 40 ns later at 192 kHz; our output is ~40 ns after the rise,
  and 8 ns of setup is necessary).
- The real power-up times of the codec.
- The 12 MHz GPOUT as MCLK.
- The isochronous endpoints at 480 bytes with a real host.
- The levels above.

## Not yet verified

- **On real hardware:** the firmware has not run on real hardware at this
  time. The first board or bench rig is the real test.
- **Flash write:** the emulator does not model flash programming. Thus we
  did not examine if a saved mode stays after a power cycle.
- **UART pin routing:** the emulator also does not model the UART-to-GPIO
  routing. Thus we did not examine the swap between the UART pins of chip A
  and chip B.
- **Baud learning on real lines:** real transceivers make low and high
  periods that are slightly unequal (slew). The detector permits 5/16 of a
  bit, which is probably sufficient at 230 400 baud. But it must be
  examined on the bench.
- **Real host drivers:** we did not try usbser of Windows, macOS and Linux
  cdc_acm, and usb-serial-for-android of Android.
- **Bluetooth:** the W firmware builds, but no radio function has run. The
  emulator has no CYW43, and there is no W board or Pico W rig at this time.
  The first test is a Pico W wired to the transceivers (SPEC-W.md §9):
  pairing from an iPhone and from Android, the D-Modem app through SPP, the
  ownership rules, and throughput.
- **Not built at this time:**
  - The remaining items of SPEC-W.md §4, listed under
    [Bluetooth](#bluetooth-w-board).
  - Microsoft OS descriptors, so that Windows binds WinUSB to the control
    interface without Zadig.
