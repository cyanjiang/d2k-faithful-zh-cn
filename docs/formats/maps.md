# Maps and missions

Code: `src/formats/map.cpp`. RE: `docs/re/06-terrain.md`.

## .MAP (`MISSIONS\<name>.MAP`, `Data\maps\*.MAP`)

LoadMapData (`FUN_0044c6d0`) reads:
- u16 width, u16 height (each must be ≤ 128);
- then `width × height` cells, row-major, each `u16 tile, u16 special`.

There is no header beyond the size. `tile` indexes the mission's tileset
(`Data\<tileset>.R16`: 800 entries of 32×32, 16-bit). `special` values:

| Value | Meaning (`FUN_00469790`) |
|---|---|
| 0 | nothing |
| 1 | thin spice. Tile → 0x319, flags → 0x216000 (spice level 2) |
| 2 | thick spice. Tile → 0x2ED, flags → 0x416000 (level 4) |
| 3 | error |
| 0x17 | player start position (multiplayer; up to 8) |
| 0x29–0x2D | spice bloom spot. Tile → 0x319, flags → 0x217000, plus a bloom object |
| 0x3C–0x3E | concrete for side 7: 2×2 (mask 0x33), 3×3 (0x777), 4×4 (0xFFFF), laid by `FUN_00456210` (0x469c12). The set-up first requires TILEDATA[special] ≠ −1, and 1.06's TILEDATA gives −1 for all three, so they place nothing (only TEST.MAP uses 0x3E) |
| other | TILEDATA.BIN (`0x6d7de8`, loaded by `FUN_00465e60`). Unless it gives −1, a unit or building to place (docs/formats/templates.md) |

## .MIS (`MISSIONS\_<name>.MIS`)

The file is a fixed 68,066-byte dump that the mission loader
(`FUN_0044f8e0`) reads straight into globals, in this order (script and
end of mission: `docs/re/19-mission-script.md`):

| Off | Size | Global | Meaning |
|---:|---:|---|---|
| 0 | 0x50 | `0x6b87c8` | tech level per player slot (8 bytes), credits (8 × u32 at +8); bytes 0x28–0x4F are zero in 1.06 and never read (scan of `0x6b87c8 … 0x6b8818`) |
| 0x50 | 8 | `0x8cd4f0` | house of each player slot |
| 0x58 | 0xEDC0 | `0x4f4118` | AI data, 8 × 0x1DB8: +0 side, +1 AI active, rest: `[AI]` fields (doc 19 §1.5) |
| 0xEE18 | 0x40 | `0x798830` | 8×8 alliances [side][other]: 0 friendly, 1 hostile, 2 neutral |
| 0xEE58 | 0x1200 | `0x6b7058` | events, 64 × 0x48 (doc 19 §1.3) |
| 0x10058 | 0x540 | `0x6b8280` | conditions, 48 × 0x1C (doc 19 §1.2) |
| 0x10598 | 200 | `0x5166a0` | tileset name, e.g. `BLOXTREE` |
| 0x10660 | 200 | `0x5165c8` | tile attribute file, e.g. `TILEATR1` → `Data\bin\TILEATR1.BIN` |
| 0x10728 | 1 + 1 + 4 | `0x6b63cc`, `0x6b8260`, `0x6b8270` | u8 event count, u8 condition count, i32 mission timer (−1 = none) |
| 0x1072E | 0x2B4 | `0x504030` | message-line ring (doc 11 §2); zero in all 1.06 missions |

`_TESTH.MIS` and `_TESTO.MIS` are 74,210-byte leftovers in an older layout
that the game never loads.

## TILEATR*.BIN (`Data\bin`)

The file holds 800 × u32 attribute flags, then 800 × u32 text ids. Known
flags:

| Flag | Meaning |
|---|---|
| 0x8000 | build on |
| 0x4000 | infantry |
| 0x2000 | vehicles |
| 0x10000 | sand |
| bits 29–31 | a 3-bit class |

The text form is `<tileatr>.dat`, read when the debug flag `0x4eb014` is
off. The retail game reads `bin\<tileatr>.bin`.
