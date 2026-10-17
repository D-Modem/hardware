#!/usr/bin/env python3
"""pico-serial schematic, generated from ../netlist/build_netlist.py.

Writes, next to this script:
  pico-serial.kicad_sch   KiCad 9 schematic (opens in KiCad 9 and 10)
  pico-serial.kicad_pro   minimal project file
  pico-serial.kicad_sym   symbols KiCad doesn't ship (SP3238E, MAX3243E)
  sym-lib-table           points KiCad at pico-serial.kicad_sym
  preview.svg             a rough drawing of the sheet, for a quick look

Every pin gets a short wire ending in a net label or a power symbol, and
parts are grouped into blocks by function. Unused pins get no-connect
flags.

Checks before writing (exit 1 on any error):
  - the connectivity read back from the drawing matches the netlist exactly
    (same nets, same pins, nothing joined by accident)
  - no two labels, symbols or texts overlap

Symbols come from KiCad's official library, 9.0 branch. They are cached in
lib/symbol-cache.sexp so this runs offline; refresh the cache with
  --kicad-sym9 <kicad-symbols checkout at tag 9.0.x>
"""
import argparse, datetime, math, os, sys, uuid
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "netlist"))
sys.path.insert(0, HERE)
import sexpr  # noqa: E402
from sexpr import Sym, find, first  # noqa: E402

_argv, sys.argv = sys.argv, sys.argv[:1]
import build_netlist as N  # noqa: E402  (builds the circuit on import)
sys.argv = _argv

PROJECT = "pico-serial"
NS = uuid.UUID("0b4c1d1e-5e7a-4c2b-8c51-9f0a7d3e6b21")
GRID = 1.27
STUB = 2.54
FONT = 1.27
CHAR_W = 1.27 * 0.82      # average stroke-font character width at 1.27 mm
POWER = {"GND": "power:GND", "+3V3": "power:+3V3", "+1V1": "power:+1V1", "VBUS": "power:VBUS"}
BODY_UP = {"power:+3V3", "power:+1V1", "power:VBUS", "power:PWR_FLAG"}   # graphic above the pin
BLOCKS = [  # key, title, max width (mm)
    ("usb", "USB-C, protection and 3.3 V supply", 195),
    ("mcu", "RP2040 and its decoupling", 165),
    ("clock", "Crystal, QSPI flash, BOOTSEL and RESET", 130),
    ("rsa", "Chip A: SP3238E, modem side (DCE)", 120),
    ("rsb", "Chip B: MAX3243E, computer side (DTE)", 120),
    ("db25", "DB25 female (hand-fitted)", 45),
    ("led", "Status LEDs (optional, not fitted)", 118),
    ("tp", "Test pads", 100),
    ("flags", "Power flags for ERC", 60),
]
# rows of columns; a column stacks its blocks top to bottom
SHEET_ROWS = [[["usb"], ["clock"], ["db25"], ["led"]], [["mcu"], ["rsa"], ["rsb"], ["tp", "flags"]]]
FLAGS_SIZE = (35.56, 15.24)


def uid(*parts):
    return str(uuid.uuid5(NS, "/".join(map(str, parts))))


def snap(v, g=GRID):
    return round(round(v / g) * g, 4)


def text_w(s, size=FONT):
    return len(s) * size * 0.82 + 0.4


# ===========================================================================
# Symbols
# ===========================================================================
LIB_OF = {}


def lib_id(ref):
    p = N.parts[ref]
    pins = p["pins"]
    if pins is N.SP3238E:
        return "pico-serial:SP3238E"
    if pins is N.MAX3243E:
        return "pico-serial:MAX3243E"
    if pins is N.TWO:
        return {"R": "Device:R", "C": "Device:C", "F": "Device:Polyfuse"}[ref.rstrip("0123456789")]
    if pins is N.DIODE:
        return "Device:LED" if p["value"].startswith("LED") else "Device:D_Zener"
    lib, name = N.SYMBOLS[id(pins)]
    return f"{lib}:{name}"


def custom_symbol(name, left, right, top, bottom, desc, fp, ds):
    """A plain box: logic pins left, RS-232 and charge pump right, VCC top, GND bottom."""
    pins_tab = N.SP3238E if name == "SP3238E" else N.MAX3243E
    by_name = {}
    for num, (pname, ty) in pins_tab.items():
        by_name.setdefault(pname, []).append((num, ty))
    rows = max(len(left), len(right))
    h = (rows + 1) * 2.54
    top_y, w = snap(h / 2, 2.54), 12.7
    body = [Sym("rectangle"), [Sym("start"), -w, top_y], [Sym("end"), w, -top_y],
            [Sym("stroke"), [Sym("width"), 0.254], [Sym("type"), Sym("default")]],
            [Sym("fill"), [Sym("type"), Sym("background")]]]
    pins = []

    def pin(pname, x, y, ang):
        num, ty = by_name[pname].pop(0)
        pins.append([Sym("pin"), Sym(ty), Sym("line"), [Sym("at"), x, y, ang], [Sym("length"), 3.81],
                     [Sym("name"), pname, [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]]]],
                     [Sym("number"), num, [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]]]]])
    for i, pn in enumerate(left):
        if pn:
            pin(pn, -w - 3.81, top_y - 2.54 * (i + 1), 0)
    for i, pn in enumerate(right):
        if pn:
            pin(pn, w + 3.81, top_y - 2.54 * (i + 1), 180)
    for i, pn in enumerate(top):
        pin(pn, -2.54 * (len(top) - 1) / 2 + 2.54 * i, top_y + 3.81, 270)
    for i, pn in enumerate(bottom):
        pin(pn, -2.54 * (len(bottom) - 1) / 2 + 2.54 * i, -top_y - 3.81, 90)
    assert not any(by_name.values()), f"{name}: pins not placed: {by_name}"

    def prop(k, v, x, y, hide=False):
        eff = [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]]]
        if hide:
            eff.append([Sym("hide"), Sym("yes")])
        return [Sym("property"), k, v, [Sym("at"), x, y, 0], eff]
    return [Sym("symbol"), name, [Sym("exclude_from_sim"), Sym("no")], [Sym("in_bom"), Sym("yes")],
            [Sym("on_board"), Sym("yes")],
            prop("Reference", "U", -w, top_y + 1.27), prop("Value", name, -w, -top_y - 1.27),
            prop("Footprint", fp, 0, 0, True), prop("Datasheet", ds, 0, 0, True),
            prop("Description", desc, 0, 0, True),
            [Sym("symbol"), f"{name}_0_1", body], [Sym("symbol"), f"{name}_1_1", *pins],
            [Sym("embedded_fonts"), Sym("no")]]


