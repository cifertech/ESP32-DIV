#!/usr/bin/env python3
"""Check that every menu's label and icon tables are fully initialised.

The menus are parallel arrays: a `..._items[N]` of labels beside a
`..._icons[N]` of bitmap pointers, walked by the same index. Add an entry to
one and forget the other and C++ says nothing. An array with fewer
initialisers than its size is legal, and the missing elements are null.
Neither -Wall nor -Wextra warns.

What happens next is not a blank row. displaySubmenu() hands the pointer
straight to TFT_eSPI's drawBitmap, which dereferences it, so the board faults
the moment that menu page is opened. The crash is in the drawing code, a long
way from the table that caused it, and it only happens on the one page.

    python tools/check_menu_tables.py

Reads source. Needs no board and no toolchain. Exit status is 0 when every
table is full and each label table's bound matches its icon table's.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), "ESP32-DIV", "ESP32-DIV.ino")

# `const char *foo[BAR] = { ... };` for labels, `const unsigned char *foo[BAR]`
# for icons. Both are matched the same way.
TABLE = re.compile(
    r"const\s+(?:unsigned\s+char|char)\s*\*\s*(\w+)\s*\[\s*(\w+)\s*\]\s*=\s*\{(.*?)\}\s*;",
    re.S)
SIZE = re.compile(
    r"(?:const\s+int|static\s+constexpr\s+int)\s+(\w+)\s*=\s*(\d+)\s*;")


def count_initialisers(body):
    """Entries in a brace-initialiser list, ignoring comments and any
    trailing comma."""
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    return len([p for p in body.split(",") if p.strip()])


def main():
    src = io.open(SRC, encoding="utf-8", errors="replace", newline="").read()
    sizes = {k: int(v) for k, v in SIZE.findall(src)}

    tables = {}
    for m in TABLE.finditer(src):
        name, dim, body = m.group(1), m.group(2), m.group(3)
        if dim not in sizes:
            continue                      # a literal or unknown bound
        tables[name] = (dim, sizes[dim], count_initialisers(body))

    if not tables:
        print("found no menu tables. has the file moved?", file=sys.stderr)
        return 1

    bad = []
    for name, (dim, want, got) in sorted(tables.items()):
        if got != want:
            bad.append("%s[%s] is %d but has %d initialiser%s"
                       % (name, dim, want, got, "" if got == 1 else "s"))

    # A label table and its icon table must agree on their bound, or the two
    # are walked with different lengths and the shorter one runs off its end.
    pairs = 0
    for name, (dim, want, got) in sorted(tables.items()):
        if not name.endswith("_items"):
            continue
        for icons in (name[:-6] + "_icons", name[:-6] + "icons"):
            if icons in tables:
                pairs += 1
                if tables[icons][0] != dim:
                    bad.append("%s is sized %s but %s is sized %s"
                               % (name, dim, icons, tables[icons][0]))
                break

    for name, (dim, want, got) in sorted(tables.items()):
        mark = "ok " if got == want else "BAD"
        print("  %s %-28s %-28s %2d/%-2d" % (mark, name, dim, got, want))

    print()
    if bad:
        print("FAILED:", file=sys.stderr)
        for b in bad:
            print("  " + b, file=sys.stderr)
        return 1

    print("ok: %d tables fully initialised, %d label/icon pairs agree"
          % (len(tables), pairs))
    print("a short icon table is a null pointer handed to drawBitmap, and "
          "nothing else catches it")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
