# 30: MCV deploy, engineers and saboteurs (1.06)

Written for a line-by-line port. `unit+…` / `bld+…` are object fields
(objects of the player's array at `player+0x30`, stride 0x94, lists linked
by `+0x7c`, unit fields as in doc 09 §2, building fields as in doc 12 §6);
`player+…` is the struct returned by `FUN_004a4660(side)`. `cell(x, y)` is
the u32 at `0x517df4 + (0x547df0[y] + x) · 12` (doc 09 §6). `urec+…` is the
unit record (`0x77e250 + rec · 0x100`, kind at +0x2D), `brec+…` the
building record (`0x6dc540 + rec · 0x10C`).

"Verified" = read from the decompile **and** checked in the disassembly
(`re/disasm.py`) where it matters. All open points were settled against
the exe on 2026-10-16 (§3).
`rand()` is the shared MSVC LCG (`thunk_FUN_004b1810`, doc 23 §1); the
"line" numbers are the debug line arguments pushed with each call.

## 0. Corrections to earlier docs

- **Engineer = kind 3, saboteur = kind 4** (verified: Templates.bin
  ENGINEER +0x2D = 3, SABOTEUR +0x2D = 4, matching doc 08's parser names;
  and state 0x17 below calls the capture only for kind 3). Doc 10 §3 /
  §5 has the names swapped. The flag mapping in the code is:

  | Kind | Unit | Needs building flag | Cursor over such a building |
  |---|---|---|---|
  | 3 | ENGINEER | **0x400** (capturable) | 4 (green down arrow, "enter") |
  | 4 | SABOTEUR | **0x200** | 0x1F (doc 10's "engineer figure"; the code only picks the index, the image is frame 0x1F of the game's own cursor sheet, which we load, so what it shows does not matter for the port) |

  Building record flags (Templates.bin +0x58): **0x400** is set on every
  building except concrete, walls, the Sietch and both gun turrets; **0x200**
  is set on the same ones **plus** the medium and large gun turrets. So
  engineers cannot capture turrets, saboteurs can destroy them.
- Doc 10 §3 "mode 3 sell, capture path `FUN_0046d6d0` plus `*_CAPTURE`":
  that branch needs `0x4eb008 == 0`, and `0x4eb008` is initialised to 1 and
  never written (only read; one `cmp`). **Dead code in retail** (a
  debug "click an enemy building in sell mode to capture it" cheat). Do not
  port it.
- Doc 12 §6, the flags of `FUN_004563b0(player, rec, cx, cy, f5, f6, f7)`:
  the four callers are

  | Call site | Caller | f5, f6, f7 |
  |---|---|---|
  | `0x469c05` | map loader `FUN_00469790` | 1, 0, 0 |
  | `0x45882f` | order 0x26 (place building) | 0, 0, 0 |
  | `0x49b26e` | unit update `FUN_00497b90`, state 0x1E = **MCV deploy** | 0, 0, 0 |
  | `0x46d7ef` | `FUN_0046d6d0` = **building capture** | 0, 1, 1 |

  So f6 = 1 and f7 = 1 are used only by the capture: f6 = 1 skips the
  construction-yard concrete and the refinery's free harvester, f7 = 1
  skips the crane animation of the primary yard. Inside the function
  (verified): concrete is laid when `!(brec flags & 0x400000) && (f5 ||
  (kind == 2 && !f6))`; build-up animation when `!f5` and the record has
  animation-1 frames; crane flag 0x20 on the primary yard when `!f5 &&
  slot != -1 && !f7`; free harvester when `kind == 4 && !f6`.
- Doc 22 §3.8 DoMCVs: the "else move it" belongs to the failed cell test;
  when the cell is fine but neither condition holds, nothing happens (§1.6).

## 1. MCV deploy

### 1.1 Deploy test `FUN_00443250(x, y)` (verified, disassembly)

```
CanDeploy(x, y):                       // x, y = cell
  for row = y-1 .. y+1:
    for col = x-1 .. x+1:
      if col < 0 or col >= mapW: return 0          // 0x517de8
      if row < 1:               return 0          // see quirk
      if row >= mapH:           return 0          // 0x517dec
      f = cell(col, row)
      if (!(f & 0x8000) or (f & 0x3f8)) and !(col == x and row == y):
          return 0
  return 1
```

- 0x8000 = buildable terrain, 0x3f8 = building (0x10), vehicle (8) or
  infantry (0x20..0x200) bits. The **centre cell is exempt** from the
  buildable/occupied test (the MCV stands there), but not from the bounds
  tests.
