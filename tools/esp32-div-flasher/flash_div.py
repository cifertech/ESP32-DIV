#!/usr/bin/env python3
"""
ESP32-DIV firmware flasher (GUI + CLI).

Uses esptool.py under the hood with the same layout as the Arduino-ESP32 uploader
for ESP32-S3 and classic ESP32 boards.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import queue
import re
import subprocess
import sys
import ssl
import tempfile
import threading
import urllib.error
import urllib.parse
import urllib.request
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Optional

APP_NAME = "ESP32-DIV"
APP_TAGLINE = "Firmware installer"
__version__ = "1.2.0"
GITHUB_REPO_URL = "https://github.com/cifertech/ESP32-DIV"
GITHUB_API_LATEST_RELEASE = "https://api.github.com/repos/cifertech/ESP32-DIV/releases/latest"
GITHUB_API_RELEASES = "https://api.github.com/repos/cifertech/ESP32-DIV/releases"
# Browser flasher UI reference: https://github.com/cifertech/FirmwareHub
FH_BROWSER_APP_URL = "https://cifertech.github.io/FirmwareHub/"

_FH_PALETTE: dict[str, dict[str, str]] = {
    "dark": {
        "canvas": "#08090c",
        "shell": "#101114",
        "panel": "#16181d",
        "surface": "#1e2028",
        "border": "#2e3140",
        "text": "#eaedf3",
        "muted": "#8690a2",
        "accent": "#f59e0b",
        "accent_hover": "#fbbf24",
        "accent_dim": "#b45309",
        "accent_text": "#0c0c0c",
        "accent_glow": "#f59e0b",
        "input": "#0d0e12",
        "log_bg": "#0b0c10",
        "log_fg": "#c8cdd8",
        "sel_bg": "#f59e0b",
        "sel_fg": "#0c0c0c",
        "pill_bg": "#1a1c24",
        "pill_hover": "#262830",
        "success": "#22c55e",
        "error": "#ef4444",
        "step_num": "#f59e0b",
    },
    "light": {
        "canvas": "#eef0f4",
        "shell": "#f8f9fc",
        "panel": "#ffffff",
        "surface": "#f0f2f6",
        "border": "#d0d5e0",
        "text": "#0f172a",
        "muted": "#64748b",
        "accent": "#d97706",
        "accent_hover": "#b45309",
        "accent_dim": "#f59e0b",
        "accent_text": "#ffffff",
        "accent_glow": "#d97706",
        "input": "#ffffff",
        "log_bg": "#fafbfd",
        "log_fg": "#1e293b",
        "sel_bg": "#d97706",
        "sel_fg": "#ffffff",
        "pill_bg": "#e8ebf0",
        "pill_hover": "#dde1e8",
        "success": "#16a34a",
        "error": "#dc2626",
        "step_num": "#d97706",
    },
}


def _fh_ui_family(root: object) -> str:
    """Prefer Segoe UI Variable (Windows 10/11) for a more modern look."""
    import tkinter as tk
    import tkinter.font as tkfont

    if not isinstance(root, tk.Misc):
        return "Segoe UI"
    for cand in ("Segoe UI Variable Text", "Segoe UI Variable", "Segoe UI"):
        try:
            tkfont.Font(root, family=cand, size=10)
            return cand
        except tk.TclError:
            continue
    return "Segoe UI"


def _fh_apply_clam_theme(root: object, style: object, pal: dict[str, str]) -> tuple[str, str, str]:
    """Apply theme on clam. Returns (primary_btn_style, secondary_btn_style, ui_family)."""
    import tkinter as tk
    from tkinter import ttk

    root.configure(bg=pal["canvas"])  # type: ignore[attr-defined]
    cast_style = style  # ttk.Style
    cast_style.theme_use("clam")
    ui = _fh_ui_family(root)
    shell, panel, surf, brd = pal["shell"], pal["panel"], pal["surface"], pal["border"]
    fg, mut = pal["text"], pal["muted"]
    ac, ac_txt = pal["accent"], pal["accent_text"]
    ac_hov = pal.get("accent_hover", ac)
    ac_dim = pal.get("accent_dim", ac)

    cast_style.configure(".", background=shell, foreground=fg)
    cast_style.configure("TFrame", background=shell, foreground=fg)
    cast_style.configure("TLabel", background=shell, foreground=fg, font=(ui, 10))
    cast_style.configure("FHMuted.TLabel", background=shell, foreground=mut, font=(ui, 9))
    cast_style.configure("FHInner.TLabel", background=panel, foreground=fg, font=(ui, 10))
    cast_style.configure("FHInnerMuted.TLabel", background=panel, foreground=mut, font=(ui, 9))
    cast_style.configure("FHInnerStrong.TLabel", background=panel, foreground=fg, font=(ui, 10, "bold"))
    cast_style.configure("FHInnerAccent.TLabel", background=panel, foreground=ac, font=(ui, 9, "bold"))
    cast_style.configure(
        "FHStepNum.TLabel",
        background=panel,
        foreground=pal.get("step_num", ac),
        font=(ui, 11, "bold"),
    )
    cast_style.configure(
        "TLabelframe",
        background=panel,
        foreground=fg,
        bordercolor=brd,
        relief="flat",
        borderwidth=1,
    )
    cast_style.configure(
        "TLabelframe.Label",
        background=panel,
        foreground=mut,
        font=(ui, 9, "bold"),
    )
    cast_style.configure(
        "TEntry",
        fieldbackground=pal["input"],
        foreground=fg,
        bordercolor=brd,
        lightcolor=brd,
        darkcolor=brd,
        insertcolor=fg,
        padding=(8, 5),
    )
    cast_style.map(
        "TEntry",
        fieldbackground=[("readonly", pal["input"]), ("disabled", surf)],
        foreground=[("readonly", fg), ("disabled", mut)],
        bordercolor=[("focus", ac), ("!focus", brd)],
        lightcolor=[("focus", ac), ("!focus", brd)],
        darkcolor=[("focus", ac), ("!focus", brd)],
    )
    cast_style.configure(
        "TCombobox",
        fieldbackground=pal["input"],
        background=surf,
        foreground=fg,
        arrowcolor=mut,
        bordercolor=brd,
        padding=(7, 4),
    )
    cast_style.map(
        "TCombobox",
        fieldbackground=[("readonly", pal["input"]), ("disabled", surf)],
        foreground=[("readonly", fg), ("disabled", mut)],
        bordercolor=[("focus", ac), ("!focus", brd)],
        arrowcolor=[("focus", ac), ("!focus", mut)],
    )
    cast_style.configure("TCheckbutton", background=panel, foreground=fg, font=(ui, 10))
    cast_style.map(
        "TCheckbutton",
        background=[
            ("disabled", surf),
            ("pressed", surf),
            ("active", pal["pill_hover"]),
            ("!disabled", panel),
        ],
        foreground=[("disabled", mut), ("!disabled", fg)],
        indicatorcolor=[("selected", ac), ("!selected", mut)],
    )
    cast_style.configure("TNotebook", background=shell, borderwidth=0)
    try:
        cast_style.configure("TNotebook", tabmargins=[2, 4, 2, 0])
    except tk.TclError:
        pass
    cast_style.configure(
        "TNotebook.Tab",
        background=surf,
        foreground=mut,
        padding=(14, 6),
        font=(ui, 10, "bold"),
    )
    cast_style.map(
        "TNotebook.Tab",
        background=[("selected", panel)],
        foreground=[("selected", ac)],
    )
    cast_style.configure(
        "FHPrimary.TButton",
        background=ac,
        foreground=ac_txt,
        bordercolor=ac,
        focuscolor=ac,
        font=(ui, 11, "bold"),
        padding=(16, 8),
    )
    cast_style.map(
        "FHPrimary.TButton",
        background=[("active", ac_hov), ("pressed", ac_dim), ("disabled", brd)],
        foreground=[("disabled", mut)],
        bordercolor=[("active", ac_hov), ("pressed", ac_dim), ("disabled", brd)],
    )
    cast_style.configure(
        "FHSecondary.TButton",
        background=surf,
        foreground=fg,
        bordercolor=brd,
        font=(ui, 9),
        padding=(12, 5),
    )
    cast_style.map(
        "FHSecondary.TButton",
        background=[("active", pal["pill_hover"]), ("pressed", shell)],
        foreground=[("active", ac), ("!active", fg)],
        bordercolor=[("active", ac), ("!active", brd)],
    )
    cast_style.configure(
        "TProgressbar",
        thickness=6,
        troughcolor=surf,
        background=ac,
        bordercolor=surf,
        lightcolor=ac,
        darkcolor=ac,
    )
    cast_style.configure("TSeparator", background=brd)
    return "FHPrimary.TButton", "FHSecondary.TButton", ui


# Arduino ESP32 boot_app0.bin (8192 bytes), zlib+base64 so flashing works even when
# the user copies only flash_div.py and not bundled/boot_app0.bin.
_EMBEDDED_BOOT_APP0_ZB64 = (
    "eNrt18ERABAQBLBVm4cWjWZPBfyZpIu0JHWwZh8FAAAAPC+X/wMAAAB//H8DFETXmw=="
)

# 64x64 PNG window icon (dark tile + orange chip). Used when assets/app_icon.png is missing
# (e.g. user copied only flash_div.py).
_APP_ICON_PNG_B64 = (
    "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAkUlEQVR42u3bIQ6AMAwF0OkGUbmjcX/JJUAiGYYt9IkvJv9LxbKlLSLOymkAANyH3nuJAAAAAAAAAAAAAAAAAAAAAAC+BTj27TEAAPwAYKTo2wAAAAAAgJUBRkpk5nSMqQArTAMAAAAAAAAAwE0QAAAAAAB4FQYAwN8gAAAAAAAAAAAAAAAAAACoDGB3GEA9gAuipinzYSocjAAAAABJRU5ErkJggg=="
)

_materialized_boot_app0_path: Optional[Path] = None

PRESETS: dict[str, dict] = {
    "esp32s3": {
        "chip": "esp32s3",
        "offsets": {
            "bootloader": 0x0,
            "partition_table": 0x8000,
            "boot_app0": 0xE000,
            "app": 0x10000,
        },
        "flash_mode": "dio",
        "flash_freq": "80m",
    },
    "esp32": {
        "chip": "esp32",
        "offsets": {
            "bootloader": 0x1000,
            "partition_table": 0x8000,
            "boot_app0": 0xE000,
            "app": 0x10000,
        },
        "flash_mode": "dio",
        "flash_freq": "40m",
    },
}

PRESET_LABELS: dict[str, str] = {
    "esp32s3": "ESP32-S3 (ESP32-DIV v2)",
    "esp32": "ESP32 (CYD & ESP32-DIV v1)",
}


def infer_preset_from_app_bin(path: Path | str) -> Optional[str]:
    """
    Guess bundled preset from a release/sketch .bin filename.

    Multi-board GitHub assets use names like ESP32-DIV-{cyd|v1|v2}-v1.7.0.bin.
    CYD and v1 targets are classic ESP32; v2 is ESP32-S3.
    """
    stem = Path(path).stem.lower()
    if "-cyd-" in stem or stem.endswith("-cyd"):
        return "esp32"
    if re.search(r"-v1-v\d", stem):
        return "esp32"
    if re.search(r"-v2-v\d", stem):
        return "esp32s3"
    return None


def effective_bundled_preset(preset_key: str, app_bin: Path) -> str:
    """Use filename inference when it disagrees with the selected preset."""
    inferred = infer_preset_from_app_bin(app_bin)
    if inferred and inferred != preset_key:
        return inferred
    return preset_key


def offsets_for_chip(chip: str) -> dict[str, int]:
    for _preset_name, cfg in PRESETS.items():
        if cfg["chip"] == chip:
            return dict(cfg["offsets"])
    raise ValueError(
        f"Unknown chip {chip!r} for offset layout. Use manifest.json with a supported chip, "
        f"or one of: {sorted({c['chip'] for c in PRESETS.values()})}."
    )


@dataclass(frozen=True)
class GithubReleaseChoice:
    """One row in the GitHub release picker (GUI)."""

    tag_name: str
    asset_name: str
    download_url: str
    label: str


@dataclass
class ResolvedImages:
    chip: str
    flash_mode: str
    flash_freq: str
    bootloader: Path
    partition_table: Path
    boot_app0: Path
    app: Path


def _script_dir() -> Path:
    """
    Directory that holds flash_div.py and bundled/.

    Python sometimes reports __file__ inside __pycache__ (.pyc only), or relative paths that
    resolve against the wrong cwd. Walk ancestors until we find bundled/ or flash_div.py.
    Override with env ESP32_DIV_FLASHER_HOME if needed.
    """
    env = os.environ.get("ESP32_DIV_FLASHER_HOME", "").strip()
    if env:
        ep = Path(env).expanduser().resolve()
        if ep.is_dir():
            return ep

    if getattr(sys, "frozen", False):
        meipass = getattr(sys, "_MEIPASS", None)
        if meipass:
            return Path(meipass)

    def normalize(d: Path) -> Path:
        while d.name == "__pycache__":
            d = d.parent
        return d

    def looks_like_tool_root(d: Path) -> bool:
        return (d / "bundled").is_dir() or (d / "flash_div.py").is_file()

    candidates: list[Path] = []
    main_fp = getattr(sys.modules.get("__main__"), "__file__", None)
    if main_fp:
        candidates.append(Path(main_fp).resolve())
    candidates.append(Path(__file__).resolve())

    seen: set[Path] = set()
    for src in candidates:
        start = normalize(src.parent)
        for d in [start, *start.parents]:
            cur = normalize(d)
            if cur in seen:
                continue
            seen.add(cur)
            if looks_like_tool_root(cur):
                return cur

    # Fallback: strip __pycache__ from primary __file__ parent only
    return normalize(Path(__file__).resolve().parent)


def _apply_gui_window_icon(root: object) -> None:
    """Taskbar / window title icon (PNG). Prefer assets/app_icon.png next to the script."""
    import base64
    import tempfile
    import tkinter as tk

    if not isinstance(root, (tk.Tk, tk.Toplevel)):
        return
    icon_path = _script_dir() / "assets" / "app_icon.png"
    try:
        if icon_path.is_file():
            img = tk.PhotoImage(file=str(icon_path))
        else:
            raw = base64.b64decode(_APP_ICON_PNG_B64)
            tmp = Path(tempfile.gettempdir()) / "esp32-div-flasher-window-icon.png"
            tmp.write_bytes(raw)
            img = tk.PhotoImage(file=str(tmp))
        root.iconphoto(True, img)
        setattr(root, "_fh_app_icon_ref", img)
    except tk.TclError:
        pass


def bundled_boot_app0() -> Path:
    return _script_dir() / "bundled" / "boot_app0.bin"


def _materialize_embedded_boot_app0() -> Path:
    """Write Arduino boot_app0.bin to the user's temp dir (cached path)."""
    global _materialized_boot_app0_path
    if _materialized_boot_app0_path is not None and _materialized_boot_app0_path.is_file():
        return _materialized_boot_app0_path
    raw = zlib.decompress(base64.b64decode(_EMBEDDED_BOOT_APP0_ZB64.encode("ascii")))
    if len(raw) != 8192:
        raise RuntimeError(
            f"Internal error: embedded boot_app0 wrong length ({len(raw)}); reinstall flasher."
        )
    cache_dir = Path(tempfile.gettempdir()) / "esp32-div-flasher-cache"
    cache_dir.mkdir(parents=True, exist_ok=True)
    dest = cache_dir / "boot_app0.bin"
    dest.write_bytes(raw)
    _materialized_boot_app0_path = dest
    return dest


