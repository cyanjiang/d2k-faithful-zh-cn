# 38: Player orders for turret buildings (1.06)

Everything the player can do with a selected own turret building: how it
is selected, the cursor, the clicks that order it to attack, the order
records 0x1E / 0x1F and their handlers, the building states 4 / 5 / 6,
and the Stop key. Written for a line-by-line port.

Conventions as in doc 30 / 37: `b+…` = building fields (doc 12, doc 18
§7), `brec+…` = building record (`0x6dc540 + rec · 0x10c`), `u+…` = unit
fields (doc 09), `player+…` = `FUN_004a4660(side)`. Objects of a side live
in one slot array: slot = `player + 0x30 + idx · 0x94`, slot `+0x90` = 1
for a unit, 2 for a building (so unit and building indices share one
space). "Verified" = read in the decompile, checked in the disassembly
(`re/disasm.py`) where it matters.

Keys: `0x797e88` = Shift, `0x797e89` = Ctrl, `0x797e8a` = Alt held.
Alliance table `0x798830[player · 8 + owner]` (0 = friendly, own side
included; anything else counts as a target here).

**Turret building** below = a building whose record has a turret element,
`brec+0x18 ≠ −1` (`0x6dc558`). That is the medium and large gun turrets
(record kind `brec+0x5C` = 0x10). This is the test every function in this
doc uses; doc 10 called `FUN_004a5860` "kind 0x10 selected", which is
equivalent in the 1.06 data but not the literal test.

## 0. Function map

| Address | Meaning |
|---|---|
| `FUN_004a5860()` | first **own** building with `b+0x35` (selected) and `brec+0x18 ≠ −1`, in list order; else 0. No state test (a dying selected turret still counts) |
| `FUN_004a5830()` | any own building selected (`b+0x35`) |
| `FUN_004a57c0()` | any own unit selected (`u+0x19`) |
| `FUN_004a58b0()` | any own selected unit with a primary weapon (`urec+0x0C ≠ −1`) |
| `FUN_004a5640(cx, cy, &b, &owner, &idx)` | building on a cell: needs cell flag 0x10; scans the list of the cell owner (`cell & 7`), skips kind 0xF, `FUN_004a5570` footprint test |
| `FUN_004a5270(px, py, &owner, &idx, enemy)` | unit under a pixel (doc 18 §3.4); cloaked enemy units are not returned |
| `FUN_00497140(b, cx, cy, air)` | building range test to a cell (doc 18 §3.3), §5.1 |
| `FUN_00497490(b, tb, &cx, &cy)` / `FUN_00497530(b, tb)` | closest cell of building tb (`FUN_00497390`), then `FUN_00497140` to it; returns the test |
| `FUN_00459d20(side, towner, tidx)` | posts order **0x1E** (turret attacks a unit) |
| `FUN_00459de0(side, towner, tidx)` | posts order **0x1F** (turret attacks a building) |
| `FUN_00459c40(side)` | Stop (S): order 0x1C for units, then **0x1D** for buildings (§6) |
| `FUN_00457810` case 0xB / 0xC / 0xA | handlers of orders 0x1E / 0x1F / 0x1D (switch on type − 0x13) |
| `FUN_0049e120(b, s)` | building SetState: refuses (returns 0) when `b+0x30 == 0x11`, else `b+0x30 = s` |
| `FUN_0049b7f0` | building update; switch on `b+0x30`: 2, 4, 5, 6, 0x11 (doc 18 §6.1) |
| `FUN_004975d0` / `FUN_004976b0` / `FUN_00497090` | state 4 / 5 / 6 steps (§5) |
| `FUN_00496790` | state 2 turret auto-fire (doc 18 §6.1) |
| `FUN_00496030` | building fire (doc 18 §6.2; power rule there) |
| `FUN_0049f310(u)` | flash a unit: `u+0x1D = 7` |
| `FUN_0049f320(b)` | flash a building: `b+0x38 = 7` |

Call sites of the turret functions (all in `FUN_004433a0`, verified by
xrefs; nothing else, in particular **no AI code**, posts 0x1E / 0x1F):

