#!/usr/bin/env python3
# SPDX-License-Identifier: CERN-OHL-S-2.0
# Copyright (C) 2026 Neo-Desktop
"""pico-serial W: 3D-printed DB25 hood (two halves), board outline and checks.

Coordinates (mm): x runs from the DB25 flange's front face (x = 0) toward
the back (USB-C end); y across the width (+y = USB-C side); z up, with
the PCB's mid-plane at z = 0. The halves split at z = 0.

Writes into ./out/:
  hood-top.step / .stl, hood-bottom.step / .stl   the two printed parts
  assembly.step                                   hood + board + parts, for checking fit
  board-outline.dxf                               PCB outline and screw holes (KiCad Edge.Cuts)
  rf-keepout.dxf                                  RM2 antenna keep-out: no copper, no metal
  placement.md                                    fixed positions the layout must use
  *.svg                                           views for a quick look

Stops with an error (exit 1) if:
  - the two halves overlap each other;
  - the hood touches the PCB, DB25, RM2, USB-C, LEDs, button, thumbscrews or
    screws anywhere it shouldn't;
  - any metal part (DB25, USB-C, screws) enters the RF keep-out;
  - the thumbscrews engage the computer's posts by less than 4 mm.

See SPEC-W.md section 2.2.
"""
import os, sys
import cadquery as cq

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

# ---------------------------------------------------------------------------
# Parts the hood fits around (datasheet values)
# ---------------------------------------------------------------------------
# MH Connectors MHDB25SS (LCSC C19601171), solder-cup DB25 female
FL_W, FL_H, FL_T = 53.02, 12.55, 0.80      # flange
HOLE_PITCH, HOLE_D = 47.04, 3.10           # flange holes
D_W, D_H, D_LEN = 38.38, 7.90, 5.80        # front D-shell, sticks out in front of the flange
BODY_W, BODY_H, BODY_T = 41.20, 10.80, 0.80  # rear insulator behind the flange
CUP_END = 4.90                             # solder cups end this far behind the flange face
CUP_Y = 16.62 + 1.0                        # outermost cup edge (12 pitches of 2.77 / 2, + cup radius)
ROW_Z = 2.84 / 2                           # cup rows above/below the board mid-plane

PCB_T = 1.60
# Raspberry Pi RM2 radio module
RM2_W, RM2_L, RM2_H = 14.5, 16.5, 2.55
KEEP_W, KEEP_BEYOND, KEEP_OVER = 35.5, 13.5, 5.0   # keep-out: 35.5 wide, 13.5 past the antenna end, 5 over the module
# HRO TYPE-C-31-M-12 USB-C receptacle (top mount)
USB_W, USB_L, USB_H = 8.94, 7.35, 3.26
# Keystone 2400 thumbscrew: 4-40, 5/16" head dia, 5/16" thread
TS_D, TS_HEAD_D, TS_LEN, TS_HEAD_H = 2.84, 7.94, 7.94, 4.76
# M2 x 10 pan-head self-tapping screws
M2_HEAD_D, M2_HEAD_H, M2_LEN = 3.8, 1.3, 10.0

# ---------------------------------------------------------------------------
# Hood
# ---------------------------------------------------------------------------
WALL = 1.6
CLR = 0.20                                  # printing clearance (MJF PA12)
X_FRONT = -1.2                              # front lip, in front of the flange
Z_OUT = 7.5                                 # hood is 15 mm thick
Y_OUT = 28.0                                # hood is 56 mm wide (flange is 53)
Y_IN = Y_OUT - WALL                         # 26.4
Z_IN = Z_OUT - WALL                         # 5.9

SLOT_X1 = FL_T + 0.15                       # flange slot x = 0 .. 0.95
EAR_X1 = 3.40                               # thumbscrew head sits behind this
POCKET_X1 = EAR_X1 + TS_HEAD_H + 0.8        # 8.96: finger pocket for the knurled head
POCKET_Y0 = HOLE_PITCH / 2 - TS_HEAD_D / 2 - 0.35   # 19.2
POCKET_WALL = 1.0