def _mtime(p: Path) -> float:
    try:
        return p.stat().st_mtime
    except OSError:
        return 0.0


def _bins_direct(root: Path) -> list[Path]:
    """*.bin in this folder only (no recursion)."""
    try:
        return sorted(root.glob("*.bin"))
    except OSError:
        return []


def _firmware_folder_candidates(fw_dir: Path) -> list[Path]:
    """
    Try the selected folder first, then immediate subfolders (Arduino/release zips
    often nest build output one level deep).
    """
    fw_dir = fw_dir.expanduser().resolve()
    out = [fw_dir]
    try:
        subs = sorted(
            [p for p in fw_dir.iterdir() if p.is_dir()],
            key=lambda p: p.name.lower(),
        )
        out.extend(subs)
    except OSError:
        pass
    return out


def _pick_bootloader_bin(bins: list[Path]) -> Optional[Path]:
    # Arduino export: Sketch.ino.bootloader.bin
    tier = [p for p in bins if ".bootloader." in p.name.lower()]
    if tier:
        return max(tier, key=_mtime)
    tier = [p for p in bins if "bootloader" in p.name.lower()]
    if tier:
        return max(tier, key=_mtime)
    return None


def _pick_partitions_bin(bins: list[Path]) -> Optional[Path]:
    tier = [p for p in bins if ".partitions." in p.name.lower()]
    if tier:
        return max(tier, key=_mtime)
    tier = [
        p
        for p in bins
        if "partition" in p.name.lower() and "bootloader" not in p.name.lower()
    ]
    if tier:
        return max(tier, key=_mtime)
    return None


def _pick_app_bin(bins: list[Path], reserved: set[Path]) -> Optional[Path]:
    rest = [p for p in bins if p.resolve() not in reserved]
    if not rest:
        return None

    ino = [p for p in rest if p.name.lower().endswith(".ino.bin")]
    if ino:
        return max(ino, key=_mtime)

    for fixed in ("firmware.bin", "app.bin"):
        hits = [p for p in rest if p.name.lower() == fixed]
        if hits:
            return max(hits, key=_mtime)

    candidates = []
    for p in rest:
        n = p.name.lower()
        if any(
            x in n
            for x in (
                "bootloader",
                "partition",
                "boot_app0",
                "littlefs",
                "spiffs",
                "fatfs",
            )
        ):
            continue
        candidates.append(p)
    if not candidates:
        return None
    return max(candidates, key=lambda x: x.stat().st_size)


def _boot_app0_path(fw_root: Path) -> Path:
    local = fw_root / "boot_app0.bin"
    if local.is_file():
        return local
    bundled = bundled_boot_app0()
    if bundled.is_file():
        return bundled
    # Covers installs where only flash_div.py was copied (no bundled/ folder).
    return _materialize_embedded_boot_app0()


def _summarize_bins_near(fw_dir: Path, limit: int = 40) -> str:
    lines: list[str] = []
    seen: set[str] = set()
    for base in _firmware_folder_candidates(fw_dir):
        for p in _bins_direct(base):
            key = str(p.resolve())
            if key in seen:
                continue
            seen.add(key)
            try:
                rel = p.relative_to(fw_dir.resolve())
            except ValueError:
                rel = p
            lines.append(f"  {rel}")
    if not lines:
        return "No .bin files found in the selected folder or its immediate subfolders."
    lines.sort()
    extra = ""
    if len(lines) > limit:
        extra = f"\n  ... and {len(lines) - limit} more"
        lines = lines[:limit]
    return "Found these .bin files:\n" + "\n".join(lines) + extra


def _try_resolve_from_root(fw_root: Path, preset_key: str) -> Optional[ResolvedImages]:
    preset = PRESETS[preset_key]
    bins = _bins_direct(fw_root)
    if len(bins) < 2:
        return None

    bl = _pick_bootloader_bin(bins)
    pt = _pick_partitions_bin(bins)
    if not bl or not pt:
        return None

    ba = _boot_app0_path(fw_root)

    reserved = {bl.resolve(), pt.resolve(), ba.resolve()}
    app_bin = _pick_app_bin(bins, reserved)
    if not app_bin:
        return None

    return ResolvedImages(
        chip=preset["chip"],
        flash_mode=preset["flash_mode"],
        flash_freq=preset["flash_freq"],
        bootloader=bl,
        partition_table=pt,
        boot_app0=ba,
        app=app_bin,
    )


