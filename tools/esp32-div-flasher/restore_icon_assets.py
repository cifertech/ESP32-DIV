#!/usr/bin/env python3
"""Write assets/app_icon.png and assets/app_icon.ico from _APP_ICON_PNG_B64 in flash_div.py."""
from __future__ import annotations

import ast
import base64
import struct
import sys
from pathlib import Path


def _extract_icon_b64(flash_div_py: Path) -> str:
    tree = ast.parse(flash_div_py.read_text(encoding="utf-8"))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        for t in node.targets:
            if isinstance(t, ast.Name) and t.id == "_APP_ICON_PNG_B64":
                val = ast.literal_eval(node.value)
                if isinstance(val, tuple):
                    return "".join(val)
                if isinstance(val, str):
                    return val
    raise RuntimeError("_APP_ICON_PNG_B64 not found in flash_div.py")


def _png_to_ico(png: bytes, width: int, height: int) -> bytes:
    off = 6 + 16
    hdr = struct.pack("<HHH", 0, 1, 1) + struct.pack(
        "<BBBBHHII", width, height, 0, 0, 1, 0, len(png), off
    )
    return hdr + png


def main() -> int:
    here = Path(__file__).resolve().parent
    flash = here / "flash_div.py"
    if not flash.is_file():
        print("Expected flash_div.py next to this script.", file=sys.stderr)
        return 1
    b64 = _extract_icon_b64(flash)
    png = base64.b64decode(b64)
    assets = here / "assets"
    assets.mkdir(parents=True, exist_ok=True)
    (assets / "app_icon.png").write_bytes(png)
    (assets / "app_icon.ico").write_bytes(_png_to_ico(png, 64, 64))
    print("Wrote", assets / "app_icon.png", len(png), "bytes")
    print("Wrote", assets / "app_icon.ico")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
