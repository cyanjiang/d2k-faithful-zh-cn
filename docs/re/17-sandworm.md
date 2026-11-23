# 17: Sandworm (1.06)

Covers the sandworm unit: its record and rule values, its three states
(sleep, travel, decide), how it picks where to go (thumpers, vehicles on
sand, random cells), how it eats, what it spawns while it moves (ripples,
worm-sign effects, sounds, speech), how it is shown (it has no unit
image), how other units and buildings treat it, and how worms are placed.
Decompiles come from the local GhidraMCP server (`re/ghidra_http.py`);
data from `DUNE2000.DAT`, `Data/bin/Templates.bin`, `Data/bin/VARS.BIN`
and `Data/bin/TILEATR*.BIN`.

Main functions:

| Address | Role |
|---|---|
| `FUN_00497b90` | unit update; worm states 0x1b / 0x1c / 0x1d are switch cases 0x19 / 0x1a / 0x1b |
| `FUN_0044d500` | nearest active thumper on sand (Manhattan, from a cell) |
| `FUN_0044d630` | nearest vehicle on sand (Manhattan, from a cell) |
| `FUN_0045d730` | sand ripple: up to 8 pixel particles around the worm |
| `FUN_0045d460` / `FUN_0045d4f0` / `FUN_0045d550` | particle add / per-tick move / draw |
| `FUN_00494260` | MoveStep (doc 09 §5); marks the worm's cell with 0x1000000 |
| `FUN_0045d990` | Passable (doc 09 §6); worm: sand and no worm |
| `FUN_004a5270` | object in a cell (used for the eat test) |
| `FUN_00455870` | unit create; worm start state |
| `FUN_00469790` | map load: places map units, counts worms |
| `FUN_0049d2e0` | area damage; a worm cannot die |
| `FUN_004a4900` | target search; last pass looks for worms |
| `FUN_00426000` | radar units; worms in a fixed colour |
| `FUN_004544e0` | loads VARS.BIN (doc 13 §7) |

"Verified" means read from the code or the data. "Inferred" is marked.
Ticks are game ticks. State numbers are values of unit +0x10. Pixel values
are 16.16 fixed point unless written as "px". `rand()` is the shared MSVC
`rand()` (`FUN_004b1810`, doc 06); every call listed here advances the one
global state, so the call order matters for determinism.

## Corrections to earlier docs

- **doc 16 §5.4:** only **one** place plays the worm-sign speech. The
  second place with the same "on screen and cell byte 0" test is the
  **thumper** (state 0x20, switch case 0x1e): it plays the EX_THPUFF
  sound. The worm sign also has a **2000-tick repeat limit** (§4.2).
- **doc 11 §4 / doc 09 §2:** for a worm, unit flag 4 means
  **submerged** (no ripples, no radar dot, not a target). Unit flag
  0x200000 is set on every worm at creation; it keeps the worm out of the
  sorted draw list and the aircraft passes (doc 14 §5.1), so a worm is
  **never drawn as a unit image**.
- **doc 08:** SANDWORM has speed class rec+0xa9 = **3 (Flying)**, so its
  SPEED.BIN factor is always 1.0 (doc 09 §5.1).

## 1. Record and rule values (verified)

SANDWORM is unit record **25** (kind 5):

| Field | Value |
|---|---|
| hit points (rec+0x04) | 9990 |
| speed (rec+0x08) | 40 × 1024 = 0xA000 = 0.625 px per tick |
| armour | 5 |
| weapon | WORM (never fired, §6.4) |
| view (rec+0x10) | 2 |
| death explosion (rec+0x2f) | 18 EX_WORM |
| graphic element (rec+0x14) | −1 (none) |
| infantry flag (rec+0x11) | 0 (a "vehicle" for MoveStep) |
| speed class (rec+0xa9) | 3 |
| body turn delay (rec+0x03), rec+0x0f | 0, 0 |
| record flags (rec+0x38) | 0 |
| house mask | F only; tech 255 (never buildable) |

VARS.BIN entries (names from the table at `0x4e2730`, entries 16..19;
all four are **bytes**):

| Global | Name | 1.06 value | Text default |
|---|---|---:|---:|
| `0x6b8855` | SandWormAppetite | 3 | 1 |
| `0x6b8856` | SandWormInitialSleep | 50 | 10 |
| `0x6b8857` | SandWormFedSleep | 40 | 15 |
| `0x6b8858` | SandWormShotSleep | 90 | 35 |