- **Quirk (verified):** the row lower bound is a pointer compare
  `&rowtab[row] < 0x547df4` (= `&rowtab[1]`), so **row 0 fails**: an MCV on
  row 1 can never deploy. Columns only need ≥ 0. Concrete laying also skips
  row 0 (doc 12 §5), so it is probably deliberate.
- Same outcome order as the code: first failing cell returns 0; there is
  no side effect.

### 1.2 Human input (`FUN_004433a0`)

- **Cursor** (mouse over an own selected unit, no Shift, `0x797e88 == 0`):
  kind 6 → `CanDeploy(mouse cell)` ? 0xC : 0xD (doc 10 §5).
- **Left click** on an own unit that is already selected (`+0x19`), Shift
  not held: kind 6 →
  - `CanDeploy(mx, my)` with **(mx, my) = the cell under the mouse**
    (decompile locals b0/b4), not the MCV's own cell. Pass:
    `FUN_0045a3f0(owner, idx)`; no ack voice, no select voice.
  - Fail: `*_CANTDEPLOY` through `FUN_0046f900` (lookups `H_CANTDEPLOY`,
    `O_CANTDEPLOY`, `S_CANTDEPLOY`, house choice as in doc 12 §9).
  - Note: if the mouse cell differs from the MCV's cell (clicking the part
    of the sprite that overhangs into a neighbour), the MCV's own cell is
    a non-centre cell with bit 8 set, so the test fails. Port as is.

### 1.3 The order (`FUN_0045a3f0`, verified)

Packet (doc 09 §4 layout): type **0x2F** at +0x23, side at +0x24, tick
(`0x5173f4`) at +0x08, count 1 at +0x18, unit index at +0x26; posted with
`FUN_0045bf00`. The same order is the "special action" of kinds 7
(Devastator), 0xE (Thumper) and 4 (Saboteur).

### 1.4 ProcessOrder case 0x1C (order 0x2F, `FUN_00457810`, verified)

```
u = player(side)+0x30 + id·0x94          // id > 999: error message
if u.+0x90 != 1 or !CanTakeOrder(u, 1): return
switch urec(u).kind:
  6  (MCV):        if SetState(u, 0x1E): u.+0x14 = 1
  7  (Devastator): if SetState(u, 0x1F): u.+0x14 = 1; u.+0x29 = byte 0x6b885b
  0xE (Thumper):   if SetState(u, 0x20): u.+0x14 = 1
  4  (Saboteur):   if !(u.flags & 0x10): u.flags |= 0x10;
                     S_STEALTHUP at (u.+0x4c, u.+0x4d)   // stealth, not covered here
```

`CanTakeOrder(u, p)` = `FUN_0049f4a0`: u ≠ null, state ≠ 0x12 unless p = 1,
state ≠ 8, state ≠ 0x14, and `flags & 0x400140 == 0`. There is **no cell
test here**: the deploy test is repeated in the unit update. SetState
(doc 09 §3) makes 0x1E sticky: it only leaves to 0x11 (or by the direct
write below).

### 1.5 Unit update, state 0x1E (`FUN_00497b90` case 0x1C, `0x49b160`–`0x49b304`, verified)

```
if u.+0x4f != 0:                       // mid-step: finish the step
    u.+0x48, u.+0x49 = u.+0x50, u.+0x51
    MoveStep(u, side, idx)            // FUN_00494260, continues the step
    return
if TurnBody(u, 0x1D): return          // FUN_00493eb0, one facing step per rec+3+1 ticks
// find the construction yard record for the owner's house
h = 0x8cd4f0[side]                    // MIS house of the side
r = 0
while !((1 << h) & brec(r)+0x04) or brec(r)+0x5C != 2:
    r += 1
    if r > count (0x79553c): error "Cannot find correct construction yard"
if !CanDeploy(u.+0x4a, u.+0x4b):
    u.+0x10 = 2                       // plain write, not SetState; no sound
    return
cell(u.+0x4a, u.+0x4b) &= ~8          // drop the MCV's vehicle bit first
ratio = (float32)((x87)u.hp / (x87)urec+0x04)     // fild / fidiv / fstp dword
b = CreateBuilding(side, r, u.+0x4a − 1, u.+0x4b − 1, 0, 0, 0)   // 0x49b26e
if side == local player (0x798544):
    PlayAt(FUN_0048dd90("S_BUILDUP"), u.+0x4a, u.+0x4b)       // FUN_0046f540, positional
if b != 0xffff:
    bld(b).hp (+0x24) = ftol((x87)(u32)brec(r)+0x00 · ratio)   // fild qword (hi = 0), fmul dword
u.flags |= 0x600000
SetState(u, 0x11)
```