def resolve_firmware_dir(fw_dir: Path, preset_key: str) -> ResolvedImages:
    preset = PRESETS[preset_key]
    chip = preset["chip"]
    flash_mode = preset["flash_mode"]
    flash_freq = preset["flash_freq"]

    fw_dir = fw_dir.expanduser()
    if not fw_dir.is_dir():
        raise FileNotFoundError(f"Not a folder: {fw_dir}")

    manifest_path = fw_dir / "manifest.json"
    if manifest_path.is_file():
        data = json.loads(manifest_path.read_text(encoding="utf-8"))
        chip = data.get("chip", chip)
        flash_mode = data.get("flash_mode", flash_mode)
        flash_freq = data.get("flash_freq", flash_freq)
        missing = []
        paths: dict[str, Path] = {}
        for key in ("bootloader", "partition_table", "boot_app0", "app"):
            name = data[key]
            p = (fw_dir / name).resolve()
            paths[key] = p
            if not p.is_file():
                missing.append(f"{key} ({name})")
        if missing:
            raise FileNotFoundError(
                "manifest.json refers to missing files: "
                + ", ".join(missing)
                + "\n"
                + _summarize_bins_near(fw_dir)
            )
        return ResolvedImages(
            chip=chip,
            flash_mode=flash_mode,
            flash_freq=flash_freq,
            bootloader=paths["bootloader"],
            partition_table=paths["partition_table"],
            boot_app0=paths["boot_app0"],
            app=paths["app"],
        )

    for candidate in _firmware_folder_candidates(fw_dir):
        resolved = _try_resolve_from_root(candidate, preset_key)
        if resolved is not None:
            return resolved

    detail = _summarize_bins_near(fw_dir)
    raise FileNotFoundError(
        "Could not identify bootloader + partitions + app .bin files.\n"
        "You need Arduino-style exports (e.g. *.bootloader.bin, *.partitions.bin, *.ino.bin), "
        "or add manifest.json next to the binaries.\n\n"
        + detail
    )


def bundled_preset_dir(preset_key: str) -> Path:
    """Preferred folder for preset-specific static bins: bundled/<preset>/."""
    return _script_dir() / "bundled" / preset_key


def bundled_boot_chain_roots(preset_key: str) -> list[Path]:
    """
    Where to look for static bootloader/partitions (in order):
      1) bundled/<preset>/   e.g. bundled/esp32s3/
      2) bundled/             shared bins for all presets
    """
    base = _script_dir() / "bundled"
    preset_sub = base / preset_key
    roots: list[Path] = []
    if preset_sub.is_dir():
        roots.append(preset_sub)
    if base.is_dir():
        roots.append(base)
    seen: set[Path] = set()
    unique: list[Path] = []
    for r in roots:
        rp = r.resolve()
        if rp not in seen:
            seen.add(rp)
            unique.append(r)
    return unique


def _bin_candidates_near(root: Path) -> list[Path]:
    """
    *.bin in folder plus one level of subfolders (ZIP layouts like bundled/build/*.bin).
    """
    if not root.is_dir():
        return []
    out: list[Path] = []
    try:
        out.extend(root.glob("*.bin"))
        for child in sorted(root.iterdir(), key=lambda p: p.name.lower()):
            if child.is_dir():
                out.extend(child.glob("*.bin"))
    except OSError:
        pass
    return out


def _find_bootloader_static(root: Path) -> Optional[Path]:
    p = root / "bootloader.bin"
    if p.is_file():
        return p
    hits = [x for x in _bin_candidates_near(root) if "bootloader" in x.name.lower()]
    return max(hits, key=_mtime) if hits else None


def _find_partitions_static(root: Path) -> Optional[Path]:
    p = root / "partitions.bin"
    if p.is_file():
        return p
    hits = [
        x
        for x in _bin_candidates_near(root)
        if "partition" in x.name.lower() and "bootloader" not in x.name.lower()
    ]
    return max(hits, key=_mtime) if hits else None


def resolve_bundled_boot_chain(preset_key: str) -> tuple[Path, Path, Path]:
    roots = bundled_boot_chain_roots(preset_key)
    if not roots:
        base = _script_dir() / "bundled"
        raise FileNotFoundError(
            "No bundled folder found.\n"
            f"Create this directory and add your exported .bin files:\n  {base}\n"
            "Either put them directly there, or under a preset subfolder "
            f"(e.g. {base / preset_key})."
        )

    tried_lines: list[str] = []
    for root in roots:
        tried_lines.append(str(root.resolve()))
        bl = _find_bootloader_static(root)
        pt = _find_partitions_static(root)
        if bl and pt:
            ba = _boot_app0_path(root)
            return bl, pt, ba

    raise FileNotFoundError(
        "Bundled bootloader or partitions .bin not found.\n"
        "Checked (in order):\n  "
        + "\n  ".join(tried_lines)
        + "\n\n"
        "From Arduino: Sketch - Export compiled Binary, then copy files into bundled/:\n"
        "  *bootloader*.bin (or bootloader.bin)\n"
        "  *.partitions*.bin (or partitions.bin)\n"
        "Optional: boot_app0.bin (otherwise the built-in Arduino boot_app0 is used).\n"
        f"You can use bundled/{preset_key}/ instead of bundled/ if you prefer."
    )


def resolve_bundled_plus_app(preset_key: str, app_bin: Path) -> ResolvedImages:
    preset = PRESETS[preset_key]
    app_bin = app_bin.expanduser().resolve()
    if not app_bin.is_file():
        raise FileNotFoundError(f"Application binary not found:\n  {app_bin}")
    bl, pt, ba = resolve_bundled_boot_chain(preset_key)
    return ResolvedImages(
        chip=preset["chip"],
        flash_mode=preset["flash_mode"],
        flash_freq=preset["flash_freq"],
        bootloader=bl,
        partition_table=pt,
        boot_app0=ba,
        app=app_bin,
    )


def _http_headers() -> dict[str, str]:
    return {
        "Accept": "application/vnd.github+json",
        "User-Agent": f"{APP_NAME}-FirmwareInstaller/{__version__}",
        "X-GitHub-Api-Version": "2022-11-28",
    }


def _github_api_get_json(url: str, timeout: float = 60.0) -> Any:
    req = urllib.request.Request(url, headers=_http_headers(), method="GET")
    ctx = ssl.create_default_context()
    try:
        with urllib.request.urlopen(req, timeout=timeout, context=ctx) as resp:
            raw = resp.read()
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"GitHub API error: HTTP {e.code}") from e
    except urllib.error.URLError as e:
        raise RuntimeError(f"Could not reach GitHub: {e}") from e
    return json.loads(raw.decode("utf-8"))


def github_fetch_latest_release() -> dict[str, Any]:
    data = _github_api_get_json(GITHUB_API_LATEST_RELEASE)
    if not isinstance(data, dict):
        raise RuntimeError("Unexpected GitHub API response for latest release")
    return data


def github_fetch_release_by_tag(tag: str) -> dict[str, Any]:
    tag = tag.strip()
    if not tag:
        raise ValueError("Empty release tag")
    enc = urllib.parse.quote(tag, safe="")
    url = f"{GITHUB_API_RELEASES}/tags/{enc}"
    data = _github_api_get_json(url)
    if not isinstance(data, dict):
        raise RuntimeError("Unexpected GitHub API response for release tag")
    return data


def github_list_release_choices(per_page: int = 40) -> list[GithubReleaseChoice]:
    """Recent releases; one row per firmware .bin asset (multiple rows when a tag has variants)."""
    url = f"{GITHUB_API_RELEASES}?per_page={max(1, min(per_page, 100))}"
    data = _github_api_get_json(url, timeout=90.0)
    if not isinstance(data, list):
        return []
    out: list[GithubReleaseChoice] = []
    for rel in data:
        if not isinstance(rel, dict):
            continue
        tag = str(rel.get("tag_name") or "?")
        assets_raw = rel.get("assets")
        assets: list[dict[str, Any]] = assets_raw if isinstance(assets_raw, list) else []
        firmware_assets = github_list_firmware_assets(assets)
        if firmware_assets:
            for aname, dl_url in firmware_assets:
                lbl = f"{tag}   {aname}"
                out.append(
                    GithubReleaseChoice(
                        tag_name=tag,
                        asset_name=aname,
                        download_url=dl_url,
                        label=lbl,
                    )
                )
        else:
            lbl = f"{tag}   (no firmware .bin)"
            out.append(
                GithubReleaseChoice(
                    tag_name=tag,
                    asset_name="",
                    download_url="",
                    label=lbl,
                )
            )
    return out


def download_github_firmware_asset(
    tag_name: str,
    asset_filename: str,
    url: str,
    on_line: Callable[[str], None],
) -> Path:
    """Download one release asset into the temp cache."""
    if not url or not asset_filename:
        raise RuntimeError("Missing download URL or filename")
    safe_tag = tag_name.replace("/", "_").replace("\\", "_")
    cache = Path(tempfile.gettempdir()) / "esp32-div-flasher-downloads" / safe_tag
    cache.mkdir(parents=True, exist_ok=True)
    dest = cache / asset_filename
    http_download_file(url, dest, on_line)
    return dest


def download_release_app_bin(release: dict[str, Any], on_line: Callable[[str], None]) -> Path:
    """Pick firmware .bin from a release JSON object and download it."""
    tag = str(release.get("tag_name") or "release")
    assets_raw = release.get("assets")
    assets = assets_raw if isinstance(assets_raw, list) else []
    picked = github_pick_firmware_asset(assets)
    if picked is None:
        raise RuntimeError(
            f"Release {tag!r} has no suitable firmware .bin asset "
            "(expected ESP32-DIV-x.y.z.bin or *.ino.bin)."
        )
    name, url = picked
    return download_github_firmware_asset(tag, name, url, on_line)


def github_list_firmware_assets(assets: list[dict[str, Any]]) -> list[tuple[str, str]]:
    """
    List application-sized .bin assets from a release.
    Skips obvious bootloader/partition/helper artifacts.
    Best match first (same ranking as github_pick_firmware_asset).
    """
    scored: list[tuple[int, int, str, str]] = []
    for a in assets:
        name = str(a.get("name") or "")
        url = str(a.get("browser_download_url") or "")
        if not url or not name.lower().endswith(".bin"):
            continue
        low = name.lower()
        if any(
            x in low
            for x in (
                "bootloader",
                "partition",
                "boot_app0",
                "littlefs",
                "spiffs",
                "fatfs",
            )
        ):
            continue
        try:
            sz = int(a.get("size") or 0)
        except (TypeError, ValueError):
            sz = 0
        if sz < 80_000:
            continue
        score = 0
        if ".ino.bin" in low:
            score += 120
        if "esp32-div" in low or "esp32_div" in low:
            score += 80
        scored.append((score, sz, name, url))
    if not scored:
        return []
    scored.sort(key=lambda t: (-t[0], -t[1], t[2].lower()))
    return [(name, url) for _score, _sz, name, url in scored]


def github_pick_firmware_asset(assets: list[dict[str, Any]]) -> Optional[tuple[str, str]]:
    """Pick the best application-sized .bin from release assets."""
    items = github_list_firmware_assets(assets)
    return items[0] if items else None


