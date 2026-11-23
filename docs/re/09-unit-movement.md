# 09: Unit movement (1.06 exe)

How a ground unit is ordered, routed and stepped each tick. The addresses are
for the 1.06 `DUNE2000.DAT`. Decompiles come from `re/query.sh` (and
`re/query_retry.sh` when another headless run holds the project lock), plus
`re/disasm.py` for the float code that the decompiler leaves out.

Main functions:

| Address | Role |
|---|---|
| `FUN_00455870` | create unit |
| `FUN_00497b90` | per-tick unit update (state switch) |
| `FUN_00494260` | move step (one tick) |
| `FUN_004936a0` | GetNextSquare (route cache, re-path, blocking) |
| `FUN_0045eaf0` | pathfinder |
| `FUN_0045fcc0` | path smoothing |
| `FUN_0045d990` | cell passability for a unit |
| `FUN_0049f3b0` | per-cell speed (terrain multiplier) |
| `FUN_00493eb0` / `FUN_00493f40` | turn body / turret one step |
| `FUN_0049deb0` | set unit state |
| `FUN_0045e020` | 8-way facing between two cells |
| `FUN_0049e160` | atan2 → 0..0xfff |
| `FUN_00494050` | set infantry animation mode |
| `FUN_00459450` | GenerateUnitMoveOrder (UI → order queue) |
| `FUN_00457810` | ProcessOrder (order queue → units) |

## 1. Storage

- Units live inside the side (player) object: `side + 0x30 + index*0x94`, with
  index < 1000 ("fUnitArray index check"). `FUN_004a4660(side)` returns the
  side object.
- Each side has a live list at `side+8`, linked through unit `+0x7c`.
- The template record is `0x77e250 + rec*0x100`.

Record fields used by movement:

| Rec off | Meaning | Values |
|---|---|---|
| +0x03 | body turn delay (ticks between facing steps) | trike 0, tank 1, harvester 2 |
| +0x04 | max HP | |
| +0x08 | speed, 16.16 px/tick | light inf 0x10000, trike 0x30000, combat tank 0x1c000, harvester 0x10000 |
| +0x0c | weapon (−1 = none) | |
| +0x0f | turret turn delay | |
| +0x11 | infantry flag | |
| +0x26 | has turret | |
| +0x2d | kind | 1 harvester, 4, 5 sandworm, 0xc, ... |
| +0x35 | can crush infantry | |
| +0x38 | initial object flags | |
| +0xa9 | speed class | 0 Infantry, 1 Tracked, 2 Wheeled, 3 Flying |

## 2. Unit object layout (stride 0x94)

