# 35: Ornithopter airstrike (1.06)

Written for a line-by-line port of the Atreides **ORNITHOPTER** (unit
record 20, kind 9). In 1.06 the player never gets an ornithopter as a
normal unit. The record is an **airstrike special**, like the Death Hand.
The high-tech factory charges it, the player clicks a target cell, and 3
ornithopters fly in from the map edge. They drop 12 bombs each around
that cell and then fly away. The player cannot select or order them.
They pick no targets. They never land, rearm or return. The palace has
no separate airstrike: the "airstrike" of doc 14 §1.6 is this same path.

Conventions as in doc 33: `unit+…` are object fields (objects at
`player+0x30`, stride 0x94), `player+…` is `FUN_004a4660(side)`,
`urec+…` is `0x77e250 + rec · 0x100` (kind at +0x2D), `brec+…` is
`0x6dc540 + rec · 0x10c`. Positions and altitude are 16.16 px. A unit
production slot is `player+0x26530 + i · 0x14` (i = 0..9): +0 item
(i16), +2 progress (u16, 0x5A00 = done), +0x10 hold byte, +0x11
**"used" byte** (target already sent).

"Verified" means it was read from the decompile, and from the
disassembly where it matters. Every open point was settled against the
exe on 2026-10-16 (§12).

Main functions:

| Address | Name used here | Role |
|---|---|---|
| `FUN_004466e0` | AutoStartSpecials | every tick: (re)start every special in the unit build list |
| `FUN_00455510` | StartUnit | order 0x27 handler, also called directly by AutoStartSpecials |
| `FUN_004467e0` | DoIconBuild | slot progress, "AirStrikeReady" message (doc 11 §7, doc 12 §8.2) |
| `FUN_00442a00` | UnitBuildFactor | F = 100 × difficulty for specials |
| `FUN_004433a0` | HandleGameLoopEvents | sidebar click → mode 4, target click, cursor 9 |
| `FUN_0045a440` | PostSpecialOrder | posts order 0x30 (side, kind, x, y) |
| `FUN_00457810` case 0x1d | ProcessOrder 0x30 | clears the ready slot, kind 9 → airstrike |
| `FUN_0046cf10` | HomeView | the camera point of the ordering side's base (doc 25 A.8) |
| `FUN_0049e770` | LaunchAirstrike | creates the 3 ornithopters |
| `FUN_0049e6b0` | EdgePoint | point past the nearest map edge, facing toward the map |
| `FUN_00455870` | CreateUnit | generic unit creation (doc 12 §8.3) |
| `FUN_0049deb0` | SetState | state change with refusals |
| `FUN_00497b90` | UnitUpdate | state 7 (case 5) / state 0x11 (case 0xf) for aircraft |
| `FUN_00494fd0` | FlightStep | flight, wings, bombing, leaving (doc 14 §1) |
| `FUN_004954b0` | TurnAir | one facing step |
| `FUN_00456a30` | AddBullet | the bomb (doc 18 §4.1) |
| `FUN_0049be60` | BulletUpdate | falling bomb (doc 18 §2.3.1, §4.2) |
| `FUN_004a4900` | EnemyUnitInRange | air pass: anti-air shooters find the ornithopter (doc 18 §7b) |
| `FUN_0042a5d0` | DrawAircraft | shadow + body (doc 14 §5.5) |
| `FUN_00422270` | DoSpecialWeapons (AI) | case 3: the AI airstrike |
| `FUN_00422730` | ItemReady (AI) | ready slot of a special kind |
| `FUN_00422800` | GetBestTarget (AI) | random enemy building / unit |

## 1. Data (Templates.bin, verified with a dump script)

Unit record 20 `ORNITHOPTER` (`0x6d60ea` = 20 is the special id the
launcher uses):

| Field | Value | Meaning |
|---|---|---|
| +0x00 house mask | 0x7F | all houses (the factory limits it, below) |
| +0x02 armour | 4 | LIGHT |
| +0x03 | 3 | turn delay: one facing step per 4 ticks (doc 14 §1.2) |
| +0x04 hp | 900 | |
| +0x08 speed | 0x6C000 | 6.75 px/tick |
| +0x0C / +0x0D | 19 / 0xFF | primary ORNIBOMB, no secondary |
| +0x0E | 10 | (unused by this path) |
| +0x10 view | 0 | **does not reveal the shroud** (doc 16 §3.4 needs view ≠ 0) |
| +0x14 element | 17 | 3 frames × 32 rotations (DATA.R16 2190–2285) |
| +0x1C cost | **0** | |
| +0x20 build rate | **2** | |
| +0x24 tech | 6 | |
| +0x28 prerequisite 1 | 11 | AT_HIGH_TECH_FACTORY, needs upgrade level 1 (doc 28 §1.2) |
| +0x2D kind | 9 | |
| +0x2F death explosion | 10 | EX_BANG14 |
| +0x38 flags | 0 | |
| +0x3C..+0x5B | 0..31 | rotation = facing |

