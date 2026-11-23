# 31. Spice blooms (and the cell-object table they share with crates)

Target: Dune 2000 1.06 US (`DUNE2000.DAT` image). Everything below was read
from the disassembly / decompilation of the functions named; "verified"
means checked against the instructions, not only the decompiler. Guesses
were marked as open points; all were closed on 2026-10-16.

This replaces the short notes in doc 13 §9 and doc 26 §5 (see
§10 for the corrections).

## 0. Functions and globals

| Address | Role |
|---|---|
| `FUN_00469790` | mission set-up: map specials 0x29–0x2D create blooms |
| `FUN_0044e770` | clear the cell-object table (active = 0), from `FUN_00448550` set-up (`0x448880`) |
| `FUN_0044e790` | first free slot (active == 0), 0xFF when all 30 are used |
| `FUN_0044e8e0` | add a cell object (x, y, timer, type, image, regrow) |
| `FUN_0044e970` | find the active object at (x, y): returns its type (0xFF none) and its index |
| `FUN_0044e9d0` | "pick up" the object at (x, y) (§6.2): returns its type, 0x0B none |
| `FUN_0044ea70` | per-tick update of all cell objects (§3) |
| `FUN_0044ef10` | burst: throw spice blobs, play `S_SPICEMOUND` (§4) |
| `FUN_0044f110` | after a burst: re-arm the bloom or remove it (§4.3) |
| `FUN_0044e7c0` / `FUN_0044e800` | add a random crate (§8) |
| `FUN_0049be60` | bullet update: a SPICEEXPL blob lands and lays spice (§5) |
| `FUN_0049d2e0` | damage function: bursts a ripe bloom at the impact cell (§6.1) |
| `FUN_0049ebf0` | crate / bloom pick-up by a moving unit (§6.2) |
| `FUN_0041f670` | AI DoStopStartStuff: crate scan that also eats blooms (§6.3) |
| `FUN_00428e30` | draws the cell objects (§7) |
| `FUN_004b1810` (thunk `FUN_00448460`) | `rand()`, the shared MSVC LCG (doc 06). The two pushed args (file, line) are ignored |

| Global | Meaning |
|---|---|
| `0x517b98` | cell-object table, 30 × 12 bytes (0x168, saved with the game, doc 27) |
| `0x548048` | u8 max number of crates (§8); 0 in the campaign |
| `0x6d6117` | explosion type EX_SPICEBLOOM, set by name from Templates (= **57**) |
| `0x6d610a` | bullet type SpiceExpl (= **21**, name `SPICEEXPL`) |
| `0x7951a8[8]` | images of `Crate.bmp`, 8 frames of 32×32 = DATA.R16 entries 102–109 (doc 10) |
| `0x5173f4` | game tick counter |

## 1. The cell-object table (`0x517b98`, verified)

30 entries of 12 bytes. Every loop over it covers exactly 30 entries
(`FUN_0044e790`, `FUN_0044e970`, `FUN_0044e9d0`, `FUN_0044ea70`, the
draw loop with count 0x1E).

| Off | Type | Meaning |
|---:|---|---|
| +0x00 | u8 | cell x |
| +0x01 | u8 | cell y |
| +0x02 | u8 | **type**: 0–5 crate types, 7 = bloom (unripe), 8 / 9 / 10 = ripe bloom, small / medium / large. 6 is never created |
| +0x03 | u8 | active (1) |
| +0x04 | u8 | **image index** into `0x7951a8` (crates 0; blooms 4..7, tied to the type: image = type − 3) |
| +0x05 | u8 | **regrow count**: how many more times the bloom comes back after a burst (crates 0) |
| +0x06..07 | | unused (not written) |
| +0x08 | u32 | timer, ticks |

Cell flag **0x1000** = "there is a cell object here". It is set by
`FUN_0044e8e0` and cleared whenever an entry is deactivated. Lookups by
cell (`FUN_0044e970`, `FUN_0044e9d0`) return the first active entry with
that x, y in table order.

### 1.1 Add (`FUN_0044e8e0(x, y, timer, type, image, regrow)`, verified)

The decompiler shows it as `__thiscall`. The stack arguments are the six
above.

```
i = first entry with active == 0 (FUN_0044e790); if none (0xFF): return, nothing else
e[i].active = 1; x; y; timer; type; image; regrow
cell(x, y).flags |= 0x1000
```

## 2. Map load (`FUN_00469790`, verified)