| Site | Use |
|---|---|
| `0x4438b7` | cursor choice (§2) |
| `0x44593d` / `0x44594f` / `0x445979` | click on a unit (§3.2) |
| `0x445b98` / `0x445bd1` | click on a building (§3.3) |

## 1. Selection (verified)

- **Click on an own or allied building** with record flag 0x200
  (`brec+0x58`): `FUN_004a5ab0(owner, idx)` = deselect everything of every
  side (`FUN_004a5790`), then select the building if `b+0x30 ≠ 0x11` and
  `b+0x28 & 6 == 0`. **The deselect happens even when the building is
  then refused** (being built or sold). Turrets have flag 0x200.
- **At most one building is ever selected.** Only `FUN_004a5ab0` selects
  buildings, and it deselects all first. Box select (`FUN_004a5ae0`),
  control groups (doc 25 A.3), Next (N) never select buildings. So the
  "list of selected turrets" in order 0x1E / 0x1F has 0 or 1 entry.
- **What deselects a turret:**
  - right button in the playfield (`FUN_004a5790`);
  - any plain click that selects a unit or building (`FUN_004a5a50(…, 0)`,
    `FUN_004a5ab0`), including the "view" of an enemy unit and the
    out-of-range clicks in §3;
  - a box that contains at least one own unit (doc 10 §2; an empty box
    keeps the turret selected);
  - a control-group recall (doc 25 A.3: buildings always deselected);
  - Next unit (N, `FUN_004a5de0` deselects all).
- **Shift + click on an own unit keeps the turret selected.**
  `FUN_004a5a50(owner, idx, 1)` only toggles `u+0x19`; it does not touch
  buildings. So a turret and units can be selected together (quirk,
  kept). Effects: the cursor uses the units' table (§2 is skipped); a
  click on an enemy unit orders only the units (§3.2); a click on an
  enemy building (or Ctrl on any building) orders **only the turret** and
  the units get nothing (§3.3); S stops both (§6).
- The selected flag is not cleared when the building starts dying;
  `FUN_004a5860` still returns it (cursor 2 can show for a dying turret),
  but `FUN_00459d20` / `FUN_00459de0` skip `b+0x30 == 0x11`, so no order
  is posted.

## 2. Cursor (`FUN_004433a0`, call at `0x4438b7`, verified)

Only in mode 0, not on shroud, inside the playfield, and **only when no
own unit is selected** (`FUN_004a57c0` false). `t = FUN_004a5860()`. The
cell flags are `f`, the cell owner `o = f & 7`.

```
if FUN_004a57c0(): → units' cursor table (doc 10 §5)
t = FUN_004a5860()
if t == 0: → "another building selected" / "nothing selected" (doc 10 §5)
if f & 0x3f8 == 0: return 0                            // empty cell: arrow, also with Ctrl
if alliance[player][o] != 0 or Ctrl:
    if FUN_004a5640(cx, cy, &b, &owner, &idx):         // a building on the cell
        if b.selected and owner == player: return 0    // the selected building itself
        return FUN_00497530(t, b) ? 2 : 0
    else:                                              // unit bits only
        return FUN_00497140(t, cx, cy, air = 0) ? 2 : 0 // range to the mouse cell
else:                                                  // friendly cell, no Ctrl
    if f & 0x400 == 0: return 5                        // unit, or a non-wall/turret building
    if FUN_004a5640(...) and brec(b).kind == 0x10: return 5
    return 0                                           // a wall
```

Cursor ids: 0 arrow, 2 red attack ring, 5 select brackets (doc 10 §5).

Notes:
- "Enemy or Ctrl" uses the **cell owner**, not the unit's owner.
- Over a unit cell the range test is to the mouse cell; the unit lookup
  `FUN_004a5270` is **not** called, so a cloaked enemy's cell still shows
  2 while the click does nothing (§3.4). Quirk, kept.
- Ctrl over an own unit in range shows 2; Ctrl over an own (not
  selected) building in range shows 2; the selected turret itself shows 0
  with Ctrl and 5 without.
- **No ground attack:** an empty cell gives 0 with or without Ctrl.
- Corrects doc 10 §5 ("empty cell → 2 if in range" is really "a cell with
  unit bits and no building"; "friendly other building → 0" is only for
  0x400 cells, i.e. walls; other friendly buildings give 5).