CUSTOM = {
    "SP3238E": custom_symbol(
        "SP3238E",
        left=["T1IN", "T2IN", "T3IN", "T4IN", "T5IN", None, "R1OUT", "R2OUT", "R3OUT", "R1OUTB", None,
              "ONLINE", "~{SHUTDOWN}", "STATUS"],
        right=["T1OUT", "T2OUT", "T3OUT", "T4OUT", "T5OUT", None, "R1IN", "R2IN", "R3IN", None,
               "C1+", "C1-", "C2+", "C2-", "V+", "V-"],
        top=["VCC"], bottom=["GND"],
        desc="3 V to 5.5 V RS-232 transceiver, 5 drivers / 3 receivers, AUTO ON-LINE, +-15 kV ESD",
        fp="Package_SO:SSOP-28_5.3x10.2mm_P0.65mm", ds="https://www.maxlinear.com/ds/sp3238e.pdf"),
    "MAX3243E": custom_symbol(
        "MAX3243E",
        left=["DIN1", "DIN2", "DIN3", None, "ROUT1", "ROUT2", "ROUT3", "ROUT4", "ROUT5", "ROUT2B", None,
              "~{FORCEOFF}", "FORCEON", "INVALID"],
        right=["DOUT1", "DOUT2", "DOUT3", None, "RIN1", "RIN2", "RIN3", "RIN4", "RIN5", None,
               "C1+", "C1-", "C2+", "C2-", "V+", "V-"],
        top=["VCC"], bottom=["GND"],
        desc="3 V to 5.5 V RS-232 transceiver, 3 drivers / 5 receivers, auto-powerdown, +-15 kV ESD",
        fp="Package_SO:SSOP-28_5.3x10.2mm_P0.65mm", ds="https://www.ti.com/lit/ds/symlink/max3243e.pdf"),
}

CACHE = os.path.join(HERE, "lib", "symbol-cache.sexp")


def needed_lib_ids():
    ids = {lib_id(r) for r in N.parts} | set(POWER.values()) | {"power:PWR_FLAG"}
    return sorted(i for i in ids if not i.startswith("pico-serial:"))


def load_from_kicad(libdir):
    out = {}
    files = {}
    for lid in needed_lib_ids():
        lib, name = lid.split(":")
        if lib not in files:
            files[lib] = {first_atom(s): s for s in find(sexpr.parse(open(os.path.join(libdir, f"{lib}.kicad_sym")).read()), "symbol")}
        out[lid] = flatten(files[lib], name, lid)
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    with open(CACHE, "w") as f:
        f.write(";; Symbols used by pico-serial, from KiCad's official library (kicad-symbols 9.0 branch,\n")
        f.write(";; CC-BY-SA 4.0 with the KiCad libraries exception). Generated by build_schematic.py.\n")
        f.write(sexpr.dump([Sym("cache")] + [out[k] for k in sorted(out)]) + "\n")
    return out


def first_atom(node):
    return node[1]


def flatten(symbols, name, lid):
    """A library symbol with `extends` resolved, renamed to lib_id, as eeschema embeds it."""
    s = symbols[name]
    ext = first(s, "extends")
    if ext is None:
        node = [Sym("symbol"), lid] + s[2:]
        return node
    parent = flatten(symbols, ext[1], lid)
    child_props = {p[1]: p for p in find(s, "property")}
    out = [Sym("symbol"), lid]
    for item in parent[2:]:
        if isinstance(item, list) and item[0] == "property" and item[1] in child_props:
            out.append(child_props.pop(item[1]))
        elif isinstance(item, list) and item[0] == "symbol":
            sub = list(item)
            sub[1] = name + sub[1][len(ext[1]):]
            out.append(sub)
        else:
            out.append(item)
    # any property only the child has goes before the units
    extra = list(child_props.values())
    k = next(i for i, x in enumerate(out) if isinstance(x, list) and x[0] == "symbol")
    out[k:k] = extra
    for i, x in enumerate(out):  # child's own flags win
        if isinstance(x, list) and x[0] in ("in_bom", "on_board", "exclude_from_sim", "power"):
            c = first(s, x[0])
            if c is not None:
                out[i] = c
    return out


def load_symbols(libdir):
    syms = load_from_kicad(libdir) if libdir else {
        n[1]: n for n in find(sexpr.parse(open(CACHE).read()), "symbol")}
    for name, node in CUSTOM.items():
        syms[f"pico-serial:{name}"] = [Sym("symbol"), f"pico-serial:{name}"] + node[2:]
    missing = [i for i in needed_lib_ids() if i not in syms]
    if missing:
        raise SystemExit(f"symbols missing from cache, run with --kicad-sym9: {missing}")
    return syms


