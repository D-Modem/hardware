#!/usr/bin/env python3
# SPDX-License-Identifier: CERN-OHL-S-2.0
# Copyright (C) 2026 Neo-Desktop
"""Lay out the pico-serial PCB from the netlist script.

  hardware/kicad/build_pcb.py --kicad-fp <kicad-footprints 9.0 checkout> [--route] [--variant w]

--variant w lays out pico-serial W (pico-serial-w.kicad_pcb): the same
circuit plus the RM2 radio, on the outline the printed hood sets
(../enclosure/w_hood.py), with its antenna keep-out and screw holes.

Writes pico-serial.kicad_pcb (and the board's rules and net classes into
pico-serial.kicad_pro) next to the schematic, so the two are one KiCad
project. Every footprint carries its schematic symbol's ID, so KiCad's
"Update PCB from Schematic" finds nothing to change.

Needs KiCad 9's Python module (pcbnew). --route also needs Java 21 and the
Freerouting 1.9 jar (--freerouting) and xvfb-run.

The board outline at the top of this file is inferred from the hood's
outside dimensions (SPEC.md 7.2): confirm it on a sample before ordering.
"""
import argparse, json, os, subprocess, sys, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "netlist"))
if "--variant" in sys.argv[1:-1]:
    os.environ["PS_VARIANT"] = sys.argv[sys.argv.index("--variant") + 1]
_argv, sys.argv = sys.argv, sys.argv[:1]
import build_netlist as N  # noqa: E402  (builds the circuit on import)
sys.argv = _argv
import pcbnew  # noqa: E402

PROJECT = "pico-serial" + N.SUFFIX
LOCAL_FP = os.path.join(HERE, "lib")      # pico-serial.pretty: footprints KiCad doesn't ship
PCB = os.path.join(HERE, f"{PROJECT}.kicad_pcb")
NS = uuid.UUID("0b4c1d1e-5e7a-4c2b-8c51-9f0a7d3e6b21")   # same as build_schematic.py
mm = pcbnew.FromMM

# ---------------------------------------------------------------------------
# Board outline. y = 0 is the edge against the DB25's insulator; +y runs back
# toward the hood's cable exit; x = 0 is the connector's centre line.
# ---------------------------------------------------------------------------
W = 37.0            # across, at the connector (its rear insulator is 37.1)
BODY = 22.0         # full width this far back
TAPER_END = 28.0    # ...narrowing to TAPER_W here
TAPER_W = 24.0
TONGUE_W = 11.0     # into the cable exit, around the USB-C receptacle
MOUTH = 38.0        # back edge = the receptacle's mouth, flush with the exit
CORNER = 0.5        # corner radius-ish chamfer

OUTLINE = [(-W / 2, 0), (W / 2, 0), (W / 2, BODY), (TAPER_W / 2, TAPER_END), (TONGUE_W / 2, TAPER_END),
           (TONGUE_W / 2, MOUTH), (-TONGUE_W / 2, MOUTH), (-TONGUE_W / 2, TAPER_END),
           (-TAPER_W / 2, TAPER_END), (-W / 2, BODY)]

# ---------------------------------------------------------------------------
# Rules: JLCPCB 4-layer standard process, with margin.
# ---------------------------------------------------------------------------
RULES = dict(clearance=0.127, track=0.15, via=0.55, via_drill=0.3, min_via=0.5, min_drill=0.25,
             hole_clearance=0.2, edge_clearance=0.3, silk_min=0.8)

# class: (track, clearance, via diameter, via drill), and the nets in it
NETCLASSES = {
    "Default": ((0.15, 0.127, 0.55, 0.3), []),
    # GND is the In1 plane: every GND pad gets its own via (fanout()), and the
    # autorouter leaves it alone
    # net names as KiCad gives them: power symbols global, labels "/NAME"
    "Plane": ((0.25, 0.15, 0.5, 0.25), ["GND"]),
    "Power": ((0.25, 0.15, 0.55, 0.3), ["+3V3", "+1V1"]),
    "VBUS": ((0.4, 0.15, 0.6, 0.3), ["VBUS", "/VBUS_IN"]),     # 0.5 A
    "Pump": ((0.25, 0.15, 0.55, 0.3), ["/CP*"]),               # the charge pumps' +-5.5 V
    "RS232": ((0.2, 0.15, 0.55, 0.3), ["/RS_*", "/DB25_FG"]),
    "USB": ((0.2, 0.15, 0.55, 0.3), ["/USB_*"]),
}

# Layers: F.Cu signals; In1.Cu a solid GND plane (every signal's reference);
# In2.Cu signals, then a +3V3 pour in what's left; B.Cu signals. Every GND and
# +3V3 pad gets a via (fanout()); +3V3 is also routed, so it never depends on
# how the pour comes out.
PLANES = {"In1.Cu": "GND"}
POURS = [("In2.Cu", "+3V3"), ("F.Cu", "GND"), ("B.Cu", "GND")]   # after routing
FANOUT_NETS = ("GND", "+3V3")
# other pads that get a stub and via up front: the ESD chip's VBUS pin sits
# between D+ and D-, where a track can only leave toward the USB-C pads; its
# via goes under the chip's body instead, leaving room for the D- crossover
FANOUT_EXTRA = {("U4", "5")}

