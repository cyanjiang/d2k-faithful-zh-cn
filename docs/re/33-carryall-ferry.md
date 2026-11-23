# 33: Carryall ferry (1.06)

Written for a line-by-line port of the carryall **ferry** (a player's own
carryall lifting its own ground units). The free-harvester **delivery**
(flag 0x800) and the Starport frigate are covered by doc 13 §5 and doc 14
§2.2–2.3 and are only mentioned where they touch the ferry.

Conventions as in doc 30: `unit+…` are object fields (objects at
`player+0x30`, stride 0x94, unit list head `player+8`, next `+0x7c`),
`player+…` is `FUN_004a4660(side)`, `cell(x, y)` is the u32 at
`0x517df4 + (0x547df0[y] + x) · 12`, `urec+…` is `0x77e250 + rec · 0x100`
(kind at +0x2D). Fixed point: positions and altitude are 16.16 px, so
`0x10000` = 1 px, `0x3c0000` = 60 px, `0xa0000` = 10 px.

"Verified" = read from the decompile and checked in the disassembly
(`re/disasm.py`) where it matters (signedness, compare direction, byte vs
word). Every open point was settled against the exe on 2026-10-16 (§8).

**No `rand()` call anywhere in the ferry system.** The only draws near it
are DoProtection's line 0x40B (doc 29 §1.2, before the request is made) and
KillUnit's infantry-only draws (lines 0x1BD2, 0x1BFB), which never run for
ferry cargo because infantry can never be picked up (§2.5). The carryall
record has no weapons and urec+0x26 = 0, so the idle code never reaches
the turret `rand()` of `FUN_00494210` (line 0x39E) either.

Main functions:

| Address | Name used here | Role |
|---|---|---|
| `FUN_0046cca0` | FindCarryallSlot | first free request slot (thiscall, ECX = player) |
| `FUN_0046cd00` | AddToQueue | write a request into a slot |
| `FUN_0046ce00` | FindRequestFor | first waiting slot this carryall should take |
| `FUN_0046ce70` | IsClosestIdleCarryall | is no other idle carryall nearer to the slot's "from" cell |
| `FUN_00421800` | CellDist2 | squared cell distance of a unit's pixel position to a cell |
| `FUN_0046d180` | RequestMonitor | slot upkeep, timeout, slot-count shrink |
| `FUN_00497b90` | UnitUpdate | states 0xb / 0xd / 0x18 (request), 0x12, 0x13, carryall 2 / 7 / 0x15 / 0x16, tail copy |
| `FUN_0049cde0` | KillUnit | cargo recursion, slots of a killed carryall |
| `FUN_00455870` | CreateUnit | carryall creation (counts, edge start) |
| `FUN_0049deb0` | SetState | state change with refusals (doc 09) |
| `FUN_004954b0` | TurnAir | one facing step toward a heading (doc 14 §1.2) |
| `FUN_0045d990` | CanUnitUseSquare | passability, used for the drop cell (mode 0) |
| `FUN_004a60e0` | FindFreeCellSpiral | doc 14 §2.4 |
| `FUN_0049f4a0` | CanTakeOrders | refuses carried units and (mode 0) waiting units |

## 0. Corrections and answers for earlier docs

- **doc 13 §6, slot layout:** +4 is a **kind byte** (1 = DoProtection,
  **100** = the unit's own request, `push 0x64` at `0x49a7aa`). Nothing in
  1.06 ever reads it (scan of every `+0x26610` displacement: the only
  write is in AddToQueue). +8 is the carryall's unit **index** (u16), +0xc
  the age (u32), +0x10 the saved state (u32; the carryall keeps only its
  low byte). Full layout in §1.1.
- **doc 13 §6, "State 0x12 … state 2":** it is `SetState(2)`, which applies
  the follow-up rule (+0x14 ≠ 1 → state = +0x14, §2.4). A harvester
  delivered by the free-harvester carryall still has follow-up 0xb.
- **doc 13 §6, RequestMonitor:** it also **shrinks the slot count** by one
  per call (§1.6). The age test is "age > 79 after the increment"
  (80 calls = 640 ticks), as written there.
- **doc 14 §1.7 idle carryall:** it takes a waiting request only if **no
  other carryall of the player in state 2 without 0x800 is strictly
  nearer** to the request's "from" cell (§1.4). Slot order (lowest index
  first) is the priority; there is no age or distance ordering otherwise.
- **doc 14 §2.6 TODO "flag 0x4000000":** never set in 1.06. Unit record
  flags (urec+0x38) are only 0, 0x8000 and 0x800000; no instruction sets
  bit 26 of a unit's flags (dword and byte scans); the pick-up's
  `& 0xfbffffff` (`0x498a73`) is a no-op. The `or 0x4000000` sites in the
  AI code (`0x422e43`, `0x41b75f`, `0x41eaeb`) act on **building** flags
  (+0x28 of a building object).
