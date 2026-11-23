# 14: Aircraft and explosion effects (1.06)

Covers how aircraft fly (carryall, ornithopter, frigate), the carryall's
descend, drop and climb, how aircraft leave the map, how aircraft are
drawn, and the explosion / effect objects (fields, animation, removal,
drawing, blits and sounds). Decompiles come from the local GhidraMCP
server (`re/ghidra_http.py`) and `re/disasm.py` for the float code.

Main functions:

| Address | Role |
|---|---|
| `FUN_00494fd0` | aircraft flight step (one tick) |
| `FUN_004954b0` | aircraft turn one step |
| `FUN_0044b090` | builds the flight step tables `0x6e9840` / `0x704a98` |
| `FUN_0049f330` / `FUN_0049f370` | clear / set cell flag 0x800000 (aircraft over the cell) |
| `FUN_0049e3e0` | carryall / frigate drops the delivered units |
| `FUN_004a60e0` | spiral search for a free drop cell |
| `FUN_0049e6b0` | point off the nearest map edge, and the facing from it |
| `FUN_0049e770` | airstrike: creates 3 ornithopters |
| `FUN_0049cde0` | unit killed (death effects, then state 0x11) |
| `FUN_00458e90` | per-tick model update (units, buildings, bullets, explosions) |
| `FUN_00456df0` | create an explosion object |
| `FUN_0049dd80` | explosion update (one tick) |
| `FUN_00428ca0` | in-game world draw (calls the passes below) |
| `FUN_00428e30` | objects draw: sorted list, then the explosion and aircraft passes |
| `FUN_004282f0` | explosion draw pass |
| `FUN_0042a5d0` | aircraft draw pass (shadows, carried units, aircraft) |
| `FUN_00428220` / `FUN_00456d50` | UI click puffs (draw / add) |

"Verified" means read from the code or the data. "Inferred" is marked.
Ticks are game ticks. State numbers are the values in unit +0x10. Pixel
values are 16.16 fixed point unless written as "px".

## Corrections to earlier docs

- **doc 09, §2 unit fields:**
  - +0x2f is the **death countdown**: 10 for vehicles and aircraft, 0x32
    for infantry. In state 0x11 it drops by 1 per tick; an update that
    finds it at 0 returns 0 and the unit is removed (§3.3).
  - +0x2b ("LF" in the debug dump at `0x4e385c`) grows by 1 every 8th
    tick (tick & 7 = index & 7, saturating at 255) while unit flag 2 is
    clear. Flag 2 resets it to 0 and is cleared (`0x49808f`, after the
    stealth machine and the +0x2a counter, before the state code). It
    starts at 0xff. Flag 2 = "fired": the fire code `FUN_00495b90` sets
    it right after AddBullet (`0x495fb4`), and also writes +0x2b = 0
    directly at the weapon choice (`0x495e41`); the only other setter is
    the text loader. So a shot gives 0 in the firing update and 0 again
    in the next one, with no count in between. The carryall drop also
    uses it (§2.3).
  - +0x2c ("LM") grows every 8th tick while a ground unit stays in the
    same cell, else it is 0. Aircraft skip this, so a delivery carryall
    keeps its delivery entry index there.
  - For aircraft, +0x55 (turret facing on ground units) is the **desired
    heading**. +0x54 is the body facing used for drawing.
  - Ornithopter: +0x1e = wing frame 0–2, +0x24 = wing direction,
    +0x25 = wing timer, +0x29 = bombs left (§1.6).
  - Debug names (`0x4e388c`): +0x1e CAF (current animation frame),
    +0x20 CAT (animation type), +0x24 L, +0x25 CAD (animation delay).
- **doc 09, §6:** cell flag **0x800000** marks the cell under an aircraft
  (`FUN_0049f370`). It is moved every flight step.
- **doc 08, other blocks:** `0x781e58` is the **explosion attribute**
  table, 64 × 8 bytes: +0 own index, +1 blink mask, +4 sound sample
  (i32, −1 none) (§4.2). `0x6e5750` is the **explosion draw flags**,
  64 × u32 (§4.3).
- **doc 12, §11 "draw flag 4 (`FUN_004349a0`)":** `FUN_004349a0` is the
  **additive** blit (§5.2). The active overlay of records with flag
  0x800000 is drawn additive.
- **doc 13, §5.3 drop counter:** answered in §2.3.
- **doc 13, §5.3 empty carryall:** it is removed off the map, but it does
  not always leave by the nearest edge (§3.2).
- **doc 13, §8.2 sounds:** EX_HARV, EX_HARVDUST0–7 and EX_REF have no
  sound (attribute sound −1), and the harvester code plays none (§4.2).

## 1. Flight step (`FUN_00494fd0(u, side, index, mode)`, verified)

Aircraft are units with flag 0x40. Creation (`FUN_00455870`) gives the
carryall (kind 2), frigate (8), ornithopter (9) and kind 15 flag 0x40 and
altitude +0x3c = 0x3c0000 (60 px). Death Hand (kind 10) gets flags
0x20040 and uses a different step (`FUN_00494cd0`, not covered here).

Callers:

- state 7 (move), aircraft without 0x20000: `FlightStep(mode 0)`. A
  return of 0 means "arrived": state 2.
- idle carryall (state 2, no request): `FlightStep(mode 1)` (§1.7).
- state 0x11 (dying): `FlightStep(mode 0)` (§3.3).

Kinds 2, 8, 9 and 10 may only enter states 2, 7, 0x11, 0x14, 0x15 and
0x16 (`FUN_0049deb0` refuses the rest).