| Off | Type | Meaning | Evidence |
|---|---|---|---|
| +0x00 | i32 | speed for the current step (16.16); init = rec+8 | 455870, 494260 |
| +0x04 | i32 | hit points | |
| +0x08 | u32 | flags (see below); init = rec+0x38 | |
| +0x0c | u16 | target: the slot index in side +0x26's pool (0xffff none; docs/re/50) | 497d93 |
| +0x10 | i32 | state (see §3) | 497b90 switch |
| +0x14 | i32 | follow-up state after a move (1 = none); init 1 | 497b90 case 7 |
| +0x18 | u8 | template record index | |
| +0x19 | u8 | selected | 459450 |
| +0x1a | u8 | body turn counter (reload rec+3) | 493eb0 |
| +0x1b | u8 | turret turn counter (reload rec+0xf) | 493f40 |
| +0x1e | u8 | animation frame | |
| +0x20 | i32 | infantry animation mode (§6) | 494050 |
| +0x24 | u8 | infantry prone: modes 0..2 become +4 | 494050 |
| +0x26 | u8 | the target's side (0xff at create): the head check reads the target slot from `FUN_004a4660(+0x26)` (`0x497db2`) | 497d93 |
| +0x28 | u8 | AI team (0xff none) | 41c3e0 |
| +0x2a | u8 | time since the last hit: 0 when hit (flag 1), +1 every 8-tick slot up to 255; drives prone and idle moves (corrected, docs/re/45) | |
| +0x2f | u8 | the death countdown of state 0x11: 0x32 for infantry (rec +0x11), 10 otherwise, set only at create (`0x4559b4` / `0x4559ba`); decremented only in the 0x11 case (`0x49a09c`), the unit is freed when it is 0 (docs/re/50 section 5) | 455870 |
| +0x32 | u16 | first attacker since the last clear: slot index in side +0x30's pool; dropped by the head when that object is dying (`0x497ce5`) | 497ce5 |
| +0x34 / +0x38 | i32 | x / y, 16.16 absolute pixels (cell·32+16 = centre) | |
| +0x3c | i32 | altitude (aircraft 0x3c0000) | 49e770 |
| +0x40 / +0x44 | i32 | step target pixel (16.16) | 494260 |
| +0x48 / +0x49 | u8 | destination cell x / y | 457810 |
| +0x4a / +0x4b | u8 | current cell ("from") | |
| +0x4c / +0x4d | u8 | next cell ("to"); == current when not stepping | |
| +0x4e | u8 | half-step mark (cell hand-over) | 494260 |
| +0x4f | u8 | ticks left in current step | 494260 |
| +0x50 / +0x51 | u8 | home / guard / harvest cell; saved dest in attack-through | |
| +0x52 / +0x53 | u8 | infantry sub-slot in next / current cell (0..4) | |
| +0x54 | u8 | body facing 0..31 (0 = north, clockwise: Facing8 gives 0 N, 4 NE, 8 E, ... 28 NW, `tests/sim/test_pool.cpp`). CreateUnit: 0 when the spawn cell is the move cell (map units, `0x455a55`), else `FUN_0045e020(spawn → move)` (`0x455a86`); aircraft 0 (`0x455b27`); deliveries the edge facing (`0x455bf1`). Turret +0x55 and goal +0x56 start at 0 | 455870 |
| +0x55 | u8 | turret facing | |
| +0x5c..+0x67 | u8[12] | cached route x, used from the end | 4936a0 |
| +0x68..+0x73 | u8[12] | cached route y | |
| +0x74 | u8 | cached route count (≤ 12); next = [count−1] | |
| +0x75 | u8 | blocked level / route mode 0..3 | |
| +0x76 | u16 | full path length from the last search | |
| +0x78 | u16 | re-path delay (ticks) | |
| +0x7a | u8 | "waiting for blocker" flag | |
| +0x7c | ptr | next unit in the side list | |
| +0x8c | u16 | own slot index in the side's pool (999 down for a fresh side, LIFO reuse; docs/re/50); staggers the 8-tick slots and waits | 46ba6c |
| +0x90 | i32 | 1 = slot in use | 457810 (`+0xc0` from side+0x30) |

Flags at +0x08 used here:

| Bit | Meaning |
|---|---|
| 0x20 | turning in place; may not step until it faces the step direction |
| 0x40 | aircraft (move step skipped) |
| 0x800 | (with 0x40) excluded from AI grouping |
| 0x8000 | turret locked to body |
| 0x400000 | being removed |
| 0x1000000 | inside a building or transport |
| 0x2000000 | spawned by conversion (46d2e0) |
| 0x10000000 | "to" cell is reserved (mid-step) |
| 0x40000000 / 0x80000000 | set by the state setter (fresh order); `0x40000000` forces a route re-check at the next step |

## 3. States (+0x10), as used for movement

| State | Meaning |
|---|---|
| 2 | idle / guard: target scan every 8 ticks |
| 3 | area guard / hunt |
| 4 / 5 | attack unit / attack building |
| 7 | **move** to +0x48/+0x49 |
| 8 | move, then attack what is in the destination |
| 9 | scatter to a free adjacent cell (`FUN_0045dee0`) |
| 0xa | attack the enemy blocking the route (from GetNextSquare) |
| 0xb | harvest at +0x50/+0x51 |
| 0xc | harvester initial |
| 0xd | return to refinery |
| 0x11 | dying |

State 7 in the update: unless aircraft, `r = MoveStep()`. When `r == 0`
(arrived), set the state to +0x14, or to 2 if +0x14 is 1, 7 or 0x12.