For each cell, rows outer (y), columns inner (x), the map special value:

| Special | regrow (+0x05) |
|---:|---:|
| 0x29 | 5 |
| 0x2A | 10 |
| 0x2B | 25 |
| 0x2C | 50 |
| 0x2D | 255 |

Each case does, in this order:

```
t = rand() % 1000 + 2000                       // 1 rand() per bloom spot, in map order
FUN_0044e8e0(x, y, t, type = 7, image = 4, regrow)
cell.tile  = 0x319                             // thin spice tile
cell.flags = 0x217000                          // spice level 2, sand, infantry, vehicles, 0x1000
```

- The flags are **assigned**, not or-ed, after `FUN_0044e8e0`, so the
  cell always ends with 0x1000, even when the table was full and no object
  was made (31st and later bloom spots). Such a cell keeps 0x1000 for the
  whole mission: it blocks building placement (mask 0x13F8, doc 12) and
  crate spawning (§8), but nothing else reads it without finding an
  object (`FUN_0044e9d0` returns 0x0B and nothing happens). Port this.
- 255 is not "forever": the count is a byte that goes down by one per
  burst (§4.3).
- The table is cleared by `FUN_0044e770` earlier in the same set-up
  (`0x448880`). After `FUN_00469790`, `FUN_0044ec50` smooths all spice
  tiles (doc 06), which may change the bloom cell's tile from 0x319.
- These rand() calls happen in the order of the specials in the map,
  between the other set-up rand() users (sand tiles etc.; see the set-up
  order in our `GameMap` constructor, which currently skips them).

## 3. Per-tick update (`FUN_0044ea70`, verified)

Called once per simulation tick from the tick block of `FUN_00448550`
(`0x449ac9`): after the tick counter, the AI, the mission script and the
defeat checks (`FUN_00455480`), and before `FUN_004466e0`,
`FUN_00446690` and `FUN_0044aca0` (which runs the model update
`FUN_00458e90`). So blooms update before units, bullets and explosions.

```
crates = 0
for i in 0..29:
    e = table[i]
    if e.active != 1: continue
    if e.type < 6: crates += 1                  // counted before its timer runs out
    e.timer -= 1
    if e.timer != 0: continue
    c = cell(e.x, e.y)
    if e.type < 7 or e.type > 10:               // crates (and 6)
        e.active = 0; c.flags &= ~0x1000        // the crate expires
    elif e.type <= 9:                           // 7, 8, 9: grow
        if (c.flags & 0x7003F8) == 0:           // no spice and no unit on the cell
            e.image += 1; e.type += 1
            s = tick & 0xFF
            do s = (s + 1) & 7 while !player[s].active   // result unused (dead loop)
            if EX_SPICEBLOOM (0x6d6117) != 0xFF:
                FUN_00456df0(owner = tick & 7, EX_SPICEBLOOM,
                             x = e.x·32 + 16, y = e.y·32,   // top edge, not centre
                             height 0, flags 0, delay 0, 0, 0)
        e.timer = rand() % 1000 + 2000          // always, grown or not
    else:                                       // type 10
        if (c.flags & 0x7003F8) == 0:
            FUN_0044ef10(e.x, e.y, 6)           // burst, radius 6 (§4)
            FUN_0044f110(i)                     // re-arm or remove (§4.3)
        else:
            e.timer = rand() % 1000 + 2000
if crates < max_crates (0x548048): FUN_0044e7c0()   // at most one new crate per tick (§8)
```

Notes:

- **The bloom only grows on a clear cell.** 0x7003F8 = spice level bits
  (0x700000) + unit occupancy (0x8, 0x3E0, doc 06). A fresh bloom spot has
  spice level 2 on its own cell (§2), so it stays at type 7 until that
  spice is harvested away (and no unit stands on it). Every timer expiry
  on a blocked cell just reloads the timer; the stage does not move.
- Growth: 7 → 8 → 9 → 10 takes three clear expiries, then a fourth clear
  expiry at type 10 bursts it by itself. Each wait is 2000–2999 ticks
  (100–150 s at 20 ticks/s).
- rand() order inside one tick: entries in table order; per entry at most
  one rand() (the timer reload), except the type-10 burst, which draws the
  burst's rand() calls (§4.1) and then possibly one in `FUN_0044f110`.
  The crate spawn's rand() calls come after the whole loop.