### 1.1 Order of one step

```
speed = u.+00                                  // current speed
if state == 0x11:      straight = true         // keep speed, no turning
elif mode == 1:        straight = false; speed = rec.speed
else:
    wing animation (kind 9 or 15, §1.6)
    if (to.x >= map_w + 4 or to.y >= map_h + 4)       // to = +4c/+4d, u8
       and flags & 0x800 and flags & 0x4000:
        SetState(0x11); return 1                      // §3.2
    T = (dest * 32 + 16) px for x and y               // dest = +48/+49, u8
    if kind != 9 and cell(pos) == dest:               // centring, §1.4
        per axis: if (p >> 16) / 2 < (T >> 16) / 2: p += 1 px
                  elif >:                          p -= 1 px
        return 1 if anything moved, else 0
    d2 = (dest.x - cellx)^2 + (dest.y - celly)^2      // cells, of pos
    speed = slow-down rule (§1.3)
    straight = false
    kind 9: bombing run (§1.6), may set straight
    if cellx(pos) == dest.x or celly(pos) == dest.y: flags &= ~0x400
    +55 = atan(x - T.x, T.y - y) >> 7                 // §1.2
    flags |= 0x400
u.+00 = speed
if !straight: TurnAir(u, +55)                         // §1.2
i = facing(+54) * 512 + ((speed & 0x7ffffff) >> 11)
x += DX[i]
y -= DY[i]
clear 0x800000 on the old "to" cell
cur = to = cell(pos)                                  // +4a/+4b and +4c/+4d
set 0x800000 on that cell (only if inside the map)
+4f = 0
return 1
```

- `cell(p)` is `(p >> 16) / 32`, both divisions truncating toward zero,
  stored as a byte. So −31…31 px is cell 0, and −32 px is cell −1 = 255.
- `dest` is read as an **unsigned** byte (`movzx`, `0x4950ac`). A
  negative dest such as −4 is cell 252 (§3.2).
- The 0x400 flag is set every step and cleared when one axis is aligned
  with the dest cell. Nothing reads it: a scan of every `test` / `and` of
  bit 0x400 on unit +0x08 (dword, byte +0x09 and loaded registers) finds
  only the write at `0x4953bf` and clearing masks; the `test ah, 4`
  sites read cell or building flags. It only shows in saves and debug
  dumps. Keep the bit.
- `FUN_0049f330` / `FUN_0049f370` skip cells outside the map
  (x ≥ map_w or y ≥ map_h, as unsigned bytes).

### 1.2 Heading and turning

- **Desired heading** +0x55 = `FUN_0049e160(dx, dy) >> 7`, with
  dx = (x − T.x) >> 16 and dy = (T.y − y) >> 16 (px, truncated toward
  zero). The angle is 0..0xfff, so this gives 0..31. It **truncates**:
  there is no +0x40 rounding as in `Facing8` (doc 09 §5.3).
- **Turn** (`FUN_004954b0(want, u)`): if facing = want, nothing. If
  +0x1a = 0: +0x1a = rec+3, then step one facing toward `want` the short
  way: cw = (want − f + 32) % 32, ccw = (f − want + 32) % 32; if cw < ccw
  f += 1, else f −= 1 (so a tie at 16 turns −1). Else +0x1a −= 1.
- So one facing step every rec+3 + 1 ticks:

  | Unit | rec+3 | Ticks per step | Speed (rec+8) |
  |---|---:|---:|---|
  | CARRYALL | 1 | 2 | 0x40000 = 4 px/tick |
  | ORNITHOPTER | 3 | 4 | 0x6c000 = 6.75 px/tick |
  | FRIGATE | 4 | 5 | 0x48000 = 4.5 px/tick |

- The aircraft moves along its **current** facing, not the desired one.
  So it flies curves while it turns. There is no acceleration (see §1.3).

### 1.3 Speed (verified, float from disassembly at `0x49528e`)

```
if !(flags & 0x10000) and (flags & 0xb00) and d2 <= 16:
    speed = (speed <= 0x10000) ? 0x10000 : trunc(double(speed) * 0.95)
else:
    speed = rec.speed
```

- 0xb00 = 0x800 (delivery), 0x200 (ferry job), 0x100 (loaded). So a
  carryall at work slows down within 4 cells of its dest by 5 % per tick,
  down to 1 px/tick. 0.95 is a double at `0x4d7050`; the speed is loaded
  as a 64-bit integer. Compute in double and truncate.
- Note the order: a speed just above 0x10000 can drop below it for one
  tick, then it is set to 0x10000.
- In all other cases the speed **snaps** to the record speed. There is no
  ramp up.
- Airstrike ornithopters have flag 0x10000 and never slow down.

### 1.4 Arrival

For every kind but the ornithopter: once the aircraft's pixel position is
inside the dest cell, the step does not use the tables. It moves each axis
by 1 px toward the cell centre per tick and does not turn or update the
cells. It stops when (p >> 16) / 2 equals (centre px) / 2, i.e. at
centre px or centre px + 1. Then it returns 0 and state 7 ends in state 2.

A carryall at 4 px/tick cannot skip a 32 px cell, so it always lands in
the dest cell.

### 1.5 Step tables (`FUN_0044b090`, verified)

`DX` at `0x6e9840` and `DY` at `0x704a98`, int32, index
`facing · 512 + s` with s = 0..511 (speed in steps of 0x800 = 1/32 px):

