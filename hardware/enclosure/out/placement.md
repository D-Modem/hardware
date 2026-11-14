<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 D-Modem Authors -->
# pico-serial W: placement fixed by the hood

The script `w_hood.py` makes this file. All coordinates are in mm. The point x = 0 is at the front face of the DB25 flange, and +x is toward the USB-C end. The point y = 0 is on the centre line of the hood, and +y is toward the USB-C side. The board top is the side that faces the top half of the hood.

| Item | Position | Notes |
|---|---|---|
| Board outline | `board-outline.dxf` | front edge x = 1.8, main board to x = 44.0, USB tongue to x = 57.1; width ±25.3 (±18.0 for x < 9.16) |
| DB25 cup pads | x 1.8–5.4, rows on top and bottom | 13-pin row on top |
| Screw holes, Ø2.3, no copper within Ø5.4 | (15.5, -22.5), (15.5, 22.5), (40.0, 18.5) | M2 × 10 self-tapping, from below |
| RM2 (top) | x 27.5–44.0, y -15.90–-1.40 | antenna end flush with the board edge at x = 44.0 |
| RF keep-out | x 39.0–57.5, y -26.4–9.10 (`rf-keepout.dxf`) | no copper on any layer, no parts |
| USB-C (top) | mouth at x = 57.8, centred y = 16.0 | HRO TYPE-C-31-M-12; overhangs the tongue edge by 0.7 |
| LEDs, 0603 (top) | BT (21.0, 23.0), TR (25.0, 23.0), CD (29.0, 23.0), RD (33.0, 23.0), SD (37.0, 23.0) | under light tubes; nothing else taller than 1.2 mm within 1.5 mm |
| Pairing button (top) | (24.0, -20.0) | TS-1088, 2.0 mm tall; push it through a pinhole |
| Height limits | top ≤ 4.8 mm, bottom ≤ 4.8 mm | inside the hood; USB-C is 3.26 |
| Keep clear | x < 9.16: board narrows to ±18.0 for the thumbscrew pockets | |

The thumbscrews (Keystone 2400, 4-40 × 5/16″) engage the jack posts of the computer by 4.5 mm.
