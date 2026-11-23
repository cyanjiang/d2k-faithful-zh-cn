# Unit / building templates and their graphics (1.06)

Data: `Data\bin\Templates.bin` (`docs/formats/templates.md`). Validation
dump: `python re/dump_templates.py`.

**Sources.** Field meaning comes from the developer-mode text loaders in
the data set-up `FUN_00466140`, which run when the debug flag `0x4eb014` is
clear:
- `UnitAtrb.dat`: record parser `FUN_00463990`, plus the caller around
  `0x468100`;
- `BuilAtrb.dat`: parser `FUN_00464960`, caller around `0x467d20`;
- `Graphics.dat` / `bGraphic.dat` for the graphic elements;
- `BullAtrb.dat`, `ExplAtrb.dat`.

The text columns are unnamed and read in a fixed order. **Each field below
is settled by its readers in the game code** (addresses = reading
instructions, found by a scan of every `.text` access into the record
tables, `0x77e250 + rec·0x100` and `0x6dc540 + rec·0x10C`, plus the
pointer walks such as `0x46c1b8`; the text loaders `0x463000…0x469400`
excluded). Fields the parser fills but nothing reads are marked
**unused**.

## Names, records and house variants

- **Unit and building names** (blocks `0x6da7d8` / `0x787e08`, counts
  `0x6d9b50` = 26 / `0x6e5850` = 24) are the type ids. The `.MAP` objects
  (TILEDATA) and the AI refer to these.
- **Records** (counts `0x795538` = 30 units / `0x79553c` = 62 buildings)
  are per-house variants. A record holds its name index (unit +0x01,
  building +0x88) and a **house mask** (unit +0x00, building +0x04).
- **House mask** (`FUN_004656e0`): letters in the text map to bits, A 1,
  H 2, O 4, E 8 (Emperor), F 16 (Fremen), S 32 (Smugglers),
  M 64 (Mercenaries), X 128. The value 127 means all houses.
- **Record lookup** (units `FUN_0046c570`, buildings `FUN_0046c450`):
  1. Scan all records whose name index matches.
  2. The first match is the default.
  3. A later match whose mask contains bit `1 << side` replaces it; the
     last such match wins.

  `side` is the player's house (player +0x24253; .MIS bytes 0x50..0x57,
  0 = Atreides, 1 = Harkonnen, 2 = Ordos, …). An optional "disallowed" list
  per player makes the lookup return 0xFF.

## Unit record (0x100 bytes, `0x77e250`)