# board
BOARD_X0 = FL_T + BODY_T + 0.2              # front edge butts the rear insulator: 1.8
BOARD_FRONT_Y = POCKET_Y0 - POCKET_WALL - 0.2   # narrow front strip, clear of the pocket walls: 18.0
BOARD_NARROW_X1 = POCKET_X1 + 0.2
BOARD_Y = Y_IN - 1.1                        # 25.3
BOARD_X1 = 44.0                             # rear edge of the main board = RM2 antenna end

# RM2 at the rear, antenna end flush with the board edge; keep-out spans to the left wall
KEEP_Y0 = -Y_IN
RM2_YC = KEEP_Y0 + KEEP_W / 2               # -8.65
KEEP_Y1 = KEEP_Y0 + KEEP_W                  # 9.1
KEEP_X0, KEEP_X1 = BOARD_X1 - KEEP_OVER, BOARD_X1 + KEEP_BEYOND   # 39 .. 57.5
X_IN = KEEP_X1 + 0.3                        # rear inner wall: 57.8
X_OUT = X_IN + WALL                         # 59.4

# USB-C tongue, outside the keep-out
TONGUE_Y0 = KEEP_Y1 + 1.4                   # 10.5
TONGUE_Y1 = TONGUE_Y0 + 11.0
USB_YC = (TONGUE_Y0 + TONGUE_Y1) / 2
USB_X1 = X_IN                               # receptacle mouth at the inner face of the back wall
USB_X0 = USB_X1 - USB_L
TONGUE_X1 = USB_X1 - 0.7                    # receptacle overhangs the board edge
USB_ZC = PCB_T / 2 + USB_H / 2
PLUG_W, PLUG_H = 13.2, 7.6                  # opening for the plug's overmould

SCREWS = [(15.5, -22.5), (15.5, 22.5), (40.0, 18.5)]     # through the board
PINS = [(55.0, -24.0), (55.0, 24.1)]                     # alignment pins, rear corners
LEDS = [("BT", 21.0), ("TR", 25.0), ("CD", 29.0), ("RD", 33.0), ("SD", 37.0)]
LED_Y = 23.0
PAIR = (24.0, -20.0)                                     # pairing button, top side

BOSS_R = 2.6
LED_TUBE_Z0 = PCB_T / 2 + 0.8 + 0.4         # 0603 LED is 0.8 tall
PAIR_TUBE_Z0 = PCB_T / 2 + 2.0 + 0.5        # TS-1088 is 2.0 tall


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def cyl_x(x0, x1, y, z, d):
    return cq.Workplane("YZ").workplane(offset=x0).center(y, z).circle(d / 2).extrude(x1 - x0)


def cyl_z(x, y, z0, z1, d):
    return cq.Workplane("XY").workplane(offset=z0).center(x, y).circle(d / 2).extrude(z1 - z0)


