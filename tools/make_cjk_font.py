#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build a CKF bitmap font covering every non-ASCII character used by the
translation tables. Run locally: the generated bitmap is derived from a font
installed on your machine and is NOT redistributed with this project.

    python tools/make_cjk_font.py --strings i18n/zh-cn/strings.tsv \
        --font C:/Windows/Fonts/simhei.ttf --out i18n/zh-cn/font.ckf

Requires Pillow: python -m pip install pillow
For redistributable packs prefer an SIL OFL font (Noto Sans SC / LXGW WenKai).
"""

import argparse
import struct
import sys
from pathlib import Path

MAGIC = b"D2KCKF1\0"


def collect_chars(paths):
    chars = set()
    for raw in paths:
        p = Path(raw)
        if not p.is_file():
            raise SystemExit(f"missing translation table: {p}")
        for line in p.read_text(encoding="utf-8-sig").splitlines():
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t", 1)
            if len(parts) != 2:
                continue
            value = parts[1].replace("\\n", "").replace("\\\\", "")
            for ch in value:
                if ord(ch) > 127:
                    chars.add(ch)
    return sorted(chars, key=ord)


def default_font():
    import os
    windir = os.environ.get("WINDIR", r"C:\Windows")
    for name in ("simhei.ttf", "msyh.ttc", "simsun.ttc"):
        p = Path(windir) / "Fonts" / name
        if p.is_file():
            return str(p)
    return None


def render(chars, font_path, size, cell, advance, baseline, offset):
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        raise SystemExit("Pillow is required: python -m pip install pillow")
    font = ImageFont.truetype(font_path, size)
    out = []
    for ch in chars:
        img = Image.new("L", (cell, cell), 0)
        draw = ImageDraw.Draw(img)
        draw.text((offset, offset), ch, font=font, fill=255, anchor="lt")
        alpha = img.tobytes()
        out.append((ord(ch), advance, alpha))
    return out


def main():
    ap = argparse.ArgumentParser(description="Generate a CKF font from translation tables")
    ap.add_argument("--strings", nargs="+", default=["i18n/zh-cn/strings.tsv"])
    ap.add_argument("--font", default=None, help="TTF/TTC path (default: a Windows CJK font)")
    ap.add_argument("--out", default="i18n/zh-cn/font.ckf")
    ap.add_argument("--size", type=int, default=13, help="font size in px")
    ap.add_argument("--cell", type=int, default=16, help="bitmap cell width and height")
    ap.add_argument("--advance", type=int, default=14, help="pen advance for each glyph")
    ap.add_argument("--baseline", type=int, default=0, help="extra y offset when drawing")
    ap.add_argument("--offset", type=int, default=1, help="text inset inside the cell")
    args = ap.parse_args()

    chars = collect_chars(args.strings)
    if not chars:
        raise SystemExit("no non-ASCII characters found in the translation tables")
    font_path = args.font or default_font()
    if not font_path or not Path(font_path).is_file():
        raise SystemExit("no CJK font found; pass --font <path-to-ttf>")
    glyphs = render(chars, font_path, args.size, args.cell, args.advance, args.baseline, args.offset)

    buf = bytearray(MAGIC)
    buf += struct.pack("<HHHH", args.cell, args.cell, args.baseline, 0)
    buf += struct.pack("<I", len(glyphs))
    for cp, advance, alpha in glyphs:
        buf += struct.pack("<IH", cp, advance)
        buf += alpha
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_bytes(bytes(buf))
    print(f"font: {font_path} @ {args.size}px, cell {args.cell}x{args.cell}")
    print(f"glyphs: {len(glyphs)}  ->  {args.out} ({len(buf)} bytes)")
    print("chars: " + "".join(chars[:80]) + (" ..." if len(chars) > 80 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