`SetState(obj, s)` (`FUN_0049deb0`):
- Refuses an attack state when rec+0x0c = −1, and some states for some
  kinds. Returns 0 when refused.
- Clears +0x74, +0x76, +0x78 and +0x7a, and +0x75 too unless s = 0xa.
- flags = (flags & 0x3fffffff) | 0x80000000.
- Returns 1 at once if already in state s.
- Sticky states: 0x11, 0x17, 0x1e and 0x1f only leave to 0x11.
- s = 9 saves the current state in +0x14 (0x12 becomes 2).
- s = 2 with +0x14 ≠ 1 restores +0x14 instead, then sets +0x14 = 1.

## 4. Order entry

1. UI (`FUN_004433a0`, 3 call sites) and the AI call
   `GenerateUnitMoveOrder(side, x, y)` (`FUN_00459450`). It builds an order
   record and posts it with `FUN_0045bf00`, which numbers it and queues it
   (`FUN_0045b9b0`; network send when `DAT_007984c4`). The record holds:
   - tick (`DAT_005173f4`);
   - type 0x13 (move) at +0x23;
   - side at +0x24;
   - x, y as u16 at +0x1c / +0x1e;
   - count at +0x18;
   - up to 64 unit ids at +0x26. These are every unit in the side list with
     +0x19 (selected) set and state ≠ 0x11.
2. `ProcessOrder` (`FUN_00457810`), case 0x13. For each id whose slot is in
   use and passes `FUN_0049f4a0(u, 1)` (ebx = 1 at `0x457897`). That test
   fails for state 8 or 0x14, flags 0x400140, and state 0x12 unless the
   argument is 1; there is no 0x11 test (SetState refuses it). Cases 0..7,
   9 and 0x1c pass 1, case 0xe passes 0. The fields are written only when
   SetState succeeds:
   - Harvester (kind 1) and the target cell has spice (`& 0x700000`): if
     `SetState(u, 0xb)` succeeds, set +0x14 = 1, dest = harvest cell
     (+0x50/51) = (x, y).
   - Otherwise: if `SetState(u, 7)` succeeds, set +0x14 = 1 and dest = (x, y).

   No path is computed here. GetNextSquare does it lazily.

The delay between posting and processing: 2 ticks in single player, at the
start of the model update (docs/re/49). Other order
types are 0x14 attack unit, 0x15 attack building, 0x16 state 6, 0x17 return
(0xd), 0x18 guard (3), 0x1a move+... (case 7: 0xd/7).

## 5. Move step: `MoveStep(u, side, index)`, `FUN_00494260`, once per tick

```
if steps(+4f)==0 and cur==dest: return 0                      // arrived
if steps==0:
    if +78 != 0 and (index & 7 == tick & 7 or target != 0xFFFF)  // 0x4943f3
       and not stealthed (FUN_0049f5f0) and state != 0xA and kind != 5:
        AutoFire(u)  (FUN_00496480, doc 18 §3.5; every state that steps)
    if to==cur:
        GetNextSquare(u); CratePickup(u)  (FUN_0049ebf0)
        if to==cur: goto TURN_ONLY  (returns 1)                // waiting
    dir  = Facing8(cur, to)                                    // FUN_0045e020
    diff = (facing - dir + 32) & 31
    if !(flags & 0x20) and (diff < 7 or diff > 24):           // within ±6 steps
        if !Passable(to, u, side, mode 0) or flags & 0x40000000:
            +75 = 1 (or re-check route); steps = 0; +74 = 0; to = cur
            return 1
        flags |= 0x10000000
        target = vehicle ? cellcentre(to)
                         : (to*32 + subpos[slot]) where slot = first free in
                           `to`, searching down from +53 (or slot 4)
        speed = CellSpeed(u, to)                                // §5.1
        vehicle: if hp*3 < maxhp:  speed = ftol(speed * 0.85f)
                 elif hp*2 < maxhp: speed = ftol(speed * 0.66f)
        infantry: if mode(+20)==5: speed >>= 1                  // crawling
        +00 = speed
        steps = max(|x-tx|, |y-ty|) / speed   (unsigned; >255 is an error)
        +4e = steps >> 1
        reserve `to`: vehicle  flags = (f&~7)|side|8
                      worm     f |= 0x1000000
                      infantry f = (f & ~7) | 0x20<<slot | side, bits25..27 = side; +52 = slot (owner bits replaced, 0x4947f9)
TURN:
    dir = (to==cur) ? facing : Facing8(cur, to)
    canMove = !(flags&0x20) and (diff<7 or diff>24)
    if !canMove: flags |= 0x20 (top bits 0x80000000 -> 0x40000000)
    TurnBody(u, dir)                                            // §5.2
    if rec+0x26: TurnTurret(u, Facing(cur -> dest))    (0x8000: turret = body)
    if canMove:
        clear top flag bits; infantry: SetInfAnim(u, 1)   // walk
        s = +00
        per axis:  if (x>>16) < ((tx - s/2)>>16): x += s
                   elif (x>>16) > ((tx + s/2)>>16): x -= s
        steps -= 1
        if steps < +4e:                     // passed half way: hand over cell
            +4e = 0
            vehicle:  cell(cur) &= ~8; if it still holds infantry, owner bits = bits25..27
            infantry: cell(cur) &= ~(0x20<<+53); +53 = +52
            cur = to; flags &= ~0x10000000
return 1
```