```
angle = facing * 6.283185308 * 0.03125      // both doubles, 0x4c93c0 / 0x4c93c8
DX[facing][s] = trunc(sin(angle) * (s * 0x800))
DY[facing][s] = trunc(cos(angle) * (s * 0x800))
```

Facing 0 moves north (y −= DY), facing 8 east. The constant is not
exactly 2π. Computing these in double gives the same integers: the only
products near an integer are the near-zero ones (cos at facing 8,
sin at facing 16), which truncate to 0 either way (checked for all
32 × 512 entries).

### 1.6 Ornithopter (kind 9, verified)

- **Wings** (also kind 15): when +0x25 = 0, +0x25 = 4 and:
  - +0x24 = 0: if +0x1e < 2, +0x1e += 1; else +0x24 = 1 and +0x25 = 20;
  - +0x24 = 1: if +0x1e = 0, +0x24 = 0; else +0x1e −= 1.

  Else +0x25 −= 1. So frames go 0 → 1 → 2 every 5 ticks, frame 2 holds
  for 26 ticks, then 2 → 1 → 0; one cycle is 46 ticks.
- **Bombing** (after the slow-down rule): if d2 < 32 cells²: it flies
  straight (no turn). Within that, if d2 < 16, global tick & 7 = 0 and
  bombs +0x29 ≠ 0: fire its primary bullet (rec+0x0c, ORNIBOMB) with
  `FUN_00456a30` from its own position to its own position, bombs −= 1.
- Bombs = 0: flies straight and sets 0x4000 (leaving).
- It never centres on the dest, so after a pass it turns back for
  another one until the bombs are gone (inferred from the step code).
- **Airstrike** (`FUN_0049e770`, ProcessOrder case for kind 9):
  3 ornithopters. Each starts 4 + 3·i cells past the map edge nearest
  to a point from `FUN_0046cf10` (inferred: the palace), with dest =
  the target cell, altitude 60 px, bombs 12, flags |= 0x10800, state 7,
  body and desired facing from `FUN_0049e6b0`.

### 1.7 Idle carryall (state 2, verified)

A carryall with none of 0x800 / 0x100 / 0x200 asks `FUN_0046ce00` for a
waiting ferry request. If none: every 8th tick (tick & 7 = index & 7) the
desired heading +0x55 += 1, then `FlightStep(mode 1)`. Mode 1 uses the
record speed, turns toward +0x55 and moves. It has no edge, arrival or
removal test. So an idle carryall flies a slow circle: 32 heading steps ×
8 ticks = 256 ticks per lap, about 5 cells radius at 4 px/tick
(inferred from the numbers).

## 2. Carryall descend, drop and climb

### 2.1 Getting to state 0x15 (state 2 code, verified)

For kind 2 and 8 in state 2:

- **Delivery** (0x800 set): if 0x100 set and 0x4000 clear: state 0x15
  when the "to" cell = dest, else state 7 (fly there).
  If 0x200 is clear (empty, leaving): dest = `FUN_0049e6b0` of the "to"
  cell, state 7 (§3.2).
- **Ferry** (0x800 clear): loaded (0x100): state 0x15 at the request's
  "to" cell, else clear 0x200 and fly there (state 7). Job but not loaded
  (0x200): state 0x15.

### 2.2 State 0x15, delivery branch (flag 0x800, verified)

1. Frigate (kind 8): if the player has no Starport (`player+0x260a6` =
   0xffff) and 0x4000 is clear: flags = (flags & ~0x200) | 0x4000, speed =
   record speed, state 0x16. If a Starport exists and the "to" cell is not
   its (x/32 + 1, y/32 − 2): state 0x16 with that cell as dest and the
   record speed.
2. If altitude = 10 px exactly, or the kind is not 2, or the dest is
   passable for the carryall (`FUN_0045d990`, mode 0): go on. Else
   `FUN_004a60e0(&dest, 8)` (§2.4) and state 0x16.
3. Altitude > 10 px: altitude −= 1 px. From 60 px this is **50 ticks**.
4. Altitude ≤ 10 px: `FUN_0049e3e0` (§2.3). When it returns 0 (done):
   - frigate: `player+0x262a5` = 0 and `FUN_0046eaf0`;
   - state 0x16, flags = (flags & ~0x300) | 0x4000;
   - a per-player stats entry is updated (`0x4f4118` table; skip).

The carryall does not move sideways in 0x15. It descends where it
stopped (§1.4).

### 2.3 `FUN_0049e3e0(u, side)`: one drop step (verified)

Entry E = `player+0x262ec + u.+0x2c · 0x38` (doc 13 §5.1).

1. Acts only when tick % 4 = 0 (type 1, carryall) or tick % 8 = 0
   (type 2, frigate). Global tick, not staggered. Else return 1.
2. **Blocked test** on the "to" cell: type 1 tests `flags & 0x3f8`
   (unit or building); type 2 tests `flags & 0x3e8` (units only).
   If blocked:
   - +0x2b += 1 (byte, wraps);
   - type 1 and +0x2b > 40: state 0x16, return 1 (climb, entry kept);
   - +0x2b < 81: return 1;
   - else entry inactive (+3 = 0), return 0. In practice only the frigate
     gets here, because a carryall climbs at 41.
3. Not blocked: +0x2b = 0. Skip records that are 0xfe. 0xff: entry
   inactive, return 0.
4. Easy rule (`0x4e8bf0` = 0): odd record positions are skipped when the
   player is not allied to the local player (doc 13).