## 3. Left click (mode 0, `FUN_004433a0`, ends at `0x445d99`, verified)

The click resolves in this order: shroud, then the unit under the pixel
(`u = FUN_004a5270(px, py, &owner, &idx, enemy)` with
`enemy = (o ≠ player and f & 0x3e8 ≠ 0)`; an enemy unit with flag 4,
cloaked, counts as none), then a building on the cell, then the ground.
Turret parts in bold; everything else as in doc 10 §3.

### 3.1 Ground (no unit, no building)

```
if not Ctrl: if FUN_004a57c0(): move order + marker + ack voice
else:        if FUN_004a58b0(): attack ground (FUN_004598d0) + marker + ack voice
```

**A selected turret is ignored; it stays selected.** There is no turret
ground-attack order in 1.06 (state 6 exists but is never entered, §5.4).

### 3.2 A unit (`0x44593d`..)

```
if owner != player or Ctrl:
    if alliance[player][owner] == 0 and not Ctrl:
        FUN_004a5a50(owner, idx, 0)                // select the ally (turret deselected)
    elif FUN_004a57c0():                           // units selected: they attack (doc 10)
        FUN_0049f310(u); FUN_004596f0(...) / Alt crusher move; ack voice
    else:
        t = FUN_004a5860()
        if t == 0:                       FUN_004a5a50(owner, idx, 0)   // view it
        elif FUN_00497140(t, cx, cy, 0):                              // 0x44594f
            FUN_0049f310(u)                         // flash the target unit
            FUN_00459d20(player, owner, idx)        // order 0x1E
        else:                            FUN_004a5a50(owner, idx, 0)   // out of range: select / view it
    // no voice on any of these turret paths (jumps to 0x445d99)
else: own unit, no Ctrl: Shift toggles (turret kept), else deploy specials /
      exclusive select + select voice (doc 10)
```

- The range test is from the turret to the **clicked cell** (`px >> 5`,
  `py >> 5`), ground only (`push 0` at `0x445944`, verified in the
  disassembly; the decompiler drops that argument).
- Ctrl on an own unit in range: order 0x1E against the own unit
  (accepted by the handler, §4.1; the turret shoots it). Out of range:
  the own unit is selected.
- `FUN_004a5a50(…, 0)` refuses units with flags 0x140, factory-exit units
  not yet stepping, and dying units; then **nothing changes** (the turret
  stays selected, since the deselect is inside the accepted branch).

### 3.3 A building (`0x445b98`..)

```
if owner == player and not Ctrl: own-building rules (doc 10 §3: dock,
    repair pad, primary, else FUN_004a5ab0 if brec flag 0x200)
    // clicking the selected turret again: FUN_004432f0 is false for kind 0x10, nothing
elif alliance[player][owner] == 0 and not Ctrl:
    if brec flag 0x200: FUN_004a5ab0(owner, idx)     // select the allied building
else:                                                // enemy, or Ctrl on any building
    t = FUN_004a5860()                               // 0x445b98, tested BEFORE the units
    if t == 0: units / nothing selected rules (doc 10 §3, doc 30)
    elif FUN_00497530(t, b):                         // range to b's closest cell
        FUN_0049f320(b)                              // flash the target building
        FUN_00459de0(player, owner, idx)             // order 0x1F, 0x445bd1
    elif brec flag 0x200:
        FUN_004a5ab0(owner, idx)                     // out of range: select it
    // else nothing (turret stays selected); no voice in any case
```

- Ctrl on the selected turret itself: in range (distance 0), so the
  flash and order 0x1F are posted even though the cursor shows 0; the
  handler then skips the turret because it is its own target (§4.2).
- Ctrl on another own building in range: order 0x1F against it (the
  turret attacks its own base).
- With units also selected (Shift quirk, §1): only the turret is ordered.

### 3.4 Cloaked enemy units

Cursor 2 can show over them (§2); the click finds no unit, then no
building, so §3.1 applies and nothing happens.

## 4. Orders 0x1E / 0x1F (verified)

### 4.0 Record (built by `FUN_00459d20` / `FUN_00459de0`)