Notes:
- The step is a fixed number of ticks (`steps`), and each axis moves a full
  `s` while it is outside a half-speed window around the target. Diagonal
  steps therefore move `s` on both axes, so they are faster than straight
  ones, as in the original. There is no snap to the target: the unit stops
  within `s/2` of it, and the next step starts from that position. Every
  step aims at the absolute pixel of the next cell (centre or sub-slot), so
  the offset never exceeds `s/2` and does not accumulate.
- `diff` is computed against the 8-way direction while the unit faces any of
  32 directions. A step may start while the unit is still up to 6 facing steps
  off, and it keeps turning while moving.
- The flag 0x20 latch: set on every tick the unit cannot move while it has
  a step (`0x4948ac..0x4948c8`); there a fresh order (flag 0x80000000)
  becomes 0x40000000. It is cleared in two places only, both when the unit
  faces the step **exactly** (diff == 0): the step start (`0x49450f`) and
  the turn section (`0x494894`), before the can-move test. So a latched unit
  turns all the way, then steps in the same tick.
- Flag 0x40000000 at the step start (`0x494560`): the cached route is
  checked cell by cell in mode 0, +0x75 = 1 if any cell is blocked, else 0
  (`0x494c76` / `0x494c80`); steps = 0, route count = 0 (`0x494c95`), to =
  cur. The plain blocked branch sets +0x75 = 1 (`0x494c8e`).

### 5.1 Cell speed, `FUN_0049f3b0` (x87, from disassembly)

```
base = rec+0x08 (int)
f = cell flags of the cell being entered
if f & 0x800:  v = ftol((double)base * 1.1)                    // double @0x4d7060, concrete/road
else:          v = ftol((double)base * SPEED[(f>>29)*4 + rec+0xa9])   // float32 table @0x795128
assert v != 0
```

- `SPEED` is `bin\SPEED.BIN`: the first 32 float32 values, indexed
  [terrain class 0..7][speed class 0..3]. The class names follow. The rest of
  the "96 floats" is the name block and garbage, which is never indexed.

  | Terrain | Infantry | Tracked | Wheeled | Flying |
  |---:|---:|---:|---:|---:|
  | 0 | 1 | 1 | 0.9 | 1 |
  | 1 | 1 | 1 | 1 | 1 |
  | 2 | 0.8 | 0.7 | 0.5 | 1 |
  | 3 | 0.7 | 0.5 | 0.5 | 1 |
  | 4 | 0.5 | 0.5 | 0.5 | 1 |
  | 5 | 0.7 | 0.5 | 0.5 | 1 |
  | 6 | 1 | 1 | 1 | 1 |
  | 7 | 1 | 1 | 1 | 1 |

- Damage factors: float32 0.85 @`0x4d7020` and 0.66 @`0x4d7024`, applied
  to the int result as `ftol(float(int) * f)`.