Bullet 19 `ORNIBOMB` (`0x782068 + 19 · 28`):

| Field | Value |
|---|---|
| speed | 6.0 px/tick (not used: the bomb has dx = dy = 0) |
| damage | 400 |
| flags | 0x180 = 0x80 animated (frame cycles every tick) + 0x100 falls from the firer's altitude |
| fire sound | −1 (none) |
| impact explosion | 10 EX_BANG14 (23 frames, draw flag 0x200 additive, sound 19 `S_SMALLEXP4` / explsml4) |
| trail | none |
| anti-air | 0 |
| warhead | 2 W_SPECIAL (radius 64 px; NONE 90, WALL 50, BLDG 75, WOOD 60, LIGHT 60, HEAVY 60, CONCR 100, CY 25, HARV 60 %) |
| range | 32 |
| frames | 4 (`0x795340[19]`, u32 table) |

## 2. Production (verified)

### 2.1 Auto-start (`FUN_004466e0`, every tick)

It runs in the tick block after the script, the defeat checks and the
blooms, before `FUN_00446690` (doc 33 §5):

```
for p in 0 .. player count − 1:
    for each rec in p.unit_list (+0x261b4, count +0x261ac):
        if urec[rec].kind in (9, 10, 4, 12): StartUnit(p, rec)     // FUN_00455510
```

`StartUnit` (the order 0x27 handler, called directly, no order) for a
special:

```
if some slot already has rec:
    if slot.hold == 0: return                       // already running or ready
    if cash + spice == 0 and urec.cost != 0: return
    slot.hold = 0
    local player only: *_TRAINING (infantry) / *_BUILDING voice   // quirk, below
    return
FUN_004430d0(p, rec, 1)                             // specials always pass
take the first free slot: item = rec, progress = 0, prev = 0, float = 0, paid = 0, hold = 0
if cash + spice == 0: hold = 1                      // no voice for specials
```

So the airstrike starts **by itself** as soon as it is in the build list,
and starts again on the next tick after it is used. The sidebar never
starts it: a click on a special that is not ready does nothing (§3.1).

Quirk (cost 0): a player with cash + spice = 0 gets the new slot on hold.
On the next tick StartUnit sees the hold and clears it, because the
cost is 0. For the local player it then plays `*_BUILDING` once.

### 2.2 Progress

DoIconBuild (doc 12 §8.2): each tick, if the factory exists
(`FUN_00442c20`, check 0; for prerequisite name 11 it is the primary
high-tech factory `player+0x2609e`, else `+0x260a0`), the slot is not on
hold and progress < 0x5A00:

- the factory gets flag 0x10 (it animates while charging);
- step = rate × F / 100, with F = `FUN_00442a00` = 100 × difficulty / 100
  for kinds 9 / 4 / 12. Power does not matter. Difficulty
  (`FUN_00442960`) is easy 125, normal 100, hard 75 for the local player
  and 100 for the AI. Integer maths: easy and normal give step 2, hard
  gives step 1;
- payment 0 (cost 0), which `FUN_0046c720` always accepts;
- at 0x5A00: for the local player only, text message `AirStrikeReady`
  (`FUN_0048dc10("AirStrikeReady", 1, −1)` → `FUN_00425720`). No voice.
  The factory flag 0x10 is cleared. The slot stays at 0x5A00. A special
  never spawns from the factory.

Charge time: 23040 / 2 = **11520 ticks** (9 min 36 s at 20 ticks/s) on
easy and normal; **23040 ticks** on hard.

If the factory is gone (F = 0 from `FUN_00442c20`), the slot is cleared
with no message. It comes back through AutoStart once a factory exists
again and the build list has the item.

## 3. Player orders (verified)

### 3.1 Sidebar click (`FUN_004433a0`, left button up, unit column)

```
rec = unit_list[row + scroll]
if urec[rec].kind in (9, 10, 4, 12):
    found = false
    for each of the 10 slots: if item == rec and progress == 0x5A00 and used(+0x11) == 0:
        found = true
        if kind in (4, 12): used = 1                // only saboteur / Fremen here
    if found:
        kind 9 → mode 0x516438 = 4                  // airstrike target
        kind 10 → mode 5; kind 4 / 12 → order 0x30 at once
else: order 0x27
```

The airstrike's "used" byte is **not** set by the icon click. Right
clicks on a special do nothing (doc 11). Right button down anywhere sets
mode 0, which cancels the targeting.

### 3.2 Cursor

While mode = 4, the playfield cursor is **9** (blue ring, doc 10 §5). No
other test is made: shroud, unit and building cells all show 9. Outside
the playfield the normal edge, radar and sidebar cursors are used.

### 3.3 Target click (left button up, mode 4, `0x445470`..`0x4455a7`)

