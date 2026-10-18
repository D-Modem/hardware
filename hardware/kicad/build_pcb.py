#!/usr/bin/env python3
# SPDX-License-Identifier: CERN-OHL-S-2.0
# Copyright (C) 2026 Neo-Desktop
"""Lay out the pico-serial PCB from the netlist script.

  hardware/kicad/build_pcb.py --kicad-fp <kicad-footprints 9.0 checkout> [--route]

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
import build_netlist as N  # noqa: E402  (builds the circuit on import)
import pcbnew  # noqa: E402

PROJECT = "pico-serial"
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
    "Plane": ((0.25, 0.15, 0.5, 0.25), ["GND"]),
    "Power": ((0.25, 0.15, 0.55, 0.3), ["+3V3", "+1V1"]),
    "VBUS": ((0.4, 0.15, 0.6, 0.3), ["VBUS", "VBUS_IN"]),      # 0.5 A
    "Pump": ((0.25, 0.15, 0.55, 0.3), ["CP*"]),                # the charge pumps' +-5.5 V
    "RS232": ((0.2, 0.15, 0.55, 0.3), ["RS_*", "DB25_FG"]),
    "USB": ((0.2, 0.15, 0.55, 0.3), ["USB_*"]),
}

# Layers: F.Cu signals; In1.Cu a solid GND plane (every signal's reference);
# In2.Cu signals, then a +3V3 pour in what's left; B.Cu signals. Every GND and
# +3V3 pad gets a via (fanout()); +3V3 is also routed, so it never depends on
# how the pour comes out.
PLANES = {"In1.Cu": "GND"}
POURS = [("In2.Cu", "+3V3"), ("F.Cu", "GND"), ("B.Cu", "GND")]   # after routing
FANOUT_NETS = ("GND", "+3V3")

# ---------------------------------------------------------------------------
# Placement: ref -> (x, y, rotation degrees, side). Bottom-side parts are
# flipped left-right, then rotated.
# ---------------------------------------------------------------------------
MCU = (-1.0, 17.0)    # RP2040 centre
CHIP_Y = 9.6          # transceivers' centre line (bottom)
CHIP_B_X, CHIP_A_X = -10.0, 8.5
PLACE = {
    # --- connectors
    "J2": (0.0, 1.99, 180, "F"),           # DB25: pin 1 at x = -16.62; 1-13 on top, 14-25 underneath
    "J1": (0.0, MOUTH - 3.65, 0, "F"),     # USB-C, mouth flush with the back edge
    # --- top: RP2040 (USB and flash pins face back), flash, clock, buttons
    "U1": (*MCU, 180, "F"),
    "U2": (9.3, 20.6, 0, "F"),
    "C18": (14.6, 18.7, 90, "F"),          # flash VCC
    "Y1": (2.8, 10.4, 0, "F"),
    "R8": (-0.5, 10.6, 90, "F"),           # XOUT series
    "C16": (-1.7, 10.6, 90, "F"),          # XIN load
    "C17": (5.5, 9.55, 90, "F"),           # XTAL_OUT load
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
    "R4": (-7.2, 19.3, 0, "F"),            # VBUS divider
    "R5": (-7.2, 18.2, 0, "F"),
    # --- bottom: transceivers, RS-232 pins toward the DB25, charge pumps at the pin-1 end
    "U6": (CHIP_B_X, CHIP_Y, 270, "B"),    # chip B, DTE
    "C26": (-16.9, 6.2, 0, "B"), "C28": (-16.9, 7.7, 0, "B"),       # C2 flying, V- (0603)
    "C25": (-16.9, 9.2, 0, "B"), "C27": (-16.9, 10.7, 0, "B"),      # C1 flying, V+ (0603, nearest pin 27)
    "C29": (-13.2, 15.4, 0, "B"), "C30": (-11.0, 15.4, 0, "B"),     # VCC
    "R13": (-9.0, 15.4, 0, "B"),           # B_FORCEOFF_N pull-down
    "U5": (CHIP_A_X, CHIP_Y, 270, "B"),    # chip A, DCE
    "C20": (1.6, 6.2, 0, "B"), "C22": (1.6, 7.7, 0, "B"),
    "C19": (1.6, 9.2, 0, "B"), "C21": (1.6, 10.7, 0, "B"),
    "C23": (5.4, 15.4, 0, "B"), "C24": (7.6, 15.4, 0, "B"),
    "R12": (15.0, 6.6, 90, "B"),           # A_SHDN_N pull-down
    "R14": (15.0, 8.8, 90, "B"),           # RS_FORCEON pull-down
    # --- bottom: RP2040 decoupling, ~1.8 mm beyond its pad ends: close, but
    # leaving a ring free for the pins' own escape vias
    "C4": (4.6, 20.0, 90, "B"), "C5": (4.6, 17.0, 90, "B"),            # IOVDD 1, 10
    "C6": (-0.8, 11.0, 0, "B"), "C14": (-2.9, 11.0, 0, "B"),           # IOVDD 22, DVDD 23
    "C7": (-6.4, 16.0, 90, "B"), "C8": (-6.4, 19.6, 90, "B"),          # IOVDD 33, 42
    # back edge: two staggered rows
    "C11": (-4.6, 22.9, 90, "B"), "C13": (-2.6, 22.9, 90, "B"), "C9": (-0.6, 22.9, 90, "B"),   # AVDD 43, VREG_VOUT 45, IOVDD 49
    "C12": (-3.6, 25.0, 90, "B"), "C10": (-1.6, 25.0, 90, "B"), "C15": (0.4, 25.0, 90, "B"),   # VREG_VIN 44, USB_VDD 48, DVDD 50
    # --- bottom, back of the tongue: USB shield
    "R3": (-2.2, 36.6, 0, "B"), "C1": (2.2, 36.6, 0, "B"),
}
# test pads: bottom, a 2.6 mm grid behind chip A (pogo-pin friendly)
for i, ref in enumerate(["TP1", "TP2", "TP3", "TP4", "TP5", "TP6", "TP7", "TP8", "TP9", "TP10"]):
    PLACE[ref] = (7.0 + 2.6 * (i % 5), 17.6 + 2.6 * (i // 5), 0, "B")
PLACE["TP11"] = (7.0, 22.8, 0, "B")
PLACE["TP12"] = (9.6, 22.8, 0, "B")
# optional LEDs (not fitted): bottom, behind chip B, LED then its resistor
for i, n in enumerate(range(2, 8)):
    x, y = (-16.4 if i < 3 else -10.6), 17.4 + 2.0 * (i % 3)
    PLACE[f"D{n}"] = (x, y, 0, "B")
    PLACE[f"R{n + 13}"] = (x + 2.7, y, 0, "B")


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


def add_parts(board, fpdir):
    nets = {}
    for name in sorted(N.nets):
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        nets[name] = ni
    pin_net = {(r, p): n for n, mem in N.nets.items() for r, p in mem}
    fps = {}
    for ref, p in N.parts.items():
        lib, name = p["footprint"].split(":")
        fp = pcbnew.FootprintLoad(os.path.join(fpdir, f"{lib}.pretty"), name)
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
            fp.SetDNP(True); fp.SetExcludedFromBOM(True); fp.SetExcludedFromPosFiles(True)
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
    (f"pico-serial rev {REVISION}", -9.8, 9.4, 1.0, "F", "c"),
    ("DCE / DTE auto", -9.8, 10.9, 0.8, "F", "c"),
    ("DTE: male-male changer", -9.8, 12.2, 0.8, "F", "c"),
    # the licence notice and Source Location, CERN-OHL-S-2.0 section 4 (LICENSING.md)
    ("github.com/Neo-Desktop/pico-serial", 0.0, 26.8, 0.8, "B", "c"),
    ("CERN-OHL-S-2.0", 0.0, 28.0, 0.8, "B", "c"),
]


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
        if net not in FANOUT_NETS or pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
            continue
        if fp.GetReference() == "U1" and (pad.GetNumber() == "57" or net == "+3V3"):
            continue   # exposed pad has its own vias; +3V3 pins are routed (vias here block the escape)
        layer = lay(pad)
        pc = pad.GetPosition()
        bb = pad.GetBoundingBox()
        hw, hh = pcbnew.ToMM(bb.GetWidth()) / 2, pcbnew.ToMM(bb.GetHeight()) / 2
        if fp.GetReference() == "J2":
            out = (0, 1)
        else:
            d = pc - fp.GetPosition()
            dx, dy = pcbnew.ToMM(d.x), pcbnew.ToMM(d.y)
            out = ((1 if dx > 0 else -1), 0) if abs(dx) >= abs(dy) else (0, (1 if dy > 0 else -1))
        dirs = [out, (out[1], out[0]), (-out[1], -out[0]), (out[0] + out[1], out[1] + out[0]),
                (out[0] - out[1], out[1] - out[0]), (-out[0], -out[1])]
        w = min(0.25, 2 * min(hw, hh))
        done = False
        for ddx, ddy in dirs:
            norm = (ddx * ddx + ddy * ddy) ** 0.5
            ux, uy = ddx / norm, ddy / norm
            ext = abs(ux) * hw + abs(uy) * hh
            for k in range(20):
                dist = ext + FAN_CLEAR + vr + 0.1 * k
                vx, vy = pcbnew.ToMM(pc.x) + ux * dist, pcbnew.ToMM(pc.y) + uy * dist
                if not _inside(OUTLINE, vx, vy, vr + RULES["edge_clearance"]):
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kicad-fp", help="KiCad 9.0 footprint library checkout")
    ap.add_argument("--route", action="store_true", help="autoroute with Freerouting")
    ap.add_argument("--ses", help="import this Freerouting session instead of routing")
    ap.add_argument("--freerouting", default=os.environ.get("FREEROUTING_JAR", "freerouting-1.9.0.jar"))
    ap.add_argument("--passes", type=int, default=40)
    ap.add_argument("--rounds", type=int, default=4, help="routing rounds, each starting from the last")
    ap.add_argument("--workdir", default=os.path.join(HERE, "route"))
    ap.add_argument("--fab", action="store_true", help="also write Gerbers, drill and the JLC CPL file to fab/")
    args = ap.parse_args()
    fpdir = footprint_dir(args)

    board = pcbnew.NewBoard(PCB)
    board.SetCopperLayerCount(4)
    set_rules(board)
    outline(board)
    fps = add_parts(board, fpdir)
    place(fps)
    planes(board)
    hole_keepouts(board)
    ep_vias(board, fps)
    fanout(board)
    silk(board)
    if args.ses:
        if not pcbnew.ImportSpecctraSES(board, args.ses):
            sys.exit("SES import failed")
    elif args.route:
        os.makedirs(args.workdir, exist_ok=True)
        # route, then route again from that result until nothing is left or a round doesn't help
        left = None
        for rnd in range(args.rounds):
            route(board, args.freerouting, args.passes if rnd == 0 else max(10, args.passes // 2), args.workdir)
            n = unrouted(board)
            print(f"round {rnd + 1}: {n} connections unrouted")
            if n == 0 or (left is not None and n >= left):
                break
            left = n
    pours(board)
    fill(board)
    pcbnew.SaveBoard(PCB, board)
    if args.fab:
        fab(os.path.join(HERE, "fab"))
    print(f"ok: {len(fps)} footprints, {len(N.nets)} nets -> {os.path.relpath(PCB)}")


if __name__ == "__main__":
    main()
