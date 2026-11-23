r"""Renders frame 0 of each of the 33 cursor types in Data\MOUSE.R16
(entry = type * 8 + frame, FUN_004626a0) into a labelled sheet.
Usage: python re/mouse_sheet.py <gamedata> <out.png>"""
import struct
import sys
import zlib

root, out = sys.argv[1], sys.argv[2]
d = open(root + '/Data/MOUSE.R16', 'rb').read()
ents, pos, last_pal = [], 0, None
while pos < len(d):
    t = d[pos]
    if t == 0:
        ents.append(None); pos += 1; continue
    w, h, ox, oy, ih, ph = struct.unpack_from('<IIiiII', d, pos + 1)
    bpp = d[pos + 25]
    p = pos + 29
    px = d[p:p + w * h * (bpp // 8)] if ih else b''
    if ih:
        p += w * h * (bpp // 8)
    if t == 1 and ph:
        last_pal = struct.unpack_from('<256H', d, p + 8); p += 8 + 512
    ents.append((w, h, ox, oy, bpp, px, last_pal)); pos = p
print('entries', len(ents))
cell = 56
W, H = 11 * cell, 3 * cell
img = [[(40, 40, 40)] * W for _ in range(H)]
for t in range(33):
    e = ents[t * 8]
    if not e:
        continue
    w, h, ox, oy, bpp, px, pal = e
    print(f'type {t:2}: {w}x{h} offset {ox},{oy} bpp {bpp}')
    bx, by = (t % 11) * cell, (t // 11) * cell
    for y in range(min(h, cell)):
        for x in range(min(w, cell)):
            if bpp == 8:
                i = px[y * w + x]
                if i == 0:
                    continue
                v = pal[i] if pal else 0x7fff
            else:
                v = struct.unpack_from('<H', px, 2 * (y * w + x))[0]
            img[by + y][bx + x] = ((v >> 10 & 31) * 8, (v >> 5 & 31) * 8, (v & 31) * 8)
raw = b''.join(b'\0' + bytes(c for p_ in row for c in p_) for row in img)
chunk = lambda t, b: struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
open(out, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
                      + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