- Exact reproduction: compute in double, `(double)int * (double)float32`, and
  truncate toward zero. The product of an int below 2^24 and a 24-bit mantissa
  is exact in a double, so this matches x87 bit for bit.
- 1.1 is a double constant, so use `1.1` (not `1.1f`).

### 5.2 Turning, `FUN_00493eb0(u, dir)` (turret: `FUN_00493f40`, +0x55/+0x1b/rec+0xf)

```
if facing == dir: return 0
infantry: anim mode = 0
if +1a > 0: +1a -= 1; return 1
d = dir - facing
if |d| < 16: facing += (d > 0) ? 1 : -1  else  facing += (d > 0) ? -1 : 1   (mod 32)
+1a = rec+3
return 1
```

One facing step every `rec+3 + 1` ticks: the trike turns every tick, the tank
every 2nd tick, the harvester every 3rd. The tie |d| = 16 takes the
second branch: the facing steps +1 when the goal is below the facing, else
-1 (`FUN_00493eb0`; the turret `FUN_00493f40` the same).

### 5.3 Facing helpers

- `FUN_0049e160(dx = from.x − to.x, dy = to.y − from.y)` returns an angle
  0..0xfff. It is an octant-reduced polynomial atan; the float coefficients
  are at `0x4e95b0..0x4e95c0`.
- `Facing8(a, b)` = `((angle + 0x40) & 0xfff) >> 7 & ~3`. For cell-to-cell
  steps the result is always one of 0, 4, …, 28. Use a lookup on
  (sign dx, sign dy). `FUN_0045e020(x1, y1, x2, y2)` calls
  `FUN_0049e160(x1 - x2, y2 - y1)`: 0 north, 8 east, clockwise (test in
  `tests/sim/test_pool.cpp`).

## 6. Occupancy (cell flags `0x517df4`, 12 bytes per cell; row table `0x547df0`)

| Bits | Meaning |
|---|---|
| 0..2 | owner of the vehicle (or infantry) in the cell |
| 0x8 | vehicle present or reserved |
| 0x20 << slot | infantry sub-slot 0..4 (mask 0x3e0) |
| 25..27 | infantry owner |
| 0x1000000 | sandworm |

- A vehicle reserves `to` when it starts the step, and frees `cur` half way.
  For a few ticks it holds both cells.
- Infantry sub-slot pixel offsets inside the cell, at `0x4d7028` (also
  `0x4c93e8`, with −16 for creation):

  | Slot | (x, y) |
  |---:|---|
  | 0 | (16, 21) |
  | 1 | (23, 14) |
  | 2 | (23, 26) |
  | 3 | (7, 26) |
  | 4 | (7, 14) |

  The search starts at slot 4 and goes down to 0 (creation and step).

Passable(cell, u, side, mode) (`FUN_0045d990`), mode 0:
- Out of bounds: false.
- Sandworm: needs sand (0x10000) and no 0x1000000.
- Vehicle onto its own side's building cell while flag 0x10000000 is set:
  true.
- Vehicle: needs 0x2000; no 0x8 and no 0x10. Cells with infantry only if
  rec+0x35 (crusher) and the infantry is hostile (alliance table
  `0x798830[side*8 + owner]`).
- Infantry: needs 0x4000; no 0x8 and no 0x10. Infantry already there must be
  the same owner with a free slot (`FUN_0044da90`).
- Attack states 4/5/0x18 let the target cell count as passable.
- Modes 1..3 progressively ignore units (used for "route around later" and
  attack-through); the rules are in §8, every branch checked against the
  exe.

## 7. Infantry animation

`SetInfAnim(u, m)` (`FUN_00494050`) runs only for infantry, and only when
m ≠ 0, or the current mode is < 0xe and not a firing mode with a target. It
sets +0x20 = m (+4 if prone and m < 3). Frame +0x1e is clamped into
[first, last] = u32 pair at `0x4e9530 + mode*8`:

| Mode | Frames |
|---|---|
| 0 stand | 0 |
| 1 walk | 1–6 |
| 2 fire | 7–11 |
| 3 | 12 |
| 4 prone stand | 13 |
| 5 crawl | 14–16 |
| 6 prone fire | 17–21 |
| 7 | 12 |
| 8 | 7–11 |
| 9 | 22–33 |

Mode 5 halves the step speed (§5).

The walk frame advances in `FUN_004940d0`, which `FUN_00497b90` calls first
in every unit update (before the state code): +1 every 3rd tick; past frame 6
the mode drops to stand, and the move step's `SetInfAnim(u, 1)` in the same
tick clamps the frame back to 1. So the walk cycles 1..6 in every state that
moves (move, attack, guard), and stops when the unit stops (doc 18 §3.8).

## 8. GetNextSquare, `FUN_004936a0(u, side, index)`

**Implemented 2026-10-06 from the decompile** (`World::next_square`):
with a cached route and the next cell blocked only by units, the unit
searches again at once in mode 0 (units block) when +0x78 is 0, and takes
the new route if it is shorter than the remaining length + 10; else it
waits (+0x78 = (index & 0xf) + 0x50), accepts 10 more cells on the next
try, and raises +0x75 (route mode) up to 2. A failed first search waits
(index & 0x3f) + 800. At mode 2 an enemy blocker is attacked (state 0xa,
the destination kept in +0x50/+0x51) and own / allied idle blockers are
asked to move aside (`FUN_00497af0`, state 9, `FUN_0049f4a0` filter).
State 0xd resets the mode instead of giving up. A step resets +0x7a and
the mode (unless it is 1). Without a route: search in the current mode; a
blocked first cell makes the unit wait in place; no path raises the mode,
and the unit gives up above 3 (at 3 when unarmed, rec+0x0c = -1).

`FUN_0045d990` (CanUnitUseSquare) modes: 0 units block (crushers may
enter hostile infantry; infantry share own cells with a free slot); 1
own / allied units that are moving (`FUN_0049e140`: mid-step, a route, or
state 7) can be passed; 2 own / allied units are ignored; 3 units are
ignored (walls / turrets by the alliance table). "Allied" = alliance
`0x798830[side][cell owner]` = 0. Before the mode rules: state 0x18
the cells of the unit's repair pad (GetBuilding(side, +0x5a), no unit
there, own building), state 5 the attacked building's cells, state 4 the
attacked unit's cell when not next to the unit (not a carryall, kinds 8..10
or a worm, not 0x400140). Mode 3: a cell without 0x400 is passable unless
it holds a building; walls / turrets (0x400) only when hostile.

`FUN_0049deb0` (SetState) refuses a change from dying (0x11), from
0x17 / 0x1e / 0x1f, to state 9 from 0x19 / 0x1a / 0xb, and from the dock
states (0xe only to 0xf / 0x10, 0xf only to 0x10, 0x10 only to 0xb with
dock frame 0). Aircraft kinds and ground kinds each refuse the other's
states; unarmed units refuse 3, 4, 6, 10 (5 except kinds 3 / 4). The route
fields are cleared even when the change is refused. State 9 saves the old
state in +0x14; SetState(2) with +0x14 != 1 goes to +0x14 instead.