Notes:

- **Placement:** the yard's top-left cell is (MCV cell − 1, MCV cell − 1);
  the yard (masks 0x777 / 0x777) covers the 3 × 3 block that CanDeploy
  tested, with the MCV in the centre.
- **Record lookup** is a plain first-match on the house bit, not the
  per-house lookup `FUN_0046c450` (doc 08). The yard records have disjoint
  masks (AF, HE, OSM, E), so the result is the same for houses 0–6.
- **What CreateBuilding does with 0, 0, 0** (doc 12 §6): no pre-place, so
  **concrete is laid** under the 3 × 3 mask-0x50 cells (kind 2, f6 = 0),
  one `rand() % 3` per slab laid (doc 12 §5; the cleared bit 8 lets the
  centre cell get a slab too); the hit points from the concrete rule are
  then overwritten here; build-up animation (flag 2); primary yard if the
  slot is empty; the crane flag 0x20 goes to whichever yard is primary
  **after** that step, which is the new yard itself when it just became
  primary; shroud reveal for the local player; build lists rebuilt.
- **Hit points:** yard hp = trunc(yard max hp × MCV hp / MCV max hp). With
  MCV 4500 max and yard 3000, a full MCV gives 3000. Port: compute `ratio`
  as float32 (`(float)hp / (float)max`), then the product **in double**
  (`(double)(uint32)rec_hp * (double)ratio`) and truncate. The product is
  exact in double, which matches the x87 product bit for bit.
- **The MCV is removed silently:** flag 0x200000 = not drawn (draw paths
  `0x42a670`, `0x42a907`, `0x42afc2` skip it; inferred meaning), 0x400000 =
  removed (state 0x11 skips the cell clean-up, doc 09 §2; the bit was
  already cleared above). SetState(0x11) also clears `+0x19` (selected).
  The state 0x11 countdown `+0x2f` (10 for vehicles) then runs while
  hidden, and the slot is freed when it reaches 0. **No loss counters**
  are touched (no `+0x24a08`), no explosion, no death sound.
- **Quirk (verified):** if CreateBuilding fails (inactive player or the
  object pool is full, "ReachedUnitLimit"), the MCV is **still removed**.
- The MCV turns to facing **0x1D** before deploying (constant pushed at
  `0x49b17a`). The deploy test is done after the turn, so a unit that
  drives into the block during the turn cancels the deploy (state 2).
- No other state exists for the MCV: order → 0x1E → (finish step, turn,
  test, create) → 0x11. The building's own build-up (doc 12 §7.1) is the
  visible "unpacking".

### 1.6 Computer player, DoMCVs (`FUN_00422140`, verified)

```
if player.+0x260b4 != 0: ai.+0x19EC = 1           // side has a build list
noList = (player.+0x260b4 == 0)
if tick & 0x3F != 0: return
for u in player units (list +8):
    if kind(u) != 6 or u.state != 2 or !CanTakeOrder(u, 0): continue
    cx = (u.x / 32) / 65536; cy = (u.y / 32) / 65536   // 16.16 → cell, C signed division
    if !CanDeploy(cx, cy): DealWithMCV(u)         // FUN_00422ec0
    elif u.+0x2E != 0 or noList: FUN_0045a3f0(side, u.index)
    // else: nothing
```

`DealWithMCV` (`FUN_00422ec0`, verified 2026-10-09):

```
x, y = (u.x / 32) / 65536, (u.y / 32) / 65536
if !CanIDeployMCVHere(x, y):                       // FUN_00423020
    leg = left = 1; dir = 0
    loop:
        x += DX[dir]; y += DY[dir]                 // 0x4eb2d8 / 0x4eb2dc
        if CanIDeployMCVHere(x, y): break
        if --left == 0: leg += GROW[dir]; dir = (dir + 1) & 3; left = leg
        if leg > 13: return                        // nothing found
    ClearUnits(side)                               // FUN_0041cdb0: every +0x19 = 0
    if u.state == 0x11: return
    u.+0x19 = 1
    ai.+F2C = x − 1; +F34 = x + 1; +F30 = y − 1; +F38 = y + 1
    GenerateUnitMoveOrder(side, x, y)              // FUN_00459450: order 0x13, the selected units
    ai.+0x1D49 = 1                                 // the rebuild flag
KickUnitsOutofArea(x − 1, y − 1, x + 1, y + 1, exclude = u)   // FUN_00423100
```

`CanIDeployMCVHere` (`FUN_00423020`): false within 2 cells of the edge
(x < 2, y < 2, x > W − 2, y > H − 2); else every cell of the 3 × 3 block
around (x, y) needs flags `& 0xa000 == 0xa000`, and a cell with any of
0x3f8 must belong to the side (`flags & 7`).

