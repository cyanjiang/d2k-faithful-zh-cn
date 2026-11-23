# Rules: Templates.bin and TILEDATA.BIN (`Data\bin`)

Code: `src/formats/templates.cpp`.

## Templates.bin (133,140 bytes)

The file is a dump of exe globals in a fixed order. It is read by the data
set-up `FUN_00466140`; the writer mirror is at `0x4691e4`.

Field layouts: `docs/re/08-templates.md`.

| # | Exe address | Bytes | Content |
|---:|---|---:|---|
| 1 | `0x77e250` | 0x3C00 | unit records, 60 × 0x100 (30 used) |
| 2 | `0x6dc540` | 0x68B0 | building records, 100 × 0x10C (62 used) |
| 3 | `0x782068` | 0x700 | bullet records, 64 × 28 (docs/re/18 §1.1) |
| 4 | `0x781e58` | 0x200 | explosion attributes, 64 × 8 (+1 blink mask, +4 sound) |
| 5 | `0x782770` | 0x168 | unit graphic element frame counts (u32) |
| 6 | `0x7951d0` | 0x168 | unit graphic element rotation counts (u32) |
| 7 | `0x795528` | 4 | unit graphic element count (29) |
| 8 | `0x795538` | 1 | unit record count (30) |
| 9 | `0x795534` | 4 | explosion count (60) |
| 10 | `0x6d5fd0` | 0x100 | explosion frame counts (u32 × 64) |
| 11 | `0x795544` | 1 | explosion count (byte) |
| 12 | `0x795530` | 4 | bullet count (34) |
| 13 | `0x795340` | 0x100 | bullet frame counts (u32 × 64) |
| 14 | `0x795540` | 1 | bullet count (byte) |
| 15 | `0x79552c` | 4 | building graphic element count (65) |
| 16 | `0x6e5a60` | 0x1E0 | building element frame counts (u32 × 120) |
| 17 | `0x79553c` | 1 | building record count (62) |
| 18 | `0x6f9908` | 0xAFC8 | building record graphic names, 100 × 0x1C2 |
| 19 | `0x6db390` | 0xC80 | bullet names, 64 × 50 |
| 20 | `0x7943a8` | 0xC80 | explosion names, 64 × 50 |
| 21 | `0x7891b0` | 0x6978 | unit record graphic names, 60 × 0x1C2 |
| 22 | `0x6da7d8` | 0xBB8 | unit names, 60 × 50 |
| 23 | `0x787e08` | 0x1388 | building names, 100 × 50 |
| 24 | `0x6d60d8` | 0x56 | special type / explosion ids at fixed offsets (docs/re/08) |
| 25 | `0x6e5750` | 0x100 | explosion default draw flags, 64 × u32 |
| 26 | `0x7954b0` | 0x64 | building animation-2 frame counts per record |
| 27 | `0x714a98` | 0x64 | building animation-1 frame counts per record |
| 28 | `0x6e5850` | 1 | building name count (24) |
| 29 | `0x6d9b50` | 1 | unit name count (26) |

Names are type ids; records are per-house variants (house mask and name
index), chosen per player by `FUN_0046c570` / `FUN_0046c450`.

**Unit types (1.06):**
0 LIGHT_INF, 1 TROOPER, 2 ENGINEER, 3 THUMPER, 4 SARDAUKAR, 5 TRIKE,
6 RAIDER, 7 QUAD, 8 HARVESTER, 9 COMBAT_TANK, 10 MCV, 11 MISSILE_TANK,
12 DEVIATOR, 13 SIEGE_TANK, 14 SONIC_TANK, 15 DEVASTATOR, 16 CARRYALL,
17 ORNITHOPTER, 18 FREMEN, 19 NSFREMEN, 20 SABOTEUR, 21 DEATH_HAND,
22 SANDWORM, 23 FRIGATE, 24 GRENADIER, 25 STEALTH_RAIDER.

**Building types:**
0 CONSTRUCTION_YARD, 1 CONCRETE, 2 CONC2, 3 WIND_TRAP, 4 BARRACKS,
5 SIETCH, 6 WALL, 7 REFINERY, 8 MEDIUM_GUN_TURRET, 9 OUTPOST,
10 LARGE_GUN_TURRET, 11 AT_HIGH_TECH_FACTORY, 12 HIGH_TECH_FACTORY,
13 LIGHT_FACTORY, 14 SILO, 15 HEAVY_FACTORY, 16 STAR_PORT, 17 REPAIR_PAD,
18 RESEARCH_CENTRE, 19 ATREIDES_PALACE, 20 HARKONNEN_PALACE,
21 ORDOS_PALACE, 22 IMPERIAL_PALACE, 23 SP_OUTPOST.

## TILEDATA.BIN (4,000 bytes)

The file holds 1000 × i32, indexed by a `.MAP` special value; it is loaded
by `FUN_00465e60`. Its text source `tiledata.dat` lists name, owner and so
on.
- −1 means no object.
- Otherwise: bit 31 is set for a building, bits 16–23 give the owner
  (player slot), and the low byte is the type index.
- Mission set-up (`FUN_00469790`) handles specials 0, 1, 2, 3, 0x17 and
  0x29–0x2D itself before looking up this table. Specials 0x3C–0x3E become
  the fixed building codes 0x800000C8–CA.