# ---------------------------------------------------------------------------
class Geo:
    """Graphics and pins of one symbol, in library coordinates (y up)."""

    def __init__(self, node):
        self.node = node
        self.pins, self.shapes = [], []
        name = node[1].split(":")[1]
        for sub in find(node, "symbol"):
            tag = sub[1][len(name) + 1:].split("_")
            if len(tag) == 2 and tag[1] == "2":      # De Morgan body style: skip
                continue
            for g in sub[2:]:
                if not isinstance(g, list):
                    continue
                k = g[0]
                if k == "pin":
                    at = first(g, "at")
                    hide = any(x == "hide" for x in g) or (first(g, "hide") or [None, "no"])[1] == "yes"
                    self.pins.append(dict(
                        type=str(g[1]), x=float(at[1]), y=float(at[2]), ang=float(at[3]) if len(at) > 3 else 0,
                        length=float(first(g, "length")[1]), name=first(g, "name")[1],
                        number=first(g, "number")[1], hide=hide))
                elif k in ("rectangle", "polyline", "circle", "arc", "bezier"):
                    self.shapes.append(g)
        self.hide_names = self._hidden("pin_names")
        self.hide_numbers = self._hidden("pin_numbers")
        off = first(first(node, "pin_names") or [], "offset")
        self.name_offset = float(off[1]) if off else 0.508
        self.power = first(node, "power") is not None

    def _hidden(self, key):
        n = first(self.node, key)
        if n is None:
            return False
        return any(x == "hide" for x in n) or (first(n, "hide") or [None, "no"])[1] == "yes"

    def shape_points(self):
        pins, self.pins = self.pins, []
        try:
            yield from self.points()
        finally:
            self.pins = pins

    def points(self):
        for g in self.shapes:
            k = g[0]
            if k == "rectangle":
                for key in ("start", "end"):
                    a = first(g, key)
                    yield float(a[1]), float(a[2])
            elif k in ("polyline", "bezier"):
                for xy in find(first(g, "pts"), "xy"):
                    yield float(xy[1]), float(xy[2])
            elif k == "circle":
                c, r = first(g, "center"), float(first(g, "radius")[1])
                yield float(c[1]) - r, float(c[2]) - r
                yield float(c[1]) + r, float(c[2]) + r
            elif k == "arc":
                for key in ("start", "mid", "end"):
                    a = first(g, key)
                    yield float(a[1]), float(a[2])
        for p in self.pins:
            yield p["x"], p["y"]
            dx, dy = DIRS[p["ang"]]
            yield p["x"] + dx * p["length"], p["y"] + dy * p["length"]


DIRS = {0: (1, 0), 90: (0, 1), 180: (-1, 0), 270: (0, -1)}


def xf(x, y, ox, oy, rot=0):
    """library point -> sheet point for a symbol at (ox, oy) rotated rot degrees CCW."""
    r = math.radians(rot)
    xr, yr = x * math.cos(r) - y * math.sin(r), x * math.sin(r) + y * math.cos(r)
    return round(ox + xr, 4), round(oy - yr, 4)


def out_dir(pin_ang, rot=0):
    """Sheet-space unit vector pointing away from the body at a pin's end."""
    dx, dy = DIRS[pin_ang]
    r = math.radians(rot)
    ox, oy = -(dx * math.cos(r) - dy * math.sin(r)), -(dx * math.sin(r) + dy * math.cos(r))
    return round(ox), round(-oy)


# ===========================================================================
# Placement: a "cell" is one symbol with its stubs, labels, power symbols
# and property texts, built at the origin and then moved into place.
# ===========================================================================
class Box:
    def __init__(self, x0, y0, x1, y1, owner, kind):
        self.x0, self.y0, self.x1, self.y1 = min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)
        self.owner, self.kind = owner, kind

    def moved(self, dx, dy):
        return Box(self.x0 + dx, self.y0 + dy, self.x1 + dx, self.y1 + dy, self.owner, self.kind)

    def hits(self, o, gap=0.0):
        return not (self.x1 + gap <= o.x0 or o.x1 + gap <= self.x0 or self.y1 + gap <= o.y0 or o.y1 + gap <= self.y0)


NET_OF = {}
for _net, _members in N.nets.items():
    for _m in _members:
        NET_OF[_m] = _net