def http_download_file(url: str, dest: Path, on_line: Callable[[str], None]) -> None:
    req = urllib.request.Request(url, headers=_http_headers(), method="GET")
    ctx = ssl.create_default_context()
    dest.parent.mkdir(parents=True, exist_ok=True)
    on_line(f"Downloading:\n  {url}")
    try:
        with urllib.request.urlopen(req, timeout=300, context=ctx) as resp:
            total = int(resp.headers.get("Content-Length") or 0)
            chunk = 256 * 1024
            got = 0
            with dest.open("wb") as out:
                while True:
                    buf = resp.read(chunk)
                    if not buf:
                        break
                    out.write(buf)
                    got += len(buf)
                    if total > 0 and got % (chunk * 4) < chunk:
                        on_line(f"  ... {100 * got // total}% ({got // (1024 * 1024)} MiB)")
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"Download failed: HTTP {e.code}") from e
    except urllib.error.URLError as e:
        raise RuntimeError(f"Download failed: {e}") from e
    on_line(f"Saved: {dest}")


def download_latest_release_app_bin(on_line: Callable[[str], None]) -> Path:
    return download_release_app_bin(github_fetch_latest_release(), on_line)


def download_tagged_release_app_bin(tag: str, on_line: Callable[[str], None]) -> Path:
    return download_release_app_bin(github_fetch_release_by_tag(tag), on_line)


def esptool_base_prefix() -> list[str]:
    return [sys.executable, "-m", "esptool"]


def esptool_import_ok(on_line: Optional[Callable[[str], None]] = None) -> bool:
    """True if this interpreter can run ``python -m esptool`` (same as ``import esptool``)."""
    try:
        import esptool  # noqa: F401

        return True
    except ImportError:
        if on_line:
            on_line("Missing dependency: the esptool package is not installed for this Python.")
            on_line(f"  Interpreter: {sys.executable}")
            on_line("  Install:  pip install esptool")
            on_line("  Or from this folder:  pip install -r requirements.txt")
        return False


def _esptool_major_version() -> int:
    try:
        import esptool

        parts = str(getattr(esptool, "__version__", "0")).split(".")
        return int(parts[0]) if parts and parts[0].isdigit() else 0
    except Exception:
        return 0


def _esp_cli(sub_old: str, sub_new: str) -> str:
    """esptool v5+ prefers hyphenated verbs (write-flash); v4 uses underscores."""
    return sub_old if _esptool_major_version() < 5 else sub_new


def normalize_serial_port(port: str) -> str:
    return port.strip().strip('"').strip("'")


def resolve_serial_port(port: str) -> tuple[bool, str]:
    """
    Return (ok, canonical_device).
    On Windows, COMx casing may differ between UI and pyserial.
    """
    port = normalize_serial_port(port)
    if not port:
        return False, port
    devices = list_serial_ports()
    names = [d for d, _ in devices]
    if port in names:
        return True, port
    low = {n.lower(): n for n in names}
    if port.lower() in low:
        return True, low[port.lower()]
    return False, port


def format_available_ports_hint() -> str:
    devs = list_serial_ports()
    if not devs:
        return (
            "No serial ports detected. Check USB cable (data, not charge-only), "
            "CP2102 driver, and that the board powers up."
        )
    lines = "\n".join(f"  - {lbl}" for _, lbl in devs[:16])
    extra = f"\n  ... ({len(devs)} total)" if len(devs) > 16 else ""
    return "Ports pyserial sees right now:\n" + lines + extra


def run_esptool_stream(
    args: list[str],
    on_line: Callable[[str], None],
) -> int:
    proc = subprocess.Popen(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
    )
    assert proc.stdout is not None
    for line in proc.stdout:
        on_line(line.rstrip("\n"))
    proc.wait()
    return int(proc.returncode or 0)


def flash_firmware(
    port: str,
    baud: int,
    images: ResolvedImages,
    erase_all: bool,
    on_line: Callable[[str], None],
) -> int:
    if not esptool_import_ok(on_line):
        return 1

    ok, port_c = resolve_serial_port(port)
    if not ok:
        on_line(
            f"Serial port {normalize_serial_port(port)!r} is not available "
            "(wrong number, unplugged, or held open by another program)."
        )
        on_line(format_available_ports_hint())
        on_line(
            "Tips: click Refresh ports and pick the CP210x/USB-SERIAL port; "
            "close Arduino IDE Serial Monitor or any serial terminal using that COM port."
        )
        return 2

    off = offsets_for_chip(images.chip)
    chip = images.chip
    legacy = _esptool_major_version() < 5
    before_after = (
        ("default_reset", "hard_reset")
        if legacy
        else ("default-reset", "hard-reset")
    )
    fm = "--flash_mode" if legacy else "--flash-mode"
    ff = "--flash_freq" if legacy else "--flash-freq"
    fs = "--flash_size" if legacy else "--flash-size"

    base = esptool_base_prefix() + [
        "--chip",
        chip,
        "--port",
        port_c,
        "--baud",
        str(baud),
        "--before",
        before_after[0],
        "--after",
        before_after[1],
    ]

    if erase_all:
        rc = run_esptool_stream(base + [_esp_cli("erase_flash", "erase-flash")], on_line)
        if rc != 0:
            return rc

    write = base + [
        _esp_cli("write_flash", "write-flash"),
        "-z",
        fm,
        images.flash_mode,
        ff,
        images.flash_freq,
        fs,
        "detect",
        hex(off["bootloader"]),
        str(images.bootloader),
        hex(off["partition_table"]),
        str(images.partition_table),
        hex(off["boot_app0"]),
        str(images.boot_app0),
        hex(off["app"]),
        str(images.app),
    ]
    return run_esptool_stream(write, on_line)


def list_serial_ports() -> list[tuple[str, str]]:
    try:
        from serial.tools import list_ports
    except ImportError:
        return []
    out: list[tuple[str, str]] = []
    for p in list_ports.comports():
        label = p.device
        if p.description and p.description != p.device:
            label = f"{p.device} - {p.description}"
        out.append((p.device, label))
    return out