| Off | Type | Meaning | Readers / evidence |
|---:|---|---|---|
| 0x00 | u8 | house mask | `FUN_004656e0`, lookups `FUN_0046c570` |
| 0x01 | u8 | name index (type id) | `0x46c5bc`, `0x494ba8` |
| 0x02 | u8 | armour type (index into the armour names at `0x786d90`) | `0x49d8d4`, `0x49ea38` (doc 18) |
| 0x03 | u8 | turn delay: ticks between body facing steps | `0x455982`, `0x493f27`, `0x4954e9` (doc 09) |
| 0x04 | i32 | **hit points** | 21 readers, e.g. `0x455958` |
| 0x08 | i32 | **speed** × 1024 (the text value << 10) | `0x4559e7`, `0x4952b5` (doc 09) |
| 0x0C | u8 | primary weapon (bullet index, 0xFF = none) | 25 readers, e.g. `0x45532b` |
| 0x0D | u8 | secondary weapon | `0x495e07`, `0x4964aa` |
| 0x0E | u8 | reload delay in ticks (not a range) | `0x41e2ee`, `0x455994`, `0x495fc0`, `0x49f71e` (doc 18) |
| 0x0F | u8 | turret turn delay (the parser writes +0x03 − 1 when column 26 is 0) | `0x45598b`, `0x493fb7` |
| 0x10 | u8 | **view distance** (0..7; the parser checks this) | `0x44d056` |
| 0x11 | u8 | infantry flag | 52 readers |
| 0x14 | i32 | **base graphic element** | `0x42922f`, `0x42a6a6` |
| 0x18 | i32 | **turret graphic element**, −1 = none | `0x429365`, `0x495cb3` |
| 0x1C | i32 | **cost** | `0x442bcb`, `0x4556f9` |
| 0x20 | i32 | build rate: progress per tick out of 0x5A00 (doc 11 §6) | `0x4469f0` |
| 0x24 | u8 | tech level | `0x46c1f5` |
| 0x25 | u8 | offered by the Starport | `0x46e806` (doc 34) |
| 0x26 | u8 | has a turret | `0x49421d`, `0x4948dd`, `0x495a2a` |
| 0x27 | u8 | upgrade level needed of prerequisite 1 | `0x46c1da` (doc 11) |
| 0x28 | i32 | prerequisite building 1 (name index, −1 = none) | `0x46c1bf`, `0x442a70` |
| 0x2C | u8 | house mask for prerequisite 1 | `0x46c1cc`, `0x442dbe` |
| 0x2D | u8 | special kind (1 HARVESTER, 2 CARRYALL, 3 ENGINEER, 4 SABOTEUR, 5 SANDWORM, 6 MCV, 7 DEVASTATOR, 8 FRIGATE, 9 ORNITHOPTER, 10 DEATH_HAND, 11 SARDAUKAR, 12 FREMEN, 13 FR Non Stealth, 14 THUMPER, 16 Deviator) | 113 readers |
| 0x2E | u8 | **unused** (1 for infantry; parser only) | none |
| 0x2F | u8 | death explosion (0xFF = none) | `0x494e2c`, `0x49ac07` |
| 0x30 | i32 | prerequisite building 2 | `0x46c1e3` |
| 0x34 | u8 | **unused** (parser only) | none |
| 0x35 | u8 | crushes infantry | `0x4200cf`, `0x45d9a7`, `0x4a5a34` |
| 0x36 | u8 | selection bracket size 0..5 | `0x429317`, `0x42b1d6` |
| 0x37 | u8 | muzzle offset (pixels along the facing) | `0x495ca3` |
| 0x38 | u32 | initial object flags (0x8000 from text column 26) | `0x455965` |
| 0x3C–0x5B | u8[32] | facing (0..31) → rotation frame of the base element | `0x4291fd`, `0x429379` |
| 0x5C–0x7F | u32[9] | select voices, `[house·3 + tick % 3]` | `0x49f2d4` |
| 0x80–0xA3 | u32[9] | order voices | `0x445321`, `0x49f2f2` |
| 0xA4 | u32 | voice priority | `0x49f295` |
| 0xA8 | u8 | muzzle flash explosion (0xFF = none) | `0x495ca9` |
| 0xA9 | u8 | speed class (0 infantry, 1 tracked, 2 wheeled, 3 flying) | `0x49f42c` (doc 09) |
| 0xAA | u8 | skirmish / multiplayer only | `0x46c20a` (doc 11) |

## Building record (0x10C bytes, `0x6dc540`)