class Cell:
    def __init__(self, ref, syms):
        self.ref = ref
        self.lid = lib_id(ref)
        self.geo = Geo(syms[self.lid])
        self.wires, self.labels, self.powers, self.ncs = [], [], [], []
        self.boxes = []
        self.props = {}
        part = N.parts[ref]
        pts = list(self.geo.points())
        xs = [x for x, _ in pts]
        ys = [-y for _, y in pts]
        bx0, bx1, by0, by1 = min(xs), max(xs), min(ys), max(ys)
        self.boxes.append(Box(bx0, by0, bx1, by1, ref, "symbol"))
        shp = [p for p in Geo.shape_points(self.geo)] or pts
        self.body = (min(x for x, _ in shp), min(-y for _, y in shp), max(x for x, _ in shp), max(-y for _, y in shp))
        self.vertical_only = all(DIRS[p["ang"]][0] == 0 for p in self.geo.pins)
        # one stub per distinct pin end (stacked hidden pins share it)
        ends = defaultdict(list)
        for p in self.geo.pins:
            ends[(p["x"], p["y"])].append(p)
        for (x, y), plist in ends.items():
            nets = {NET_OF.get((ref, p["number"])) for p in plist}
            ncs = {(ref, p["number"]) in N.noconnect for p in plist}
            if len(nets) > 1 or (True in ncs and nets != {None}):
                raise SystemExit(f"{ref}: stacked pins {[p['number'] for p in plist]} disagree: {nets}")
            sx, sy = xf(x, y, 0, 0)
            net = nets.pop()
            if net is None:
                self.ncs.append((sx, sy))
                continue
            vis = [p for p in plist if not p["hide"]] or plist
            ux, uy = out_dir(vis[0]["ang"])
            ex, ey = snap(sx + ux * STUB), snap(sy + uy * STUB)
            self.wires.append(((sx, sy), (ex, ey)))
            if net in POWER:
                self.add_power(POWER[net], net, ex, ey, ux, uy)
            else:
                self.add_label(net, ex, ey, ux, uy)
        self.place_props(part)

    def add_label(self, net, x, y, ux, uy):
        ang = {(1, 0): 0, (0, -1): 90, (-1, 0): 180, (0, 1): 270}[(ux, uy)]
        w = text_w(net)
        self.labels.append((net, x, y, ang))
        if ux:
            self.boxes.append(Box(x, y - 1.55, x + ux * w, y - 0.2, self.ref, "label " + net))
        else:
            self.boxes.append(Box(x - 1.55, y, x - 0.2, y + uy * w, self.ref, "label " + net))

    def add_power(self, lid, value, x, y, ux, uy):
        up = lid in BODY_UP
        # rotation (CCW) that turns the symbol's body toward (ux, uy)
        rot = ({(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270} if up else
               {(0, 1): 0, (1, 0): 90, (0, -1): 180, (-1, 0): 270})[(ux, uy)]
        w = text_w(value)
        name = "power " + value
        if uy:   # text turned vertical, so neighbours 2.54 mm apart don't collide
            ty = y + uy * 3.6
            vat = (x, ty, "left" if uy < 0 else "right", 90)
            self.boxes.append(Box(x - 1.2, y + uy * 0.2, x + 1.2, y + uy * 2.8, self.ref, name))
            self.boxes.append(Box(x - 0.75, ty, x + 0.75, ty + uy * w, self.ref, name + " text"))
        else:
            tx = x + ux * 3.3
            vat = (tx, y, "left" if ux > 0 else "right", 0)
            self.boxes.append(Box(x + ux * 0.2, y - 1.0, x + ux * 2.8, y + 1.0, self.ref, name))
            self.boxes.append(Box(tx, y - 0.7, tx + ux * w, y + 0.7, self.ref, name + " text"))
        self.powers.append((lid, value, x, y, rot, vat))

    def place_props(self, part):
        """Reference and value where nothing else is: beside a vertical two-pin part,
        otherwise above and below everything the part has drawn."""
        ref, val = self.ref, part["value"] + (" (DNP)" if part["dnp"] else "")
        self.value_text = val
        x0, y0, x1, y1 = self.body
        if self.vertical_only and (x1 - x0) < 8:
            x = x1 + 1.27
            self.props["Reference"] = (snap(x), -0.635, "left")
            self.props["Value"] = (snap(x), 1.905, "left")
            for t, (px, py, _) in self.props.items():
                self.boxes.append(Box(px, py - 0.7, px + text_w(ref if t == "Reference" else val), py + 0.7, ref, t))
            return
        top = min(b.y0 for b in self.boxes)
        bot = max(b.y1 for b in self.boxes)
        wide = (x1 - x0) >= 8
        def put(t, s, y):
            if wide:
                px, j = x0, "left"
                bx = (px, px + text_w(s))
            else:
                px, j = (x0 + x1) / 2, "center"
                bx = (px - text_w(s) / 2, px + text_w(s) / 2)
            self.props[t] = (snap(px), snap(y), j)
            self.boxes.append(Box(bx[0], snap(y) - 0.7, bx[1], snap(y) + 0.7, ref, t))
        put("Reference", ref, top - 1.27)
        put("Value", val, bot + 1.27)

    def bbox(self):
        return (min(b.x0 for b in self.boxes), min(b.y0 for b in self.boxes),
                max(b.x1 for b in self.boxes), max(b.y1 for b in self.boxes))


def order_key(ref):
    p = N.parts[ref]
    kind = 0 if ref[0] in "UJ" else 1 if ref.startswith(("Y", "SW", "F", "D")) and not p["dnp"] else 2
    return (kind, N.natural(ref))


def layout(syms):
    cells = {r: Cell(r, syms) for r in N.parts}
    placed, titles = {}, []
    block_size = {}
    block_items = {}
    for key, title, maxw in BLOCKS:
        refs = sorted((r for r in N.parts if N.parts[r]["block"] == key), key=order_key)
        x = y = 0.0
        row_h = 0.0
        items = []
        left = 0.0
        if refs and refs[0][0] in "UJ":   # the block's chip on the left, the rest in a grid beside it
            r = refs.pop(0)
            x0, y0, x1, y1 = cells[r].bbox()
            items.append((r, snap(-x0 + 2.54, 2.54), snap(-y0 + 2.54, 2.54)))
            left = items[0][1] + x1 + 5.08
            x = left
        for r in refs:
            c = cells[r]
            x0, y0, x1, y1 = c.bbox()
            w, h = x1 - x0, y1 - y0
            if x > left and x + w > maxw:
                x, y, row_h = left, y + row_h + 3.81, 0.0
            ox, oy = snap(x - x0, 2.54), snap(y - y0, 2.54)
            if ox + x0 < x - 1e-6:
                ox += 2.54
            if oy + y0 < y - 1e-6:
                oy += 2.54
            items.append((r, ox, oy))
            x = ox + x1 + 2.54
            row_h = max(row_h, oy + y1 - y)
        block_items[key] = items
        bw = max((ox + cells[r].bbox()[2] for r, ox, oy in items), default=0)
        bh = max((oy + cells[r].bbox()[3] for r, ox, oy in items), default=0)
        block_size[key] = (bw, bh)
    block_size["flags"] = FLAGS_SIZE
    # blocks onto the sheet, row by row
    margin, title_h, gap = 17.78, 7.62, 10.16
    y = margin
    sheet_w = 0
    origins = {}
    names = dict((k, t) for k, t, _ in BLOCKS)
    for row in SHEET_ROWS:
        x = margin
        rh = 0
        for column in row:
            cy, cw = y, 0
            for key in column:
                bw, bh = block_size[key]
                bx, by = snap(x, 2.54), snap(cy + title_h, 2.54)
                origins[key] = (bx, by)
                titles.append((names[key], bx, by - 3.81))
                for r, ox, oy in block_items[key]:
                    placed[r] = (snap(bx + ox, 2.54), snap(by + oy, 2.54))
                cy = by + bh + gap
                cw = max(cw, bw)
            x = snap(x, 2.54) + cw + gap
            rh = max(rh, cy - y - gap)
        sheet_w = max(sheet_w, x)
        y += rh + gap
    return cells, placed, titles, (sheet_w + margin, y + margin), origins


PAPER = [("A3", 420, 297), ("A2", 594, 420), ("A1", 841, 594), ("A0", 1189, 841)]


def pick_paper(w, h):
    for name, pw, ph in PAPER:
        if w <= pw - 10 and h <= ph - 10:
            return name, pw, ph
    raise SystemExit(f"drawing {w:.0f} x {h:.0f} mm doesn't fit A0")


# ===========================================================================
# Writing the schematic
# ===========================================================================
def eff(size=FONT, justify=None, hide=False, bold=False):
    font = [Sym("font"), [Sym("size"), size, size]]
    if bold:
        font.append([Sym("bold"), Sym("yes")])
    e = [Sym("effects"), font]
    if justify:
        e.append([Sym("justify")] + [Sym(j) for j in justify.split()])
    if hide:
        e.append([Sym("hide"), Sym("yes")])
    return e


def prop(k, v, x, y, justify=None, hide=False, ang=0):
    return [Sym("property"), k, v, [Sym("at"), x, y, ang], eff(justify=justify, hide=hide)]


def build(syms):
    cells, placed, titles, (w, h), origins = layout(syms)
    paper, pw, ph = pick_paper(w, h)
    root = uid("root")
    items = []
    pwr_n = [0]

    def instance(lid, ref, x, y, rot, props, pins, dnp=False, in_bom=True, on_board=True):
        node = [Sym("symbol"), [Sym("lib_id"), lid], [Sym("at"), x, y, rot], [Sym("unit"), 1],
                [Sym("exclude_from_sim"), Sym("no")], [Sym("in_bom"), Sym("yes" if in_bom else "no")],
                [Sym("on_board"), Sym("yes" if on_board else "no")], [Sym("dnp"), Sym("yes" if dnp else "no")],
                [Sym("uuid"), uid("sym", ref)]]
        node += props
        node += [[Sym("pin"), n, [Sym("uuid"), uid("pin", ref, n)]] for n in pins]
        node.append([Sym("instances"), [Sym("project"), PROJECT, [Sym("path"), f"/{root}",
                                                                   [Sym("reference"), ref], [Sym("unit"), 1]]]])
        return node

    def power_symbol(lid, value, x, y, rot, vat):
        pwr_n[0] += 1
        ref = f"#PWR{pwr_n[0]:03d}"
        vx, vy, j, va = vat
        return instance(lid, ref, x, y, rot,
                        [prop("Reference", ref, x, y, hide=True),
                         prop("Value", value, vx, vy, justify=None if j == "center" else j, ang=va),
                         prop("Footprint", "", x, y, hide=True), prop("Datasheet", "", x, y, hide=True),
                         prop("Description", "", x, y, hide=True)], ["1"])

    world = []   # for checks: (kind, data)
    boxes = []
    for ref in sorted(N.parts, key=N.natural):
        c, (X, Y) = cells[ref], placed[ref]
        part = N.parts[ref]
        props = []
        for k in ("Reference", "Value"):
            px, py, j = c.props[k]
            props.append(prop(k, ref if k == "Reference" else part["value"], X + px, Y + py,
                              justify=None if j == "center" else j))
        props.append(prop("Footprint", part["footprint"], X, Y, hide=True))
        lsym = syms[c.lid]
        ds = next((p[2] for p in find(lsym, "property") if p[1] == "Datasheet"), "")
        props.append(prop("Datasheet", ds, X, Y, hide=True))
        desc = part["desc"] or next((p[2] for p in find(lsym, "property") if p[1] == "Description"), "")
        props.append(prop("Description", desc, X, Y, hide=True))
        if part["lcsc"]:
            props.append(prop("LCSC", part["lcsc"], X, Y, hide=True))
        if part["note"]:
            props.append(prop("Note", part["note"], X, Y, hide=True))
        pins = sorted({p["number"] for p in c.geo.pins}, key=N.natural)
        items.append(instance(c.lid, ref, X, Y, 0, props, pins, dnp=part["dnp"],
                              in_bom=part["assembly"] != "none"))
        for p in c.geo.pins:
            world.append(("pin", (ref, p["number"]), xf(p["x"], p["y"], X, Y)))
        for (a, b) in c.wires:
            pa, pb = (a[0] + X, a[1] + Y), (b[0] + X, b[1] + Y)
            items.append([Sym("wire"), [Sym("pts"), [Sym("xy"), *pa], [Sym("xy"), *pb]],
                          [Sym("stroke"), [Sym("width"), 0], [Sym("type"), Sym("default")]],
                          [Sym("uuid"), uid("wire", ref, *pa)]])
            world.append(("wire", None, (pa, pb)))
        for net, x, y, ang in c.labels:
            j = "left bottom" if ang in (0, 90) else "right bottom"
            items.append([Sym("label"), net, [Sym("at"), x + X, y + Y, ang], eff(justify=j),
                          [Sym("uuid"), uid("label", ref, x, y)]])
            world.append(("label", net, (x + X, y + Y)))
        for lid, value, x, y, rot, (vx, vy, j, va) in c.powers:
            items.append(power_symbol(lid, value, x + X, y + Y, rot, (vx + X, vy + Y, j, va)))
            world.append(("power", value, (x + X, y + Y)))
        for x, y in c.ncs:
            items.append([Sym("no_connect"), [Sym("at"), x + X, y + Y], [Sym("uuid"), uid("nc", ref, x, y)]])
            world.append(("nc", None, (x + X, y + Y)))
        boxes += [b.moved(X, Y) for b in c.boxes]

    # power flags: GND and VBUS have no power output pin on them
    fx, fy = snap(origins["flags"][0] + 2.54, 2.54), snap(origins["flags"][1] + 7.62, 2.54)
    flags = [("GND", "power:GND", fx, fy), ("VBUS", "power:VBUS", fx + 15.24, fy)]
    for i, (net, lid, x, y) in enumerate(flags, 1):
        up = lid in BODY_UP
        items.append(power_symbol(lid, net, x, y, 0, (x, y + (-5.08 if up else 5.08), "center", 0)))
        world.append(("power", net, (x, y)))
        fxp = x + 5.08
        items.append([Sym("wire"), [Sym("pts"), [Sym("xy"), x, y], [Sym("xy"), fxp, y]],
                      [Sym("stroke"), [Sym("width"), 0], [Sym("type"), Sym("default")]], [Sym("uuid"), uid("flagwire", net)]])
        world.append(("wire", None, ((x, y), (fxp, y))))
        ref = f"#FLG{i:02d}"
        items.append(instance("power:PWR_FLAG", ref, fxp, y, 0,
                              [prop("Reference", ref, fxp, y, hide=True),
                               prop("Value", "PWR_FLAG", fxp, y - 3.81, justify="left"),
                               prop("Footprint", "", fxp, y, hide=True), prop("Datasheet", "", fxp, y, hide=True),
                               prop("Description", "", fxp, y, hide=True)], ["1"]))
        world.append(("flag", net, (fxp, y)))
    items.append(text_node("GND and VBUS have no power-output pin", fx - 2.54, fy + 10.16, 1.27))

    for t, x, y in titles:
        items.append(text_node(t, x, y, 2.0, bold=True))
        boxes.append(Box(x, y - 2.2, x + text_w(t, 2.0) * 1.05, y + 0.4, "title", "title " + t))

    note = ("Generated by hardware/schematic/build_schematic.py from hardware/netlist/build_netlist.py. "
            "Edit the netlist script and regenerate; hand edits here are lost.")
    items.append(text_node(note, 20.32, ph - 12.7, 1.27))

    sch = [Sym("kicad_sch"), [Sym("version"), Sym("20250114")], [Sym("generator"), "eeschema"],
           [Sym("generator_version"), "9.0"], [Sym("uuid"), root], [Sym("paper"), paper],
           [Sym("title_block"), [Sym("title"), "pico-serial: USB-C to DB25 RS-232 (DCE/DTE)"],
            [Sym("date"), datetime.date.today().isoformat()], [Sym("rev"), "0.3"],
            [Sym("comment"), 1, "RP2040, SP3238E (DCE) + MAX3243E (DTE) on one female DB25"],
            [Sym("comment"), 2, "See SPEC.md; parts and nets come from hardware/netlist/build_netlist.py"]],
           [Sym("lib_symbols")] + [syms[k] for k in sorted({lib_id(r) for r in N.parts} | set(POWER.values()) | {"power:PWR_FLAG"})]]
    sch += items
    sch += [[Sym("sheet_instances"), [Sym("path"), "/", [Sym("page"), "1"]]], [Sym("embedded_fonts"), Sym("no")]]
    return sch, world, boxes, cells, placed, (paper, pw, ph)


def text_node(s, x, y, size, bold=False):
    return [Sym("text"), s, [Sym("exclude_from_sim"), Sym("no")], [Sym("at"), x, y, 0],
            eff(size=size, justify="left bottom", bold=bold), [Sym("uuid"), uid("text", s)]]


# ===========================================================================
# Checks
# ===========================================================================
def connectivity(world):
    """Nets as KiCad would see them: by coincident points, labels and power names."""
    parent = {}

    def f(a):
        parent.setdefault(a, a)
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def u(a, b):
        parent[f(a)] = f(b)

    def pt(p):
        return ("pt", round(p[0], 3), round(p[1], 3))
    segs = []
    for kind, data, geo in world:
        if kind == "wire":
            a, b = geo
            u(pt(a), pt(b))
            segs.append((a, b))
        elif kind == "pin":
            u(("pin",) + data, pt(geo))
        elif kind == "label":
            u(("net", data), pt(geo))
        elif kind in ("power", "flag"):
            u(("net", data), pt(geo))
        elif kind == "nc":
            f(pt(geo))
    # a point in the middle of a wire would join it in KiCad (with a junction): forbid
    errs = []
    for kind, data, geo in world:
        if kind in ("pin", "label", "power", "nc"):
            for a, b in segs:
                near = any(abs(geo[0] - e[0]) < 1e-3 and abs(geo[1] - e[1]) < 1e-3 for e in (a, b))
                if on_segment(geo, a, b) and not near:
                    errs.append(f"{kind} {data} at {geo} sits on the middle of a wire")
    nets = defaultdict(set)
    for k in list(parent):
        if k[0] == "pin":
            nets[f(k)].add((k[1], k[2]))
    return [frozenset(v) for v in nets.values()], errs, f


def on_segment(p, a, b):
    (x, y), (x0, y0), (x1, y1) = p, a, b
    if abs((x1 - x0) * (y - y0) - (y1 - y0) * (x - x0)) > 1e-6:
        return False
    return min(x0, x1) - 1e-6 <= x <= max(x0, x1) + 1e-6 and min(y0, y1) - 1e-6 <= y <= max(y0, y1) + 1e-6


def check(world, boxes, cells):
    errs = []
    got, e2, _ = connectivity(world)
    errs += e2
    want = [frozenset(m) for m in N.nets.values()]
    # pins left open: each should be a net of one (its own) in the drawing
    got_multi = {g for g in got if len(g) > 1}
    want_set = set(want)
    # stacked pins of one symbol at the same point come out joined; that's fine if the
    # netlist has them on one net. Compare after dropping singletons.
    for g in got_multi - want_set:
        errs.append(f"drawing has a net the netlist doesn't: {sorted(g)}")
    for w_ in want_set - got_multi:
        errs.append(f"netlist net not drawn as one net: {sorted(w_)}")
    for g in got:
        if len(g) == 1:
            (m,) = g
            if m not in N.noconnect:
                errs.append(f"pin {m} is isolated in the drawing but not marked NC")
    # overlaps (text, labels, bodies): a label touching its own wire end is fine
    bs = sorted(boxes, key=lambda b: b.x0)
    for i, a in enumerate(bs):
        for b in bs[i + 1:]:
            if b.x0 >= a.x1:
                break
            if a.hits(b, gap=-0.05) and not (a.owner == b.owner and "symbol" in (a.kind, b.kind)):
                errs.append(f"overlap: {a.owner} {a.kind} / {b.owner} {b.kind}")
    return errs


# ===========================================================================
# Preview (rough SVG: shapes, pins, labels; good enough to eyeball layout)
# ===========================================================================
def preview(sch, syms, path, pw, ph):
    S = 4  # px per mm
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{pw * S:.0f}" height="{ph * S:.0f}" '
           f'viewBox="0 0 {pw} {ph}" font-family="Helvetica, Arial, sans-serif">',
           '<rect width="100%" height="100%" fill="#f5f4ee"/>',
           f'<rect x="10" y="10" width="{pw - 20}" height="{ph - 20}" fill="none" stroke="#8a2f2f" stroke-width="0.25"/>']
    col_body, col_line, col_wire, col_text, col_label = "#fffbd8", "#8a2f2f", "#1b7a3a", "#1a3d8f", "#222"

    def T(x, y, s, size=1.27, anchor="start", rot=0, color=col_text, weight="normal", baseline="auto"):
        tr = f' transform="rotate({-rot} {x:.3f} {y:.3f})"' if rot else ""
        s = s.replace("&", "&amp;").replace("<", "&lt;").replace("~{", "").replace("}", "")
        return (f'<text x="{x:.3f}" y="{y:.3f}" font-size="{size * 1.15:.2f}" text-anchor="{anchor}" fill="{color}" '
                f'font-weight="{weight}" dominant-baseline="{baseline}"{tr}>{s}</text>')
    for node in sch:
        if not isinstance(node, list):
            continue
        k = node[0]
        if k == "wire":
            (a, b) = [tuple(float(v) for v in xy[1:]) for xy in find(first(node, "pts"), "xy")]
            out.append(f'<line x1="{a[0]}" y1="{a[1]}" x2="{b[0]}" y2="{b[1]}" stroke="{col_wire}" stroke-width="0.15"/>')
        elif k == "label":
            at = first(node, "at")
            x, y, ang = float(at[1]), float(at[2]), float(at[3])
            anchor = "start" if ang in (0, 90) else "end"
            rot = 90 if ang in (90, 270) else 0
            out.append(T(x, y - 0.3 if not rot else y, node[1], anchor=anchor, rot=rot, color=col_label)
                       if not rot else T(x - 0.3, y, node[1], anchor=anchor, rot=rot, color=col_label))
        elif k == "no_connect":
            at = first(node, "at")
            x, y = float(at[1]), float(at[2])
            out.append(f'<path d="M{x - .6},{y - .6}L{x + .6},{y + .6}M{x - .6},{y + .6}L{x + .6},{y - .6}" '
                       f'stroke="#1a3d8f" stroke-width="0.15"/>')
        elif k == "text":
            at = first(node, "at")
            size = float(first(first(first(node, "effects"), "font"), "size")[1])
            bold = first(first(first(node, "effects"), "font"), "bold") is not None
            out.append(T(float(at[1]), float(at[2]), node[1], size=size, color="#333",
                         weight="bold" if bold else "normal"))
        elif k == "symbol":
            lid = first(node, "lib_id")[1]
            at = first(node, "at")
            X, Y, rot = float(at[1]), float(at[2]), float(at[3])
            g = Geo(syms[lid])

            def P(x, y):
                return xf(x, y, X, Y, rot)
            for s in g.shapes:
                kind = s[0]
                fill = first(first(s, "fill") or [], "type")
                fc = col_body if fill and fill[1] == "background" else ("#8a2f2f" if fill and fill[1] == "outline" else "none")
                if kind == "rectangle":
                    a, b = P(*map(float, first(s, "start")[1:3])), P(*map(float, first(s, "end")[1:3]))
                    out.append(f'<rect x="{min(a[0], b[0])}" y="{min(a[1], b[1])}" width="{abs(a[0] - b[0])}" '
                               f'height="{abs(a[1] - b[1])}" fill="{fc}" stroke="{col_line}" stroke-width="0.2"/>')
                elif kind in ("polyline", "bezier"):
                    pts = " ".join("%.3f,%.3f" % P(float(xy[1]), float(xy[2])) for xy in find(first(s, "pts"), "xy"))
                    out.append(f'<polyline points="{pts}" fill="{fc}" stroke="{col_line}" stroke-width="0.2"/>')
                elif kind == "circle":
                    c = P(*map(float, first(s, "center")[1:3]))
                    out.append(f'<circle cx="{c[0]}" cy="{c[1]}" r="{first(s, "radius")[1]}" fill="{fc}" '
                               f'stroke="{col_line}" stroke-width="0.2"/>')
                elif kind == "arc":
                    a, m, b = (P(*map(float, first(s, key)[1:3])) for key in ("start", "mid", "end"))
                    out.append(arc_path(a, m, b, col_line))
            for p in g.pins:
                if p["hide"]:
                    continue
                dx, dy = DIRS[p["ang"]]
                a = P(p["x"], p["y"])
                b = P(p["x"] + dx * p["length"], p["y"] + dy * p["length"])
                out.append(f'<line x1="{a[0]}" y1="{a[1]}" x2="{b[0]}" y2="{b[1]}" stroke="{col_line}" stroke-width="0.2"/>')
                if g.power or p["length"] == 0:
                    continue
                sx, sy = b[0] - a[0], b[1] - a[1]
                L = math.hypot(sx, sy) or 1
                ux, uy = sx / L, sy / L
                if not g.hide_names and p["name"] not in ("~", ""):
                    off = g.name_offset if g.name_offset > 0 else 0
                    if off:
                        nx, ny = b[0] + ux * (off + 0.3), b[1] + uy * (off + 0.3)
                        anchor = "start" if ux > 0.5 else "end" if ux < -0.5 else "middle"
                        r = 90 if abs(uy) > 0.5 else 0
                        if r:
                            anchor = "start" if uy < 0 else "end"
                        out.append(T(nx, ny + (0.45 if not r else 0), p["name"], size=1.0, anchor=anchor, rot=r, color="#2b5e88"))
                if not g.hide_numbers:
                    mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
                    if abs(uy) > 0.5:
                        out.append(T(mx - 0.4, my, p["number"], size=0.9, anchor="middle", rot=90, color="#8a2f2f"))
                    else:
                        out.append(T(mx, my - 0.35, p["number"], size=0.9, anchor="middle", color="#8a2f2f"))
            for pr in find(node, "property"):
                e = first(pr, "effects")
                if first(e, "hide") is not None or not pr[2]:
                    continue
                pat = first(pr, "at")
                j = first(e, "justify")
                anchor = "middle"
                if j is not None and "left" in j:
                    anchor = "start"
                elif j is not None and "right" in j:
                    anchor = "end"
                dnp = first(node, "dnp")[1] == "yes"
                ang = float(pat[3]) if len(pat) > 3 else 0
                out.append(T(float(pat[1]), float(pat[2]), pr[2] + (" (DNP)" if dnp and pr[1] == "Value" else ""),
                             anchor=anchor, rot=ang, color="#0b5c5c" if pr[1] == "Reference" else "#444",
                             baseline="middle"))
    out.append("</svg>")
    open(path, "w").write("\n".join(out))


