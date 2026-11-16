<img src=".github/banner.png" alt="D-Modem/hardware: Open source hardware modem designs" width="100%">

# pico-serial

Open RP2040 boards for the D-Modem suite: serial adapters and phone-line
USB sticks.

pico-serial connects a phone or computer to vintage serial ports and to
real telephone lines. The phone or computer runs [D-Modem](https://github.com/D-Modem/D-Modem)
or [the D-Modem app](https://github.com/D-Modem/app). Each board has a bare
RP2040 with its own flash, crystal and regulator. The boards do not use a
Pico module.

| Board | What it is | Spec |
| --- | --- | --- |
| pico-serial | USB-C to DB25 RS-232. One female DB25 operates as a modem (DCE) or as a computer port (DTE). The board finds the mode automatically | [SPEC.md](SPEC.md) |
| pico-serial W | The same adapter with Bluetooth: BLE for iPhone and iPad, and classic Bluetooth serial for Android | [SPEC-W.md](SPEC-W.md) |
| pico-serial Line | A USB modem stick for a real telephone line, with a Si3056 DAA. The modem is D-Modem on the host | [SPEC-L.md](SPEC-L.md) |
| pico-serial WB | A wideband stick for bare copper pairs. It samples at 96 kHz for the V.wb mode of D-Modem | [SPEC-WB.md](SPEC-WB.md) |

Status: the specs are drafts. CI builds the firmware and runs its tests
on the host and in an RP2040 emulator. A script makes the board files for
KiCad 9. At this time, nothing has run on a real board.

## Quick start

```sh
cmake -S firmware -B firmware/build -G Ninja   # -DPS_BOARD=hood, w, line, wb or pico
ninja -C firmware/build                        # -> firmware/build/pico-serial-hood.uf2
```

The script `hardware/netlist/build_netlist.py` makes the board files.

## Documentation

The documentation is also a website, [d-modem.github.io/hardware](https://d-modem.github.io/hardware/). The website has reference pages that a script makes from the code.

- [docs/](docs/README.md): an index of all specs and guides
- [Firmware](firmware/README.md): how to build and flash, Bluetooth, tests
- [Control protocol](firmware/PROTOCOL.md): the USB vendor interface for host apps
- [Hardware](hardware/README.md): netlist, schematic, PCB, enclosures

## The D-Modem suite

| Repository | What it is |
| --- | --- |
| [D-Modem](https://github.com/D-Modem/D-Modem) | The open V.92 softmodem: dmodemd, libdmodem and the Asterisk, FreeSWITCH and ARI modules |
| [app](https://github.com/D-Modem/app) | The D-Modem app for Android and iOS: a dial-up modem on a SIP call |
| [hardware](https://github.com/D-Modem/hardware) | pico-serial: RP2040 serial adapters and phone-line USB sticks |
| [menu](https://github.com/D-Modem/menu) | modemmenu: the login and the menu that dial-up callers get to |
| [bbs](https://github.com/D-Modem/bbs) | A bulletin board for telnet, SSH, TN3270 and modem callers |
| [go3270](https://github.com/D-Modem/go3270) | A Go library for 3270 terminal servers, used by bbs |
| [brand](https://github.com/D-Modem/brand) | Logos, colours, type and the naming guide |

ISDN S0 stick (an ISDN basic rate S/T port for D-Modem, TE or NT): see [SPEC-S0.md](SPEC-S0.md).

FPGA line board (a study: ADSL, ISDN U or S0 through gateware and host software): see [SPEC-FX.md](SPEC-FX.md).

## Licence

Firmware: AGPL-3.0-only, or a commercial licence from the D-Modem Authors. Hardware: CERN-OHL-S-2.0, or a commercial licence from the D-Modem Authors. See [LICENSING.md](LICENSING.md). Contributions come with a contributor agreement ([CONTRIBUTING.md](CONTRIBUTING.md)).