The game reads the binary file, so the 1.06 values apply (doc 13 §7).
Each is read once (xrefs): Appetite at `0x49b067`, InitialSleep at
`0x455cdb`, FedSleep at `0x49b078`, ShotSleep at `0x49aaff` and
`0x49ad66`.

Unit fields used by the worm (doc 09 §2 names):

| Off | Use |
|---|---|
| +0x1b | sleep sub-counter (0..100); starts at rec+0x0f = 0 |
| +0x25 | sleep length (in units of 101 ticks) in state 0x1d; pause ticks in state 0x1c |
| +0x29 | eaten count (0 at creation; +1 per meal; never reset, §3.4) |
| +0x48 / +0x49 | destination cell |
| +0x75 | blocked flag from MoveStep |
| flag 4 | submerged |
| flag 0x200000 | not drawn |

## 2. States (`FUN_00497b90`, verified)

The worm uses three states only. State = switch case + 2.

| State | Case | Meaning |
|---|---|---|
| 0x1d | 0x1b | **sleep** (submerged, does not move) |
| 0x1b | 0x19 | **travel** to +0x48 / +0x49; eat on arrival |
| 0x1c | 0x1a | **decide** where to go next |

`FUN_0049deb0` (SetState) has no state filter for kind 5; it clears the
route fields as for every unit (doc 09).

### 2.1 Sleep, state 0x1d

Each tick:

```
flags |= 4                                   // submerged
if +25 != 0:
    +1b += 1
    if +1b > 100: +1b = 0; +25 -= 1
    return
hp = record max hp (9990)
dest.x = rand() % map_w                      // 0x517de8
dest.y = rand() % map_h                      // 0x517dec
SetState(0x1b)
```

So a sleep of N lasts N × 101 ticks with +0x25 > 0, and the worm wakes on
the next tick (N × 101 + 1 ticks in total, when +0x1b starts at 0; it is
always 0 after a finished sleep). Waking restores **full hit points**
and picks a **random cell on the whole map** (x first, then y). Flag 4
stays set, so the first trip after a sleep is made submerged (§4.4).

With the 1.06 values: initial sleep 50 → 5051 ticks, fed sleep 40 →
4041 ticks, shot sleep 90 → 9091 ticks. At the default 20 ticks/s
(doc 02) that is about 4.2, 3.4 and 7.6 minutes.

### 2.2 Travel, state 0x1b

Each tick, in this order:

```
if hp < maxhp >> 1:                          // 9990 >> 1 = 4995
    SetState(0x1d); +25 = SandWormShotSleep; return
if MoveStep(u) == 0:                         // arrived (doc 09 §5)
    v = ObjectInCell(cur.x*32, cur.y*32, &owner, ..., 0)   // FUN_004a5270
    if v == 0 or v is infantry (rec+0x11 != 0):
        flags &= ~4; +25 = 0; SetState(0x1c)
    else:
        Eat(v, owner)                         // §3
    return
if +75 == 0:                                  // moving normally
    if !(flags & 4) and (+4e == 0 or +4f < 2 * +4e):
        Ripple(x px, y px)                    // §4.1, FUN_0045d730
        if rand() % 80 == 0: WormSign()       // §4.2
else:                                         // MoveStep reported "blocked"
    flags &= ~4; +75 = 0; +25 = 0
    if FreeAdjacentCell(cur, &c):             // FUN_0045dee0
        dest = c                              // state stays 0x1b
    else:
        SetState(0x1d); +25 = SandWormShotSleep
```

Notes:
- The hit-point test runs before the step. MoveStep's damage slow-down
  (doc 09 §5: hp·3 < max, hp·2 < max) therefore never applies to a worm.
- If the worm is shot to below half while mid-step, it sleeps mid-step,
  with its "to" cell still marked. It finishes the step after waking.
- The destination does not follow a moving target. The worm goes to the
  cell it chose and checks that cell only when it arrives.
- `+4e == 0 or +4f < 2·+4e` (half-step mark, ticks left) is copied as is.
  It is true on nearly every tick of a step.

### 2.3 Decide, state 0x1c

