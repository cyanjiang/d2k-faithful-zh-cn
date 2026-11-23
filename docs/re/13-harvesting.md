# 13: Harvesting, refineries and the free harvester (1.06)

Covers the harvester states, how a harvester finds and eats spice, how it
returns to a refinery and unloads, the refinery's free harvester (carryall
delivery), the harvester orders, and what is drawn. Spice blooms get a short
note at the end.

Main functions:

| Address | Role |
|---|---|
| `FUN_00497b90` | per-tick unit update; `switch (state − 2)` |
| `FUN_0044d760` | find spice (whole map) |
| `FUN_0044d970` | prefer a thick spice cell in the 3×3 around a cell |
| `FUN_004a5e80` | least-used refinery (new harvesters) |
| `FUN_004a5f80` | nearest building of a kind, plus its dock cell |
| `FUN_004a4740` | building by index (0 if gone) |
| `FUN_0049e290` | free vehicle cell in the 3×3 around a cell |
| `FUN_0046c790` | pending spice += amount |
| `FUN_00456df0` | create an explosion / effect object |
| `FUN_004544e0` | loads `bin\VARS.BIN` into `0x6b8818` |
| `FUN_0046cca0` / `FUN_0046cd00` | carryall request: free slot / add |
| `FUN_0046d180` | carryall request table upkeep |
| `FUN_0046def0` | delivery table (carryall reinforcements, Starport frigate) |
| `FUN_0049e3e0` | carryall drops the delivered units |
| `FUN_0049e6b0` | point off the nearest map edge, and the facing from it |

"Verified" means read from the code or the data. "Inferred" is marked.
Ticks are game ticks. State numbers are the values in unit +0x10.

## Corrections to earlier docs

- **doc 09, §3 state names.** The harvester states are: 0xb **drive to a
  spice cell**, 0xc **harvest here**, 0xd **drive to the refinery**,
  0xe **dock**, 0xf **unload**, 0x10 **undock**, 0x12 **wait for a
  carryall**, 0x13 **carried**. 0xc is not an "initial" state. It is the
  harvest state, and new harvesters start in it.
- **doc 11, §8 `*_SILOSNEEDED`:** the group byte `0x6d60df` is 7, the
  REFINERY name index (verified with Templates.bin).
- **doc 09, §3 state 8.** For a harvester, state 8 ends in state 0xc
  (§1.8). Units in state 8 step with `FUN_00495560` (straight steps, no
  route) while they stand on a building cell (`FUN_0049d280`: cell flag
  0x10). Once they are off building cells and between steps, a harvester
  goes to 0xc and every other unit to 2. This answers doc 12 §10 "harvester
  behaviour after its spawn move".
- **doc 06, "Harvesting".** 5 → 4 and 4 → 3 also write tile 0x2ED. 7 and 6
  both go to 2. The written tile is replaced at once by the re-smoothing
  for any level ≥ 1 (§3.2), so only the level-0 sand tile is ever seen.
- **doc 12, §6 step 13.** The delivery cell (cx + 2, cy + 1) is the
  refinery's **dock cell** (§4.1). The delivery is a carryall (§5).

## 1. Harvester states (`FUN_00497b90`, verified)

Unit fields used here (doc 09 §2 names):

| Off | Use |
|---|---|
| +0x1e | dock animation frame (0 = normal image) |
| +0x25 | harvester timer (ticks) |
| +0x29 | load, 0–7 (the 7 pips, doc 10) |
| +0x48 / +0x49 | destination cell |
| +0x50 / +0x51 | **home**: the last spice cell it was sent to or ate |
| +0x58 | refinery (building index, 0xffff none) |

Rule values come from `bin\VARS.BIN` (§7): unload delay **60**, blob value
**100**, load delay **60**, drive distance **64**.

A timer "wait" in this doc means: while +0x25 ≠ 0, +0x25 −= 1 and do nothing
else this tick. So a delay of 60 gives one action every **61** ticks.

### 1.1 State 0xb: drive to a spice cell