5. Create the unit at the "to" cell (`FUN_00455870`), body facing = the
   carryall's +0x54. Harvester / other unit set-up as doc 13 §5.3. Type 1
   then sends it to a free 3×3 cell (`FUN_0049e290`) with state 7. The new
   unit gets flags |= 0x40000, or 0xc0000 when it was the last record,
   and +0x2e = the entry index. 0x40000 = "delivered, not grouped yet":
   its only reader is the AI's GroupNewUnits (`0x41c5c5`), which clears
   it and puts the unit into the group of entry +0x2e (doc 23 §3.4).
   0x80000 ("last record") has no reader on units (the `test 0x80000`
   sites at `0x49b98a` / `0x49b9db` read building flags +0x28).
6. Mark the record 0xfe, return 1.

**The +0x2b counter (doc 13's question):** it is shared with the common update
(see the corrections). That adds 1 every 8th tick, saturating at 255,
and a carryall never resets it there because it never fires (inferred).
It starts at 0xff, so it sits at 0xff during the first flight. At the
first drop check it becomes 0: a clear check resets it, and a blocked
check wraps 0xff + 1 to 0. From then on it gains 1 per blocked check
(every 4 ticks) plus 1 per 8 ticks, about 3 per 8 ticks. So a carryall
waits about 110 ticks over a blocked cell before it climbs. After a climb
the counter is still above 40 and keeps growing, so on the next landing
the first blocked check makes it climb again at once; a clear check
resets it. All of this follows from the code: +0x2b = 0xff at creation
(`0x4559dc`), the common counter (`0x49808f`, only flag 2 resets it and
a carryall never fires) and the drop step's own writes (`0x49e4ad`
+1, `0x49e4e5` = 0) are the only writers. Nothing else is needed from
the reference game.

**Free harvester timeline:** the drop on the dock cell happens on the
first clear check after landing. On the next checks the harvester itself
blocks the cell until it drives to its 3×3 cell. Only then does the
carryall read 0xff and finish. So the carryall waits for the harvester to
drive off.

### 2.4 `FUN_004a60e0(&x, &y, r)`: free cell spiral (verified)

Walks a square spiral from (x, y): west 1, south 1, east 2, north 2,
west 3, south 3, … (steps `0x4eb2d8`: dx −1, 0, 1, 0; `0x4eb2dc`: dy 0,
1, 0, −1; the run grows by `0x4eb2e0` = 0, 1, 0, 1). It stops when the run
length reaches 2r + 2 (18 for r = 8). The first cell with
0 ≤ x < map_w − 1, 0 ≤ y < map_h − 1 and `flags & 0x63f8 = 0x6000`
(vehicle and infantry passable, no unit, no building) is written back.
None: the input is unchanged and it returns 0.

In the ferry branch the new cell is also written into the request's "to"
cell.

### 2.5 State 0x16: climb (verified)

Altitude < 60 px: altitude += 1 px per tick. At 60 px: state 2. State 2
then decides the next step (§2.1).

### 2.6 State 0x15, ferry branch (flag 0x800 clear, verified)

Summary for doc 13 §6 (the target unit is +0x0c, its saved state is in
carryall +0x26, the request slot in +0x29):

- Request done (slot state 3) and not loaded: clear 0x200, climb.
- Loaded and the drop cell is not passable: `FUN_004a60e0(…, 8)`, store
  the cell in the request, climb.
- At exactly 60 px and not loaded: first turn the body to the unit's body
  facing (`FUN_004954b0`), one step per call; descend only when equal.
- Above 10 px: altitude −= 1 px.
- At ≤ 10 px, not loaded: if the unit is not carried yet and is in state
  0x12: free its cell (vehicle bit; infantry owner bits as in doc 09 §6),
  carryall flags = (flags & ~0x4000000) | 0x100, unit unselected, unit
  flags |= 0x100, unit state 0x13, slot state 4, climb. Else flags =
  (flags & ~0x200) | 0x4000 and climb.
- At ≤ 10 px, loaded: unit flags &= ~0x100, unit altitude 0, unit state =
  the saved state, mark the unit's cell (owner | 8, plus the infantry bits
  for infantry), slot state 0, carryall clears 0x100, climb.
- While loaded, the update copies the carryall's cells, x, y, facings and
  altitude − 10 px into the carried unit every tick (tail of
  `FUN_00497b90`).

Flag 0x4000000 is never set on units in 1.06 and 0x4000 without 0x800
has no effect: answered in doc 33 §0.

## 3. Leaving the map and removal

### 3.1 Entry from the edge (verified)

`FUN_0049e6b0(&x, &y, k)` (doc 13 §5.2): with n = 4 + k, it moves the
point n cells past the nearer of top/bottom (if not farther than
left/right) or left/right. It writes the new coordinate as a 16-bit value
and returns the facing toward the map: top 16, bottom 0, left 8, right 24.
Creation of any carryall (`FUN_00455870`) already places it this way and
sends it (state 7) to its creation cell. The delivery code then overrides
position, dest and facings (doc 13 §5.2). Position = edge cell × 32 px
(the cell corner), as a signed value, so negative positions work.

### 3.2 Leaving (verified from code)

- A delivery carryall that is done has 0x800 and 0x4000. In state 2 it
  gets dest = `FUN_0049e6b0` of its "to" cell and state 7.
- The flight step tests the "to" cell **before** moving: x ≥ map_w + 4
  or y ≥ map_h + 4, as unsigned bytes. The `< −4` tests in the code can
  never be true, because the fields are unsigned.