```
if +25 != 0: +25 -= 1; return               // pause after a meal (50 ticks)
if NearestThumper(cur, &t):                 // FUN_0044d500, §5.5
    if NearestVehicleOnSand(t, &v):         // FUN_0044d630, distance from t
        if |t.x - v.x| < 5 and |t.y - v.y| < 5: dest = v
        else: dest = t
    else: dest = t
    SetState(0x1b); return
if NearestVehicleOnSand(cur, &v):           // distance from the worm
    if +29 < SandWormAppetite: dest = v; SetState(0x1b)
    else: SetState(0x1d); +25 = SandWormFedSleep
    return
dest.x = rand() % map_w; dest.y = rand() % map_h
SetState(0x1b)
```

`NearestVehicleOnSand(c)` (`FUN_0044d630`): if cell c itself has
occupancy bit 8, it returns c. Otherwise it scans all cells, rows top to
bottom, columns left to right, and keeps the cell with flags 0x10000
(sand) **and** 8 (vehicle present or reserved) with the smallest
Manhattan distance |dx| + |dy| to c (16-bit compare, strictly smaller, so
the first one found wins a tie). The start value is map_w + map_h; it
reports "found" when the best is below that.

There is **no range limit**: any vehicle on sand anywhere on the map
draws the worm. There is no owner test either (§5.1).

## 3. Eating (verified)

### 3.1 Conditions

The worm eats only at the end of a trip, in the cell it arrived at.
`FUN_004a5270(cell.x·32, cell.y·32, &owner, &idx, 0)`:

- reads the owner bits 0..2 of the cell's flag word;
- if the cell has bit 8 (a vehicle): returns that owner's ground unit
  whose current cell, or reserved "to" cell (unit flag 0x10000000), is
  this cell. It skips infantry, kinds 2, 5, 8, 9, 10 (carryall, worm,
  frigate, ornithopter, Death Hand), and units with flags & 0x400140
  (being removed, carried, aircraft);
