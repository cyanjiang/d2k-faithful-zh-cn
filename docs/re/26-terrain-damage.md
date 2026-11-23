# 26: Terrain damage: craters and broken concrete (1.06)

How weapon hits change the ground. Everything here happens at the end of
the damage function `FUN_0049d2e0` ("DamageTiles" in its error strings),
which every ground bullet impact calls (doc 18 §3). The map cell layout is
in doc 06 (12 bytes per cell at `0x517df0`); the DATA.R16 crater images in
doc 08 §"Graphics in DATA.R16".

"Verified" = read from the code. No open guesses remain (closed
2026-10-16).

## 1. Summary

- Every map cell has a **damage counter**: byte **+9** (`0x517df9`), not
  just its high nibble as doc 06 / our code assumed.
- Each ground hit on a cell adds a warhead-dependent amount to it.
- The terrain draw shows a **crater overlay** when the counter is above
  0x10. Its size grows with the counter (15 steps, `counter >> 4`), for rock
  (buildable) and sand cells.
- On **concrete**, the first time the counter passes 0x10 the slab breaks:
  its tile is replaced by one of 8 broken-concrete tiles and the cell loses
  its concrete flag (0x800).
- Nothing else changes: passability, speed, buildable and the tile
  attributes stay as they were. The counter never decays; it is reset only
  by new concrete, a new building on the cell, or falling spice.
- Destroyed buildings leave no rubble tiles. Their death explosions do not
  call the damage function, so they leave no craters either. Debris bullets
  do (doc 21 / `FUN_0049ca10` TODO).

## 2. The counter (`FUN_0049d2e0` tail, verified)

After the building and unit loops (doc 18), for the **impact cell**
`(x px >> 5, y px >> 5)`:

```
if !air                                      // bullet height <= 0x60000
   and f & 0x4000                            // "walk on" (infantry passable)
   and f & 0x700000 == 0                     // no spice
   and f & 0x10 == 0:                        // no building
    inc = ((ARMOUR[warhead][0] * bullet.damage) / 100) / 40      // u32; armour class 0 ("none")
    if counter + inc < 0xff:
        counter += inc                       // byte +9
        if counter > 0x10 and f & 0x800 and f & 0x8000:          // concrete on buildable ground
            f &= ~0x800                      // no longer concrete
            tile = kBroken[rand() & 7]       // one rand(), rand line 0x1e08
            return
    else:
        counter = 0xff                       // saturated; never breaks concrete here
```

- `ARMOUR[warhead][0]` = `0x790550[warhead·0x14]`, the percent against
  armour class 0; `bullet.damage` = `0x78206c[type·0x1c]`;
  `warhead` = `0x78207d[type·0x1c]`.
- **inc per bullet (1.06 Templates.bin + ARMOUR.BIN, every bullet; test
  "no 1.06 weapon saturates the crater counter"):** LMG 3, BAZOOKA 0,
  ROCKET 1, HMG 4, 80MM GUN 1, **155MM HOWITZER 14** (artillery), SOUND 2,
  HYDRA 3, 110MM GUN 1, FREMEN_S 3, FREMEN_L 1, BULLET14 0, NEW ROCKET 1,
  NON HOMING 1, MTANK PRI 2, MTANK SEC 1, DEVIATOR 2, DEVBULLET 8,
  **DEVASTATOR 125**, ORNIBOMB 9, DHBOMB 5, SPICEEXPL 1, WORM 2500 (never
  fired, doc 17 §6.4), DEBRIS 0, DEBRIS2 5, DEBRIS3 3, DEBRIS4 5,
  **BARRELHIT 112**, SARDDEATH 1, **CRATE 225**, M_LMG 3, M_HMG 1,
  GRENADE 4, GRENDEATH 4. So the rocket weapons add 0 or 1 per hit (a
  rocket turret or launcher barely marks the ground), an artillery shell
  14, and one Devastator blast or crate explosion makes a big crater at
  once.
- So a cell needs about 5 HMG hits before a crater shows, and concrete
  breaks on the hit that takes the counter past 0x10.
- **The saturation quirk:** a hit that would reach 0xFF or more sets the
  counter to 0xFF without the concrete check. A concrete cell that jumps
  from ≤ 0x10 straight to the cap (a single huge hit, inc ≥ 0xEF) keeps
  its concrete. **No 1.06 weapon does that:** the largest fired increment
  is the CRATE's 225 (< 0xEF); only the never-fired WORM bullet is above.
  The quirk only matters for a cell already above 0x10, whose concrete
  is already broken.
- `f & 0x4000` excludes cliffs and other non-walkable tiles; sand and rock
  both pass. Spice cells take no craters (the harvester code rewrites
  their tiles, doc 13).

**Broken concrete tiles** (`0x4e0af0`, u16 × 8): **552, 553, 554, 555,
556, 572, 573, 574** (0x228–0x22C, 0x23C–0x23E). They are tile indexes
in the mission's tileset, the same in every tileset (as the concrete
tiles 0x28B / 0x29F / 0x2B3, doc 12). The cell's flags are **not**
reloaded from TILEATR for the new tile.

## 3. Resets (all writers of `0x517df9`, verified)

| Where | Effect |
|---|---|
| `FUN_0044c6d0` (map load) | counter = 0, state +8 = 0x11 |
| `FUN_00456210` (concrete laid, doc 12) | counter = 0 on each new concrete cell |
| `FUN_0044dac0` (new building, called by `FUN_004563b0`) | counter = 0 on every cell of the record's **place mask** (+0x50) |
| `FUN_0049be60` (a falling spice-bloom blob) | counter = 0 when the cell becomes spice |
| `FUN_0049d2e0` | the increment above |
| `FUN_00423d20` | the draw (read only) |