# ---------------------------------------------------------------------------
# Placement: ref -> (x, y, rotation degrees, side). Bottom-side parts are
# flipped left-right, then rotated.
# ---------------------------------------------------------------------------
MCU = (-1.0, 18.0)    # RP2040 centre: far enough back for its front-edge vias to clear the chips' logic pins
CHIP_Y = 9.6          # transceivers' centre line (bottom)
CHIP_A_X, CHIP_B_X = -10.0, 8.5
PLACE = {
    # --- connectors
    "J2": (0.0, 1.99, 180, "F"),           # DB25: pin 1 at x = -16.62; 1-13 on top, 14-25 underneath
    "J1": (0.0, MOUTH - 3.65, 0, "F"),     # USB-C, mouth flush with the back edge
    # --- top: RP2040 (USB and flash pins face back), flash, clock, buttons
    "U1": (*MCU, 180, "F"),
    "U2": (10.2, 21.8, 0, "F"),            # back and right: room for U1's GPIO vias
    "C18": (15.7, 20.3, 90, "F"),          # flash VCC
    "Y1": (2.8, 11.4, 0, "F"),
    "R8": (-0.5, 11.6, 90, "F"),           # XOUT series
    "C16": (-1.7, 11.6, 90, "F"),          # XIN load
    "C17": (5.5, 10.55, 90, "F"),          # XTAL_OUT load
    "SW1": (12.5, 7.2, 0, "F"),            # BOOTSEL
    "R9": (8.4, 7.2, 90, "F"),
    "SW2": (12.5, 11.2, 0, "F"),           # RESET
    "R10": (8.4, 11.2, 90, "F"),           # RUN pull-up
    "R11": (-16.6, 5.6, 90, "F"),          # frame ground 0R, by DB25 pin 1
    # --- top, back: USB and power
    "U4": (-0.4, 26.9, 270, "F"),          # USB ESD: D- pins on the left, D+ on the right, as at U1
    "R6": (-1.65, 23.2, 90, "F"),          # D+ 27R
    "R7": (-2.75, 23.2, 90, "F"),          # D- 27R
    "R1": (-3.6, 27.9, 90, "F"),           # CC1
    "R2": (3.0, 27.6, 90, "F"),            # CC2
    "F1": (-7.6, 26.3, 0, "F"),
    "D1": (-7.6, 23.7, 0, "F"),
    "C2": (-12.0, 25.6, 0, "F"),           # VBUS 10uF
    "U3": (-12.0, 22.6, 0, "F"),           # LDO
    "C3": (-7.6, 21.3, 0, "F"),            # 3V3 10uF
    "R4": (-11.0, 19.0, 0, "F"),           # VBUS divider, out of U1's left-side escape
    "R5": (-11.0, 20.1, 0, "F"),
    # --- bottom: transceivers, RS-232 pins toward the DB25, charge pumps at the pin-1 end
    # chip A (DCE) on the left, chip B (DTE) on the right: of the arrangements
    # tried, the one whose RP2040-to-chip connections cross each other least
    "U5": (CHIP_A_X, CHIP_Y, 270, "B"),    # chip A, DCE
    # charge pumps at the pin-1 end, 1.75 mm apart (room for the GND vias), each
    # with pad 1 (the chip-pin side; pad 2 is GND or the far flying-cap pin) toward the chip
    "C20": (-16.9, 6.2, 180, "B"), "C22": (-16.9, 7.95, 180, "B"),  # C2 flying, V- (0603)
    "C19": (-16.9, 9.7, 180, "B"), "C21": (-16.9, 11.45, 180, "B"), # C1 flying, V+ (0603, nearest pin 27)
    "C23": (-13.2, 15.4, 0, "B"), "C24": (-11.0, 15.4, 0, "B"),     # VCC
    "R12": (-9.0, 15.4, 0, "B"),           # A_SHDN_N pull-down
    "U6": (CHIP_B_X, CHIP_Y, 270, "B"),    # chip B, DTE
    "C26": (1.6, 6.2, 180, "B"), "C28": (1.6, 7.95, 180, "B"),
    "C25": (1.6, 9.7, 180, "B"), "C27": (1.6, 11.45, 180, "B"),
    "C29": (5.4, 15.4, 0, "B"), "C30": (7.6, 15.4, 0, "B"),
    "R13": (15.0, 6.6, 90, "B"),           # B_FORCEOFF_N pull-down
    "R14": (15.0, 8.8, 90, "B"),           # RS_FORCEON pull-down
    # --- bottom: RP2040 decoupling, ~1.8 mm beyond its pad ends: close, but
    # leaving a ring free for the pins' own escape vias
    "C4": (4.6, 21.0, 90, "B"), "C5": (4.6, 18.0, 90, "B"),            # IOVDD 1, 10
    "C6": (-1.0, 12.0, 0, "B"), "C14": (-3.0, 12.0, 0, "B"),           # IOVDD 22, DVDD 23
    # at U1's left corners, clear of the left side's escape (LEDs, GP28, GP29)
    "C7": (-6.0, 16.0, 0, "B"), "C8": (-5.9, 21.9, 0, "B"),            # IOVDD 33, 42
    # back edge: two short columns either side of the USB D+/D- corridor
    # (x -3.0..-1.2), so no via lands between U1 and the series resistors
    "C11": (-4.9, 23.2, 0, "B"), "C12": (-4.9, 24.4, 0, "B"), "C13": (-4.9, 25.6, 0, "B"),   # AVDD 43, VREG_VIN 44, VREG_VOUT 45
    "C10": (0.4, 21.6, 0, "B"), "C9": (0.4, 22.65, 0, "B"), "C15": (0.4, 23.7, 0, "B"),       # USB_VDD 48, IOVDD 49, DVDD 50
    # --- bottom, back of the tongue: USB shield
    "R3": (-2.2, 36.6, 0, "B"), "C1": (2.2, 36.6, 0, "B"),
}
# test pads: bottom, a 2.6 mm grid behind the flash (pogo-pin friendly), leaving
# the space between U1 and chip A free for the GPIO lines' vias
_tp = [t for t in (f"TP{n}" for n in range(1, 13)) if t != "TP8"]
for i, ref in enumerate(_tp):
    PLACE[ref] = (6.5 + 2.6 * (i % 4), 21.0 + 2.6 * (i // 4), 0, "B")
PLACE["TP8"] = (-9.5, 23.8, 0, "B")      # GP28: near its pin, on U1's left
# optional LEDs (not fitted): bottom, behind chip B, LED then its resistor
for i, n in enumerate(range(2, 8)):
    x, y = (-16.4 if i < 3 else -10.6), 17.4 + 2.0 * (i % 3)
    PLACE[f"D{n}"] = (x, y, 0, "B")
    PLACE[f"R{n + 13}"] = (x + 2.7, y, 0, "B")


# ---------------------------------------------------------------------------
# pico-serial W: the front half (DB25, both chips, RP2040, flash, clock,
# buttons, test pads) is placed exactly as on the hood board; the outline,
# RM2, USB-C, LEDs, pairing button and screws are where the printed hood
# puts them (../enclosure/out/placement.md). The hood's x runs back from the
# DB25 flange and its y across; here y runs back from the board's front edge.
# ---------------------------------------------------------------------------
SCREWS, RF_KEEPOUT = [], None
if N.W:
    sys.path.insert(0, os.path.join(HERE, "..", "enclosure"))
    import w_hood as H  # noqa: E402

    def hood(x, y):
        return (round(y, 3), round(x - H.BOARD_X0, 3))

    OUTLINE = [hood(*p) for p in H.board_outline()]
    MOUTH = H.USB_X1 - H.BOARD_X0                 # USB-C mouth, 0.7 past the tongue's edge
    SCREWS = [hood(*p) for p in H.SCREWS]         # M2 self-tappers through the board
    SCREW_HOLE, SCREW_CLEAR = 2.3, 5.4            # hole; no copper or parts within this diameter
    (kx0, ky0), (kx1, ky1) = hood(H.KEEP_X0, H.KEEP_Y0), hood(H.KEEP_X1, H.KEEP_Y1)
    RF_KEEPOUT = (min(kx0, kx1), min(ky0, ky1), max(kx0, kx1), max(ky0, ky1))
    RM2_C = hood(H.BOARD_X1 - H.RM2_L / 2, H.RM2_YC)
    for r in [r for r in PLACE if r.startswith(("D", "TP")) or r in ("R15", "R16", "R17", "R18", "R19", "R20")]:
        del PLACE[r]
    PLACE.update({
        "J1": (H.USB_YC, MOUTH - 3.65, 0, "F"),
        # RM2: antenna end flush with the board's back edge (footprint antenna is -y)
        "U7": (*RM2_C, 180, "F"),
        # its supply: VIN (pin 16) and VDDIO (pin 14) face the left edge and the RP2040
        "C32": (-18.4, 26.6, 90, "F"), "C31": (-18.4, 29.0, 90, "F"), "C33": (-18.4, 32.4, 90, "F"),
        # gSPI: data-out and host-wake share GP24 through these
        "R20": (0.4, 27.2, 90, "F"), "R21": (-6.6, 23.6, 90, "F"),
        # USB-C: ESD and CC resistors in the tongue, as on the hood board relative to J1
        "U4": (H.USB_YC - 0.4, MOUTH - 3.65 - 7.45, 270, "F"),
        "R1": (H.USB_YC - 3.6, MOUTH - 3.65 - 6.45, 90, "F"),
        "R2": (H.USB_YC + 3.0, MOUTH - 3.65 - 6.75, 90, "F"),
        "R3": (H.USB_YC - 2.2, MOUTH - 3.65 + 1.55, 0, "B"), "C1": (H.USB_YC + 2.2, MOUTH - 3.65 + 1.55, 0, "B"),
        # fuse, TVS, LDO: a column between the antenna keep-out and the rear screw
        "F1": (13.2, 41.2, 0, "F"), "D1": (13.2, 38.9, 0, "F"), "C2": (13.2, 36.8, 0, "F"),
        "U3": (13.2, 34.0, 0, "F"), "C3": (13.2, 31.2, 0, "F"),
        "R4": (9.6, 34.6, 0, "F"), "R5": (9.6, 35.7, 0, "F"),   # VBUS sense, to the RM2's GPIO0
        # the front strip is 0.5 mm narrower than the hood board's (thumbscrew pockets):
        # chip A and its charge-pump column move in
        "U5": (CHIP_A_X + 0.6, CHIP_Y, 270, "B"),
        "C20": (-16.4, 6.2, 180, "B"), "C22": (-16.4, 7.95, 180, "B"),
        "C19": (-16.4, 9.7, 180, "B"), "C21": (-16.4, 11.45, 180, "B"),
    })
    # status LEDs under the hood's light tubes, each with its resistor beside it
    for i, (name, hx) in enumerate(H.LEDS):
        x, y = hood(hx, H.LED_Y)
        PLACE[f"D{2 + i}"] = (x, y, 90, "F")
        PLACE[f"R{15 + i}"] = (x - 1.7, y, 90, "F")
    PLACE["SW3"] = (*hood(*H.PAIR), 90, "F")       # pairing button, under the hood's pinhole
    for i, ref in enumerate(f"TP{n}" for n in range(1, 12)):
        PLACE[ref] = (6.5 + 2.6 * (i % 4), 21.0 + 2.6 * (i // 4), 0, "B")


def uid(*parts):
    return str(uuid.uuid5(NS, "/".join(map(str, parts))))


def footprint_dir(args):
    for d in (args.kicad_fp, os.environ.get("KICAD9_FOOTPRINT_DIR"), "/usr/share/kicad/footprints"):
        if d and os.path.isdir(os.path.join(d, "Connector_Dsub.pretty")):
            return d
    sys.exit("KiCad 9 footprint libraries not found: pass --kicad-fp <kicad-footprints checkout>")


def set_rules(board):
    ds = board.GetDesignSettings()
    ds.m_MinClearance = mm(RULES["clearance"])
    ds.m_TrackMinWidth = mm(0.127)
    ds.m_ViasMinSize = mm(RULES["min_via"])
    ds.m_MinThroughDrill = mm(RULES["min_drill"])
    ds.m_ViasMinAnnularWidth = mm(0.1)
    ds.m_HoleClearance = mm(RULES["hole_clearance"])
    ds.m_HoleToHoleMin = mm(0.25)
    ds.m_CopperEdgeClearance = mm(RULES["edge_clearance"])
    ds.m_MinSilkTextHeight = mm(RULES["silk_min"])
    ds.m_TentViasFront = ds.m_TentViasBack = True
    ns = ds.m_NetSettings
    ns.ClearNetclassPatternAssignments()
    for name, ((tw, cl, vd, vdr), pats) in NETCLASSES.items():
        nc = ns.GetDefaultNetclass() if name == "Default" else pcbnew.NETCLASS(name)
        nc.SetTrackWidth(mm(tw)); nc.SetClearance(mm(cl))
        nc.SetViaDiameter(mm(vd)); nc.SetViaDrill(mm(vdr))
        if name != "Default":
            ns.SetNetclass(name, nc)
            for p in pats:
                ns.SetNetclassPatternAssignment(p, name)


def outline(board):
    pts = OUTLINE + OUTLINE[:1]
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pcbnew.VECTOR2I(mm(x1), mm(y1))); s.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(y2)))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        board.Add(s)


def schematic_nets():
    """(ref, pin) -> net name exactly as KiCad names it from the schematic: local
    labels are "/NAME", power symbols "NAME", open pins "unconnected-(...)". Using
    KiCad's own names is what makes the PCB match the schematic (DRC parity)."""
    import re, tempfile
    sch = os.path.join(HERE, f"{PROJECT}.kicad_sch")
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "sch.net")
        subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o", out, sch],
                       check=True, capture_output=True)
        text = open(out).read()
    pin_net = {}
    for m in re.finditer(r'\(net \(code "\d+"\) \(name "([^"]*)"\)(.*?)(?=\(net \(code|\Z)', text, re.S):
        for ref, pin in re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', m.group(2)):
            pin_net[(ref, pin)] = m.group(1)
    # the same circuit as the netlist script, names aside
    mine = {(r, p): n for n, mem in N.nets.items() for r, p in mem}
    for k, n in mine.items():
        if pin_net.get(k, "").lstrip("/") != n:
            sys.exit(f"schematic and netlist script disagree at {k}: {pin_net.get(k)} vs {n}")
    extra = [k for k, n in pin_net.items() if k not in mine and not n.startswith("unconnected-")]
    if extra:
        sys.exit(f"schematic has connections the netlist script doesn't: {extra[:5]}")
    return pin_net


