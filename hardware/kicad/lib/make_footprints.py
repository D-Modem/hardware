#!/usr/bin/env python3
# SPDX-License-Identifier: CERN-OHL-S-2.0
# Copyright (C) 2026 D-Modem Authors
"""Writes the footprints KiCad doesn't ship into pico-serial.pretty/:

RaspberryPi_RM2: from the RM2 datasheet (Raspberry Pi Radio Module 2,
Figure 6 "PCB footprint").
MountingHole_M2: the W hood's screw holes.
RJ11_Ckmtw_R-RJ11R06P-A800: the phone-line stick's jack (LCSC C189750),
from its drawing 55A-1X1-6P6C-13H.
Fuse_Littelfuse_0461: TeleLink surface-mount telecom fuse (04611.25ER),
from Littelfuse's recommended pad layout.
Pulse_PE-65848: the WB stick's line transformer (PE-65848NL), from Pulse's
drawing (rev M12) "suggested land pattern".

Origin: module centre. Antenna end toward -y (pins 1 and 21 nearest it).
  - 7 pads down each side, 1.5 mm pitch; lowest pad centres 1.8 mm above the
    module's bottom edge; pads 2.0 x 1.0, reaching 0.5 mm outside the module;
  - 7 pads along the bottom, 1.5 mm pitch, first centre 2.75 mm from the
    left edge; pads 1.0 x 2.0, reaching 0.5 mm below the module;
  - numbered counter-clockwise from the top left (pin 1).
The 35.5 x 18.5 mm antenna keep-out lies mostly off the module; the board
script draws it (it depends on where the module sits).
"""
import os

W, L = 14.5, 16.5
X0, Y0 = -W / 2, -L / 2          # top-left corner of the module
pads = []
for k in range(7):               # left side, pins 1..7, top to bottom
    pads.append((str(1 + k), X0 + 0.5, L / 2 - 1.8 - 1.5 * (6 - k), 2.0, 1.0))
for k in range(7):               # bottom, pins 8..14, left to right
    pads.append((str(8 + k), X0 + 2.75 + 1.5 * k, L / 2 - 0.5, 1.0, 2.0))
for k in range(7):               # right side, pins 15..21, bottom to top
    pads.append((str(15 + k), -X0 - 0.5, L / 2 - 1.8 - 1.5 * k, 2.0, 1.0))


def line(x1, y1, x2, y2, layer, w):
    return (f'\t(fp_line (start {x1:g} {y1:g}) (end {x2:g} {y2:g}) '
            f'(stroke (width {w:g}) (type solid)) (layer "{layer}"))')


def rect(x1, y1, x2, y2, layer, w):
    return "\n".join(line(*a, layer, w) for a in ((x1, y1, x2, y1), (x2, y1, x2, y2), (x2, y2, x1, y2), (x1, y2, x1, y1)))


out = ['(footprint "RaspberryPi_RM2"', '\t(version 20241229)', '\t(generator "pico-serial")',
       '\t(generator_version "1")', '\t(layer "F.Cu")',
       '\t(descr "Raspberry Pi Radio Module 2 (RMC20452T), 21 castellated pads, 1.5 mm pitch; '
       'datasheet Figure 6. Antenna end toward -y: keep all metal 13.5 mm beyond it and 10.5 mm either side")',
       '\t(tags "RM2 CYW43439 Bluetooth WiFi module castellated")',
       '\t(attr smd)',
       f'\t(property "Reference" "REF**" (at 0 {-L / 2 - 1.2:g} 0) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))',
       f'\t(property "Value" "RM2" (at 0 {L / 2 + 2.2:g} 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))',
       '\t(property "Datasheet" "https://datasheets.raspberrypi.com/rm2/rm2-datasheet.pdf" (at 0 0 0) (layer "F.Fab") (hide yes) (effects (font (size 1 1))))']
out.append(rect(X0, Y0, -X0, -Y0, "F.Fab", 0.1))
out.append(line(X0, Y0 + 5.0, -X0, Y0 + 5.0, "F.Fab", 0.1))         # antenna region boundary
out.append(f'\t(fp_text user "ANTENNA" (at 0 {Y0 + 2.5:g} 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))')
c = 0.25
out.append(rect(X0 - 0.5 - c, Y0 - c, -X0 + 0.5 + c, -Y0 + 0.5 + c, "F.CrtYd", 0.05))
# silkscreen: the module's sides above the pads (the antenna end is usually
# flush with the board edge, so no line across it), plus a pin-1 mark
s = 0.12 / 2 + 0.2
top_pad = pads[0][2] - 0.5
out.append(line(X0 - s, Y0 + 0.5, X0 - s, top_pad - 0.3, "F.SilkS", 0.12))
out.append(line(-X0 + s, Y0 + 0.5, -X0 + s, top_pad - 0.3, "F.SilkS", 0.12))
out.append(f'\t(fp_circle (center {X0 - 1.2:g} {pads[0][2]:g}) (end {X0 - 0.95:g} {pads[0][2]:g}) '
           '(stroke (width 0.12) (type solid)) (fill solid) (layer "F.SilkS"))')