def _gui() -> None:
    import tkinter as tk
    import tkinter.font as tkfont
    import webbrowser
    from tkinter import filedialog, messagebox, scrolledtext, ttk

    log_q: queue.Queue[str] = queue.Queue()
    preset_rows: list[tuple[str, str]] = [(k, PRESET_LABELS.get(k, k)) for k in PRESETS]

    root = tk.Tk()
    root.title(f"{APP_NAME} — Desktop flasher")
    win_w, win_h = 1440, 810
    root.minsize(win_w, win_h)
    root.maxsize(win_w, win_h)
    root.resizable(False, False)
    root.geometry(f"{win_w}x{win_h}")
    root.rowconfigure(0, weight=1)
    root.columnconfigure(0, weight=1)
    root.after_idle(lambda: _apply_gui_window_icon(root))

    def set_windows_titlebar_dark(enabled: bool) -> None:
        if sys.platform != "win32":
            return
        try:
            import ctypes

            root.update_idletasks()
            value = ctypes.c_int(1 if enabled else 0)
            hwnd = ctypes.windll.user32.GetParent(root.winfo_id())
            if not hwnd:
                hwnd = root.winfo_id()
            # Windows 10 20H1+ uses 20; older builds used 19.
            for attr in (20, 19):
                result = ctypes.windll.dwmapi.DwmSetWindowAttribute(
                    hwnd,
                    attr,
                    ctypes.byref(value),
                    ctypes.sizeof(value),
                )
                if result == 0:
                    break
        except Exception:
            pass

    style = ttk.Style(root)
    fh_variant: str = "dark"
    pal: dict[str, str] = dict(_FH_PALETTE[fh_variant])
    prim_st, sec_st, ui_family = _fh_apply_clam_theme(root, style, pal)
    set_windows_titlebar_dark(fh_variant == "dark")

    outer = tk.Frame(root, bg=pal["canvas"])
    outer.grid(row=0, column=0, sticky="nsew")
    outer.rowconfigure(0, weight=1)
    outer.columnconfigure(0, weight=1)

    shell = ttk.Frame(outer, padding=(16, 12))
    shell.grid(row=0, column=0, sticky="nsew", padx=10, pady=10)
    shell.rowconfigure(0, weight=1)
    shell.columnconfigure(0, weight=1)

    main = ttk.Frame(shell)
    main.grid(row=0, column=0, sticky="nsew")
    main.columnconfigure(0, weight=2)
    main.columnconfigure(1, weight=1)
    main.rowconfigure(1, weight=1)

    def center_window() -> None:
        root.update_idletasks()
        w, h = win_w, win_h
        x = max(0, (root.winfo_screenwidth() - w) // 2)
        y = max(0, (root.winfo_screenheight() - h) // 2)
        root.geometry(f"{w}x{h}+{x}+{y}")

    def show_about() -> None:
        messagebox.showinfo(
            f"About {APP_NAME}",
            f"{APP_NAME} {APP_TAGLINE}\n\n"
            f"Version {__version__}\n\n"
            "Flashes exported Arduino binaries using esptool.\n"
            "For research and educational use only.",
            parent=root,
        )

    def open_repo() -> None:
        webbrowser.open(GITHUB_REPO_URL)

    def open_firmwarehub() -> None:
        webbrowser.open(FH_BROWSER_APP_URL)

    def show_shortcuts() -> None:
        messagebox.showinfo(
            "Keyboard shortcuts",
            "F5 — Refresh serial ports\n"
            "Ctrl+Enter — Flash firmware\n"
            "Ctrl+L — Clear activity log\n"
            "Ctrl+Q — Exit",
            parent=root,
        )

    header = tk.Frame(main, bg=pal["shell"])
    header.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 2))
    header.columnconfigure(0, weight=1)

    try:
        title_f = tkfont.Font(root, family="Segoe UI Variable Display", size=20, weight="bold")
    except tk.TclError:
        try:
            title_f = tkfont.Font(root, family=ui_family, size=20, weight="bold")
        except tk.TclError:
            title_f = tkfont.Font(size=20, weight="bold")
    try:
        sub_f = tkfont.Font(root, family=ui_family, size=9)
    except tk.TclError:
        sub_f = tkfont.Font(size=9)
    try:
        ver_f = tkfont.Font(root, family=ui_family, size=8)
    except tk.TclError:
        ver_f = tkfont.Font(size=8)

    ht_left = tk.Frame(header, bg=pal["shell"])
    ht_left.grid(row=0, column=0, sticky="w")

    title_row = tk.Frame(ht_left, bg=pal["shell"])
    title_row.pack(anchor="w")
    title_lbl = tk.Label(
        title_row,
        text=APP_NAME,
        font=title_f,
        fg=pal["accent"],
        bg=pal["shell"],
    )
    title_lbl.pack(side="left")
    ver_lbl = tk.Label(
        title_row,
        text=f"v{__version__}",
        font=ver_f,
        fg=pal["muted"],
        bg=pal["pill_bg"],
        padx=6,
        pady=1,
    )
    ver_lbl.pack(side="left", padx=(8, 0), pady=(4, 0))

    sub_lbl = tk.Label(
        ht_left,
        text="Guided firmware flashing for ESP32 boards",
        font=sub_f,
        fg=pal["muted"],
        bg=pal["shell"],
    )
    sub_lbl.pack(anchor="w", pady=(1, 0))

    links_fr = tk.Frame(header, bg=pal["shell"])
    links_fr.grid(row=0, column=1, sticky="e", padx=(8, 0))
    link_labs: list[tk.Label] = []
    extra_link_labs: list[tk.Label] = []

    def _style_link_pill(lb: tk.Label) -> None:
        def _in(_e: object | None = None) -> None:
            lb.configure(bg=pal["pill_hover"])

        def _out(_e: object | None = None) -> None:
            lb.configure(bg=pal["pill_bg"])

        lb.configure(bg=pal["pill_bg"])
        lb.bind("<Enter>", _in)
        lb.bind("<Leave>", _out)

    for txt, url in (
        ("FirmwareHub", FH_BROWSER_APP_URL),
        ("ESP32-DIV", GITHUB_REPO_URL),
    ):
        lb = tk.Label(
            links_fr,
            text=txt,
            fg=pal["accent"],
            bg=pal["pill_bg"],
            cursor="hand2",
            font=(ui_family, 9, "bold"),
            padx=10,
            pady=4,
        )
        lb.pack(side="left", padx=(0, 6))
        lb.bind("<Button-1>", lambda _e, u=url: webbrowser.open(u))
        _style_link_pill(lb)
        link_labs.append(lb)

    header_extras = tk.Frame(links_fr, bg=pal["shell"])
    header_extras.pack(side="left", padx=(8, 0))

    def add_header_link(text: str, command: Callable[[], None]) -> None:
        lb = tk.Label(
            header_extras,
            text=text,
            fg=pal["muted"],
            bg=pal["pill_bg"],
            cursor="hand2",
            font=(ui_family, 8),
            padx=8,
            pady=3,
        )
        lb.pack(side="left", padx=(0, 4))
        lb.bind("<Button-1>", lambda _e: command())
        _style_link_pill(lb)
        extra_link_labs.append(lb)

    accent_bar = tk.Frame(header, height=2, bg=pal["accent"], cursor="")
    accent_bar.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))

    body = ttk.Frame(main)
    body.grid(row=1, column=0, columnspan=2, sticky="nsew", pady=(8, 0))
    body.columnconfigure(0, weight=3)
    body.columnconfigure(1, weight=2)
    body.rowconfigure(0, weight=1)

    left = ttk.Frame(body)
    left.grid(row=0, column=0, sticky="nsew", padx=(0, 12))
    left.columnconfigure(0, weight=1)
    left.rowconfigure(1, weight=1)

    right = ttk.Frame(body)
    right.grid(row=0, column=1, sticky="nsew")
    right.columnconfigure(0, weight=1)
    right.rowconfigure(1, weight=1)

    lf_conn = ttk.LabelFrame(left, text="\u2460  Connection", padding=(12, 8))
    lf_conn.grid(row=0, column=0, sticky="ew", pady=(0, 8))
    lf_conn.columnconfigure(0, minsize=100)
    lf_conn.columnconfigure(1, weight=1)
    lf_conn.columnconfigure(2, weight=0)

    ttk.Label(lf_conn, text="Serial port", style="FHInnerMuted.TLabel").grid(
        row=0, column=0, sticky="e", padx=(0, 10), pady=3
    )
    port_var = tk.StringVar()
    port_combo = ttk.Combobox(lf_conn, textvariable=port_var, state="readonly", width=22)
    port_combo.grid(row=0, column=1, sticky="ew", pady=3)

    refresh_btn = ttk.Button(lf_conn, text="Refresh", width=11, style=sec_st)
    refresh_btn.grid(row=0, column=2, padx=(10, 0), pady=3)

    ttk.Label(lf_conn, text="Upload baud", style="FHInnerMuted.TLabel").grid(
        row=1, column=0, sticky="e", padx=(0, 10), pady=(4, 3)
    )
    baud_var = tk.StringVar(value="921600")
    baud_line = ttk.Frame(lf_conn)
    baud_line.grid(row=1, column=1, columnspan=2, sticky="ew", pady=(4, 3))
    baud_line.columnconfigure(1, weight=1)
    baud_entry = ttk.Entry(baud_line, textvariable=baud_var, width=12)
    baud_entry.grid(row=0, column=0, sticky="w")
    ttk.Label(baud_line, text="Use 115200 if uploads fail", style="FHInnerMuted.TLabel").grid(
        row=0, column=1, sticky="w", padx=(12, 0)
    )

    lf_fw = ttk.LabelFrame(left, text="\u2461  Firmware source", padding=(12, 8))
    lf_fw.grid(row=1, column=0, sticky="nsew", pady=(0, 8))
    lf_fw.columnconfigure(0, minsize=100)
    lf_fw.columnconfigure(1, weight=1)
    lf_fw.rowconfigure(1, weight=1)

    ttk.Label(lf_fw, text="Board preset", style="FHInnerMuted.TLabel").grid(
        row=0, column=0, sticky="e", padx=(0, 10), pady=3
    )
    preset_display_var = tk.StringVar(value=preset_rows[0][1])
    preset_combo = ttk.Combobox(
        lf_fw,
        textvariable=preset_display_var,
        values=[r[1] for r in preset_rows],
        state="readonly",
        width=24,
    )
    preset_combo.grid(row=0, column=1, sticky="ew", pady=3)

    src_var = tk.StringVar(value="folder")
    flash_nb = ttk.Notebook(lf_fw)
    flash_nb.grid(row=1, column=0, columnspan=2, sticky="nsew", pady=(3, 0))
    tab_local = ttk.Frame(flash_nb, padding=(2, 4))
    tab_local.columnconfigure(0, weight=1)
    tab_local.rowconfigure(1, weight=1)
    tab_remote = ttk.Frame(flash_nb, padding=(2, 4))
    tab_remote.columnconfigure(0, weight=1)
    tab_remote.rowconfigure(1, weight=1)
    tab_help = ttk.Frame(flash_nb, padding=(8, 6))
    tab_help.columnconfigure(0, weight=1)
    tab_help.rowconfigure(0, weight=1)
    howto_text = scrolledtext.ScrolledText(
        tab_help,
        height=12,
        state="normal",
        font=(ui_family, 10),
        wrap=tk.WORD,
        borderwidth=0,
        relief="flat",
        padx=14,
        pady=10,
    )
    howto_text.grid(row=0, column=0, sticky="nsew")
    try:
        howto_text.tag_configure("h1", font=(ui_family, 13, "bold"))
        howto_text.tag_configure("h2", font=(ui_family, 10, "bold"))
    except Exception:
        pass
    _howto_body = (
        "Quick start\n\n"
        "\u2460  Connection — Choose the COM port for your board (often labeled CP210x or USB-SERIAL). "
        "Use Refresh if the list is empty. Unplug/replug USB or install the USB–UART driver if needed.\n\n"
        "\u2461  Board preset — Auto-set when you pick a GitHub release (CYD/v1 → ESP32, v2 → ESP32-S3). "
        "For older single-file releases, choose ESP32-S3 (v2) or ESP32 (CYD & v1) manually.\n\n"
        "\u2462  Firmware source — Pick one of the two flashing tabs:\n"
        "   \u2022  Local export — Folder from Arduino IDE: Sketch → Export compiled Binary.\n"
        "   \u2022  Toolbox / GitHub — Uses bundled bootloader/partitions/boot_app0. "
        "Pick a GitHub release and Download, or Browse to a sketch .bin you already have.\n\n"
        "④  Flash — “Erase entire flash” gives a clean install; leave it off for a normal upgrade. "
        "Press Flash firmware — it runs esptool and shows live output in the activity log.\n\n"
        "Tips — Close Arduino Serial Monitor before flashing. Try baud 115200 if upload fails.\n"
        "Shortcuts: F5 refresh · Ctrl+Enter flash · Ctrl+L clear log · Ctrl+Q exit\n"
    )
    howto_text.insert("1.0", _howto_body)
    howto_text.configure(state="disabled")

    flash_nb.add(tab_local, text="  Local export  ")
    flash_nb.add(tab_remote, text="  Toolbox / GitHub  ")
    flash_nb.add(tab_help, text="  How to use  ")

    frm_folder = ttk.Frame(tab_local)
    frm_folder.grid(row=0, column=0, sticky="ew")
    frm_folder.columnconfigure(0, minsize=96)
    frm_folder.columnconfigure(1, weight=1)
    frm_folder.columnconfigure(2, weight=0)
    ttk.Label(frm_folder, text="Export folder", style="FHMuted.TLabel").grid(
        row=0, column=0, sticky="e", padx=(0, 10), pady=2
    )
    folder_var = tk.StringVar()
    folder_entry = ttk.Entry(frm_folder, textvariable=folder_var)
    folder_entry.grid(row=0, column=1, sticky="ew", pady=2)
    browse_btn = ttk.Button(frm_folder, text="Browse…", width=11, style=sec_st)
    browse_btn.grid(row=0, column=2, padx=(10, 0), pady=2)

    dz_widgets: list[tk.Widget] = []

    def _paint_dropzone(w: tk.Misc, active: bool = False) -> None:
        bg = pal["pill_hover"] if active else pal["surface"]
        border = pal.get("accent_hover", pal["accent"]) if active else pal["border"]
        try:
            w.configure(bg=bg, highlightbackground=border, highlightcolor=border)
            sub = getattr(w, "_dz_sub", None)
            for child in w.winfo_children():
                if isinstance(child, tk.Label):
                    child.configure(
                        bg=bg,
                        fg=pal["muted"] if child is sub else pal["accent"],
                    )
        except tk.TclError:
            pass

    def _register_dropzone(w: tk.Misc) -> None:
        def hover_in(_e: object | None = None) -> None:
            _paint_dropzone(w, active=True)

        def hover_out(_e: object | None = None) -> None:
            _paint_dropzone(w, active=False)

        w.bind("<Enter>", hover_in)
        w.bind("<Leave>", hover_out)

    dz_folder = tk.Frame(
        tab_local,
        bg=pal["surface"],
        highlightthickness=1,
        highlightbackground=pal["border"],
        highlightcolor=pal["border"],
        cursor="hand2",
    )
    dz_folder_icon = tk.Label(
        dz_folder,
        text="\U0001F4C2",
        fg=pal["accent"],
        bg=pal["surface"],
        font=(ui_family, 18),
        cursor="hand2",
    )
    dz_folder_l1 = tk.Label(
        dz_folder,
        text="Select Arduino export folder",
        fg=pal["accent"],
        bg=pal["surface"],
        font=(ui_family, 10, "bold"),
        cursor="hand2",
        anchor="w",
    )
    dz_folder_l2 = tk.Label(
        dz_folder,
        text="Folder with bootloader, partitions, boot_app0, and app .bin",
        fg=pal["muted"],
        bg=pal["surface"],
        font=(ui_family, 9),
        cursor="hand2",
        wraplength=400,
        justify="left",
        anchor="w",
    )
    dz_folder._dz_sub = dz_folder_l2  # type: ignore[attr-defined]
    dz_folder.columnconfigure(0, weight=0)
    dz_folder.columnconfigure(1, weight=1)
    dz_folder.rowconfigure(0, weight=0)
    dz_folder.rowconfigure(1, weight=0)
    dz_folder.rowconfigure(2, weight=1)
    dz_folder_icon.grid(row=0, column=0, rowspan=2, sticky="ns", padx=(14, 6), pady=(12, 10))
    dz_folder_l1.grid(row=0, column=1, sticky="ew", padx=(0, 12), pady=(12, 1))
    dz_folder_l2.grid(row=1, column=1, sticky="ew", padx=(0, 12), pady=(0, 10))
    for _w in (dz_folder, dz_folder_icon, dz_folder_l1, dz_folder_l2):
        _w.bind("<Button-1>", lambda _e: browse())
    dz_folder.grid(row=1, column=0, sticky="nsew", pady=(6, 0))
    dz_widgets.append(dz_folder)
    _register_dropzone(dz_folder)

    frm_app = ttk.Frame(tab_remote)
    frm_app.grid(row=0, column=0, sticky="ew")
    frm_app.columnconfigure(0, minsize=100)
    frm_app.columnconfigure(1, weight=1)
    frm_app.columnconfigure(2, weight=0)
    ttk.Label(frm_app, text="GitHub release", style="FHMuted.TLabel").grid(
        row=0, column=0, sticky="e", padx=(0, 10), pady=3
    )
    release_var = tk.StringVar(value="")
    release_combo = ttk.Combobox(
        frm_app,
        textvariable=release_var,
        state="readonly",
        width=22,
    )
    release_combo.grid(row=0, column=1, sticky="ew", pady=3)
    gh_refresh_btn = ttk.Button(frm_app, text="Refresh list", width=13, style=sec_st)
    gh_refresh_btn.grid(row=0, column=2, padx=(10, 0), pady=3)

    gh_download_btn = ttk.Button(frm_app, text="\u2B07  Download release", width=18, style=sec_st)
    gh_download_btn.grid(row=1, column=1, columnspan=2, sticky="ew", pady=(4, 0))

    ttk.Label(frm_app, text="Sketch app (.bin)", style="FHMuted.TLabel").grid(
        row=2, column=0, sticky="e", padx=(0, 10), pady=(10, 3)
    )
    app_bin_var = tk.StringVar()
    app_bin_entry = ttk.Entry(frm_app, textvariable=app_bin_var)
    app_bin_entry.grid(row=2, column=1, sticky="ew", pady=(10, 3))
    appbrowse_btn = ttk.Button(frm_app, text="Browse\u2026", width=11, style=sec_st)
    appbrowse_btn.grid(row=2, column=2, padx=(10, 0), pady=(10, 3))

    ttk.Label(
        frm_app,
        text="Download sets this path automatically. Browse below only if you already have the .bin.",
        style="FHInnerMuted.TLabel",
        wraplength=520,
    ).grid(row=3, column=0, columnspan=3, sticky="w", pady=(0, 4))

    dz_bin = tk.Frame(
        tab_remote,
        bg=pal["surface"],
        highlightthickness=1,
        highlightbackground=pal["border"],
        highlightcolor=pal["border"],
        cursor="hand2",
    )
    dz_bin_icon = tk.Label(
        dz_bin,
        text="\U0001F4E6",
        fg=pal["accent"],
        bg=pal["surface"],
        font=(ui_family, 18),
        cursor="hand2",
    )
    dz_bin_l1 = tk.Label(
        dz_bin,
        text="Pick sketch .bin (optional if downloaded)",
        fg=pal["accent"],
        bg=pal["surface"],
        font=(ui_family, 10, "bold"),
        cursor="hand2",
        anchor="w",
    )
    dz_bin_l2 = tk.Label(
        dz_bin,
        text="GitHub download fills this automatically. Click to browse a .bin you already have.",
        fg=pal["muted"],
        bg=pal["surface"],
        font=(ui_family, 9),
        cursor="hand2",
        wraplength=400,
        justify="left",
        anchor="w",
    )
    dz_bin._dz_sub = dz_bin_l2  # type: ignore[attr-defined]
    dz_bin.columnconfigure(0, weight=0)
    dz_bin.columnconfigure(1, weight=1)
    dz_bin.rowconfigure(0, weight=0)
    dz_bin.rowconfigure(1, weight=0)
    dz_bin.rowconfigure(2, weight=1)
    dz_bin_icon.grid(row=0, column=0, rowspan=2, sticky="ns", padx=(14, 6), pady=(12, 10))
    dz_bin_l1.grid(row=0, column=1, sticky="ew", padx=(0, 12), pady=(12, 1))
    dz_bin_l2.grid(row=1, column=1, sticky="ew", padx=(0, 12), pady=(0, 10))
    for _w in (dz_bin, dz_bin_icon, dz_bin_l1, dz_bin_l2):
        _w.bind("<Button-1>", lambda _e: browse_app_bin())
    dz_bin.grid(row=1, column=0, sticky="nsew", pady=(6, 0))
    dz_widgets.append(dz_bin)
    _register_dropzone(dz_bin)

    github_release_cache: list[GithubReleaseChoice] = []

    def sync_src_ui(*_args: object) -> None:
        if src_var.get() == "folder":
            dev_source_var.set("Local export")
            folder_entry.configure(state="normal")
            browse_btn.configure(state="normal")
            app_bin_entry.configure(state="disabled")
            appbrowse_btn.configure(state="disabled")
            release_combo.configure(state="disabled")
            gh_refresh_btn.configure(state="disabled")
            gh_download_btn.configure(state="disabled")
        else:
            dev_source_var.set("Bundled + app .bin")
            folder_entry.configure(state="disabled")
            browse_btn.configure(state="disabled")
            app_bin_entry.configure(state="normal")
            appbrowse_btn.configure(state="normal")
            release_combo.configure(state="readonly")
            gh_refresh_btn.configure(state="normal")
            gh_download_btn.configure(state="normal")

    def on_flash_tab(_e: object | None = None) -> None:
        try:
            idx = flash_nb.index(flash_nb.select())
        except tk.TclError:
            return
        if idx == 0:
            src_var.set("folder")
        elif idx == 1:
            src_var.set("bundled")
        # Tab "How to use" (idx 2): keep current src_var; do not switch mode.
        sync_src_ui()
        if idx == 2:
            status_var.set("How to use — switch to Local export or Toolbox / GitHub when you are ready to flash")
        elif src_var.get() == "folder":
            status_var.set("Local export selected - choose the folder with all .bin files")
        else:
            status_var.set("Bundled source selected - choose an app .bin or GitHub release")

    flash_nb.bind("<<NotebookTabChanged>>", lambda _e: on_flash_tab())

    bottom_controls = ttk.Frame(left)
    bottom_controls.grid(row=2, column=0, sticky="ew")
    bottom_controls.columnconfigure(0, weight=1)
    bottom_controls.columnconfigure(1, weight=0)

    lf_dev = ttk.LabelFrame(right, text="Device summary", padding=(12, 8))
    lf_dev.grid(row=0, column=0, sticky="ew", pady=(0, 8))
    lf_dev.columnconfigure(0, minsize=64)
    lf_dev.columnconfigure(1, weight=1)
    for _drow, _dlbl, _dstyle, _dvar in (
        (0, "Board", "FHInner.TLabel", None),
        (1, "Port", "FHInnerStrong.TLabel", None),
        (2, "Baud", "FHInnerStrong.TLabel", None),
        (3, "Source", "FHInnerAccent.TLabel", None),
    ):
        ttk.Label(lf_dev, text=_dlbl, style="FHInnerMuted.TLabel").grid(
            row=_drow, column=0, sticky="e", padx=(0, 12), pady=3
        )
    dev_board_lbl = ttk.Label(
        lf_dev, textvariable=preset_display_var, style="FHInner.TLabel", wraplength=280
    )
    dev_board_lbl.grid(row=0, column=1, sticky="ew", pady=3)
    dev_port_lbl = ttk.Label(lf_dev, textvariable=port_var, style="FHInnerStrong.TLabel", wraplength=280)
    dev_port_lbl.grid(row=1, column=1, sticky="ew", pady=3)
    ttk.Label(lf_dev, textvariable=baud_var, style="FHInnerStrong.TLabel").grid(
        row=2, column=1, sticky="w", pady=3
    )
    dev_source_var = tk.StringVar(value="Local export")
    ttk.Label(lf_dev, textvariable=dev_source_var, style="FHInnerAccent.TLabel").grid(
        row=3, column=1, sticky="w", pady=3
    )

    lf_opt = ttk.LabelFrame(bottom_controls, text="\u2462  Flash options", padding=(12, 8))
    lf_opt.grid(row=0, column=0, sticky="nsew", padx=(0, 8))
    lf_opt.columnconfigure(0, weight=1)
    erase_var = tk.BooleanVar(value=False)
    erase_chk = ttk.Checkbutton(
        lf_opt,
        text="Erase entire flash before write (clean install)",
        variable=erase_var,
    )
    erase_chk.grid(row=0, column=0, sticky="w", pady=2)

    lf_log = ttk.LabelFrame(right, text="Activity log", padding=(12, 8))
    lf_log.grid(row=1, column=0, sticky="nsew", pady=0)
    lf_log.columnconfigure(0, weight=1)
    lf_log.rowconfigure(1, weight=1)

    log_bar = ttk.Frame(lf_log)
    log_bar.grid(row=0, column=0, sticky="ew", pady=(0, 4))
    log_bar.columnconfigure(1, weight=1)
    ttk.Label(log_bar, text="Live output", style="FHInnerMuted.TLabel").grid(row=0, column=0, sticky="w")

    try:
        log_font = tkfont.Font(family="Cascadia Code", size=9)
    except tk.TclError:
        try:
            log_font = tkfont.Font(family="Consolas", size=9)
        except tk.TclError:
            log_font = tkfont.Font(family="TkFixedFont", size=9)

    log_widget = scrolledtext.ScrolledText(
        lf_log,
        height=12,
        state="disabled",
        font=log_font,
        wrap=tk.NONE,
        borderwidth=0,
        relief="flat",
        padx=10,
        pady=8,
    )
    log_widget.grid(row=1, column=0, sticky="nsew", pady=(4, 0))

    def sync_log_theme() -> None:
        cfg = dict(
            background=pal["log_bg"],
            foreground=pal["log_fg"],
            insertbackground=pal["log_fg"],
            selectbackground=pal["sel_bg"],
            selectforeground=pal["sel_fg"],
            highlightthickness=1,
            highlightbackground=pal["border"],
            highlightcolor=pal["accent"],
        )
        for w in (log_widget, howto_text):
            try:
                w.configure(**cfg)
            except tk.TclError:
                pass
        try:
            log_widget.tag_configure("success", foreground=pal.get("success", "#22c55e"))
            log_widget.tag_configure("error", foreground=pal.get("error", "#ef4444"))
            log_widget.tag_configure("accent", foreground=pal["accent"])
        except tk.TclError:
            pass

    def sync_dropzone_theme() -> None:
        for w in dz_widgets:
            try:
                if isinstance(w, tk.Frame):
                    _paint_dropzone(w, active=False)
                elif isinstance(w, tk.Label):
                    w.configure(
                        bg=pal["surface"],
                        fg=pal["muted"],
                        highlightbackground=pal["border"],
                        highlightcolor=pal["border"],
                    )
            except tk.TclError:
                pass

    sync_log_theme()

    def clear_log() -> None:
        log_widget.configure(state="normal")
        log_widget.delete("1.0", "end")
        log_widget.configure(state="disabled")
        status_var.set("Log cleared")

    clear_btn = ttk.Button(log_bar, text="Clear log", command=clear_log, style=sec_st)
    clear_btn.grid(row=0, column=2, sticky="e")

    actions = ttk.LabelFrame(bottom_controls, text="\u2463  Flash", padding=(12, 8))
    actions.grid(row=0, column=1, sticky="nsew")
    actions.columnconfigure(0, weight=1)

    flash_btn = ttk.Button(actions, text="\u26A1  Flash firmware", style=prim_st, width=18)
    flash_btn.grid(row=0, column=0, sticky="ew")

    progress = ttk.Progressbar(actions, mode="indeterminate", length=190)
    progress.grid(row=1, column=0, sticky="ew", pady=(8, 0))

    footer = ttk.Frame(main)
    footer.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(6, 0))
    footer.columnconfigure(0, weight=1)
    footer.columnconfigure(1, weight=0)

    status_var = tk.StringVar(value="Ready")
    status = ttk.Label(footer, textvariable=status_var, style="FHMuted.TLabel")
    status.grid(row=0, column=0, sticky="ew")

    hint = "F5 refresh  \u00B7  Ctrl+Enter flash  \u00B7  Ctrl+L clear log  \u00B7  Ctrl+Q exit"
    hint_lbl = ttk.Label(footer, text=hint, wraplength=560, justify="right", style="FHMuted.TLabel")
    hint_lbl.grid(row=0, column=1, sticky="e", padx=(16, 0))
    sync_src_ui()

    def on_main_configure(_evt: object | None = None) -> None:
        try:
            left_w = left.winfo_width()
            right_w = right.winfo_width()
            if right_w > 1:
                hint_lbl.configure(wraplength=max(280, min(520, right_w - 24)))
            for dz in (dz_folder, dz_bin):
                dz_w = dz.winfo_width() or left_w
                if dz_w > 80:
                    sub = getattr(dz, "_dz_sub", None)
                    if isinstance(sub, tk.Label):
                        sub.configure(wraplength=max(180, min(left_w - 36, dz_w - 28)))
            dw = lf_dev.winfo_width() or right_w
            if dw > 96:
                wl = max(96, min(right_w - 28, dw - 28))
                dev_board_lbl.configure(wraplength=wl)
                dev_port_lbl.configure(wraplength=wl)
        except tk.TclError:
            pass

    main.bind("<Configure>", lambda e: on_main_configure(e))

    def set_fh_theme(variant: str) -> None:
        nonlocal fh_variant, pal
        fh_variant = variant
        pal.clear()
        pal.update(_FH_PALETTE[variant])
        _fh_apply_clam_theme(root, style, pal)
        outer.configure(bg=pal["canvas"])
        header.configure(bg=pal["shell"])
        ht_left.configure(bg=pal["shell"])
        title_row.configure(bg=pal["shell"])
        title_lbl.configure(fg=pal["accent"], bg=pal["shell"])
        sub_lbl.configure(fg=pal["muted"], bg=pal["shell"])
        ver_lbl.configure(fg=pal["muted"], bg=pal["pill_bg"])
        links_fr.configure(bg=pal["shell"])
        header_extras.configure(bg=pal["shell"])
        for lb in link_labs:
            lb.configure(fg=pal["accent"], bg=pal["pill_bg"])
        for lb in extra_link_labs:
            lb.configure(fg=pal["muted"], bg=pal["pill_bg"])
        accent_bar.configure(bg=pal["accent"])
        set_windows_titlebar_dark(variant == "dark")
        sync_log_theme()
        sync_dropzone_theme()

    add_header_link("Dark", lambda: set_fh_theme("dark"))
    add_header_link("Light", lambda: set_fh_theme("light"))
    add_header_link("Keys", show_shortcuts)
    add_header_link("About", show_about)
    add_header_link("Exit", root.destroy)

    busy_ev = threading.Event()

    def preset_key_from_ui() -> str:
        lab = preset_display_var.get()
        for key, label in preset_rows:
            if label == lab:
                return key
        return preset_rows[0][0]

    def set_preset_key(key: str) -> None:
        if key not in PRESETS:
            return
        label = PRESET_LABELS.get(key, key)
        preset_display_var.set(label)

    def maybe_apply_preset_for_bin(path_or_name: str, *, announce: bool = True) -> None:
        inferred = infer_preset_from_app_bin(path_or_name)
        if not inferred:
            return
        if preset_key_from_ui() == inferred:
            return
        set_preset_key(inferred)
        if announce:
            name = Path(path_or_name).name
            status_var.set(f"Board preset → {PRESET_LABELS[inferred]} (matches {name})")

    def refresh_ports() -> None:
        status_var.set("Refreshing serial ports...")
        paths = list_serial_ports()
        names = [x[0] for x in paths]
        port_combo["values"] = names
        cur = normalize_serial_port(port_var.get())
        if paths:
            if not cur or cur not in names:
                canon_ok, canon = resolve_serial_port(cur) if cur else (False, "")
                if canon_ok and canon in names:
                    port_var.set(canon)
                else:
                    port_var.set(paths[0][0])
            status_var.set(f"Found {len(paths)} serial port(s) - ready to flash")
        else:
            port_var.set("")
            status_var.set("No serial ports - check USB cable and drivers")

    refresh_btn.configure(command=refresh_ports)

    def browse() -> None:
        p = filedialog.askdirectory(
            title="Select folder containing firmware .bin files",
            parent=root,
        )
        if p:
            folder_var.set(p)
            status_var.set(f"Selected export folder: {Path(p).name or p}")

    browse_btn.configure(command=browse)

    def browse_app_bin() -> None:
        p = filedialog.askopenfilename(
            title="Select application firmware (.bin)",
            parent=root,
            filetypes=[("Firmware binary", "*.bin"), ("All files", "*.*")],
        )
        if p:
            app_bin_var.set(p)
            maybe_apply_preset_for_bin(p)
            status_var.set(f"Selected app binary: {Path(p).name}")

    appbrowse_btn.configure(command=browse_app_bin)

    def selected_github_choice() -> Optional[GithubReleaseChoice]:
        sel = release_var.get().strip()
        for c in github_release_cache:
            if c.label == sel:
                return c
        return None

    def apply_github_release_rows(rows: list[GithubReleaseChoice]) -> None:
        github_release_cache.clear()
        github_release_cache.extend(rows)
        labels = [r.label for r in rows]
        release_combo["values"] = labels
        if labels:
            release_combo.current(0)
            release_var.set(labels[0])
            status_var.set(f"Loaded {len(labels)} GitHub firmware choice(s)")
        else:
            release_var.set("")
            status_var.set("No releases returned from GitHub")

    def refresh_github_releases() -> None:
        if busy_ev.is_set():
            status_var.set("Another operation is already running")
            return

        def worker() -> None:

            def ol(line: str) -> None:
                append_log(line)

            try:
                ol("Fetching releases from GitHub API...")
                rows = github_list_release_choices(per_page=40)
                ol(f"Found {len(rows)} firmware choice(s).")
                root.after(0, lambda: apply_github_release_rows(rows))
            except Exception as ex:
                msg = str(ex)

                def err(m: str = msg) -> None:
                    status_var.set("Could not load GitHub releases")
                    messagebox.showerror("GitHub releases", m, parent=root)

                root.after(0, err)
            finally:
                def restore() -> None:
                    busy_ev.clear()
                    sync_src_ui()

                root.after(0, restore)

        status_var.set("Fetching GitHub releases...")
        busy_ev.set()
        gh_refresh_btn.configure(state="disabled")
        threading.Thread(target=worker, daemon=True).start()

    gh_refresh_btn.configure(command=refresh_github_releases)

    def download_github_selected() -> None:
        if busy_ev.is_set():
            status_var.set("Another operation is already running")
            return
        choice = selected_github_choice()
        if not choice:
            messagebox.showwarning(
                "GitHub release",
                "Refresh the release list first, then choose a row in the dropdown.",
                parent=root,
            )
            return
        if not choice.download_url:
            messagebox.showwarning(
                "GitHub release",
                f"{choice.tag_name!r} has no firmware .bin asset on GitHub. Pick another tag.",
                parent=root,
            )
            return

        def dl_worker() -> None:
            try:

                def ol(line: str) -> None:
                    append_log(line)

                path = download_github_firmware_asset(
                    choice.tag_name,
                    choice.asset_name,
                    choice.download_url,
                    ol,
                )
                ps = str(path)

                def done() -> None:
                    app_bin_var.set(ps)
                    maybe_apply_preset_for_bin(path.name)
                    status_var.set(f"Downloaded {path.name}")

                root.after(0, done)
            except Exception as ex:
                msg = str(ex)

                def err(m: str = msg) -> None:
                    status_var.set("Download failed - see error")
                    messagebox.showerror("Download", m, parent=root)

                root.after(0, err)
            finally:
                def restore() -> None:
                    busy_ev.clear()
                    sync_src_ui()

                root.after(0, restore)

        status_var.set(f"Downloading {choice.asset_name}...")
        busy_ev.set()
        gh_download_btn.configure(state="disabled")
        threading.Thread(target=dl_worker, daemon=True).start()

    gh_download_btn.configure(command=download_github_selected)

    def on_release_selected(_evt: object | None = None) -> None:
        choice = selected_github_choice()
        if choice and choice.asset_name:
            maybe_apply_preset_for_bin(choice.asset_name)

    release_combo.bind("<<ComboboxSelected>>", on_release_selected)

    def append_log(line: str) -> None:
        log_q.put(line)

    def _log_tag_for(line: str) -> str:
        low = line.lower()
        if "error" in low or "fail" in low or "traceback" in low:
            return "error"
        if "success" in low or "done" in low or "saved:" in low or "verified" in low:
            return "success"
        if line.startswith("---") or line.startswith("==="):
            return "accent"
        return ""

    def pump_log_queue() -> None:
        try:
            while True:
                line = log_q.get_nowait()
                tag = _log_tag_for(line)
                log_widget.configure(state="normal")
                if tag:
                    log_widget.insert("end", line + "\n", tag)
                else:
                    log_widget.insert("end", line + "\n")
                log_widget.see("end")
                log_widget.configure(state="disabled")
        except queue.Empty:
            pass
        root.after(120, pump_log_queue)

    form_widgets_readonly = (port_combo, preset_combo)
    form_widgets_normal = (
        baud_entry,
        folder_entry,
        refresh_btn,
        browse_btn,
        app_bin_entry,
        appbrowse_btn,
        release_combo,
        gh_refresh_btn,
        gh_download_btn,
        erase_chk,
        clear_btn,
        flash_nb,
    )

    def set_form_busy(on: bool) -> None:
        ro = "disabled" if on else "readonly"
        for w in form_widgets_readonly:
            w.configure(state=ro)
        st = "disabled" if on else "normal"
        for w in form_widgets_normal:
            w.configure(state=st)
        try:
            if on:
                flash_nb.state(["disabled"])
            else:
                flash_nb.state(["!disabled"])
        except tk.TclError:
            pass
        if not on:
            sync_src_ui()

    def do_flash() -> None:
        if busy_ev.is_set():
            return
        port = port_var.get().strip()
        if not port:
            status_var.set("Choose a serial port before flashing")
            messagebox.showwarning(
                "Serial port",
                "Choose a serial port first. Use Refresh if the list is empty.",
                parent=root,
            )
            return
        try:
            baud = int(baud_var.get().strip())
            if baud <= 0:
                raise ValueError
        except ValueError:
            status_var.set("Invalid baud rate")
            messagebox.showwarning(
                "Baud rate",
                "Enter a positive integer (921600 or 115200 are common).",
                parent=root,
            )
            return
        pkey = preset_key_from_ui()
        preset_note = ""
        try:
            if src_var.get() == "folder":
                fw = folder_var.get().strip().strip('"').strip("'")
                if not fw or not Path(fw).is_dir():
                    status_var.set("Choose a valid firmware export folder")
                    messagebox.showwarning(
                        "Firmware folder",
                        "Choose the folder that contains the exported bootloader, partitions, boot_app0, and app .bin files.",
                        parent=root,
                    )
                    return
                resolved = resolve_firmware_dir(Path(fw), preset_key=pkey)
            else:
                ap = app_bin_var.get().strip().strip('"').strip("'")
                if not ap or not Path(ap).is_file():
                    status_var.set("Choose an application .bin before flashing")
                    messagebox.showwarning(
                        "Sketch app (.bin)",
                        "Download a release above (path fills automatically), or browse for a sketch .bin you already have.",
                        parent=root,
                    )
                    return
                app_path = Path(ap)
                use_pkey = effective_bundled_preset(pkey, app_path)
                if use_pkey != pkey:
                    set_preset_key(use_pkey)
                    preset_note = (
                        f"Board preset adjusted to {PRESET_LABELS[use_pkey]} "
                        f"(matches {app_path.name})."
                    )
                resolved = resolve_bundled_plus_app(use_pkey, app_path)
        except Exception as ex:
            status_var.set("Firmware source could not be resolved")
            messagebox.showerror("Firmware", str(ex), parent=root)
            return

        clear_log()
        status_var.set("Preparing flash command...")
        if preset_note:
            append_log(preset_note)
        append_log(f"Folder used: {resolved.bootloader.parent}")
        append_log(f"Bootloader : {resolved.bootloader.name}")
        append_log(f"Partitions : {resolved.partition_table.name}")
        append_log(f"boot_app0  : {resolved.boot_app0.name}")
        append_log(f"App        : {resolved.app.name}")
        append_log("---")

        def worker() -> None:
            busy_ev.set()
            root.after(0, lambda: progress.start(14))
            root.after(0, lambda: status_var.set("Flashing - do not unplug"))
            root.after(0, lambda: flash_btn.configure(state="disabled"))
            root.after(0, set_form_busy, True)

            def on_line(line: str) -> None:
                append_log(line)

            rc = flash_firmware(
                port=port,
                baud=baud,
                images=resolved,
                erase_all=erase_var.get(),
                on_line=on_line,
            )
            append_log("---")
            append_log(f"Done (exit code {rc}).")

            def finish_ui() -> None:
                progress.stop()
                busy_ev.clear()
                flash_btn.configure(state="normal")
                set_form_busy(False)
                if rc == 0:
                    status_var.set("Flash completed successfully")
                    messagebox.showinfo("Flash complete", "Firmware was written successfully.", parent=root)
                else:
                    status_var.set("Flash failed - see log")
                    messagebox.showerror(
                        "Flash failed",
                        "esptool reported an error. Review the activity log for details.",
                        parent=root,
                    )

            root.after(0, finish_ui)

        threading.Thread(target=worker, daemon=True).start()

    flash_btn.configure(command=do_flash)

    def bind_shortcuts() -> None:
        root.bind_all("<F5>", lambda e: (refresh_ports(), "break")[1])
        root.bind_all("<Control-Return>", lambda e: (do_flash(), "break")[1])
        root.bind_all("<Control-L>", lambda e: (clear_log(), "break")[1])
        root.bind_all("<Control-l>", lambda e: (clear_log(), "break")[1])
        root.bind_all("<Control-q>", lambda e: (root.destroy(), "break")[1])
        root.bind_all("<Control-Q>", lambda e: (root.destroy(), "break")[1])

    bind_shortcuts()
    refresh_ports()
    pump_log_queue()
    root.after_idle(center_window)
    root.after_idle(on_main_configure)
    port_combo.focus_set()
    root.mainloop()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--version",
        action="version",
        version=f"%(prog)s {__version__}",
        help="Show version and exit",
    )
    parser.add_argument("--gui", action="store_true", help="Open graphical interface (default if no CLI args)")
    parser.add_argument("--port", "-p", help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baud", "-b", type=int, default=921600)
    parser.add_argument("--preset", choices=list(PRESETS.keys()), default="esp32s3")
    fw_src = parser.add_mutually_exclusive_group(required=False)
    fw_src.add_argument(
        "--firmware-folder",
        "-d",
        type=Path,
        help="Folder with Arduino export (bootloader, partitions, sketch .bin)",
    )
    fw_src.add_argument(
        "--bundled-app-bin",
        type=Path,
        metavar="PATH",
        help="Application .bin only; uses bootloader/partitions from bundled/<preset>/ or bundled/",
    )
    fw_src.add_argument(
        "--download-latest-app",
        action="store_true",
        help="Download application .bin from GitHub into cache (latest or --github-tag)",
    )
    parser.add_argument(
        "--github-tag",
        metavar="TAG",
        help="With --download-latest-app, use this release tag (e.g. v1.5.3) instead of latest",
    )
    parser.add_argument("--erase", action="store_true", help="Erase full flash before write")
    parser.epilog = (
        "CLI flashing: pass --port and exactly one of -d, --bundled-app-bin, --download-latest-app."
        " Use --github-tag TAG with --download-latest-app to pick a release."
    )
    args = parser.parse_args()

    cli_flash = args.port is not None and (
        args.firmware_folder is not None
        or args.bundled_app_bin is not None
        or args.download_latest_app
    )

    if not cli_flash and not args.gui and len(sys.argv) == 1:
        _gui()
        return

    if not cli_flash:
        _gui()
        return

    n = sum(
        [
            args.firmware_folder is not None,
            args.bundled_app_bin is not None,
            args.download_latest_app,
        ]
    )
    if n != 1:
        parser.error(
            "With --port use exactly one of: --firmware-folder (-d), "
            "--bundled-app-bin, --download-latest-app"
        )

    def on_line(line: str) -> None:
        print(line, flush=True)

    if args.firmware_folder is not None:
        images = resolve_firmware_dir(Path(args.firmware_folder), preset_key=args.preset)
    elif args.bundled_app_bin is not None:
        app_path = Path(args.bundled_app_bin)
        preset = effective_bundled_preset(args.preset, app_path)
        if preset != args.preset:
            on_line(
                f"Using preset {preset!r} ({PRESET_LABELS[preset]}) inferred from {app_path.name}."
            )
        images = resolve_bundled_plus_app(preset, app_path)
    else:
        if args.github_tag:
            app_path = download_tagged_release_app_bin(args.github_tag, on_line)
        else:
            app_path = download_latest_release_app_bin(on_line)
        preset = effective_bundled_preset(args.preset, app_path)
        if preset != args.preset:
            on_line(
                f"Using preset {preset!r} ({PRESET_LABELS[preset]}) inferred from {app_path.name}."
            )
        images = resolve_bundled_plus_app(preset, app_path)

    rc = flash_firmware(
        port=args.port,
        baud=args.baud,
        images=images,
        erase_all=args.erase,
        on_line=on_line,
    )
    raise SystemExit(rc)


if __name__ == "__main__":
    main()