def add_parts(board, fpdir):
    pin_net = schematic_nets()
    nets = {}
    for name in sorted(set(pin_net.values())):
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        nets[name] = ni
    fps = {}
    for ref, p in N.parts.items():
        lib, name = p["footprint"].split(":")
        fp = pcbnew.FootprintLoad(os.path.join(LOCAL_FP if lib == "pico-serial" else fpdir, f"{lib}.pretty"), name)
        if fp is None:
            sys.exit(f"{ref}: footprint {p['footprint']} not found")
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        fp.SetReference(ref)
        fp.SetValue(p["value"])
        fp.SetPath(pcbnew.KIID_PATH("/" + uid("sym", ref)))
        for k, v in (("LCSC", p["lcsc"]), ("Description", p["desc"])):
            if v:
                fp.SetField(k, v)
                fp.GetFieldByName(k).SetVisible(False)
        if p["dnp"]:
            fp.SetDNP(True); fp.SetExcludedFromPosFiles(True)   # in the BOM, marked DNP, as in the schematic
        if p["assembly"] != "jlc":
            fp.SetExcludedFromPosFiles(True)
            if p["assembly"] == "none":
                fp.SetExcludedFromBOM(True)
        fp.Reference().SetLayer(pcbnew.F_Fab)   # flipped with the part; the silkscreen has no room
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.5), mm(0.5)))
        fp.Reference().SetTextThickness(mm(0.08))
        fp.Reference().SetPosition(fp.GetPosition())
        for pad in fp.Pads():
            n = pin_net.get((ref, pad.GetNumber()))
            if n:
                pad.SetNet(nets[n])
        board.Add(fp)
        fps[ref] = fp
    return fps


def place(fps):
    flip = getattr(pcbnew, "FLIP_DIRECTION_LEFT_RIGHT", True)
    for ref, (x, y, rot, side) in PLACE.items():
        fp = fps[ref]
        pos = pcbnew.VECTOR2I(mm(x), mm(y))
        fp.SetPosition(pos)
        if side == "B" and not fp.IsFlipped():
            fp.Flip(pos, flip)
        fp.SetOrientationDegrees(rot)
    missing = sorted(set(fps) - set(PLACE))
    if missing:
        sys.exit(f"not placed: {' '.join(missing)}")