def hood():
    body = (cq.Workplane("XY").box(X_OUT - X_FRONT, 2 * Y_OUT, 2 * Z_OUT, centered=False)
            .translate((X_FRONT, -Y_OUT, -Z_OUT)).edges("|Z").fillet(2.5))
    # cavity: behind the flange (connector body), past the thumbscrew ears, then full width
    cav = (box(SLOT_X1, EAR_X1, -(BODY_W / 2 + 0.3), BODY_W / 2 + 0.3, -Z_IN, Z_IN)
           .union(box(EAR_X1 - 0.01, POCKET_X1, -(POCKET_Y0 - POCKET_WALL), POCKET_Y0 - POCKET_WALL, -Z_IN, Z_IN))
           .union(box(POCKET_X1 - 0.01, X_IN, -Y_IN, Y_IN, -Z_IN, Z_IN)))
    body = body.cut(cav)
    # flange slot and the front opening for the mating shell and the computer's jack posts
    body = body.cut(box(0, SLOT_X1, -(FL_W / 2 + CLR), FL_W / 2 + CLR, -(FL_H / 2 + 0.17), FL_H / 2 + 0.17))
    body = body.cut(box(X_FRONT - 1, 0.01, -21.5, 21.5, -5.6, 5.6))
    for s in (-1, 1):
        y = s * HOLE_PITCH / 2
        body = body.cut(cyl_x(X_FRONT - 1, 0.01, y, 0, 7.2))            # jack post clearance
        body = body.cut(cyl_x(X_FRONT - 1, EAR_X1 + 0.01, y, 0, TS_D + 0.55))   # thumbscrew shaft
        # finger pocket: open at the side, top and bottom
        y0, y1 = (POCKET_Y0, Y_OUT + 1) if s > 0 else (-Y_OUT - 1, -POCKET_Y0)
        body = body.cut(box(EAR_X1, POCKET_X1, y0, y1, -Z_OUT - 1, Z_OUT + 1))
    # screw bosses: bottom half has the clearance hole and counterbore, top half the pilot hole
    for x, y in SCREWS:
        body = body.union(cyl_z(x, y, -Z_IN - 0.01, -PCB_T / 2, 2 * BOSS_R))
        body = body.union(cyl_z(x, y, PCB_T / 2, Z_IN + 0.01, 2 * BOSS_R))
        body = body.cut(cyl_z(x, y, -Z_OUT - 1, -PCB_T / 2 + 0.01, 2.3))
        body = body.cut(cyl_z(x, y, -Z_OUT - 1, -Z_OUT + 1.6, M2_HEAD_D + 0.6))
        body = body.cut(cyl_z(x, y, PCB_T / 2 - 0.01, Z_OUT - 1.4, 1.7))
    # alignment pillars at the rear corners (pins added per half below)
    for x, y in PINS:
        body = body.union(cyl_z(x, y, -Z_IN - 0.01, Z_IN + 0.01, 4.4))
    # USB-C plug opening in the back wall
    body = body.cut(box(X_IN - 0.01, X_OUT + 1, USB_YC - PLUG_W / 2, USB_YC + PLUG_W / 2,
                        USB_ZC - PLUG_H / 2, USB_ZC + PLUG_H / 2))
    # LED windows with light tubes, and the pairing pinhole with a guide tube
    for _, x in LEDS:
        body = body.union(cyl_z(x, LED_Y, LED_TUBE_Z0, Z_IN + 0.01, 3.0))
        body = body.cut(cyl_z(x, LED_Y, LED_TUBE_Z0 - 0.01, Z_OUT + 1, 1.5))
    body = body.union(cyl_z(*PAIR, PAIR_TUBE_Z0, Z_IN + 0.01, 3.4))
    body = body.cut(cyl_z(*PAIR, PAIR_TUBE_Z0 - 0.01, Z_OUT + 1, 1.6))
    return body


def split(body):
    top = body.intersect(box(X_FRONT - 5, X_OUT + 5, -Y_OUT - 5, Y_OUT + 5, 0, Z_OUT + 5))
    bot = body.intersect(box(X_FRONT - 5, X_OUT + 5, -Y_OUT - 5, Y_OUT + 5, -Z_OUT - 5, 0))
    for x, y in PINS:
        bot = bot.union(cyl_z(x, y, -0.01, 1.6, 1.9))
        top = top.cut(cyl_z(x, y, -0.01, 2.0, 2.3))
    top = label(top)
    bot = label_bottom(bot)
    return top, bot


SOURCE_LOCATION = "github.com/Neo-Desktop/pico-serial"


def label_bottom(bot):
    """CERN-OHL-S-2.0 section 4: the product shows its Source Location."""
    try:
        wp = cq.Workplane(cq.Plane(origin=(0, 0, -Z_OUT), xDir=(1, 0, 0), normal=(0, 0, -1)))
        bot = bot.cut(wp.center(29.0, 6.0).text(SOURCE_LOCATION, 2.6, -0.5, kind="bold", halign="center", valign="center"))
        bot = bot.cut(wp.center(29.0, 11.0).text("CERN-OHL-S-2.0", 2.6, -0.5, kind="bold", halign="center", valign="center"))
    except Exception as e:  # noqa: BLE001
        print("note: bottom labels skipped:", e)
    return bot


