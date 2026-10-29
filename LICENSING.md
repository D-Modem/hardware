# Licensing

pico-serial is copyright the D-Modem Authors.

## Firmware (`firmware/`)

The firmware is available under either of two licences:

- the **GNU Affero General Public License, version 3 only**
  (AGPL-3.0-only), in [firmware/COPYING](firmware/COPYING); or
- a **commercial licence** from the D-Modem Authors, for uses that cannot meet
  the AGPL's terms. Ask the D-Modem Authors (<https://github.com/D-Modem>)
  for its terms.

This is the same arrangement as the rest of D-Modem. Each source file names
its licence on an `SPDX-License-Identifier` line. This also covers:
- the generated `firmware/src/generated/board_pins.h`;
- the firmware CI workflow (`.github/workflows/firmware.yml`).

### Not ours, used by the firmware

- `firmware/pico_sdk_import.cmake` is Raspberry Pi's, copied unchanged
  from the Pico SDK. It is under the BSD-3-Clause licence in its header.
- **Fetched at build time**, not in the repository:
  - the **Raspberry Pi Pico SDK** (BSD-3-Clause), and **picotool**
    (BSD-3-Clause), which builds the UF2;
  - **TinyUSB** (MIT).

  A firmware binary links the SDK and TinyUSB. Their copyright notices
  and licences go with a binary that is shipped. Both combine with the
  firmware under the AGPL and under the commercial licence alike.
- **Fetched by the emulator test only:**
  - **rp2040js** (MIT);
  - the RP2040 boot ROM image from its repository (Raspberry Pi,
    BSD-3-Clause).

## Hardware (`hardware/`, `SPEC*.md`)

The hardware design is available under either of two licences:

- the **CERN Open Hardware Licence Version 2, Strongly Reciprocal**
  (CERN-OHL-S-2.0), in [hardware/LICENSE](hardware/LICENSE); or
- a **commercial licence** from the D-Modem Authors, for uses that cannot meet
  CERN-OHL-S's terms, such as shipping a product made from a modified
  design without publishing that design. Ask the D-Modem Authors for their terms.

### What it covers

- the netlist, schematic, PCB and enclosure scripts, and the hardware CI
  workflow (`.github/workflows/hardware.yml`);
- everything they generate:
  - the netlist and parts lists;
  - the KiCad schematic, symbols and PCB, its renders, Gerbers and
    placement file;
  - the hood's STEP/STL files, board outline and placement rules;
- the specifications (`SPEC.md`, `SPEC-W.md`).

Each source file names its licence on an `SPDX-License-Identifier` line.

### Source Location

The Source Location (CERN-OHL-S-2.0 section 1.9) is
<https://github.com/D-Modem/hardware>.

The repository must be **public**, and expected to stay reachable for at
least three years, before any Product made from the design is given to
anyone (section 1.9). It is private as of this writing.

**Notice under section 4:** a Product made from this design must visibly
and securely display that Source Location, and the licence name. This is
already done on the original design:
- on the PCB silkscreen;
- debossed in the hood's underside: `github.com/D-Modem/hardware`,
  `CERN-OHL-S-2.0`.

A modified design must point to its own Source Location instead, and
carry a modification notice (section 3.3).

### Not ours

`hardware/kicad/lib/symbol-cache.sexp`, the symbols embedded in the
schematic and the footprints embedded in the PCB come from KiCad's
official libraries. They are under
CC-BY-SA-4.0 with the KiCad libraries' exception, which lets designs use
them under any licence. The parts (RP2040, transceivers, connectors and
so on) are Available Components in CERN-OHL-S's terms, under their
makers' own terms.

## Contributions

Contributions can be accepted only under terms that let the D-Modem Authors offer
them under all the licences above. Ask before sending one.