**Quirk (kept):** DealWithMCV sets the rebuild flag `+0x1D49` and the
rebuild rectangle. A side with a build list then runs the rebuild's
KickUnitsOutofArea on that box every 16 ticks with **no exclusion**, so
it pushes the MCV itself out of its site; DoMCVs only checks every 64
ticks. In a headless A6V2 test (a Mercenary MCV next to its yard) the
round trip was exactly 6 × 64 ticks and the MCV ping-ponged until the
rebuild gives up (10 × 40 passes). A side without a build list (no yard)
has no rebuild pass and deploys normally.

**Unit `+0x2E` (verified 2026-10-16).** CreateUnit (`FUN_00455870`) never
writes it, and a pool slot is always zeroed before reuse: the side clear
`FUN_0046b990` (`rep stosd` of 0x9a64 dwords at `0x46b9c8`) and the object
free `FUN_0046bdb0` (zeroes +0x00..+0x7B). Its writers (byte-write scan):
the delivery drop `0x49e687` (the delivery entry, with flag 0x40000), the
Deviator `0x49dc41` (0xF0) and the deviation timer `0x497ff7` /
`0x49808c`, and a debug text loader (`0x46407b`). So an MCV deploys
through DoMCVs only when its side has no build list, or when it came from
a delivery entry other than 0 (or is deviated). A built or map-placed MCV
of a side with a construction yard never deploys by itself. (Our default
was 0xff, which made every AI MCV on a good site deploy; it is 0 now.)

## 2. Engineers (kind 3) and saboteurs (kind 4)

### 2.1 Human input (`FUN_004433a0`, verified)

Left click on an enemy building (or Ctrl on any building), with units
selected and no turret building selected
(after `FUN_004a57c0` = "any unit selected"; with nothing selected the
building is selected if it has flag 0x200):

```
if owner == player and !AnyArmedSelected(): nothing   // Ctrl on own building
elif AllSelectedAre(4):  if !(bld.flags & 0x200): nothing; else Flash; Order15
elif AllSelectedAre(3):  if !(bld.flags & 0x400): nothing; else Flash; Order15
else:                    Flash; Order15               // mixed groups: no flag test
then ack voice FUN_0049f240(1)
```

`AllSelectedAre(k)` = `FUN_004a59c0`: at least one selected unit and every
selected unit is kind k. `bld.flags` is the object's `+0x28` (record +0x58
plus run-time bits). Flash = `FUN_0049f320(bld)`. The cursor
(`FUN_004a57f0` = "any selected unit of kind k"): armed selected → 2;
else any kind 4 selected → 0x1F if the building is not own and has 0x200,
else 3; else any kind 3 selected → 4 if not own and 0x400, else 3.

### 2.2 The order (`FUN_00459800`, order 0x15, verified)

Packet: type 0x15, side +0x24, target owner +0x22, target index +0x20,
tick +0x08, ids from +0x26 (≤ 64). It collects selected units of the
player that are not in state 0x11 and not kind 1 (harvester), 6 (MCV) or
0xE (thumper); **when the target owner is the player itself, kinds 3 and 4
are also left out** (Ctrl-click on an own building). No packet when the
list is empty.

ProcessOrder case 2 (verified): only if the target slot is a building
(`+0x90 == 2`); for each id with `+0x90 == 1` and `CanTakeOrder(u, 1)`:
`SetState(u, 5)` (accepted for kinds 3 / 4 although unarmed, doc 09 §3);
on success `+0x14 = 1`, `+0x26` = target owner (error if > 8), `+0x0c` =
target index.

### 2.3 State 5 for kinds 3 / 4 (`FUN_00496d10`, verified)

```
if u.target == -1:
    if u.+0x4f: MoveStep; return 1
    return 0
b = target building (owner u.+0x26, index u.+0x0c)
if b.+0x90 != 2 or b.state == 0x11:
    if u.+0x4f: MoveStep; return 1
    return 0
px, py = u.x >> 16, u.y >> 16                 // pixels
if kind is 3 or 4:
    if EntryCell(px, py, b, &ex, &ey) and u.+0x4f == 0:
        if b.flags & 0x100000C:      SetState(u, 2); return 1   // selling 4 / sold 8 / being entered
        if kind 3 and !(b.flags & 0x400): SetState(u, 2); return 1
        if kind 4 and !(b.flags & 0x200): SetState(u, 2); return 1
        RemoveUnitTileData(u)        // FUN_0044e120: clear its cell bits (and its "to" cell)
        u.+0x48, u.+0x49 = ex, ey
        SetState(u, 0x17)
        u.flags |= 0x400000
        b.flags |= 0x1000000         // reserved: one entrant at a time
        Uncloak(u)                   // FUN_0049f5c0
        return 1
// not adjacent yet (or mid-step), same as armed units:
ClosestCell(px >> 5, py >> 5, b, &cx, &cy)    // FUN_00497390 (doc 18 quirk)
u.+0x48, u.+0x49 = cx, cy
MoveStep(u, side, idx); return 1
```

