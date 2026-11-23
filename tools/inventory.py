#!/usr/bin/env python3
"""Inventory a Dune 2000 data folder: path, size, SHA-256, guessed type.

    inventory.py GAMEDIR [--csv OUT.csv]

Prints a per-extension summary and writes a CSV with one row per file.
The CSV contains only names/sizes/hashes (no game data) so it is safe to
commit as docs/inventory-1.06.csv for reproducibility.
"""
import csv
import hashlib
import os
import sys
from collections import defaultdict


def guess_type(path, head):
    ext = os.path.splitext(path)[1].upper()
    if head[:2] == b"MZ":
        return "pe-executable" if ext in (".EXE", ".DAT", ".DLL") else "mz"
    if head[:4] == b"FORM" and head[8:12] == b"WVQA":
        return "vqa"
    if head[:4] == b"RIFF" and head[8:12] == b"WAVE":
        return "wav"
    return {
        ".AUD": "westwood-aud?", ".R8": "r8-sprites?", ".R16": "r16-sprites?",
        ".MAP": "map?", ".MIS": "mission?", ".BIN": "bin-table?",
        ".UIL": "ui-layout?", ".UIB": "ui-binary?", ".FNT": "font?",
        ".FPL": "font-palette?", ".TXT": "text", ".CFG": "text-config?",
        ".INI": "ini", ".DOC": "doc", ".HTM": "html",
    }.get(ext, "unknown")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    root = argv[1]
    out_csv = argv[argv.index("--csv") + 1] if "--csv" in argv else None
    rows = []
    for dirpath, _, files in os.walk(root):
        for f in files:
            p = os.path.join(dirpath, f)
            with open(p, "rb") as fh:
                data = fh.read()
            rel = os.path.relpath(p, root).replace(os.sep, "/")
            rows.append((rel, len(data), hashlib.sha256(data).hexdigest(),
                         guess_type(p, data[:16])))
    rows.sort(key=lambda r: r[0].lower())
    if out_csv:
        with open(out_csv, "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["path", "size", "sha256", "guessed_type"])
            w.writerows(rows)
    summary = defaultdict(lambda: [0, 0])
    for rel, size, _, t in rows:
        top = rel.split("/")[0] if "/" in rel else "."
        key = (top, os.path.splitext(rel)[1].upper() or "(none)", t)
        summary[key][0] += 1
        summary[key][1] += size
    print(f"{'dir':<12}{'ext':<8}{'type':<16}{'count':>6}{'bytes':>14}")
    for (top, ext, t), (n, b) in sorted(summary.items()):
        print(f"{top:<12}{ext:<8}{t:<16}{n:>6}{b:>14}")
    print(f"total: {len(rows)} files, {sum(r[1] for r in rows)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