def arc_path(a, m, b, color):
    (x1, y1), (x2, y2), (x3, y3) = a, m, b
    d = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2))
    if abs(d) < 1e-9:
        return f'<line x1="{x1}" y1="{y1}" x2="{x3}" y2="{y3}" stroke="{color}" stroke-width="0.2"/>'
    ux = ((x1 ** 2 + y1 ** 2) * (y2 - y3) + (x2 ** 2 + y2 ** 2) * (y3 - y1) + (x3 ** 2 + y3 ** 2) * (y1 - y2)) / d
    uy = ((x1 ** 2 + y1 ** 2) * (x3 - x2) + (x2 ** 2 + y2 ** 2) * (x1 - x3) + (x3 ** 2 + y3 ** 2) * (x2 - x1)) / d
    r = math.hypot(x1 - ux, y1 - uy)
    cross = (x2 - x1) * (y3 - y1) - (y2 - y1) * (x3 - x1)
    sweep = 1 if cross > 0 else 0
    large = 0
    return (f'<path d="M{x1:.3f},{y1:.3f} A{r:.3f},{r:.3f} 0 {large} {sweep} {x3:.3f},{y3:.3f}" fill="none" '
            f'stroke="{color}" stroke-width="0.2"/>')


def write_lib(path):
    lib = [Sym("kicad_symbol_lib"), [Sym("version"), Sym("20241209")], [Sym("generator"), "kicad_symbol_editor"],
           [Sym("generator_version"), "9.0"]] + [CUSTOM[k] for k in sorted(CUSTOM)]
    open(path, "w").write(sexpr.dump(lib) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--kicad-sym9", help="kicad-symbols checkout (9.0 tag), refreshes lib/symbol-cache.sexp")
    a = ap.parse_args()
    syms = load_symbols(a.kicad_sym9)
    sch, world, boxes, cells, placed, (paper, pw, ph) = build(syms)
    errs = check(world, boxes, cells)
    for e in errs:
        print("ERROR:", e)
    if errs:
        sys.exit(1)
    text = sexpr.dump(sch) + "\n"
    sexpr.parse(text)   # round-trips
    open(os.path.join(HERE, f"{PROJECT}.kicad_sch"), "w").write(text)
    write_lib(os.path.join(HERE, f"{PROJECT}.kicad_sym"))
    open(os.path.join(HERE, "sym-lib-table"), "w").write(
        '(sym_lib_table\n\t(version 7)\n\t(lib (name "pico-serial") (type "KiCad") '
        '(uri "${KIPRJMOD}/pico-serial.kicad_sym") (options "") (descr "SP3238E, MAX3243E"))\n)\n')
    pro = os.path.join(HERE, f"{PROJECT}.kicad_pro")
    if not os.path.exists(pro):
        open(pro, "w").write('{\n  "meta": {\n    "filename": "pico-serial.kicad_pro",\n    "version": 3\n  }\n}\n')
    preview(sch, syms, os.path.join(HERE, "preview.svg"), pw, ph)
    n_sym = sum(1 for x in sch if isinstance(x, list) and x[0] == "symbol")
    n_lab = sum(1 for x in sch if isinstance(x, list) and x[0] == "label")
    print(f"ok: {paper} sheet, {len(N.parts)} parts, {n_sym - len(N.parts)} power symbols/flags, "
          f"{n_lab} labels; drawing connects exactly as the netlist ({len(N.nets)} nets)")


if __name__ == "__main__":
    main()