- **EntryCell** (`FUN_004a47f0`): building top-left (bx, by) = (b.x >> 16) /
  32, ((b.y >> 16) − brec+0x86) / 32 (C signed division). Scan the 4 × 4
  mask +0x54 in bit order (row 0..3, column 0..3, bit = row·4 + col); the
  **first** occupied cell (bx + col, by + row) with |(px >> 5) − (bx + col)|
  < 2 and |(py >> 5) − (by + row)| < 2 is returned. So the unit must stand
  on a cell touching the building (8-neighbourhood); the entry cell is the
  first such building cell, not the nearest.
- **No hit-point threshold** anywhere (state 5, state 0x17 or the capture
  itself): engineers capture at any health.
- `Uncloak` (`FUN_0049f5c0`): if flags & 4 and `+0x31 == 0`: `+0x31 = 0x16`,
  flags &= ~4: ends stealth with the 22-tick fade timer (verified in doc 32
  §1, ported as `World::uncloak`).
- The 0x1000000 building bit makes a second engineer / saboteur give up
  (state 2) when it gets there, and blocks selling (`FUN_00456ee0` tests it
  at `0x456f03`). Only the capture clears it (`0x46d8da`). If the entry ends
  without a capture (the building started selling, see §2.4), the bit stays
  for the rest of the game. Port as is.

### 2.4 State 0x17, walking in (`FUN_00497b90` case 0x15, verified)

```
if MoveUnitSimple(u, side, 0, 0): return       // FUN_00495560, still walking
b = FUN_004a4740(u.+0x26, u.+0x0c)              // null unless slot is a live building
if u.+0x0c != -1 and b and !(b.flags & 0xC):
    victim = u.+0x26
    if kind(u) == 3:                            // ENGINEER
        if CaptureBuilding(victim, side, u.+0x0c):     // FUN_0046d6d0, §2.5
            player(victim).+0x24a0c += 1               // buildings lost
            player(side).+0x25418[(b.rec·8 + victim)] += 1   // i32 array
            player(side).+0x24c94 += 1                 // buildings killed
            player(side).+0x24784 += 1                 // buildings built (doc 60); CreateBuilding also adds 1
            if alliance[victim·8 + side] == 2: MakeHostile(victim, side)   // FUN_0049f870
            if side == local:     voice *_CAPTURE      // H_/O_/S_CAPTURE, FUN_0046f900
            elif victim == local: voice *_BUILDINGLOST; message "BuildingEngineered"
    else:                                       // SABOTEUR (any non-3 kind here)
        player(victim).+0x24a0c += 1
        player(side).+0x25418[(b.rec·8 + victim)] += 1
        player(side).+0x24c94 += 1
        if alliance[victim·8 + side] == 2: MakeHostile(victim, side)
        BuildingDestroyed(victim, u.+0x0c, 0)   // FUN_0049ca10 (doc 18)
        if victim == local: voice *_BUILDINGLOST
// always, also when the building is gone or selling:
u.flags |= 0x200000                             // hidden
SetInfAnim(u, 0)                                // FUN_00494050
player(side).+0x24a08 += 1                      // units lost (the entrant counts as lost)
player(side).+0x24a10[u.rec] += 1
KillUnit(side, idx)                             // FUN_0049cde0
```

- `MoveUnitSimple` (`FUN_00495560` with p3 = p4 = 0): a single straight
  step to +0x48/+0x49 **without** the passability test (skipped for state
  0x17), without reserving the "to" cell (p4 = 0) and without clearing the
  old cell at the hand-over (p3 = 0). Turns to the 8-way direction first;
  infantry pick a sub-slot in the building cell (first free from slot 4
  down) for the target pixel. Returns 0 when steps = 0 and current = dest.
  Step count for infantry uses `+0x00`, movement uses rec+0x08 (same values
  in practice). **Verified 2026-10-16** against `World::move_simple`: the
  "to" cell from `FUN_0045e060`; vehicles aim at the cell centre and count
  steps with rec+0x08, infantry at the sub-slot point (first free from slot
  4 down when the current slot bit is taken in the "to" cell) with unit
  +0x00; with p4 the "to" cell is reserved (vehicle: owner | 8; infantry:
  slot bit | owner, owner also in bits 25..27) and flag 0x10000000 set;
  each update turns the body (and turret) one step when the direction
  differs, else moves by rec+0x08 per axis toward the target with the
  half-speed dead zone, counts down +0x4f and at the half-way point (with
  p3) clears the old cell, then takes the "to" cell and clears 0x10000000.
