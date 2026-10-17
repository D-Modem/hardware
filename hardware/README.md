# pico-serial hardware

`netlist/build_netlist.py` defines every part and every connection on the
board. The netlist, the readable net list and the JLC parts list are all
generated from it, so change the script, not the outputs.

| File | What it is |
|---|---|
| `netlist/build_netlist.py` | The circuit: parts, nets, the checks |
| `netlist/pico-serial.net` | KiCad netlist (generated) |
| `netlist/NETLIST.md` | Every net and every part, readable (generated) |
| `netlist/bom-jlc.csv` | JLC assembly parts list with LCSC numbers (generated) |

## Regenerate

```sh
cd hardware/netlist
python3 build_netlist.py                  # checks + writes the outputs
```

To also check every pin against KiCad's own libraries, point it at
checkouts of the official symbol and footprint libraries:

```sh
git clone --depth 1 https://gitlab.com/kicad/libraries/kicad-symbols.git
git clone --depth 1 https://gitlab.com/kicad/libraries/kicad-footprints.git
python3 build_netlist.py --kicad-sym kicad-symbols --kicad-fp kicad-footprints
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

## Using it in KiCad

1. Create a project and open the PCB editor.
2. **File → Import → Netlist…**, choose `pico-serial.net`, then **Update
   PCB**. The footprints come from KiCad's standard libraries.
3. LCSC numbers are in each footprint's `LCSC` field; the DNP field marks
   the LEDs.

The import hasn't been tried in KiCad yet: KiCad couldn't be installed
where this was made. The file follows KiCad's netlist format (version
"E") and parses cleanly, but expect to fix small issues on the first
import.

There is no schematic yet. This netlist goes straight to layout. A
generated KiCad schematic can come next if wanted.

## Before layout

- Lock the board outline from the sample connector and hood (SPEC §7, §9).
- Bench test the SP3238E's leakage while shut down (SPEC §8). If it fails,
  the DCE chip changes to the TI TRS3238E (TSSOP-28) and its footprint
  changes too.