```
if mouse_x < 480 (0x786d48) and top (0x4e41dc) < mouse_y:      // in the playfield
    any = false
    for each slot: if item ≥ 0 and urec[item].kind == 9 and progress == 0x5A00 and used == 0:
        used = 1; any = true
    if any:
        cx = (cam_x + mouse_x) >> 5                 // unsigned
        cy = (cam_y − top + mouse_y) >> 5
        PostSpecialOrder(local, 9, cx, cy)          // FUN_0045a440: order 0x30
    mode = 0
```

- **No shroud test**: an unexplored cell can be targeted.
- No sound or voice on the click.
- **A drag release never reaches this code** (verified, `FUN_004433a0`):
  a left press in the playfield arms the drag (`0x516458` = 1, start
  point `0x516450` / `0x516454`) in every mode. On the release the drag
  test comes first: with the drag armed and the mouse 4 px or more away
  from the start on **both** axes, the box select `FUN_004a5ae0` runs
  (and the select voice when it returns non-zero), then mode = 0
  (`0x445463`..`0x445478`, or `LAB_004455a7`). So a box drawn in mode 4
  selects units and cancels the targeting; no slot is marked used and no
  order is sent. A release closer than 4 px on either axis is a click.
  The same holds for mode 5 (Death Hand). Ported (`GameScreen`).

### 3.4 Order 0x30 (`FUN_00457810` case 0x1d)

The order record (`FUN_0045a440`): type +0x23 = 0x30, side +0x24, kind
+0x26 (u16), x +0x1c, y +0x1e, tick `0x5173f4`. It goes through the
order queue (`FUN_0045bf00`) and runs on the next order pass, as all
orders do (doc 09).

```
P = player(side)                                    // ebx
cleared = false
for each slot: if item ≥ 0 and urec[item].kind == order.kind and progress == 0x5A00:
    item = −1; used = 0; cleared = true             // the used byte is not checked
if !cleared: return
kind 9:
    HomeView(P, &px, &py, 0)                        // FUN_0046cf10, thiscall ECX = P
    LaunchAirstrike(side, (px / 32) & 0xffff, (py / 32) & 0xffff, order.x, order.y)
```

`HomeView` (doc 25 A.8) returns the **camera top-left** in pixels that
would centre the side's base, not the base itself. The base is the
primary construction yard, else the building with the smallest name
index, else the last MCV, else the first unit, else the local player's
current camera (`0x798538` / `0x79853c`, which differs per machine in a
network game).

**That last fallback is unreachable from order 0x30** (verified):

1. The order clears a slot only if it is ready (0x5A00). A ready slot
   was kept by DoIconBuild (`FUN_004467e0`, `0x4469..`) in the previous
   tick, and DoIconBuild clears every slot whose FindFactory
   (`FUN_00442c20`, check 0) returns 0. FindFactory returns non-zero
   only for a primary factory index or a building in the side's list
   `+0x10`. So the side had a building at that point.
2. A building leaves the list only when the model update removes it
   (`FUN_00458e90`), and the order pass runs at the start of
   `FUN_0044aca0`, before it. The steps between DoIconBuild and the next
   order pass (`FUN_0046d180` / `FUN_0046ddc0` / `FUN_0046def0`,
   `FUN_0044c520`, then DoAI, the script, `FUN_00455480`, the blooms,
   AutoStartSpecials, `FUN_00446690`) remove no building from a list:
   destroyed buildings stay listed while they burn down. HomeView's
   building loop has no state test, so it finds one.
3. Only an earlier order in the same pass could change that: the
   surrender order 0x31 / a leave (`FUN_0046e6b0`) of the same side. It
   only kills (the buildings stay listed) and sets `+0x24250` = 0, and
   CreateUnit refuses an inactive side (`0x45591b`), so no ornithopter
   is created and HomeView's result is not used.

So single player and network games behave the same; our `order_special`
passes (0, 0) in that case, which is never used. The camera point is x − 240 and y − 190 (minus half the
image height for a building), clamped to [0, map·32 − view − 1].
Dividing it by 32 rounds toward 0, so the origin cell is about 7 cells
left of and 6 cells above the base, or 0 near the top-left corner.
Argument 4 = 0, so no camera move or selection happens here.

## 4. Launch (`FUN_0049e770`, verified)

```
LaunchAirstrike(side, ox, oy, tx, ty):
    for i in 0, 1, 2:
        idx = CreateUnit(side, 20, 5, 5, 5, 5, 0, 0)          // FUN_00455870
        if idx == 0xffff: continue                            // pool full / side inactive
        u = unit idx
        x = ox; y = oy                                        // i16 each
        f = EdgePoint(&x, &y, 3 · i)                          // n = 4 + 3i cells
        u.+34 x = (i16)x << 21                                // cell corner, px << 16
        u.+38 y = (i16)y << 21
        u.+48 dest x = tx; u.+49 dest y = ty                  // bytes
        u.+3c altitude = 0x3c0000                             // 60 px
        u.+29 bombs = 12
        u.flags |= 0x10800
        SetState(u, 7)
        u.+54 body facing = u.+55 desired heading = f
```