- MoveStep (doc 09). When it returns 0 (arrived), state 0xc.
- **Carryall check** (also in 0xd): when (cur − dest)² summed over x and y
  > 64 (`0x6b8840`, i.e. more than 8 cells straight), the unit is between
  steps (+0x4f = 0) and the "to" cell is not reserved (flag 0x10000000):
  if the player has a carryall (`player+0x245c0` ≠ 0) and a free request
  slot (`FUN_0046cca0`), queue a request (`FUN_0046cd00`, from = current
  cell, to = dest, saved state = the current state) and go to state 0x12
  (§6).

### 1.2 State 0xc: harvest here

Per tick, in this order:

1. Timer wait.
2. Load > 6 → state 0xd. **Capacity is 7 loads.** No record field.
3. `FUN_0044d970` on the current cell (§2.2). If it picks a thick
   neighbour N:
   - N passable for this unit (`FUN_0045d990`) → dest = home = N,
     state 0xb;
   - else, `FUN_004a5270(N.x·32, N.y·32, .., 0)` finds a unit (the cell
     owner's ground vehicle on or stepping into N, else that owner's
     infantry) whose record kind is **not** 1 (harvester) → the same;
   - else (a harvester, or no unit found and N blocked) → `SetState(0xb)`
     with dest and home unchanged (`LAB_00499dd2`). Normally dest is the
     current cell, so the next tick 0xb arrives and returns to 0xc: the
     harvester **waits and does not eat its own cell** until N is free.
     Verified from the code (switch case 10: `FUN_0044d970` at
     `0x499372`, `FUN_0045d990` at `0x4993a3`, `FUN_004a5270` at
     `0x4993fa`, the kind test at `0x499412`); there is no
     counter or way out, so it alternates 0xc / 0xb every tick while N
     stays taken. Our earlier port tested every unit standing on N
     without the owner and "stepping into" rules of `FUN_004a5270`;
     corrected 2026-10-16.
4. Home = the current cell.
5. Current cell has no spice (`flags & 0x700000 = 0`):
   - `FUN_0044d760` finds spice (§2.1) → dest = home = that cell,
     state 0xb;
   - none, load ≠ 0 → state 0xd;
   - none, load 0 → state 2.
6. Current cell has spice → **eat one level** (§3), +0x25 = 60 (load delay
   `0x6b8820`), load += 1, and spawn the dust effect (§8).

### 1.3 State 0xd: drive to the refinery

1. R = building +0x58. If R is gone or not a refinery (kind 4):
   - R = `FUN_004a5f80(cur cell, player, 4)`: the refinery with the
     smallest Manhattan distance from the harvester's cell to the
     building's +0x18/+0x19 (the largest x / y of its mask-0x54 cells,
     corrected 2026-10-06; not the top-left). u16 strict less-than from
     0x4000, so ties keep the first in the building list. Output: its
     dock cell.
   - There is no busy test: a harvester keeps its own refinery (+0x58) even
     when another harvester is docked there, and queues by the normal move
     blocking. Only an idle or guarding unit (state 2 / 3) on the dock cell
     is told to move aside (state 9). New harvesters get the least-used
     refinery (`FUN_004a5e80`, counting harvesters only).
   - None: state 2 if between steps, else keep stepping.
   - Found: dest = dock cell, +0x58 = R; MoveStep; if it returns 0,
     +0x1e = 0, state 0xe.
2. R valid: dest = dock cell (§4.1); MoveStep. When cur = dock, or the
   unit is mid-step (flag 0x10000000) with "to" = dock: +0x1e = 0,
   state 0xe.
3. **Dock cell blocker** (only in step 2's path, after its MoveStep and
   possible state 0xe; `0x499900`..`0x499952`): `v = FUN_004a5270(dock.x
   ·32, dock.y·32, &owner, .., 0)`, the cell owner's ground vehicle on or
   stepping into the dock cell, else that owner's nearest infantry. If v
   takes orders (`FUN_0049f4a0(v, 0)`), is in state 2 or 3, and is
   allied to the harvester's side (`0x798830[side·8 + owner]` = 0), v
   gets `SetState(9)` (move aside). This runs on every tick of
   state 0xd, however far the harvester still is. An enemy or neutral
   unit, or a busy one, is left there.
