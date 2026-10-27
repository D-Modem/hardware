<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
<!-- Copyright (C) 2026 Neo-Desktop -->
# pico-serial W hood

A 3D-printed DB25 hood for the wireless board (see `../../SPEC-W.md` §2.2).

```sh
pip install cadquery          # once
python3 hardware/enclosure/w_hood.py
```

It writes `out/` and refuses to if anything collides or any metal part
enters the RM2 antenna keep-out.

| File | Use |
|---|---|
| `out/hood-top.stl`, `out/hood-bottom.stl` | Send to the print service (MJF PA12 recommended) |
| `out/hood-top.step`, `out/hood-bottom.step` | The same parts as STEP, for services or CAD that prefer it |
| `out/assembly.step` | Hood, board, DB25, RM2, USB-C, screws together, for checking fit in any CAD viewer |
| `out/board-outline.dxf` | PCB outline and screw holes: import into KiCad as Edge.Cuts |
| `out/rf-keepout.dxf` | Antenna keep-out: no copper on any layer, no parts |
| `out/placement.md` | Positions the layout must use: RM2, USB-C, LEDs, button, holes, height limits |
| `out/*.svg` | Views for a quick look |

All dimensions are parameters at the top of `w_hood.py`. Change a
parameter and re-run; the checks run again.

**Hardware per hood:**
- 2 × Keystone 2400 thumbscrews (4-40 × 5/16″, DigiKey 36-2400-ND);
- 3 × M2 × 10 pan-head self-tapping screws.

**Not yet printed.** Order one pair first and check:
- the flange fit;
- that the thumbscrews reach a real computer's jack posts;
- that your USB-C cable's plug seats fully.

# pico-serial Line stick

The phone-line stick's shell (`../../SPEC-L.md` §7): about the length of a
disposable lighter, like Apple's USB Modem, with the RJ11 at one end and
the USB-C at the other.

```sh
python3 hardware/enclosure/stick.py
```

It writes `out-line/` (the two halves as STL and STEP, an assembly, the
board outline, `placement.md`) and refuses to if anything collides.

- **Size:** 82 × 24 × 19.9 mm. The 13 mm-tall RJ11 sets the thickness;
  a lighter is about 15.
- **RJ11:** its opening is flush with the end face; the socket's front
  fills the end wall's cut-out.
- **USB-C:** the receptacle sits 1.1 mm inside a 13.2 × 7.6 mm opening,
  so a plug's overmould seats fully (as the W hood).
- **Fixing:** one M2 × 8 self-tapping screw from below, through the
  board, on the USB side of the isolation gap. The end walls hold the
  board's ends.
- **LEDs:** OH (off hook) and RI (ringing), under light tubes, labelled.
- **Bottom face:** the Source Location and `CERN-OHL-S-2.0`.

Print in MJF PA12 as the hood. **Not yet printed.**

## pico-serial WB stick

The wideband stick (`../../SPEC-WB.md`) uses the same shell:

```sh
python3 hardware/enclosure/stick.py --variant wb
```

It writes `out-wb/`. The differences: the lid reads "pico-serial WB", the
LEDs are LINK and DATA, and the collision check includes the 8.6 mm-tall
line transformer, which the shell's 13 mm headroom clears. **Not yet
printed.**
