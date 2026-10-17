"""Minimal reader/writer for KiCad's s-expression files.

A node is a Python list whose first item is its keyword. Atoms are Sym
(bare words and numbers) or str (quoted strings), so quoting survives a
round trip.
"""
import re


class Sym(str):
    """A bare (unquoted) atom."""


_TOKEN = re.compile(r'\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|([^\s()"]+))', re.S)


def parse(text):
    """Parse one top-level node. Lines starting with ;; are comments."""
    text = "\n".join(l for l in text.split("\n") if not l.lstrip().startswith(";;"))
    stack, out, pos = [], None, 0
    while True:
        m = _TOKEN.match(text, pos)
        if not m or m.end() == pos:
            if text[pos:].strip():
                raise ValueError(f"cannot parse at {pos}: {text[pos:pos + 40]!r}")
            break
        pos = m.end()
        lp, rp, s, a = m.groups()
        if lp:
            stack.append([])
        elif rp:
            node = stack.pop()
            if stack:
                stack[-1].append(node)
            else:
                out = node
        elif s is not None:
            stack[-1].append(re.sub(r'\\(.)', r'\1', s))
        else:
            stack[-1].append(Sym(a))
    if stack:
        raise ValueError("unbalanced parentheses")
    return out


def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def atom(x):
    if isinstance(x, Sym):
        return str(x)
    if isinstance(x, bool):
        return "yes" if x else "no"
    if isinstance(x, (int, float)):
        return num(x)
    return q(x)


def num(v):
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def dump(node, indent=0):
    """KiCad-style layout: short nodes on one line, others one child per line."""
    if not isinstance(node, list):
        return atom(node)
    head = [atom(x) for x in node if not isinstance(x, list)]
    kids = [x for x in node if isinstance(x, list)]
    line = "(" + " ".join(head)
    if not kids:
        return line + ")"
    flat = line + " " + " ".join(dump(k, 0) for k in kids) + ")"
    if len(flat) + indent <= 100 and "\n" not in flat:
        return flat
    pad = "\t" * (indent + 1)
    return line + "\n" + "\n".join(pad + dump(k, indent + 1) for k in kids) + "\n" + "\t" * indent + ")"


def find(node, key):
    return [x for x in node if isinstance(x, list) and x and x[0] == key]


def first(node, key, default=None):
    r = find(node, key)
    return r[0] if r else default
