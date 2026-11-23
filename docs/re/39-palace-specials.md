# 39: Palace specials, saboteur and Fremen (1.06)

Written for a line-by-line port of the two palace specials that are
**units**: the Ordos **SABOTEUR** (unit record 23, kind 4) and the
Atreides **FREMEN** (unit record 21, kind 12). The airstrike (kind 9) is
in doc 35. The Death Hand (kind 10) is not covered here.

Both specials charge in a unit production slot like the airstrike
(doc 35 §2). When the charge is ready, the player clicks the icon and the
units appear at once at the palace exit. No target is chosen. The AI
calls them in through DoSpecialWeapons and later puts them in groups of
their own.

Conventions as in docs 33 / 35: `unit+…` are object fields, `player+…`
is `FUN_004a4660(side)`, `urec+…` is `0x77e250 + rec · 0x100` (kind at
+0x2D, build rate at +0x20), `ai+…` is the side's AI block (doc 22), and
`group g +…` is `ai + g · 0x2C + …` (doc 23 §3.1). A unit slot is
`player+0x26530 + i · 0x14`: +0 item, +2 progress (0x5A00 = done), +0x10
hold, +0x11 **used**.

"Verified" means it was read from the decompile, and from the
disassembly where it matters. Every open point was settled against the
exe on 2026-10-16 (§6).

Main functions:

| Address | Name used here | Role |
|---|---|---|
| `FUN_004466e0` | AutoStartSpecials | (re)starts kinds 9 / 10 / 4 / 12 every tick (doc 35 §2.1) |
| `FUN_004467e0` | DoIconBuild | slot progress; ready text `SaboteurReady` / `FremenReady` |
| `FUN_00442a00` | BuildFactor | F = 100 × difficulty / 100 for kinds 9 / 4 / 12 |
| `FUN_004433a0` | sidebar / playfield left-up | icon click on a ready special |
| `FUN_0045a440` | PostSpecialOrder | posts order 0x30 (side, kind, x, y) |
| `FUN_00457810` case 0x1d | ProcessOrder 0x30 | clears the ready slots, creates the units |
| `FUN_00442c20` | FindFactory | palace and exit cells (doc 12 §8.1) |
| `FUN_00455870` | CreateUnit | doc 12 §8.3 |
| `FUN_004544e0` | LoadVars | `bin\VARS.BIN`, incl. **NumberOfFremen** `0x6b8854` |
| `FUN_00422270` | DoSpecialWeapons (AI) | cases 0, 1, 2 (this doc), 3 (doc 35) |
| `FUN_00422730` | ItemReady (AI) | doc 35 §10 |
| `FUN_00422bf0` | PalaceExitFree (AI) | `FUN_00442c20(rec, side, …, check 1) != 0` |
| `FUN_0041c3e0` | GroupNewUnits (AI) | counts ungrouped kinds 4 / 12, calls DoFremen / DoSaboteur |
| `FUN_00422a80` | DoFremen (AI) | the Fremen attack group (job 5) |
| `FUN_00422900` | DoSaboteur (AI) | the saboteur group (job 0x12) |
| `FUN_0041d070` | MonitorGroups (AI) | job 0x12: when the saboteur turns on stealth |
| `FUN_0041c3a0` | FindFirstFreeGroup | first g in 1..31 with group +0x52 = 0, else 0 |
| `FUN_0041c340` | GetGroupStrength(g, job) | sum of GetUnitStrength(u, job) (doc 23 §3.2) |
| `FUN_00420a70` | GetAveragePositionOfGroup | unsigned mean cell |
| `FUN_0041bbb0` | GetBestBuilding(side, x, y, f) | doc 23 §4.4 |
| `FUN_00421710` | MergeGroups(g, into) | every unit of g → `into`; g +0x52 = 0 |
| `FUN_0041cdb0` / `FUN_0041cce0` / `FUN_0041fc80` | ClearUnits / SelectedGroup / CommenceOperation | doc 23 §3.3 |

## 1. Data (verified)