- **Explosion owner quirk:** the player search loop computes a side but
  the explosion is created with `owner = tick & 7`, whatever that side
  is. `FUN_00456df0` does not check that the side is active; it appends
  to that player's explosion list. The update and draw loops cover sides
  `0 .. 0x798628 − 1`. `0x798628` is the number of sides created, and it
  is always **8**: the set-up loop `0x448826..0x448835` calls
  `FUN_004a45d0` for sides 0..7 in every game type (each call sets the
  side active and counts `0x798628` up; it is reset to 0 only at
  `0x448811` just before), and a load restores the saved count
  (`FUN_004418b0`, `0x441a3f`). So the effect always goes into an
  updated and drawn list and always shows; the owner has no other
  effect. All 8 sides are active, so the search loop always ends.
  (Verified 2026-10-16.)
- EX_SPICEBLOOM (57): default flags 0x200 (additive blit), no sound
  (attribute +4 = −1), so **growth is silent**. Drawn by the explosion
  system (doc 14 §4–5), at height 0, i.e. in the low explosion pass.
- Not covered here: the explosion's frame count (from its graphics).

## 4. The burst

### 4.1 `FUN_0044ef10(x, y, n)` (verified)

`n` = 4, 5 or 6 (type 8, 9, 10). Throws `2·n²` blobs (32, 50 or 72).

```
side = 0
repeat 2·n·n times:
    r1 = rand(); r2 = rand(); r3 = rand()       // always these three, in this order
    a = r1 & 0x1F                               // direction 0..31
    s = r3 % n                                  // distance step 0..n-1
    tx = x + sdiv2048(DX[a][s])                 // DX 0x6e9840, DY 0x704a98 (doc 14 §1.5)
    ty = y + sdiv2048(DY[a][s])                 // sdiv2048 = C signed /2048 (toward zero)
    if 0 < tx < map_w and 0 < ty < map_h and (cell(tx, ty).flags & 0x710000) != 0:
        do side = (side + 1) & 7 while !player[side].active   // round robin, from side 1
        jy = rand() & 0x1F                      // y jitter first (line 0x8d5) ...
        jx = rand() & 0x1F                      // ... then x jitter (line 0x8d4)
        FUN_00456a30(side, SPICEEXPL, delay = r2 & 0xF, firer = 0xFFFF,
                     x0 = x·32 + 16, y0 = y·32 + 16,
                     tx·32 + jx, ty·32 + jy, tidx = 0xFFFF, tside = 0xFF)
sample = FUN_0048dd90("S_SPICEMOUND"); FUN_0046f540(sample, x, y)   // positional, doc 12 §9
```

- The step tables hold `trunc(sin/cos(angle) · s · 0x800)`; the code adds
  0x7FF to negative values before `>> 11`, i.e. C division toward zero.
  So a blob lands at most n − 1 cells away, s = 0 is the bloom's own
  cell, and the spread is denser near the centre.
- Row 0 and column 0 are never targets (`> 0`, not `>= 0`).
- Target cells must be sand or spice (0x710000 = level bits + sand).
  Rock, cliffs, buildings: no blob, but r1–r3 were still drawn.
- `side` restarts at 0 on every call; only accepted blobs advance it.
  The owner matters for update order (bullet lists per player) and for
  the damage call (§5), not for hostility (SpiceExpl is excluded, doc 18
  §2.2).
- The sound plays once per burst even if no blob was thrown.
- The bloom cell itself is not changed here.

### 4.2 The blob bullet (SPICEEXPL, Templates bullet 21, verified from the data)

Speed 6 px/tick (0x60000), damage 75, warhead 0 `W_FIRE` (radius 64 px,
NONE 90 %, WALL 5, BLDG 65, WOOD 50, LIGHT 40, HEAVY 30, CONCRETE 100,
CY 20, HARV 25; doc 18 §1.2), range 128, flags 0x11 = arc + trail,
trail explosion EX_TRAIL (16), impact explosion EX_TANKGUN (6, no
sound), no fire sound, not anti-air. Flight as any arc bullet (doc 18
§4.1): `ticks = (dist << 16) / speed`, starting after `delay` ticks
(not drawn, not moved meanwhile).

### 4.3 Re-arm or remove (`FUN_0044f110(i)`, verified)

```
if e.regrow != 0:
    e.regrow -= 1
    e.image = 4
    e.timer = rand() % 1000 + 2000
    e.type  = 7                                 // back to a small unripe bloom, same cell
else:
    e.active = 0; cell.flags &= ~0x1000         // gone for good
```

