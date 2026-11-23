# 16: Shroud (1.06)

Covers the per-cell shroud byte, the start state, how the shroud is
revealed (circles, the edge pass), how it is drawn on the map and the
radar, what it changes in play, and save/load and crates. Decompiles come
from the local GhidraMCP server (`re/ghidra_http.py`); data from
`DUNE2000.DAT` and `Data/bin/CIRCLES.BIN`.

**Project rule.** Our version must never show more than the original.
The exact edge shapes, the "edge pass opens cells" rule (§3.3), and the
radar rules (§4.3) all decide what is visible. Copy them exactly.

Main functions:

| Address | Role |
|---|---|
| `FUN_0044c6d0` | load map; every cell state = 0x11 (with "read cells" = 1) |
| `FUN_0044e3b0` | reveal a circle (centre cell, radius 0..7), then the edge pass |
| `FUN_0044e330` | reveal around a building (record view range +0x0e) |
| `FUN_0044cd10` | edge pass over a rect: new edge shapes, radar copy |
| `FUN_0044cff0` | per-tick unit reveal (allied units, 1/8 of them per tick) |
| `FUN_0049f4d0` | full reveal: 8 × `FUN_0044cff0`, then all own buildings |
| `FUN_0044c480` | open the whole map (every state = 0) |
| `FUN_0044f440` | anti-cheat checksum of the shroud bytes |
| `FUN_00423fb0` | shroud draw (inside the world draw `FUN_00428ca0`) |
| `FUN_00431560` | full shroud blit: black 32×32 |
| `FUN_004310c0` → `FUN_004310f0` | edge blit: darken by the image ("BlitTImageShade") |
| `FUN_00495b90` / `FUN_00496030` | unit / building fires: the shooter is revealed (§3.5) |
| `FUN_0049ebf0` | crate pick-up: reveal map (2) and "no map" (3) |

"Verified" means read from the code or the data. "Inferred" is marked.
Ticks are game ticks.

## Corrections to earlier docs

- **doc 06, cell table:** the +8 byte is the shroud byte. Low nibble =
  shape (0 open, 1 full shroud, 2–15 edge), bits 4–5 = draw variant (§1).
- **doc 10, `0x4dfb04`:** it is a constant 1 in `.data`, never written
  (6 reads, no writes; a byte search of the exe finds only those 6 code
  references). It is not set by a mission, skirmish option or "/CHEATED".
  The shroud is always on. "/CHEATED" is the Westwood Online anti-cheat
  (§6.3).
- **doc 11, §4 radar units:** "byte +8 is 0" is confirmed. It means the
  whole byte is 0, not only the low nibble (see §1.2). The TODO "where
  shroud reveal copies pixels into `0x6d7ba0`" is answered in §4.3.
- **doc 12, §2 draw order:** the shroud draw is **inside** `FUN_00428ca0`,
  as its last pass before the mission won/lost banner. It is not after the
  whole call. Order: placement grid, `FUN_0042af00`, `FUN_00427d50`
  (buildings), `FUN_0045d550` (pixel particles), `FUN_00428e30` (objects,
  explosions, aircraft), `FUN_00428070`, `FUN_00428220` (click puffs),
  **`FUN_00423fb0` (shroud)**, then the won/lost image. So the click puffs
  are also under the shroud.

## 1. The shroud byte (cell +8)

One byte per cell, in the game map (`0x517df0`, cell +8 = `0x517df8`).
There is **one shroud for the whole game**, and it belongs to the local
player (`0x798544`). There is no per-player shroud. AI players do not read
it (§5.5).

### 1.1 Bit layout (verified)

| Bits | Meaning |
|---|---|
| 0–3 | shape: 0 = open, 1 = full shroud, 2–15 = an edge shape |
| 4–5 | draw variant = `(x & 1) | (y & 1) << 1` |
| 6–7 | always 0 |

The draw image index is the **whole byte** (0..63), §4.1.

Writers:

| Writer | Value |
|---|---|
| map load `FUN_0044c6d0` | 0x11 |
| reveal circle `FUN_0044e3b0` | `byte &= mask`; mask is 0 inside the circle, 0xff outside, so inside cells become **0** |
| edge pass `FUN_0044cd10` | only cells with shape ≠ 0: `variant << 4 | shapeTable[mask]` |
| shooter reveal (§3.5) | 0 |
| "open map" `FUN_0044c480` | 0 everywhere |
| crate "no map" (§6.2) | 1 everywhere |

