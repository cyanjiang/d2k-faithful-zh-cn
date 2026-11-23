# 12: Building placement and unit spawn

Covers 1.06: placement mode (the grid, the rules, the click, the order and
what creating a building does), and how a finished unit leaves its
factory. A short separate section covers how voices and samples are looked
up and played. Decompiles are in `re/exports/1.06/q_pl1.txt` … `q_pl9.txt`,
`q_bcreate.txt`, `q_ucreate.txt`, `q_exec.txt`, `q_sb3.txt` and
`q_inp1.txt` (not committed).

"Verified" means read from the code or the data. "Inferred" is marked.

## Corrections to earlier docs

- **doc 10, §5 "Mode 1":** the cursor **is** used. Mode 1 sets cursor type 0
  (the arrow) everywhere except over the radar (`0x443d34`). Edge cursors
  are not shown in mode 1 (they need mode 0), but edge scrolling still
  works.
- **doc 10, §6 "Event voices":** `FUN_0048dd90` does not build a prefix. It
  looks a sample name up in the samples.uib table and returns its index.
  The caller looks up all three house names, and `FUN_0046f900` picks one
  (§9).
- **doc 08, building record:** +0x60/+0x61 is the **spawn cell** and
  +0x62/+0x63 the **first move cell** of a factory, relative to the top-left
  footprint cell (§7). The 0x50 mask is also the **placement grid** and the
  cells that lose the buildable flag. The 0x54 mask is the occupied cells.
- **doc 11, §6 "flag +0x28 | 0x10":** it is set each tick while a slot
  works, on the factory `FUN_00442c20(…, check = 0)` returns. It makes the
  factory play its active animation (§7.3).
- **doc 11, §6 "Ready" (unit):** if no matching factory exists at all, the
  slot is **cleared without a refund** (§7.2).

## 1. Entering and leaving placement mode

Mode is `0x516438`. Placement is mode 1.

- **Enter:** left release on the building column while the building slot
  (`player+0x2651c`) is at 0x5A00 and `player+0x2652d` is clear (doc 11 §6).
- **Leave:**
  - any right press (doc 10 §3 sets mode 0);
  - a drag box: a left release after a press in the playfield, with at
    least 4 px on both axes (`0x444cbc`), box-selects (`FUN_004a5ae0`) and
    sets mode 0 (`0x444d48` / `0x4455a7`) before any mode is looked at, so
    it cancels placement (and repair, sell and the strike modes too);
  - a valid left click on the playfield (§4);
  - the repair / sell buttons (mode 2 / 3);
  - `FUN_00428ca0` sets mode 0 when the building slot item is < 0
    (cancelled or placed).
- An invalid click does **not** leave the mode, nor does a release
  outside the playfield.
- **Release order (verified 2026-10-16, `FUN_004433a0` `0x444bfc`..):**
  pressed sidebar buttons go up; the drag box above; a release on the
  build lists (`0x444d57`, any mode); a release on the radar (`0x445010`,
  any mode: with units selected, a move order to that cell and the
  acknowledge `FUN_0049f240(1)` at `0x445d8f`); only then mode 1 (`0x4450ef`),
  2, 3, 4, 5 and the mode-0 playfield click.
- `player+0x2652d` is a "request pending" flag. The placement click and a
  right click on the building icon set it. The 0x26 handler clears it.
  While it is set, the ready icon does not enter mode 1 again.

## 2. The placement grid

`FUN_00428ca0` (draws the playfield objects) starts with the grid when
mode = 1:

- record kind (+0x5c) 15 (concrete): `FUN_004288d0(mask54, mask54, surface,
  1)`;
- every other kind: `FUN_004285c0(player, mask50, mask54, surface)`. The
  mask54 argument is not used.

Both return the "valid" byte, stored in **`0x51643d`**. The click reads it
(§4).

### Draw order (verified)

The grid is drawn **first** in `FUN_00428ca0`, before air/ground objects,
buildings, units, bullets and the move markers. The shroud
(`FUN_00423fb0`) is drawn after the whole `FUN_00428ca0` call. So:

- objects draw over the grid;
- **full shroud hides the grid**. Neither the grid nor the rules look at
  the shroud.

### Anchor (verified)

From the **current mouse position** (`0x4eb048` / `0x4eb04c`):

- If mouse y < 20 (`0x4e41dc`), nothing is drawn and the result is invalid.
- cx = (scroll_x + mouse_x) / 32, cy = (mouse_y − 20 + scroll_y) / 32.
- **If cy < 2, cy = 1.** Row 0 of the map can never be used. The screen y is
  clamped to match.
- **The cell under the mouse is the top-left of the 4×4 mask** (bit 0 =
  row 0, col 0; bit = row·4 + col). It is not centred.
- Each mask cell is drawn at its cell position, snapped to the 32 px grid,
  clipped to the playfield (0, 20)–(480, 400). Over the sidebar the grid
  lands off the playfield and is clipped away.

### Images (verified)

DATA.R16 fixed entries (doc 10 §8), 32×32, 8-bit, diagonal stripes on a
transparent background, drawn with blit flags (1, 1):