| Off | Size | Value |
|---|---|---|
| +0x08 | u32 | `gGameTicks` (`0x5173f4`) |
| +0x18 | u16 | count |
| +0x20 | u16 | target index |
| +0x22 | u8 | target side |
| +0x23 | u8 | type 0x1E / 0x1F |
| +0x24 | u8 | ordering side |
| +0x26 | u16[] | building indices (`b+0x8C`) |

List: the own buildings in list order with `b+0x35` set, `b+0x30 ≠ 0x11`
and `brec+0x18 ≠ −1`, at most 64 (in practice 0 or 1, §1). Posted with
`FUN_0045bf00` (the normal order queue) only if the count is not 0.

### 4.1 Order 0x1E, attack a unit (`FUN_00457810` case 0xB, SetState at `0x4585cd`)

```
P  = player(order.side)                           // fatal error if null
ts = slot(order.tside, order.tidx)
if ts.kind (+0x90) != 1: return                   // not a unit
if ts.flags (+0x08) & 0x400140: return            // being removed / carried / aircraft
for each id in order.list:
    s = slot(P, id)
    if s.kind == 2 and brec(s).turret != -1
       and (order.side != order.tside or id != order.tidx)   // not itself
       and FUN_0049e120(s, 4):                    // not dying → state 4
        s+0x3C = order.tside; s+0x2C = order.tidx
```

No range test and no alliance test here: any side's unit is accepted,
the own side's included. Aircraft are refused (flag 0x40).

### 4.2 Order 0x1F, attack a building (case 0xC, SetState at `0x4586c8`)

Same, with: the target slot must have `+0x90 == 2` (no dying test, no
flags test), and the new state is **5**.

### 4.3 Refusals, summarised

The order changes nothing when: the target slot is not the right kind;
(0x1E) the target unit has flags 0x400140; the listed building is not a
building slot, has no turret, is the target itself, or is dying. The
order does **not** check range or power.

## 5. Building states (`FUN_0049b7f0`, verified)

Every tick, before the switch (doc 18 §6.1): reload `b+0x37` −1 if not 0;
then the target check:

```
if b+0x2C != 0xFFFF:
    k = slot(b+0x3C, b+0x2C).kind
    if k == 1:
        u = FUN_004a46b0(side, idx)
        drop if u != 0 and (u.state == 0x11 or u.flags & 0x200004
                            or (u.flags & 0x100 and not u.flags & 0x40))
        (u == 0 keeps the target)
    elif k == 2:
        bb = FUN_004a4740(side, idx)
        drop if bb != 0 and bb.state == 0x11      (bb == 0 keeps it)
    else: drop                                     // freed slot
    drop: b+0x2C = 0xFFFF
```

Then `switch (b+0x30)`:

```
case 2:  if brec+0x0C (primary) == -1: break
         if (idx & 7) != (tick & 7) and b+0x2C == 0xFFFF: break
         FUN_00496790(b, side, idx, &b+0x3C, &b+0x2C)
case 4:  if !FUN_004975d0(b, side, idx): SetState(b, 2)
case 5:  if !FUN_004976b0(b, side, idx): SetState(b, 2)
case 6:  FUN_00497090(b, side, idx)            // never leaves state 6
case 0x11: death (doc 18 / 21)
default: fatal "Invalid building state"
```

States 4 and 5 run **every tick** (no 8-tick slot). Going back to state 2
**keeps** `b+0x3C / b+0x2C`.

### 5.1 Range test `FUN_00497140(b, cx, cy, air)`

```
w1 = brec+0x0C (primary), w2 = brec+0x0D (secondary)
if w1 == -1: return 0
r1 = bullet[w1].range (+0x18 at 0x782080), r2 = w2 != -1 ? bullet[w2].range : 0
aa1 = bullet[w1]+0x14, aa2 = w2 != -1 ? bullet[w2]+0x14 : 0   // anti-air bytes
if not air:  r = max(r1, r2)
elif aa1:    r = (aa2 and r2 > r1) ? r2 : r1
elif aa2:    r = r2
else:        return 0
ox = (b.x >> 16) + 16;  oy = (b.y >> 16) - brec+0x86 + 16       // top-left cell centre
tx = cx * 32 + 16;      ty = cy * 32 + 16
return (ox - tx)^2 + (oy - ty)^2 <= r^2      // FUN_004970f0, low 16 bits of each value
```

