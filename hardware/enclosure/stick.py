#!/usr/bin/env python3
# SPDX-License-Identifier: CERN-OHL-S-2.0
# Copyright (C) 2026 Neo-Desktop
"""pico-serial Line and WB: the 3D-printed stick (two halves), board outline and checks.

A stick the length of a disposable lighter, Apple USB Modem style: the
RJ11 socket at one end, the USB-C receptacle at the other (SPEC-L.md §7).

Coordinates (mm): x runs along the stick from the RJ11 end's outer face
(x = 0) to the USB-C end; y across (+y = the side the LEDs' labels read
from, looking down on the top); z up, with the PCB's mid-plane at z = 0.
The halves split at z = 0.

The same shell holds the wideband stick (SPEC-WB.md): `--variant wb` (or
PS_VARIANT=wb) relabels its LEDs, moves the isolation gap to the line
transformer and checks the transformer's height; output goes to ./out-wb/.

Writes into ./out-line/ (./out-wb/):
  stick-top.step / .stl, stick-bottom.step / .stl   the two printed parts
  assembly.step                                     shell + board + RJ11, USB-C, screws
  board-outline.dxf                                 PCB outline and screw holes
  placement.md                                      positions the layout must use
  *.svg                                             views for a quick look

Stops with an error (exit 1) if the halves overlap, or the shell touches
the board, the RJ11, the USB-C, the LEDs or the screws.

Only the dimensions are needed by hardware/kicad/build_pcb.py, which
imports this without CadQuery.
"""
import os, sys
try:
    import cadquery as cq
except ImportError:   # the dimensions below are still usable
    cq = None

VARIANT = os.environ.get("PS_VARIANT", "line")
if "--variant" in sys.argv[1:-1]:
    VARIANT = sys.argv[sys.argv.index("--variant") + 1]
WB = VARIANT == "wb"          # the wideband stick; anything else is the Line stick
NAME = "pico-serial WB" if WB else "pico-serial Line"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out-wb" if WB else "out-line")

# ---------------------------------------------------------------------------
# Parts the shell fits around (datasheet values)
# ---------------------------------------------------------------------------
PCB_T = 1.60
# Ckmtw R-RJ11R06P-A800 (LCSC C189750), drawing 55A-1X1-6P6C-13H: 12.2 wide,
# 13.0 tall, 17.75 long (14.55 + 3.2 over the contacts); its opening is the
# end of the stick
RJ_W, RJ_H, RJ_L = 12.2, 13.0, 17.75
RJ_PIN_BACK = 0.33          # rear contact row, in from the jack's back
RJ_PEG = 4.84               # pegs, in front of the rear contact row
# HRO TYPE-C-31-M-12 USB-C receptacle (top mount)
USB_W, USB_L, USB_H = 8.94, 7.35, 3.26
USB_OVERHANG = 0.7          # past the board's edge, as on the W board
PLUG_W, PLUG_H = 13.2, 7.6  # opening for a plug's overmould (as the W hood)
# 0603 LEDs
LED_L, LED_W, LED_H = 1.6, 0.8, 0.8
# M2 x 8 pan-head self-tapping screws, from below through the board
M2_HEAD_D, M2_HEAD_H, M2_LEN = 3.8, 1.3, 8.0
SCREW_HOLE, SCREW_CLEAR = 2.3, 5.4

# ---------------------------------------------------------------------------
# Shell
# ---------------------------------------------------------------------------
LEN = 82.0                  # a BIC classic lighter is 81
END_WALL = 1.6
SIDE = 1.4                  # side, top and bottom walls
CLR = 0.20                  # printing clearance (MJF PA12)
BOARD_Y = 10.3              # board half-width
Y_IN = BOARD_Y + 0.3        # cavity half-width
Y_OUT = Y_IN + SIDE         # 12.0: 24 mm wide
Z_TOP_IN = PCB_T / 2 + RJ_H + 0.3     # the RJ11 sets the height
Z_BOT_IN = -(PCB_T / 2 + 2.2)          # the through-hole pins underneath
Z_TOP, Z_BOT = Z_TOP_IN + SIDE, Z_BOT_IN - SIDE
CORNER = 2.0

# board: between the end walls
BOARD_X0 = END_WALL + 0.2              # 1.8: the RJ11 end
BOARD_X1 = LEN - END_WALL - 0.2        # 80.2: the USB-C end
# the RJ11's opening flush with the end face; the USB-C 1.1 mm inside its opening
RJ_X0, RJ_X1 = 0.0, RJ_L
RJ_PINS_X = RJ_X1 - RJ_PIN_BACK        # rear contact row
USB_X1 = BOARD_X1 + USB_OVERHANG       # the receptacle's mouth
USB_X0 = USB_X1 - USB_L
USB_ZC = PCB_T / 2 + USB_H / 2

