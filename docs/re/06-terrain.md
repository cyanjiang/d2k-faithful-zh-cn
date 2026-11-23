# Terrain: map set-up, spice tiles, drawing (1.06)

Code: `src/sim/game_map.cpp`, `src/sim/exe_tables.cpp`, `src/sim/rng.hpp`.
Formats: `docs/formats/maps.md`.

## Game map

`gGameMap` (`0x517df0`) holds 12-byte cells, with a row-offset table at
`0x547df0`. Each cell has:

| Off | Field | Notes |
|---:|---|---|
| +0 | u16 tile | |
| +4 | u32 flags | on load: the map's special value |
| +8 | u8 state | set to 0x11 on load; low nibble 1 = cell not drawn |
| +9 | u8 marks | high nibble = crater / damage overlay index |

**Known flag bits:**
- 0x700000: spice level, 0–7;
- 0x10000: sand;
- 0x8000 / 0x4000 / 0x2000: build / infantry / vehicles;
- 0x1000: cell object (crate or bloom);
- 0x10 and bits 0–2: a building and its owner colour (radar, `FUN_00462140`);
- 0x8, 0x3E0: unit occupancy (`FUN_0044e120`).

**Mission set-up (`FUN_00469790`).** For every cell:
1. `special = map special`;
2. `flags = TILEATR[tile]`;
3. the special is interpreted (see the formats doc).

After that, `FUN_0044ec50` smooths spice over the whole map, and units and
buildings are placed.

## Spice tiles (`FUN_0044ec50`, also called after harvesting a 3×3 area)

For each cell in the rect (half-open, clipped to the map):

**Thin spice (level 1 or 2).** Build an 8-neighbour mask of the neighbours
that have spice (level ≠ 0):
- the bits are NW 1, N 2, NE 4, W 8, E 0x10, SW 0x20, S 0x40, SE 0x80;
- cells outside the map count as spice (x = 0 → 0x29, right edge → 0x94,
  bottom → 0xE0, top → 7);
- `tile = table[0x4e0c18][mask]`, 256 u16 values in the range 0x2EE–0x31F.

**Thick spice (level ≥ 3).**
`tile = table[0x4e0b10][pattern[(y & 7)·8 + (x & 7)]]`. That table holds
0x12C, 0x12D, 0x140, 0x141. The 8×8 `pattern` (`0x790510`) is filled once
with `rand() & 3` during data set-up (`FUN_00466140`).

**Level 0.** Left as is.

**Harvesting** (in the unit update `FUN_00497b90`) lowers the level one
step:
- 2 → 1: tile 0x319;
- 1 → 0: a random sand tile from `0x4e9520`;
- 3 → 2: 0x319;
- 5 → 4 and 4 → 3: the tile stays;
- 6 / 7 → 2: 0x2ED.

It then re-smooths the 3×3 area around the cell.

## RNG

The RNG is MSVC `rand()` (`FUN_004b1810`, state `0x4ed870`):
`s = s·214013 + 2531011; return (s >> 16) & 0x7FFF`. Single player seeds it
with the clock (`FUN_004b1e60`); multiplayer uses the host's seed.

## Drawing (`FUN_00423d20`)

**Area.** The scroll position is in pixels (`0x798538` / `0x79853c`). The
view starts at y = `0x4e41dc` on screen and is `0x786d48` × `0x6dc538`
pixels. TODO: read those values. That size is the visible map area, a
gameplay property.

**Per visible cell:** unless `state & 0xF == 1`, blit `tile[cell.tile]`
(32×32). If `marks > 0x10`, also draw a crater:
- the image comes from the table at `0x6d7be8` (concrete) or `0x6d7bec`
  (sand), indexed by `(marks >> 4) + 16·(parity of x + y)`;
- it is jittered by the tables at `0x502ed8` / `0x503f88` and drawn with
  blit flags (1,1).
