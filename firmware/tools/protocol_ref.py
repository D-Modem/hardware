#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 D-Modem Authors
"""docs/protocol-reference.md from the protocol headers the firmware and the
host apps share: every constant, request, enum and wire struct (with each
field's offset and size), and the comments beside them.

  python3 firmware/tools/protocol_ref.py           # writes the page
  python3 firmware/tools/protocol_ref.py --check   # exit 1 if it is out of date

firmware/PROTOCOL.md says how the protocols are used; this page is what
the headers define, so the two can't disagree on a value.
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "docs", "protocol-reference.md")
HEADERS = [
    ("pico-serial control", "firmware/src/core/protocol.h", "the control interface of the DB25 and W boards"),
    ("Line stick", "firmware/src/core/line_proto.h", "pico-serial Line, the phone-line stick"),
    ("WB stick", "firmware/src/core/wb_proto.h", "pico-serial WB, the wideband stick"),
]
SIZES = {"uint8_t": 1, "int8_t": 1, "uint16_t": 2, "int16_t": 2, "uint32_t": 4, "int32_t": 4}


def cell(s):
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return " ".join(s.replace("|", "\\|").split())


def split_comment(line):
    """(code, trailing comment) of a line."""
    m = re.match(r"(.*?)\s*//\s?(.*)$", line)
    return (m.group(1), m.group(2).strip()) if m else (line, "")


def int_value(v):
    v = v.strip()
    if re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", v):
        return int(v, 0)
    return None


def parse(text):
    """The header's blocks in order: ("intro", text), ("defines", note,
    rows), ("enum", name, note, rows), ("struct", name, note, rows, size)."""
    lines = text.splitlines()
    blocks, note, intro, i = [], [], None, 0
    # the licence comment, then the header's own comment
    while i < len(lines) and not lines[i].startswith("//"):
        i += 1
    while i < len(lines) and lines[i].startswith("//"):
        note.append(lines[i][2:].strip())
        i += 1
    blocks.append(("intro", " ".join(note)))
    note = []
    while i < len(lines):
        line = lines[i]
        s = line.strip()
        if s.startswith("//"):
            note.append(s[2:].strip())
            i += 1
            continue
        if not s or s.startswith(("#pragma", "#include")):
            if not s:
                note = [] if not blocks or blocks[-1][0] != "defines" else note
            i += 1
            continue
        if s.startswith("#define"):
            rows = []
            text_note = " ".join(note)
            note = []
            while i < len(lines) and lines[i].startswith("#define"):
                code, com = split_comment(lines[i])
                m = re.match(r"#define\s+(\w+)\s+(.*)", code)
                rows.append((m.group(1), m.group(2).strip(), com))
                i += 1
            blocks.append(("defines", text_note, rows))
            continue
        m = re.match(r"enum\s*(\w*)\s*\{(.*)", s)
        if m:
            name, rest = m.group(1), m.group(2)
            text_note = " ".join(note)
            note = []
            body = []
            if "}" in rest:
                code, com = split_comment(rest)
                inner = code.split("}")[0]
                for part in [p for p in inner.split(",") if p.strip()]:
                    body.append((part.strip(), ""))
                if com:
                    text_note = (text_note + " " + com).strip()
                i += 1
            else:
                i += 1
                while not lines[i].strip().startswith("}"):
                    t = lines[i].strip()
                    if t.startswith("//"):
                        body.append((None, t[2:].strip()))
                    elif t:
                        code, com = split_comment(t)
                        body.append((code.rstrip(","), com))
                    i += 1
                i += 1
            rows, nxt = [], 0
            for code, com in body:
                if code is None:
                    rows.append((None, None, com))
                    continue
                n, _, v = code.partition("=")
                val = int_value(v) if v else nxt
                rows.append((n.strip(), val if val is not None else v.strip(), com))
                nxt = (val if isinstance(val, int) else 0) + 1
            blocks.append(("enum", name, text_note, rows))
            continue
        if s.startswith("typedef struct"):
            text_note = " ".join(note)
            note = []
            i += 1
            rows, off = [], 0
            while not lines[i].strip().startswith("}"):
                code, com = split_comment(lines[i].strip())
                fm = re.match(r"(\w+)\s+(.*);", code)
                if fm:
                    typ = fm.group(1)
                    for f in fm.group(2).split(","):
                        f = f.strip()
                        am = re.match(r"(\w+)\[(\d+)\]", f)
                        count = int(am.group(2)) if am else 1
                        size = SIZES[typ] * count
                        rows.append((off, size, typ + ("[%d]" % count if am else ""),
                                     am.group(1) if am else f, com))
                        off += size
                i += 1
            name = re.match(r"\}\s*(\w+)", lines[i].strip()).group(1)
            blocks.append(("struct", name, text_note, rows, off))
            i += 1
            continue
        if s.startswith("_Static_assert"):
            m = re.match(r"_Static_assert\(sizeof\((\w+)\)\s*==\s*(\d+)", s)
            if m:
                for b in blocks:
                    if b[0] == "struct" and b[1] == m.group(1) and b[4] != int(m.group(2)):
                        sys.exit("protocol_ref: %s is %d bytes here, %s bytes in the header"
                                 % (b[1], b[4], m.group(2)))
            i += 1
            continue
        # anything else (an inline function): to its end
        note = []
        if "{" in s and "}" not in s:
            depth = 0
            while True:
                depth += lines[i].count("{") - lines[i].count("}")
                i += 1
                if depth <= 0:
                    break
        else:
            i += 1
    return blocks


def show(v):
    if isinstance(v, int):
        return "`%d`" % v if v < 16 else "`%d` (`0x%02X`)" % (v, v)
    return "`%s`" % v


def sentence(s):
    s = s.strip()
    if not s:
        return ""
    first = s.split()[0]
    if "_" not in first and "." not in first.rstrip(".") and not first.startswith("pico-"):
        s = s[0].upper() + s[1:]
    return s if s.endswith((".", ")", ":")) else s + "."


def page():
    b = ["<!-- The script firmware/tools/protocol_ref.py makes this page from the protocol headers. "
         "Change the headers (or the script), not this page. -->\n\n",
         "# Protocol reference\n\n",
         "This page gives the definitions of the protocol headers of the firmware, "
         "value by value: the requests, constants and wire structs that a host app "
         "shares with the boards. [PROTOCOL.md](../firmware/PROTOCOL.md) tells how to "
         "use them. All structs are packed and little-endian.\n\n"]
    for title, rel, what in HEADERS:
        with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
            blocks = parse(f.read())
        b.append("## %s\n\n" % title)
        b.append("%s: [`%s`](../%s).\n\n" % (sentence(what)[:-1], os.path.basename(rel), rel))
        for blk in blocks:
            kind = blk[0]
            if kind == "intro":
                if blk[1]:
                    b.append(sentence(blk[1]) + "\n\n")
            elif kind == "defines":
                if blk[1]:
                    b.append(sentence(blk[1]) + "\n\n")
                b.append("| name | value | |\n|---|---|---|\n")
                for n, v, com in blk[2]:
                    iv = int_value(v)
                    b.append("| `%s` | %s | %s |\n" % (n, show(iv) if iv is not None else "`%s`" % cell(v), cell(com)))
                b.append("\n")
            elif kind == "enum":
                _, name, text_note, rows = blk
                b.append("### %s\n\n" % ("`enum %s`" % name if name else
                                         "`%s_*`" % os.path.commonprefix([r[0] for r in rows if r[0]]).rstrip("_")))
                if text_note:
                    b.append(sentence(text_note) + "\n\n")
                b.append("| name | value | |\n|---|---|---|\n")
                for n, v, com in rows:
                    if n is None:
                        b.append("| | | *%s* |\n" % cell(com))
                    else:
                        b.append("| `%s` | %s | %s |\n" % (n, show(v), cell(com)))
                b.append("\n")
            else:
                _, name, text_note, rows, size = blk
                b.append("### `%s`\n\n%d bytes.%s\n\n" % (name, size, " " + sentence(text_note) if text_note else ""))
                b.append("| offset | size | type | field | |\n|---|---|---|---|---|\n")
                for off, sz, typ, f, com in rows:
                    b.append("| %d | %d | `%s` | `%s` | %s |\n" % (off, sz, typ, f, cell(com)))
                b.append("\n")
    return "".join(b).rstrip("\n") + "\n"


def main():
    text = page()
    old = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else None
    if "--check" in sys.argv[1:]:
        if old != text:
            print("protocol_ref: docs/protocol-reference.md is out of date: "
                  "run python3 firmware/tools/protocol_ref.py", file=sys.stderr)
            return 1
        return 0
    if old != text:
        with open(OUT, "w", encoding="utf-8") as f:
            f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