- otherwise, if the cell has infantry, it returns the nearest infantry
  (pixel Manhattan distance from the cell's top-left corner, < 0x800).

An infantry result is not eaten. So the worm eats **vehicles only**:
infantry, aircraft and units carried by a carryall are never eaten.
Verified from the code (2026-10-16): the arrival test is
`v == 0 || record[v].+0x11 (0x77e261) != 0` → decide (`FUN_004a5270`
at `0x49ab44`, the infantry byte at `0x49ab5e`, switch case 0x19); there
is no other eat
path (the worm never fires, §5.4). Infantry on sand survive a worm in
1.06, whatever common memory says.

### 3.2 What happens

For a victim v with owner o:

1. `SetState(v, 0x11)`: the normal removal countdown (doc 14 §3.3). No
   death explosion, no `*_UNITLOST`, no kill credit to anyone.
   The eat code does not write v's +0x2f; it keeps the value unit create
   gave it (`0x4559ba`: 10 for a vehicle; a scan of every byte write to
   `[reg+0x2f]` finds no other writer for a unit). SetState(0x11) only
   clears +0x19 (`FUN_0049deb0`). The vehicle draw pass (`FUN_00428e30`,
   test at `0x4291e8`) has no state test, so v **stays drawn** with its
   normal body image (its death mode +0x20 is untouched) under EX_WORM
   until it is removed. Its cell bit 8 (and a "to" cell's) is cleared by
   the removal update of state 0x11 (switch case 0xf, when +0x2f is 0),
   as for every dying vehicle (doc 14 §3.3). (Verified 2026-10-16.)
2. Owner o: units lost `+0x24a08` += 1, lost per record
   `+0x24a10[rec]` += 1.
3. Worm: +0x25 = 50 (0x32, a fixed constant), flags &= ~4, state 0x1c.
4. Sound: the sample of EX_WORM (`0x781e5c + 18·8` = 49 S_WORM) at the
   worm's cell, `FUN_0046f540` (positional, doc 12 §9). No screen or
   shroud test.
5. If o is the local player (`0x798544`): `*_WORMATTACK` speech,
   `FUN_0046f900(S, O, H, mode 1, limit 0, prio 0)`.
6. Explosions, owner = the worm's owner, at the worm's x, y px, height 0,
   no extra flags: EX_WORM (rec+0x2f = 18), then EX_WORMDUST
   (rec+0x2f + 1 = 19).
7. Eaten count +0x29 += 1.

There is no other cool-down. After the 50-tick pause the worm decides
again (§2.3).

### 3.3 The eat effects

| Type | Name | Frames | Default flags | Sound |
|---:|---|---:|---|---|
| 18 | EX_WORM | 16 | 0 (normal keyed blit) | 49 S_WORM (played by the eat code) |
| 19 | EX_WORMDUST | 21 | 0x10 (50 % translucent) | none |

Both use the common explosion update (doc 14 §4.4): one frame per 3
ticks, shown frames 1 … n − 1. EX_WORM shows for 45 ticks, EX_WORMDUST
for 60. EX_WORM's first DATA.R16 entry is 4452 (4273 + frames of types
0..17). The worm's "mouth" is these two explosions only; nothing else is
drawn.

### 3.4 Appetite

+0x29 counts meals and is never reset. Writers in the exe: unit create
(0) and the eat code (+1); the other writes to +0x29 in
`FUN_00497b90` belong to other kinds (saboteur, thumper, Devastator).
A full scan of the byte writes to `[reg+0x29]` (2026-10-16, 30 sites)
confirms it: unit create `0x4559e4` (0) and the eat `0x49acee` (+1) are
the only ones a worm reaches. The others are kind 4 (`0x497ca5`,
`0x497cc7`, `0x49f20e`), the Death Hand (`0x455b2a`, `0x494d02`,
`0x49e971`), the Devastator (`0x458a0f`, `0x49b3a5`), ornithopters
(`0x495319`, `0x49e82f`), the carryall ferry (`0x498340`), the harvester
load (`0x49970b`, `0x499cc4`), the thumper (`0x49b4e1`, `0x49b4ec`),
state 0x21 (`0x498e3f`) and stack locals.

So after **3 meals** (SandWormAppetite) the worm no longer hunts
vehicles: each time it decides and a vehicle is on sand somewhere, it
sleeps for FedSleep, wakes at full hit points, travels submerged to a
random cell, and decides again. It still eats:
- a vehicle that happens to be in the random cell it arrives at;
- vehicles near an active thumper (the thumper branch has no appetite
  test).

The worm does not leave the map or disappear. "Full" means "asleep".

## 4. What is shown

### 4.1 Sand ripple (`FUN_0045d730`, verified)

Called every travel tick while surfaced (§2.2), with the worm's x, y px.
Four tries:

```
repeat 4:
    dx = 12 - rand() % 25;  dy = 12 - rand() % 25          // -12..12
    if dx*dx + dy*dy < 144:
        px = float(x + dx); py = float(y + dy)
        vx = float(rand() % 200) * 0.001; if dx < 0: vx = -vx
        vy = float(rand() % 200) * 0.001; if dy < 0: vy = -vy
        colour = terrain pixel at the worm's own x, y (tile of cell
                 (x >> 5, y >> 5), pixel (x & 31, y & 31)), darkened
        add particle(px, py, vx, vy, life = rand() % 30 + 10, colour)
        add particle(px, py, 0, 0,   life = rand() % 30 + 50, colour)
```

- rand() order per try: dx, dy, then (only inside the circle) vx, vy,
  life 1, life 2. The colour is read after vx, vy and before life 1
  (`0x45d884`..`0x45d8dd`); life 2 is rolled after the first particle is
  added (`0x45d941`).
- The colour pixel: cell = (x >> 5, y >> 5) of the worm's own position,
  pixel (x % 32, y % 32) of that cell's tile (`0x6d9b58[tile]`, row
  stride 32), read **when the particle is made**; both particles of a
  try share it.
- Darkening for 16-bit tiles (verified from the listing `0x45d650`):
  `FUN_0045d650(p, 0.85f)` (the float `0x3f59999a` pushed at `0x45d8ce`).
  If the surface format `0x798384` is `0xcaca` (5-5-5): r = p >> 10 & 31,
  g = p >> 5 & 31, b = p & 31; else (5-6-5): r = p >> 11 & 31,
  g = p >> 5 & 63, b = p & 31. Each channel is `FILD`, `FMUL` by the float,
  `_ftol` (truncation), and the result is (r << 5 | g) << 5 | b, resp.
  (r << 6 | g) << 5 | b. Bit 15 is dropped. 8-bit tiles use the palette
  shade table (`0x795550` + 0x200) instead; we render 16-bit 5-5-5.