| Off | Type | Meaning | Readers / evidence |
|---:|---|---|---|
| 0x00 | i32 | **hit points** | 20 readers, e.g. `0x45642e` |
| 0x04 | u8 | house mask | `0x46bf34`, `0x4a60a7` |
| 0x05 | u8 | armour type | `0x49d531`, `0x49ea80` |
| 0x06 | u8 | turret turn delay (copied to object +0x36) | `0x456437`, `0x494039` |
| 0x07 | u8 | reload delay (copied to object +0x37) | `0x456440`, `0x496452` |
| 0x08 | i32 | ≠ 0: its destruction shakes the screen (0 for concrete / wall / silo) | `0x49caeb` |
| 0x0C, 0x0D | u8 | primary / secondary weapon | `0x49604a`, `0x4961a8` |
| 0x0E | u8 | view distance | `0x44e363` |
| 0x0F | u8 | can fire | `0x4960f2` |
| 0x10–0x13 | u8[4] | tech level for build and up to 3 upgrades | `0x46c118`, `0x427b72`, `0x44323e` |
| 0x14 | i32 | **base graphic element** | 10 readers, e.g. `0x427e1e` |
| 0x18 | i32 | second graphic element (turret top), −1 = none | `0x42903b`, `0x4585ac` |
| 0x1C–0x2B | i32[4] | **cost** for build / upgrades | `0x442c05` |
| 0x2C–0x3B | i32[4] | build rate for build / upgrades | `0x446859`, `0x446c01` |
| 0x3C | i32 | **power** (positive uses, negative produces) | `0x422d14`, `0x46ca4a` |
| 0x40 / 0x44 | i32 / u8 | prerequisite 1 (name index, −1) / its house mask | `0x46c0fa` / `0x46c107` |
| 0x45 | u8 | upgrade level needed of prerequisite 1 | `0x46c12c` (doc 11) |
| 0x48 / 0x4C | i32 / u8 | prerequisite 2 / its house mask | `0x46c135` / `0x46c141` |
| 0x4D | u8 | upgrade level needed of prerequisite 2 | `0x46c14f` |
| 0x4E | u8 | needs power to work (LARGE_GUN_TURRET 1) | `0x422d79`, `0x496128` |
| 0x4F | u8 | death explosion | `0x49ca4d` |
| 0x50 | u32 | 4×4 cell mask (bit = row·4 + col): drawn size; concrete is laid on these cells (`FUN_00456210`) | 7 readers |
| 0x54 | u32 | 4×4 mask of occupied cells (`FUN_004563b0`) | 15 readers |
| 0x58 | u32 | initial object flags (0x4000 scenery, 0x400000 Fremen barracks / sietch: no concrete hit-point rule, 0x40 / 0x10 animation, 0x200000 draw-list key − 32 px, 0x800000 active animation drawn additive, docs/formats/r8.md) | 18 readers, `FUN_0042aa90`, `0x429135` |
| 0x5C | u8 | special kind (1 OUTPOST, 2 CONSTRUCTION, 3 WINDTRAP — not matched for "WIND_TRAP", 4 REFINERY, 5 SILO?, 6 REPAIR, 7 LIGHT_FACTORY, 8 HEAVY_FACTORY, 9/10 high tech, 11 BARRACKS, 12 ?, 13 STAR_PORT, 14 WALL, 15 CONCRETE, 16 TURRET, 17–19 palaces) | 54 readers |
| 0x5D | u8 | the AI's sell order in a power shortage | `0x422e04` (doc 29) |
| 0x5E | u8 | **unused** (parser only) | none |
| 0x5F | u8 | selection bracket size 0..5 | `0x428fe3` |
| 0x60 / 0x61 | i8 | factory spawn cell (turret offset for turrets) | 10 readers each |
| 0x62 / 0x63 | i8 | first move cell after leaving | `0x442e60`, `0x49e8e7` |
| 0x64–0x83 | u8[32] | facing → frame of the second element | `0x429052`, `0x496291` |
| 0x85 | u8 | animation 2 frame divisor (rate) | `0x427fc6`, `0x42910b` |
| 0x86 / 0x87 | u8 | height / width in pixels, from the 0x50 mask (rows / cols × 32) | 28 / 7 readers |
| 0x88 | u8 | name index | 38 readers |
| 0x89 | u8 | build-up frame from which the base image shows | `0x427eb5`, `0x4290bb`, `0x49b9ad` |
| 0x8A | u8 | record whose animation-1 frames (build-up) are used | `0x429093`, `0x49b82e` |
| 0x8B | u8 | record whose animation-2 frames (active) are used | `0x427fad`, `0x4290f6` |
| 0x8C | u8 | muzzle flash explosion (MUZ_GTUR for the medium turret) | `0x49624b` |

**Building hit points at creation** (`FUN_004563b0`):
`hp = hp/2 + hp × cells_on_concrete / (2 × footprint cells)`. If not every
cell is on concrete, the object gets flag 0x8000. The 0x400000 types skip
this rule.

## Graphics in DATA.R16 (`FUN_004606d0`)

DATA.R16 is read **sequentially**; each entry goes through
`SetTImageToResource`. A type-2 entry takes the last type-1 palette. All
6,555 entries are accounted for:

1. **206 fixed entries:**
   - 9 singles;
   - groups of 6, 2, 10, 11, 64, 8, 4 (UI pieces, mouse, …);
   - **craters**: 64 entries, **not** interleaved (corrected per
     docs/re/10-ingame-input.md): entries 114–145 are the 32 concrete
     craters (`0x6d7be8[i·2]`), 146–177 the 32 sand craters (`0x6d7bec[i·2]`);
   - 16 and 12 more.
2. **Unit graphic elements** (`0x795528` = 29). For element e, frame f
   (0..`0x782770[e]`−1) and rotation r (0..`0x7951d0[e]`−1), in that nesting
   order, the image goes to `0x71aca0[(e·35 + f)·32 + r]`. Infantry have
   34 frames × 8 rotations, vehicles 1 × 32, ORNITHOPTER 3 × 32,
   DEATH HAND 1 × 2, FRIGATE 1 × 1.
