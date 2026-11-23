"""Decodes Data\\bin\\Templates.bin (1.06) with the field names worked out
in docs/re/08-templates.md, and prints the DATA.R16 entry ranges of the
unit / building graphic elements.

Usage: python re/dump_templates.py [gamedata/1.06]
"""
import os
import struct
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else 'gamedata/1.06'

# Blocks in file order: (exe address, size). See docs/formats/templates.md.
BLOCKS = [
    (0x77e250, 0x3c00), (0x6dc540, 0x68b0), (0x782068, 0x700), (0x781e58, 0x200),
    (0x782770, 0x168), (0x7951d0, 0x168), (0x795528, 4), (0x795538, 1),
    (0x795534, 4), (0x6d5fd0, 0x100), (0x795544, 1), (0x795530, 4),
    (0x795340, 0x100), (0x795540, 1), (0x79552c, 4), (0x6e5a60, 0x1e0),
    (0x79553c, 1), (0x6f9908, 0xafc8), (0x6db390, 0xc80), (0x7943a8, 0xc80),
    (0x7891b0, 0x6978), (0x6da7d8, 0xbb8), (0x787e08, 0x1388), (0x6d60d8, 0x56),
    (0x6e5750, 0x100), (0x7954b0, 0x64), (0x714a98, 0x64), (0x6e5850, 1),
    (0x6d9b50, 1),
]

data = open(os.path.join(ROOT, 'Data', 'bin', 'Templates.bin'), 'rb').read()
off = {}
pos = 0
for addr, size in BLOCKS:
    off[addr] = pos
    pos += size
assert pos == len(data), 'unexpected Templates.bin size'


def u8(a, i=0):
    return data[off[a] + i]


def u32(a, i=0):
    return struct.unpack_from('<I', data, off[a] + 4 * i)[0]


def names(a, count, size=50):
    o = off[a]
    return [data[o + i * size:o + (i + 1) * size].split(b'\0')[0].decode('latin1')
            for i in range(count)]


def houses(mask):
    return ''.join(c for bit, c in enumerate('AHOEFSMX') if mask & (1 << bit)) or '-'


unit_names = names(0x6da7d8, 60)
building_names = names(0x787e08, 100)
bullet_names = names(0x6db390, 64)
explosion_names = names(0x7943a8, 64)
n_unit_records = u8(0x795538)
n_building_records = u8(0x79553c)


def bname(i):
    return 'none' if i < 0 or i >= len(building_names) else building_names[i]


def bullet(i):
    return '-' if i == 0xff else bullet_names[i]


def expl(i):
    return '-' if i == 0xff else explosion_names[i]


print(f'== {n_unit_records} unit records')
for i in range(n_unit_records):
    r = data[off[0x77e250] + i * 0x100:][:0x100]
    s32 = lambda o: struct.unpack_from('<i', r, o)[0]
    print(f'{i:2} {unit_names[r[1]]:14} houses={houses(r[0]):8} hp={s32(4):5} '
          f'speed={s32(8) / 1024:6.2f} armour={r[2]} cost={s32(0x1c):5} buildtime={s32(0x20):4} '
          f'tech={r[0x24]} view={r[0x10]} range?={r[0x0e]} infantry={r[0x11]} kind={r[0x2d]} '
          f'weapons={bullet(r[0x0c])}/{bullet(r[0x0d])} death={expl(r[0x2f])} '
          f'needs={bname(s32(0x28))},{bname(s32(0x30))} gfx={s32(0x14)}/{s32(0x18)}')

print(f'== {n_building_records} building records')
for i in range(n_building_records):
    r = data[off[0x6dc540] + i * 0x10c:][:0x10c]
    s32 = lambda o: struct.unpack_from('<i', r, o)[0]
    costs = [s32(0x1c + 4 * k) for k in range(4)]
    times = [s32(0x2c + 4 * k) for k in range(4)]
    techs = list(r[0x10:0x14])
    print(f'{i:2} {building_names[r[0x88]]:20} houses={houses(r[4]):8} hp={s32(0):5} armour={r[5]} '
          f'power={s32(0x3c):5} view={r[0x0e]} tech/cost/time={list(zip(techs, costs, times))[:2]} '
          f'needs={bname(s32(0x40))}({houses(r[0x44])}),{bname(s32(0x48))}({houses(r[0x4c])}) '
          f'size={r[0x87]}x{r[0x86]} footprint={s32(0x50):04x} kind={r[0x5c]} '
          f'weapons={bullet(r[0x0c])}/{bullet(r[0x0d])} death={expl(r[0x4f])} gfx={s32(0x14)}/{s32(0x18)}')

# DATA.R16 entry ranges (FUN_004606d0 reads every entry in this order).
FIXED = 9 + 6 + 2 + 10 + 11 + 64 + 8 + 4 + 64 + 16 + 12
print(f'== DATA.R16: {FIXED} fixed entries (UI, craters, ...) first')
idx = FIXED
n_unit_elements = u32(0x795528)
for e in range(n_unit_elements):
    frames, rots = u32(0x782770, e), u32(0x7951d0, e)
    print(f'unit element {e:2}: entries {idx}..{idx + frames * rots - 1} '
          f'({frames} frames x {rots} rotations, frame-major)')
    idx += frames * rots
n_building_elements = u32(0x79552c)
for e in range(n_building_elements):
    frames = u32(0x6e5a60, e)
    print(f'building element {e:2}: base {idx}, frames {idx + 1}..{idx + frames}, '
          f'damaged {idx + 1 + frames}..{idx + 2 * frames}')
    idx += 1 + 2 * frames
print(f'bullets start at {idx}')