- `alliance` = `0x798830[a·8 + b]`: 0 allied, 1 enemy, 2 neutral (inferred
  from `FUN_0049f870`, which sets both entries to 1 and, if `b` is the local
  player, shows `NeutralBecomeHostile<house of a>`).
- **KillUnit for the entrant** (`FUN_0049cde0`): infantry whose `+0x20` is
  not 9..13 get death mode `(rand() & 3) + 9` (line 0x1bd2); because
  SetInfAnim(u, 0) ran just before, **exactly one rand()** is used here. No
  S_MANDYING scream (skipped in state 0x17). Engineers have no death
  explosion (urec+0x2F = 0xFF). hp = 0, SetState(0x11). **Kinds 4 and 11
  (saboteur and Sardaukar) then get an area hit** `FUN_0049d2e0(cx·32,
  cy·32, 0, SARDDEATH, side, 0xffff, 0)` at the **top-left pixel** of the
  unit's cell (+0x4a/+0x4b, = the building cell it walked into).
  `0x6d60f0` is the bullet index of "SARDDEATH" (set by the bullet loader
  at `0x466ff8`; Templates.bin value 28). So a saboteur both destroys the
  building and explodes.
- The entrant stays hidden in state 0x11 for its `+0x2f` countdown (0x32
  for infantry) and is then freed; flag 0x400000 stops the cell clean-up,
  which RemoveUnitTileData already did.
- rand() order for a saboteur: the rands of BuildingDestroyed (doc 18),
  then the death-mode rand, then those of the SARDDEATH damage. For an
  engineer: CreateBuilding lays no concrete (f6 = 1), so the capture
  itself uses none, and neither do unit conversions (§2.6): `FUN_00455870`
  and its callees (`FUN_0045e020`, `FUN_004a5e80`, `FUN_0049e6b0`,
  SetState) call no rand() (verified 2026-10-16); then the one death-mode
  rand.

### 2.5 The capture, `FUN_0046d6d0(victim, captor, idx)` (verified)

```
old = player(victim)+0x30 + idx·0x94
if old.flags & 0xC: return 0                  // selling (4) / sold (8)
(bx, by) = old top-left cell (as EntryCell)
RemoveBuildingTiles(old.rec, bx, by, victim)  // FUN_0044dee0: group count −1,
                                              // FUN_0044df60 clears the cells,
                                              // rec flag 0x200000: FUN_0044f2f0
n = CreateBuilding(captor, old.rec, bx, by, 0, 1, 1)
if n == 0xffff: return 0                      // quirk: tiles already removed
new = player(captor)+0x30 + n·0x94
copy 0x7C bytes old → new                     // fields +0x00 .. +0x7B
new.+0x42 = n; new.+0x40 = captor; new.+0x50 = 0xff; new.+0x52 = 0xffff
if new.+0x35 == 1: DeselectAll(); new.+0x35 = 1    // FUN_004a5790
RecalcStorage()                               // FUN_0044abe0, all 8 sides
if kind(old.rec) is 4 (refinery) or 5 (silo):
    a = (u32)(player(victim).spice · 2000) / player(victim).capacity   // +0x24254, +0x24258
    player(captor).pendingSpice += a          // FUN_0046c790 (+0x24264)
    Spend(player(victim), a)                  // FUN_0046c720 (doc 11)
old.+0x3b = 1                                 // death countdown 1
SetBuildingState(old, 0x11)                   // FUN_0049e120
old.flags |= 0x110000
if new.flags & 0x80: new.flags &= ~0x90
new.flags = (new.flags & ~0x01000001) | 0x2000000
for h in player(victim) units:                // docked harvesters
    if kind(h) == 1 and h.state == 0xF and h.+0x58 == idx:
        player(victim).+0x24a08 += 1; player(victim).+0x24a10[h.rec] += 1
        c = ConvertUnit(victim, captor, h.index, 1)    // FUN_0046d2e0
        if c: c.+0x58 = n
SetBuildingState(new, 2)
return 1
```

What this means for a port:

- **Same record, same place.** The captured building keeps the victim's
  house variant (`+0x34` record is copied), its hit points, animation
  counters, upgrade state and everything else in +0x00..+0x7B. CreateBuilding's
  own concrete hit points, build-up flag and primary flag are overwritten by
  the copy.
- **`+0x3c` keeps the victim's side** (only +0x40 is rewritten). Verified
  2026-10-16: +0x3c is not an owner but the **side of the building's
  target** (+0x2c): the turret orders write both (`0x4585dc`, `0x4586d7`),
  the turret states 4 / 5 (`FUN_004975d0` / `FUN_004976b0`) and the target
  check of `FUN_0049b7f0` (`0x49b919`) read them, and the target clean-up
  `0x46eb36` compares them; CreateBuilding just starts it at the owner.
  Capturable buildings (record flag 0x400) are never turrets, so after a
  capture nothing acts on the copied value.
- **Primary slots:** CreateBuilding put the new index in the captor's
  slot if that kind's slot was empty; the copy then overwrote the flags, and
  0x80 / 0x10 are cleared if the victim's building was primary. Result: the
  captured building **never has flag 0x80**, even when it fills the
  captor's slot. The victim's slot is repaired one tick later, when the old
  object's state 0x11 runs `FUN_0046d9a0` (doc 21 §3.3).
- **Old object:** state 0x11 with countdown 1 (one tick of decrement, then
  removal on the next update); flag 0x10000 skips the second tile removal
  and the survivors (doc 21 §3.3–3.4); flag 0x100000 is tested by the
  building draw (`0x428efd`, `0x427ddb`), inferred "not drawn". No
  explosion, no loss counter in this function (the caller counts).
- **New flags:** 0x1000000 ("being entered") and bit 0 cleared; **0x2000000
  set** = captured. A computer player sells buildings with 0x2000000 at once
  (MonitorBuildings, doc 22 §3: every 8 ticks for its side), so **the AI
  sells everything it captures**.
- **Production queues / sidebar:** nothing here touches the victim's build
  queue or the captor's lists beyond CreateBuilding's `FUN_0046be50`
  (build lists rebuilt for the captor) and `+0x245c1` group counts (victim
  −1 in RemoveBuildingTiles, captor +1 in CreateBuilding). The victim's
  side is updated when the old object is removed one tick later, like any
  removal (verified 2026-10-16): its state 0x11 runs `FUN_0046d9a0`
  (primary slots, doc 21 §3.3), and the player loop `FUN_00458e90`
  rebuilds that side's build lists (`FUN_0046be50` at `0x45906d`). A unit
  being built for a factory kind that no longer exists is then cleared
  without a refund by the next slot pass (doc 12 §8.2).
- **Spice transfer:** only for refineries and silos. The share uses 2000
  for both (silo capacity is 1500): a = victim spice × 2000 / victim
  capacity, computed after `RecalcStorage` but while the old building is
  still in the victim's list, so the capacity still includes it. u32
  multiply and divide (overflow ignored). The captor gets it as pending
  spice; the victim pays it through the normal Spend (which fails and takes
  nothing if the victim cannot cover it).
- **AI lists of the victim:** its initial-building entry sees the old object
  in state 0x11 and marks it for rebuilding (doc 22 MonitorBuildings).
- **Units on the footprint** (doc 12 §6 step 12): CreateBuilding runs
  `FUN_0044dbd0`, which converts every unit of another side found on the
  occupied cells (`FUN_004a5270` / `FUN_004a54a0`, units whose current or
  next cell is there, not worms, not flags 0x400140) with
  `ConvertUnit(owner, captor, idx, 0)`, counting a loss for the owner. The
  entrant is not found (its cell bits were cleared and it has 0x400000).
  A docked harvester is never found here (verified 2026-10-16): the dock
  cell is record +0x60/+0x61 = (2, 1), which is outside the refinery
  footprint 0x36 (cells (1, 0), (2, 0), (0, 1), (1, 1)), so only the
  explicit loop above converts it, keeping state 0xf and getting +0x58 =
  the new building (test in `tests/sim/test_sell_repair_capture.cpp`).
- **Capturing a dying building** (verified 2026-10-16): state 0x17 takes
  the building from `FUN_004a4740`, which returns any building still in
  its slot, and BuildingDestroyed (`FUN_0049ca10`) sets state 0x11 and
  +0x3b = 0x1e but neither flag 4 nor 8, so a building destroyed during
  the walk-in is still captured. The copy then has state 0x11 and the
  remaining countdown, `FUN_0049e120(new, 2)` refuses to leave 0x11, and
  the captured copy is removed when its countdown ends. (We forced the
  copy alive before.)