### 1.2 What "shrouded" and "revealed" mean to each reader

Two different tests are used. They are not the same thing.

- **Full shroud = `byte & 0xf == 1`.** Used by: the terrain draw (no tile,
  doc 06), the shroud draw (black), the click and cursor (`FUN_004433a0`),
  the tooltip (`FUN_0044b5c0`), the pixel particles (`FUN_0045d550`).
- **Fully open = `byte == 0`.** Used by: radar unit pixels
  (`FUN_00426000`), the worm-sign speech (`FUN_00497b90`), mission
  condition type 7 (`FUN_00453b90`), the 8-bit radar pixel write
  (`FUN_00462010`), and the radar restore after a load (`FUN_004418b0`).

A cell can be open in shape but not 0: the edge pass can give shape 0 to a
cell and still write the variant bits (§3.3). Such a cell is drawn open,
but by the second test it is **not** open: units on it are not shown on
the radar, and so on. It stays like that until a reveal circle or a
shooter reveal sets it to 0. Copy this exactly.

### 1.3 Fog?

There is no fog of war. The shroud only opens. Nothing closes a cell
again, except the "no map" crate (§6.2), which closes the whole map at
once.

## 2. Start of a mission (verified)

Order inside the mission start (`FUN_00448550`):

1. `FUN_0044c6d0(map, 1)`: reads the map file. Every cell: state = 0x11,
   marks = 0. So **the whole map starts as full shroud**. (This call is
   made, when no saved game is being loaded, if `gGameType ≠ 0` or
   `FUN_00492c40(0, 1)` is true. `FUN_00492c40` is the CD check: it
   returns 1 when the CD is found (or not needed, `0x797e30`); it
   returns 0 only after the player gives up in the INSERT_CD box, and
   then sets gGameState `0x4dfb08` = 5, so the main loop (`while
   gGameState == 2`) never runs and the game quits. There is no other
   campaign path: a loaded game reloads the map through `FUN_004418b0`
   → `FUN_0044c6d0(map, 0)`, the only other caller.) It also sets the
   circle mask pointers (§3.1).
2. `FUN_00469790` (mission set-up):
   - `FUN_00462140` fills the radar colours and clears the **shown** radar
     to black (§4.3);
   - spice smoothing (doc 06);
   - map units (`FUN_00455870`): **no reveal** when a unit is created;
   - concrete (`FUN_00456210`, doc 12 §5): for the local player, every
     cell of the mask gets `FUN_0044e330` with the concrete record. This
     runs before the slab checks, so it reveals even where no slab is
     laid;
   - buildings (`FUN_004563b0`, doc 12 §6 step 10): local player only,
     `FUN_0044e330` around the building.
3. Later in the same start path, after a saved game is loaded if there is
   one: `FUN_0049f4d0(local player)`:
   - 8 × `FUN_0044cff0` (§3.4). Each call handles the units whose index
     & 7 equals the call counter & 7, so 8 calls reveal around **every
     allied unit** once;
   - every building in the **local player's** list (+0x10, next +0x7c):
     `FUN_0044e330(record, cx, cy)` with
     `cx = (x >> 16) >> 5`, `cy = ((y >> 16) − record.height_px) >> 5`
     (object x / y are 16.16 pixels).

**Our order (fixed 2026-10-13, O9 playtest):** the World now gets the
local player in its constructor, before the pre-placed objects are
created. Before, the slot was set afterwards, so in every Harkonnen and
Ordos mission slot 0's (Atreides) pre-placed buildings and concrete
revealed at creation. Also fixed: the per-tick unit reveal (§3.4) only
counted the local player's units, not every allied player's.

There is no mission field for a start reveal. What is open at the start is
only the circles around the local player's buildings and concrete, and
around the units of every player allied with the local player. Mission
events can reveal more later (§5.6).

## 3. Revealing

### 3.1 Circle masks (verified)

`FUN_0044e3b0(cx, cy, r)`:

- r > 7 is a fatal error ("Shroud circles only defined up to size %d").
- The mask for r is `(2r+1) × (2r+1)` bytes, rows top to bottom,
  pointer in `0x547ff0[r]`.
- For each cell (cx+dx, cy+dy), dx, dy = −r..r, inside the map (unsigned
  compare, so off-map cells are skipped): `state &= mask`.
- Then the edge pass on the rect from (cx − r − 1, cy − r − 1) to
  (cx + r + 2, cy + r + 2), half-open; a negative start is set to 0.