- **Edge quirk.** For a left or top edge the dest is −4, stored as the
  byte 252. The step reads dest unsigned, so the carryall heads for cell
  252 on that axis: it flies **right / down across the map** instead of
  left / up. It is removed when its cell reaches map_w + 4 (or map_h + 4)
  on the far side. For a right or bottom edge it flies to the nearest edge
  as intended. Verified in the listing: the state 2 code stores only the
  low byte of EdgePoint's 16-bit result (`0x49823a` / `0x49823d`, so −4
  is 0xfc) and the flight step loads dest with `xor eax, eax; mov al`
  (`0x4950aa`..`0x4950af`, zero-extended). Vanilla behaviour, kept.
- Also from the byte compare: an aircraft with 0x800 | 0x4000 whose
  position goes 32 px past the left or top edge has cell 255 and is
  removed there.
- Ornithopters (flags 0x10800) leave the same way once their bombs are
  gone (0x4000); they fly straight on in their current facing.
- Frigates leave the same way after their drop (0x4000).

### 3.3 Removal (state 0x11, verified)

`SetState(0x11)` starts the death countdown. Each tick in state 0x11
(switch case 0xF, `0x499de2`; corrected 2026-10-16, docs/re/37 §3.5):

1. the Grenadier test (`+0x2f == 0x2E`, GRENDEATH, doc 37 §3.5);
2. **if +0x2f is 0** (`0x499e31`): an aircraft clears 0x800000 on its
   cell, a ground unit frees its cells, and the update returns 0;
3. else +0x2f −= 1 (`0x49a09a`), and then an aircraft (0x40) still runs
   `FlightStep(mode 0)` (`FUN_00494fd0`; `FUN_00494cd0` for 0x20000):
   straight on at its current speed, no turning.

The zero test comes before the decrement, so a unit spends **+0x2f + 1**
updates in state 0x11 (11 for a vehicle, 51 for infantry) and an aircraft
makes +0x2f flight steps. When the update returns 0, `FUN_00458e90`
removes the unit:

- harvester: `player+0x245b8` −= 1;
- carryall without 0x800: `player+0x245c0` −= 1 (owned carryalls);
- carryall: `player+0x2688d` −= 1;
- per-record count `player+0x24628[rec]` −= 1; state = 1; free the object.

So a leaving aircraft flies **10 more ticks** after the edge test fires
and disappears in the 11th. No explosion or sound: the state 0x11 code spawns none
for aircraft. A shot-down aircraft gets its effects in `FUN_0049cde0`
(§4.6), then flies on for the same 10 ticks.

A shot-down loaded carryall also kills its cargo (`FUN_0049cde0` recurses
on +0x0c when flags & 0x140 = 0x140).

## 4. Explosion / effect objects

### 4.1 Object (`FUN_00456df0`, verified)

Explosions live in each player's object list at `player+0x20` (head) /
`+0x24` (tail), from the shared object pool (`FUN_0046bd40`), list type 4.
New ones are **appended** (`FUN_0046bd60`). The checksum dump covers
0x38 bytes.

| Off | Type | Meaning |
|---|---|---|
| +0x00 | u8 | explosion type (0..59; > 63 is refused) |
| +0x01 | u8 | frame |
| +0x02 | u8 | tick counter (blink step in mode 0x400) |
| +0x04 | u32 | flags = `0x6e5750[type]` **XOR** the caller's flags |
| +0x08 | u16 | attached unit index (same owner), 0xffff none |
| +0x0c / +0x10 | i32 | x / y (pixel << 16) |
| +0x14 | i32 | height (16.16), drawn as y − height |
| +0x30 / +0x34 | i32 | one-time move dx / dy (flag 0x1000) |
| +0x7c… | | pool links; +0x8c index; +0x90 = 4 |

`FUN_00456df0(owner, type, x px, y px, height, flags, delay, dx, dy)`:
frame 0, counter = delay if the caller's flags have 0x20 else 0, +0x08 =
0xffff, dx/dy stored only when the final flags have 0x1000. Returns the
object index, or 0xffff when the pool is full.

### 4.2 Attribute table `0x781e58` (64 × 8 bytes, verified)

| Off | Meaning |
|---|---|
| +0 | the explosion's own index |
| +1 | blink mask for mode 0x400 (bit k = visible at step k) |
| +2, +3 | 0 |
| +4 | i32 sound sample, −1 none |

The sound is **not** played by the explosion. The code that spawns it
reads +4 and calls `FUN_0046f540(sample, cell x, cell y)` (doc 12 §9),
e.g. unit death (`FUN_0049cde0`) and bullets (`FUN_0049be60`). Sample
ids index the samples.uib table (doc 12 §9). Types with a sound:

| Type | Name | Sample |
|---:|---|---|
| 3 | EX_155MMGUN | 21 S_MEDIUMEXP2 |
| 8 | EX_BANG11 | 16 S_SMALLEXP1 |
| 9 | EX_BANG13 | 18 S_SMALLEXP3 |
| 10 | EX_BANG14 | 19 S_SMALLEXP4 |
| 18 | EX_WORM | 49 S_WORM |
| 20–25 | EX_WORMSN1–6 | 13 S_WORMSIGNSPARK |
| 27 | EX_THPUFF | 12 S_THUMPER |
| 36 | EX_CASH | 9 S_CRATEREVEAL |
| 37, 38 | EX_DEVASTATOR, EX_DEVDEATH | 24 S_LARGEEXP2 |
| 48 | EX_WALL | 25 S_LARGEEXP3 |
| 53–55 | EX_CRATEREVEAL, EX_CRATENOMAP, EX_CRATESTEALTH | 9 S_CRATEREVEAL |
| 58 | EX_BUILDING | 26 S_HUGEEXP1 |
| 59 | EX_GRENADE | 16 S_SMALLEXP1 |