No time-based decay.

## 4. Drawing the crater (`FUN_00423d20`, verified)

Per visible cell, after its tile (and only when shroud state +8 & 0xF ≠ 1):

```
if counter > 0x10:
    k = (counter >> 4) + 16 · ((x + y) & 1)          // 1..15, +16 on odd cells
    if f & 0x8000:      img = 0x6d7be8[k·2]           // "concrete" table: DATA.R16 entry 114 + k
    elif f & 0x10000:   img = 0x6d7bec[k·2]           // sand table: DATA.R16 entry 146 + k
    else: none
    jx = 0x503f88[(x + y) & 0xF]                      // -12..-1
    jy = 0x502ed8[(x + y) & 0xF]                      // -12..-1 (indexed by x + y + 16, same low bits)
    blit img at (cell screen x + jx, cell screen y + jy), flags (1, 1)
```

- The loop carries `x + y` in one counter (+1 per column, net +1 per row),
  so both the parity and the jitter index are `(x + y)`.
- The "concrete" table is used for any **buildable** cell (rock and
  concrete); the sand table for sand. Non-buildable, non-sand cells (e.g.
  rough rock that is walkable but not buildable) get no overlay, although
  their counter grows.
- Entry k = 0 of each table (`counter >> 4 = 0`) is never drawn, because
  the counter must be > 0x10.
- **Jitter tables:** filled by `FUN_00466140` (`0x466420..`), 16
  iterations of `0x503f88[i] = rand() % 12 − 12; 0x502ed8[i] = rand() %
  12 − 12` (in that order), right after 64 `rand() & 3` bytes into
  `0x790510`. **Corrected:** `FUN_00466140` has one caller, the game
  set-up `FUN_00448550` (`0x44865e`), and runs there at **every game
  start unless `0x797e1c` (RestartGame / the set-up loop's restart) is
  set**. So the tables change with each new mission and stay the same
  over a restart (doc 52 §2.1; our `GameSession`). They are **not saved**:
  `FUN_004418b0` does not touch them, so a loaded game keeps the tables
  of the set-up that loaded it (ours: `GameScreen::load_blob` skips the
  copy in our save format since 2026-10-16).
- **Blit flags (1, 1)** (`FUN_00430770` at `0x423f3b` → `FUN_0042fac0`):
  for an 8-bit image on the 16-bit screen, palette index 0 is the screen
  pixel (transparent), index 1 the screen pixel at half brightness
  (`(dst & mask) >> 1`, a shadow), every other index its palette colour;
  for a 16-bit image 0xF81F is transparent and 0x001F the half-dark
  shadow. No image offsets are applied (the crater entries 114–177 are
  32 × 32, 8-bit, offsets 0). Our keyed sprite draw is exactly this.

## 5. Related, not terrain damage

- **Spice blooms** (the head of `FUN_0049d2e0`): if the impact cell is an
  active bloom in the table at `0x517b98` (12-byte entries: x, y, type 8 /
  9 / 10, active), `FUN_0044ef10(x, y, 4 / 5 / 6)` throws n² · 2 spice
  blobs around it (bullet type `0x6d610a`, random offsets from the circle
  tables `0x6e9840` / `0x704a98`), plays `S_SPICEMOUND`, and
  `FUN_0044f110` re-arms or removes the bloom (flag 0x1000 cleared). The
  damage function then **returns**: no unit / building damage and no
  crater for that hit. The blobs make spice cells (tile 0x31A, level 1 or
  2) and reset the counter (`FUN_0049be60`). Belongs to the spice-bloom
  work (doc 13 open item).
- **Walls** (cell 0x400) hit by bullets with flag 0x1000 call the damage
  function at the wall cell (`FUN_0049be60`, 0x49c0..); walls are
  buildings, damaged as in doc 18.

## 6. Implementation checklist (our code)

1. `sim::Cell::marks` (+9) is the whole damage counter: fix the comments
   (`game_map.hpp`, doc 06) and the resets in `placement.cpp` (lines that do
   `marks &= 0x0f` → `marks = 0`): concrete laying (`FUN_00456210`) and
   building creation over the **place mask** (`FUN_0044dac0`).
2. `World::damage_at` (`src/sim/combat.cpp`): at the end, for ground hits,
   the §2 increment with `armour_.warheads[w].percent[0]` and
   `bullet.damage`, the concrete break (clear `kConcrete`, tile =
   kBroken[rng & 7], one `rng_.next()`), and the 0xFF saturation quirk.
   Keep the order: after the building / unit loops.
3. Terrain draw (`src/render/terrain.cpp` or the game screen): after the
   tile, the crater overlay of §4 from DATA.R16 entries 114 + k (buildable)
   or 146 + k (sand), with the two jitter tables; skip shrouded cells.
4. The jitter tables: generate them at each game set-up (not on a
   restart) with the shared rng in the exe's order: 64 + 32 `rand()`
   calls in `FUN_00466140`, the first step of `FUN_00448550`, before the
   campaign's clock seed (doc 52 §2.1). Done in `GameScreen`'s
   constructor with `GameSession::rng`.
5. A golden test: N HMG impacts on one rock cell → counter = 4·N; on a
   concrete cell the tile becomes one of the 8 ids and `kConcrete` clears
   once the counter passes 0x10.
6. Later: spice-bloom bursts (§5) with the bloom table.

Note (2026-10-07): the spice blob landing sets level 2 or 4, not 1 or 2; see doc 31 section 5.
