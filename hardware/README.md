# pico-serial hardware

`netlist/build_netlist.py` defines every part and every connection on the
board. The netlist, the readable net list, the JLC parts list and the
schematic are all generated from it, so change the script, not the
outputs.

Everything targets **KiCad 9**: KiCad 9 file formats and the KiCad 9.0
symbol and footprint libraries. KiCad 10 opens it too.

| File | What it is |
|---|---|
| `netlist/build_netlist.py` | The circuit: parts, nets, the checks |
| `netlist/pico-serial.net` | KiCad netlist (generated) |
| `netlist/NETLIST.md` | Every net and every part, readable (generated) |
| `netlist/bom-jlc.csv` | JLC assembly parts list with LCSC numbers (generated) |
| `schematic/build_schematic.py` | Draws the schematic from the netlist script |
| `schematic/pico-serial.kicad_sch` | KiCad schematic, one A2 sheet (generated) |
| `schematic/pico-serial.kicad_sym` | SP3238E and MAX3243E symbols, which KiCad doesn't ship (generated) |
| `schematic/preview.svg` | Rough drawing of the sheet for a quick look (generated) |

## Regenerate

```sh
python3 hardware/netlist/build_netlist.py       # checks + netlist, NETLIST.md, BOM
python3 hardware/schematic/build_schematic.py   # checks + schematic and preview
```

Neither needs KiCad or a network connection. The schematic script uses
symbols cached in `schematic/lib/symbol-cache.sexp`.

To check every pin against KiCad's own libraries, and to refresh the
symbol cache, point the scripts at the KiCad 9.0 library checkouts:

```sh
git clone --depth 1 --branch 9.0.6 https://gitlab.com/kicad/libraries/kicad-symbols.git
git clone --depth 1 --branch 9.0.6 https://gitlab.com/kicad/libraries/kicad-footprints.git
python3 hardware/netlist/build_netlist.py --kicad-sym kicad-symbols --kicad-fp kicad-footprints
python3 hardware/schematic/build_schematic.py --kicad-sym9 kicad-symbols
```

It stops with an error, and writes nothing, if:
- a pin is neither connected nor listed as deliberately open;
- a net has only one pin;
- a net has two outputs;
- an RS-232 line doesn't have exactly one driver and one receiver;
- a power input isn't on a supply net;
- with the KiCad checkouts: a pin number or name differs from KiCad's
  symbol, or a footprint lacks a pad for a pin.

## What's in it

The circuit follows `../SPEC.md` (draft 0.3): §4.2–4.8 for the parts and
§4.6 for the GPIO map.

- **Power and USB:**
  - USB-C (HRO TYPE-C-31-M-12) with its own 5.1 kΩ on each CC pin;
  - 0.5 A PTC fuse, 5 V TVS diode, USBLC6 ESD protection;
  - AP2112K 3.3 V regulator, VBUS ÷2 sense into GP29.
- **RP2040:**
  - Raspberry Pi's minimal design: per-pin decoupling, 12 MHz crystal
    with 15 pF caps and 1 kΩ on XOUT, 27 Ω USB series resistors;
  - W25Q128JV flash;
  - BOOTSEL and RESET buttons, 10 kΩ RUN pull-up.
- **RS-232:**
  - SP3238E (chip A, DCE) and MAX3243E (chip B, DTE) on the same 8 DB25
    pins, so every line has one driver and one receiver;
  - each chip has 220 nF + 1 µF charge-pump caps and 1 µF + 100 nF on VCC;
  - both chips are held off by 100 kΩ pull-downs until firmware chooses
    a role.
- **DB25:** solder-cup female (MH Connectors MHDB25SS). Pin 7 to GND, pin
  1 to GND through a 0 Ω link. All other unused pins stay open.
- **Optional:** six status LEDs on GP22–27, not fitted (DNP).
- **Test pads:** VBUS, 3V3, 1V1, GND, SWCLK, SWDIO, RUN, GP28, and the TX/RX
  lines of both chips.

The two RS-232 chips have no KiCad symbol. Their pin tables are copied
from the datasheets (MaxLinear SP3238E rev 1.0.1; TI MAX3243E SLLS657E,
Table 6-2) and marked as such in the script. Every other part's pins
match KiCad's library symbols.

## The schematic

`schematic/pico-serial.kicad_sch` is one A2 sheet in blocks:
- USB-C, protection and supply;
- RP2040 and its decoupling;
- crystal, flash and buttons;
- chip A (DCE) and chip B (DTE), each with its charge-pump capacitors;
- the DB25;
- the optional LEDs;
- test pads and the power flags ERC needs.

Every pin ends in a short wire with a net label or power symbol, so parts
connect by name, not by long wires. Each part carries its LCSC number, a
note on its purpose, and its footprint.

The schematic script refuses to write if:
- the connections read back from the drawing differ from the netlist in
  any way (a missing pin, an extra join, a pin touching the middle of a
  wire);
- any two texts, labels or symbols overlap.

It's generated, so hand edits are overwritten on the next run. Either
change `build_netlist.py` and regenerate, or stop generating and edit in
KiCad from then on.

## Using it in KiCad

1. Open `schematic/pico-serial.kicad_pro` in KiCad 9 or 10. The project
   table points at `pico-serial.kicad_sym`, which has the two RS-232
   symbols. Every symbol is also embedded in the schematic.
2. Run ERC.
3. Open the PCB editor and choose **Tools → Update PCB from Schematic**
   (or import `netlist/pico-serial.net`). Footprints come from KiCad's
   standard libraries.

Neither file has been opened in KiCad yet: KiCad couldn't be installed
where they were made. Both parse cleanly, and the schematic also passes
an independent parser (kiutils). Expect small fixes the first time
KiCad opens them.

## Before layout

- Lock the board outline from the sample connector and hood (SPEC §7, §9).
- Bench test the SP3238E's leakage while shut down (SPEC §8). If it fails,
  the DCE chip changes to the TI TRS3238E (TSSOP-28) and its footprint
  changes too.