| Entry | Global | Colour | Use |
|---:|---|---|---|
| 0 | `0x786fe8` | grey, 555 `0x6318` = RGB(192, 192, 192) | valid cell |
| 1 | `0x795440` | red, `0x7c00` | invalid cell |
| 2 | `0x6e5854` | white, `0x7fff` | valid cell that is already concrete |

### Which cells are drawn

- **Normal buildings:** every bit of mask 0x50 (the drawn size, e.g. 3×3
  for the construction yard, though only 2 rows are occupied). Cells off
  the map are drawn red.
- **Concrete:** every bit of mask 0x54. Cells off the map are not drawn.

## 3. Validity rules (client side, verified)

### Normal buildings (`FUN_004285c0`)

**Pass 1, "near" test.** For every mask-0x50 cell inside the map, scan the
5×5 square around it: x − 2 … x + 2, y − 2 … y + 2, clamped to the map
(start at 0). `FUN_00428c30` checks each scanned cell, and **skips row 0**:

- bit 1: the cell has a building (flag 0x10) and owner bits 0–2 = player;
- bit 2: the cell is concrete (flag 0x800) and concrete owner bits 17–19
  = player.

If any mask cell is off the map (x ≥ map_w or y ≥ map_h), the whole
placement is invalid.

So the rule is: **some cell of the footprint must be within 2 cells
(Chebyshev) of an own building cell or own concrete.** Walls and turrets
are buildings (flag 0x10), so they count. Concrete counts too. Allied
buildings do not count.

**Pass 2, per cell.** A mask-0x50 cell is valid when all hold:

- inside the map and row ≥ 1;
- `flags & 0x13f8 == 0`: no building (0x10), no vehicle (0x8), no infantry
  (0x20…0x200), no crate or bloom (0x1000);
- `flags & 0x8000` (buildable);
- the near test passed.

Any invalid cell makes the whole placement invalid. Each cell is drawn
grey, white (valid and already concrete) or red.

**Terrain.** Only the 0x8000 bit is tested. In all seven TILEATR files no
sand tile (0x10000) has 0x8000, and the spice tiles do not have it either.
So buildings go on rock only. Spice is not tested by itself.

### Concrete (`FUN_004288d0` with the last argument 1)

- The same near test (5×5 around each mask-0x54 cell, own building or own
  concrete). If any mask cell is off the map, the near test fails and
  nothing is valid.
- A cell is valid when: `flags & 0x13f8 == 0`, **not already concrete**
  (0x800 clear), buildable, the near test passed, row ≥ 1.
- Valid cells are drawn grey (entry 0); invalid ones red.
- **Placement is allowed if at least one cell is valid.** Off-map cells
  are not drawn.

Concrete records (kind 15):

| Record | Name | mask 0x54 |
|---|---|---|
| 4–6 | CONCRETE | 0x33 (2×2) |
| 7–9 | CONC2 ("test concrete") | 0x777 (3×3) |

### Walls and turrets

Walls (kind 14) and turrets (kind 16) use the normal path with a 1-cell
mask. There is no special rule for them in placement.

## 4. The left click (mode 1, in `FUN_004433a0`)

On left release, when the **current** mouse x < 480 and y > 20:

- **`0x51643d` = 0 (invalid):** play the CANTBUILD voice (S_ / H_ / O_,
  §9). Mode stays 1.
- **Valid:**
  - cell from the **release point** (`0x798388` / `0x79838c`):
    cx = (scroll_x + x) / 32; world y = y + scroll_y − 20, cy = 1 if world
    y < 32, else world y / 32 (the same clamp as the grid);
  - send order **0x26** with `FUN_00459fd0(player, slot item, cx, cy)`;
  - set `player+0x2652d` = 1 and mode = 0.

No sound plays on a valid click. The order fields: +0x14 cx, +0x1a cy,
+0x23 type 0x26, +0x24 player, +0x26 building record. `FUN_00459fd0` only
logs an error if the record is not the one in the slot.

## 5. Order 0x26 (handler in `FUN_00457810`, switch case 0x13)

The executor's switch index is order − 0x13. Steps:

1. `player+0x2652d` = 0.
2. **Concrete (kind 15):**
   - clear the slot (item = −1);
   - `FUN_00456210(player, rec, cx, cy, 0, mask54)` lays the slabs;
   - local player: `S_BUILDCONC` (file `chung`) at (cx, cy), positional.
   - **There is no server-side check.** The near rule is only enforced by
     the client grid.
3. **Other buildings:** `FUN_00457740(rec, cx, cy)` checks every mask-0x50
   cell: inside the map and `flags & 0x3f8 == 0` (no building, vehicle or
   infantry). **It does not test buildable, crates or the near rule.**
   - **Pass:** clear the slot, `FUN_004563b0(player, rec, cx, cy, 0, 0, 0)`.
     Local player: `S_BUILDUP` (file `build1`) at (cx, cy), positional.
   - **Fail** (e.g. a unit drove onto the site after the click): nothing
     happens. No sound. The slot stays ready, and since `+0x2652d` is
     cleared, the player can click the icon and try again.

### Laying concrete (`FUN_00456210`)

For each mask cell inside the map, a slab is laid when
`flags & 0x13f8 == 0`, buildable, not already concrete, and row ≠ 0:

- the crater byte (+9) is cleared;
- the tile becomes one of 0x28b, 0x29f, 0x2b3, picked by `rand() % 3`
  (MSVC rand, doc 06);
