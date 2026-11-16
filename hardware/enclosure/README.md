<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 D-Modem Authors -->
# pico-serial W hood

This is a 3D-printed DB25 hood for the wireless board (see `../../SPEC-W.md` §2.2).

```sh
pip install cadquery          # once
python3 hardware/enclosure/w_hood.py
```

The script writes `out/`. If a part collides with a different part, or if a
metal part goes into the keep-out area of the RM2 antenna, the script stops
and does not write the files.

| File | Use |
|---|---|
| `out/hood-top.stl`, `out/hood-bottom.stl` | Send these files to the print service (we recommend MJF PA12) |
| `out/hood-top.step`, `out/hood-bottom.step` | The same parts as STEP files, for services or CAD tools that use STEP |
| `out/assembly.step` | The hood, board, DB25, RM2, USB-C and screws together. Use this file to examine the fit in a CAD viewer |
| `out/board-outline.dxf` | The PCB outline and the screw holes. Import this file into KiCad as Edge.Cuts |
| `out/rf-keepout.dxf` | The antenna keep-out area: no copper on any layer, no parts |
| `out/placement.md` | The positions that the layout must use: RM2, USB-C, LEDs, button, holes, height limits |
| `out/*.svg` | Views for a quick examination |

All dimensions are parameters at the top of `w_hood.py`. If you change a
parameter, run the script again. Then the script does the checks again.

**Hardware for each hood:**
- 2 × Keystone 2400 thumbscrews (4-40 × 5/16″, DigiKey 36-2400-ND).
- 3 × M2 × 10 pan-head self-tapping screws.

**Not printed at this time.** Order one pair first. Then examine these items:
- The fit of the flange.
- The thumbscrews must engage the jack posts of a real computer.
- The plug of your USB-C cable must go fully into the receptacle.

# pico-serial Line stick

This is the shell of the phone-line stick (`../../SPEC-L.md` §7). Its length
is about the length of a disposable lighter, as with the USB Modem of Apple.
The RJ11 is at one end and the USB-C is at the other end.

```sh
python3 hardware/enclosure/stick.py
```

The script writes `out-line/`: the two halves as STL and STEP, an assembly,
the board outline and `placement.md`. If a part collides with a different
part, the script stops and does not write the files.

- **Size:** 82 × 24 × 19.9 mm. The RJ11 is 13 mm tall, and it sets the
  thickness. The thickness of a lighter is about 15.
- **RJ11:** Its opening is flush with the end face. The front of the socket
  fills the cut-out in the end wall.
- **USB-C:** The receptacle is 1.1 mm inside a 13.2 × 7.6 mm opening. Thus
  the overmould of a plug can go fully in (the same as the W hood).
- **Attachment:** One M2 × 8 self-tapping screw goes in from below, through
  the board, on the USB side of the isolation gap. The end walls hold the
  ends of the board.
- **LEDs:** OH (off hook) and RI (ring signal), under light tubes, with
  labels.
- **Bottom face:** The Source Location and `CERN-OHL-S-2.0`.

Print in MJF PA12, the same as the hood. **Not printed at this time.**

## pico-serial WB stick

The wideband stick (`../../SPEC-WB.md`) uses the same shell:

```sh
python3 hardware/enclosure/stick.py --variant wb
```

The script writes `out-wb/`. These are the differences:

- The lid shows the text "pico-serial WB".
- The LEDs are LINK and DATA.
- The collision check includes the line transformer, which is 8.6 mm tall.
  The 13 mm headroom of the shell is sufficient for it.

**Not printed at this time.**