What `CreateUnit` gives first (doc 12 §8.3): hp 900, flags = urec+0x38 |
0x40 (for kind 9 also altitude 0x3c0000), speed = record speed, +0x1a =
3, +0x2f = 10, wing fields +0x1e = +0x24 = +0x25 = 0, group +0x28 = 0xff,
+0x0c target = 0xffff. Current / next / home cells (+0x4a/4b, +0x4c/4d,
+0x50/51) = **(5, 5)**. Kind 9 skips the ground-cell test and the cell
occupation. The initial state: spawn = move cell, so a local player's
unit keeps state 2. For an AI side in single player it asks for state 3,
which SetState refuses for kinds 2/8/9/10. Then LaunchAirstrike sets
state 7.

Final flags: 0x40 aircraft, 0x800, 0x10000, plus the 0x80000000 that
SetState sets. 0x10000 keeps the record speed every step (no slow-down,
doc 14 §1.3). 0x800 makes the leaving test work (§5.4).

### 4.1 Edge point (`FUN_0049e6b0`, verified; differs from our code)

```
n = k + 4
ex = (x > W/2) ? W − x : x          // W, H = map size; W/2 signed int division
ey = (y > H/2) ? H − y : y
if ey <= ex (unsigned):             // top / bottom wins ties
    if y <= H/2: y = −n; return 16  // top, flying south
    else:        y = H + n; return 0
else:
    if x <= W/2: x = −n; return 8   // left, flying east
    else:        x = W + n; return 24
```

Only one coordinate changes. The other keeps the home-view cell. So the 3
ornithopters start in a line straight out from the edge, 4, 7 and 10
cells past it, all facing straight into the map. They then turn toward
the target at one step per 4 ticks. They arrive about 3 cells (14 ticks)
apart.

## 5. Flight and bombing (state 7, verified)

`UnitUpdate` (`FUN_00497b90`) for an aircraft in state 7 (switch case 5,
flag 0x40, no 0x20000) calls `FlightStep(u, side, idx, 0)`. A return
of 0 would set state 2. For kind 9 the step **always returns 1** (no
centring, §5.3), so the ornithopter stays in state 7 until state 0x11.
Before the step, the generic top of `UnitUpdate` runs: damage smoke for
non-infantry (+0x27, doc 14 §7), the +0x2b and +0x2a counters, and the
target-index check (+0x0c stays 0xffff). Nothing there is kind 9
specific.

### 5.1 One FlightStep for kind 9 (mode 0, not state 0x11)

```
// wings (also kind 15), every tick before anything else
if u.+25 == 0:
    u.+25 = 4
    if u.+24 == 0:   if u.+1e < 2: u.+1e += 1  else: u.+24 = 1; u.+25 = 20
    else:            if u.+1e == 0: u.+24 = 0  else: u.+1e −= 1
else: u.+25 −= 1

// leaving (§5.4)
if (to.x ≥ W + 4 or to.y ≥ H + 4) and flags & 0x800 and flags & 0x4000:   // to = +4c/+4d, unsigned bytes
    SetState(u, 0x11); return 1

T = dest · 32 + 16 px, as 16.16 (dest = +48/+49, unsigned bytes)
// no centring for kind 9
cx = trunc0((x >> 16) / 32); cy likewise           // signed, from the position before the move
d2 = (dest.x − cx)² + (dest.y − cy)²               // signed ints
speed = record speed                               // 0x10000 set: never slows
straight = false
if d2 < 32:                                        // within ~5.6 cells
    if d2 < 16 and (global tick & 7) == 0 and u.+29 != 0:
        AddBullet(side, urec+0x0c = 19, delay 0, firer idx,
                  x0 = x px, y0 = y px, tx = x px, ty = y px, 0xffff, −1)   // px = trunc0(p >> 16)
        u.+29 −= 1
    straight = true
if u.+29 == 0:
    straight = true; flags |= 0x4000              // bombs gone: leave
if cell x == dest.x or cell y == dest.y: flags &= ~0x400
u.+55 = atan(x px − T.x, T.y − y px) >> 7          // desired heading, truncating
flags |= 0x400
u.+00 = speed
if !straight: TurnAir(u, u.+55)
move by DX/DY[facing][speed >> 11], update cells and the 0x800000 cell flag (doc 14 §1.1)
return 1
```

Notes, all from the code:

- **Wings:** the frames run 0 → 1 → 2, one step every 5 ticks. Frame 2
  holds for 26 ticks, then the frames run back 2 → 1 → 0. One cycle is
  46 ticks. The 3 ornithopters start in phase.
- **Bomb timing** uses the **global** tick (`0x5173f4 & 7`), not the unit
  index. All ornithopters in range drop on the same ticks, one bomb per
  8 ticks each.
