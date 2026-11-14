<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 D-Modem Authors -->
# pico-serial Line: placement fixed by the stick

The script `stick.py` makes this file. All coordinates are in mm. The point x = 0 is at the outer face of the RJ11 end, and +x is toward the USB-C end. The point y = 0 is on the centre line. The board top is the side that faces the top half.

| Item | Position | Notes |
|---|---|---|
| Board outline | `board-outline.dxf` | x 1.8–80.2, y ±10.3 |
| RJ11 (top) | opening at x = 0.0, rear contact row at x = 17.42, centred y = 0 | Ckmtw R-RJ11R06P-A800, opening flush with the end face |
| USB-C (top) | mouth at x = 80.9, centred y = 0 | overhangs the board edge by 0.7; 1.1 inside the end face, in a 13.2 × 7.6 opening |
| Screw holes, Ø2.3, no copper within Ø5.4 | (36.0, -7.4), (68.0, 7.4) | M2 × 8 self-tapping, from below |
| LEDs, 0603 (top) | OH (52.0, 6.4), RI (52.0, -6.4) | under light tubes |
| Isolation gap | across x 31.0–35.0 | line side toward the RJ11, USB side beyond |
| Height limits | top ≤ 13.0 mm, bottom ≤ 1.9 mm (through-hole pins trimmed) | |