4. The carryall check of §1.1.

There is **no queue**. A second harvester drives at the dock cell like any
move. While another harvester sits there (state 0xe / 0xf / 0x10, so the
blocker rule does not move it), the waiting one follows the common
MoveStep blocking rules of doc 09 §5: state 0xd has no wait timer or
re-path of its own (verified, switch case 0xb). So it waits as long as
MoveStep makes it wait.

### 1.4 State 0xe: dock

1. Between steps, else MoveStep and stop.
2. Load = 0 → state 0x10.
3. R not valid, or the unit is not on the dock cell → state 0x10.
4. Turn the body towards facing **12** (`FUN_00493eb0(u, 0xc)`; facing 0 =
   north, clockwise, so 12 = south-east). A harvester turns one step every
   3rd tick.
5. Facing reached: if +0x25 = 0, set +0x25 = 5 and +0x1e += 1; else
   +0x25 −= 1. So **one frame every 6 ticks**.
6. When +0x1e ≥ frames(EX_HARV) − 1 = **10**: state 0xf, +0x25 = 60
   (unload delay `0x6b8818`).

### 1.5 State 0xf: unload

1. Timer wait.
2. If R is valid and the unit is on the dock cell: +0x25 = 60. Then if
   load ≠ 0:
   - pending spice (`player+0x24264`) += **100** (`0x6b881c`,
     `FUN_0046c790`);
   - `player+0x2477c` += 100 (inferred: harvested total for the score);
   - load −= 1;
   - effect EX_REF (`0x6d6128` = 49) at the refinery pixel position
     (x >> 16) + 62, (y >> 16) − 79 (`0x6b8838` / `0x6b883c`). The building
     y is its bottom edge (doc 12 §6).
3. Otherwise (R gone, not on the dock cell, or load 0) → state 0x10.

So a full load is **700** credits of spice, paid as 7 × 100 every 61 ticks.
The money then drips from pending spice into spice (doc 11 §8). Spice over
the capacity is lost there.

### 1.6 State 0x10: undock

1. Timer wait. (It enters with +0x25 = 60, from 0xf.)
2. +0x25 = 5. If +0x1e ≠ 0: +0x1e −= 1 and stop. So the dock animation
   runs **backwards**, one frame every 6 ticks.
3. `FUN_0044d760` (§2.1) finds spice → dest = it (home unchanged),
   state 0xb.
4. None: `FUN_0049e290(player, &dest)` picks a free cell next to the dock
   cell (§5.3), state 0xb. On arrival, 0xc finds nothing with load 0 and
   the harvester goes idle (state 2) beside the refinery.

### 1.7 State 2 (idle) for a harvester

After the common idle code, a harvester (kind 1) checks only its cell: if
it has spice, **state 0xc**. It never scans for targets.

### 1.8 Other entries into the harvester states

- **Map units and spawns on their own cell** (`FUN_00455870`, x1 = x2):
  state 0xc.
- **Factory exit** (state 8): state 0xc once off the factory (see the
  corrections).
- **Carryall drop** (§5.3): state 7 with follow-up 0xb.

## 2. Finding spice

### 2.1 `FUN_0044d760(unit, player, &x, &y)` (verified)

1. If home (+0x50/+0x51) has spice, return home. Occupancy is not checked.
2. Reference point P: the refinery +0x58, if valid, at (x >> 16) / 32,
   (y >> 16) / 32 (the cell below its bottom-left corner, since y is the
   bottom edge). Otherwise P = home.
3. Scan **the whole map**, row by row, left to right. A candidate has
   spice and `flags & 0x3f8 = 0` (no building, no unit).
4. Keep the candidate with the smallest Manhattan distance to **home**.
   On a tie, the smaller Manhattan distance to P wins. On a full tie, the
   first one found stays.