- **Bomb position** is the ornithopter's ground point **before** this
  tick's move. A bomb does not inherit the aircraft's motion (dx = dy =
  0, §6).
- **Bomb zone:** d2 < 16 (strict), i.e. the cell distance is less than
  4. **No-turn zone:** d2 < 32. So it flies a straight chord across the
  target area and only turns once it is about 5.7 cells away.
- **No target search, no weapon range, no reload:** the "weapon" is just
  this timed drop. `+0x0c`, `+0x26`, `+0x2e` (reload) are never used.
- **First step:** the "to" cell is still (5, 5) from creation.
  `FUN_0049f330` therefore clears cell flag 0x800000 at (5, 5) once, and
  the new cell is outside the map (no flag set). Harmless unless another
  aircraft is over (5, 5) at that tick; then that aircraft is not seen
  by the air pass for one tick. Keep it.

### 5.2 Passes (inferred from the numbers)

At 6.75 px/tick a chord through the centre of the bomb zone (about 8
cells, 256 px) takes about 38 ticks, so about 4–5 bombs per pass. A full
turn takes 32 × 4 = 128 ticks (radius about 137 px, 4.3 cells). Expect
about 3 passes before the 12 bombs are gone. The pass count is not a
rule of its own: it follows from the step code of §5.1 (ported exactly,
`World::flight_step`) and the approach angle, so the port gives the
exe's count for every strike.

### 5.3 No arrival

`FlightStep` skips the centring block for kind 9 (the `kind != 9` test
at `0x4950cc`). The ornithopter never stops, never lands and never goes
to state 2. The idle-carryall circle (doc 14 §1.7) is carryall-only. In
1.06 the state 2 code has no kind 9 branch. If an ornithopter ever were
in state 2, it would fall through to the generic idle code
(`FUN_0049f5f0` / `FUN_00496480` / `FUN_00494210`). Nothing puts it
there.

### 5.4 Leaving and removal

Once the bombs reach 0, flag 0x4000 is set and every later step flies
straight (no turn) in the current facing. On the next steps the leaving
test reads the **unsigned** "to" cell:

- right / bottom: removed when the cell reaches W + 4 / H + 4;
- left / top: at x in [−63, −32] px the cell is −1 = 255 ≥ W + 4, so it
  is removed about 32 px past the edge. The `< −4` tests in the code can
  never be true.

`SetState(0x11)` → `UnitUpdate` case 0xf: +0x2f counts down from 10 while
`FlightStep(mode 0)` keeps flying it straight at its speed. No wings
(the state 0x11 branch skips them), no bombs. At 0 the cell flag 0x800000
is cleared and the unit is removed (`FUN_00458e90`: +0x24628[20] −= 1, no
carryall counters). No explosion and no sound (doc 14 §3.3).

A bomb dropped while the ornithopter is outside the map is removed on
its first update with no impact (bullet update step 3, doc 18 §4.2).

## 6. The bomb (`FUN_00456a30` → `FUN_0049be60`, verified in doc 18)

- Creation: from = to, so dist = 0. Flag 0x100 keeps it linked (other
  dist-0 bullets are dropped), with dx = dy = 0. flags |= 2 (shadow),
  height = firer's altitude = 60 px, vz = 0. Frame 0 (flag 0x80).
- Each update: frame = (frame + 1) % 4; vz += 0x2000; height −= vz.
  After k updates, height = 60 px − k(k+1)/16 px. It lands on update 31
  (30·31 = 930 < 960 ≤ 31·32).
- Landing: height = 0, impact at its own pixel (target = current
  pixel). `DamageTiles(x, y, 0, …)` (doc 18 §2): height 0 is "not air",
  so it hits ground units and buildings within 64 px. Damage =
  pct(W_SPECIAL, armour) · 400 / 100 · (64 − d) / 64. **There is no
  friendly-fire check**: own and allied objects are hit too. If a spice
  bloom sits on the impact cell, the bomb bursts it and damages nothing
  else (doc 18 §2, doc 31). Craters per doc 18 §2.4.
  EX_BANG14 is placed on the ground, additive, sound S_SMALLEXP4 at the
  cell.
- The firer index is the ornithopter's. If the ornithopter is removed
  while the bomb falls, firer = 0xFFFF (doc 18 §4.2 step 1), so the hit
  unit records no attacker index.
- Drawing (doc 18 §4.4): bomb image `0x714b00[19 · 32 + frame]` at
  y − height, and its shadow at the ground point, drawn over units,
  explosions and aircraft.

## 7. What can hit it (verified)

- **Bombs and ground explosions cannot**: DamageTiles hits an aircraft
  (flag 0x40) only when the impact height is > 6 px.