REVISION = "A"
# silkscreen: (text, x, y, height, side, justify); SPEC.md 7.3 and the licence notice (LICENSING.md)
SILK = [
    (f"pico-serial{' W' if N.W else ''} rev {REVISION}", -9.8, 9.4, 1.0, "F", "c"),
    ("DCE / DTE auto", -9.8, 10.9, 0.8, "F", "c"),
    ("DTE: male-male changer", -9.8, 12.2, 0.8, "F", "c"),
    # the licence and its Source Location, CERN-OHL-S-2.0 section 4 (LICENSING.md)
    ("github.com/Neo-Desktop/pico-serial", 0.0, 4.45, 0.8, "B", "c"),  # between the cups and the chips
    ("CERN-OHL-S-2.0", -9.8, 7.9, 0.8, "F", "c"),
]
if N.W:
    SILK += [(name, PLACE[f"D{2 + i}"][0] - 3.6, PLACE[f"D{2 + i}"][1] + 0.3, 0.8, "F", "c")
             for i, (name, _) in enumerate(H.LEDS)]
    SILK += [("PAIR", PLACE["SW3"][0], PLACE["SW3"][1] - 3.6, 0.8, "F", "c"),
             ("ANTENNA", 3.9, RF_KEEPOUT[1] + 2.0, 0.8, "F", "c"),
             ("no copper", 3.9, RF_KEEPOUT[1] + 3.3, 0.8, "F", "c")]


def silk(board):
    for text, x, y, h, side, just in SILK:
        t = pcbnew.PCB_TEXT(board)
        t.SetText(text)
        t.SetLayer(pcbnew.F_SilkS if side == "F" else pcbnew.B_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(h), mm(h)))
        t.SetTextThickness(mm(max(0.15, h * 0.15)))
        t.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        if side == "B":
            t.SetMirrored(True)
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER)
        board.Add(t)


def poly_zone(board, layer, netname, pts, priority=0, clearance=0.25, thermal=False):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNetCode(board.FindNet(netname).GetNetCode())
    ol = z.Outline()
    ol.NewOutline()
    for x, y in pts:
        ol.Append(mm(x), mm(y))
    z.SetLocalClearance(mm(clearance))
    z.SetMinThickness(mm(0.2))
    z.SetAssignedPriority(priority)
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL if thermal else pcbnew.ZONE_CONNECTION_FULL)
    z.SetThermalReliefGap(mm(0.25))
    z.SetThermalReliefSpokeWidth(mm(0.3))
    board.Add(z)
    return z


def planes(board):
    for lname, net in PLANES.items():
        layer = board.GetLayerID(lname)
        board.SetLayerType(layer, pcbnew.LT_POWER)
        poly_zone(board, layer, net, OUTLINE)


def pours(board):
    """Copper pours added after routing, filling around the tracks."""
    for lname, net in POURS:
        thermal = lname in ("F.Cu", "B.Cu")
        poly_zone(board, board.GetLayerID(lname), net, OUTLINE, priority=1 if not thermal else 0,
                  thermal=thermal)


def hole_keepouts(board):
    """No plane copper near unplated holes (the USB-C's locating pegs)."""
    import math
    for pad in board.GetPads():
        if pad.GetAttribute() != pcbnew.PAD_ATTRIB_NPTH:
            continue
        c = pad.GetPosition()
        r = pcbnew.ToMM(pad.GetDrillSize().x) / 2 + RULES["hole_clearance"] + 0.1
        z = pcbnew.ZONE(board)
        z.SetIsRuleArea(True)
        z.SetDoNotAllowCopperPour(True)
        z.SetDoNotAllowTracks(False); z.SetDoNotAllowVias(False); z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
        ls = pcbnew.LSET()
        for name in ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu"):
            ls.AddLayer(board.GetLayerID(name))
        z.SetLayerSet(ls)
        ol = z.Outline()
        ol.NewOutline()
        for i in range(16):
            a = 2 * math.pi * i / 16
            ol.Append(c.x + mm(r * math.cos(a)), c.y + mm(r * math.sin(a)))
        board.Add(z)


def edge_keepout(board):
    """No tracks or vias within the edge clearance (Freerouting doesn't apply KiCad's):
    a thin strip inside each edge."""
    w = RULES["edge_clearance"] + 0.05
    n = len(OUTLINE)
    area2 = sum(OUTLINE[k][0] * OUTLINE[(k + 1) % n][1] - OUTLINE[(k + 1) % n][0] * OUTLINE[k][1] for k in range(n))
    side = 1 if area2 > 0 else -1   # which normal points inside
    ls = pcbnew.LSET()
    for name in ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu"):
        ls.AddLayer(board.GetLayerID(name))
    for k in range(n):
        (x1, y1), (x2, y2) = OUTLINE[k], OUTLINE[(k + 1) % n]
        L = ((x2 - x1) ** 2 + (y2 - y1) ** 2) ** 0.5
        nx, ny = -side * (y2 - y1) / L * w, side * (x2 - x1) / L * w
        z = pcbnew.ZONE(board)
        z.SetIsRuleArea(True)
        z.SetDoNotAllowTracks(True); z.SetDoNotAllowVias(True)
        z.SetDoNotAllowCopperPour(False); z.SetDoNotAllowPads(False); z.SetDoNotAllowFootprints(False)
        z.SetLayerSet(ls)
        ol = z.Outline()
        ol.NewOutline()
        for x, y in ((x1, y1), (x2, y2), (x2 + nx, y2 + ny), (x1 + nx, y1 + ny)):
            ol.Append(mm(x), mm(y))
        board.Add(z)


def rule_area(board, pts, layers, tracks=True, vias=True, pour=True, pads=False, footprints=False):
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(tracks); z.SetDoNotAllowVias(vias); z.SetDoNotAllowCopperPour(pour)
    z.SetDoNotAllowPads(pads); z.SetDoNotAllowFootprints(footprints)
    ls = pcbnew.LSET()
    for name in layers:
        ls.AddLayer(board.GetLayerID(name))
    z.SetLayerSet(ls)
    ol = z.Outline()
    ol.NewOutline()
    for x, y in pts:
        ol.Append(mm(x), mm(y))
    board.Add(z)
    return z


CU = ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu")


def rf_keepout(board):
    """SPEC-W 2: no metal in the RM2's antenna keep-out, on any layer (the
    module's own top 5 mm included, which holds no pads)."""
    if RF_KEEPOUT:
        x0, y0, x1, y1 = RF_KEEPOUT
        z = rule_area(board, ((x0, y0), (x1, y0), (x1, y1), (x0, y1)), CU, pads=True)
        z.SetZoneName("RM2 antenna keep-out")


def screw_holes(board):
    """The hood's M2 screws pass through the board: a plain hole each (a board-only
    footprint, so it isn't expected in the schematic), and a keep-out around it for
    the screw head and the hood's bosses."""
    import math
    for k, (x, y) in enumerate(SCREWS, 1):
        fp = pcbnew.FOOTPRINT(board)
        fp.SetReference(f"H{k}"); fp.SetValue("M2 screw")
        fp.SetBoardOnly(True); fp.SetExcludedFromBOM(True); fp.SetExcludedFromPosFiles(True)
        fp.Reference().SetVisible(False); fp.Value().SetVisible(False)
        pad = pcbnew.PAD(fp)
        pad.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
        pad.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
        pad.SetSize(pcbnew.VECTOR2I(mm(SCREW_HOLE), mm(SCREW_HOLE)))
        pad.SetDrillSize(pcbnew.VECTOR2I(mm(SCREW_HOLE), mm(SCREW_HOLE)))
        pad.SetLayerSet(pad.UnplatedHoleMask())
        fp.Add(pad)
        fp.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        board.Add(fp)
        r = SCREW_CLEAR / 2
        rule_area(board, [(x + r * math.cos(2 * math.pi * i / 24), y + r * math.sin(2 * math.pi * i / 24))
                          for i in range(24)], CU, footprints=True)