- flags |= 0x800, owner in bits 17–19.

The local player also gets `FUN_0044e330` per cell (reveal, below). Laying
concrete does not rebuild the build lists.

## 6. Creating a building (`FUN_004563b0`)

Arguments: player, record, cx, cy, then three flags. Flag 5 ≠ 0 is a
pre-placed (map) building, flag 6 skips the construction yard concrete
and the free harvester, flag 7 skips the crane animation (verified
2026-10-16 from the four callers, the only ones: the map loader
`0x469c05` passes 1, 0, 0; the order 0x26 handler `0x45882f` 0, 0, 0;
the MCV deploy `0x49b26e` 0, 0, 0; the capture `0x46d7ef` 0, 1, 1).

1. Fails (returns 0xffff) if the player is not active (`+0x24250`). If the
   object pool is empty, it shows the `ReachedUnitLimit` message and
   fails.
2. Counters: `player+0x24784` += 1, `player+0x24878[rec]` += 1.
3. Fields:

   | Off | Value |
   |---|---|
   | +0x00 / +0x04 | x = cx·32 << 16, y = (cy·32 + height_px) << 16 (bottom-left) |
   | +0x16 / +0x17 | cx, cy |
   | +0x18 / +0x19 | largest x / y of the mask-0x54 cells |
   | +0x24 | hit points (below) |
   | +0x28 | flags = record +0x58, plus the bits below |
   | +0x2c | 0xffff (target) |
   | +0x30 | state 2 |
   | +0x34 | record |
   | +0x35 | 0 (not selected) |
   | +0x36 / +0x37 | record +6 / +7 |
   | +0x39..+0x3f | 0 |
   | +0x3c / +0x40 | owner |
   | +0x41 | 0xff |
   | +0x42 | own index |
   | +0x48 | tick `0x5173f4` |
   | +0x50 / +0x52 | 0xff / 0xffff |

4. **Concrete under it:** laid (mask 0x50) only for pre-placed buildings,
   or for a construction yard (kind 2) with flag 6 = 0. Not for normal
   placed buildings. Records with flag 0x400000 never get it.
5. **Hit points** (`FUN_0044dac0`): on = mask-0x54 cells with concrete,
   total = mask-0x54 cells. hp = (on · hp >> 1) / total + hp / 2. If
   on ≠ total, flag **0x8000** (not fully on concrete). Records with flag
   0x400000 (Fremen barracks / sietch) skip this. `FUN_0044dac0` also
   clears the crater byte of the mask-0x50 cells. **Re-read 2026-10-16:**
   `FUN_0044dac0` is called for every record (`0x45646f`); only the hit
   point formula at `0x4564f6` tests 0x400000, so the crater bytes are
   cleared for the 0x400000 records too. The counting loop has no bounds
   test (cell index = row offset `0x547df0[cy + row]` + cx + col), so a
   mask cell past the right map edge reads the next row's first cell.
   No caller reaches that in 1.06: order 0x26 (`FUN_00457740`) and the MCV
   deploy (`FUN_00443250`) test the whole 0x50 mask inside the map, a
   capture reuses a placed building's cells, the 0x54 footprint lies inside
   the 0x50 mask for every record, and no map building of the 1.06 campaign
   overhangs the map (all 3856 checked in `tests/sim/test_placement.cpp`).
   We skip off-map cells, which only an edited map could reach.

   **Map-placed buildings (verified 2026-10-16):** the map loader
   (`FUN_00469790`, call at `0x469c05`) uses this same function with
   flags (1, 0, 0), so step 4 lays concrete under the mask-0x50 cells
   first (only where `FUN_00456210` allows it: buildable, not row 0, no
   object or concrete yet) and step 5 then gives full hit points only
   when every footprint cell got a slab. A map building standing partly
   on sand or rough rock starts with fewer hit points and flag 0x8000
   (decay to half, §7). In the 1.06 campaign this hits 16 of the 3856
   map buildings over all missions, e.g. A7V1's record 18 at (12, 0): row
   0 never gets concrete, so it starts at 1000 of 2000 hit points (golden
   test in `tests/sim/test_placement.cpp`). There is no other hit point rule for map objects:
   the loader's concrete specials 0x3c..0x3e (`0x800000c8..ca` →
   `FUN_00456210`) are dead in 1.06, because TILEDATA has −1 for them.
6. **Build-up animation:** if not pre-placed and the record has animation-1
   frames (`0x714a98[rec]` ≠ 0): +0x3e = 0, +0x44 = tick, flag **2**.
   Walls have 0 frames, so they appear at once. **Index (corrected
   2026-10-09):** creation (`FUN_004563b0`) and selling (`FUN_00456ee0`)
   index `0x714a98` by the **record** (obj +0x34); only the per-tick
   build-up update (`FUN_0049b7f0`) and the image lookup use rec +0x8a.
   The parser leaves rec +0x8a at 0 for records without an animation
   (walls), so indexing by +0x8a gave walls the construction yard's
   MCV-unpacking animation (and its reverse when sold).
7. `player+0x245c1[name]` += 1; add to the building list
   (`FUN_0046bd60(obj, 2)`); **rebuild the build lists** (`FUN_0046be50`).
