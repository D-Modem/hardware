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