- **Anti-air shooters**: the air pass of `EnemyUnitInRange` (doc 18
  §3.4, §7b) finds enemy aircraft over cells with flag 0x800000 (not the
  frigate). In 1.06 only two bullets are anti-air: NEW ROCKET (12) and
  MTANK PRI (14). The shooters are the **MISSILE TANK** (record 13,
  primary 14) and the **rocket turret** (building records 29–31, name
  10, kind 16, primary 12). Their homing missiles hit at the ornithopter's
  altitude (doc 18 §4.3).
- Units cannot be ordered to attack it: `FUN_004a5270` (unit under a
  pixel) skips kinds 2, 5, 8, 9 and 10, so a click on an ornithopter is a
  click on the cell under it.
- Sandworms ignore it (doc 17).
- Death (`FUN_0049cde0`, doc 14 §4.6): EX_WHITEN1 on the ground, the
  EX_BANG14 sound, EX_BANG14 at its altitude. Both explosions get flag
  0x1000 and one flight step of motion. Then hp = 0 and state 0x11: it
  flies straight on for 10 ticks and disappears. Its unfallen bombs keep
  falling.

## 8. Selection, radar, shroud

- **Not selectable**: every selection test needs `flags & 0x140 == 0`
  (doc 10 §2).
- **Box-select quirk** (`FUN_004a5ae0`, verified): the first loop, "is
  any unit inside?", tests only the state (not 8 without +0x4f, not 0x11)
  and the position, **not** flags 0x140. A box that contains only own
  aircraft (ornithopters, carryalls) still returns true and runs
  `FUN_004a5790` (deselect all). The second loop then selects nothing.
  The select voice `FUN_0049f240(0)` finds no selected unit and stays
  silent. Net effect: such a box **clears the selection**. The same
  applies to the "select all on screen" call at `0x446046`
  (`FUN_004a5ae0` with the viewport; the drag box is `0x444d2e`). Our `World::box_select` filters
  with `selectable()` first, so it differs.
- **Radar** (doc 11, `FUN_00426000`): aircraft are skipped except kind 9.
  Ornithopters of **every** player are drawn in the owner's colour at
  their "to" cell, if the cell's shroud byte is 0. **Corrected:** the
  exe tests `to.x < W` and `to.y < H` (as ints of the unsigned bytes)
  before it reads the shroud byte, in both the 16-bit and the 8-bit
  branch, so an ornithopter whose "to" cell is off the map is simply not
  plotted. No out-of-bounds read; our radar does the same.
- **Shroud**: view distance 0, so an ornithopter reveals nothing
  (`FUN_0044cff0` needs view ≠ 0). The player does not see the
  target area unless it is already revealed.

## 9. Drawing (verified, doc 14 §5.5)

- Not in the sorted ground list (flag 0x40). Pass 0 part (a) draws the
  **shadow**: the same body image at the ground point (x − off_x,
  y − off_y), shade blit.
- Body: altitude 60 ≥ 20 px, so it is drawn in **pass 1** (after the
  high explosions), at y − 60 px. Image
  `0x71aca0[((17 · 35) + wing frame +0x1e) · 32 + facing +0x54]`, house
  colours 240..255.
- No group digit (+0x28 = 0xff).
- Wing frame +0x1e only changes in FlightStep (§5.1). A dying
  ornithopter keeps its last frame.

## 10. AI airstrikes (`FUN_00422270` DoSpecialWeapons, verified)

DoAI calls it after DoProtection and before DoDefence (doc 23 §1.1):

```
DoSpecialWeapons(ai):                       // ai+0 = side
    if ai.+0x1e (BESERK) != 0: return
    if (tick + side) & 0x1f != 0: return     // every 32 ticks, phased by side
    if tick < ai.+0x1d18 (SpecialWeaponDelay): return
    P = player(side)
    best = GetBestSideToAttack(ai)            // FUN_0041cdf0: may run RecalculateSideToAttack (rand)
    switch ((side + tick) >> 5) & 7:
      case 0: thumper (kind 3) group...       // not covered here
      case 1: saboteur (kind 4)   case 2: Fremen (kind 12)
      case 3:                                  // airstrike, so once per 256 ticks
        if !ItemReady(ai, 9, &slot): return    // FUN_00422730
        if P.slot[slot].used (+0x11) != 0: return
        tside = GetBestSideToAttack(ai)        // second call: counter −1 again
        t = GetBestTarget(tside, 9)            // FUN_00422800
        if t == 0: return
        if t is a unit (+0x90 == 1):     px = t.+34, py = t.+38      // centre
        elif t is a building (== 2):     px = t.+00, py = t.+04      // bottom-left corner
        else: px = py = 0
        PostSpecialOrder(side, 9, trunc0((px >> 16) / 32), trunc0((py >> 16) / 32))
        P.slot[slot].used = 1
      case 4: Death Hand (kind 10)
```

**ItemReady** (`FUN_00422730`): the kind must be in the side's unit build
list. Then the **last** slot whose item has that kind is taken. It is
ready when its progress > 0x59FF.

