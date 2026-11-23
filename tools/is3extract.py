#!/usr/bin/env python3
"""Extract an InstallShield 3 .Z archive (e.g. SETUP\\SETUP.Z on the Dune 2000 CD).

Format notes: docs/formats/installshield3.md
Compression is PKWARE DCL "implode"; the decoder below is written from the
published format description (same scheme as zlib's contrib/blast).

Usage:
    is3extract.py ARCHIVE.Z            # list
    is3extract.py ARCHIVE.Z OUTDIR     # extract
"""
import os
import struct
import sys

SIGNATURE = 0x8C655D13

# ---------------------------------------------------------------- DCL explode

# Compact code-length tables: each byte = (repeat-1) << 4 | bit length.
_LIT_LEN = bytes([
    11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8,
    9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24, 12, 11, 7, 9, 11, 12, 6, 7, 22, 5,
    7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12,
    8, 12, 5, 38, 5, 38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27,
    44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61, 12, 45,
    44, 173])
_LEN_LEN = bytes([2, 35, 36, 53, 38, 23])
_DIST_LEN = bytes([2, 20, 53, 230, 247, 151, 248])
_LEN_BASE = (3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264)
_LEN_EXTRA = (0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8)


def _build_table(compact):
    """Return a dict mapping (nbits, code) -> symbol for a canonical code.

    DCL stores code bits inverted and MSB-first in an LSB-first stream, so we
    key on the code as the decoder accumulates it.
    """
    lengths = []
    for b in compact:
        lengths += [b & 15] * ((b >> 4) + 1)
    table = {}
    code = 0
    for nbits in range(1, 14):
        for sym, ln in enumerate(lengths):
            if ln == nbits:
                table[(nbits, code)] = sym
                code += 1
        code <<= 1
    return table


_LIT_TABLE = _build_table(_LIT_LEN)
_LEN_TABLE = _build_table(_LEN_LEN)
_DIST_TABLE = _build_table(_DIST_LEN)


class _Bits:
    __slots__ = ("data", "pos", "buf", "cnt")

    def __init__(self, data):
        self.data, self.pos, self.buf, self.cnt = data, 0, 0, 0

    def bits(self, n):
        while self.cnt < n:
            if self.pos >= len(self.data):
                raise ValueError("DCL stream truncated")
            self.buf |= self.data[self.pos] << self.cnt
            self.pos += 1
            self.cnt += 8
        v = self.buf & ((1 << n) - 1)
        self.buf >>= n
        self.cnt -= n
        return v

    def decode(self, table):
        code = 0
        for nbits in range(1, 14):
            code = (code << 1) | (self.bits(1) ^ 1)
            sym = table.get((nbits, code))
            if sym is not None:
                return sym
        raise ValueError("DCL bad huffman code")


def explode(data):
    s = _Bits(data)
    coded_lit = s.bits(8)
    dict_bits = s.bits(8)
    if coded_lit > 1 or not 4 <= dict_bits <= 6:
        raise ValueError(f"DCL bad header lit={coded_lit} dict={dict_bits}")
    out = bytearray()
    while True:
        if s.bits(1):
            sym = s.decode(_LEN_TABLE)
            length = _LEN_BASE[sym] + s.bits(_LEN_EXTRA[sym])
            if length == 519:
                break
            shift = 2 if length == 2 else dict_bits
            dist = (s.decode(_DIST_TABLE) << shift) + s.bits(shift) + 1
            if dist > len(out):
                raise ValueError("DCL distance too far back")
            start = len(out) - dist
            for i in range(length):  # may overlap, copy bytewise
                out.append(out[start + i])
        else:
            out.append(s.decode(_LIT_TABLE) if coded_lit else s.bits(8))
    return bytes(out)


# ------------------------------------------------------------- IS3 container

def read_index(blob):
    sig, = struct.unpack_from("<I", blob, 0)
    if sig != SIGNATURE:
        raise ValueError("not an InstallShield 3 archive")
    file_count, = struct.unpack_from("<H", blob, 12)
    archive_size, = struct.unpack_from("<I", blob, 18)
    dir_off, = struct.unpack_from("<I", blob, 41)
    dir_count, = struct.unpack_from("<H", blob, 49)
    if archive_size != len(blob):
        raise ValueError(f"size mismatch: header {archive_size}, file {len(blob)}")

    dirs, p = [], dir_off
    for _ in range(dir_count):
        nfiles, entry_size, name_len = struct.unpack_from("<HHH", blob, p)
        name = blob[p + 6:p + 6 + name_len].decode("latin-1")
        dirs.append((name, nfiles))
        p += entry_size

    # Files are listed grouped by directory, in directory order.
    files = []
    for dname, nfiles in dirs:
        for _ in range(nfiles):
            usize, csize, off = struct.unpack_from("<III", blob, p + 3)
            date, time = struct.unpack_from("<HH", blob, p + 15)
            entry_size, = struct.unpack_from("<H", blob, p + 23)
            name_len = blob[p + 29]
            name = blob[p + 30:p + 30 + name_len].decode("latin-1")
            files.append(dict(dir=dname, name=name, usize=usize, csize=csize,
                              off=off, date=date, time=time))
            p += entry_size
    if len(files) != file_count:
        raise ValueError(f"file count mismatch: {len(files)} vs {file_count}")
    return files


def dos_datetime(date, time):
    return (f"{(date >> 9) + 1980:04d}-{(date >> 5) & 15:02d}-{date & 31:02d} "
            f"{time >> 11:02d}:{(time >> 5) & 63:02d}:{(time & 31) * 2:02d}")


def main(argv):
    if len(argv) not in (2, 3):
        print(__doc__)
        return 2
    with open(argv[1], "rb") as f:
        blob = f.read()
    files = read_index(blob)
    outdir = argv[2] if len(argv) == 3 else None
    for i, e in enumerate(files):
        rel = os.path.join(e["dir"].replace("\\", os.sep), e["name"])
        print(f"{dos_datetime(e['date'], e['time'])} {e['usize']:>10} "
              f"{e['csize']:>10}  {rel}", flush=True)
        if outdir:
            data = explode(blob[e["off"]:e["off"] + e["csize"]])
            if len(data) != e["usize"]:
                raise ValueError(f"{rel}: got {len(data)} bytes, want {e['usize']}")
            path = os.path.join(outdir, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(data)
    print(f"{len(files)} files", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