def crystal_keepout(board, fps):
    """Nothing passes under the crystal: keep-outs in the gaps between its four
    pads (tracks on F.Cu; vias on every layer). Its own pads are still reached
    from outside. Below F.Cu, the In1 GND plane shields it."""
    y1 = fps["Y1"]
    c = y1.GetPosition()
    def area(x0, y0, x1, y1_, layers, tracks):
        z = pcbnew.ZONE(board)
        z.SetIsRuleArea(True)
        z.SetDoNotAllowTracks(tracks); z.SetDoNotAllowVias(True)
        z.SetDoNotAllowCopperPour(False); z.SetDoNotAllowPads(False); z.SetDoNotAllowFootprints(False)
        ls = pcbnew.LSET()
        for name in layers:
            ls.AddLayer(board.GetLayerID(name))
        z.SetLayerSet(ls)
        ol = z.Outline()
        ol.NewOutline()
        for x, y in ((x0, y0), (x1, y0), (x1, y1_), (x0, y1_)):
            ol.Append(c.x + mm(x), c.y + mm(y))
        board.Add(z)
    # pads 1.4 x 1.15 at (+-1.1, +-0.85): the cross between them, with 0.05 to spare
    for x0, y0, x1, y1_ in ((-0.35, -1.3, 0.35, 1.3), (-1.85, -0.22, 1.85, 0.22)):
        area(x0, y0, x1, y1_, ["F.Cu"], True)
        area(x0, y0, x1, y1_, ["In1.Cu", "In2.Cu", "B.Cu"], False)


def ep_vias(board, fps):
    """GND vias in the RP2040's exposed pad (heat, and its ground)."""
    u1 = fps["U1"]
    c = u1.FindPadByNumber("57").GetPosition()
    gnd = board.FindNet("GND")
    for dx in (-0.8, 0.8):
        for dy in (-0.8, 0.8):
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pcbnew.VECTOR2I(c.x + mm(dx), c.y + mm(dy)))
            v.SetWidth(mm(0.5)); v.SetDrill(mm(0.25))
            v.SetNet(gnd)
            board.Add(v)


FAN_VIA = (0.5, 0.25)    # fanout via: diameter, drill
FANOUT_IDS = set()       # their stubs and vias: never ripped up
FAN_CLEAR = 0.15


def _inside(board_pts, x, y, margin):
    """Point inside the outline, at least margin from every edge."""
    inside = False
    n = len(board_pts)
    for i in range(n):
        (x1, y1), (x2, y2) = board_pts[i], board_pts[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            inside = not inside
        dx, dy = x2 - x1, y2 - y1
        t = max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / (dx * dx + dy * dy)))
        if ((x - x1 - t * dx) ** 2 + (y - y1 - t * dy) ** 2) ** 0.5 < margin:
            return False
    return inside