def label(top):
    """Debossed text on the top face; skipped if no font is available."""
    try:
        wp = cq.Workplane("XY").workplane(offset=Z_OUT)
        for name, x in LEDS:
            top = top.cut(wp.center(x, LED_Y - 3.4).text(name, 2.4, -0.5, kind="bold", halign="center", valign="center"))
        top = top.cut(wp.center(PAIR[0], PAIR[1] + 3.4).text("PAIR", 2.6, -0.5, kind="bold", halign="center"))
        top = top.cut(wp.center(24.0, 2.0).text("pico-serial W", 4.5, -0.5, kind="bold", halign="center", valign="center"))
    except Exception as e:  # noqa: BLE001
        print("note: labels skipped:", e)
    return top


# ---------------------------------------------------------------------------
# Board and parts, for the fit checks and the layout outline
# ---------------------------------------------------------------------------
def board_outline():
    pts = [(BOARD_X0, -BOARD_FRONT_Y), (BOARD_NARROW_X1, -BOARD_FRONT_Y), (BOARD_NARROW_X1, -BOARD_Y),
           (BOARD_X1, -BOARD_Y), (BOARD_X1, TONGUE_Y0), (TONGUE_X1, TONGUE_Y0), (TONGUE_X1, TONGUE_Y1),
           (BOARD_X1, TONGUE_Y1), (BOARD_X1, BOARD_Y), (BOARD_NARROW_X1, BOARD_Y),
           (BOARD_NARROW_X1, BOARD_FRONT_Y), (BOARD_X0, BOARD_FRONT_Y)]
    return pts


def pcb():
    w = cq.Workplane("XY").workplane(offset=-PCB_T / 2).polyline(board_outline()).close().extrude(PCB_T)
    for x, y in SCREWS:
        w = w.cut(cyl_z(x, y, -2, 2, 2.3))
    return w


def db25():
    fl = box(0, FL_T, -FL_W / 2, FL_W / 2, -FL_H / 2, FL_H / 2)
    for s in (-1, 1):
        fl = fl.cut(cyl_x(-1, 2, s * HOLE_PITCH / 2, 0, HOLE_D))
    d = box(-D_LEN, 0, -D_W / 2, D_W / 2, -D_H / 2, D_H / 2)
    bodyr = box(FL_T, FL_T + BODY_T, -BODY_W / 2, BODY_W / 2, -BODY_H / 2, BODY_H / 2)
    cups = box(FL_T + BODY_T, CUP_END, -CUP_Y, CUP_Y, -(ROW_Z + 0.9), ROW_Z + 0.9)
    return fl.union(d).union(bodyr), cups


def parts():
    p = {}
    p["rm2"] = box(BOARD_X1 - RM2_L, BOARD_X1, RM2_YC - RM2_W / 2, RM2_YC + RM2_W / 2, PCB_T / 2, PCB_T / 2 + RM2_H)
    p["usb"] = box(USB_X0, USB_X1, USB_YC - USB_W / 2, USB_YC + USB_W / 2, PCB_T / 2, PCB_T / 2 + USB_H)
    for name, x in LEDS:
        p[f"led {name}"] = box(x - 0.8, x + 0.8, LED_Y - 0.4, LED_Y + 0.4, PCB_T / 2, PCB_T / 2 + 0.8)
    p["pair button"] = box(PAIR[0] - 2.0, PAIR[0] + 2.0, PAIR[1] - 1.5, PAIR[1] + 1.5, PCB_T / 2, PCB_T / 2 + 2.0)
    for s in (-1, 1):
        y = s * HOLE_PITCH / 2
        shaft = cyl_x(EAR_X1 - TS_LEN, EAR_X1, y, 0, TS_D)
        head = cyl_x(EAR_X1, EAR_X1 + TS_HEAD_H, y, 0, TS_HEAD_D)
        p[f"thumbscrew {s:+d}"] = shaft.union(head)
    for i, (x, y) in enumerate(SCREWS):
        head_z = -Z_OUT + 1.6
        # the thread core only: the thread itself cuts into the pilot hole by design
        p[f"M2 screw {i + 1}"] = (cyl_z(x, y, head_z - M2_HEAD_H, head_z, M2_HEAD_D)
                                  .union(cyl_z(x, y, head_z, head_z + M2_LEN, 1.6)))
    return p