3. **Building graphic elements** (`0x79552c` = 65). For element e:
   - one image to `0x6e5880[e]` (base, often empty);
   - n = `0x6e5a60[e]` frames to `0x6e5c40[e·32 + f]`;
   - n damaged frames to `0x7907a8[e·32 + f]`.

   Walls have 16 frames (connections), silos 4, turret tops 32 rotations.
4. **Bullets** (`0x795540` = 34): `0x795340[b]` frames each.
5. **Explosions** (`0x795534` = 60): `0x6d5fd0[x]` frames each.
6. **Unit icons:** one per unit record (`0x6d9260[record]`).
7. 10 singles (`0x6e5858`).
8. **Building icons:** one per building record (`0x787c78`).
9. One single (`0x789194`).
10. **Building animation 1**, per record: `0x714a98[rec]` frames
    (`0x782aa8 + rec·0xA8`).
11. **Building animation 2**, per record: `0x7954b0[rec]` frames
    (`0x716b00 + rec·0xA8`).

`re/dump_templates.py` prints the resulting entry ranges. For example,
unit element 0 (light infantry) is entries 206..477, and the trike's
element 7 starts at 1870.

## Drawing

- **Position.** Objects keep 16.16 fixed-point pixel positions. An image is
  drawn at `(x >> 16) − img.offset_x − scroll_x`,
  `(y >> 16) − img.offset_y + 20 − scroll_y` (R8 header offsets), with
  blit flags (1,1): index 0 transparent, index 1 shadow
  (`FUN_00427d50` buildings, `FUN_00428e30` units).
- **Units** are placed at the cell centre:
  `x = (cx·32 + 16 + sub_x) << 16`, likewise y. Infantry sub-positions come
  from the table at `0x4c93e8` (−16) (`FUN_00455870`).
- **Buildings** are placed at `x = cx·32 << 16`,
  `y = (cy·32 + height_px) << 16`, i.e. the bottom-left of the footprint
  (`FUN_004563b0`).
- **Building image choice.**
  - If the element has no frames, hit points are below half and the
    building is not being built or sold, the game draws the damaged image
    `0x7907a8[e·32]`; otherwise it draws the base `0x6e5880[e]`.
  - Animation-2 frame = `obj[+0x3f] / record[rec2].0x85`, drawn from
    `0x716b00` when the 0x30 and 0x40 flags are set and hit points are at
    least half.
- **House colours.** Before drawing an 8-bit image, the game copies the
  owner's 16 colours from `bin\Colours.bin` (8 houses × 16 × u16 555,
  `0x786c48`) over **palette entries 240..255** (byte offset 0x1E0). The
  colour index is the player's colour (`0x5175d8[player]`).

## Other Templates.bin blocks

| Block | Meaning |
|---|---|
| `0x782770` / `0x7951d0` | unit element frames / rotations (u32 × 90) |
| `0x6e5a60` | building element frame counts (u32 × 120) |
| `0x795340` / `0x6d5fd0` | bullet / explosion frame counts (u32 × 64) |
| `0x714a98` / `0x7954b0` | building animation 1 / 2 frame counts per record (u8 × 100) |
| `0x7891b0` | per unit record: graphic name text (0x1C2 each, e.g. "TRIKE") |
| `0x6f9908` | per building record: graphic name text (0x1C2 each, e.g. "ATREIDES CONSTRUCTION YARD") |
| `0x6db390` / `0x7943a8` | bullet / explosion names (64 × 50) |
| `0x795528`, `0x79552c`, `0x795534`, `0x795530`/`0x795540`, `0x795544`, `0x795538`, `0x79553c`, `0x6e5850`, `0x6d9b50` | counts: unit elements 29, building elements 65, explosions 60, bullets 34, explosions 60, unit records 30, building records 62, building names 24, unit names 26 |
| `0x6d60d8` | special unit / building / explosion ids looked up at fixed offsets, 86 bytes (e.g. +0x0D concrete for the map set-up at 0x469c1d, +0x3B / +0x3D / +0x3E explosions; `sim::ExeTables`) |
| `0x782068` | bullet records, 64 × 28 bytes (docs/re/18 §1.1; `BulletRecord`) |
| `0x781e58` | explosion attributes, 64 × 8 bytes: +1 blink mask, +4 sound (`Templates::explosion_blink` / `explosion_sound`) |
| `0x6e5750` | explosion default draw flags, 64 × u32 (`Templates::explosion_flags`) |