- vx, vy: `FILD qword` of rand() % 200, `FMUL double [0x4c9450]` (the
  **double** 0.001), `FSTP float` (`0x45d82f`, `0x45d85c`), then negated
  when dx / dy < 0.
- Particles (`0x6b9920`, 4096 slots of 24 bytes: float x, y, vx, vy,
  life, colour u16, used byte): `FUN_0045d460` takes the first free slot,
  or drops the particle when 4096 are in use. `FUN_0045d4f0` runs once
  per simulation tick in the main loop (`0x44a057`, inside the tick
  branch): a particle with life 0 is freed, otherwise x += vx, y += vy,
  life −= 1. The `jne 0x44a061` at `0x4499e6` that would skip the tick
  branch tests `0x5179bc`, which is only ever written 0 (`0x448606`, its
  one writer), so it never skips. `FUN_0045d550` draws each as **one
  pixel** at (round(x), round(y)) on the view, after the building
  foundations (`FUN_00427d50`) and before `FUN_00428e30` (cell objects,
  bodies, units; doc 16 §1), and not on full-shroud cells.

So a moving worm leaves a short trail of single darker sand pixels: half
of them drift outwards for 10–39 ticks, the other half stay for 50–79
ticks. This trail is the only constant sign of a surfaced worm.

### 4.2 Worm sign (verified)

With chance 1/80 per surfaced travel tick (`rand() % 80 == 0`, after the
ripple):

1. `t = 0x6d6121[rand() % 6]`: one of EX_WORMSN1–6 (types 20–25).
   Spawn it at the worm's x, y px, height 0, no extra flags, owner = the
   worm's owner. This happens whether or not anyone can see it.
2. If the worm is on screen — x px in [scroll_x, scroll_x + view_w] and
   y px in [scroll_y, scroll_y + view_h] (`0x798538`, `0x786d48`,
   `0x79853c`, `0x6dc538`, inclusive) — and its current cell's shroud
   byte is 0 (doc 16 §1.2; the `0x4dfb04` test is always on):
   - `*_WORMSIGN` speech: `FUN_0046f900(S, O, H, mode 1, limit 2000,
     prio 0)`. The limit means the speech is skipped if it played less
     than **2000 ticks** ago (doc 12 §9);
   - if the sample of t (13 S_WORMSIGNSPARK) is not playing on any
     channel (`FUN_0046f4f0`), play it at the worm's cell
     (`FUN_0046f540`).

| Type | Name | Frames | Flags |
|---:|---|---:|---|
| 20, 21, 22 | EX_WORMSN1–3 | 5 | 0x200 additive |
| 23 | EX_WORMSN4 | 10 | 0x200 additive |
| 24, 25 | EX_WORMSN5–6 | 5 | 0x200 additive |

### 4.3 No unit image (verified)

The worm's record has no graphic element (−1), and flag 0x200000 keeps it
out of every unit draw pass (`0x4291e8` tests 0x200140; doc 14 §5.1 and
§5.5). It is also never selectable by a click: the hit test
`FUN_004a5270` skips kind 5, and a worm does not write owner bits into
its cell. The drag box (`FUN_004a5ae0`) walks only the local player's
own unit list (`FUN_004a4660(0x798544)`) and has no kind test; worms
belong to side 7 (every campaign mission, golden test; skirmish worms
are side 7 too, doc 52), which is never the local player, so a box never
selects one; "select all on screen" (E) is the same function (doc 25).
(Verified 2026-10-16.)

### 4.4 Surfaced or submerged

| When | Flag 4 | Ripple / sign | Radar dot | Can be targeted |
|---|---|---|---|---|
| state 0x1d (sleep) | set | no | no | no |
| first trip after waking | set | no | no | no |
| after the first arrival, pause, later trips | clear | yes (in state 0x1b) | yes | in state 0x1b only |

Flag 4 is cleared on every arrival and when blocked (§2.2).

### 4.5 Radar (`FUN_00426000`, verified)

The worm is drawn as one pixel at its "to" cell (+0x4c / +0x4d), like
other units (doc 11 §4), when the cell's shroud byte is 0 and (the local
player owns it, or flag 4 is clear). The colour is `0x5162b8`
= RGB(0xc6, 0xb5, 0xa5) in the 16-bit path (`0x5165bc` in the 8-bit
path), not the owner's colour.

## 5. Interactions

### 5.1 Who is in danger (verified)