`FUN_004970f0` masks each coordinate and r to 16 bits before
subtracting/squaring (harmless on real maps).

### 5.2 State 4, ordered attack on a unit: `FUN_004975d0`

```
if b+0x2C == 0xFFFF: return 0
if b+0x2C > 999: fatal "AttackUnit: unit->enemyIndex > kMaxUnitsPer..."
s = slot(b+0x3C, b+0x2C)
if s.kind != 1 or s.state (+0x10) == 0x11: return 0
air = s.flags & 0x40
if not FUN_00497140(b, s+0x4A, s+0x4B, air): return 0     // its current cell
if b+0x37 (reload) != 0: return 1
FUN_00496030(b, side, idx, s.x >> 16, s.y >> 16, b+0x3C, b+0x2C, kind 1)
return 1
```

So: in range → keeps state 4 and fires at the unit's pixel each time the
reload allows (and the power rule of `FUN_00496030` allows: the large
turret turns and fires only at ≥ 100 % power, doc 18 §6.2; with low power
it **stays** in state 4 doing nothing). Out of range or gone → state 2.

### 5.3 State 5, ordered attack on a building: `FUN_004976b0`

```
if b+0x2C == 0xFFFF: return 0
if b+0x2C > 999: fatal (same text)
s = slot(b+0x3C, b+0x2C)
if s.kind != 2 or s.state (+0x30) == 0x11: return 0
if not FUN_00497490(b, s, &cx, &cy):                 // closest cell, range
    fatal "Building out of range - should not happen"  // message box, exit
    return 1                                          // (not reached in retail)
if b+0x37 != 0: return 1
FUN_00496030(b, side, idx, cx*32+16, cy*32+16, b+0x3C, b+0x2C, kind 2)
return 1
```

- A state-5 turret only leaves state 5 when the target is gone / dying
  (or the slot changes kind). Buildings do not move, so the target stays
  in range.
- The fatal path is reachable only if the target slot is freed and reused
  by a new building out of range while the turret is still in state 5.
  The building head (`0x49b90e..0x49b983`) runs in every update before the
  state switch and drops a target that is dying or whose slot is not a unit
  / building; every target is in state 0x11 for at least one holder update
  before its slot is freed (docs/re/50 section 5), so a target held in state
  5 is always dropped first. The one way in is an order: case 0xc tests
  only that the record's slot is a building (no state, no range), so a
  turret order posted on a building in the tick it is sold (freed the next
  tick) runs on whatever building took the slot by then, e.g. through a
  place order (0x26) of that side queued earlier in the same batch; out of
  range, the fatal message box fires. Port: a crash fix, return 0 (back
  to state 2); allowed by default (pure crash fix). Our records resolve the
  slot the same way (docs/re/50 section 6).
- `FUN_00497390` (closest cell) is doc 18's squared-distance version: a
  4×4 scan of `brec+0x54` occupancy bits (`0x6dc594`), strictly smaller
  wins, but a best distance of 0 lets every later cell overwrite it
  (`d < best or best == 0`). Quirk, kept.

### 5.4 State 6, attack a cell: `FUN_00497090`

```
if FUN_00497140(b, b+0x14, b+0x15, 0) and b+0x37 == 0:
    FUN_00496030(b, side, idx, b+0x14*32+16, b+0x15*32+16, 0xFF, -1, kind 0)
return 1                                       // stays in state 6 forever
```

**Unreachable in 1.06:** every `FUN_0049e120` call passes 2, 4, 5 or
0x11 (`0x46d8b7`, `0x46d989`, `0x49b888`, `0x44b1d8`, `0x49cdc7`,
`0x49bce1`, `0x457729`, `0x4585cd` = 4, `0x4586c8` = 5), and a byte scan
for `mov dword [reg+0x30], 6` finds nothing. The only register stores to
a building's `+0x30` are create (`0x456416`, = 2) and `FUN_0049e120`
itself (`0x49e131`); a capture copies the 0x7C bytes then sets 2
(`0x46d989`), and a load restores saved records, which never hold 6. Port
it for completeness but nothing enters it.