# fixed positions on the board (top side unless said)
# one M2 screw, on the centre line between the Si3056 and the RP2040 (the USB side
# of the isolation gap: nothing a finger can reach is near the line). The end
# walls hold the board's ends: the RJ11 and the USB-C sit in their openings.
SCREWS = [(46.8, 0.0)]
BARRIER_X = (32.25, 34.75)             # the DAA's isolation gap runs across here (SPEC-L §3.3)
LEDS = [("OH", 47.3, 8.6), ("RI", 47.3, -8.6)]
# WB: Pulse PE-65848NL transformer across the gap (body .475 x .460 x .340 in), its
# rows of pins along y; the gap runs under its middle
XFMR_X, XFMR_W, XFMR_L, XFMR_H = 26.8, 12.07, 11.68, 8.64
if WB:
    BARRIER_X = (XFMR_X - 2.0, XFMR_X + 2.0)
    # the screw to one side and the LEDs forward 3 mm: the codec's connections to the
    # RP2040 run down the middle, where the Line stick has its screw
    SCREWS = [(48.8, -6.4)]
    LEDS = [("LINK", 44.3, 8.6), ("DATA", 44.3, -8.6)]
LED_TUBE_Z0 = PCB_T / 2 + LED_H + 0.4
BOSS_R = 2.4


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def cyl_z(x, y, z0, z1, d):
    return cq.Workplane("XY").workplane(offset=z0).center(x, y).circle(d / 2).extrude(z1 - z0)


def board_outline():
    """The board: a rectangle between the end walls, its corners cut for the shell's fillets."""
    c = 0.6
    return [(BOARD_X0 + c, -BOARD_Y), (BOARD_X1 - c, -BOARD_Y), (BOARD_X1, -BOARD_Y + c), (BOARD_X1, BOARD_Y - c),
            (BOARD_X1 - c, BOARD_Y), (BOARD_X0 + c, BOARD_Y), (BOARD_X0, BOARD_Y - c), (BOARD_X0, -BOARD_Y + c)]


def shell():
    body = (cq.Workplane("XY").box(LEN, 2 * Y_OUT, Z_TOP - Z_BOT, centered=False)
            .translate((0, -Y_OUT, Z_BOT)).edges("|X").fillet(CORNER))
    body = body.cut(box(END_WALL, LEN - END_WALL, -Y_IN, Y_IN, Z_BOT_IN, Z_TOP_IN))
    # the RJ11's opening: its whole front, flush with the end face
    body = body.cut(box(-1, END_WALL + 0.01, -(RJ_W / 2 + CLR), RJ_W / 2 + CLR,
                        PCB_T / 2 - 0.01, PCB_T / 2 + RJ_H + CLR))
    # the USB-C plug's opening
    body = body.cut(box(LEN - END_WALL - 0.01, LEN + 1, -PLUG_W / 2, PLUG_W / 2,
                        USB_ZC - PLUG_H / 2, USB_ZC + PLUG_H / 2))
    # ledges under the board's long edges, and over them, so it sits between the halves
    for s in (-1, 1):
        y0, y1 = (BOARD_Y - 0.8, Y_IN + 0.01) if s > 0 else (-Y_IN - 0.01, -BOARD_Y + 0.8)
        body = body.union(box(END_WALL, LEN - END_WALL, y0, y1, Z_BOT_IN - 0.01, -PCB_T / 2))
    # screw bosses: clearance and counterbore below, a pilot hole above
    for x, y in SCREWS:
        body = body.union(cyl_z(x, y, Z_BOT_IN - 0.01, -PCB_T / 2, 2 * BOSS_R))
        body = body.union(cyl_z(x, y, PCB_T / 2, Z_TOP_IN + 0.01, 2 * BOSS_R))
        body = body.cut(cyl_z(x, y, Z_BOT - 1, Z_BOT + 1.6, M2_HEAD_D + 0.6))
        body = body.cut(cyl_z(x, y, Z_BOT, -PCB_T / 2 + 0.01, 2.4))
        body = body.cut(cyl_z(x, y, PCB_T / 2 - 0.01, PCB_T / 2 + 6.0, 1.6))
    # LED windows with light tubes
    for _, x, y in LEDS:
        body = body.union(cyl_z(x, y, LED_TUBE_Z0, Z_TOP_IN + 0.01, 3.0))
        body = body.cut(cyl_z(x, y, LED_TUBE_Z0 - 0.01, Z_TOP + 1, 1.5))
    return body