- **Vehicles on sand** (cell bit 0x10000), of **every** owner, including
  the worm's own owner and allies. There is no alliance test anywhere in
  the worm code.
- **Rock is safe.** Passable for a worm (`FUN_0045d990`, `0x45da0a`)
  needs bit 0x10000 and no 0x1000000; the hunt only looks at sand
  cells. Units, buildings and concrete are not tested.
- **Spice** is sand: the spice tiles carry 0x10000 in TILEATR (checked
  tiles 0x2EE–0x31F in all sets 1–6, and the thick spice tiles 0x12C,
  0x12D, 0x140, 0x141 in TILEATR1). A mission's TILEATR file is named in
  its `.MIS` (`0x5165c8`, docs/formats/maps.md); in every campaign
  mission every cell with spice has the sand bit (golden test,
  34026 cells). Eating spice only rewrites the level bits
  (`& 0xff8fffff`, `0x499470`..`0x49969e`), and no runtime write of the
  cell flags clears 0x10000 (doc 31 §5), so an eaten spice cell stays
  sand. (Verified 2026-10-16.)
- **Harvesters** are ordinary vehicles. A harvester lifted by a carryall
  (flag 0x100) is safe.
- **Infantry** are never eaten (§3.1).
- **Aircraft** never set cell bit 8 and are skipped by the eat test.

### 5.2 Worm movement (verified)

The worm moves with the common MoveStep (doc 09 §5) and route search.
Its cell speed is rec+0x08 × SPEED[terrain][3] = 0xA000 × 1.0
(×1.1 only on a concrete/road cell, which a worm cannot enter). One cell
straight: (32 << 16) / 0xA000 = 51 ticks (about 2.6 s at 20 ticks/s).
The worm marks its "to" cell with **0x1000000** at the start of a step
and clears it on its "from" cell at the half-way point. It never touches
bit 8 or the owner bits. Two worms can not share a cell; a worm can
enter a cell with a vehicle or infantry.

The worm does not fire while moving: MoveStep skips its auto-fire call
for kind 5 (`0x494450`, `bVar15`).

### 5.3 Damage (`FUN_0049d2e0`, verified)

A worm takes damage like any unit (armour 5), including splash while
asleep (the area damage does not test flag 4). When a hit would kill it,
its hit points are set to **1** instead: a worm cannot die. For a worm the
hit code also skips the smoke timer, the deviator conversion and the
`*_UNITLOST` speech. The attacker's kill counters (`+0x24c90`,
`+0x24c98[...]`) still go up on such a "kill". Below half hit points the
worm goes to sleep for ShotSleep at its next travel tick (§2.2) and wakes
at full hit points.

### 5.4 Who shoots at worms (verified unless marked)

- The target search `FUN_004a4900` has a last pass (argument 13 ≠ 0) that
  spirals over cells with the worm bit 0x1000000 and returns a worm that
  is in state 0x1b and surfaced (flag 4 clear) and passes
  `FUN_004970f0` (inferred: within weapon range). The unit auto-fire `FUN_00496480` and the building
  auto-fire `FUN_00496790` both pass 1, so **units and turrets
  automatically shoot a travelling, surfaced worm** when no other target
  was found first.