def fanout(board):
    """A short stub and a via from every GND / +3V3 surface-mount pad down to its plane."""
    vr = FAN_VIA[0] / 2
    pads = [p for p in board.GetPads()]
    vias = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_VIA_T]
    tracks = []
    fails = []
    lay = lambda p: pcbnew.F_Cu if p.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu

    def clear(v, seg, layer, own):
        circ = pcbnew.SHAPE_CIRCLE(v, mm(vr))
        for q in pads:
            if q is own:
                continue
            same = q.GetNetname() == own.GetNetname()
            for L in (pcbnew.F_Cu, pcbnew.B_Cu):
                if q.IsOnLayer(L) and q.GetEffectiveShape(L).Collide(circ, mm(0.05 if same else FAN_CLEAR)):
                    return False
                if L == layer and not same and q.IsOnLayer(L) and q.GetEffectiveShape(L).Collide(seg, mm(FAN_CLEAR)):
                    return False
        for o in vias:
            if (o.GetPosition() - v).EuclideanNorm() < mm(FAN_VIA[0] + FAN_CLEAR):
                return False
            if o.GetNetname() != own.GetNetname() and seg.Collide(
                    pcbnew.SHAPE_CIRCLE(o.GetPosition(), o.GetWidth(pcbnew.F_Cu) // 2), mm(FAN_CLEAR)):
                return False
        for t, tl, net in tracks:
            if net != own.GetNetname() and (t.Collide(circ, mm(FAN_CLEAR)) or (tl == layer and t.Collide(seg, mm(FAN_CLEAR)))):
                return False
        return True

    for pad in pads:
        net = pad.GetNetname()
        fp = pad.GetParentFootprint()
        if (net not in FANOUT_NETS and (fp.GetReference(), pad.GetNumber()) not in FANOUT_EXTRA) \
                or pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
            continue
        if fp.GetReference() == "U1" and pad.GetNumber() == "57":
            continue   # exposed pad: has its own vias
        layer = lay(pad)
        pc = pad.GetPosition()
        bb = pad.GetBoundingBox()
        hw, hh = pcbnew.ToMM(bb.GetWidth()) / 2, pcbnew.ToMM(bb.GetHeight()) / 2
        # U1: under the QFN, between its pads and the exposed pad, so the ring outside stays free
        inward = fp.GetReference() == "U1" or (fp.GetReference(), pad.GetNumber()) in FANOUT_EXTRA
        if fp.GetReference() == "J2":
            out = (0, 1)
        else:
            d = pc - fp.GetPosition()
            dx, dy = pcbnew.ToMM(d.x), pcbnew.ToMM(d.y)
            out = ((1 if dx > 0 else -1), 0) if abs(dx) >= abs(dy) else (0, (1 if dy > 0 else -1))
        dirs = [out, (out[1], out[0]), (-out[1], -out[0]), (out[0] + out[1], out[1] + out[0]),
                (out[0] - out[1], out[1] - out[0]), (-out[0], -out[1])]
        if inward:
            dirs = [(-out[0], -out[1])]
        w = min(0.25, 2 * min(hw, hh))
        done = False
        for ddx, ddy in dirs:
            norm = (ddx * ddx + ddy * ddy) ** 0.5
            ux, uy = ddx / norm, ddy / norm
            ext = abs(ux) * hw + abs(uy) * hh
            for k in range(20):
                dist = ext + FAN_CLEAR + vr + 0.1 * k
                vx, vy = pcbnew.ToMM(pc.x) + ux * dist, pcbnew.ToMM(pc.y) + uy * dist
                if not _inside(OUTLINE, vx, vy, vr + RULES["edge_clearance"] + 0.06):   # past edge_keepout()'s strip
                    break
                v = pcbnew.VECTOR2I(mm(vx), mm(vy))
                seg = pcbnew.SHAPE_SEGMENT(pc, v, mm(w))
                if clear(v, seg, layer, pad):
                    t = pcbnew.PCB_TRACK(board)
                    t.SetStart(pc); t.SetEnd(v); t.SetWidth(mm(w)); t.SetLayer(layer); t.SetNet(pad.GetNet())
                    board.Add(t)
                    via = pcbnew.PCB_VIA(board)
                    via.SetPosition(v); via.SetWidth(mm(FAN_VIA[0])); via.SetDrill(mm(FAN_VIA[1]))
                    via.SetNet(pad.GetNet())
                    board.Add(via)
                    FANOUT_IDS.update((t.m_Uuid.AsString(), via.m_Uuid.AsString()))
                    vias.append(via)
                    tracks.append((seg, layer, net))
                    done = True
                    break
            if done:
                break
        if not done:
            fails.append(f"{fp.GetReference()}.{pad.GetNumber()}")
    if fails:
        print("fanout: no room for a via at", " ".join(fails))
    return fails


def route(board, jar, passes, workdir):
    """Autoroute with Freerouting 1.9 (its pass limit works on the command line;
    2.1's doesn't). It needs a display, so it runs under xvfb-run. GND and +3V3
    (class Plane) are left out: fanout() already took them to the planes."""
    workdir = os.path.abspath(workdir)
    dsn = os.path.join(workdir, f"{PROJECT}.dsn")
    ses = os.path.join(workdir, f"{PROJECT}.ses")
    if os.path.exists(ses):
        os.remove(ses)
    if not pcbnew.ExportSpecctraDSN(board, dsn):
        sys.exit("DSN export failed")
    cmd = ["xvfb-run", "-a", "java", "-jar", jar, "-de", dsn, "-do", ses, "-mp", str(passes),
           "-inc", "Plane", "-da"]
    print("routing:", " ".join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=workdir)   # it writes logs/ there
    log = os.path.join(workdir, "freerouting.log")
    open(log, "w").write(r.stdout + r.stderr)
    if r.returncode or not os.path.exists(ses):
        sys.exit(f"Freerouting failed, see {log}")
    if not pcbnew.ImportSpecctraSES(board, ses):
        sys.exit("SES import failed")


# ---------------------------------------------------------------------------
# A small grid router to finish what Freerouting leaves (usually a few short
# links in crowded spots): A* on a 0.1 mm grid over F.Cu, In2.Cu and B.Cu,
# every step checked against KiCad's own copper shapes.
# ---------------------------------------------------------------------------
GRID = 0.1
DEBUG = False
ALL_CU = {pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu}
VIA_COST = 12.0


def _islands(board, net):
    """The net's pads, tracks and vias, grouped by what's joined (zones aside)."""
    conn = board.GetConnectivity()
    key = lambda it: it.m_Uuid.AsString()   # the Python wrappers differ call to call
    pads = [p for p in board.GetPads() if p.GetNetname() == net]
    seen, groups = set(), []
    for p in pads:
        if key(p) in seen:
            continue
        group, todo = [], [p]
        seen.add(key(p))
        while todo:
            it = todo.pop()
            group.append(it)
            for q in list(conn.GetConnectedTracks(it)) + list(conn.GetConnectedPads(it)):
                if q.GetNetname() == net and key(q) not in seen:
                    seen.add(key(q))
                    todo.append(q)
        groups.append(group)
    return groups


def _geom(item):
    """An item's copper as plain numbers, read from KiCad once:
    ("seg", x1, y1, x2, y2, r) | ("circ", x, y, r) | ("rect", x, y, hw, hh, angle), and its layers."""
    import math
    T = pcbnew.ToMM
    item = item.Cast()   # connectivity hands vias back as plain tracks
    if item.Type() == pcbnew.PCB_VIA_T:
        p = item.GetPosition()
        return ("circ", T(p.x), T(p.y), T(item.GetWidth(pcbnew.F_Cu)) / 2), ALL_CU
    if item.Type() == pcbnew.PCB_TRACE_T:
        a, b = item.GetStart(), item.GetEnd()
        return ("seg", T(a.x), T(a.y), T(b.x), T(b.y), T(item.GetWidth()) / 2), {item.GetLayer()}
    p = item.GetPosition()
    layers = {L for L in ALL_CU if item.IsOnLayer(L)}
    if item.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
        return ("circ", T(p.x), T(p.y), T(item.GetDrillSize().x) / 2), ALL_CU
    sz = item.GetSize(pcbnew.F_Cu) if hasattr(item, "GetSize") else item.GetSize()
    hw, hh = T(sz.x) / 2, T(sz.y) / 2
    if item.GetShape(pcbnew.F_Cu) == pcbnew.PAD_SHAPE_CIRCLE:
        return ("circ", T(p.x), T(p.y), hw), layers
    return ("rect", T(p.x), T(p.y), hw, hh, math.radians(item.GetOrientationDegrees())), layers


def _dist(g, X, Y):
    """Distance (mm, 0 inside) from grid points X, Y (numpy arrays) to the shape."""
    import numpy as np
    if g[0] == "circ":
        return np.maximum(np.hypot(X - g[1], Y - g[2]) - g[3], 0)
    if g[0] == "seg":
        x1, y1, x2, y2, r = g[1:]
        dx, dy = x2 - x1, y2 - y1
        L2 = dx * dx + dy * dy
        t = np.clip(((X - x1) * dx + (Y - y1) * dy) / L2, 0, 1) if L2 else 0
        return np.maximum(np.hypot(X - x1 - t * dx, Y - y1 - t * dy) - r, 0)
    x, y, hw, hh, a = g[1:]
    c, s_ = np.cos(a), np.sin(a)
    lx, ly = (X - x) * c - (Y - y) * s_, (X - x) * s_ + (Y - y) * c
    ex, ey = np.maximum(np.abs(lx) - hw, 0), np.maximum(np.abs(ly) - hh, 0)
    return np.hypot(ex, ey)


RIP_COST = 4.0   # mm-equivalent for crossing another net's track in a rip-up search


def _route_link(board, net, src, dst, layers, narrow=False, rip=False, keep=()):
    """A* from island src to island dst over a grid. Returns the items it added
    (empty if it found no way).
    narrow: use the default class's width and via (a short stub, e.g. VBUS to
    the ESD chip's reference pin, between pins a wide track can't pass).
    rip: a dry run that may cross other signal nets' tracks and vias (at a
    cost); returns the set of nets its path crosses, or None. Nets in keep
    are never crossed."""
    import heapq
    import numpy as np
    T = pcbnew.ToMM
    nc = board.GetDesignSettings().m_NetSettings.GetEffectiveNetClass(net)
    w = T(nc.GetTrackWidth()); vd = T(nc.GetViaDiameter()); vdr = T(nc.GetViaDrill())
    clr = T(nc.GetClearance()) + 0.005
    if narrow:
        (w, _, vd, vdr), _ = NETCLASSES["Default"]
    items = src + dst
    xs = [T(i.GetPosition().x) for i in items]; ys = [T(i.GetPosition().y) for i in items]
    # grid aligned with a source pad's centre, so it can leave fine-pitch pads straight out
    anchor = next((i for i in src if i.Type() == pcbnew.PCB_PAD_T), src[0]).GetPosition()
    ax, ay = T(anchor.x), T(anchor.y)
    x0 = ax - GRID * round((ax - (min(xs) - 3)) / GRID)
    y0 = ay - GRID * round((ay - (min(ys) - 3)) / GRID)
    nx = int((max(xs) + 3 - x0) / GRID) + 1
    ny = int((max(ys) + 3 - y0) / GRID) + 1
    X, Y = np.meshgrid(x0 + GRID * np.arange(nx), y0 + GRID * np.arange(ny), indexing="ij")
    lid = [board.GetLayerID(l) for l in layers]
    # inside the board, away from the edge
    if DEBUG:
        print(f"  grid {nx}x{ny}", flush=True)
    inside_t = np.vectorize(lambda x, y: _inside(OUTLINE, x, y, w / 2 + RULES["edge_clearance"]))(X, Y)
    inside_v = np.vectorize(lambda x, y: _inside(OUTLINE, x, y, vd / 2 + RULES["edge_clearance"]))(X, Y)
    free = {L: inside_t.copy() for L in lid}
    vfree = inside_v.copy()
    soft = {L: np.full(inside_t.shape, -1, dtype=np.int32) for L in lid}   # rip: whose track is there
    vsoft = np.full(inside_t.shape, -1, dtype=np.int32)
    soft_nets = []
    xa, ya = x0 - 1, y0 - 1
    xb, yb = x0 + nx * GRID + 1, y0 + ny * GRID + 1
    for o in list(board.GetPads()) + list(board.GetTracks()):
        if o.GetNetname() == net:
            continue
        bb = o.GetBoundingBox()
        if T(bb.GetRight()) < xa or T(bb.GetLeft()) > xb or T(bb.GetBottom()) < ya or T(bb.GetTop()) > yb:
            continue
        g, ls = _geom(o)
        d = _dist(g, X, Y)
        on = o.GetNetname()
        if rip and o.Type() != pcbnew.PCB_PAD_T and on and on not in FANOUT_NETS and on not in keep:
            if on not in soft_nets:
                soft_nets.append(on)
            k = soft_nets.index(on)
            for L in lid:
                if L in ls:
                    m = (d < w / 2 + clr) & (soft[L] < 0)
                    soft[L][m] = k
            vsoft[(d < vd / 2 + clr) & (vsoft < 0)] = k
            continue
        for L in lid:
            if L in ls:
                free[L] &= d >= w / 2 + clr
        vfree &= d >= vd / 2 + clr
    for o in src + dst:   # no via inside a pad of the link's own ends
        if o.Type() == pcbnew.PCB_PAD_T:
            vfree &= _dist(_geom(o)[0], X, Y) > vd / 2
    # start and goal: grid points on each island's copper
    def cells(group):
        out = set()
        for it in group:
            g, ls = _geom(it)
            d = _dist(g, X, Y)
            ii, jj = np.nonzero(d <= 1e-6)
            for li, L in enumerate(lid):
                if L in ls:
                    out.update(zip(ii.tolist(), jj.tolist(), [li] * len(ii)))
        return out
    start, goal = cells(src), cells(dst)
    if DEBUG:
        def _name(it):
            it = it.Cast()
            return f"{it.GetParentFootprint().GetReference()}.{it.GetNumber()}" if it.Type() == pcbnew.PCB_PAD_T else it.GetClass()
        print("   src", [_name(i) for i in src][:6], "dst", [_name(i) for i in dst][:6])
        for li, L in enumerate(lid):
            print("   layer", layers[li], "free", round(float(free[L].mean()), 3), "start here", sum(1 for c in start if c[2] == li))
    if not start or not goal:
        return None if rip else []
    gpts = np.array([(a, b) for a, b, _ in goal])
    gmin, gmax = gpts.min(axis=0), gpts.max(axis=0)
    def h(i, j):   # distance to the goal's bounding box: never more than the real cost
        dx = max(gmin[0] - i, 0, i - gmax[0]); dy = max(gmin[1] - j, 0, j - gmax[1])
        return GRID * (dx * dx + dy * dy) ** 0.5
    ok = lambda i, j, li: bool(free[lid[li]][i, j])
    openq, came, cost = [], {}, {}
    for st in start:
        cost[st] = 0.0
        heapq.heappush(openq, (h(st[0], st[1]), st))
    steps = [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
             (1, 1, 1.414), (1, -1, 1.414), (-1, 1, 1.414), (-1, -1, 1.414)]
    end, n_exp, done = None, 0, set()
    while openq and n_exp < 600000:
        _, cur = heapq.heappop(openq)
        if cur in done:
            continue
        done.add(cur)
        n_exp += 1
        if cur in goal:
            end = cur
            break
        i, j, li = cur
        nbrs = []
        for di, dj, c in steps:
            a, b2 = i + di, j + dj
            if not (0 <= a < nx and 0 <= b2 < ny):
                continue
            if di and dj:   # diagonal: only past two free cells, so it never clips a corner
                if not all(sd in start or sd in goal or ok(*sd) for sd in ((i + di, j, li), (i, j + dj, li))):
                    continue
            nbrs.append(((a, b2, li), c * GRID))
        if vfree[i, j]:
            for lj in range(len(lid)):
                if lj != li:
                    nbrs.append(((i, j, lj), VIA_COST * GRID + (RIP_COST if vsoft[i, j] >= 0 else 0)))
        for nb, c in nbrs:
            if nb not in goal and nb not in start and not ok(*nb):
                continue
            if rip and soft[lid[nb[2]]][nb[0], nb[1]] >= 0:
                c += RIP_COST * GRID
            nc2 = cost[cur] + c
            if nc2 < cost.get(nb, 1e18):
                cost[nb] = nc2
                came[nb] = cur
                heapq.heappush(openq, (nc2 + h(nb[0], nb[1]), nb))
    if DEBUG:
        print(f"  {net}: start {len(start)} goal {len(goal)} expanded {n_exp} grid {nx}x{ny} found {end is not None}")
    if end is None:
        return None if rip else []
    path = [end]
    while path[-1] in came:
        path.append(came[path[-1]])
    path.reverse()
    if rip:
        crossed = {soft_nets[soft[lid[c[2]]][c[0], c[1]]] for c in path if soft[lid[c[2]]][c[0], c[1]] >= 0}
        crossed |= {soft_nets[vsoft[a[0], a[1]]] for a, b2 in zip(path, path[1:])
                    if a[2] != b2[2] and vsoft[a[0], a[1]] >= 0}
        return crossed
    added = []
    netinfo = board.FindNet(net)
    # straight runs -> segments; layer changes -> vias
    pts = lambda c: pcbnew.VECTOR2I(mm(x0 + c[0] * GRID), mm(y0 + c[1] * GRID))
    seg_start = path[0]
    for k in range(1, len(path)):
        a, b2 = path[k - 1], path[k]
        if a[2] != b2[2]:
            if (seg_start[0], seg_start[1]) != (a[0], a[1]):
                added.append(_track(board, netinfo, pts(seg_start), pts(a), lid[a[2]], w))
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pts(a)); v.SetWidth(mm(vd)); v.SetDrill(mm(vdr)); v.SetNet(netinfo)
            board.Add(v)
            added.append(v)
            seg_start = b2
            continue
        nxt = path[k + 1] if k + 1 < len(path) else None
        if nxt is None or nxt[2] != b2[2] or (nxt[0] - b2[0], nxt[1] - b2[1]) != (b2[0] - a[0], b2[1] - a[1]):
            added.append(_track(board, netinfo, pts(seg_start), pts(b2), lid[b2[2]], w))
            seg_start = b2
    return [a for a in added if a is not None]


def _track(board, net, a, b, layer, w):
    if a == b:
        return None
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(a); t.SetEnd(b); t.SetLayer(layer); t.SetWidth(mm(w)); t.SetNet(net)
    board.Add(t)
    return t


def _connect(board, net, layers):
    """Join the net's islands; the items added, or None if it couldn't."""
    added = []
    for _ in range(30):
        board.BuildConnectivity()
        groups = _islands(board, net)
        if len(groups) < 2:
            return added
        groups.sort(key=len)
        rest = [it for g in groups[1:] for it in g]
        got = _route_link(board, net, groups[0], rest, layers) or \
            _route_link(board, net, groups[0], rest, layers, narrow=True)
        if not got:
            for it in added:
                board.Remove(it)
            return None
        added += got
    return added


def _rip_and_route(board, net, layers, depth=3, keep=()):
    """Rip up the signal nets in the way, route net, then route them again
    (ripping up in turn if they need it, depth levels deep, never touching
    keep). Everything is undone if any of it fails. True if it worked."""
    board.BuildConnectivity()
    groups = sorted(_islands(board, net), key=len)
    if len(groups) < 2:
        return True
    keep = set(keep) | {net}
    crossed = _route_link(board, net, groups[0], [it for g in groups[1:] for it in g], layers, rip=True, keep=keep)
    if DEBUG:
        print(f"   rip {net} (depth {depth}, keep {sorted(keep)}): crosses {crossed}", flush=True)
    if not crossed:
        return False
    removed = [t for t in board.GetTracks() if t.GetNetname() in crossed]
    for t in removed:
        board.Remove(t)
    before = {n: {t.m_Uuid.AsString() for t in board.GetTracks() if t.GetNetname() == n}
              for n in [net] + sorted(crossed)}

    def undo():   # take out what this attempt added, put back what it ripped up
        for m, had in before.items():
            for t in [t for t in board.GetTracks() if t.GetNetname() == m and t.m_Uuid.AsString() not in had]:
                board.Remove(t)
        for t in removed:
            board.Add(t)
        board.BuildConnectivity()

    if _connect(board, net, layers) is None:
        if DEBUG:
            print(f"   rip {net}: still no route after ripping", flush=True)
        undo()
        return False
    for n in sorted(crossed):
        if _connect(board, n, layers) is None and not (
                depth > 1 and _rip_and_route(board, n, layers, depth - 1, keep | crossed)):
            undo()
            return False
    print(f"  rip-up: routed {net}, re-routed {' '.join(sorted(crossed))}")
    return True


def finish(board):
    """Route what's left, net by net, joining islands two at a time; if a link
    won't go, rip up the nets in its way and route them again."""
    layers = ["F.Cu", "In2.Cu", "B.Cu"]
    left = []
    for net in sorted({p.GetNetname() for p in board.GetPads() if p.GetNetname()}):
        if net in PLANES.values():
            continue
        if _connect(board, net, layers) is None:
            ok = False
            for _ in range(3):
                if _rip_and_route(board, net, layers):
                    ok = True
                    break
            if not ok:
                left.append(net)
    return left


def unrouted(board):
    """Connections still missing, with the planes filled (GND joins through In1)."""
    fill(board)
    return board.GetConnectivity().GetUnconnectedCount(True)


def fill(board):
    board.BuildListOfNets()
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()


FAB_LAYERS = "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts"


def fab(outdir):
    """Gerbers + drill (zipped), and JLCPCB's placement (CPL) file, with kicad-cli."""
    import csv, shutil, zipfile
    g = os.path.join(outdir, "gerbers")
    shutil.rmtree(g, ignore_errors=True)
    os.makedirs(g)
    run = lambda *a: subprocess.run(["kicad-cli", "pcb", "export", *a], check=True, capture_output=True)
    run("gerbers", "--layers", FAB_LAYERS, "--subtract-soldermask", "--no-x2", "--no-netlist",
        "-o", g + "/", PCB)
    run("drill", "--format", "excellon", "--drill-origin", "absolute", "--excellon-units", "mm",
        "--generate-map", "--map-format", "gerberx2", "-o", g + "/", PCB)
    with zipfile.ZipFile(os.path.join(outdir, f"{PROJECT}-gerbers.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(g)):
            z.write(os.path.join(g, f), f)
    pos = os.path.join(outdir, "pos.csv")
    run("pos", "--side", "both", "--format", "csv", "--units", "mm", "--exclude-dnp", "-o", pos, PCB)
    with open(pos) as fi, open(os.path.join(outdir, f"{PROJECT}-cpl-jlc.csv"), "w", newline="") as fo:
        w = csv.writer(fo)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in csv.DictReader(fi):
            if N.parts[r["Ref"]]["assembly"] != "jlc":
                continue
            w.writerow([r["Ref"], f'{float(r["PosX"]):.3f}mm', f'{float(r["PosY"]):.3f}mm',
                        "Top" if r["Side"] == "top" else "Bottom", f'{float(r["Rot"]):.0f}'])
    os.remove(pos)
    shutil.rmtree(g)


def build(fpdir):
    """The placed, unrouted board, with its planes, keep-outs and fanout vias."""
    board = pcbnew.NewBoard(PCB)
    board.SetCopperLayerCount(4)
    set_rules(board)
    outline(board)
    fps = add_parts(board, fpdir)
    place(fps)
    planes(board)
    screw_holes(board)
    hole_keepouts(board)
    rf_keepout(board)
    edge_keepout(board)
    crystal_keepout(board, fps)
    ep_vias(board, fps)
    fanout(board)
    silk(board)
    return board, fps


def autoroute(board, args, passes):
    """Freerouting in rounds, each from the last, then the finisher. Returns what's left."""
    left = None
    for rnd in range(args.rounds):
        route(board, args.freerouting, passes if rnd == 0 else max(10, passes // 2), args.workdir)
        n = unrouted(board)
        print(f"  round {rnd + 1}: {n} connections unrouted")
        if n == 0 or (left is not None and n >= left):
            break
        left = n
    if unrouted(board):
        stuck = finish(board)
        print(f"  finisher: {unrouted(board)} connections unrouted" + (f" ({' '.join(stuck)})" if stuck else ""))
    return unrouted(board)


# Freerouting is deterministic: the same board and pass count give the same
# result. When one pass count leaves something, the next may not.
PASS_COUNTS = (40, 30, 50, 35, 45)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kicad-fp", help="KiCad 9.0 footprint library checkout")
    ap.add_argument("--route", action="store_true", help="autoroute with Freerouting")
    ap.add_argument("--ses", help="import this Freerouting session instead of routing")
    ap.add_argument("--freerouting", default=os.environ.get("FREEROUTING_JAR", "freerouting-1.9.0.jar"))
    ap.add_argument("--passes", type=int, nargs="+", default=list(PASS_COUNTS),
                    help="Freerouting pass counts to try, in turn, until one routes everything")
    ap.add_argument("--rounds", type=int, default=4, help="routing rounds, each starting from the last")
    ap.add_argument("--workdir", default=os.path.join(HERE, "route" + N.SUFFIX))
    ap.add_argument("--fab", action="store_true", help="also write Gerbers, drill and the JLC CPL file to fab/")
    ap.add_argument("--variant", choices=N.VARIANTS, default=N.VARIANT, help="which board (default: hood)")
    args = ap.parse_args()
    fpdir = footprint_dir(args)

    board, fps = build(fpdir)
    left = 0
    if args.ses:
        if not pcbnew.ImportSpecctraSES(board, args.ses):
            sys.exit("SES import failed")
        if unrouted(board):
            finish(board)
        left = unrouted(board)
    elif args.route:
        os.makedirs(args.workdir, exist_ok=True)
        best = None
        for passes in args.passes:
            if best is not None:
                board, fps = build(fpdir)
            print(f"routing with {passes} passes")
            n = autoroute(board, args, passes)
            if best is None or n < best[0]:
                best = (n, board, passes)
            if n == 0:
                break
        left, board, passes = best
        print(f"kept: {passes} passes, {left} connections unrouted")
    pours(board)
    fill(board)
    pcbnew.SaveBoard(PCB, board)
    if args.fab:
        fab(os.path.join(HERE, "fab"))
    print(f"ok: {len(fps)} footprints, {len(N.nets)} nets -> {os.path.relpath(PCB)}"
          + (f"; {left} connections NOT routed" if left else ""))


if __name__ == "__main__":
    main()
