#!/usr/bin/env python3
# SPDX-License-Identifier: CERN-OHL-S-2.0
# Copyright (C) 2026 Neo-Desktop
"""Writes pico-serial.pretty/RaspberryPi_RM2.kicad_mod from the RM2 datasheet
(Raspberry Pi Radio Module 2 datasheet, Figure 6 "PCB footprint").

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