**The masks come from `Data\bin\CIRCLES.BIN`** (680 bytes = 1 + 9 + 25 +
49 + 81 + 121 + 169 + 225). `FUN_00466140` reads it because the byte
`0x4eb014` is 1. (With 0 it would build them from `SCircles.bmp`, a
development path; not used.) Every byte is 0 (reveal) or 0xff (keep).
Read from the 1.06 file (`#` = revealed):

```
r0  #          r1  .#.        r2  .###.
                   ###            #####
                   .#.            #####
                                  #####
                                  .###.
r3  ..###..    r4  ...###...   r5  ...#####...
    .#####.        .#######.       ..#######..
    #######        .#######.       .#########.
    #######        #########       ###########  (x5)
    #######        #########       .#########.
    .#####.        #########       ..#######..
    ..###..        .#######.       ...#####...
                   .#######.
                   ...###...
r6  ....#####....   r7  .....#####.....
    ..#########..       ...#########...
    .###########.       ..###########..
    .###########.       .#############.
    #############  (x5) .#############.
    .###########.       ###############  (x5)
    .###########.       .#############.
    ..#########..       .#############.
    ....#####....       ..###########..
                        ...#########...
                        .....#####.....
```

Load these from the player's CIRCLES.BIN, not from a copy in our code.

### 3.2 Radius and centre

- **Units:** record +0x10 (view distance, 0..7; `0x77e260 = 0x77e250 +
  0x10`). Centre = unit +0x4c / +0x4d (the "to" cell, doc 09).
- **Buildings:** `FUN_0044e330(record, cx, cy)`. Radius = record +0x0e
  (`0x6dc54e`). Centre = (cx + width_px / 64, cy + height_px / 64), with
  +0x87 = width and +0x86 = height in pixels (doc 08). So the centre is
  (cx + 1, cy + 1) for both a 2×2 building (its bottom-right cell) and a
  3×3 building (its middle cell); (cx, cy) for a 1×1.
- **Mission event 0xe:** record +0 = x, +4 = y, +0xe = radius (§5.6).

### 3.3 Edge pass (`FUN_0044cd10`, verified)

Arguments: a rect [x0, x1) × [y0, y1) and the map width / height. The
rect is clipped to the map. Cells are walked **row by row, left to
right, and changed in place**. So a cell sees the new values of its W, NW,
N and NE neighbours, and the old values of the others.

For every cell in the rect:

1. **If shape ≠ 0:** build an 8-bit mask of the neighbours whose shape is
   ≠ 0 (any shroud, full or edge). Bits: NW 1, N 2, NE 4, W 8, E 0x10,
   SW 0x20, S 0x40, SE 0x80 (the same bits as the spice tiles, doc 06).
   Off-map neighbours count as shroud:
   - x = 0: mask = 0x29 (NW, W, SW), and W / NW / SW are not read;
   - x = width − 1: mask |= 0x94 (NE, E, SE), and those are not read;
   - y = height − 1: mask |= 0xE0 (SW, S, SE); S is not read, and SW / SE
     are read only when y < height − 1;
   - y = 0: mask |= 7 (NW, N, NE); N is not read, and NW / NE are read
     only when y > 0.

   Then `state = ((y & 1) << 1 | (x & 1)) << 4 | table[mask]`, with the
   256-byte table at **`0x4e0b18`** in the exe.
2. **If shape ≠ 1 after step 1:** copy this cell's radar pixel from the
   true buffer to the shown buffer (§4.3).

**The table opens cells.** Only 95 masks give a shroud shape. Every other
mask gives 0, so the cell becomes open. For example a lone shrouded cell,
or a line one cell wide, is opened by the pass. The non-zero entries
(read from the 1.06 exe):

| Shape | Masks |
|---|---|
| 1 (full) | 0xFF |
| 2 | 0x7F |
| 3 | 0x1F 0x3F 0x5F 0x9F 0xBF |
| 4 | 0xDF |
| 5 | 0x6B 0x6F 0x7B 0xEB 0xEF |
| 6 | 0xD6 0xD7 0xDE 0xF6 0xF7 |
| 7 | 0xFB |
| 8 | 0xF8 0xF9 0xFA 0xFC 0xFD |
| 9 | 0xFE |
| 0xA | 0xD0–0xD5 0xD8–0xDA 0xDC 0xDD 0xF0–0xF5 |
| 0xB | 0x68–0x6A 0x6C–0x6E 0x78–0x7A 0x7C 0x7D 0xE8–0xEA 0xEC–0xEE |
| 0xC | 0x16 0x17 0x1E 0x36 0x37 0x3E 0x56 0x57 0x5E 0x76 0x77 0x96 0x97 0x9E 0xB6 0xB7 0xBE |
| 0xD | 0x0B 0x0F 0x1B 0x2B 0x2F 0x3B 0x4B 0x4F 0x5B 0x8B 0x8F 0x9B 0xAB 0xAF 0xBB 0xCB 0xCF |
| 0xE | 0xDB |
| 0xF | 0x7E |