- When the result is a worm (kind test at `0x4966a9` / `0x496a20`), both
  call `FUN_004a5270(x, y, .., 0)` with the target search's x / y outputs
  (`*param_5`, `*param_6` of `FUN_004a4900`, i.e. the worm's position
  +0x34 / +0x38 >> 16, in pixels; `0x4966b2`, `0x496a29`). If it finds a
  unit there (the cell owner's ground vehicle on or stepping into that
  cell, else that owner's infantry), they return 1 without firing. The
  unit auto-fire has already stored the worm as its target (`0x496682`);
  the building does not. (Verified 2026-10-16.)
- Units in state 3 drop a worm target (`0x498b7e`); AI team targets drop
  worms (`FUN_004237a0`, also `0x4208b2`, `0x4228d2`). A homing bullet
  loses a target with flags 0x200004 (`0x49b973`), so shots at a worm do
  not home. The worm's own weapon (WORM) is never used: the worm never
  enters an attack state.

### 5.5 Thumper (kind 0xe, record 3 THUMPER, infantry)

`FUN_0044d500` scans all cells (same order and tie rule as §2.3, from the
worm's cell) for a cell with sand (0x10000) and infantry bits (0x3e0)
whose `FUN_004a5270` result is a kind 0xe unit in **state 0x20**
(thumping, switch case 0x1e). A thumper on rock, or a thumper that is
not thumping, does not attract worms. The thumper wins over vehicles,
and over the appetite (§2.3). Thumping itself (deploy, state 0x20, the
72-tick thump with EX_THPUFF and S_THUMPER) is documented in doc 54 §4.

In skirmish the thumper is offered only when `0x6b9838` (the worm count
setting, §6.2) is not 0 (doc 11).

### 5.6 Other

- Crates: a worm never picks one up (`FUN_0049ebf0` returns 0 for kind
  5), and the random-unit crate re-rolls worms (`0x49eede`).
- Defeat checks: worms and aircraft do not count as remaining units
  (`FUN_00455290`, `FUN_00455480`). A worm keeps no player alive.

## 6. Placement and removal

### 6.1 Creation (`FUN_00455870`, verified)

As for any unit (doc 12 §8.3), with these differences for kind 5: no
"building in the spawn cell" test, no cell occupancy written, and at the
end:

```
+25 = SandWormInitialSleep; SetState(0x1d); flags |= 0x200000
```

So every worm starts **asleep and submerged** for 5051 ticks. The owner
must be an active player (`player+0x24250` ≠ 0), or creation fails.
Map-placed worms belong to player slot 7 in every campaign map (golden
test: 49 worms in 42 missions, all side 7). Slot 7 is always active: the
set-up loop `0x448826..0x448835` creates sides 0..7 in every game type
(`FUN_004a45d0` sets `player+0x24250` = 1 and counts `0x798628` up to
8; doc 52 §2.1 step 8), and a load restores that count. (Verified
2026-10-16.)

### 6.2 Map load (`FUN_00469790`, verified)

- **Campaign:** every map unit is created; worms are counted. A map with
  **no worm** is a fatal error: `FUN_0042b590("Setup.cpp", "No worms
  found on this map (%s)")` shows a message box and aborts.
- **Skirmish / multiplayer** (`0x797e34 == 1` or network `0x7984c4`):
  map worms are created in order only while their count is ≤ `0x6b9838`.
  `0x6b9838` is **gNetWorms**, the skirmish / multiplayer "Worms"
  multistate box (`wormsCheck`, 4 states: 0 off, 1 low, 2 medium,
  3 high; a value > 3 is reset to 0), byte +0x1c of the setup message in
  `FUN_004470a0`, 0 in `FUN_00470710` (docs 51 §2, 52 §1, 55). The exact
  creation walk (map worm entries repeated per side while the counter is
  ≤ gNetWorms) is in doc 52 §2.5.

### 6.3 Removal

None. A worm cannot die (§5.3), no code sets a worm to state 0x11, and
no code re-creates worms. There is no respawn timer and no VARS.BIN
worm count. No mission event adds or removes one: only DEPLOY (0),
REINFORCE (1) and ADDUNITS (18) create units (doc 19 §3), and no such
event in any 1.06 campaign `.MIS` lists a kind-5 record (golden test);
no event type removes units. The random-unit crate re-rolls worms
(§5.6). (Verified 2026-10-16.)

## 7. Determinism notes

Per worm and tick, rand() is called:
- sleep: 2 calls on waking (dest x, y);
- travel, surfaced: the ripple (2 per try, plus 4 per accepted try),
  then 1 (the 1/80 roll), then 1 more on a hit (which sign);
- decide: 2 calls when no thumper and no vehicle on sand was found.

Worms are updated in their owner's unit list order (doc 14 §4.4), so
their rand() calls fall between the other players' calls.

## 8. Open points

None. Closed 2026-10-16 from the listing and the 1.06 data: infantry
immunity (§3.1), the eaten vehicle's countdown, drawing and cell bit
(§3.2), the +0x29 writers (§3.4), the ripple colour math, the double
0.001 and the colour read time (§4.1, ours now stores the tile pixel at
creation), the `0x5179bc` skip (never set, §4.1), the drag box (§4.3),
the TILEATR / sand bits (§5.1), the position tested before firing at a
worm (§5.4, ours now uses the worm's pixel), the thumper (doc 54), slot
7 (§6.1), gNetWorms (§6.2), events (§6.3).
