#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Neo-Desktop
"""Every function in the firmware has a comment saying what it does.

  python3 firmware/tools/check_docs.py        # lists what's missing; exit 1 if anything is

The rule:
  - a function's definition has a comment directly above it (attribute or
    `static` lines may sit in between), or
  - it is public and its prototype in a header has one: the header documents
    the interface, the definition needn't repeat it;
  - every prototype in a header has one.
Checked: C sources and headers, the C block of .pio files, and the
emulator test (TypeScript). Generated files (src/generated) and the SDK's
own files are skipped.
"""
import os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SKIP_DIRS = {"generated", "node_modules", ".git"}
SKIP_FILES = {"pico_sdk_import.cmake"}
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "do", "else", "case", "defined", "_Static_assert"}


def blank_code(text):
    """The text with comments and strings blanked (newlines kept), and the set
    of line numbers (1-based) on which a comment ends."""
    out, ends, i, n = [], set(), 0, len(text)
    line = 1
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i)); ends.add(line); i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            seg = text[i:j]
            out.append(re.sub(r"[^\n]", " ", seg))
            line += seg.count("\n"); ends.add(line); i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            seg = text[i:j + 1]
            out.append(c + re.sub(r"[^\n]", " ", seg[1:-1]) + c); line += seg.count("\n"); i = j + 1
        else:
            out.append(c); line += c == "\n"; i += 1
    return "".join(out), ends


def documented(lines, ends, first, last=None):
    """A comment ends on the line above `first` (1-based), skipping lines that
    only carry attributes or qualifiers of the same declaration, or trails the
    signature itself (`void f(void);  // what it does`)."""
    if any(ln in ends for ln in range(first, (last or first) + 1)):
        return True
    k = first - 1
    while k >= 1 and re.fullmatch(r"\s*(static|inline|__attribute__\(\(.*\)\)|__not_in_flash_func.*|\w+_ISR)?\s*",
                                  lines[k - 1]) and lines[k - 1].strip():
        k -= 1
    return k in ends


def c_functions(path):
    """(name, first line, is_definition, is_static, documented) for each function
    declared or defined at file scope."""
    text = open(path).read()
    if path.endswith(".pio"):   # only its C block
        m = re.search(r"% c-sdk \{(.*?)%\}", text, re.S)
        if not m:
            return []
        pre = text[:m.start(1)]
        text = re.sub(r"[^\n]", " ", pre) + m.group(1)
    code, ends = blank_code(text)
    lines = text.split("\n")
    found, depth, stmt_start, i = [], 0, None, 0
    stmt = []
    for ln, raw in enumerate(code.split("\n"), 1):
        s = raw
        if s.lstrip().startswith("#"):        # preprocessor line (and its continuations, close enough)
            continue
        for ch in s:
            if depth == 0:
                if not stmt and ch.isspace():
                    continue
                if not stmt:
                    stmt_start = ln
                if ch == "{" or ch == ";":
                    body = "".join(stmt)
                    m = re.search(r"(\w+)\s*\(([^()]|\([^()]*\))*\)\s*(__attribute__\(\(.*\)\)\s*)?$", body)
                    if m and m.group(1) not in KEYWORDS and not re.match(r"\s*(typedef|extern\s+\"C\")", body) \
                            and "=" not in body.split("(")[0] and not re.match(r"\s*(struct|enum|union)\s+\w+\s*$", body) \
                            and re.match(r"\s*[A-Za-z_]", body):
                        found.append((m.group(1), stmt_start, ch == "{", bool(re.match(r"\s*static\b", body)),
                                      documented(lines, ends, stmt_start, ln)))
                    stmt = []
                    if ch == "{":
                        depth = 1
                    continue
                stmt.append(ch)
            else:
                depth += (ch == "{") - (ch == "}")
        if depth == 0 and stmt:
            stmt.append(" ")
    return found


def ts_functions(path):
    text = open(path).read()
    code, ends = blank_code(text)
    lines = text.split("\n")
    out = []
    for ln, s in enumerate(code.split("\n"), 1):
        m = re.match(r"\s*(export\s+)?(async\s+)?function\s+(\w+)|\s*(export\s+)?const\s+(\w+)\s*=\s*(async\s*)?\(.*\)\s*(:\s*[^=]+)?=>", s)
        if m:
            out.append((m.group(3) or m.group(5), ln, True, True, documented(lines, ends, ln)))
    return out


def main():
    missing, total = [], 0
    header_doc = {}
    files = []
    for d, dirs, fs in os.walk(ROOT):
        dirs[:] = [x for x in dirs if x not in SKIP_DIRS and not x.startswith("build")]
        for f in sorted(fs):
            if f in SKIP_FILES:
                continue
            if f.endswith((".c", ".h", ".pio", ".ts")):
                files.append(os.path.join(d, f))
    parsed = {p: (ts_functions(p) if p.endswith(".ts") else c_functions(p)) for p in sorted(files)}
    for p, fns in parsed.items():
        if p.endswith(".h"):
            for name, ln, is_def, st, doc in fns:
                if not is_def:
                    header_doc[name] = header_doc.get(name, False) or doc
    for p, fns in parsed.items():
        rel = os.path.relpath(p, ROOT)
        for name, ln, is_def, st, doc in fns:
            if not is_def and not p.endswith(".h"):
                continue       # a forward declaration in a .c: the definition carries the comment
            total += 1
            if doc or (is_def and not st and header_doc.get(name)):
                continue
            missing.append(f"{rel}:{ln}: {name}")
    for m in missing:
        print("undocumented:", m)
    print(f"{total - len(missing)} of {total} functions documented")
    sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()