Our code should read the table from the player's exe (like the other
exe tables, `src/sim/exe_tables.cpp`), not hard-code it.

Inferred from the masks: 0xA = shroud to the south, 0xB = west,
0xC = east, 0xD = north; 2–9, 0xE, 0xF are corner shapes. Not needed to
reproduce it.

### 3.4 When units reveal (`FUN_0044cff0`, verified)

Called **once per game tick** from the tick block of `FUN_00448550` at
`0x449aeb`: after the model update `FUN_0044aca0` (orders, every object,
repairs, power, storage; `0x449ad8`) and the debug cell check
`FUN_0044d0e0`, and before the production `FUN_004467e0` (`0x449af0`).
So units reveal from where this tick's object pass left them, and a unit
the production builds this tick reveals from the next tick on.
(Corrected 2026-10-16: we had it at the top of the tick, before the AI
and the script.) Each call:

1. `counter (0x54804c) += 1`. The counter is never reset and not saved
   (only `FUN_0044cff0` touches it).
2. For each player p = 0..7 with `alliance[p·8 + local] == 0`
   (`0x798830`; the local player is included): for each unit in p's list
   (+8, next +0x7c), in list order, reveal when all hold:
   - `(unit +0x8c) & 7 == counter & 7` (+0x8c = the unit's index);
   - view distance (record +0x10) ≠ 0;
   - unit flag +8 & 0x100 is clear (a carried unit, or a loaded carryall,
     doc 14, does not reveal).

   Reveal = `FUN_0044e3b0(+0x4c, +0x4d, view distance)`.

So **each allied unit reveals once every 8 ticks**, at its "to" cell. Not
on each step or cell change. Units of players not allied to the local
player never reveal.

Buildings do not reveal per tick. They reveal when created (local player
only), and in `FUN_0049f4d0` (mission start, load, "no map" crate). So
**allied buildings never reveal**; only allied units do.

**The counter phase** (verified): `0x54804c` is 0 at program start
(`.data`), and its only writer is `FUN_0044cff0` (`0x44d01f`), called once
per tick (`0x449aeb`) and 8 times by each `FUN_0049f4d0` (mission start,
load, "no map" crate). So at a mission start its value mod 8 is the
number of ticks played since the program started, mod 8 (the 8-call
sweeps add 0 mod 8). For the first game of a run that is 0: the start
sweep handles index residues 1, 2, …, 7, 0 and tick t reveals the units
with index & 7 = t & 7. Later games of the same run start where the
previous ones left it. Ours: `GameSession::reveal_counter` carries it
from game to game (`GameSetup::reveal_counter`, set before the start
sweep); a loaded game also goes on with the run's counter, not a saved
one (§6.1). Only a game loaded in a fresh run can differ from the
original, whose counter would hold that earlier run's history.

### 3.5 Being shot reveals the shooter (verified)

When a unit (`FUN_00495b90`) or building (`FUN_00496030`) fires and the
target belongs to the local player (target owner == `0x798544`):

- the shooter's cell state = 0. Unit: its +0x4c / +0x4d cell. Building:
  cell ((x >> 16) + 16) >> 5, ((y >> 16) − height_px + 16) >> 5;
- edge pass on the rect (cx − 2, cy − 2) to (cx + 2, cy + 2), half-open
  (so x − 2..x + 1, y − 2..y + 1; not symmetric).

This is done for every shooter, allied or not, any range.

## 4. Drawing

### 4.1 Map (`FUN_00423fb0`, verified)

- Images: **DATA.R16 entries 38–101** (64 × 32×32, 8-bit, no palette),
  loaded in order into `0x795028[64]` (doc 10 §8 lists them as "?").
  Image = `0x795028[state]`, i.e. entry `38 + state byte`.
- Walk: first cell = (scroll_x >> 5, scroll_y >> 5). Screen x starts at
  −(scroll_x & 31) and goes in steps of 32 while x < view_w − (scroll_x &
  31) + 32. Same for y, from y0 = 20 (`0x4e41dc`), using view_h. Clip
  rect = (0, 20) to (view_w, 20 + view_h). No map bounds check.
- Per cell:
  - shape 0: nothing;
  - shape 1: **`FUN_00431560`: fills the clipped 32×32 rect with 0
    (black).** The image pixels are not used, only its size;
  - shape 2–15: **`FUN_004310f0` shade blit**: per pixel, per 5-bit
    channel, `c = max(c − idx, 0)` with idx = the image pixel (signed
    char), via the table `0x790110` (doc 14 §5.2, blit flag 0x2). On a
    565 surface green loses its low bit.
- The edge image pixel values are 0 and 2..31 (read from the 1.06
  DATA.R16 entries 38–101: 8-bit, never 1 and never above 31), so the
  signed-char read never goes negative and a channel loses at most 31.

**What this hides (verified):** everything in `FUN_00428ca0` before the
shroud pass is drawn first. Full shroud covers it with opaque black:
terrain (not drawn there anyway, doc 06), placement grid, buildings,
units, bullets, explosions, aircraft and shadows, click puffs. Nothing
skips drawing because of the shroud, except the pixel particles
(`FUN_0045d550`: a pixel is not drawn if its cell is full shroud).
**On edge cells, objects stay visible but darkened.** The won/lost image
is drawn after the shroud.

### 4.2 Pixel particles (`FUN_0045d550`, verified)

The 4096-entry list at `0x6b9920` (float x, y; colour at +0x14). A pixel
is drawn only if `0x4dfb04 == 0` or its cell (round(x) >> 5,
round(y) >> 5) does not have shape 1. Inferred: worm trails / dust.

### 4.3 Radar (verified)

Two buffers (doc 11 §4): true colours `0x781e54` and shown `0x6d7ba0`.

- **Mission start:** `FUN_00462140` clears the shown buffer to 0 (black)
  and fills the true buffer.
- **Edge pass:** every cell in its rect with shape ≠ 1 (after the pass)
  gets its true pixel copied to the shown buffer. So **edge cells show
  their terrain on the radar**.
- **Pixel updates** (building placed or removed, spice eaten to 0, a thin
  bloom blob; the full list is doc 11 §4): the 16-bit
  writer `FUN_004620b0` always writes the true buffer, and writes the
  shown one only if the shown pixel is not black (`0x5174f8` = RGB 0).
  The 8-bit writer `FUN_00462010` writes the shown one only if the cell
  byte == 0. We render 16-bit, so we use the first rule.
- **Units on the radar** (`FUN_00426000`): only on cells whose byte is 0
  (§1.2).
- **Crate "reveal map":** the whole true buffer is copied to the shown
  one (`FUN_004305f0`).
- **Crate "no map":** the shown buffer is cleared to black (`FUN_00430860`).
- **Loaded game:** the shown buffer is rebuilt from the true one only for
  cells whose byte is 0 (`FUN_004418b0`: `FUN_00462140` clears it, then
  `0x441e4c` (8-bit) / `0x441ebb` (16-bit) copy). Edge cells stay black on
  the radar until a later edge pass covers them; the start sweep
  `FUN_0049f4d0` that follows the load (§6.1) runs edge passes around
  the units and own buildings at once. Ported 2026-10-16
  (`World::rebuild_shown_radar`, called by `GameScreen::load_saved`; our
  save keeps `radar_known`, the rebuild overrides it as the exe does).

## 5. Effects on play

### 5.1 Mouse (verified)

- **Cursor** (doc 10): over full shroud (`0x4dfb04` and shape 1): 1 with
  units selected, else 0.
- **Left-button release on full shroud:** with units selected, a move
  order to that cell (`FUN_00459450`) and the click puff. Without units,
  nothing. No hit test is made, so nothing under full shroud can be
  selected or attacked with a click.
- **Edge cells** are not "shroud" for input. The normal hit test
  (`FUN_004a5270`) runs, so a visible (darkened) enemy on an edge cell can
  be clicked and attacked. `FUN_004a5270` has no shroud test. Its last
  argument is 1 when the cell is not the local player's and has unit
  occupancy bits (`flags & 1000`, decimal); it is not a shroud flag.

### 5.2 Tooltip (verified)

`FUN_0044b5c0`, after the hover delay (`0x51756c` ≥ 0x12): over a full
shroud cell it shows the string `"Unrevealed"` (key at `0x4e07b0`, text in
`0x5175e0`) and does no hit test.

### 5.3 Placement

Building placement ignores the shroud (doc 12 §2). The placement
functions are not in the list of readers of the byte (§5.5).

### 5.4 Sounds

The worm-sign speech (H/O/S_WORMSIGN) in `FUN_00497b90` plays only when
the worm is on screen and the byte of its +0x4a / +0x4b cell is 0. Two
places, both with this test. Other sounds have no shroud test (inferred
from §5.5).

### 5.5 AI, targeting, movement

The complete list of direct references to `0x517df8` is: the terrain and
shroud draw, the radar, load, input, tooltip, the shroud functions,
mission conditions, particles, the two fire functions, the worm sign and
the crate. No AI, target search or path code reads it: **the AI and
automatic targeting ignore the shroud**. Reads through a cell pointer
were checked too: of the references to the cell table bases `0x517df0`
and `0x517df4` (the shroud byte is +8 / +4 from them), only four form a
pointer (`lea` at `0x4230a0`, `0x44ecef`, `0x44f281`, `0x49c77d`), and
those read +0 (tile) and +4 (flags) or write +9 (the crater counter);
none reads +8. All other references index the table directly.

### 5.6 Mission scripts (verified code, names inferred)

- **Event type 0xe** (`FUN_00454120`): `FUN_0044e3b0(+0, +4, +0xe)`. It
  reveals a circle for the local shroud, whatever the event's player.
- **Condition type 7** (`FUN_00453b90`): true when the cell
  (`0x6b828c`, `0x6b8290`) has byte 0 and the counter at `0x6b8288` is
  not 0; the counter then drops by 1. Inferred: "cell revealed".

## 6. Other

### 6.1 Save / load (verified)

The save holds the map block `0x517de8`, 0x30008 bytes (width, height and
all 128 × 128 cells), so the shroud bytes are saved and loaded as is.
After loading, `FUN_0044c6d0(map, 0)` reloads the tile data without
touching the cells, the radar is rebuilt (§4.3), and the normal start path
then runs `FUN_0049f4d0(local)` (§2 step 3; the load at `0x448bc4`, the
sweep at `0x448c36`, after `*_GAMELOADED`). The tick counter `0x54804c`
is not saved. Ours: the World's save format holds the counter (the
determinism tests use it), but `GameScreen::load_blob` puts the run's
counter back after the load, and `load_saved` runs the radar rebuild and
`reveal_start` as the exe does.