| Item | Value | Source |
|---|---|---|
| SABOTEUR | record **23** = `0x6d60ec` (Templates.bin block `0x6d60d8` byte 0x14); kind 4; houses O; tech 5; prerequisite ORDOS_PALACE; cost 0; **build rate 5**; hp 400; no weapon | dump_templates |
| FREMEN | record **21** = `0x6d60ed` (byte 0x15); kind 12; houses A; tech 6; prerequisite ATREIDES_PALACE; cost 0; **build rate 5**; hp 700; FREMEN_S / FREMEN_L | dump_templates |
| ATREIDES_PALACE / ORDOS_PALACE | building records 56 / 58, houses AF / OSM; 3 × 3 cells; spawn cell (1, 1), move cell (0, 2) | doc 12 §8.1 |
| **NumberOfFremen** | byte `0x6b8854` = VARS.BIN offset **0x3C** = **2** in 1.06 (text default 4) | `FUN_004544e0` (`0x4545a6` default, `0x454a48` text path); name table `0x4e2730` entry 15 |

The game reads the binary VARS.BIN (doc 13 §7), so **a palace call-in
gives 2 Fremen**, not 4. The byte is read only by the order 0x30
handler (`0x458c90`, `0x458cc6`).

The palaces have no record flag 0x40 (doc 12 §7.3: only the Harkonnen
palace animates), so flag 0x10 on an Atreides / Ordos palace has no
visible effect.

## 2. Charging (verified)

The same path as the airstrike (doc 35 §2), with these values:

- **AutoStartSpecials** (`FUN_004466e0`) calls StartUnit for every kind
  4 / 12 record in the side's unit build list, every tick. So the slot
  restarts on the tick after the special is used. The build list has
  the saboteur for an Ordos side with an Ordos palace and tech ≥ 5, and
  Fremen for an Atreides side with an Atreides palace and tech ≥ 6
  (normal build-list rules, doc 11).
- **Factory:** `FUN_00442c20(rec, side, …, check 0)`. The palace slots
  are tried in this order: the own house's palace first, then the other
  two (doc 12 §8.1), and the house masks must match. No palace means the
  slot is cleared, with no message.
- **Step** per tick = rate × F / 100, with F = `FUN_00442a00` = 100 ×
  difficulty / 100. Power and the palace count do not matter.
  Difficulty (`FUN_00442960`): local player easy 125, normal 100, hard
  75; every AI side 100. Rate 5 gives:

  | | F | step | ticks to 0x5A00 (23040) | at 20 ticks/s |
  |---|---:|---:|---:|---:|
  | easy | 125 | 6 | 3840 | 3 min 12 s |
  | normal / AI | 100 | 5 | 4608 | 3 min 50 s |
  | hard | 75 | 3 | 7680 | 6 min 24 s |

  (`0x59ff < step + progress` → progress = 0x5A00; all three divide
  exactly.)
- Cost 0: the payment (`FUN_0046c720`) always passes.
- The palace gets flag 0x10 every tick while progress < 0x5A00.

### 2.1 Ready (`FUN_004467e0`, `0x446a9b`..`0x446b25`, verified)

```
progress = 0x5A00
kind 9:  if side == local: TextMessage("AirStrikeReady")
kind 4:  if side == local: TextMessage("SaboteurReady")       // 0x4e001c
kind 12: if side == local: TextMessage("FremenReady")         // 0x4e0010
kind 10: if side == local: voice H_/O_/S_DEATHREADY
TextMessage(s) = FUN_00425720(FUN_0048dcc0(FUN_0048dc10(s, 1, -1)))
→ jump to the next slot (0x446b25)
```

- No voice for kinds 9 / 4 / 12. The message is shown only to the local
  player. AI sides get nothing.