**GetBestTarget** (`FUN_00422800`, kind 9: building chance 10, unit
chance 5, out of 128):

```
if tside == 0xff: return 0
for b in player(tside).buildings (list +0x10):
    if brec[b.+34].kind != 14 (wall) and (rand() & 0x7f) < 10: return b   // walls: no rand()
for u in player(tside).units (list +8):
    if (rand() & 0x7f) < 5 and !(u.flags & 0x400140) and urec[u].kind != 5: return u
                                                   // rand() first, for every unit tried
return 0
```

- **Building target quirk:** building +0x04 is the **bottom** edge y
  (doc 12: y = (cy·32 + height_px) << 16). So the airstrike cell is the
  column of the building's left edge, one row **below** the building. The
  bombs' 64 px radius and the 4-cell drop zone still reach it.
- One `rand()` per non-wall building tried, then one per unit tried (in
  list order), until a pick. Plus whatever GetBestSideToAttack draws.
  Copy this exactly for RNG parity.
- The AI's ornithopters start from the AI's own home view (ProcessOrder
  uses the ordering side's player).
- Production is the same as the player's (§2). AutoStart runs for every
  side, so any AI with the item in its unit build list charges
  airstrikes. That means an Atreides-house AI with an upgraded
  AT_HIGH_TECH_FACTORY and tech ≥ 6. UnitBuildPriority does not matter
  for specials. **The 1.06 missions** (survey of every campaign mission
  with our port of the set-up and the build lists, golden test in
  `tests/sim/test_aircraft_specials.cpp`): the Atreides AI (side 0) owns
  an AT_HIGH_TECH_FACTORY at the start in H7V1, H8V1, H9V1, H9V2, O6V2,
  O7V1, O8V1, O9V1 and O9V2. O6V2 has tech 5, so it never gets the
  airstrike. In the other eight the factory needs its upgrade first
  (AI UpgradeBuildings); in our runs the airstrike entered the build
  list at about tick 3300 (H7V1) or 3800 (the rest) and the first AI
  strike flew at about tick 15500 in H9V1, H9V2, O7V1, O9V1 and O9V2.
  No A mission has one.
- **RNG note for everyone:** the top-level `GetBestSideToAttack` call
  runs for every AI side every 32 ticks once SpecialWeaponDelay has
  passed, even without specials. It decrements the side countdown
  (+0x18) and recalculates (with `rand()` per side) when it reaches 0.
  Ported (`World::ai_special_weapons`, every case).

## 11. Saved state

Ornithopter: x, y, altitude, speed, facing +0x54, desired heading +0x55,
turn counter +0x1a, wing frame +0x1e, wing direction +0x24, wing timer
+0x25, bombs +0x29, dest +0x48/49, cells +0x4a..+0x4d, flags (0x40,
0x800, 0x4000, 0x10000, 0x400), state, countdown +0x2f. Slot: item,
progress, hold, **used (+0x11)**. Bombs: the normal bullet fields
(height, vz, frame, firer).

## 12. Open points

Settled on 2026-10-16:

- The number of passes and the spread follow from the ported step code
  (§5.2); nothing to copy beyond it.
- A drag release in mode 4 selects and cancels the mode (§3.3); ported.
- An off-map ornithopter is not plotted on the radar (§8, corrected).
- The AI sides that can airstrike in 1.06 (§10).
- HomeView's local-camera fallback is unreachable from order 0x30
  (§3.4).
- The score screen does not count the 3 ornithopters: CreateUnit adds
  them to `+0x24780` and `+0x24788[20]`, but those built counters are
  read only by the dead debug box and the Westwood Online upload
  (doc 60, doc 56 §3), not by any score page. No visible effect.
- Not covered: DoSpecialWeapons cases 0, 1, 2 and 4 (thumper group,
  saboteur, Fremen, Death Hand), and the Death Hand flight
  (`FUN_00494cd0`).

## 13. Checklist: our code

| # | Finding | Where | Status |
|---|---|---|---|
| 1 | Unit fields for wings: +0x1e `frame` exists; +0x24 wing direction and +0x25 wing timer (`timer` exists; +0x24 is `prone`, infantry-only) and +0x29 bombs (`load`) | `sim/world.hpp` `Unit`, `save_state.cpp` | reuse or add named fields; save them |
| 2 | Auto-start of specials every tick (`FUN_004466e0`, §2.1), including the hold-resume quirk and `*_BUILDING` | `sim/economy.cpp` (new, in the tick after the script / blooms) | done (parity row "Airstrike (ornithopter)") |
| 3 | Special progress at F = 100 × difficulty, no spawn | `World::tick_production` | done (f = 100, then × speed) |
| 4 | `AirStrikeReady` text for the local player | `tick_production` (TODO at the ready branch) | done (parity row "Airstrike (ornithopter)") |
| 5 | Slot "used" byte +0x11 | `sim/player.hpp` `ProductionSlot` | done (parity row "Airstrike (ornithopter)") |
| 6 | Sidebar click on a ready, unused airstrike → mode 4 | `app/sidebar.cpp` `left_up` (TODO "specials") | done (parity row "Airstrike (ornithopter)") |
| 7 | Mode 4: cursor 9 in the playfield; right button down cancels | `app/game_screen.cpp` cursor choice / `sidebar.hpp` `mode` | done (parity row "Airstrike (ornithopter)") |
| 8 | Target click: in-playfield test, mark used, order 0x30 with (cam + mouse) >> 5, mode 0, no shroud test | `app/game_screen.cpp` left-up handler | done (parity row "Airstrike (ornithopter)") |
| 9 | Order 0x30 handler: clear every ready slot of the kind, then kind 9 → HomeView + LaunchAirstrike | `sim/` (new `order_special`) | done (parity row "Airstrike (ornithopter)") |
| 10 | HomeView as a sim function (camera point of a side's base), shared with the H key | `World::home_view`, used by the start camera, H and order 0x30 | done (2026-10-16) |
| 11 | LaunchAirstrike: 3 × CreateUnit(20, 5,5 → 5,5), EdgePoint(k = 3i), cell-corner pos, altitude, bombs 12, flags 0x10800, state 7, facings | `sim/aircraft.cpp` (new) | done (parity row "Airstrike (ornithopter)") |
| 12 | CreateUnit for kinds 8 / 9 / 15: flag 0x40 and altitude 60 px; cells (5, 5); AI state 3 refused | `sim/placement.cpp` `create_unit` (TODO "aircraft starts") | done (parity row "Airstrike (ornithopter)") |
| 13 | EdgePoint exact rules: `x > W/2 ? W − x : x`, top/bottom on ties, `<= H/2` → top | `World::edge_point` | done (parity row "Airstrike (ornithopter)") |
| 14 | FlightStep: wings for kinds 9 / 15 (not in state 0x11, not mode 1) | `World::flight_step` (TODO at line 66) | done (parity row "Airstrike (ornithopter)") |
| 15 | FlightStep: kind 9 bombing (d2 < 32 straight, d2 < 16 and global tick & 7 = 0 drop, bombs = 0 → 0x4000 and straight), pre-move pixel | `World::flight_step` | done (parity row "Airstrike (ornithopter)") |
| 16 | No centring for kind 9 | `World::flight_step` | done |
| 17 | Speed: flag 0x10000 keeps the record speed | `World::flight_step` | done |
| 18 | Leaving test on unsigned "to" cells, 10-tick state 0x11 flight, removal | `flight_step` / `tick_aircraft_state` case 0x11 | done (check that `to_x` is compared as a byte) |
| 19 | State 7 for kind 9 calls FlightStep; state 2 has no kind 9 branch | `World::tick_aircraft_state` | done (state 7 is generic) |
| 20 | Bomb: AddBullet with flag 0x100, from = to, height = firer altitude; falling, animated, additive EX_BANG14 | `sim/combat.cpp` `add_bullet` / `tick_bullets` | done for debris; check that `firer` resolves to the ornithopter and that frame cycling (0x80) runs for falling bullets |
| 21 | DamageTiles: aircraft only hit when height > 6 px | `combat.cpp` (≈ l. 527–592) | done |
| 22 | Air pass: missile tank and rocket turret target ornithopters | `World::target_unit_in_range` | done |
| 23 | Click on an ornithopter = empty cell (`FUN_004a5270` skips kind 9) | `World::unit_at_pixel` / `click_unit` | done: kind 9 and flag 0x140 skipped (2026-10-16) |
| 24 | Worms ignore kind 9 | `sim/worm.cpp` l. 58 | done |
| 25 | Death effects at altitude, one-step move | `combat.cpp` kill path | done (aircraft generic) |
| 26 | Radar shows kind 9 aircraft | `app/sidebar.cpp` l. 559 | done |
| 27 | Box select deselects all when only aircraft are inside (quirk §8) | `World::box_select` | done (parity row "Airstrike (ornithopter)") |
| 28 | Draw: shadow pass 0, body pass 1 at y − 60, frame = wing frame | `GameScreen` aircraft draw (≈ l. 690–730) | done (uses `u.frame`; needs #14 to animate) |
| 29 | No shroud reveal (view 0) | `World::reveal_units` | done (view == 0 skipped) |
| 30 | Damage smoke (+0x27) also for aircraft (top of UnitUpdate) | `World::tick`, before the aircraft branch | done (doc 43 §2, `0x497e81`) |
| 31 | AI DoSpecialWeapons case 3, ItemReady, GetBestTarget (rand order), and the top-level GetBestSideToAttack call | `sim/ai.cpp` (TODO in `tick_ai`) | done (parity row "Airstrike (ornithopter)") |