for num, x, y, w, h in pads:
    out.append(f'\t(pad "{num}" smd rect (at {x:g} {y:g}) (size {w:g} {h:g}) (layers "F.Cu" "F.Paste" "F.Mask"))')
out.append(')')
here = os.path.dirname(os.path.abspath(__file__))
open(os.path.join(here, "pico-serial.pretty", "RaspberryPi_RM2.kicad_mod"), "w").write("\n".join(out) + "\n")

# MountingHole_M2: a plain 2.3 mm hole for the W hood's M2 self-tapping
# screws. Board-only (no schematic symbol). The courtyard is the 5.4 mm
# circle kept free of copper and parts for the screw head and the hood's bosses.
HOLE, CLEAR = 2.3, 5.4
mh = ['(footprint "MountingHole_M2"', '\t(version 20241229)', '\t(generator "pico-serial")',
      '\t(generator_version "1")', '\t(layer "F.Cu")',
      '\t(descr "M2 screw hole, unplated, 2.3 mm; 5.4 mm kept clear (pico-serial W hood)")',
      '\t(tags "mounting hole M2")',
      '\t(attr board_only exclude_from_pos_files exclude_from_bom)',
      '\t(property "Reference" "H**" (at 0 -3.4 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))',
      '\t(property "Value" "M2 screw" (at 0 3.4 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))',
      f'\t(fp_circle (center 0 0) (end {CLEAR / 2:g} 0) (stroke (width 0.05) (type solid)) (fill no) (layer "F.CrtYd"))',
      f'\t(fp_circle (center 0 0) (end {CLEAR / 2:g} 0) (stroke (width 0.05) (type solid)) (fill no) (layer "B.CrtYd"))',
      f'\t(fp_circle (center 0 0) (end {HOLE / 2:g} 0) (stroke (width 0.1) (type solid)) (fill no) (layer "F.Fab"))',
      f'\t(pad "" np_thru_hole circle (at 0 0) (size {HOLE:g} {HOLE:g}) (drill {HOLE:g}) (layers "*.Cu" "*.Mask"))',
      ')']
open(os.path.join(here, "pico-serial.pretty", "MountingHole_M2.kicad_mod"), "w").write("\n".join(mh) + "\n")


def fp_header(name, descr, tags, attr, ref_y, val_y):
    return [f'(footprint "{name}"', '\t(version 20241229)', '\t(generator "pico-serial")',
            '\t(generator_version "1")', '\t(layer "F.Cu")', f'\t(descr "{descr}")', f'\t(tags "{tags}")',
            f'\t(attr {attr})',
            f'\t(property "Reference" "REF**" (at 0 {ref_y:g} 0) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))',
            f'\t(property "Value" "{name}" (at 0 {val_y:g} 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))']


def save(name, lines):
    open(os.path.join(here, "pico-serial.pretty", f"{name}.kicad_mod"), "w").write("\n".join(lines + [")"]) + "\n")


# RJ11, right angle, through hole. Origin: the rear contact row's middle.
# Contacts at 1.02 mm pitch along y, alternating between the rear row
# (x = 0: pins 1, 3, 5) and the front row (x = 2.54: 2, 4, 6); two plastic
# pegs 12.0 apart at x = 4.84; the opening faces +x. Body 12.2 wide, 13.0
# tall: 3.2 behind the pegs' block over the contacts, 14.55 from there to
# the opening (the body edges are read off the drawing: check a sample).
REAR, BODY_L, BODY_W = -0.33, 3.2 + 14.55, 12.2
rj = fp_header("RJ11_Ckmtw_R-RJ11R06P-A800",
               "RJ11/RJ12 6P6C jack, right angle, through hole, Ckmtw R-RJ11R06P-A800 (LCSC C189750); opening toward +x",
               "RJ11 RJ12 6P6C modular jack telephone", "through_hole", -BODY_W / 2 - 1.2, BODY_W / 2 + 1.4)