METAL = ("db25", "usb", "thumbscrew", "M2 screw")


def vol(a, b):
    try:
        return a.intersect(b).val().Volume()
    except Exception:  # noqa: BLE001  (empty result)
        return 0.0


def check(top, bot, pcbw, dsub, cups, p):
    errs = []
    v = vol(top, bot)
    if v > 1e-3:
        errs.append(f"halves overlap by {v:.3f} mm^3")
    things = dict(p, pcb=pcbw, db25=dsub)
    for name, solid in things.items():
        for hname, h in (("top", top), ("bottom", bot)):
            v = vol(h, solid)
            if v > 1e-3:
                errs.append(f"{hname} half hits {name}: {v:.3f} mm^3")
    for hname, h in (("top", top), ("bottom", bot)):
        v = vol(h, cups)
        if v > 1e-3:
            errs.append(f"{hname} half hits the DB25 solder cups: {v:.3f} mm^3")
    keep = box(KEEP_X0, KEEP_X1, KEEP_Y0, KEEP_Y1, -Z_OUT - 5, Z_OUT + 5)
    for name, solid in things.items():
        if name.startswith(METAL):
            v = vol(keep, solid)
            if v > 1e-3:
                errs.append(f"metal part {name} is inside the RF keep-out ({v:.2f} mm^3)")
    engage = TS_LEN - EAR_X1
    if engage < 4.0:
        errs.append(f"thumbscrews engage only {engage:.1f} mm")
    return errs, engage


def export_2d(path, wires):
    import ezdxf  # noqa: F401  (cadquery's DXF writer uses it)
    w = cq.Workplane("XY")
    for wp in wires:
        w = w.add(wp.vals())
    cq.exporters.export(w, path, "DXF")


def main():
    os.makedirs(OUT, exist_ok=True)
    h = hood()
    top, bot = split(h)
    pcbw = pcb()
    dsub, cups = db25()
    p = parts()
    errs, engage = check(top, bot, pcbw, dsub, cups, p)
    for e in errs:
        print("ERROR:", e)
    if errs:
        sys.exit(1)

    for name, w in (("hood-top", top), ("hood-bottom", bot)):
        cq.exporters.export(w, os.path.join(OUT, f"{name}.step"))
        cq.exporters.export(w, os.path.join(OUT, f"{name}.stl"), tolerance=0.02, angularTolerance=0.1)
    asm = cq.Assembly()
    asm.add(bot, name="hood-bottom", color=cq.Color(0.35, 0.35, 0.38, 1))
    asm.add(top, name="hood-top", color=cq.Color(0.45, 0.45, 0.48, 0.5))
    asm.add(pcbw, name="pcb", color=cq.Color(0.1, 0.45, 0.2, 1))
    asm.add(dsub, name="db25", color=cq.Color(0.75, 0.75, 0.75, 1))
    for name, s in p.items():
        asm.add(s, name=name.replace(" ", "_").replace("+", "p").replace("-", "m"),
                color=cq.Color(0.8, 0.6, 0.2, 1) if name.startswith(METAL) else cq.Color(0.2, 0.2, 0.2, 1))
    asm.save(os.path.join(OUT, "assembly.step"))

    # 2D: board outline with screw holes, and the RF keep-out
    outline = cq.Workplane("XY").polyline(board_outline()).close()
    holes = [cq.Workplane("XY").center(x, y).circle(1.15) for x, y in SCREWS]
    export_2d(os.path.join(OUT, "board-outline.dxf"), [outline] + holes)
    keep = cq.Workplane("XY").rect(KEEP_X1 - KEEP_X0, KEEP_Y1 - KEEP_Y0, centered=False).translate((KEEP_X0, KEEP_Y0, 0))
    export_2d(os.path.join(OUT, "rf-keepout.dxf"), [keep])

    # views
    view = dict(width=900, height=520, marginLeft=20, marginTop=20, showAxes=False, strokeWidth=0.4,
                showHidden=False)
    cq.exporters.export(top.union(bot), os.path.join(OUT, "hood.svg"),
                        opt=dict(view, projectionDir=(-0.8, -1.2, 0.9)))
    cq.exporters.export(bot.union(pcbw).union(dsub).union(p["rm2"]).union(p["usb"])
                        .union(p["thumbscrew +1"]).union(p["thumbscrew -1"]),
                        os.path.join(OUT, "inside.svg"), opt=dict(view, projectionDir=(-0.8, -1.2, 1.4)))
    cq.exporters.export(top, os.path.join(OUT, "top-face.svg"), opt=dict(view, projectionDir=(0, 0, 1)))

    write_placement(engage)
    bb = top.union(bot).val().BoundingBox()
    print(f"ok: hood {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm (+{D_LEN} mm of DB25 shell in front); "
          f"board {BOARD_X1 - BOARD_X0:.1f} x {2 * BOARD_Y:.1f} mm + USB tongue; "
          f"thumbscrews engage {engage:.1f} mm; no collisions, no metal in the RF keep-out")


