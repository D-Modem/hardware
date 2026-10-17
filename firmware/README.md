# pico-serial firmware

RP2040 firmware for the DB25 hood board (SPEC.md §5): a USB CDC serial port
bridged to the DB25. It works out by itself whether a computer (DCE role) or
a modem (DTE role) is attached, and switches on the matching RS-232 chip.

## Build

Needs CMake 3.13+, Ninja or Make, and the Arm GNU toolchain (`arm-none-eabi-gcc`).

```sh
cmake -S firmware -B firmware/build -G Ninja      # -DPS_BOARD=hood (default) or pico
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
| Line coding | 5–8 data bits, none/odd/even parity, 1 or 2 stop bits. Mark/space parity and 1.5 stop bits are mapped to the nearest the UART can do, and flagged in GET_INFO, as are speeds above the transceivers' 250 kbit/s |
| Break | CDC SEND_BREAK, timed or until cleared |
| LEDs | PWR, TR, CD, SD, RD, RI, as on a modem's front panel (optional on the board) |
| Safety | Both transceivers start off (as do the board's pull-downs) and are never on together. Watchdog 1 s |
| USB IDs | `1209:0001`, pid.codes' shared **test** PID. Get a real PID before giving boards to others (`src/rp2040/board_config.h`) |

## Layout

```
src/core/       portable: role detection, line mapping, flow control, settings (no SDK)
src/rp2040/     HAL (GPIO, UART + IRQ, flash), USB descriptors and callbacks, main
src/generated/  board_pins.h, written by hardware/netlist/build_netlist.py
boards/         SDK board header for the hood board
test/           host tests for the core (fake HAL) and the emulator test
```

The GPIO numbers come from the hardware netlist script. Change the board
there, and the pin header follows.

## Tests

```sh
make -C firmware/test run                                   # core: 22 tests, sanitizers on
firmware/test/emu/run.sh firmware/build/pico-serial-hood.bin   # whole firmware in an emulator
```

- **Host tests:** the core's logic against a fake board. The tests were
  checked by breaking the core on purpose: removing DCE detection, the
  buffer hysteresis, the DSR mapping, the one-shot error events. Each break
  is caught.
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
  - getting through a settings save without a hang or watchdog reset.

## Not yet verified

- **On real hardware:** nothing has run on one yet. The first board or
  bench rig is the real test.
- **Flash write:** the emulator doesn't model flash programming, so a
  saved mode surviving a power cycle is unchecked.
- **UART pin routing:** the emulator doesn't model the UART-to-GPIO
  routing either, so the swap between chip A's and chip B's UART pins is
  unchecked.
- **Real host drivers:** Windows' usbser, macOS and Linux cdc_acm, and
  Android's usb-serial-for-android haven't been tried.
- **Not built yet:**
  - the Bluetooth (W) board's BLE/SPP links, SPEC-W.md §4–5;
  - Microsoft OS descriptors, so Windows binds WinUSB to the control
    interface without Zadig.