- **doc 14 §2.6 TODO "0x4000 without 0x800":** harmless. A failed pick-up
  sets 0x4000 and it is never cleared, but every reader of 0x4000 for
  kind 2 also needs 0x800 (state 2 delivery branch, the flight step's edge
  removal). Keep the bit (saved state, debug dumps).
- **doc 12 §8.3 step 5 (carryall):** verified, see §3.1: the start point is
  4 cells past the map edge nearest the **move cell (x2, y2)**, the
  destination is the **spawn cell (x1, y1)**, no slow-down on the way.
  **Map units:** `FUN_00469790` creates them with (x1, y1) = (x2, y2)
  (`0x469cf3`–`0x469cfb`), so a carryall placed on the map at mission start
  (A5V1 Harkonnen) **starts off the map** and flies in to its map cell.
  Verified in the listing: the map loader pushes the same two bytes for
  both pairs and does not touch the unit afterwards (`0x469d02`..), and
  CreateUnit's kind-2 branch (`0x455b7d`..`0x455c43`) always moves the
  carryall to EdgePoint(x2, y2) with dest (x1, y1) and state 7. The flight
  in is the ordinary flight step (doc 14 §1).
- **doc 21 §4 TODO "far away … carryall request":** verified, §2.1. The
  same code as the harvester's, saved state 0x18, threshold VARS +0x2c
  (`0x6b8844` = 64), checked only when the move step did not arrive.
- **doc 29 §1.4 TODOs:** (a) order within a tick: DoAI (DoProtection) runs
  **before** the unit update, RequestMonitor **after** it (§5). (b) **A
  carryall never lifts a protected unit through the DoProtection request:**
  the pick-up needs the unit in state 0x12 (§2.5) and DoProtection never
  sets 0x12. A carryall can take the request (it flies there), then either
  RequestMonitor marks it done within 8 ticks (unit not in 0x11/0x12/0x13)
  and the carryall turns back, or it lands and the pick-up fails (0x4000,
  climb). The only real airlift is indirect: the overwritten destination
  makes a harvester in **state 0xb** drive to the base position, and its
  own far check (§2.1) then requests a lift there. In state 0xd the
  overwrite is lost at once (0xd re-sets dest to the dock cell each tick).
  Verified: DoProtection (`FUN_0041c070`) calls no SetState at all (it
  writes the dest, flag 8 and the request), and the pick-up tests
  `T.state == 0x12` (§3.5).
- **doc 13 §10 / doc 14 §2.6 TODO "full pick-up / put-down sequence":**
  §3.

## 1. The request table

### 1.1 Layout (player struct, verified)