def split(body):
    top = body.intersect(box(-5, LEN + 5, -Y_OUT - 5, Y_OUT + 5, 0, Z_TOP + 5))
    bot = body.intersect(box(-5, LEN + 5, -Y_OUT - 5, Y_OUT + 5, Z_BOT - 5, 0))
    return label(top), label_bottom(bot)


SOURCE_LOCATION = "github.com/D-Modem/hardware"


def label_bottom(bot):
    """CERN-OHL-S-2.0 section 4: the product shows its Source Location."""
    try:
        wp = cq.Workplane(cq.Plane(origin=(0, 0, Z_BOT), xDir=(1, 0, 0), normal=(0, 0, -1)))
        bot = bot.cut(wp.center(LEN / 2, -2.2).text(SOURCE_LOCATION, 2.4, -0.4, kind="bold", halign="center", valign="center"))
        bot = bot.cut(wp.center(LEN / 2, 2.2).text("CERN-OHL-S-2.0", 2.4, -0.4, kind="bold", halign="center", valign="center"))
    except Exception as e:  # noqa: BLE001
        print("note: bottom labels skipped:", e)
    return bot


def label(top):
    """Debossed text on the top face; skipped if no font is available."""
    try:
        wp = cq.Workplane("XY").workplane(offset=Z_TOP)
        for name, x, y in LEDS:
            top = top.cut(wp.center(x + 4.2, y).text(name, 2.4, -0.4, kind="bold", halign="center", valign="center"))
        top = top.cut(wp.center(24.0, 0).text(NAME, 3.4, -0.4, kind="bold", halign="center", valign="center"))
        top = top.cut(wp.center(74.0, 0).text("USB-C", 2.4, -0.4, kind="bold", halign="center", valign="center"))
    except Exception as e:  # noqa: BLE001
        print("note: labels skipped:", e)
    return top


def pcb():
    w = cq.Workplane("XY").workplane(offset=-PCB_T / 2).polyline(board_outline()).close().extrude(PCB_T)
    for x, y in SCREWS:
        w = w.cut(cyl_z(x, y, -2, 2, SCREW_HOLE))
    return w


def parts():
    p = {}
    p["rj11"] = box(RJ_X0, RJ_X1, -RJ_W / 2, RJ_W / 2, PCB_T / 2, PCB_T / 2 + RJ_H)
    p["usb"] = box(USB_X0, USB_X1, -USB_W / 2, USB_W / 2, PCB_T / 2, PCB_T / 2 + USB_H)
    for name, x, y in LEDS:
        p[f"led {name}"] = box(x - LED_L / 2, x + LED_L / 2, y - LED_W / 2, y + LED_W / 2, PCB_T / 2, PCB_T / 2 + LED_H)
    if WB:
        p["transformer"] = box(XFMR_X - XFMR_L / 2, XFMR_X + XFMR_L / 2, -XFMR_W / 2, XFMR_W / 2,
                               PCB_T / 2, PCB_T / 2 + XFMR_H)
    for i, (x, y) in enumerate(SCREWS):
        head_z = Z_BOT + 1.6
        p[f"M2 screw {i + 1}"] = (cyl_z(x, y, head_z - M2_HEAD_H, head_z, M2_HEAD_D)
                                  .union(cyl_z(x, y, head_z, head_z + M2_LEN, 1.6)))
    return p


def vol(a, b):
    try:
        return a.intersect(b).val().Volume()
    except Exception:  # noqa: BLE001  (empty result)
        return 0.0


def check(top, bot, pcbw, p):
    errs = []
    v = vol(top, bot)
    if v > 1e-3:
        errs.append(f"halves overlap by {v:.3f} mm^3")
    for name, solid in dict(p, pcb=pcbw).items():
        for hname, h in (("top", top), ("bottom", bot)):
            v = vol(h, solid)
            if v > 1e-3:
                errs.append(f"{hname} half hits {name}: {v:.3f} mm^3")
    tip = M2_LEN + Z_BOT + 1.6
    if tip > Z_TOP - 0.6:
        errs.append(f"screws reach z = {tip:.1f}, through the top")
    return errs


def export_2d(path, wires):
    import ezdxf  # noqa: F401  (cadquery's DXF writer uses it)
    w = cq.Workplane("XY")
    for wp in wires:
        w = w.add(wp.vals())
    cq.exporters.export(w, path, "DXF")