All others are −1. Blink masks: EX_TRIKEM 0x55, MUZ_TANK 0x03,
MUZ_DEVAS 0x0f, MUZ_GTUR 0x03; all others 0.

### 4.3 Flags (`0x6e5750` and callers, verified unless marked)

| Bit | Meaning |
|---|---|
| 0x1 | rises: height += 2 px per tick |
| 0x2 | **darken** blit (§5.2) |
| 0x4 | **brighten** blit |
| 0x8 | house colours: the owner's 16 colours are copied into palette entries 240..255 before drawing (8-bit images) |
| 0x10 | **50 % translucent** blit |
| 0x20 | waiting for the start delay: not drawn |
| 0x40 / 0x80 | red tint / green tint blits (no type uses them by default) |
| 0x200 | **additive** blit |
| 0x400 | blink mode (muzzle flashes) |
| 0x800 | hidden (blink off) |
| 0x1000 | one-time move by (+0x30, +0x34) on the first update |

Default flags per type (`0x6e5750`): 0 for EX_HARV, EX_ROCKET, EX_BANG11,
EX_WORM, EX_CASH, EX_WALL, EX_CRATEREVEAL, EX_CRATENOMAP; 2 EX_PUFF;
3 EX_SMOKE3, EX_SMOKE4; 4 EX_WHITEN1, EX_FLASH; 0x10 EX_TRAIL3,
EX_WORMDUST, EX_THPUFF, EX_HARVDUST0–7, EX_CRATESTEALTH; 0x18 EX_DEVIATE,
EX_GREENEXP; 0x600 EX_TRIKEM and the MUZ_ types; 0x200 all others
(EX_BAZOOKA, EX_155MMGUN, EX_NEW ROCKET, EX_HMG, EX_TANKGUN, EX_MG,
EX_BANG13, EX_BANG14, EX_TRAIL, EX_WORMSN1–6, EX_DEVASTATOR, EX_DEVDEATH,
EX_FLAME, EX_FLAME2, EX_TRAIL2, EX_DHM, EX_REF, EX_DEVSPARK1–3,
EX_DEATHHAND, EX_SPICEBLOOM, EX_BUILDING, EX_GRENADE).
`python re/dump_templates.py` style dumps of both tables are easy to
regenerate from Templates.bin.

### 4.4 Update (`FUN_0049dd80`, once per tick, verified)

`FUN_00458e90` updates, for each player in order: units, buildings,
bullets, then explosions. An explosion spawned this tick by the same
player's units is therefore updated in the same tick.

```
if +08 != 0xffff:
    if that slot is a unit: if its state == 0x11: return 0 (remove)
    else +08 = 0xffff
if flags & 0x1000: x += +30; y += +34; +30 = +34 = 0
if flags & 0x400:                                  // blink mode
    if blinkmask[type] & (1 << +02): flags &= ~0x800 else flags |= 0x800
    +02 += 1
    return +02 != 8                                // removed after 8 updates
if +02 == 0:
    flags &= ~0x20; +02 = 2; frame += 1
    if frame >= frames[type]: frame = 0; return 0  // remove
else: +02 -= 1
if flags & 1: height += 2 px
return 1
```

The first branch is dead in 1.06: `FUN_00456df0` sets +08 = 0xFFFF and
no caller writes it afterwards (a scan of the instructions after all 29
calls: the muzzle flash spawners `0x495cdd` / `0x495d30` / `0x4962e5`
write only +0x01, the frame). Checked 2026-10-16.

- **Rate:** one frame every **3 ticks**, the same for every type. There is
  no per-type rate.
- **Frame 0** lasts only until the first update. A normal spawn is updated
  in the same tick, so frame 0 is drawn only when the explosion is created
  after its owner's explosion list ran (e.g. by another player's object)
  or with a start delay. Shown frames are 1 … n − 1, each for 3 ticks; the
  object lives 3·(n − 1) + 1 updates.
- **Start delay** (caller flag 0x20 with a delay d): hidden for d ticks,
  then frame 1.
- **Blink mode:** the frame is not advanced, so the spawner must pick the
  frame (32 frames = rotations for MUZ_ types). The muzzle flash
  spawners set the frame to the shooter's facing and leave +0x08 at
  0xFFFF (doc 18 §0, §3.1, §6.2).

### 4.5 Harvester effects

| Effect | Type | Frames | Flags | Entries (§4.7) | Spawn |
|---|---:|---:|---|---|---|
| EX_HARV | 0 | 11 | 0 | 4273–4283 | not spawned; the docked harvester draws frame +0x1e as its body (doc 13 §8.2) |
| EX_HARVDUST0–7 | 28–35 | 6 each | 0x10 translucent | 4534–4581 | each eaten level, at the harvester's x, y, height 0, flags 0 (doc 13 §8.2) |
| EX_REF | 49 | 14 | 0x200 additive | 4788–4801 | each unload step, at refinery (x + 62, y − 79) px, height 0, flags 0 |

None has a sound, and the spawn sites play none. A dust cloud shows
frames 1–5 for 15 ticks; EX_REF shows frames 1–13 for 39 ticks.
EX_HARV frame 0 is not used by docking (doc 13 §1.4).

### 4.6 Unit death effects (`FUN_0049cde0`, verified)

For a unit with a death explosion (rec+0x2f as a signed byte ≥ 0) that
is not dying yet:

1. EX_WHITEN1 (`0x6d6112`) at the unit's x, y px, height 0.
2. The death explosion's sound at the unit's cell.
3. The death explosion at x, y px with height = the unit's altitude.

For aircraft both explosions get flag 0x1000 with (DX, −DY) of the
unit's current speed and facing, so they start one flight step ahead.
Then hp = 0 and state 0x11. Infantry (no death explosion) play
`S_MANDYINGn` (n = 1..9, random) at their cell, unless in state 0x17.
The infantry death animation modes 9–13 (mode → rotation slot mode − 9)
belong to doc 18 §3.8.

## 5. Drawing

### 5.1 Pass order (`FUN_00428ca0` → `FUN_00428e30`, verified)

Inside `FUN_00428e30`, in this order:

1. The 30 cell objects at `0x517b98` (blooms / crates, doc 13 §9) that
   are active, image `0x7951a8[type]`, at the cell corner.
2. The **sorted draw list** (`FUN_0042aa90`): buildings and ground units.
   Units with flags & 0x200140 (aircraft, carried, hidden) are not in it,
   nor units with 0x1000000.
3. `FUN_004282f0(0)`: explosions with height **< 20 px**.
4. `FUN_0042a5d0(0)`: (a) carried units, aircraft shadows and carryall
   cargo, then (b) aircraft with altitude **< 20 px**.
5. `FUN_004282f0(1)`: explosions with height ≥ 20 px.
6. `FUN_0042a5d0(1)`: aircraft with altitude ≥ 20 px.

Each pass loops over the players in index order, then each list in its
order (creation order). Explosions and aircraft are **not sorted** by y:
every ground-level explosion is drawn over every building and ground
unit, and under aircraft and their shadows. After `FUN_00428e30`,
`FUN_00428ca0` calls `FUN_00428070` and then `FUN_00428220` (UI click
puffs, §5.4).

### 5.2 Blits used by explosions and shadows (8-bit image on a 16-bit surface)

Channels are 5-bit. On a 565 surface (`0x798384` = 0xfbce) green is read
as its top 5 bits and written back shifted, so its low bit is lost.
"idx" is the image pixel (palette index), "src" its palette colour.

| Flag | Function | Per pixel |
|---|---|---|
| none | `FUN_00430770(…, 1, 1)` | normal keyed blit (index 0 transparent, 1 shadow; docs/formats/fonts.md) |
| 0x2 | `FUN_004310c0` → `FUN_004310f0` | every pixel: channel = max(channel − idx, 0) (`0x790110` table); idx is read as a signed char |
| 0x4 | `FUN_00433340` | every pixel: channel = min(channel + idx, 31) |
| 0x10 | `FUN_004340e0` | idx ≠ 0: dst = (dst + src) / 2 per channel |
| 0x40 | `FUN_004337d0` | 565 surface: red + idx (max 31), green and blue − idx (min 0). 555 surface: all three + idx (max 31), i.e. a brighten (verified in the decompile; we always draw on a 565 surface) |
| 0x80 | `FUN_00433c50` | red − idx, green + idx, blue unchanged |
| 0x200 | `FUN_004349a0` | every pixel: channel = min(dst + src, 31) (`0x6d8da8` table) |
| shadow | `FUN_00434620` | idx ≠ 0: dst = (dst >> 1) & (mask >> 1), mask `0x797e6c` = 0xfbde (555) / 0xf7de (565) |

The tables are built in `FUN_00466140`: `0x6d8da8[a·32 + b]` =
min(a + b, 31), `0x790110[a·32 + b]` = max(a − b, 0). Flags are checked in
the order 0x820 (skip), 2, 4, 0x10, 0x40, 0x80, 0x200; the first match
wins. The additive and darken blits have **no** transparency test; index
0 of these images is black or 0, so it adds or removes nothing (inferred
from the tables).

### 5.3 Explosion position (`FUN_004282f0`, verified)

`screen x = (x >> 16) − img.off_x − scroll_x`,
`screen y = (y >> 16) − (height >> 16) − img.off_y + top − scroll_y`,
image `0x6e2e20[type · 40 + frame]`, with the R8 header offsets as in
doc 08 (`top` = `0x4e41dc`). The image offsets centre the effect on its
point (EX_HARV entries have off (21, 18) in a 48 × 48 frame).

### 5.4 UI click puffs (`FUN_00456d50`, `FUN_00428220`)

Not simulation objects. A move click (`FUN_004433a0`) adds an EX_PUFF at
the clicked world pixel into a 10-slot table at `0x516490` (x, y, frame,
counter; 16 bytes). A slot is free when its frame ≥ frames(EX_PUFF) = 5;
if none is free, the slot with the highest frame is reused (the code then
clears the counter one entry past the table: that is the red health-bar
colour 0x51653c, which turns black until the next mission; corrected in docs/re/46 section 2.2). Drawing advances them: counter += 1 per **draw**, frame
+= 1 when it passes 2. So they animate at the render rate, not the tick
rate. Drawn with the darken blit (flag 2), no height.

### 5.5 Aircraft (`FUN_0042a5d0`, verified)

Image = `0x71aca0[((element · 35) + frame) · 32 + rotation]` with
element = rec+0x14, frame = +0x1e, rotation = rec+0x3c[body facing +0x54].
House colours are copied into palette entries 240..255 first (8-bit
images), as for ground units.

