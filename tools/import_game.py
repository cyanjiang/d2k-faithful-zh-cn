#!/usr/bin/env python3
"""Build an installed-equivalent Dune 2000 1.06 folder from the original CD
image and the official patch, without running either installer.

    import_game.py DUNE2000.iso d2k106us.exe OUTDIR

Steps:
  1. Extract SETUP\\SETUP.Z (InstallShield 3) from the ISO; unpack its DUNE\\
     directory into OUTDIR (this is what the installer copies to disk).
  2. Copy MOVIES\\, MUSIC\\, MISSIONS\\ from the ISO into OUTDIR (normally
     read from the CD at runtime; we keep them alongside for convenience).
  3. Overlay the 1.06 patch (a self-extracting archive) onto OUTDIR.

Needs 7-Zip (7z on PATH or in Program Files) for the ISO and patch.
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import is3extract  # noqa: E402


def find_7z():
    for c in (shutil.which("7z"), r"C:\Program Files\7-Zip\7z.exe",
              r"C:\Program Files (x86)\7-Zip\7z.exe"):
        if c and os.path.exists(c):
            return c
    sys.exit("7-Zip not found")


def run7z(sevenz, *args):
    subprocess.run([sevenz, *args], check=True, stdout=subprocess.DEVNULL)


def overlay(src, dst):
    """Copy src tree onto dst, matching existing names case-insensitively."""
    for root, _, files in os.walk(src):
        rel = os.path.relpath(root, src)
        target = dst
        if rel != ".":
            for part in rel.split(os.sep):
                existing = next((d for d in os.listdir(target)
                                 if d.lower() == part.lower()
                                 and os.path.isdir(os.path.join(target, d))), part)
                target = os.path.join(target, existing)
                os.makedirs(target, exist_ok=True)
        for f in files:
            existing = next((x for x in os.listdir(target) if x.lower() == f.lower()), f)
            shutil.copy2(os.path.join(root, f), os.path.join(target, existing))
            print(f"  patch: {os.path.join(rel, f)}")


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    iso, patch, out = argv[1:]
    if os.path.exists(out) and os.listdir(out):
        sys.exit(f"{out} exists and is not empty")
    sevenz = find_7z()
    with tempfile.TemporaryDirectory() as tmp:
        print("[1/3] SETUP.Z")
        run7z(sevenz, "e", "-y", f"-o{tmp}", iso, r"SETUP\SETUP.Z")
        with open(os.path.join(tmp, "SETUP.Z"), "rb") as f:
            blob = f.read()
        for e in is3extract.read_index(blob):
            d = e["dir"]
            if d != "DUNE" and not d.startswith("DUNE\\"):
                continue  # API\ = Westwood Online support files, not the game
            rel = os.path.join(*d.split("\\")[1:], e["name"])
            data = is3extract.explode(blob[e["off"]:e["off"] + e["csize"]])
            assert len(data) == e["usize"], rel
            path = os.path.join(out, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(data)

        print("[2/3] CD folders")
        run7z(sevenz, "x", "-y", f"-o{out}", iso, "MOVIES", "MUSIC", "MISSIONS")

        print("[3/3] 1.06 patch")
        pdir = os.path.join(tmp, "patch")
        run7z(sevenz, "x", "-y", f"-o{pdir}", patch)
        overlay(pdir, out)
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