8. **Primary building:** if this kind's primary slot is empty, this
   building becomes primary and gets flag **0x80**:

   | Kind | Slot | Kind | Slot |
   |---|---|---|---|
   | 2 construction yard | `+0x26098` | 11 barracks | `+0x260a2` |
   | 7 light factory | `+0x2609a` | 12 (unused) | `+0x260a4` |
   | 8 heavy factory | `+0x2609c` | 13 Starport | `+0x260a6` |
   | 9 high tech (H, O) | `+0x2609e` | 17 Harkonnen palace | `+0x260aa` |
   | 10 high tech (A) | `+0x260a0` | 18 Ordos palace | `+0x260ac` |
   | | | 19 Atreides palace | `+0x260a8` |

   Slots hold the object index (u16, −1 = none). So **the first factory of
   each kind is primary automatically**.
9. **Crane animation:** if not pre-placed, flag 7 = 0 and a primary
   construction yard exists, that yard gets flag **0x20** (one cycle of its
   active animation, §7.3).
10. Local player: `FUN_0044e330` reveals the shroud around the building
    centre (cx + width/64, cy + height/64) with the record view range
    (+0x0e).
11. **Map cells** (`FUN_0044dbd0`):
    - every mask-0x50 cell loses the buildable flag 0x8000;
    - every mask-0x54 cell: flags = (flags & ~0x8007) | owner | 0x10, and
      the radar pixel gets the owner colour;
    - repair pad (kind 6): the occupied cells other than (1, 1) also get
      0x10000000 (`0x44ddba`; the pad's footprint 0x272 is a cross, so the
      four arms); heavy factory (kind 8): cell (0, 2) gets it (`0x44ddf0`);
      light factory (kind 7): cell (2, 1) gets it (`0x44de22`), each only
      when that cell is in the footprint. **Verified 2026-10-16:** these
      three are the only writers of cell bit 0x10000000 (immediate scan of
      `.text`), the building removal `FUN_0044df60` clears it with 0x10
      (`& 0xefffffef`), and its only reader is the passability test
      `FUN_0045d990` (`0x45da52`, §11): a non-infantry unit may enter such a
      cell of its own side's building when it has no unit bits. So it marks
      the drive-through cells of the factory exits and the pad; it is never
      set on a refinery dock (the dock (2, 1) is outside the refinery
      footprint 0x36);
    - walls and turrets: the top-left cell also gets 0x400.