### 5.5 Back in state 2 with a kept target (`FUN_00496790`)

After a state-4 / 5 order ends (out of range, or Stop, §6), the target
fields remain. In state 2, with `b+0x2C ≠ 0xFFFF`:

- the 8-tick slot test is skipped, so the routine runs **every tick**;
- if the stored side **≠ the turret's side** (no alliance test):
  - a unit slot: range to its pixel cell `(x>>16)>>5, (y>>16)>>5`, ground
    only; in range → fire at it (kind 1), return;
  - a building slot whose record kind is 0x10 (a turret): closest cell,
    range; in range → fire (kind 2), return;
- otherwise (own side, a non-turret building, out of range): the normal
  search (units, then buildings); a hit replaces the stored target; **no
  hit leaves the old target stored** (so the routine keeps running every
  tick).

Consequences: a Ctrl-ordered attack on an **allied or neutral** unit
continues in state 2 whenever it is back in range (and after Stop); an
order against an own unit stops for good once the unit leaves range.

## 6. Stop (S) and Guard (G)

`FUN_00459c40(side)` (verified):

```
rec.type = 0x1C; n = 0
for own units: if selected and state != 0x11 and n < 64: list[n++] = id
if n: post(rec)                            // count = n
rec.type = 0x1D                            // the list is NOT reset
for own buildings: if selected and state != 0x11 and n < 64: list[n++] = id
if n: post(rec)                            // units' ids + buildings' ids
```

- Order 0x1D carries the unit ids too (quirk); case 0xA (`FUN_00457710`)
  acts only on slots with `+0x90 == 2`, so they are ignored, but with 64
  units selected no building fits.
- Case 0xA: for each building slot, `FUN_0049e120(b, 2)` (not if dying).
  **The target fields are not cleared**, so §5.5 applies: a turret
  stopped while its foreign target is in range keeps firing at it.
- No voice for buildings (the unit order has none either, doc 25 A.7).
- Guard (G, order 0x18) and Retreat (R, 0x1A) list units only
  (`FUN_00459980` / `FUN_00459ab0`); they never touch turrets.
- Scatter (X, 0x19) units only. Primary (P, 0x20) for the selected
  building; turrets cannot be primary (`FUN_004432f0`: kind 0x10 is not
  in its list).

## 7. Open points

- The flash counters `u+0x1D` / `b+0x38`: set to 7 by `FUN_0049f310` /
  `FUN_0049f320` and to 0 at create (`0x4559c5` / `0x4564ef`), counted
  down only by the draw `FUN_00428e30` (`0x42a56d`, `0x42a582`,
  `0x429f4c`); the model's checksum dump zeroes and restores them
  (`0x459169..0x4591de`). Drawing only (docs/re/44).
- §5.3 fatal path: only through an order on a sold building whose slot a
  new building took within the 2-tick latency (§5.3); kept as a crash fix.
- State 6: no writer (§5.4).
- **Confirmed in the original by the user (2026-10-16):** a selected
  turret Ctrl-clicked on an own unit in range fires on it (no voice), and
  S with an enemy in range does not stop it firing. Ours does both
  (§3.2, §6).

## 8. Port status

Ported (save version 11, "turret orders"): `Building::state`,
`World::selected_turret` (`FUN_004a5860`), the Shift / refused-building
selection rules (`World::select_unit`, §1), the turret cursor
(`GameScreen` `turret_or`, §2), the unit / building clicks with a turret
(`GameScreen::turret_click_building`, §3.2 / §3.3), orders 0x1E / 0x1F
(`World::order_turret_attack_unit` / `_building`, §4), states 4 / 5 with
the air range rule and the closest cell (`World::tick_building_combat`,
`building_in_range_of`, §5.1..§5.3; the fatal path as a crash fix), the
kept target in state 2 (§5.5), Stop keeping the target with the shared
64-entry list (`World::order_stop`, `World::capture_selection`, §6) and
the flashes (`flash = 7`, docs/re/44). State 6 has no writer (§5.4).