5. Nothing found (best ≥ map_w + map_h) → return 0.
6. Else run `FUN_0044d970` on the result (§2.2), return 1.

There is no radius, no shroud test, and no check for other harvesters
heading to the same cell.

### 2.2 `FUN_0044d970(&x, &y)`: prefer thick spice (verified)

- If the cell's level is 3 or 4, return 0 (keep it).
- Otherwise scan x − 1 … x + 1, y − 1 … y + 1 (clamped to the map), row by
  row. A cell matches when its level is **≥ 3** and it differs from the
  **current output**. Each match overwrites the output.
- Return 1 if anything matched.

So the result is the **last** thick cell in scan order (bottom-right
first). The original cell is excluded only until the first match. The
function ignores occupancy and passability.

## 3. Eating spice

### 3.1 Level change (in state 0xc, verified)

| Level | New level | Tile written |
|---:|---:|---|
| 7, 6 | 2 | 0x2ED |
| 5 | 4 | 0x2ED |
| 4 | 3 | 0x2ED |
| 3 | 2 | 0x319 |
| 2 | 1 | 0x319 |
| 1 | 0 | sand: `0x4e9520[rand() & 7]` = 0x30–0x33, 0x44–0x47 |

- 1 → 0 also updates the radar pixel: `FUN_004620b0` in 16-bit mode
  (`0x4eb018` = 16), else `FUN_00462010`, at the harvester's cell
  (+0x4a, +0x4b) with the sand colour `0x5173f0` (`0x4994f0`..`0x499500`).
  The other level changes do not touch the radar (doc 11 §4, the radar
  image is event-driven).
- The `rand()` is the shared MSVC RNG (doc 06).
- One step = one load. A level-5 cell gives 5 loads, but a level-7 cell
  gives only 3 (7 → 2 → 1 → 0).

### 3.2 Re-smoothing

`FUN_0044ec50` on the rect (x − 1, y − 1) to (x + 2, y + 2), half-open:
the 3×3 around the cell. Every cell with spice gets its tile from the
smoothing rules (doc 06). Cells at level 0 keep their tile.

## 4. The refinery

### 4.1 Dock cell (verified)

Dock = top-left cell + (record +0x60, +0x61). For all three refinery
records (20–22) that is **(2, 1)**. Mask 0x54 = 0x36 occupies (1, 0),
(2, 0), (0, 1), (1, 1), so the dock cell has **no building flag**. It is
a plain cell under the refinery image. It lost the buildable flag (mask
0x50 = 0x777).

In states 0xd–0xf the top-left is computed from the building position:
x / 32 and ((y >> 16) − record +0x86) / 32 (+0x86 = height 96).

### 4.2 Which refinery

- **New harvester** (`FUN_00455870`, kind 1): `player+0x245b8` += 1 and
  +0x58 = `FUN_004a5e80(player)`. It counts, for each refinery, the
  harvesters whose +0x58 points at it (in building +0x37), then takes the
  refinery with the **fewest**. Ties keep the first in the building list.
- **Refinery lost:** the nearest one (§1.3).
- **Dock order:** the clicked one (§6).

### 4.3 The refinery itself

Nothing on the refinery changes while a harvester docks. Its record flags
(0x200700) lack 0x40, so it has no active animation (doc 12 §7.3). The
only refinery-side visual is the EX_REF effect per unload step (§1.5).

The 16 refinery pips show spice / capacity (doc 10), not the dock state.

## 5. The free harvester of a new refinery

### 5.1 Queueing (`FUN_004563b0`, verified)

A new refinery (kind 4, flag 6 = 0) takes the first delivery entry whose
active byte is not 1. Table `player+0x262ec`, 10 entries of 0x38 bytes:

| Off | Field | Refinery value |
|---|---|---|
| +0 / +1 | delivery cell | (cx + 2, cy + 1), the dock cell |
| +2 | started | 0 |
| +3 | active | 1 |
| +4 | due tick | the current tick |
| +8… | unit records, up to 40, 0xff ends, 0xfe = done | HARVESTER (`0x6d60e8`), 0xff |
| +0x35 | type: 1 carryall, 2 Starport frigate | 1 |
| +0x36 | 4 = no REINFORCE voice | 4 |