| Off | Type | Field |
|---|---|---|
| `player+0x2660c + s·0x14` | 32 slots of 0x14 bytes, s = 0..31 | |
| slot +0 / +1 | u8, u8 | **from** cell (the unit's current cell +0x4a/+0x4b) |
| slot +2 / +3 | u8, u8 | **to** cell (the unit's dest +0x48/+0x49 at request time; may be replaced by the drop-cell search, §3.6) |
| slot +4 | u8 | kind: 100 own request, 1 DoProtection (never read) |
| slot +5 | u8 | **state**: 0 free, 1 waiting, 2 taken (carryall on the way), 3 done (freed by the next RequestMonitor call), 4 picked up |
| slot +6 | u16 | unit index (same player) |
| slot +8 | u16 | carryall index (written when taken) |
| slot +0xa | u16 | unused |
| slot +0xc | u32 | age (RequestMonitor calls while state 1 or 2) |
| slot +0x10 | u32 | saved state (the unit's +0x10 when the request was made) |
| `player+0x2688c` | u8 | **slot count** n (slots 0..n−1 are searched) |
| `player+0x2688d` | u8 | **live carryalls** (delivery ones included) |
| `player+0x245c0` | u8 | owned carryalls (without delivery ones; doc 13 §5.2) |

0x2660c + 32 · 0x14 = 0x2688c, so the table, the count and the live count
are one contiguous block of the player struct (save it with the player).

**Player reset** (code at `0x46bc99`–`0x46bce4`): slot state +5 = 0 for all
32 slots, +0x245c0 = 0 (in the 0x245bc.. count loop), +0x2688c = 0,
+0x2688d = 0. Other slot bytes keep their old values. A stale value
cannot matter: every reader of a slot field other than the state reads
only slots whose state is not 0 (RequestMonitor, FindRequestFor, the
state 0x12 search, AddToQueue's duplicate test, KillUnit's state-2 loop),
and AddToQueue writes every field those readers use (from, to, kind,
unit, age, saved state) before it sets state 1; the carryall index is
written when the slot is taken (state 2), before KillUnit can read it.

### 1.2 Slot count (verified)

- **CreateUnit, kind 2** (any carryall, delivery ones too, `0x455ba3`):
  `n = n + 1; if n > 31 (u8): n = 32`; live += 1; owned (+0x245c0) += 1.
  The delivery code then undoes **only** the owned count (doc 13 §5.2), so
  delivery carryalls do add a slot and a live count.
- **Removal** (`FUN_00458e90`, `0x458f3b`): kind 2 without 0x800: owned −= 1;
  kind 2: live −= 1.
- **RequestMonitor** shrinks n by one per call (§1.6).

So n ≥ live, and n reaches the live count again only through the shrink,
one slot per 8 ticks, and only while the last slot is free.

### 1.3 FindCarryallSlot (`FUN_0046cca0`, thiscall ECX = player, arg unit; verified)

```
if u.state == 0x12: return 0        // returns slot 0 (low byte 0), not 0xFF; unreachable from both callers
if P.u8[0x245c0] == 0: return 0xFF
for i = 0; i < P.u8[0x2688c]; i++:   // u8 loop counter
    if slot[i].state == 0: return i
return 0xFF
```

### 1.4 AddToQueue (`FUN_0046cd00(P, unit, id, s, kind, saved)`; verified)

```
(debug only: error if saved == 0x12; error if s > 31)
for i = 0; i < n; i++:                         // n re-read each pass
    if slot[i].unit == id (u16) and slot[i].state != 0:
        if slot[i].state != 1: return 0       // taken / done / picked up: fail, nothing written
        slot[i].state = 0                     // an older waiting request is dropped
e = slot[s]
e.state = 1; e.saved = saved; e.age = 0
e.from = (u.+0x4a, u.+0x4b); e.to = (u.+0x48, u.+0x49)
e.unit = id; e.kind = kind
return 1
```

Note the order in the caller: FindCarryallSlot runs **before** AddToQueue,
so a dropped older request is never the slot that gets reused in the same
call (it was not free when the slot was chosen).

### 1.5 Taking a request (verified)

**CellDist2** (`FUN_00421800(u, x, y)`): `cx = trunc(u.x / 2^21)`,
`cy = trunc(u.y / 2^21)` (C signed divisions `/32` then `/65536`, i.e. the
cell of the pixel position, toward zero), returns
`(cx − x)² + (cy − y)²` as int, x and y as u8.

**IsClosestIdleCarryall** (`FUN_0046ce70(P, c, s)`):

```
d = CellDist2(c, slot[s].from)
for v in P.units (list +8, next +0x7c):
    if urec[v].kind == 2 and v.state == 2 and !(v.flags & 0x800)
       and CellDist2(v, slot[s].from) < d:      // unsigned compare (jae at 0x46ceed)
        return 0
return 1
```

Every carryall in state 2 without 0x800 counts, even one that holds a job
(0x100 / 0x200) and has not been updated yet this tick. A tie does not
block: the first of two equally near carryalls to be updated takes the slot.

**FindRequestFor** (`FUN_0046ce00(P, c)`):

```
if n == 0: return 0xFF
for i = 0; i < n; i++:
    if slot[i].state == 1 and IsClosestIdleCarryall(P, c, i): return i
return 0xFF
```

### 1.6 RequestMonitor (`FUN_0046d180`, fastcall ECX = player; verified)

Called once per tick for the player `tick & 7` only, and only when that
player is active (`player+0x24250` ≠ 0), after the unit update (§5).

```
n = P.u8[0x2688c]
if P.u8[0x2688d] < n and slot[n−1].state == 0:        // u8 compare; 0x265fd + n·0x14
    P.u8[0x2688c] = n − 1
i = 0
if P.u8[0x2688c] != 0:
  do:
    s = slot[i]
    if s.state == 3: s.state = 0
    (debug: error if s.unit == 0xFFFF)
    if s.state != 0:
        st = unit[s.unit].state                       // P + 0x40 + id·0x94
        if st == 0x11:
            s.state = 3
            SetState(unit[s.unit], s.saved)           // refused (dying) but see the note
        elif st != 0x12 and st != 0x13:
            s.state = 3
        if s.state == 1 or s.state == 2:
            s.age += 1                                // u32
            if s.age > 79:                            // unsigned
                SetState(unit[s.unit], s.saved)
                s.state = 3
    i += 1
  while i < P.u8[0x2688c]
```

- A slot only lives while its unit is in 0x12 (waiting) or 0x13 (carried).
  Done slots (3) are freed on the next call, so a slot index can be
  re-used by another request 8 ticks after it was marked done.
- **Timeout:** 80 calls in state 1 or 2 = 640 ticks; the unit gets its
  saved state back (it drives again). Slot state 4 never ages.
- **SetState note:** SetState first clears unit +0x74, +0x76, +0x7a,
  +0x78, +0x75 (unless the new state is 10) and sets
  `flags = (flags & 0x3fffffff) | 0x80000000`, and only then refuses the
  change for a unit in 0x11 (doc 09). So the call on a dying unit still
  has these side effects. Port SetState with that order.

## 2. Who requests a lift

### 2.1 The request check (unit update, `0x4999dd` / `0x49a781`; verified)

Only three unit states reach it (`goto` targets checked in the decompile;
FindCarryallSlot / AddToQueue have no other callers than this and
DoProtection):

| State | Where in the state | Threshold |
|---|---|---|
| 0xb harvester to spice (case 9) | after MoveStep returned ≠ 0 (not arrived) | VARS `0x6b8840` = 64 |
| 0xd harvester to refinery (case 0xb) | at the end of every path of the state (after MoveStep, the 0xe switch, the dock blocker's state 9, even the "no refinery → state 2" path) | `0x6b8840` = 64 |
| 0x18 to the repair pad (case 0x16) | after the move step when not arrived (doc 21 §4) | VARS `0x6b8844` = 64 |

```
dx = u.+0x4a − u.+0x48;  dy = u.+0x4b − u.+0x49     // u8 → int
if dx² + dy² > T                                      // signed, jle skips (0x4999fc)
   and u.+0x4f == 0                                   // between steps
   and !(u.flags & 0x10000000):                       // "to" cell not reserved
    s = FindCarryallSlot(P, u)
    if s != 0xFF:
        if AddToQueue(P, u, index, s, 100, u.state):
            SetState(u, 0x12)
```

(For state 0x18 the order of the tests is +0x4f, flag, then distance;
same result.) So a harvester or a unit sent to the pad asks for a lift
whenever it is more than 8 cells (straight) from its destination, between
steps, and the player owns a carryall and has a free slot. It asks again
every tick in which this holds, so after a timeout it re-asks as soon as a
slot is free.

**Player orders:** a plain move (order 0x13 off spice → state 7) never
requests a lift. The **harvest order** (move on spice, state 0xb, doc 13
§8.1) and the **dock** (0xd) and **repair** (order 0x21 → 0x18) orders do,
through the check above. Retreat sends harvesters to 0xd, so they can be
lifted too.

### 2.2 DoProtection (`FUN_0041c070`)

Doc 29 §1.2 / §1.4: FindCarryallSlot, then dest = base position, then
AddToQueue(kind 1, saved = the current state). No SetState(0x12). See §0
for why this never ends in a pick-up.

### 2.3 Infantry

Order 0x21 skips infantry (doc 21), the AI repair uses the same order, and
harvesters are vehicles. So the request check only ever runs for vehicles.
A DoProtection request for an infantry unit (if a mission protected one)
fails at the pick-up like any DoProtection request. The infantry branches
of the pick-up and drop (§3.5, §3.6) are therefore dead in 1.06; port them
anyway.

### 2.4 State 0x12, waiting (case 0x10; verified)

```
found = false
for each slot i < n:
    if slot.unit == index (u16) and slot.state != 0
       and slot.from == (u.+0x4a, u.+0x4b): found = true
if !found: SetState(u, 2)          // follow-up rule: +0x14 ≠ 1 → state = +0x14 (0x12 → 2), +0x14 = 1
u.+0x2c = 0
if FUN_0049f5f0(u): FUN_0049f5c0(u)     // cloak test (flag 0x10) / re-cloak, stealth units only (doc 32)
if index % 8 == tick & 7 or u.+0x0c != 0xFFFF:
    if !FUN_0049f5f0(u): FUN_00496480(u, …)              // fire at a target in range (doc 18); harvesters: no weapon, returns 0
```

The unit does not move. "found" includes a slot in state 3 (not freed
yet). **Orders while waiting:** CanTakeOrders(u, mode) refuses state 0x12
only in mode 0. ProcessOrder uses mode 1 for every order except the repair
order 0x21 (mode 0). So the player can re-order a waiting harvester; it
leaves 0x12 and the request dies at the next RequestMonitor call (or the
carryall finds it not in 0x12 and gives up, §3.5). A repair order is
refused while waiting. A carried unit (flag 0x100) refuses every order
(CanTakeOrders tests `flags & 0x400140`).

### 2.5 State 0x13, carried (case 0x11)

Does nothing (`break`). The carrying carryall moves it (§3.7). The common
part of the unit update still runs (counters; the +0x2a counter skips
units with 0x100). A carried unit does not reveal shroud (doc 16), is not
in the sorted draw list and is dropped as a target by attackers (doc 18).

## 3. The carryall side

Carryall fields used by the ferry:

| Field | Ferry use |
|---|---|
| +0x0c (u16) | the unit to carry (index in the same player's list). Not reset after a job; harmless, only read with 0x100 / 0x200 |
| +0x26 (u8) | saved state of that unit (low byte of slot +0x10) |
| +0x29 (u8) | slot index |
| +0x3c | altitude |
| +0x48 / +0x49 | dest (the slot's from, then its to) |
| flags 0x40 | aircraft |
| flags 0x200 | job taken, not loaded yet |
| flags 0x100 | loaded |
| flags 0x4000 | set by a failed pick-up, never cleared, no effect without 0x800 |

The top-of-update target check that clears +0x0c when the target is dead
or carried skips kind 2 (`0x497d93`), so +0x0c survives.

### 3.1 Creation (`FUN_00455870`, kind 2; verified)

After the common fields (doc 12 §8.3; hp, speed 0x40000, flags =
urec+0x38 = 0x800000, state 2, +0x14 = 1, +0x1a = urec+3 = 1, +0x2b = 0xff,
+0x0c = 0xffff, +0x29 = 0):

```
P.u8[0x245c0] += 1
u.alt = 0x3c0000; u.flags |= 0x40
n += 1; live += 1; if n > 31: n = 32
(ex, ey) = (x2, y2); f = EdgePoint(&ex, &ey, 0)   // FUN_0049e6b0: 4 cells past the edge nearest (x2, y2), doc 13 §5.2
u.+0x55 = u.+0x54 = f                             // facing toward the map
u.x = (i16)ex << 21; u.y = (i16)ey << 21          // cell corner, signed
cur / to / home cells (+0x4a/4b, +0x4c/4d, +0x50/51) = (ex, ey)   // bytes, so −4 = 252
dest (+0x48/49) = (x1, y1)
SetState(u, 7)
```

The end of CreateUnit does not change the state of kinds 2 / 8 / 10, so it
stays 7, and kind 2 marks no ground cell. **Factory carryall** (High Tech
factory, spawn (1, 0), move (0, 0), doc 12 §8.1): it appears 4 cells past
the edge nearest the factory's (0, 0) cell and flies to the factory's
(1, 0) cell at the full 4 px/tick (no 0xb00 flag, so no slow-down). It
reaches state 2 there and starts idling or takes a request. **Map
carryall** (A5V1 Harkonnen): the same, flying to its map cell. The
factory still waits for a free spawn cell (`flags & 0x3e8`), which the
carryall never occupies.

### 3.2 State 2, ferry branch (kind 2 or 8, flag 0x800 clear; `0x498250`–`0x498364`, verified)

```
if u.flags & 0x100:                                   // loaded
    s = (i8)u.+0x29                                   // movsx
    if (u.+0x4c, u.+0x4d) == slot[s].to: SetState(u, 0x15)
    else:
        u.flags &= ~0x200
        dest = slot[s].to
        SetState(u, 7)
elif u.flags & 0x200:                                 // job, not loaded: arrived over the unit
    SetState(u, 0x15)
else:                                                 // idle
    s = FindRequestFor(P, u)
    if s == 0xFF:
        if tick & 7 == index & 7: u.+0x55 = (u.+0x55 + 1) & 31
        FlightStep(u, mode 1)                         // doc 14 §1.7: slow circle
    else:
        dest = slot[s].from
        u.+0x0c = slot[s].unit; u.+0x26 = (u8)slot[s].saved; u.+0x29 = s
        slot[s].state = 2; slot[s].carryall = index
        SetState(u, 7)
        u.flags |= 0x200                              // after SetState
```

Then the common state-2 tail runs (kind 1 test; the 8-tick fire check:
`FUN_00496480` returns 0 for the weaponless carryall, `FUN_00494210` does
nothing because urec+0x26 = 0). No `rand()`.

A newly taken job is flown to on the **next** tick (state 7). The request
can be taken in the same tick it was made if the carryall is updated
after the requesting unit.

### 3.3 State 7, aircraft branch (case 5, flag 0x40; verified)

```
(kind 8 Starport flag, not ferry)
if u.flags & 0x200:
    if slot[(i8)u.+0x29].state == 3 and !(u.flags & 0x100):
        u.flags &= ~0x200; SetState(u, 2); break        // job cancelled
if FlightStep(u, mode 0) == 0: SetState(u, 2)            // arrived (centred in dest)
```

FlightStep (doc 14 §1.1–1.4): with 0x200 or 0x100 set the carryall slows
by 5 % per tick within d² ≤ 16 cells of the dest, down to 1 px/tick, then
centres in the dest cell. While loaded (0x100, 0x200 already cleared) the
slot is **not** checked in state 7.

### 3.4 State 0x15, ferry branch (flag 0x800 clear; `FUN_00497b90` case 0x13; verified)

```
s = (i8)u.+0x29
if slot[s].state == 3 and !(u.flags & 0x100):
    u.flags &= ~0x200; SetState(u, 0x16); break               // job cancelled: climb
(debug: error if u.+0x0c == 0xFFFF)
if u.flags & 0x100:                                            // loaded: check the drop cell every tick
    if !CanUnitUseSquare(dest.x, dest.y, u /*the carryall*/, side, 0):
        FindFreeCellSpiral(&dest.x, &dest.y, 8)                // unchanged if none found
        slot[s].to = dest                                      // u.+0x29 as u8 here
        SetState(u, 0x16); break
T = P.unit[u.+0x0c]                                            // same player
if u.alt == 0x3c0000 and !(u.flags & 0x100):
    if TurnAir(T.+0x54, u) == 0: break                         // face the unit's body facing first
if u.alt <= 0xa0000:                                           // signed, < 0xa0001
    if !(u.flags & 0x100): PickUp (§3.5)
    else:                  Drop (§3.6)
else:
    u.alt -= 0x10000
```

- CanUnitUseSquare is called **with the carryall**, so the drop cell must
  pass the carryall's (vehicle) test in mode 0: inside the map, vehicle
  bit 0x2000, no unit / building bits 0x18 (with the exceptions of
  `FUN_0045d990`: an own building cell with cell flag 0x10000000 and no
  units passes). **Corrected:** cell flag 0x10000000 is written only by
  the building placement `FUN_0044dbd0` (scan of every `or` with bit 28:
  `0x44ddba`, `0x44ddf0`, `0x44de22`; the other two are unit flags):
  every repair pad cell except its (1, 1), the heavy factory's (0, 2) and
  the light factory's (2, 1) exit cell (doc 12 §6 step 11; checked in the
  listing: column / row at `[esp+0x3c]` / `[esp+0x40]`). A refinery dock is
  not one of them. So a ferry can drop on an own repair pad's outer
  cells or a factory exit cell, never on a refinery.
- TurnAir (`FUN_004954b0`) returns 1 when the facing equals the target,
  else 0; it steps one facing when its +0x1a delay is 0 (reload urec+3 =
  1), so one step per 2 ticks. The descent starts in the same tick the
  facing matches. No turning while loaded or below 60 px.
- From 60 px: 50 ticks of descent, the action on the 51st tick.
- No position check: the carryall is wherever state 7 centred it (the
  slot's "from" cell, or the "to" cell when loaded).

### 3.5 PickUp (verified, around `0x4989cb`–`0x498a80`)

```
if !(T.flags & 0x100) and T.state == 0x12:
    (debug: error if T is mid-step with "to" ≠ "cur" and flag 0x10000000)
    c = cell(T.+0x4a, T.+0x4b)
    c &= ~8                                             // vehicle bit
    if c & 0x3e0:                                       // infantry sub-slots still set
        c = (c & ~7) | ((c >> 25) & 7)                  // owner bits from the infantry owner field
    (debug: error if T.flags & 0x10000000)
    u.flags = (u.flags & ~0x4000000) | 0x100            // 0x4000000 never set (§0)
    T.+0x19 = 0                                         // unselected
    T.flags |= 0x100
    SetState(T, 0x13)
    slot[(u8)u.+0x29].state = 4
    SetState(u, 0x16)
else:
    u.flags = (u.flags & ~0x200) | 0x4000               // unit gone / moved on / already carried
    SetState(u, 0x16)
```

- Only the vehicle bit is cleared. An infantry cargo's sub-slot bit
  (0x20 << slot) would stay set (dead code in 1.06, §2.3).
- The pick-up does not check that the unit is under the carryall. It is,
  because a waiting unit does not move and the slot's "from" cell is its
  cell (state 0x12 keeps itself alive only while from = its cell).
- In the same tick the tail copy (§3.7) runs, so the cargo gets the
  carryall's position and altitude 10 − 10 = 0 px at once.

### 3.6 Drop (verified, around `0x498820`–`0x498870`)

```
T.flags &= ~0x100
T.alt = 0
SetState(T, (u8)u.+0x26)                         // zero-extended (0x498841)
c = cell(T.+0x4a, T.+0x4b)                        // = the carryall's cell (copied last tick)
c = (c & ~7) | side | 8
if urec[T].infantry (+0x11):                      // dead in 1.06
    cc = cell(u.+0x4a, u.+0x4b)
    cc = (cc & 0xf1ffffff) | (side << 25)
slot[(u8)u.+0x29].state = 0                       // freed directly, no state 3
u.flags &= ~0x100
SetState(u, 0x16)
```

- The cargo keeps the position the carryall had (it may be centre px + 1,
  doc 14 §1.4). MoveStep needs nothing special for that: a step starts
  from any pixel position, takes max(|dx|, |dy|) / speed ticks and stops
  within half a speed of the next cell's centre, with no snap (doc 09
  §5), so the extra pixel is absorbed by the first step like any other
  offset. Its facings are the carryall's body facing (+0x54) and
  desired heading (+0x55, so a turret points where the carryall last
  wanted to turn), "to" = "cur", +0x4f = 0. Its dest (+0x48/49) and home
  (+0x50/51) are untouched: a harvester in 0xb drives on to its spice cell
  from the drop cell, 0xd re-computes the dock, 0x18 the pad.
- The drop cell passed CanUnitUseSquare in this same tick (§3.4), so the
  vehicle bit is free. No re-check of the cargo's own passability.
- Saved states seen in 1.06: 0xb, 0xd, 0x18 (§2.1). SetState from 0x13
  accepts them.

### 3.7 State 0x16 and the tail copy (verified)

- **State 0x16** (case 0x14): `if alt < 0x3c0000: alt += 0x10000` else
  `SetState(u, 2)`. So 50 climb ticks, state 2 on the 51st.
- **Tail copy**, at the end of the unit update (only reached through
  `break`, so not on the removal tick of state 0x11): for a kind-2 unit
  with 0x100 and without 0x800, if `T.flags & 0x100`:

  ```
  T.+0x4c = u.+0x4c; T.+0x4d = u.+0x4d       // to
  T.+0x4a = u.+0x4a; T.+0x4b = u.+0x4b       // cur
  T.x = u.x; T.+0x4f = 0; T.y = u.y
  T.alt = u.alt
  T.+0x54 = u.+0x54; T.+0x55 = u.+0x55
  T.alt -= 0xa0000
  ```

  It runs after the carryall's own state code in the same update, so the
  cargo is one step behind only if it was updated earlier in the list;
  it does nothing in 0x13 anyway.

### 3.8 One full ferry, tick by tick (from the code above)

1. Unit update (0xb / 0xd / 0x18): far → slot state 1 → unit state 0x12.
2. Idle carryall, state 2: FindRequestFor → slot 2, dest = from, state 7,
   0x200.
3. State 7: flight; within 4 cells slow-down; centring; state 2.
4. State 2: 0x200 → state 0x15.
5. 0x15: turn to the unit's facing (≤ 16 steps × 2 ticks), 50 ticks down,
   PickUp → unit 0x13, slot 4, state 0x16.
6. 0x16: 50 ticks up, state 2.
7. State 2: loaded, not at slot.to → clear 0x200, dest = to, state 7.
8. State 7 flight, state 2, then 0x15 at to.
9. 0x15: drop-cell check each tick (else spiral, store, climb, back to
   step 7), 50 ticks down, Drop → unit gets its saved state, slot 0,
   state 0x16.
10. 0x16 → state 2 → idle (or the next request).

Slot timeout during steps 1–5 (640 ticks in state 1 or 2) cancels it.
From the pick-up on there is no timeout.

## 4. Losses and edge cases

### 4.1 The carryall is killed (KillUnit `FUN_0049cde0(side, index)`; verified)

In this order:

1. Not an aircraft and flags 0x100 (that is: a **carried cargo**): flags
   |= 0x400000. An aircraft: explosion flag 0x1000 for the death effects.
2. `(flags & 0x140) == 0x140` and kind 2 and +0x0c ≠ 0xFFFF (a loaded
   carryall): **KillUnit(side, +0x0c)** first (the cargo dies too, with
   its own death effects at its position, step 1 marks it 0x400000).
3. (infantry only: death animation, `rand()` line 0x1BD2.)
4. Kind 2: for every slot i < n with **state 2** and slot.carryall ==
   index (u16 compare, `0x49cef4`): `SetState(unit[slot.unit],
   slot.saved)`, slot.state = 3. A waiting unit drives on. Slots in state 4
   (picked up) are left to RequestMonitor: the cargo is now in 0x11, so
   the next call marks the slot 3 (and calls SetState on the dying cargo,
   §1.6 note).
5. Death explosions (if urec+0x2f ≥ 0 and not already 0x11), hp = 0,
   SetState(0x11), etc. (doc 14 §3.3 / doc 18).

**Quirk:** step 4 restores the saved state even when the unit is no longer
waiting (only possible for a DoProtection request, whose unit is never in
0x12): such a unit is set back to the state it had when the request was
made.

**Falling with cargo:** the carryall in 0x11 keeps flying straight for 10
ticks (doc 14 §3.3) and its update still ends in `break`, so the tail copy
keeps the dead cargo (still 0x100) under it. The cargo (vehicle, death
countdown 10) is removed after 10 ticks; because of 0x400000 its removal
does **not** clear any cell bits (`(flags & 0x400040) == 0` test in state
0x11), which is right because the pick-up already freed its cell. Both
disappear without leaving occupancy behind.

### 4.2 The unit dies or changes while the carryall is on the way

- Unit dies (0x11) → RequestMonitor: slot 3 (+ SetState side effects).
  The carryall sees slot 3 in state 7 or 0x15 and turns back (clears
  0x200; in 0x15 it climbs first). If it reaches the pick-up before the
  monitor runs, PickUp fails (not 0x12) → 0x4000, climb, idle.
- Unit re-ordered by the player (leaves 0x12): the same, through the
  "not in 0x12 / 0x13" rule.
- Timeout (640 ticks): the unit gets its saved state, slot 3, the carryall
  turns back.

### 4.3 Destination destroyed

Nothing in the ferry tests the destination object. The slot's "to" cell is
a plain cell: the carryall drops there (or at the spiral cell). The unit's
own state then re-targets: 0xd picks the nearest refinery, 0x18 the
nearest pad (doc 13 §1.3, doc 21 §4), 0xb drives to its spice cell.

### 4.4 Cargo killed while carried (not by the carryall's death)

Nothing in 1.06 targets a carried unit (no cell bit, attackers drop such
targets). If a path did kill it alone: it gets 0x400000 and 0x11,
RequestMonitor marks the slot 3 then frees it, but the loaded carryall
does not check the slot and still "drops" it later, setting the vehicle
bit on the drop cell for a unit whose removal will not clear it.

**No 1.06 path does that** (verified): KillUnit `FUN_0049cde0` has five
callers. The damage code `FUN_0049d2e0` skips units with 0x100
(`0x49d80e`), the crush `FUN_0049f8f0` skips 0x400140, the entrant's own
death (`0x49a495`) is a unit in state 0x17, the recursion (`0x49ce62`) is
the carryall's death, and the side removal `FUN_0046e6b0` (network
games) kills the carryall in the same call, so the pair falls together
(§4.1). Port as is.

### 4.5 No owned carryall left

FindCarryallSlot returns 0xFF when +0x245c0 = 0, so no new requests.
Requests made before the last carryall died stay waiting until the 640-tick
timeout. A dying carryall still counts as owned until it is removed (10
ticks).

### 4.6 One carryall, many harvesters

With one owned carryall and no delivery in flight n = 1: while the
carryall holds slot 0 (states 1/2/4) no other unit can request, so the
others drive. A delivery carryall raises n by one while it lives, so a
second request can then wait up to 640 ticks for the busy carryall.

## 5. Per-tick order (`FUN_00448550`, verified)

1. tick += 1.
2. DoAI for every AI side (`FUN_0041b610`): DoProtection (AddToQueue),
   DoBuildCarryalls (factory order only).
3. Script `FUN_00453b90`, `FUN_00455480`, blooms `FUN_0044ea70`,
   `FUN_004466e0`, `FUN_00446690`.
4. Model update `FUN_0044aca0` → `FUN_00458e90`: every player's units in
   list order (`FUN_00497b90` per unit: requests, waiting, carryall
   states, tail copy; removals after state 0x11).
5. `FUN_0044d0e0` (if `0x4eb008` = 0), `FUN_0044cff0` (shroud reveal).
6. Factories `FUN_004467e0` (a built carryall is created here; its first
   update is next tick).
7. For player `tick & 7`, if active: **RequestMonitor** `FUN_0046d180`,
   `FUN_0046ddc0`, delivery `FUN_0046def0` (delivery carryalls created
   here).
8. `FUN_0044c520`.

## 6. Saved state

Must be saved (our state format, `sim/save_state.cpp`; the original's save
file layout does not matter, because d2k-faithful never reads the
original's saves):

- Player: the 32 slots (all fields, including kind, age, saved state), the
  slot count +0x2688c, the live count +0x2688d, the owned count +0x245c0.
- Carryall: +0x0c, +0x26, +0x29, altitude, flags 0x100 / 0x200 / 0x4000,
  +0x1a turn delay, speed +0x00, +0x55 desired heading.
- Cargo: flags 0x100 / 0x400000, state 0x13, altitude.
- Waiting unit: state 0x12 and its +0x14 follow-up.

## 7. Drawing

Nothing beyond doc 14 §5.5: a carried unit (0x100 without 0x40) is drawn
in part (a) of pass 0 at y − altitude (carryall − 10 px), no shadow; the
carryall body and shadow as any aircraft. The cargo is drawn before every
aircraft body, so it sits under its carryall.

## 8. Open points

All settled on 2026-10-16:

- A map carryall (A5V1 Harkonnen) flies in from the map edge at mission
  start: verified in the listing (§0, §3.1).
- DoProtection never produces a direct lift: verified, it sets no state
  (§0).
- Cells with flag 0x10000000: repair pad outer cells and the heavy / light
  factory exit cells, not the refinery dock (§3.4, corrected).
- The dropped unit at centre + 1 px: MoveStep starts from any position
  (§3.6).
- Nothing in 1.06 kills a carried unit on its own (§4.4).
- Stale slot fields after a player reset are never read (§1.1).