### 6.2 Crates (`FUN_0049ebf0`, verified)

Only when the local player picks the crate up:

- **Type 2 (reveal map, EX_CrateReveal):** `FUN_0044c480` sets every byte
  to 0, and the whole true radar is copied to the shown radar.
- **Type 3 (no map, EX_CrateNoMap):** shown radar cleared to black, every
  byte set to **1** (not 0x11), then `FUN_0049f4d0(player)` re-reveals
  around all allied units and the player's own buildings.

### 6.3 Multiplayer and anti-cheat

- At the end of a network game (`0x7984c4` set, two places in
  `FUN_00448550`: `0x449c7f` and `0x449eda`, each under `0x7984c4 != 0
  && 0x517400 == 0`), `FUN_0044c480` opens the whole map (shroud bytes
  only; the shown radar stays as it is). **Not in skirmish**: a skirmish
  has `0x7984c4` = 0 (gGameType 1 is not the network flag). Ported
  2026-10-16 (`GameScreen`, the network end, `World::open_map`).
- Every function that writes the shroud first compares `FUN_0044f440` with
  `0x548010`, and stores the new sum after writing. `FUN_0044f440` sums the
  bytes of every 4th cell in every 4th row. If the sum changed outside
  these functions (a memory trainer), `0x517500[local] |= 1`. The Westwood
  Online score upload (`FUN_0040d2d0`) then adds "/CHEATED" and
  ":SHROUD_OFF". Not needed for us; do not implement.

## 7. Implementation notes

- Store the byte exactly as the original does (shape + variant).
- Keep both tests of §1.2 separate.
- Run the unit reveal once per game tick with the index & 7 stagger.
- Port the edge pass with its in-place row order and its map-edge rules.
- Take CIRCLES.BIN and the table at `0x4e0b18` from the player's data.
- Draw full shroud as opaque black, edges as the shade blit, both after
  the click puffs and before the won/lost image.