- **The factory flag 0x10 is NOT cleared** for any special. Only the
  normal-unit branch clears it (`0x446c..`, `& 0xffffffef`). The waiting
  branch (progress = 0x5A00) also skips specials and does not touch the
  flag. So the palace (and, for the airstrike, the high-tech factory)
  keeps flag 0x10 while a charge is ready. The flag is cleared only on
  hold (`LAB_00447086`). **Correction to doc 35 §2.2**, which says the
  flag is cleared. A visible effect exists only for buildings with
  record flag 0x40: the high-tech factory keeps animating while an
  airstrike is ready.

## 3. Player (verified)

### 3.1 Icon click (`FUN_004433a0`, unit column, left button up)

```
rec = unit_list[row + scroll]
if urec[rec].kind in (9, 10, 4, 12):
    found = false
    for each of the 10 slots:
        if slot.item == rec and slot.progress == 0x5A00 and slot.used == 0:
            found = true
            if kind in (4, 12): slot.used = 1
    if found:
        kind 9  → mode 0x516438 = 4
        kind 10 → mode 5
        kind 4  → PostSpecialOrder(local, 4, 0, 0)       // FUN_0045a440
        kind 12 → PostSpecialOrder(local, 12, 0, 0)
else: StartUnit order 0x27 (FUN_0045a0b0)
```

- x = y = 0. The order handler does not read them for kinds 4 / 12.
- No cursor mode, no sound and no voice on the click. Right clicks on
  special icons do nothing (doc 11).
- The slot search uses the **record**, but the order handler uses the
  **kind**.
- A click while the slot is not ready, or after it was already used,
  does nothing.

### 3.2 Order 0x30 (`FUN_00457810` case 0x1d, `0x458b..`..`0x458cd0`)

```
P = player(order.side)                       // "Invalid side" error if 0
cleared = false
for each of the 10 slots:
    if item ≥ 0 and urec[item].kind == order.kind and progress == 0x5A00:
        item = −1; used = 0; cleared = true  // used is not checked
if !cleared: return
switch order.kind:
  10: Death Hand (not covered)
  9:  airstrike (doc 35 §3.4)
  4:  if FindFactory(0x6d60ec /*23*/, side, &x1, &y1, &x2, &y2, check 1):
          CreateUnit(side, 23, x1, y1, x2, y2, 0, 0)          // FUN_00455870
  12: if FindFactory(0x6d60ed /*21*/, side, &x1, &y1, &x2, &y2, check 1):
          for i in 0 .. NumberOfFremen − 1:                    // byte 0x6b8854 = 2; skipped if 0
              CreateUnit(side, 21, x1, y1, x2, y2, 0, 0)
```

Consequences:

- **Charge lost quirk:** the slots are cleared **before** the exit test.
  If no palace has a free spawn cell (cell `flags & 0x3E8 ≠ 0`, i.e. any
  unit on the palace's (1, 1) cell), nothing is created, there is no
  message, and the charge is gone. AutoStart restarts it next tick.
  Verified in the listing: after the slot loop the kind 4 / 12 branches
  call `FUN_00442c20(rec, side, …, 1)` (`0x458bf6`..`0x458c13`,
  `0x458c63`..`0x458c80`) and return on 0 (`je 0x458df2`) with nothing
  restored.
- All Fremen use the same cells. Each CreateUnit takes the next free
  infantry sub-slot of the spawn cell (0x200, then 0x100, …, doc 12
  §8.3). The exit test runs once only. With more than 5 copies
  CreateUnit reaches the fatal "Couldn't find free slot" error. This
  cannot happen with the 1.06 value 2.
- A full unit pool makes CreateUnit return 0xFFFF and the loop goes on.
  No check is made.
- **The units are normal units.** CreateUnit (doc 12 §8.3) gives:
  full hp; flags = record flags; group +0x28 = 0xFF; **+0x29 = 0** (for
  the saboteur, that is the stealth charge, doc 32 §1.9);
  +0x2A = +0x2B = 0xFF; the position is the centre of the (1, 1) cell plus
  the infantry sub-slot offset; it faces the move cell; counters
  `+0x24780`, `+0x24788[rec]`, `+0x24628[rec]` += 1. Because spawn ≠ move
  cell, **state 8** (move, then attack) to the palace's (0, 2) cell,
  for every side (the AI state-3 rule only applies when spawn = move).
- Fremen (kind 12): flag **0x10** (stealth ability) at creation
  (`0x455f43`). Flag 4 (cloaked) is not set. The first stealth update
  cloaks them unless something blocks it (doc 32 §1.1, §1.3).
- Saboteur: nothing special at creation. Its stealth needs +0x29 to
  charge to 0xA0 (+1 every 16 ticks, so 2560 ticks), and then the player
  order 0x2F (doc 32 §1.9).
- **No voice and no message** when the units appear: no `*_UNITREADY`,
  because specials never reach the spawn branch of DoIconBuild.

### 3.3 Palace saboteur vs other saboteurs (question 4)

There is no other way to build a saboteur in 1.06. Record 23 is the
only kind-4 record, and its prerequisite is ORDOS_PALACE, so the
barracks never offers it. The palace path differs from a normal factory
spawn only in:

1. The units appear when the order runs, not when progress ends.
2. The exit test runs only once, and a failure loses the charge.
3. No UNITREADY voice.
4. Fremen come in groups of NumberOfFremen.

After CreateUnit, nothing marks the unit as "from the palace".
Map-placed saboteurs (`mission.cpp`) and palace saboteurs behave the
same: +0x29 starts at 0, there are the same orders 0x15 / 0x2F, and the
same state 5 / 0x17 destroy-on-entry (doc 30 §2.7).

## 4. AI (verified)

### 4.1 DoSpecialWeapons (`FUN_00422270`)

The top part and case 3 are in doc 35 §10: not BESERK, `(tick + side) &
0x1F == 0`, `tick ≥ ai+0x1D18`, and one top-level GetBestSideToAttack
call. Then `switch (((side + tick) >> 5) & 7)`:

```
case 1:                                            // saboteur, once per 256 ticks
    if !ItemReady(ai, 4, &slot): break             // FUN_00422730: last slot of kind 4, progress > 0x59FF
    if !PalaceExitFree(0x6d60ec, side): break      // FUN_00422bf0 → FindFactory(23, side, …, check 1)
    if P.slot[slot].used: break
    PostSpecialOrder(side, 4, 0, 0); P.slot[slot].used = 1
case 2:                                            // Fremen
    the same with kind 12 and 0x6d60ed (21)
```

- **No rand()** in cases 1 / 2. Only the top-level GetBestSideToAttack
  draws one.
- The test order is ItemReady → exit free → used. Case 3 is ItemReady →
  used → GetBestSideToAttack. Unlike the player, the AI never loses a
  charge to a blocked exit, unless the exit becomes blocked between
  the post and the next order pass.
- `used` is set on the ItemReady slot only. The order handler then
  clears every ready slot of the kind (§3.2).

**Case 0 (engineers, brief; doc 35 calls it "thumper", but kind 3 is
ENGINEER, record 2):**

```
eng = first unit of the side (list order) with kind 3 and group == base group (ai+0x1D3C)
if !eng: break
g = FindFirstFreeGroup(); if g == 0: break
eng.group = g
n = rand() & 7                                       // one rand(), line 0x185d
for u in side's units from the list start, while n > 0:
    if urec[u].infantry (+0x11) and u.group == base and urec[u].weapon1 (+0x0C) != 0xFF:
        u.group = g; n −= 1
g.+0x38 = g.+0x3C = GetGroupStrength(g, 0x10); g.+0x52 = 1
(x, y) = GetAveragePositionOfGroup(g)
if best == 0xFF: return                              // the group stays in use with its old job (quirk)
b = GetBestBuilding(best, x, y, 2.0f)                // one rand() per building
if b: g.target = b.+0x8C; g.side = best; g.cell = (b.x >> 16) / 32, (b.y >> 16) / 32
      g.sub = 2; g.+0x40 = 0; g.job = 0x10; g.+0x38 = GetGroupStrength(g, 0x10)
      ClearUnits; SelectedGroup(g, all); CommenceOperation(g)    // order 0x15 → capture
else: MergeGroups(g, base)
```

`best` is the value returned by the top-level GetBestSideToAttack call.
The building cell uses b.+0x04, which is the bottom edge (doc 35 §10
quirk). It is used only for substates 7 / 8.

### 4.2 GroupNewUnits additions (`FUN_0041c3e0`)

In the not-BESERK loop (doc 23 §3.4), for a unit with group 0xFF and
`flags & 0x840 == 0`, after the kind 1 / 2 / 6 skip:

```
kind 4:  nSab    += 1; continue      // before the busy test: also counted in state 8
kind 12: nFremen += 1; continue      // never put in the base group, never "busy"
```

After the loop, after the group start and the BESERK = 1 rampage parts:

```
if nFremen > 4 and ai.BESERK == 0: DoFremen(force)       // first
if nSab   > 0 and ai.BESERK == 0: DoSaboteur(force)
if nNewBase > 0: GuardOrder(side)                        // FUN_00459980, order 0x18 for the selected units
```

- The counts are only made when GroupNewUnits runs in full: on its tick
  `(tick & 0x1F) == side` or when forced, and only if FindFirstFreeGroup
  ≠ 0 (otherwise it returns at once).
- `force` is passed on. Both functions do nothing when force = 1 (the
  activation call, doc 23 §3.1).
- With 2 Fremen per call-in, the AI needs **3 call-ins (6 Fremen)**
  before DoFremen runs. Until then the Fremen stand where state 8 left
  them, ungrouped.
- **Selection quirk (verified in the listing):** GroupNewUnits calls
  ClearUnits once, at its start, and never again: not around the start
  of a delivered group (SetGroupToGuard / SelectedGroup(g, 0) /
  CommenceOperation, `0x41c75a`..) and not at its end (the last call is
  the guard order `FUN_00459980`; DoAI's own final ClearUnits follows).
  DoFremen's success path calls ClearUnits, SelectedGroup(g, 1) and
  CommenceOperation and stops there (`0x422a53`..`0x422a67`). DoSaboteur
  ends with ClearUnits (`0x422a6e`) when it acts. So:
  - Without DoFremen / DoSaboteur acting, the guard order goes to the
    units that just joined the base group **and** to the started
    delivered group (selected by SelectedGroup(g, 0)). For a group in
    guard mode 0, SetGroupToGuard (`FUN_0041e490` → `FUN_0041fea0` per
    unit) does ClearUnits, select one unit, guard order, for each live
    unit of the group, so it also deselects the new base units first.
  - When DoFremen succeeds and DoSaboteur does not act, the final guard
    order goes to the **Fremen**. The units that just joined the base
    group get no guard order.
  - When DoSaboteur acts, the guard order selects nobody.

  Orders keep their unit list from the time they are posted (doc 23
  §3.3). In the DoFremen case the queue holds 0x15 (attack building) and
  then 0x18 (guard) for the same Fremen. Ported as is
  (`World::ai_group_new_units`, 2026-10-16: the extra selection clears
  of the earlier port were removed).

### 4.3 DoFremen (`FUN_00422a80(force)`)

```
if force == 1: return
g = FindFirstFreeGroup(); if g == 0: return
for u in side's units: if u.group == 0xFF and urec[u].kind == 12: u.group = g
                                              // no 0x840 / state test here
side2 = GetBestSideToAttack()                 // may rand()
(x, y) = GetAveragePositionOfGroup(g)
b = GetBestBuilding(side2, x, y, 0.25f)       // one rand() per building of side2
if !b: MergeGroups(g, ai.base group); return  // the Fremen join the base group
g.job = 5 (AttackBuilding); g.target = b.+0x8C; g.side = side2
g.+0x50 / +0x51 = (b.x >> 16) / 32, (b.y >> 16) / 32
g.sub = 2; g.+0x40 = 0; g.+0x38 = GetGroupStrength(g, 5); g.+0x52 = 1
ClearUnits; SelectedGroup(g, all = 1); CommenceOperation(g)
                                              // job 5, sub 2 → order 0x15 attack building
```

- `+0x3C` (start strength) is **not** set, and neither are the timer
  +0x34, age +0x44 and flags +0x4C. They keep the values of the group
  slot's last user (FindFirstFreeGroup does not reset a slot). Morale
  (doc 23 §4.7) therefore compares with a stale start strength. Keep
  group records persistent and do not zero them on reuse. Verified:
  nothing resets a freed group. FindFirstFreeGroup (`FUN_0041c3a0`) only
  reads +0x52; the frees (`0x41d31b`, `0x4216e9`, MergeGroups `0x421759`)
  write only +0x52; +0x3C is written only by ActivateAI (`0x41b476`,
  `0x41b4a8`), CreateSplinterGroup (`0x41cb4c`) and DoSpecialWeapons
  case 0 (`0x4223c4`). The initial values come from the .MIS AI block.
- SelectedGroup skips units in state 8 or 0x11. A Fremen still walking
  out of the palace gets no attack order now. It joins later when the
  group is re-ordered.
- Afterwards it is an ordinary job-5 group (doc 23 §4.6): it can turn
  into a Rampaging group, retarget, return to base on low morale, and so
  on.
- `side2 == 0xFF`: GetBestBuilding calls `FUN_004a4660(0xFF)`, which
  shows the fatal "Trying to access side out of range" error
  (`FUN_0042b590`, doc 31). There is no guard as in case 0.
  **Reachable, but only in a corner** (verified): GetBestSideToAttack
  (`FUN_0041cdf0`) returns its stored side while that is still an enemy
  and the countdown runs; else RecalculateSideToAttack (`FUN_0041ce50`)
  scores all 8 sides with one rand() each, `((rand % 40 + 80) · n) / 100`
  (× 4 for a side without an AI), where n grows with an enemy's counted
  buildings and units (3 per unit) and is 1 for every other side, so a
  non-enemy scores 0 or 1. 0xFF comes back only if all 8 scores are 0.
  So it needs an AI whose enemies have (almost) nothing left (in single
  player: the player has just lost everything, before the end-of-mission
  check sees it) and 8 low rolls (about 1 in 256). The exe then stops with the fatal error. We treat it as "no
  target" (MergeGroups into the base group), a crash fix that changes
  nothing else.

### 4.4 DoSaboteur (`FUN_00422900(force)`)

```
if force == 1: return
g = FindFirstFreeGroup(); if g == 0: return
sab = first unit (list order) with group 0xFF, kind 4 and +0x29 ≥ 0xA0     // fully charged stealth
if !sab: return                                   // no rand()
sab.group = g                                     // one saboteur per call
side2 = GetBestSideToAttack()
(x, y) = GetAveragePositionOfGroup(g)
b = GetBestBuilding(side2, x, y, 0.25f)
if !b: MergeGroups(g, base); return               // into the base group
g.job = 0x12; g.target = b.+0x8C; g.side = side2
g.+0x50 / +0x51 = building cell (as above); g.sub = 2; g.+0x40 = 0
g.+0x38 = 100                                     // constant, not GetGroupStrength
g.+0x52 = 1
ClearUnits; SelectedGroup(g, 1); CommenceOperation(g); ClearUnits
```

- CommenceOperation treats job 0x12 like 5 / 0x10 (`FUN_0041fc80`): sub
  2 with a target posts **order 0x15** (attack building). For a saboteur
  that is the walk-in-and-destroy path (doc 30 §2.7, states 5 / 0x17).
- A palace saboteur starts with +0x29 = 0. So the AI holds it ungrouped
  for **at least 2560 ticks** (160 × 16), standing near the palace,
  before DoSaboteur can take it.
- The 0xFF side and stale +0x3C notes of §4.3 apply here too.

### 4.5 MonitorGroups, job 0x12 (`FUN_0041d070`, `0x41d360`..`0x41d41f`)

It runs in the per-group part, after the strength update (doc 23 §4.6
step 4):

```
case 0x12:
    u = first unit of the side (list order) with group == g
    if u:
        near = EnemyUnitInRange(u.x >> 16, u.y >> 16, side, 0xC0,
                                &…, &…, &…, &…, 9, −1, 0, 0, 1)   // FUN_004a4900; always called first
        if (u.+0x2A < 10 or CellDist2(u, g.+0x50, g.+0x51) < 0x40 or near)
           and u.+0x29 == 0xA0 and !(u.flags & 0x10):
            u.flags |= 0x10                       // stealth on; no S_STEALTHUP sound
    fall through to the job-5 code
```

- The saboteur turns on stealth when it is hit (ticks/8 since hit
  < 10), when it is within 8 cells of the target cell (squared distance
  < 64, `FUN_00421800`), or when an enemy unit is within 192 px (worm
  pass enabled, cloaked units not found, doc 18 / doc 32). Its +0x29
  must then be exactly 0xA0. After that +0x29 drains by 4 every 16
  ticks (doc 32 §1.9).
- **The fall-through to the job-5 code** (`switchD_0041d358_caseD_5`):
  `age > 10 and DoStopStartStuff(g, 8, 1) and (rand() & 0x3F) != 0 and
  job == 5` → Rampaging. For jobs 0x12 and 0x10 the **rand() is still
  drawn** (short-circuit order). Only the job test stops the
  conversion. Then MoraleCheck (Morale[0x12] = 100 in every 1.06
  mission AI block) → ReturnToBase on failure, and the substate 7 / 8 /
  4 rules. In substate 4 (target gone) after 3 quiet passes it calls
  AttackBestBuilding (`FUN_0041fee0`), which sets **job 5**. From then
  on the saboteur group is a normal attack group.
- Weights for job 0x12 (SpeedWeight / HPWeight [18]) are 1.0 in every
  active 1.06 mission AI block (checked over the 45 MIS files), so the
  recomputed strength is never 0 while the saboteur lives.

## 5. Corrections to other docs

- doc 35 §2.2: the factory flag 0x10 is **not** cleared when a special
  becomes ready (§2.1).
- doc 35 §10 / §12: DoSpecialWeapons case 0 is the **engineer** capture
  group (kind 3), not a thumper.
- doc 12 §8.4: Fremen copies = `0x6b8854` = **2** (VARS.BIN), and the
  slots are cleared before the exit test (charge lost when blocked).
- doc 32 §4.3: DoFremen takes ungrouped kind-12 units without the
  0x840 / state tests. DoSaboteur exists and takes one charged
  saboteur (job 0x12).

## 6. Open points

Settled on 2026-10-16:

- A blocked palace exit loses the charge: verified in the listing (§3.2).
- The DoFremen / guard-order selection quirk: verified, and the port's
  extra selection clears were removed (§4.2).
- A freed group's +0x3C / +0x34 / +0x44 / +0x4C are never reset (§4.3).
- Side 0xFF in DoFremen / DoSaboteur: reachable only in a corner, kept as
  a crash fix (§4.3).
- **The 1.06 missions with an AI palace special** (survey of every
  campaign mission, golden test in `tests/sim/test_aircraft_specials.cpp`):
  saboteur (Ordos AI, ORDOS_PALACE, tech 7) in A8V1 (side 2) and H8V1
  (side 2); Fremen (Atreides AI, ATREIDES_PALACE, tech 7–8) in H9V1, H9V2,
  O8V1, O9V1 and O9V2 (side 0). The Harkonnen AI's Death Hand (doc 41) in
  A9V1, A9V2, O8V1, O9V1 and O9V2 (side 1). All of these are in the
  side's build list from the first tick.
- Not covered: the Death Hand (order 0x30 kind 10, `FUN_0049e860`, the
  `DeathHandAccuracy` scatter of 2 rand() calls, DoSpecialWeapons case
  4).

## 7. Checklist: our code

| # | Finding | Where | Status |
|---|---|---|---|
| 1 | `NumberOfFremen` = VARS.BIN byte 0x3C (1.06: 2) | `sim/world.hpp` `Vars`, `World::set_vars`, `save_state.cpp` | done (parity row "Palace specials, Deviator") |
| 2 | AutoStart for kinds 4 / 12 | `World::auto_start_specials` | done (`is_special` covers 4 / 12) |
| 3 | Charge step rate 5 × F / 100, F = 100 × difficulty | `World::tick_production` | done (special f = 100) |
| 4 | `SaboteurReady` / `FremenReady` text for the local player, same call as AirStrikeReady | `tick_production` (TODO at the ready branch, `economy.cpp` ≈ l. 648) | done (parity row "Palace specials, Deviator") |
| 5 | Do not clear factory flag 0x10 when a special (9 / 4 / 12) becomes ready | `tick_production`: `buildings_[factory].flags &= ~0x10u` runs for all | done (parity row "Palace specials, Deviator") |
| 6 | Sidebar icon click on a ready unused kind 4 / 12: set `used` on every matching slot, then order 0x30 (player, kind, 0, 0) | `app/sidebar.cpp` `left_up` (TODO "saboteur and Fremen") | done (parity row "Palace specials, Deviator") |
| 7 | Order 0x30 kinds 4 / 12: clear slots first; `find_factory(23 or 21, check = true)`; one `create_unit` or NumberOfFremen of them on the same cells; no voice | `World::order_special` (TODO line) | done (parity row "Palace specials, Deviator") |
| 8 | `create_unit`: Fremen flag 0x10; +0x29 (`load`) = 0; state 8 to the exit; infantry sub-slots | `sim/placement.cpp` | done (check that `load` starts at 0 for new units) |
| 9 | AI DoSpecialWeapons cases 1 / 2: ItemReady → exit free (`find_factory(.., true)`) → used → order (no rand) | `World::ai_special_weapons` | done (parity row "Palace specials, Deviator") |
| 10 | AI case 0: engineer group (rand & 7 escorts, strength with job 0x10, GetBestBuilding f = 2.0, job 0x10, MergeGroups) | `ai_special_weapons` | done (parity row "Palace specials, Deviator") |
| 11 | GroupNewUnits: count ungrouped kinds 4 / 12 (before the busy test), then DoFremen (> 4) and DoSaboteur (> 0) when not BESERK and not forced, **before** the guard order | `World::ai_group_new_units` (`continue` with TODO) | done (parity row "Palace specials, Deviator") |
| 12 | DoFremen (§4.3) incl. no-target MergeGroups, strength(g, 5), the stale +0x3C | `sim/ai_groups.cpp` (new) | done (parity row "Palace specials, Deviator") |
| 13 | DoSaboteur (§4.4): first charged saboteur (`load ≥ 0xA0`), job 0x12, strength 100, ClearUnits at the end | `sim/ai_groups.cpp` (new) | done (parity row "Palace specials, Deviator") |
| 14 | ai_commence: job 0x12 like 5 | `World::ai_commence` | done |
| 15 | MonitorGroups job 0x12: stealth test (EnemyUnitInRange r = 192 first, then +0x2A < 10, CellDist2 < 64), `load == 0xA0`, set 0x10 with no sound; then the job-5 code with the rand() drawn for 0x12 / 0x10 but the Rampaging switch only for 5 | `World::ai_monitor_groups` (no case 0x12 / 0x10; they fall to `default`) | done (parity row "Palace specials, Deviator") |
| 16 | Selection carry-over: DoFremen leaves the Fremen selected for the guard order; no ClearUnits around the delivered-group start or at the end; SetGroupToGuard one unit per guard order | `ai_group_new_units` | done 2026-10-16 (§4.2) |
| 17 | Side 0xFF in DoFremen / DoSaboteur is a fatal error in the exe | `ai_do_fremen` / `ai_do_saboteur` | done: no target, MergeGroups (crash fix, §4.3) |