State 9 (move aside): `FUN_0045dee0` looks for a free adjacent cell (the
facing's direction, then +4 / -4 alternately); if found, it steps there
(and becomes a move, state 7, once the step starts); else it asks the
friendly neighbours (not in state 7, 0x1a, 0xb, 9) to move aside. Then it
returns to +0x14 (state 2 for 1 / 0x12).


Picks the next cell (`to`). It is called only when the unit is between steps.

```
if +78: +78 -= 1
if cur == dest: clear route; return
if +75 >= 2 and state == 7 and Chebyshev(cur, dest) <= 2: dest = cur; return   // close enough
if +74 > 0:                                   // cached route
    n = route[+74 - 1]                        // must be adjacent to cur
    if Passable(n, mode 0):
        to = n; +74--; +76--; +78 = 0; +7a = 0
    elif Passable(n, mode +75):               // blocked by a unit
        if +78 == 0 and state != 0xa: try re-path (below); accept if newlen < +76 + 10
        if failed and +7a == 0: +78 = (index & 0x3f) + 800
        else escalate +75 (max 2; state 0xd resets to 0; otherwise give up: dest = cur)
        if +75 >= 2 and n is occupied:
            hostile blocker -> SetState(0xa), saving dest in +50/+51 (attack through)
            friendly        -> FUN_00497af0: ask the blocker to scatter (state 9)
        if +78 == 0: +78 = (index & 0xf) + 0x50
        +7a = 1
    else (terrain-blocked):
        if +75 == 0: re-path, else clear the route
else if +78 == 0:                             // no route: full search
    len = FindPath(cur -> dest, mode +75) then Smooth()
    if len > 0: +76 = len; cache min(len-1, 12) cells (reversed, so the next is last)
    else: +75++; +78 = (index & 7) + 8
          if +75 > 3 (or == 3 with no weapon): dest = cur; +78 = (index & 0xf) + 0x50
    if the first cell is not passable now: stay (to = cur)
```

All delays are staggered by the unit index. Only 12 cells are cached. When the
cache runs out, the next call does a fresh full search toward `dest`.

## 9. Pathfinder, `FUN_0045eaf0(sx, sy, tx, ty, u, side, &out, &?, &len, mode)`

It is not A* and has no cost model. Passability is boolean (`Passable`
with `mode`), and every move costs the same.

1. **Line probe** (`FUN_0045e960`): walk a Bresenham line from start to goal.
   - `FUN_0045fd40` sets it up: major axis, error terms `2·minor` and
     `2·minor − 2·major`, initial error `2·minor − major`.
   - `FUN_0045fe20` steps one cell: diagonal when error ≥ 0, else major-axis
     only.
   - Each obstacle run along the line records an entry point (the last free
     cell before it, list `0x6d57b8`, count `0x6d5fc8`). It also records an
     exit point (the first free cell after it, list `0x6d5bb8`, count
     `0x6d5fcc`). If the goal itself is blocked, there is no final exit
     point.
2. **Trace**: from the start, step along the line. At each obstacle:
   - Wall-follow both ways: left hand into buffer A (`0x6d1928`, count
     `0x6d5fbc`) and right hand into buffer B (`0x6d3870`, count
     `0x6d5fc4`). Each buffer holds 1000 (x, y) int pairs.
   - `FUN_0045e3b0` / `FUN_0045e1a0` do the following.
   - `FUN_0045e0e0` ("GetBestAngle") gives the 8-way direction 0..28 toward
     a point.
   - A side stops when it reaches the obstacle's exit point (or the goal).
     The shorter side is kept, and the line walk continues from there.
3. **Limits / failure**: if a side reaches 1000 entries, or the goal cannot be
   reached, `FUN_0045e890` scans both buffers. It picks the entry closest to
   the goal by Chebyshev distance (the first one wins on ties). The search then
   returns a partial path to that point, from the shorter or better buffer.
   - Result code 1 = goal reached, 3 = partial, 4 = failed (no progress).
   - The returned pointer is into buffer A or B, at a start offset
     (`0x6d5fb8` / `0x6d5fc0`).
4. **Smooth** (`FUN_0045fcc0` → `FUN_0045f970`, `FUN_0045fa10`,
   `FUN_0045fb00`): removes detours where a straight line between two path
   points is passable. Details in §9.1 (ported exactly). The exe's loop has
   no cap; ours stops at 1000 rounds, which 300,000 random paths never came
   near (16 at most), and a path that looped would hang the exe.

### 9.1 Exact port (verified 2026-10-07, `src/sim/pathfind.cpp`)

- **Directions** (`FUN_0045de30`): 0 N, 4 NE, 8 E, 12 SE, 16 S, 20 SW,
  24 W, 28 NW. `FUN_0045deb0(d, 1)` = d + 4, `(d, 0)` = d - 4 (mod 32).
  **GetBestAngle** (`FUN_0045e0e0`) gives a diagonal only when |dx| = |dy|,
  otherwise the dominant axis.
- **Bresenham** (`FUN_0045fd40` / `FUN_0045fe20`): x-major when |dy| <=
  |dx|; sign(0) = +1.
- **Exits** (`FUN_0045e960`): walking the line, after each blocked run the
  first free cell is an exit; reaching the goal ends the probe (the goal is
  an exit only when it ends a blocked run and is free). Entry points are
  recorded but not used by the search.
- **Main loop**: both buffers A and B get every free line cell. At a
  blocked cell the target is the next exit (or the goal when they ran out),
  the start direction is GetBestAngle(last free cell -> blocked cell), and
  `FUN_0045e3b0` follows both walls in lockstep from the last free cell:
  - A turns +4 until a cell is passable (or is the search start), B turns
    -4; more than 8 turns -> both 3 (boxed in);
  - each new cell is appended (1000 -> both 4); passing the search start
    sets that side's start offset to the next index;
  - the first side whose new cell is within 1 (Chebyshev) of the target
    appends the target (if passable) and is 2, the other 4; a blocked
    target -> both 4;
  - a side that comes back within 1 of where it began more than 8 times
    -> both 4;
  - after a step A turns -12, B +12.
- **Both 4**: the later exits are searched in the new parts of A and B; for
  the first exit found there, that side's new part is dropped and
  `FUN_0045e1a0` (one wall-follow, same rules) runs again towards it. If
  none is found, the side with the entry closest to the goal (Chebyshev
  from the last free cell; equal distance: B if its index is lower) is cut
  after that entry, which becomes the target, and the exit counter steps
  back. If neither side gets closer, the search returns the partial path
  closest to the last cell reached.
- **A side reached (2)**: the shorter side (A on a tie, counted from each
  side's start offset) is copied into both buffers and the line walk
  restarts from the target. **Both 3**: result 3 with no cells. Other
  combinations are logged by the exe ("Problem with routefind") and the
  walk restarts from the target anyway.
- **Ends**: the goal reached -> 1, the shorter buffer. The goal blocked, or
  1000 cells -> the entry closest to the goal (`FUN_0045e890`: the first
  strictly closer entry; the start offset is 1 + the last index before it
  that equals the start), the path up to it.
- **GetNextSquare**: every search is followed by the smoothing. The full
  search caches min(len - 1, 12) cells after the first; a re-route of a
  cached route (result 1 only, shorter than the remaining length + 10)
  caches min(len - 1, 12) cells **from the first** (no step on that call).
- **Smoothing** (`FUN_0045fcc0`): (1) `FUN_0045f970` cuts the first loop
  (a cell i < 12 met again later), repeated until none; (2) repeat:
  `FUN_0045fa10` drops the first cell if the unit's cell touches the
  second, then for up to 12 positions drops cell k+1 when cells k and k+2
  touch (staying on k after a drop); `FUN_0045fb00` (mode-0 passability)
  straightens an L (the unit's cell or cell k, and cell k+2, two apart on
  one axis, with k+1 off that line) when the cell in between is free;
  until nothing shortens or changes.

## 10. Creation, as far as movement cares (`FUN_00455870`)

- Position = `(cell*32 + 16 + off) << 16`. For infantry, `off` is the slot
  offset − 16.
- Facing = `Facing8(cell, target)` if the target differs from the cell, else
  0. Map-placed units therefore face 0.
- cur = to = cell, and dest = target.
- State:
  - 8 (move+attack) when the target differs;
  - harvester: 0xc;
  - AI-owned non-special units: 3;
  - otherwise: 2.
- The cell is marked as in §6.
- State 8 (unit update case 6): a carryall goes to 2. Otherwise, when no
  unit (or only itself) stands on the exit cell, `FUN_00495560(u, side, 1,
  1)` (`0x4990af`, MoveUnitSimple: one straight cell at a time toward the
  exit cell, no route, the record speed, the unit must face the step
  exactly) moves it, and on arrival the state ends (harvester 0xc, others
  2); an allied blocker is asked to move aside. Then, every time, between
  steps and off building cells (`FUN_0049d280`), the state ends the same
  way.
- `+0x2f` is set here (0x32 infantry, 10 others) and is the state-0x11
  countdown; `+0x8c` is the pool slot the unit got (docs/re/50).