So a bloom bursts `regrow + 1` times in total (6, 11, 26, 51, 256 for
specials 0x29–0x2D). After a re-arm it is type 7 again, and since the
burst normally throws blobs onto its own cell (s = 0), it usually waits
for that spice to be harvested again (§3).

## 5. A blob lands (`FUN_0049be60`, impact step, verified)

At impact, when the bullet is still on the map and its type is
SPICEEXPL, before the damage call. (tx, ty) = target pixel / 32:

```
c = cell(tx, ty)
if !(c.flags & 0x10000): fatal error "Spice falling onto non sandy surface"
if c has spice and all 9 cells of the 3×3 around it (itself included,
   FUN_0044e4d0: in the map and level != 0) have spice:
    c.tile = 0x31A; c.marks(+9) = 0
    c.flags = (c.flags & ~0x300000) | 0x400000          // level 4 (thick)
else:
    c.tile = 0x31A; c.marks(+9) = 0
    c.flags = (c.flags & ~0x500000) | 0x200000          // level 2 (thin)
    radar pixel = spice colour: 16-bit (0x4eb018 == 16) FUN_004620b0(tx, ty, 0x517898),
                  else FUN_00462010(tx, ty, 0x517780)
FUN_0044ec50(rect x tx-1..tx+2, y ty-1..ty+2)           // half-open: the 3×3 retile (doc 06)
```

Then, as for every bullet (doc 18 §4.2 step 11):
`FUN_0049d2e0(target px, height, SPICEEXPL, owner, firer 0xFFFF, deviate 0)`,
the impact explosion's sound (−1, none) and EX_TANKGUN at the target.

- The new level is **2 or 4 only**, whatever was there: a blob on a
  level-5 cell sets it to 4, on a level-1 cell to 2. The "thick" test
  wants the 8 neighbours with spice (out of map = no).
- Only the thin branch writes the radar pixel (the thick branch needs the
  cell to have spice already, so the pixel is already spice colour).
- The crater counter (+9) is reset (doc 26 §3).
- The error is `FUN_0042b590`, which shows a message box and **exits the
  game**. It cannot normally trigger: the target cell was sand or spice
  when the blob was thrown, and sand cells cannot get buildings or
  concrete. Our port: log it and continue with the code after the call
  (crash fix, allowed by default). No path removes 0x10000 from a cell
  during play: a scan of all 104 writes to the cell flag word
  (`0x517df4`, 2026-10-16) finds fresh values only at set-up (the map
  read in `FUN_0044c6d0` at `0x44c861`, the TILEATR flags in
  `FUN_00469790` at `0x469815`, and its map specials 0x216000 /
  0x416000 / 0x217000, which all carry sand); every other write is an OR,
  or a read-modify-write whose masks keep bit 0x10000 (spice
  `& 0xff8fffff`, placement `& 0xffff7ff8`, the crater `& ~0x800`, the
  worm `& 0xfeffffff`, ...). So the error cannot fire in 1.06.
- **Blobs do damage.** 75 × W_FIRE in a 64 px radius around the landing
  pixel, to every ground unit and building of every side (the attacker's
  side is the blob's owner; SpiceExpl never turns neutrals hostile and
  never records an attacker, doc 18 §2.2). Infantry near a burst die.
- **Chain bursts.** The damage call starts with the bloom check (§6.1),
  so a blob landing on another ripe bloom bursts it (and then does no
  damage itself).

## 6. What bursts a bloom

### 6.1 Being hit (`FUN_0049d2e0` head, verified)

```
air = height > 0x60000
if !air:
    t = FUN_0044e970(x px >> 5, y px >> 5, &i)  // first active object on the impact cell
    if t == 8:  FUN_0044ef10(cx, cy, 4); FUN_0044f110(i); return
    if t == 9:  FUN_0044ef10(cx, cy, 5); FUN_0044f110(i); return
    if t == 10: FUN_0044ef10(cx, cy, 6); FUN_0044f110(i); return
// type 7 blooms and crates: normal damage, nothing happens to them
```

- Any caller: bullets (on the scattered target pixel), sonic bullets
  along their path, explosions of dying units/buildings, worms, SpiceExpl
  blobs. The player does it with Ctrl attack-ground on the bloom cell
  (doc 10).