12. **Units on the footprint (verified 2026-10-16, `FUN_0044dbd0`
    0x44dcb0..0x44dd5d):** per mask-0x54 cell, in row-major order, **before**
    that cell's flags are written:
    - `FUN_004a5270(cell·32, cell·32, &side, &idx, 0)` takes `side` = the
      cell's owner bits (`flags & 7`) and returns non-null only when that
      side has a unit there (a ground vehicle on or stepping into the cell
      when bit 8 is set, else its nearest infantry when 0x3e0 is set;
      the worm helper `unit_in_cell`). Null: nothing happens in this cell.
    - Otherwise `FUN_004a54a0(x, y, side, &idx)` walks **all** units of
      `side` from the head of its list (idx = 0xffff at the start, then
      from the last found unit): units whose current cell (+0x4a/+0x4b) or
      "to" cell (+0x4c/+0x4d, without testing 0x10000000) is the cell,
      not worms (kind 5), not flags 0x400140. Aircraft of that side over
      the cell are found too.
    - For each one, **if `side` ≠ the placer**: units lost `+0x24a08` += 1
      and `+0x24a10[unit rec]` += 1 for `side`, then
      `ConvertUnit(side, placer, idx, 0)` (doc 30 §2.6): infantry are
      removed (state 0x11, flags 0x600000), vehicles are copied to the
      placer (state 7, flag 0x2000000) and the old one removed. No kill
      is counted for the placer. **Own units (side = placer) are left.**
    - Then the cell flags are written as above. Since the cell has no
      building bit yet, the conversion's `CreateUnit` is not refused; the
      new vehicle keeps vehicle bit 8 in the building cell.

    **A player placement never reaches this** (both checks refuse any unit
    on the site), nor does the map loader in practice. It happens for a
    capture (units of the victim on the footprint, e.g. a vehicle on a
    captured repair pad's cells) and for an MCV deploy (units of other
    sides on the 3×3 site; the MCV itself is the placer's own unit).
13. **Refinery** (kind 4) with flag 6 = 0 (verified: the map loader at
    `0x469c05` passes flags 1, 0, 0, so **map-placed refineries get one
    too**, at mission start; the order 0x26 handler passes 0, 0, 0): queues a free harvester
    delivery (table `player+0x262ec`, 10 entries of 0x38; "Too many
    deliveries" when all are in use): +0x03 = 1 (in use), +0x02 = 0, cell
    (cx + 2, cy + 1), +0x04 = tick, +0x35 = **1** (carryall), +0x36 = **4**
    (no REINFORCE voice; the same byte is the AI group mode of a delivery),
    records {`0x6d60e8` (= 8, HARVESTER), 0xff} (verified 2026-10-16,
    `FUN_004563b0`; we passed action 0 before, now 4). The delivery itself
    is the common carryall delivery: `FUN_0046def0` (doc 13 §5.2, doc 33),
    once every 8 ticks per active player after `FUN_0046d180` (doc 33 §1.6)
    and the Starport tick `FUN_0046ddc0` (doc 34 §2).
14. Walls and turrets (verified): `FUN_0044e580` picks the wall frame,
    stored at building +0x20: neighbours with cell flag 0x400 (walls and
    turrets; `FUN_0044e540` = inside the map and 0x400) give N 1, S 2,
    W 4, E 8, and the mask maps to frames 0, 3, 4, 5, 2, 12, 13, 9, 1, 11,
    15, 8, 6, 7, 10, 14. `FUN_0044e6d0` then recomputes the frame of each of
    the 8 neighbours that is a wall or turret building; the removal
    (`FUN_0044df60`) calls it too. The draw (`FUN_00428e30`) uses +0x20
    instead of the animation frame +0x39 for kinds 0xE / 0x10, and the
    damaged image is `0x7907a8[element][+0x20]` for every building, so
    other buildings (whose +0x20 stays 0) show their first damaged image.
15. **Skirt tiles (verified 2026-10-16):** records with flag 0x200000
    (the last step, `0x456997`): `FUN_0044f1b0(cx, cy, rec)` takes the
    width w = rec+0x87 >> 5 and the height h = rec+0x86 >> 5 (cells); w
    must be 2 or 3 (else an error box "Building %s can't have a skirt",
    nothing written). For the two bottom rows k = 0, 1 (cell row
    cy + h − 2 + k) and the columns c = 0..w−1 (cell column cx + c), the
    map tile becomes `base + c + 0x14·k`, with base 0x269 (w = 2) or 0x263
    (w = 3), or 0x289 / 0x283 when that cell has concrete (0x800). Only the
    tile word changes (no flags, no rand()). In 1.06 the flag is set on
    the records that are 3 cells tall and 2 or 3 wide (kinds 0, 1, 2, 4
    and 11, e.g. construction yards and refineries), and on no other; the
    data (`Templates.bin` block 0x6dc540, +0x58) decides, so the code
    reads the flag, not the kind.

    **Removal:** `FUN_0044dee0` (RemoveBuildingTiles: group count
    `+0x245c1` − 1, `FUN_0044df60`, then for flag 0x200000
    `FUN_0044f2f0`), called by the building update at the end of state
    0x11 (`0x49bdf9`, skipped for flag 0x10000 like the cell clean-up) and
    by the capture (`0x46d78c`). `FUN_0044f2f0` walks the same cells in
    the same order: a cell with concrete gets tile 0x28b; a cell without
    gets `0x4e0af0[rand() & 7]` (rand() line 0x971), **one rand() per
    non-concrete skirt cell**. So destroying or selling such a building
    takes up to 6 rand() calls, at its removal.

**Slot cleared, then created:** the slot is cleared before
`FUN_004563b0`, so the rebuild in step 7 sees an empty slot.

## 7. Building update bits (`FUN_0049b7f0`, per building)

### 7.1 Build-up and selling

- **Flag 2 (build-up):** +0x3e += 1 per tick. When +0x3e / 3 ≥ the
  animation-1 frame count of record +0x8a, flags 2 and 4 are cleared. So
  build-up lasts 3 ticks per frame (wind trap 24 frames = 72 ticks).
- During build-up the base image is drawn only once +0x3e / 3 ≥ record
  +0x89 (`FUN_00427d50`). The build-up frames are drawn in the draw list
  (`FUN_00428e30`), verified, see §11.
- Flag 4 without 2 (selling) counts +0x3e down. Not covered here.

### 7.2 Concrete decay (verified)

While flag **0x8000** is set and hp > max / 2: **hp − 1 every 16 ticks**
(`tick & 0xf == 0`). So a building not fully on concrete slowly drops to
half its hit points.

### 7.3 Active animation (animation 2)

Each tick, when not building or selling, `flags & 0x30` ≠ 0, the object
has flag 0x40 (from record +0x58) and hp ≥ max / 2:

- +0x3f += 1;
- when +0x3f / rate ≥ frames, +0x3f = 0, and flag **0x20** is cleared
  (0x20 plays one cycle). rate = record[+0x8b].0x85, frames =
  `0x7954b0[+0x8b]`.

`FUN_00427d50` draws the frame +0x3f / rate under the same conditions. So
when 0x10 and 0x20 are both clear, the overlay **stops drawing at once**,
and +0x3f keeps its value.

Flag 0x10 is the "factory working" flag (§8.2). Flag 0x40 comes from the
record for the construction yard, light / heavy / high tech factories,
Starport, repair pad, outposts, research centre, wind traps and the
Harkonnen palace. It is not set for barracks, refineries or silos, so
**barracks never animate**. Wind traps, outposts and research centres also
have 0x10 in their record flags (0x750), so **they animate all the time**.
Factories get 0x10 only while working. Examples: light and
heavy factory 10 frames × rate 10 = 100 ticks per cycle; construction yard
14 × 3.

## 8. Unit spawn

### 8.1 Finding the factory and the exit (`FUN_00442c20`)

`FUN_00442c20(unit rec, player, &x1, &y1, &x2, &y2, check)` returns the
building object, or 0.

**Step 1, primary slot.** g = unit record +0x28 (prerequisite 1, a
building name index). The name ids come from Templates.bin `0x6d60d8`:

| g | Slots tried, in order |
|---|---|
| 13 LIGHT_FACTORY (`0x6d60db`) | `+0x2609a` |
| 15 HEAVY_FACTORY (`0x6d60dc`) | `+0x2609c` |
| 12 HIGH_TECH_FACTORY or 11 AT_HIGH_TECH_FACTORY | `+0x2609e`, then `+0x260a0` |
| 4 BARRACKS (`0x6d60d9`) | `+0x260a2` |
| `0x6d60da` (= 0xff, never) | `+0x260a4` |
| 19 ATREIDES_PALACE | `+0x260a8`, `+0x260aa`, `+0x260ac` |
| 20 HARKONNEN_PALACE | `+0x260aa`, `+0x260a8`, `+0x260ac` |
| 21 ORDOS_PALACE | `+0x260ac`, `+0x260aa`, `+0x260a8` |
| other | error "Unit expects to arrive from unknown…", return 0 |

The first slot that is not −1 is used. If all are −1, it returns 0 **at
once** (no list search).

The primary building is used when unit record +0x2c (prerequisite house
mask) & building record +0x04 (house mask) ≠ 0, and, with check = 1, its
spawn cell is free. Otherwise:

**Step 2, list search.** Walk the player's building list (`+0x10`, through
+0x7c, in creation order). Take the first building whose name index
(+0x88) **equals g exactly** and whose house mask matches, and (check = 1)
whose spawn cell is free. The primary building is tried again here.

**Outputs.** With (bx, by) = the building's top-left cell
(x >> 16 / 32, ((y >> 16) − height_px) / 32):

- (x1, y1) = (bx + rec+0x60, by + rec+0x61): the **spawn cell**;
- (x2, y2) = (bx + rec+0x62, by + rec+0x63): the **first move cell**.

**Free** means spawn cell `flags & 0x3e8 == 0`: no vehicle and no
infantry at all. The building bit 0x10 is not in the test. **The spawn
cell is inside the factory's own footprint.**

| Building | Spawn | Move to |
|---|---|---|
| barracks (2×2 occupied) | (1, 1) | (1, 2), below it |
| light factory | (1, 0) | (2, 1) |
| heavy factory | (1, 1) | (0, 2) |
| high tech factory | (1, 0) | (0, 0) |
| Atreides / Ordos palace | (1, 1) | (0, 2) |
| imperial palace, sietch / Fremen barracks | (0, 1) | (0, 2) |

(Templates.bin, verified. The Harkonnen palace has (0, 0) → (60, 247):
unused, Death Hand is not spawned there.)

### 8.2 The per-tick slot logic (`FUN_004467e0`)

For each of the 10 unit slots with an item:

1. F = `FUN_00442c20(item, player, …, check = 0)`.
   - **F = 0:** the slot is cleared (item = −1). **No refund.**
2. **On hold:** F flag 0x10 cleared.
3. **Not on hold, progress < 0x5A00:**
   - F flag **0x10** set (except Death Hand, kind 10). This is set before
     the payment, so it stays on while the slot stalls for money;
   - progress and payment (doc 11 §7);
   - on reaching 0x5A00:
     - specials (kinds 9, 4, 12, 10): ready message or `*_DEATHREADY`;
     - others: `FUN_00442c20(…, check = 1)`. If found: spawn
       (`FUN_00455870(player, item, x1, y1, x2, y2, 0, 0)`), clear the
       slot, and for the local player `*_UNITREADY`. Then, found or not,
       F flag 0x10 is cleared. **The result of `FUN_00455870` is not
       tested** (`0x446f48` / `0x44702c`, verified 2026-10-16): when the
       creation fails (object pool full, "ReachedUnitLimit") the slot is
       still cleared and `*_UNITREADY` still plays, so the unit is lost
       with no refund. (We kept the slot and retried before.)
4. **Not on hold, progress = 0x5A00 (waiting):** non-specials retry
   `FUN_00442c20(…, check = 1)` every tick, and spawn the same way. The
   0x10 flag is not touched.

**The factory flag and the spawn can be different buildings.** Flag 0x10
goes on the primary (or first matching) factory. The unit leaves from the
first factory whose spawn cell is free.

**Door / exit animation:** there is none besides the active animation
(§7.3). Spawning does not change the building's flags or frame. The only
sound is `*_UNITREADY` (local player, not positional).

### 8.3 Creating the unit (`FUN_00455870`)

Arguments: player, unit record, spawn (x1, y1), move cell (x2, y2), pixel
offsets dx, dy (0 for factory spawns).

1. **Ground kinds** (all but 2 carryall, 8 frigate, 9 ornithopter,
   10 Death Hand, 5 sandworm): fail if the spawn cell has a building
   (0x10) of **another** owner. An own building is fine.
2. Fails if the player is inactive or the pool is full
   (`ReachedUnitLimit` message).
3. Counters `+0x24780`, `+0x24788[rec]`, `+0x24628[rec]` += 1.
   `player+0x260b0` = this unit (last created).
4. Fields (doc 09 §2 names):
   - hp +0x04 = record max hp (full); flags +0x08 = record +0x38;
     speed +0x00 = record +0x08;
   - state +0x10 = 2, follow-up +0x14 = 1;
   - +0x1a = rec+3, +0x1b = rec+0xf, +0x1c = rec+0xe;
     +0x2f = 10 (vehicle) or 0x32 (infantry); +0x28 group = 0xff;
   - x = (x1·32 + 16 + dx) << 16, y = (y1·32 + 16 + dy) << 16 (cell
     centre);
   - current, next and home cells (+0x4a/4b, +0x4c/4d, +0x50/51) =
     (x1, y1); destination +0x48/49 = (x2, y2);
   - **body facing +0x54** = 0 if (x1, y1) = (x2, y2), else
     `FUN_0045e020(x1, y1, x2, y2)` (the direction to the move cell);
     turret facing +0x55 = 0.
5. **Initial order:**
   - (x1, y1) ≠ (x2, y2) (every factory above): **state 8** via
     `FUN_0049deb0`, towards (x2, y2). Doc 09 lists 8 as "move, then attack
     what is in the destination". SetState does not refuse 8 for unarmed
     units, so harvesters get it too. **On arrival (verified 2026-10-16,
     `FUN_00497b90` case 6 = state 8):** a carryall is set to state 2 at
     once; otherwise the straight step `FUN_00495560(u, side, 1, 1)`
     returning 0 sets **state 0xc** for a harvester (kind 1) and 2 for the
     rest; a unit between steps that is no longer on a building cell
     (`FUN_0049d280` false) is set the same way. So a new harvester starts
     harvesting (doc 13 §1.8).
   - (x1, y1) = (x2, y2), or carryall / Death Hand: harvester → state 0xc
     (harvester initial); else, for a non-local player in single player
     (`0x7984c4` = 0), and not kinds 2 / 8 / 10 → state 3 (area guard /
     hunt); else unchanged (2).
   - Kind-specific: harvester (kind 1) +0x245b8 += 1 and +0x58 =
     `FUN_004a5e80(player)` (inferred: its refinery). Carryall (kind 2)
     starts at a point from `FUN_0049e6b0` (inferred: the map edge) at
     altitude 0x3c0000 and moves to (x1, y1). Death Hand flies to the map
     centre. Kinds 8, 9, 15: altitude 0x3c0000, flag 0x40.
6. **Occupancy** (ground kinds):
   - vehicle: cell flags = (flags & ~7) | owner | 0x8;
   - infantry: the first free sub-slot bit from **0x200 down to 0x20**
     (slot 4 first). Set the bit, owner in bits 0–2 and in bits 25–27.
     +0x52 = +0x53 = slot. Position += (table `0x4c93e8`[slot] − 16) px.
     The table is (16, 21), (23, 14), (23, 26), (7, 26), (7, 14) for slots
     0–4, so **the first infantry stands at (7, 14) in the cell**.
7. Fremen (kind 12) and the unit whose graphic name is "Stealth Raider"
   get flag 0x10 (inferred: cloaked).

### 8.4 Specials

- Saboteur and Fremen use order 0x30 (`FUN_0045a440`, doc 11). Its handler
  (`FUN_00457810` case 0x1d) clears every slot of that kind at 0x5A00 and
  calls `FUN_00442c20(…, check = 1)` for unit `0x6d60ec` (= 23 SABOTEUR) or
  `0x6d60ed` (= 21 FREMEN). The saboteur spawns once; Fremen spawn
  `0x6b8854` times, all on the same cell (infantry sub-slots).
- Airstrike and Death Hand do not use a factory exit.

## 9. Voices and samples (separate)

- **Table:** `Data\UI_DATA\samples.uib`, loaded by `FUN_00481cf0` into
  `0x7975b8` (count `0x7975bc`). File layout: u32 count (327), then per
  entry two strings, each a u16 length (includes the NUL) and the bytes:
  the **sample name** and the **file name** (in `GAMESFX\<file>.AUD`).
  Each table entry is {name, file, last-played tick}.
- **`FUN_0048dd90(name)`:** linear, case-sensitive search; returns the
  index, or 0 when not found.
- **House prefix:** callers look up `H_x`, `O_x` and `S_x` and call
  `FUN_0046f900(S, O, H, mode, limit, prio)`. It picks by the house byte
  `0x8cd4f0[local player]`: **0 → S_ (Atreides), 1 → H_, 2 → O_**; any
  other house plays nothing (there is no else branch; verified
  2026-10-16, our `World::voice` was playing S_ for them).
  samples.uib confirms S_ is Atreides: `S_BUILDREADY` → `ai_bdrdy`,
  `H_BUILDREADY` → `hi_bdrdy`, `O_BUILDREADY` → `oi_bdrdy`.
- **Placement-related samples:**

  | Name | File |
  |---|---|
  | `S_BUILDUP` | build1 |
  | `S_BUILDCONC` | chung |
  | `S_CANTBUILD` / `H_` / `O_` | ai_place / hi_place / oi_place |
  | `S_BUILDREADY` / … | ai_bdrdy / … |
  | `S_UNITREADY` / … | ai_unrdy / … |
  | `S_PRIMARY` / … | ai_prmry / … |

- **Corrected and completed in doc 59** (rings of 4, the mode-0 rule,
  the channel pick by mode, the volume formula's details).
- **Voices (`FUN_0046f820(id, mode, limit, prio)`):**
  - limit ≠ 0: skip if the sample played less than `limit` ticks ago,
    else store the tick (e.g. S_MONEYTICK 15, LOWPOWER 2000);
  - two queues, prio 0 and 1, each a ring of 5 sample ids
    (`0x795588`, heads `0x7955b0`, tails `0x795580`). A full queue drops
    the new id;
  - mode ≠ 0 (all the house voices above): queue it unless the speech
    channel is playing this id or it is already queued;
  - mode 0: queue only when `FUN_0046f6c0(i)` holds for both queues
  (`FUN_0046f6e0`), and
    the speech channel is idle or neither the new nor the playing sample
    is S_MONEYTICK. `FUN_0046f6c0(i)` is `head[i] == tail[i]`
    (`0x7955b0[i] == 0x795580[i]`): ring i is empty (verified 2026-10-16,
    as doc 59).
- **Positional effects (`FUN_0046f540(id, cx, cy)`):** distance in cells
  from the screen-centre cell (half view = 7 × 5 cells):
  - on screen: pan 0x40 (centre), volume 50;
  - off screen: volume = 35 − d·35 / map_w (min 5), d = max(|dx|, |dy|);
    if |dx| > 7, pan = 64 − |dx|·64 / map_w (min 16), mirrored
    (128 − pan) when the cell is to the right;
  - volume × SFX volume `0x4e5fc4` / 100;
  - `FUN_0046f210(…, 1, 5, 2)`: first idle channel of 1–5; if none, take
    the quietest channel if its volume is below the new one.

## 10. Open points

- Flags 6 and 7 of `FUN_004563b0`: verified 2026-10-16 (§6).
- The 0x10000000 cell bit: verified 2026-10-16 (§6 step 11).
- Primary slot on removal: doc 21 (`FUN_0046d9a0`).
- Set primary (verified): `FUN_00459ea0` sends order 0x20 with the side's
  selected buildings; ProcessOrder (switch case 0xd) passes only the first
  to `FUN_0044b1f0`, which clears 0x90 (kinds 2, 7–11) or 0x80 (kinds 12,
  13, 17–19) on the old primary, stores the index in the kind's slot and
  sets 0x80. Issued by P and by a click on the own selected building when
  `FUN_004432f0` allows it (the side owns more than one building of that
  name, player +0x245c1, and the kind is 2, 7, 8, 9, 11, 12, 13 or 17–19)
  and it is not primary yet; the click plays `*_PRIMARY`. The cursor over
  that building is 0xc then, else 0. Production speed already counts every
  factory of the name (§8.2), whichever is primary.
- The free harvester delivery for a new refinery: verified 2026-10-16
  (§6 step 13).
- Build-up drawing in `FUN_00428e30`: verified (§11).
- Harvester after its spawn move: state 8 → 0xc, verified 2026-10-16
  (§8.3).

## 11. Follow-up findings (implementation)

- **Build-up drawing** (`FUN_00428e30`, buildings with flags & 6):
  f = +0x3e / 3, image = `0x782aa8[rec+0x8a · 42 + f]` (animation 1). If the
  building has no body frames, or f < rec+0x89, that image **replaces** the
  body and there is no turret; otherwise it is drawn **over** the body.
  Animations hold up to 42 frames (`· 0x2a`).
- **Active overlay**: `0x716b00[rec+0x8b · 42 + +0x3f / rate]`, rate =
  +0x85 of the record whose index is rec+0x8b (the exe indexes the record
  table with the animation index). Records with flag 0x800000 set draw flag
  4 (`0x429135`; in 1.06 the light, heavy and high tech factories, records
  32..37 and 41..45), and the overlay of such an item is drawn by
  `FUN_00428e30` with `FUN_004349a0` (`0x429b7a`) instead of the keyed
  blit, unless the target flash makes it grey first. `FUN_004349a0`
  ("BlitTImageShade", 8-bit sprite on the 16-bit screen) writes **every**
  pixel (index 0 too) as, per 5-bit channel, `0x6d8da8[dst · 32 + src]`;
  `FUN_00466140` fills that table with min(i + j, 31). So the factories'
  working lights are **added** to the screen (saturating), verified
  2026-10-16 (our overlay was a plain keyed blit before).
- **One draw-list item per object** (`FUN_0042aa90`): body, turret and
  overlay are drawn in that order within the item. Only items are sorted.
  With a turret image, body and turret are offset by rec +0x60/+0x61 (so
  for turrets that field is a pixel offset, for factories the spawn cell).
- **DATA.R16 animation entries**: after the building icons and one single,
  animation 1 for indices 0..99 (frame counts `0x714a98[i]`), then
  animation 2 for 0..99 (`0x7954b0[i]`); the sum ends exactly at entry 6555.
- **Passable, exit cells** (`FUN_0045d990`, verified): a non-infantry unit
  may enter a cell with no unit bits (0x3e8), a building (0x10) of its own
  side and 0x10000000. So 0x10000000 marks the drive-through exit cells
  (the only reader, §6 step 11).
- **NEWOPTIONS / LOWPOWER / SILOSNEEDED / LOWMONEY** run at the end of
  `FUN_004467e0` for the local player (limits 0 / 2000 / 2000 / 2000); the
  shown list counts are copied after the NEWOPTIONS test.