| Unit | Element | Frames × rotations | Frame used | Rotation table |
|---|---:|---|---|---|
| CARRYALL | 16 | 1 × 32 (entries 2158–2189) | 0 | facing |
| ORNITHOPTER | 17 | 3 × 32 (2190–2285) | wing frame 0–2 | facing |
| FRIGATE | 27 | 1 × 1 (2784) | 0 | always 0 |
| DEATH_HAND | 21 | 1 × 2 (2382–2383) | 0 | 0 for facings 0–7 and 24–31, else 1 |

**Part (a), pass 0 only**, for units with flags & 0x140 and not 0x200000:

- **Carried unit** (0x100 without 0x40), and Death Hand (0x20000): body
  at (x − off_x, y − (altitude >> 16 & 0xff) − off_y), keyed blit (1, 1),
  no shadow. A ferried unit has altitude = carryall − 10 px (§2.6). The
  Death Hand also has 0x40, so part (b) draws it a second time.
- **Aircraft shadow** (0x40 without 0x20000): the same image at the
  **ground** position (x − off_x, y − off_y), shade blit (§5.2): the
  silhouette at half brightness. There is no offset for altitude, so the
  shadow sits straight under the aircraft's ground point.
- **Delivery cargo** (0x20000000): image of the entry's **first record**
  (`player+0x262f4 + u.+0x2c · 0x38`), with the carryall's frame and the
  cargo record's rotation table at the carryall's facing, at
  y − ((altitude − 10 px) >> 16 & 0xff). Drawn after the carryall's
  shadow and before every aircraft body, so it sits under the carryall,
  10 px lower.

**Part (b), each pass**, for units with 0x40 and not 0x200000, split by
altitude (< 20 px in pass 0, ≥ 20 px in pass 1): body at
(x − off_x, y − (altitude >> 16 & 0xff) − off_y), keyed blit (1, 1). If
+0x28 is a group number 0–9, the group digit `0x6e2df0[+0x28]` is drawn
at (x − off_x + 2, y − off_y), i.e. at ground level without the altitude.

So a carryall at 60 px is drawn 60 px above its shadow. During a drop it
passes 20 px and moves from the top pass to the lower one.

**Cargo after the drop (verified):** nothing in the exe clears
0x20000000 (scan of every `and` mask with bit 29 clear: only cell and
CRT masks). After the drop the first record is 0xfe, so the draw reads
unit record 254, past the 30 records: element byte `0x78e064` and the
rotation table `0x78e08c..`. Both lie in the unit graphic name block
`0x7891b0` (Templates.bin block 21, name entry 44 of 60), which the
loader fills from the file only, and these bytes are zero in 1.06. So
the exe draws element 0, the carryall's frame (0), rotation 0: the first
LIGHT INF image (DATA.R16 entry 206, 14 × 17, offset (5, 12)) 10 px
below the empty carryall, before every aircraft body. We draw the same
(`GameScreen::draw_aircraft`); whether the carryall body hides it is
then the same pixel result as in the exe.

## 6. DATA.R16 ranges (verified with `FUN_004606d0` and d2k-tool)

- Bullets start at entry **3912**: 34 bullets, `0x795340[b]` frames each,
  stored in `0x714b00[b · 32 + f]`.
- Explosions start at **4273** and end at **4921**: 60 types,
  `0x6d5fd0[x]` frames each, stored in `0x6e2e20[x · 40 + f]`. Entry of
  (x, f) = 4273 + sum(frames of types < x) + f.
- Unit icons follow at 4922 (60 × 47 images).
- Some starts: EX_HARV 4273, EX_SMOKE3 4397, EX_WHITEN1 4405,
  EX_FLASH 4413, EX_PUFF 4524, EX_HARVDUST0 4534, EX_HARVDUST7 4576,
  EX_REF 4788.

## 7. Other notes

- **Damaged unit smoke:** the unit update spawns EX_SMOKE3 (`0x6d6113`)
  near a vehicle with +0x27 ≠ 0 and hp ≤ half, every 4th tick (with a
  random parity), and lowers +0x27 every 4th tick; above half hp +0x27 is
  cleared. Verified and ported with its rand() order in doc 43 §2
  (`0x497e81`); it runs for aircraft too.
- **Frigate in state 7:** while it carries (0x100) and the Starport
  exists, the Starport building gets flag 0x20 each tick (inferred:
  landing animation).

## 8. Open points

All open points of this doc were settled on 2026-10-16:

- The left/top edge quirk of the leaving carryall: verified in the
  listing (§3.2).
- The ghost cargo image after the drop: verified, record 254 reads zero
  bytes, drawn as the first LIGHT INF image (§5.5); ported.
- Unit flag 2 = fired, and the +0x2b timing (§2.3, corrections): ported
  with the flag-2 reset in the next update.
- Flag 0x400 on aircraft: no reader (§1.1). 0x4000000: never set on units
  (doc 33 §0). **0x8000000** (set with 0x800 | 0x200 | 0x100 by the
  delivery launch, `0x46e36b`, never cleared): area damage skips such a
  unit (`0x49d806`, doc 18), so a delivery aircraft (carryall or
  Starport frigate) is never hurt by splash; ported
  (`aircraft.cpp` launch, `combat.cpp` damage). 0x40000 / 0x80000 on dropped units: §2.3.
- Muzzle flash spawners: doc 18 (§4.4).
- The 555 path of the red tint blit: verified (§5.2).
- Kind 15: no unit record has kind 15 in 1.06 (Templates.bin records
  0..29), so its wings / random destination code is unreachable.
- Death Hand flight (`FUN_00494cd0`) is in doc 41.