- After a burst the function **returns**: no unit / building damage and
  no crater for that hit (doc 26 §5).
- rand() order: the burst's calls (§4.1), then the re-arm's one.

### 6.2 Driving over it (`FUN_0049ebf0`, called from MoveStep, verified)

MoveStep (`FUN_00494260`, doc 09 §5) calls it every time a unit with no
step in progress has run GetNextSquare (`FUN_004936a0`), with the unit's
"to" cell (+0x4C / +0x4D). That is the **next cell, at the moment the
step is chosen**, before the passability test; when the unit is waiting
(to == current) it is its own cell.

```
if unit kind (record +0x2D) == 5 (worm): return
if !(cell(to).flags & 0x1000): return
t = FUN_0044e9d0(to.x, to.y)                    // see below
switch t:
    0..5: crate effects (doc 16 §6.2 and §8 below)
    7:    nothing
    8:  FUN_0044ef10(to.x, to.y, 4)
    9:  FUN_0044ef10(to.x, to.y, 5)
    10: FUN_0044ef10(to.x, to.y, 6)
```

`FUN_0044e9d0(x, y)`: first active entry at (x, y), else return 0x0B.
Type 7: return 7, untouched. Types 8–10: `FUN_0044f110(i)` (re-arm or
remove), return the type. Other types (crates): active = 0, flags
&= ~0x1000, return the type.

- So **all ground units** (infantry, vehicles, harvesters; any owner)
  burst a ripe bloom by moving onto it. Aircraft do not use MoveStep.
  Worms are excluded.
- Order differs from §6.1: here the re-arm (and its rand()) comes
  **before** the burst's rand() calls.
- The unit is not damaged directly, but blobs land around it (s = 0 is
  its cell) and W_FIRE hits it like anything else.
- Because the call happens when the step is chosen, a bloom can burst
  even if the unit then finds the cell blocked and does not enter.

### 6.3 The AI crate scan eats ripe blooms (`FUN_0041f670`, verified)

In DoStopStartStuff (doc 23 §5.3), for an AI team unit with no target,
once per 32-tick slot `(tick >> 5 & 7) == (unit index & 7)`:

```
for y in max(uy − 5, 0) .. min(uy + 6, map_h − 1) − 1:       // (ux, uy) = unit +0x4A/+0x4B
    for x in max(ux − 5, 0) .. min(ux + 6, map_w − 1) − 1:
        if cell(x, y).flags & 0x1000:
            t = FUN_0044e9d0(x, y)
            if t < 7 or t > 11:                 // a crate (0x0B "none" is excluded)
                (release the unit from the team) ; FUN_00459450(side, x, y)   // move order
```

`FUN_0044e9d0` is the pick-up function, so the scan itself **removes**
every crate it sees (the unit is then sent to an empty cell) and
**re-arms or removes every ripe bloom** (types 8–10) in the 11×11 box
**without a burst**: no spice, no sound, but the regrow count goes down
and the timer is redrawn (one rand() each). Doc 23 §5.3 calls this an
enemy-building scan; it is this cell-object scan (flags byte +1 bit 0x10
= 0x1000). Keep the quirk.

## 7. Drawing (`FUN_00428e30`, verified)

First pass of the object draw (doc 14 §5.1), before buildings and units:

```
for i in 0..29 (table order):
    if e.active != 1: continue
    img = 0x7951a8[e.image]                     // DATA.R16 102 + image
    blit FUN_00430770(surface, clip, e.x·32 − scroll_x, e.y·32 + view_top − scroll_y, img, 1, 1)
```

- Image per state: crate = entry 102 (frame 0 of `Crate.bmp`); bloom
  type 7 → 106, 8 → 107, 9 → 108, 10 → 109. Entries 103–105 are not used
  by these paths (crate types share one picture) nor by any other: the
  image byte is written only by `FUN_0044e8e0` (crates 0, blooms 4), the
  growth (+1 up to 7) and the re-arm (4), and the only other readers of
  `0x7951a8` loop over all 8 entries without drawing (`FUN_0045ffd0`,
  called by the art loader `FUN_00466140`; `FUN_0046a450` at the game's
  end, `0x44a4d6`). (Verified 2026-10-16.)
- No shroud or visibility test here; the shroud is drawn over the whole
  world later (doc 16), so blooms under shroud are hidden like the
  terrain.