def main():
    if cq is None:
        sys.exit("needs CadQuery: pip install cadquery")
    os.makedirs(OUT, exist_ok=True)
    top, bot = split(shell())
    pcbw = pcb()
    p = parts()
    errs = check(top, bot, pcbw, p)
    for e in errs:
        print("ERROR:", e)
    if errs:
        sys.exit(1)
    for name, w in (("stick-top", top), ("stick-bottom", bot)):
        cq.exporters.export(w, os.path.join(OUT, f"{name}.step"))
        cq.exporters.export(w, os.path.join(OUT, f"{name}.stl"), tolerance=0.02, angularTolerance=0.1)
    asm = cq.Assembly()
    asm.add(bot, name="stick-bottom", color=cq.Color(0.35, 0.35, 0.38, 1))
    asm.add(top, name="stick-top", color=cq.Color(0.45, 0.45, 0.48, 0.5))
    asm.add(pcbw, name="pcb", color=cq.Color(0.1, 0.45, 0.2, 1))
    for name, s in p.items():
        asm.add(s, name=name.replace(" ", "_"), color=cq.Color(0.8, 0.6, 0.2, 1))
    asm.save(os.path.join(OUT, "assembly.step"))
    outline = cq.Workplane("XY").polyline(board_outline()).close()
    holes = [cq.Workplane("XY").center(x, y).circle(SCREW_HOLE / 2) for x, y in SCREWS]
    export_2d(os.path.join(OUT, "board-outline.dxf"), [outline] + holes)
    view = dict(width=900, height=400, marginLeft=20, marginTop=20, showAxes=False, strokeWidth=0.4, showHidden=False)
    cq.exporters.export(top.union(bot), os.path.join(OUT, "stick.svg"), opt=dict(view, projectionDir=(-0.9, -1.2, 0.8)))
    inside = bot.union(pcbw)
    for s in p.values():
        inside = inside.union(s)
    cq.exporters.export(inside, os.path.join(OUT, "inside.svg"), opt=dict(view, projectionDir=(-0.9, -1.2, 1.4)))
    cq.exporters.export(top, os.path.join(OUT, "top-face.svg"), opt=dict(view, projectionDir=(0, 0, 1)))
    write_placement()
    bb = top.union(bot).val().BoundingBox()
    print(f"ok: stick {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm; board {BOARD_X1 - BOARD_X0:.1f} x "
          f"{2 * BOARD_Y:.1f} mm; no collisions")


def write_placement():
    L = ["<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->", "<!-- Copyright (C) 2026 Neo-Desktop -->",
         f"# {NAME}: placement fixed by the stick", "",
         "Generated by `stick.py`. Coordinates in mm; x = 0 at the RJ11 end's outer face, +x toward the "
         "USB-C end; y = 0 on the centre line. Board top = the side facing the top half.", "",
         "| Item | Position | Notes |", "|---|---|---|",
         f"| Board outline | `board-outline.dxf` | x {BOARD_X0}–{BOARD_X1}, y ±{BOARD_Y} |",
         f"| RJ11 (top) | opening at x = {RJ_X0}, rear contact row at x = {RJ_PINS_X:.2f}, centred y = 0 | "
         f"Ckmtw R-RJ11R06P-A800, opening flush with the end face |",
         f"| USB-C (top) | mouth at x = {USB_X1:.1f}, centred y = 0 | overhangs the board edge by {USB_OVERHANG}; "
         f"{LEN - USB_X1:.1f} inside the end face, in a {PLUG_W} × {PLUG_H} opening |",
         f"| Screw holes, Ø{SCREW_HOLE}, no copper within Ø{SCREW_CLEAR} | "
         + ", ".join(f"({x}, {y})" for x, y in SCREWS) + " | M2 × 8 self-tapping, from below |",
         "| LEDs, 0603 (top) | " + ", ".join(f"{n} ({x}, {y})" for n, x, y in LEDS) + " | under light tubes |",
         f"| Isolation gap | across x {BARRIER_X[0]}–{BARRIER_X[1]} | line side toward the RJ11, USB side beyond |",
         f"| Height limits | top ≤ {Z_TOP_IN - PCB_T / 2 - 0.3:.1f} mm, bottom ≤ {-Z_BOT_IN - PCB_T / 2 - 0.3:.1f} mm "
         f"(through-hole pins trimmed) | |"]
    open(os.path.join(OUT, "placement.md"), "w").write("\n".join(L) + "\n")


if __name__ == "__main__":
    main()