def write_placement(engage):
    L = ["<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->", "<!-- Copyright (C) 2026 Neo-Desktop -->",
         "# pico-serial W: placement fixed by the hood", "",
         "Generated by `w_hood.py`. Coordinates in mm; x = 0 at the DB25 flange's front face, "
         "+x toward the USB-C end; y = 0 on the hood's centre line, +y toward the USB-C side. "
         "Board top = the side facing the hood's top half.", "",
         "| Item | Position | Notes |", "|---|---|---|",
         f"| Board outline | `board-outline.dxf` | front edge x = {BOARD_X0}, main board to x = {BOARD_X1}, "
         f"USB tongue to x = {TONGUE_X1:.1f}; width ±{BOARD_Y:.1f} (±{BOARD_FRONT_Y:.1f} for x < {BOARD_NARROW_X1:.2f}) |",
         f"| DB25 cup pads | x {BOARD_X0}–{CUP_END + 0.5}, rows on top and bottom | 13-pin row on top |",
         f"| Screw holes, Ø2.3, no copper within Ø5.4 | " + ", ".join(f"({x}, {y})" for x, y in SCREWS) + " | M2 × 10 self-tapping, from below |",
         f"| RM2 (top) | x {BOARD_X1 - RM2_L}–{BOARD_X1}, y {RM2_YC - RM2_W / 2:.2f}–{RM2_YC + RM2_W / 2:.2f} | antenna end flush with the board edge at x = {BOARD_X1} |",
         f"| RF keep-out | x {KEEP_X0}–{KEEP_X1}, y {KEEP_Y0}–{KEEP_Y1:.2f} (`rf-keepout.dxf`) | no copper on any layer, no parts |",
         f"| USB-C (top) | mouth at x = {USB_X1:.1f}, centred y = {USB_YC:.1f} | HRO TYPE-C-31-M-12; overhangs the tongue edge by 0.7 |",
         "| LEDs, 0603 (top) | " + ", ".join(f"{n} ({x}, {LED_Y})" for n, x in LEDS) + " | under light tubes; nothing else taller than 1.2 mm within 1.5 mm |",
         f"| Pairing button (top) | ({PAIR[0]}, {PAIR[1]}) | TS-1088, 2.0 mm tall; reached through a pinhole |",
         f"| Height limits | top ≤ {Z_IN - PCB_T / 2 - 0.3:.1f} mm, bottom ≤ {Z_IN - PCB_T / 2 - 0.3:.1f} mm | inside the hood; USB-C is {USB_H} |",
         f"| Keep clear | x < {BOARD_NARROW_X1:.2f}: board narrows to ±{BOARD_FRONT_Y:.1f} for the thumbscrew pockets | |",
         "", f"Thumbscrews (Keystone 2400, 4-40 × 5/16″) engage the computer's jack posts by {engage:.1f} mm."]
    open(os.path.join(OUT, "placement.md"), "w").write("\n".join(L) + "\n")


if __name__ == "__main__":
    main()