x0, x1, yh = REAR, REAR + BODY_L, BODY_W / 2
rj.append(rect(x0, -yh, x1, yh, "F.Fab", 0.1))
rj.append(rect(x0 - 0.25, -yh - 0.25, x1 + 0.25, yh + 0.25, "F.CrtYd", 0.05))
rj.append(line(x0 - 0.12, -yh - 0.12, x1 - 1.0, -yh - 0.12, "F.SilkS", 0.12))
rj.append(line(x0 - 0.12, yh + 0.12, x1 - 1.0, yh + 0.12, "F.SilkS", 0.12))
rj.append(line(x0 - 0.12, -yh - 0.12, x0 - 0.12, yh + 0.12, "F.SilkS", 0.12))
for n in range(1, 7):
    x, y = (0.0 if n % 2 else 2.54), (n - 3.5) * 1.02
    shape = "rect" if n == 1 else "circle"
    rj.append(f'\t(pad "{n}" thru_hole {shape} (at {x:g} {y:g}) (size 1.4 1.4) (drill 0.9) (layers "*.Cu" "*.Mask"))')
for y in (-6.0, 6.0):
    rj.append(f'\t(pad "" np_thru_hole circle (at 4.84 {y:g}) (size 2.3 2.3) (drill 2.3) (layers "*.Cu" "*.Mask"))')
save("RJ11_Ckmtw_R-RJ11R06P-A800", rj)

# Littelfuse TeleLink 0461 (2410-size body 10.10 x 3.12 x 1.70): pads 3.25 x
# 3.43, 6.10 apart, 12.6 overall.
fu = fp_header("Fuse_Littelfuse_0461", "Littelfuse TeleLink 0461 surface-mount telecom fuse, 10.1 x 3.1 mm",
               "fuse telecom TeleLink 0461", "smd", -3.0, 3.0)
fu.append(rect(-5.05, -1.56, 5.05, 1.56, "F.Fab", 0.1))
fu.append(rect(-6.55, -1.97, 6.55, 1.97, "F.CrtYd", 0.05))
fu.append(line(-2.8, -1.68, 2.8, -1.68, "F.SilkS", 0.12))
fu.append(line(-2.8, 1.68, 2.8, 1.68, "F.SilkS", 0.12))
for n, x in (("1", -4.675), ("2", 4.675)):
    fu.append(f'\t(pad "{n}" smd rect (at {x:g} 0) (size 3.25 3.43) (layers "F.Cu" "F.Paste" "F.Mask"))')
save("Fuse_Littelfuse_0461", fu)

# Pulse PE-65848NL, SMD-8 gull wing (drawing rev M12, inches): pads .050 x
# .120 on .100 pitch, rows .400 apart inside, .640 outside (centres 13.21 mm
# apart); body .475 x .460, .340 high. Pins 1-4 one row (the codec winding),
# 5-8 the other (the line winding): the rows face the two sides of the
# isolation barrier. Numbered counter-clockwise seen from the top.
IN = 25.4
PY = (0.400 + 0.640) / 4 * IN            # row centre from the middle: 6.604
xf = fp_header("Pulse_PE-65848", "Pulse PE-65848NL line transformer, SMD-8 gull wing, 12.1 x 11.7 x 8.6 mm, 1500 Vrms",
               "transformer line DSL Pulse PE-65848", "smd", -PY - 2.6, PY + 2.6)
bx, by = 0.475 * IN / 2, 0.460 * IN / 2
xf.append(rect(-bx, -by, bx, by, "F.Fab", 0.1))
xf.append(rect(-bx - 0.25, -PY - 1.525 - 0.25, bx + 0.25, PY + 1.525 + 0.25, "F.CrtYd", 0.05))
xf.append(line(-bx - 0.12, -by + 0.5, -bx - 0.12, by - 0.5, "F.SilkS", 0.12))
xf.append(line(bx + 0.12, -by + 0.5, bx + 0.12, by - 0.5, "F.SilkS", 0.12))
xf.append(f'\t(fp_circle (center {-bx - 0.9:g} {PY:g}) (end {-bx - 0.65:g} {PY:g}) '
          '(stroke (width 0.12) (type solid)) (fill solid) (layer "F.SilkS"))')
for k in range(4):
    x = (k - 1.5) * 0.100 * IN
    xf.append(f'\t(pad "{1 + k}" smd rect (at {x:g} {PY:g}) (size 1.27 3.048) (layers "F.Cu" "F.Paste" "F.Mask"))')
    xf.append(f'\t(pad "{8 - k}" smd rect (at {x:g} {-PY:g}) (size 1.27 3.048) (layers "F.Cu" "F.Paste" "F.Mask"))')
save("Pulse_PE-65848", xf)
