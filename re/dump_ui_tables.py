"""Dumps the exe's UI script tables (commands, enum constants, globals).

Usage: python re/dump_ui_tables.py <DUNE2000.DAT> <outdir>
Writes ui_commands.tsv, ui_enums.tsv and ui_globals.tsv. The table
addresses are for 1.06.
"""
import re
import struct
import sys


def load(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    n = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    base = struct.unpack_from('<I', d, pe + 52)[0]
    secs = []
    for i in range(n):
        o = pe + 24 + opt + 40 * i
        vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8)
        secs.append((va, ro, rs))

    def off(v):
        v -= base
        for va, ro, rs in secs:
            if va <= v < va + rs:
                return ro + v - va
        raise ValueError(hex(v + base))
    return d, off


def cstr(b):
    return b.split(b'\0')[0].decode('latin1')


def main():
    d, off = load(sys.argv[1])
    out = sys.argv[2]
    # Commands: 229 x 0xBC {u32 id, name, ..., u8 init_flag at +0xB8}.
    with open(f'{out}/ui_commands.tsv', 'w') as f:
        o = off(0x4beb88)
        for i in range(0xe5):
            e = o + i * 0xbc
            f.write(f'{struct.unpack_from("<I", d, e)[0]:#x}\t{cstr(d[e+4:e+0xb8])}\t{d[e+0xb8]}\n')
    # Enum constants: 0x40-byte records {name[60], u32 value}, from the first
    # known record until names stop looking like identifiers.
    def records(start, size, valid):
        o = off(start)
        while valid(d[o:o + size]):
            yield d[o:o + size]
            o += size
    def ident(r):
        name = r[:0x3c].split(b'\0')[0]
        return (len(name) > 2 and name[:1].isalpha() and all(32 < c < 127 for c in name)
                and struct.unpack_from('<I', r, 0x3c)[0] < 0x10000)
    with open(f'{out}/ui_enums.tsv', 'w') as f:
        for r in records(0x4ca820, 0x40, ident):
            f.write(f'{cstr(r[:0x3c])}\t{struct.unpack_from("<I", r, 0x3c)[0]}\n')

    # Globals: records with the name at +0 and the variable address at +0x3C.
    # The record stride varies, so scan for "g<Name>\0" in the table area.
    lo, hi = off(0x4c8000), off(0x4cb000)
    seen = set()
    with open(f'{out}/ui_globals.tsv', 'w') as f:
        for m in re.finditer(rb'g[A-Z][A-Za-z0-9_]{2,40}\0', d[lo:hi]):
            name = m.group()[:-1].decode()
            addr = struct.unpack_from('<I', d, lo + m.start() + 0x3c)[0]
            if 0x400000 < addr < 0x800000 and name not in seen:
                seen.add(name)
                f.write(f'{name}\t{addr:#x}\n')


if __name__ == '__main__':
    main()