### 5.2 Launch (`FUN_0046def0`, verified)

`FUN_00448550` calls `FUN_0046d180`, `FUN_0046ddc0` and `FUN_0046def0` once
per tick, for **one player only: tick & 7**, and only when that player is
active. So each player's table is processed every 8 ticks.

For an active entry, not started, with due tick ≤ now:

1. started = 1.
2. `FUN_0049e6b0(&x, &y, 0)` moves the delivery cell **4 cells past the
   nearest map edge**: compare the distance to the nearer left/right side
   and to the nearer top/bottom side. If top/bottom is not farther, y = −4
   (top half, facing 16) or map_h + 4 (facing 0). Else x = −4 (left half,
   facing 8) or map_w + 4 (facing 24). It returns that facing (towards the
   map).
3. Type 1: create a **CARRYALL** (`0x6d60e9`) there with `FUN_00455870`.
   Undo the counts it added: `player+0x245c0` −= 1, `+0x24780` −= 1,
   `+0x24788[rec]` −= 1. So it is not a carryall the player owns for
   harvester ferrying.
4. Position = edge cell · 32 (cell corner), dest = the delivery cell, body
   and turret facing from step 2.
5. Voice `*_REINFORCE` for the local player unless +0x36 = 4. **The free
   harvester plays no voice.**
6. Carryall +0x2c = entry index, flags |= 0x8000b00. With exactly one
   unit and type 1, flags |= 0x20000000 (inferred: draw the cargo under
   it). State 7: it flies to the delivery cell.

### 5.3 Drop (verified; the timing details are in doc 14 §2)

- Flying state 7 ends in state 2. A carryall with flags 0x800 and 0x100
  (delivering, loaded) goes to **state 0x15** once its "to" cell is the
  dest, else back to 7.
- **State 0x15 (descend):** if the dest is not passable for the carryall
  and altitude ≠ 0xa0000, `FUN_004a60e0(&dest, 8)` picks another cell
  (inferred: within 8 cells) and state 0x16. Otherwise altitude −= 1 px
  per tick from 60 px to 10 px (**50 ticks**), then `FUN_0049e3e0` each
  tick.
- **`FUN_0049e3e0`** acts every 4th tick (tick % 4, type 1) or every 8th
  tick (type 2):
  - If the carryall's cell has a building or unit (`0x3f8`; type 2 only
    tests units), a counter (+0x2b) grows. Past 40 (type 1) the carryall
    climbs (state 0x16) and tries again. The counter is shared with the
    common update (+1 every 8th tick); its full timing is in doc 14 §2.3.
  - Else take the next record (skipping 0xfe). 0xff → entry inactive,
    return 0.
  - Create the unit at the carryall's cell with the carryall's facing.
  - **Harvester:** state 0xb, follow-up 0xb. Type 2: +0x58 =
    `FUN_004a5e80`. Type 1: +0x58 = the nearest refinery to the drop cell
    (`FUN_004a5f80`), and the drop cell becomes that refinery's dock cell.
  - Type 1: `FUN_0049e290` picks the **last free cell** (no unit or
    building bits, vehicle bit 0x2000) in the 3×3 around that cell. If
    there is none, a second pass over the same 3×3 writes the **last**
    cell whose owner bits (0..2) are the player's, whose `flags & 0x3f8`
    is not exactly 0x10 (not a plain building cell) and that vehicles
    can drive on, i.e. a cell where one of the player's own units
    stands, and returns 0 all the same. With no such cell the input
    cell stays. The three callers (state 0x10 `0x499dca`, this drop
    `0x49e62f`, the AI `0x41f886`) ignore the result and use the
    coordinates (verified from `FUN_0049e290`, 2026-10-16; ours had no
    second pass). The unit gets state 7 to it. Its
    follow-up is still 0xb, so it then harvests (§1.1 → §1.2).
  - Mark the record 0xfe.