### 2.6 ConvertUnit, `FUN_0046d2e0(from, to, idx, keepState)` (verified)

```
o = player(from) unit idx
if o.flags & 0x400000: return null
if infantry(o):                               // urec+0x11
    clear its sub-slot bits in the current cell (and the "to" cell if reserved)
    o.flags |= 0x600000; SetState(o, 0x11); return null     // removed, not converted
clear its vehicle bit 8 (current, and "to" if reserved; owner bits restored as in doc 09)
n = CreateUnit(to, o.rec, o.+0x4a, o.+0x4b, o.+0x4a, o.+0x4b, 0, 0)   // FUN_00455870
if n == 0xffff: return null
c = player(to) unit n
copy 0x7C bytes o → c
c.+0x19 = 0; c.+0x30 = 0xff; c.+0x32 = 0xffff; c.+0x2a = 0xff; c.+0x26 = 0xff
c.+0x0c = 0xffff; c.+0x14 = 2; c.+0x48, c.+0x49 = c.+0x4c, c.+0x4d; c.+0x28 = 0xff
o.flags |= 0x600000
if !keepState: SetState(c, 7)                 // walk to its "to" cell
if c.flags & 0x10000000:                      // re-reserve the "to" cell for the new side
    vehicle:  to-cell = (f & ~7) | to | 8
    infantry: (not reached)
c.flags |= 0x2000000
SetState(o, 0x11)
return c
```

### 2.7 Saboteur, the differences in one place

Kind 4, Ordos palace unit (`0x6d60ec` = record 23, doc 12 §8.4). Same
order (0x15) and state 5 / 0x17 path as the engineer, but: needs building
flag **0x200** (turrets included), cursor 0x1F; on arrival the building is
**destroyed** (`FUN_0049ca10`), there is no CAPTURE voice and no
"BuildingEngineered" message; its own death adds the SARDDEATH area hit.
It also has the stealth special action (order 0x2F, click needs `+0x29 ≥
0xA0`, sets flag 0x10, `S_STEALTHUP`), not documented further here.

## 3. Open points

All settled 2026-10-16:

- Unit `+0x2E`: §1.6 (0 unless a delivery or the Deviator set it).
  `FUN_00423020` / `FUN_00422ec0` match the pseudo-code above (the step
  table `0x4eb2d8` = DX {−1, 0, 1, 0}, DY {0, 1, 0, −1}, GROW {0, 1, 0, 1});
  `FUN_00423100` is KickUnitsOutofArea (doc 22, ported with the AI).
- Building `+0x3c`: the target's side (§2.5). Player `+0x24784`: the
  buildings-built counter, read only by the dead debug box and the
  Westwood Online upload (doc 60); not kept.
- Cursor 0x1F: only its index matters (§0).
- Unit flag 4 / `+0x31`: the stealth fade, doc 32 §1.
- `FUN_00455870` uses no rand() (§2.4).
- Reference check (needs the user's machine): deploy an MCV next to the map
  top edge (row 1) and confirm it refuses; capture an enemy refinery and
  watch the spice counters.

## 4. Implementation notes (2026-10-07)

`src/sim/capture.cpp` ports sections 1 and 2; `World::kill_unit` got
the death-mode rand() and the SARDDEATH hit. Choices that follow from
our object model:

- Buildings live in one global vector, so the captured building is a new
  entry appended by `create_building(.., captured = true, no_crane = true)`
  and the old entry is marked dying with timer 1 and flags 0x110000.
  `remove_building` skips the cell clearing for 0x10000, and
  `building_at_cell` now follows FUN_004a5640 (cell bit 0x10, the cell
  owner's buildings only), so the old object is never found on its cells.
- The death countdown of buildings now removes on the update **after** it
  reaches 0 (FUN_0049b7f0 case 0x11: decrement and return while non-zero),
  one tick later than before for destroyed buildings.
- ConvertUnit copies the whole unit and then resets the fields listed in
  section 2.6; the new unit keeps a fresh id.
- Not yet: DealWithMCV (the AI only deploys MCVs that stand on a good
  site), Uncloak (no stealth yet), the per-record kill counters
  (+0x25418) and +0x24784, the carried-cargo and delivery-table parts of
  KillUnit, the units-on-footprint conversion of CreateBuilding.
  **Update 2026-10-16:** DealWithMCV and Uncloak are ported; the capture
  of a dying building keeps it dying; unit +0x2E starts at 0.
  **Update 2026-10-16:** the per-record counters (docs/re/60; +0x24784 is
  not kept, nothing reads it) and the footprint conversion (doc 12 §6
  step 12) are ported.