- The terrain under it is the normal spice / sand tile (doc 06).
- **Radar:** nothing special. `FUN_00462140` colours a cell by building,
  spice level, sand, vehicles-walkable; the bloom cell shows as spice or
  sand like its neighbours.

## 8. Crates (brief, verified)

- Max crates `0x548048`: 0 in the campaign (`0x448756`). In skirmish /
  network games (`0x44a814..`): 0 if `0x6b9834` is 0, else `0x6b8859`,
  times the player count `0x7984c0` when `0x6b885a` is set and the count
  is ≥ 2. `0x6b9834` is gNetCrates (the skirmish / multiplayer crates
  checkbox, 0 / 1), `0x6b8859` is VARS.BIN NumberOfCrates (index 20, 1 in
  1.06) and `0x6b885a` VARS.BIN CratesPerPlayer (index 21, 0), so the
  multiplier never applies (docs 52 §2.1 step 6, 54 §0).
- New crate (`FUN_0044e7c0` → `FUN_0044e800`):
  ```
  do x = rand() % map_w; y = rand() % map_h      // x first
  while (cell(x, y).flags & 0x73F8) != 0x6000      // infantry + vehicle walkable, no object, no unit/building
  r = rand() % 100
  type = first i with r < {50, 65, 70, 75, 95, 100}[i]    // table 0x4e0e18
  FUN_0044e8e0(x, y, timer 5000, type, image 0, regrow 0)
  ```
  The rand() calls are made even if the table is full (then nothing is
  added). Spice cells are allowed. A crate expires after 5000 ticks (§3).
- Pick-up effects: `FUN_0049ebf0` cases 0–5 (cash, explosion, reveal
  map, no map, units, stealth); doc 16 §6.2 has 2 and 3. Not detailed
  here.

## 9. Other interactions

- **Movement:** cell objects do not block movement or appear in the
  passability masks (only 0x13F8 placement masks and 0x73F8 crate spawn
  test 0x1000).
- **Placement:** players and the AI (`FUN_00421000`) cannot place
  buildings or concrete on a 0x1000 cell (mask 0x13F8, doc 12).
- **Targeting / cursor:** a bloom is not an object for selection or the
  cursor; only Ctrl attack-ground (or stray fire) reaches it.
- **Harvesters** harvest the bloom cell's spice normally (no 0x1000
  check), which is what lets the bloom grow. A harvester stepping onto a
  ripe bloom bursts it (§6.2).
- **Worms** never trigger it by moving (§6.2), but a worm's damage call
  on the cell would (§6.1). A worm never makes one: its eat code
  (doc 17 §3.2) calls no damage function, the worm never fires (doc 17
  §5.4), and none of the nine `FUN_0049d2e0` call sites is in the worm
  states (bullets `FUN_0049be60` / `FUN_0049cde0` / `FUN_0049b7f0`, the
  Grenadier blast `0x499e29`, the Devastator `0x49b452`, crates
  `0x49f077`). So a worm never bursts a bloom. (Verified 2026-10-16.)
- **Save / load:** the whole table is saved (doc 27).

## 10. Corrections to earlier docs

- doc 13 §9: every active entry counts its timer down (not only 7–9);
  the growth test also requires **no spice** on the cell; types 7–9 reload
  the timer whether they grew or not; type 10 blocked reloads the timer;
  +0x04 is the image index, +0x05 the regrow count; "fewer objects below
  type 6" counts crates (types 0–5).
- doc 26 §5: the blob landing sets level **2 or 4** (thick when the 3×3
  is all spice), not "1 or 2".
- doc 23 §5.3: the ±5-cell scan looks for cell objects (crates), not enemy
  buildings, and it consumes them (§6.3).
- `src/sim/game_map.cpp`: 0x1000 = cell object (verified); the bloom
  object and its rand() are still missing there.

## 11. Implementation checklist

1. Cell-object table, 30 × {x, y, type, active, image, regrow, timer},
   saved with the game; flag 0x1000 kept in sync exactly as above
   (including the "full table at map load" case).
2. Map load: one rand() per bloom spot, in map order, at the right place
   in the set-up rand() sequence.
3. Tick update in the right tick slot (§3), with the `tick & 7` owner
   quirk.
4. Burst with the exact rand() order (§4.1, §6.1 vs §6.2), the SpiceExpl
   bullet from Templates, landing rules (§5), damage and chain bursts.
5. Drive-over pick-up at GetNextSquare time; AI scan side effect.
6. Draw pass and images.