- After the last unit: state 0x16, flags = (flags & ~0x300) | 0x4000.
- **State 0x16 (climb):** altitude += 1 px per tick back to 60 px, then
  state 2. An empty delivery carryall (0x200 clear) gets dest =
  `FUN_0049e6b0` of its cell, the nearest map edge, and state 7. It is
  removed once its cell is 4 cells past the map edge, with the left /
  top edge quirk of doc 14 §3.2 (verified there).

**Original bug: endless hover over an own factory exit (verified
2026-10-12, playtest O3V2).** The re-pick in state 0x15 runs only when
`FUN_0045d990(dest, carryall, side, 0)` says the cell is not passable. Its
first rule returns *passable* for any non-infantry unit on its own side's
building cell carrying the drive-through bit 0x10000000 (the light
factory's (2, 1) cell, the heavy factory's (0, 2)), and the carryall is
non-infantry. So the carryall descends, `FUN_0049e3e0` finds the
building bit (0x10 in 0x3f8), climbs after 40 tries (type 1 never reaches
the 81 cut-off), comes back to the same cell and repeats forever. Example:
O3V2 event 22 drops 3 raiders at (33, 18); a light factory built with its
exit cell there traps the carryall and the delivery entry. Ours does the
same (vanilla). Selling or losing the factory frees the cell.

So the free harvester is **carried in and set down on the dock cell**,
then it drives to a free neighbour cell and starts harvesting.

On easy (inferred: `0x4e8bf0` = 0), odd-numbered units of a delivery are
skipped when the player is not friendly to the local player.

### 5.4 Starport (type 2), briefly

The frigate (`0x6d60e6`) starts from the map edge nearest the primary
Starport's (x + 1, y − 2) cell and flies there. It plays the
`*_FRIGATEIN1…5` countdown voices. If there is no Starport, `FUN_0046eac0`
runs. Units drop every 8th tick. Dropped units other than carryalls get
state 0x1a. Harvesters also get +0x58 = the least-used refinery. Details
are a follow-up.

## 6. Carryall ferry (summary)

- Request table `player+0x2660c`, 0x14 bytes per slot. The slot count
  `+0x2688c` (max 32) grows by one per carryall created. Slot fields: +0
  from cell, +2 to cell, +5 state (0 free, 1 waiting, 2 taken, 3 done,
  4 picked up), +6 unit index, +8 carryall index, +0xc age, +0x10 saved
  state.
- **State 0x12** (harvester waits): if no live request has this unit at
  its current cell, state 2.
- An idle carryall (state 2, not delivering) takes a waiting request
  (`FUN_0046ce00`), flies to the unit, lands and picks it up (unit state
  0x13), flies to the "to" cell and puts it down. The unit gets its saved
  state back.
- `FUN_0046d180`: if the unit is dying, its saved state is restored
  (then it is still dying). If it left 0x12 / 0x13, the slot is done. A
  waiting or taken slot ages by 1 per call. After 80 calls (= **640
  ticks**, since the call runs every 8th tick) the unit gets its saved
  state back and the slot is dropped.

The full pick-up / put-down sequence (states 0x15 / 0x16 non-delivery
branch, `FUN_004954b0`, `FUN_004a60e0`) is in doc 33 §3 (verified there).

## 7. VARS.BIN (verified)

`FUN_004544e0` reads `bin\VARS.BIN` (0x50 bytes) straight into `0x6b8818`
because `0x4eb014` = 1. The text path with the defaults in brackets is not
used.

| Global | Name | 1.06 value |
|---|---|---:|
| `0x6b8818` | harvestUnloadDelay | 60 (30) |
| `0x6b881c` | harvestBlobValue | 100 (100) |
| `0x6b8820` | harvestLoadSpiceDelay | 60 (50) |
| `0x6b8838` / `0x6b883c` | refineryExplosionOffsetX / Y | 62 / −79 |
| `0x6b8840` | HarvesterDriveDistance (squared cells) | 64 (36) |

Other entries (Starport, repair, sandworm, crates) are listed in the
`0x4e2730` name table in the same order.

## 8. Orders and drawing

### 8.1 Orders (verified)

- **Move on spice** (order 0x13, `FUN_00459450`; ProcessOrder case 0):
  per unit. A harvester whose target cell has spice gets state 0xb,
  follow-up 1, **dest = home = the cell**. This also works in a mixed
  selection. The harvest cursor (doc 10, cursor 2) needs all selected
  units to be harvesters, but the order does not.
- **Move elsewhere:** state 7. On arrival it goes idle (state 2). It then
  harvests only if it stopped on spice (§1.7).
- **Dock** (order **0x17**, `FUN_00459540(player, building)`): sent by a
  click on an own refinery when all selected units are harvesters. Every
  selected unit is listed; ProcessOrder case 4 gives each **harvester**
  state 0xd, follow-up 1, +0x58 = that refinery. Others ignore it.
- **Retreat** (order 0x1a, `FUN_00459ab0`; hotkey action 8 or flag
  `0x7983e2`, voice `*_RETREAT`): a harvester whose player owns a
  refinery (`player+0x245c1[0x6d60df]`; `0x6d60df` = 7, the REFINERY name
  index) gets state 0xd. Other units move
  to the retreat point.

### 8.2 Drawing (verified)

- The harvester is **not hidden** while docked. It stays on the dock
  cell, drawn in the unit pass (`FUN_00428e30`).
- While +0x1e ≠ 0, its image is **explosion EX_HARV frame +0x1e**
  (`0x6e2e20[EX_HARV · 40 + frame]`) instead of the body. EX_HARV has 11
  frames; docking uses 1–10. With +0x1e = 0 the normal body is drawn.
- There are **no load frames**. The load shows only as the 7 pips on a
  selected harvester (doc 10).
- **Dust:** each eaten level spawns `0x6d6119[facing >> 2]` =
  EX_HARVDUST0…7 (explosions 28–35, 6 frames each), picked by the body
  facing, at the unit's pixel position, owned by the player.
- **Sound:** the harvester states play no sample. The credit sound comes
  from the cash drip (`S_MONEYTICK`, doc 11). The explosion data gives
  EX_HARV, EX_HARVDUST0…7 and EX_REF no sound (attribute +4 = −1, doc 14
  §4.2; golden test in `tests/sim/test_worm_harvest.cpp`). EX_HARVDUST and
  EX_REF animate as every explosion, one frame per 3 ticks (doc 14 §4.4);
  EX_HARV is not spawned, its frames are the dock image (+0x1e, one per
  6 ticks, §1.4).

## 9. Spice blooms (short, follow-up)

**Superseded by doc 31** (full RE; corrections in its section 10).

`FUN_0044ea70` runs each tick over the cell objects at `0x517b98` (12-byte
entries: x, y, type, active, stage, …, timer at +8). Bloom types 7–9 count
the timer down. At 0, if the cell is clear (`flags & 0x7003f8 = 0`), the
type and stage go up by one and the EX_SPICEBLOOM effect plays at the
cell. The timer reloads to `rand() % 1000 + 2000` ticks. At type 10 with a
clear cell, `FUN_0044ef10(x, y, 6)` (inferred: spice spread, radius 6) and
`FUN_0044f110(index)` run. If fewer objects than `0x548048` are below
type 6, `FUN_0044e7c0` adds one (inferred: crates).
All of this, and blooms being shot or driven over, is verified and
corrected in doc 31.

## 10. Open points

None. Closed 2026-10-16: the dock cell wait (§1.3, MoveStep rules) and
the blocker that moves an allied idle unit off the dock cell (§1.3,
ported); the thick-neighbour wait (§1.2, corrected to the exe's
`FUN_004a5270` test); the drop counter and the empty carryall leaving
(doc 14 §2.3, §3.2); `FUN_0049e290`'s second pass (§5.3, ported); the
ferry sequence (doc 33) and Starport state 0x1a (doc 34 §5); the effect
rates and sounds (§8.2); spice blooms (doc 31).
