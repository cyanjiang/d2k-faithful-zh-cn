# 32: Fremen, stealth (cloaking), the sietch, allied AI units (1.06)

Everything that makes Fremen units, stealth units and the SIETCH behave as
in the original, written for a line-by-line port. Motivating case: mission
A4V1 has an allied AI side (side 4, house 4 = Fremen) with a SIETCH and
NSFREMEN infantry; the player loses if the sietch dies.

Object fields: `u+…` is a unit, `b+…` a building (objects of the
player's array at `player+0x30`, stride 0x94, lists linked by `+0x7c`;
doc 09 §2). `rec+…` is the unit record `0x77e250 + type·0x100` or the
building record `0x6dc540 + type·0x10C` (doc 08). `local` is gSideId
`0x798544`, `tick` is `0x5173f4`, `alliance[a][b]` is `0x798830[a·8 + b]`
(0 friendly, 1 enemy, 2 neutral; doc 10 §3).

"Verified" = read from the decompile and checked in the disassembly
(`re/disasm.py`). Every open point was settled against the exe on
2026-10-16 (§6).

## 0. Summary for A4V1

- **NSFREMEN never cloak.** Unit record 22 NSFREMEN has special kind
  **0**, not 12 or 13 (Templates.bin, verified with
  `re/dump_templates.py` and the raw byte rec+0x2D). Only kind 12
  (record 21 FREMEN, the Atreides palace Fremen) and the unit whose art
  name is "Stealth Raider" get the stealth flag at creation (§1.1). The
  only other source is the stealth crate (§1.8). So the A4V1 Fremen are
  ordinary visible infantry: drawn normally, on the radar, targetable,
  and the AI treats them like any other infantry (§4.3).
- Special kind 13 ("FR Non Stealth") is never assigned in 1.06 data and
  no code compares against 13 (operand scan: the only direct kind
  compare with 0xC/0xD is `0x455f43`, the kind-12 stealth flag at
  creation).
- **SIETCH** (building record 16): special kind 11 (barracks), record
  flags `+0x58 = 0x00400000` only. It is not click-selectable, cannot be
  captured or sabotaged, never burns, leaves no survivors, is not
  repaired by the AI, gets full hit points regardless of concrete, and is
  never built (tech 255). See §3.
- An allied AI side is never auto-targeted by the player's units, and its
  units never auto-target the player (all scans want
  `alliance == 1`). The player can click-select a single allied unit; it
  cannot be ordered (§4).

## 1. Stealth (cloaking)

### 1.1 Unit fields and flags

| Field | Meaning |
|---|---|
| `u+0x08` bit **0x10** | **can cloak** (stealth ability). Set at creation (below), by the stealth crate (§1.8), and for the saboteur by its deploy order (§1.9) |
| `u+0x08` bit **4** | **cloaked** (hidden). Set only by Cloak `FUN_0049f580` (and by the worm state code, §1.10) |
| `u+0x31` | u8 **fade timer**: 22 (`0x16`) after a cloak or uncloak, −1 per tick, the state machine only runs again when it is 0 |
| `u+0x2A` | ticks / 8 since last hit (doc 18; reset to 0 the update after a hit) |
| `u+0x2B` | ticks / 8 since last shot (doc 18; reset after firing) |
| `u+0x19` | selected |
| `u+0x8C` | own index (used as the shimmer phase) |

Creation (`FUN_00455870`, verified): `+0x31 = 0`, `+0x2A = +0x2B = 0xFF`,
flag 4 clear. At the end:

```
if rec.kind (rec+0x2D) == 12:                 flags |= 0x10   // 0x455f43
if strcmpi(art name 0x7891b0 + type*0x1C2, "Stealth Raider") == 0: flags |= 0x10
```

So a fresh Fremen is **visible**; its first stealth update cloaks it if
nothing prevents it (§1.3), which starts a 22-tick fade-out.

### 1.2 Helpers (verified)

```
IsStealthed(u)            // FUN_0049f5f0
    if !(u.flags & 0x10): return 0
    if u.flags & 4:       return 1
    return u+0x31 > 1     // fading in after an uncloak still counts

Uncloak(u)                // FUN_0049f5c0
    if (u.flags & 4) and u+0x31 == 0:
        u+0x31 = 22; u.flags &= ~4; return 1
    return 0              // no effect while a fade is running or when visible

Cloak(u)                  // FUN_0049f580 (only caller: FUN_0049f610)
    if !(u.flags & 0x10): return 0
    u+0x31 = (u+0x31 == 0) ? 22 : 22 - u+0x31   // 2nd branch unreachable in practice
    u.flags |= 4; return 1
```

Note: doc 19 describes `FUN_0049f5c0` as "clear flag 4, +0x31 = 22"; it
does that **only** when flag 4 is set and `+0x31 == 0`.

`StealthBlocker(cx, cy, side, r = 1)` (`FUN_0049f750`, verified): scans
cells `x = max(0, cx−r) … < min(cx+r+1, mapW−1)`,
`y = max(0, cy−r) … < min(cy+r+1, mapH−1)` (quirk: the last map column and
row are never scanned). A cell counts when its owner bits
(`cell & 7`) ≠ `side` **and**

- it has infantry bits (`cell & 0x3E0`, sub-slots 0x20..0x200), or
- it has no infantry bits, has bit 0x400 (wall / turret top-left cell,
  doc 12 §6) and the building there (`FUN_004a5720` → `FUN_004a5640`)
  has a primary weapon (`rec+0x0C ≠ 0xFF`), i.e. an armed turret.

Vehicles (cell bit 8) do **not** count. The owner test is `≠ side`, not
the alliance table: **allied infantry and allied turrets next to a
stealth unit also reveal it** (and keep it from cloaking).

### 1.3 The per-tick state machine `FUN_0049f610(u, side)` (verified)

Called from the unit update `FUN_00497b90` (`0x497cd8`) for every unit,
every tick, right after the saboteur charge block (§1.9) and the reload
countdown (`+0x1C −= 1`), before the attacker bookkeeping, the
`+0x2A` / `+0x2B` counters and the state switch.

```
f = u.flags                                    // read once
if !(f & 0x10) and u+0x31 == 0:
    u.flags &= ~4; return                      // non-stealth units (worms too, §1.10)
if (f & 4) and side != local:
    u+0x19 = 0                                 // a cloaked non-local unit is deselected
if u+0x31 != 0: u+0x31 -= 1
if u+0x31 != 0: return                         // fade running: nothing else this tick
revealed = false
if !(f & 4):                                   // visible
    if StealthBlocker(u+0x4A, u+0x4B, side, 1): revealed = true
else:                                          // cloaked
    if u+0x2A < 5 or u.state == 0x20 or u.state == 0x1F:
        Uncloak(u); revealed = true            // hit within the last 5 slots (~33..40 ticks)
    elif StealthBlocker(u+0x4A, u+0x4B, side, 1):
        Uncloak(u); revealed = true
// L:
if u.hp (u+4) < (u32 rec.maxhp (rec+4)) / 3 and IsStealthed(u):
    Uncloak(u); return
if !revealed and !(u.flags & 4):               // fresh read
    if u+0x2A >= 5 and u.state not in {0x20, 0x1F}
       and (rec+0x0E >> 3) + 1 < u+0x2B        // reload/8 + 1 < slots since last shot
       and u.state != 0x12 and u.state != 0x13:  // not waiting for / carried by a carryall
        Cloak(u)
```

States: 0x1F devastator self-destruct, 0x20 thumper deployed (order
handler `FUN_00457810`, deploy case), 0x12 wait for a carryall, 0x13
carried (doc 13). `u+0x4A/+0x4B` is the current cell.

Consequences (they follow directly from the verified code above, so the
port reproduces them without a separate rule; nothing here needs the
reference game):

- **Cloak** needs: not hit for ≥ 5 slots, not fired for more than
  `(reload >> 3) + 1` slots (FREMEN reload 40 → `+0x2B ≥ 7`, Stealth
  Raider 18 → `≥ 4`), no foreign infantry / armed turret in the 3×3
  around it. The fade-out then takes 22 ticks.
- **Uncloak** happens on: a hit (`+0x2A < 5`), foreign infantry or an
  armed turret adjacent, state 0x1F / 0x20, low hit points, a fire
  attempt (§1.4), state 0x12 (§1.4), the last-units rule (§1.6). The
  fade-in takes 22 ticks, and during it `IsStealthed` stays true until
  `+0x31 ≤ 1`.
- The machine re-evaluates only when `+0x31` reaches 0, so every decision
  is at least 22 ticks after the previous transition.
- **Low hit points** (< max/3, FREMEN < 233): a visible unit still cloaks
  (the low-HP test only uncloaks), and 22 ticks later the low-HP test
  uncloaks it again. A badly damaged stealth unit therefore flickers on a
  44-tick cycle (22 fading out, 22 fading in, re-cloak). Quirk, keep it.
- A hit applies flag 1 in the damage code (`FUN_0049d2e0`, doc 18 §2);
  `+0x2A = 0` is written later in the same unit update, after
  `FUN_0049f610`, so the uncloak is seen by the next update (port the
  call order exactly). The same holds for `+0x2B`: it is counted (and
  reset by flag 2 = fired) after `FUN_0049f610` (`0x49808f`, doc 14
  §2.3), so the cloak test reads the value from before this update's
  count. Ported 2026-10-16 (before, our tick counted +0x2B first and did
  not apply the flag-2 reset, so a unit could cloak one 8-tick slot early).

### 1.4 Combat interplay (verified)

- **Fire** (`FUN_00495b90`, doc 18 §3.1 step 4): after the turn step, if
  `IsStealthed(u)`: `Uncloak(u)` and return **without firing**. A cloaked
  unit's first shot only starts the fade-in; it can fire once
  `+0x31 ≤ 1` (about 21 ticks later). A unit still fading out (flag 4,
  `+0x31 > 0`) cannot uncloak until the fade-out ends.
- **State 2 (guard)**, `0x4983e2`: the target scan / auto-fire
  `FUN_00496480` runs (on the unit's 8-tick slot or with a target) only
  if `!IsStealthed(u)`. **A cloaked unit in state 2 never auto-fires**;
  it waits until something reveals it.
- **State 3 (area guard)**: the scan `FUN_00496480` and the area search
  run without a stealth test, so a cloaked area-guard unit finds a target
  and its fire attempt uncloaks it (above). The final "back at home"
  auto-fire (`0x498d75`) is skipped when `flags & 0x10` (the ability bit,
  not the cloaked bit).
- **State 0x12** (wait for a carryall), `0x49a135`/`0x49a174`: `+0x2C = 0`;
  if `IsStealthed`: `Uncloak`; then auto-fire only if (slot or target) and
  `!IsStealthed`.
- **MoveStep** (`FUN_00494260`, `0x494427`): the moving unit's
  auto-fire is skipped while `IsStealthed` (doc 18 §3.6).
- **Target drop** in the unit update (`0x497e1d`, verified): a stored
  unit target is cleared when it is dying (0x11), or when
  `target.flags & 0x400104` (removed / carried / **cloaked**) and
  `alliance[side][target side] ≠ 0`, or when it has 0x100 without 0x40.
  So a target that cloaks is dropped by non-friendly attackers.
- **Area damage** (`FUN_0049d2e0`) does not test flag 4: cloaked units
  are hit by splash and by shots at their cell like any other unit.

### 1.5 Targeting, AI and selection rules (verified)

`UnitAtPixel(px, py, &side, &idx, skipCloaked)` (`FUN_004a5270`, doc 18
§3.4): for an infantry cell it returns the nearest infantry that is not
cloaked when `skipCloaked`; for a vehicle cell it finds the vehicle and
returns none if it is cloaked and `skipCloaked`.

| User | Rule for cloaked units |
|---|---|
| `EnemyUnitInRange` (`FUN_004a4900`) | calls `UnitAtPixel(…, includeCloaked == 0)`. **All four callers pass includeCloaked = 0** (`0x4969f2`, `0x41d3d7`, `0x496666`, `0x498cee`). Only `alliance == 1` owners are scanned |
| Enemy building search `FUN_004a4fd0` | `alliance == 1` owners only |
| AI GetNearestHostileThing `FUN_00420700` | units with `flags & 0x400144` skipped (doc 23 §6.3) |
| AI defend-area scan in MonitorGroups (`0x41dfbc`) | a unit found in an enemy cell is ignored if cloaked |
| AI group target check `FUN_004237a0` | tests 0x400140 only: a cloaked unit stays the **group** target (the per-unit target drop above still applies) |
| Cursor, nothing selected (`FUN_004433a0`) | a non-own unit cell → `UnitAtPixel(…, 1)`; nothing found → cursor 0 |
| Cursor and click, units selected | `skipCloaked = 1` when the cell owner ≠ local and the cell has unit bits; a found unit that is cloaked with `alliance[local][owner] ≠ 0` is treated as no unit. So a click on a cloaked non-own unit acts on the ground (move) |
| Selection | `FUN_004a5a50` has no owner test, but non-local cloaked units are deselected every tick by §1.3 and cannot be clicked (above). Own cloaked units are selectable |
| Tooltip (`FUN_0044b5c0`, `0x44b878`) | `UnitAtPixel` at the mouse's world pixel with skipCloaked = 0 (`0x44b73c`..`0x44b763`); friendly units (alliance 0, own included) show their name; non-friendly units show the `NeutralUnit` / `EnemyUnit` text only when not cloaked, else the text pointer stays 0 and **no tooltip** is shown (no fallback to the building or terrain text, because a unit was found). Verified 2026-10-16; our tooltip now also looks up the unit at the mouse pixel instead of the cell corner (it picks the nearest infantry like the exe) |
| Radar (`FUN_00426000`, `0x426153`/`0x4262c6`) | a unit is plotted only if its owner is local or flag 4 is clear (doc 11 §4 confirmed). **Allied cloaked units are hidden too** |

### 1.6 Last stealth units (`FUN_00455480`, verified)

Runs after the mission script every tick for `s = tick & 7` (doc 19, end-of-mission checks):

```
if hasBuildings[s] (0x6b87c0[s]) == 0:
    anyVisible = anyCloaked = false; first = null
    for u in side s units (list order):
        if u.flags & 0x40 or rec.kind == 5: continue   // air, worms
        if u.flags & 4: anyCloaked = true; first = first or u
        else: anyVisible = true
    if anyCloaked and !anyVisible: Uncloak(first)       // only if first+0x31 == 0
```

### 1.7 Drawing (verified)

Images (retail path, `0x4eb014 = 1`, DATA.R16 entries, `FUN_004606d0`;
doc 10 §8): **16 shimmer patterns** `0x786d50[16]` (entries 178–193) and
**12 dissolve masks** `0x6dc018[12]` (entries 194–205), 64×64, 8-bit.
(The debug path `FUN_00462290` loads them from `stealth.bmp` and
`unstealt.bmp` and fills shimmer frames 1–3, 5–7, … by linear
interpolation between frames 0, 4, 8, 12.)

Blit helpers (16-bit target; pattern pixel `p`, sprite pixel `s`, 5-bit
channels clamp at 0):

- `Shimmer(sprite, pattern)` `FUN_00432d80`: for each opaque sprite pixel,
  the **screen** pixel's R, G, B each −= `p`. The sprite colours are not
  drawn; the background is darkened in the sprite's shape.
- `ShimmerFaded(sprite, pattern, level)` `FUN_004353b0`: the same with
  `max(0, p − level)`.
- `Dissolve(sprite, mask)` `FUN_00434ea0`: with the sprite palette entries
  0 = screen pixel and 1 = screen pixel at half brightness
  (`(px & 0x797e6c) >> 1`), the screen pixel becomes
  `palette[s & m]` (`m` = mask pixel). So the sprite appears where the
  mask bits allow, entry 1 gives a shadow, entry 0 leaves the background.

Main draw list (`FUN_00428e30`, unit entries, both the body and the
turret layer, when `u+0x1D & 2 == 0`; verified):

```
phase = shimmer[(u+0x8C + tick) & 0xF]
if !(u.flags & 4) and u+0x31 == 0: normal blit
else:
    if owner == local: Shimmer(sprite, phase)            // own units always shimmer
    elif u+0x31 != 0:
        level = (flags & 4) ? 11 − (u+0x31 >> 1) : (u+0x31 >> 1)
        ShimmerFaded(sprite, phase, level)
    // else (non-local, cloaked, fade over): nothing drawn
    if u+0x31 != 0:
        i = (flags & 4) ? 11 − (u+0x31 >> 1) : (u+0x31 >> 1)
        Dissolve(sprite, dissolve[i])
```

So other players' (allied included) cloaked units are **invisible**;
during the 22-tick fades they dissolve in/out over a fading shimmer. Own
cloaked units are a permanent shimmer. The selection bracket and health
bar of a selected unit are drawn from the entry's selected copy
(`+0x21`) with no stealth test; in practice only own cloaked units can
be selected.

Quirk, infantry pass `FUN_0042af00` (infantry with unit flag 0x1000000,
i.e. standing in a cell that has a vehicle bit, drawn before the
buildings): same structure, but **no** `ShimmerFaded` for non-local units
and the dissolve index is reversed (flag 4 → `u+0x31 >> 1`, else
`11 − (u+0x31 >> 1)`). Verified 2026-10-16 in `FUN_0042af00`: the
non-local branch has no `FUN_004353b0` call, and the mask is
`0x6dc018[t >> 1]` with flag 4 set but `*(0x6dc044 − (t >> 1)·4)` =
`0x6dc018[11 − (t >> 1)]` without it (the main pass has them the other
way round). Ported 2026-10-13: such infantry are drawn by their own pass before the buildings and the sorted objects (`GameScreen::compose`, `render/objects.cpp`), with this stealth rule, and their flash never counts down (doc 44 §4.3).

### 1.8 Stealth crate (`FUN_0049f1b0`, crate type 5 of `FUN_0049ebf0`; verified)

For every unit of the picking side with `dx² + dy² < 9` cells from the
crate: a saboteur (kind 4) gets `+0x29 = 0xA0` (full charge); any other
unit that is not `IsStealthed` gets `+0x31 = 0` and flag 0x10. This works
on any unit type, NSFREMEN included.

### 1.9 Saboteur (kind 4) for completeness (verified)

Every 16 ticks (`tick & 0xF == 0`, unit update `0x497c..`): without flag
0x10, `+0x29 += 1` up to 0xA0; with flag 0x10, `+0x29 −= 4` (to 0); at
`+0x29 == 0` with flag 4 set: clear 0x10 and `Uncloak`. The deploy order
(`FUN_00457810`) sets flag 0x10 and plays `S_STEALTHUP` when 0x10 is
clear (the cursor offers it at `+0x29 ≥ 0xA0`, doc 10 cursor rules). Entering a
building (`FUN_00496d10`, `0x496f64`) calls `Uncloak`.

### 1.10 Flag 4 and worms

Unit flag 4 is set only at `0x49f596`/`0x49f5a8` (Cloak) and `0x49b0d0`
(worm state 0x1c, doc 17). For a worm (no 0x10, `+0x31 = 0`) the first
lines of `FUN_0049f610` clear flag 4 at the start of each of its updates;
the worm state code may set it again later in the same update. Keep the
call order.

## 2. FREMEN (kind 12) vs NSFREMEN (kind 0)

| | FREMEN (rec 21) | NSFREMEN (rec 22) |
|---|---|---|
| houses / tech | A / 6 | F / 255 (never buildable) |
| prerequisite | ATREIDES_PALACE | SIETCH |
| kind | 12 → stealth flag at creation | 0 → no stealth |
| weapons, hp, speed, art | FREMEN_S / FREMEN_L, 700, 64, gfx 2 | identical |
| AI grouping | kept out of normal groups; DoFremen (§4.3) | normal infantry |

Kind-12 code paths (all found by the kind compares): unit creation
(§1.1), GroupNewUnits / DoFremen (§4.3), the palace call-in (order 0x30,
doc 12 §8.4, unit `0x6d60ed` = 21 FREMEN, `0x6b8854` copies).

## 3. SIETCH (building record 16)

Record (Templates.bin, verified): hp 6000, armour 2, power 50 (uses),
view 3, size 64×64 (footprint 0x33), tech 255 (never built), special kind
**11** (barracks), flags `+0x58 = 0x00400000`. For comparison BARRACKS
has `0x00200700`.

What the flags mean for it (building objects copy the record flags into
`b+0x28` at creation, `0x456508`):

| Bit | SIETCH | Effect of the missing / present bit (verified sites) |
|---|---|---|
| 0x400000 (text "FREMEN BARRACKS", `FUN_00464960`) | set | no concrete laid under it at creation; no concrete hit-point rule (full hp, no flag 0x8000, so no decay) (`FUN_004563b0`, doc 08 / 12); on removal (`FUN_0044df60`) its cells do **not** get 0x8000 / 0xE000 (buildable / passable), only 0x400 and 0x10000010 are cleared |
| 0x200 | clear | not selected by clicks (doc 10 §2, `0x445aab` etc.); no damage flames (`FUN_0049d2e0` `0x49d76f`, building update `0x49bab7`); no survivors when destroyed (`FUN_004977a0`); AI DoRepairs skips it (`FUN_0041bf00`); a selected saboteur (kind 4) gets no action on it |
| 0x400 | clear | a selected engineer (kind 3) gets no capture action on it |
| 0x40 / 0x10 | clear | no active animation (doc 12 §7.3) |

Other properties:

- Kind 11 makes it a barracks: infantry exit (0, 1) → (0, 2) (doc 12
  §8.1), primary barracks slot `player+0x260a2` and flag 0x80 if the
  slot is empty (doc 12 §3 step 8), and the NSFREMEN prerequisite. With
  tech 255 NSFREMEN are never offered, so the sietch **produces
  nothing**.
- It can be attacked: no code excludes it from damage, from explicit
  attack orders (non-friendly owner or Ctrl) or from the AI building
  attacks (which only skip walls, kind 0xE, and record group `0x6d60e1`,
  doc 23 §6.3). Auto-targeting of buildings by units only looks for
  turrets (doc 18 §3.4), so it is not shot at automatically.
- Mission loss on its death comes from the mission script (doc 19), not
  from the building.
- No other code tests record flag 0x400000 (operand scan of
  `0x6dc598` loads; the remaining 0x400000 tests are the unit "being
  removed" flag).

## 4. Allied AI side next to the player

### 4.1 Targeting (verified)

All automatic searches use `alliance[side][owner] == 1`
(`EnemyUnitInRange`, `FUN_004a4fd0`, GetNearestHostileThing, the
defend-area scan). Allied (0) and neutral (2) objects are never
auto-targeted. A unit's stored target is only dropped for a cloaked
target when the alliance is non-zero (§1.4). Splash damage hits
everyone.

### 4.2 Selecting allied units (verified)

- Click on an allied unit (alliance 0, no Ctrl) → `FUN_004a5a50(owner,
  idx, 0)`: deselect all players, select that unit. Box select only
  takes local units (doc 10 §2).
- "Units selected" checks (`FUN_004a57c0`, `FUN_004a59c0`) and every
  order (`GenerateUnitMoveOrder(local, …)` etc.) use the **local** list
  only, so a selected allied unit cannot be ordered; it only shows its
  bracket and health bar.
- The AI keeps the human's pick aside while it issues its own orders:
  GetSelected `FUN_0041cc20` stores the selected unit of its side in
  `+0x1D40` and clears its `+0x19`; ResetSelected `FUN_0041cc90`
  re-selects it if it is not dying (doc 22). A second selected unit only
  logs "More than one enemy unit selected".
- Allied buildings are selected by click only with record flag 0x200,
  so not the sietch.

### 4.3 AI handling of Fremen (verified unless marked)

GroupNewUnits (`FUN_0041c3e0`, doc 23 §3.4) counts ungrouped units by
kind instead of grouping them:

- kind 4 (**saboteur**) → if any and the AI is not BESERK:
  DoSaboteur `FUN_00422900`;
- kind 12 (**FREMEN**) → if more than 4 and not BESERK: DoFremen
  `FUN_00422a80`: every ungrouped kind-12 unit joins a new group, which
  gets job 5 (AttackBuilding, doc 23 §4.6), substate 2, the target found
  by `FUN_0041bbb0(side from FUN_0041cdf0, cell from FUN_00420a70,
  0.25f)`, then is selected and ordered (`FUN_0041cce0` SelectedGroup,
  `FUN_0041fc80` CommenceOperation). No target → `FUN_00421710`
  MergeGroups (every unit of the group moves to the base group, the group
  is freed; verified, doc 39 §4.3).

Doc 23 §3.4 reads kind 4 as "MCV deploy"; in Templates.bin kind 4 is the
saboteur and kind 6 the MCV.

NSFREMEN (kind 0) take the normal path: base group, selected, guard order
0x18. AI-owned pre-placed units start in state 3 (area guard),
player-owned ones in state 2 (`FUN_00455870`: state 3 when the owner is
not local, not a network game, and the kind is not 2 / 8 / 10). Whether
side 4 of A4V1 has an active AI is set by its MIS AI block (doc 19).

## 5. Corrections to other docs

- doc 19 (end-of-mission checks): `FUN_0049f5c0` only acts when flag 4 is set and
  `+0x31 == 0` (§1.2).
- doc 10 §8: the icon `0x782060` (`0x782058[2]`) is drawn for own selected
  units in **state 0x12** (waiting for a carryall, `u+0x10 == 0x12`), not
  for "Fremen (type 0x12)".
- doc 10 (click rules): `FUN_004a59c0(4)` (all selected are saboteurs) checks
  building flag 0x200 and `FUN_004a59c0(3)` (engineers) checks 0x400.
- doc 23 §3.4: kind 4 = saboteur (DoSaboteur), kind 0xC = Fremen
  (DoFremen when > 4). Flags 0x80 / 0x1000 on units are the deviator
  (0x1000 = deviated, countdown `+0x2E`, original side `+0x2D`, set by
  the damage code; 0x80 = deviation ended), not Fremen / saboteur.
  Verified: the unit update sets 0x80 (and clears 0x1000) when the
  deviation runs out (`FUN_00497b90`, the +0x2a block), and its only
  reader, GroupNewUnits (`0x41c5b4`), puts such a unit into a one-unit
  rampage group and clears 0x80 (`0x41c6e9`); doc 40 §5.1.
- doc 17 §1: the worm "submerged" flag 4 is cleared at the start of each
  worm update (§1.10).
- doc 12 §8.3 step 7 ("inferred: cloaked"): flag 0x10 is the stealth
  **ability**; the cloaked state is flag 4.

## 6. Open points

Settled on 2026-10-16:

- The low-HP flicker and the silent cloaked guard (§1.3, §1.4) are
  direct consequences of the verified state machine and state 2 code;
  the infantry-pass draw quirk is verified in `FUN_0042af00` (§1.7).
- DATA.R16 entries 178–205 are all 8-bit 64 × 64 images (type 2, no
  palette of their own): 178–193 hold the values 0..18 (shimmer
  darkening), 194–205 the full 0..255 range (dissolve masks), as the
  blits need.
- Every remaining `UnitAtPixel` caller pushes a constant 0 for
  skipCloaked (finds cloaked units): `0x445292` (repair mode),
  `0x44dcb6` (building footprint), `0x45dc91`, `0x45dd0b`, `0x497b1d`,
  `0x498fc1`, `0x4993fa`, `0x499900`, `0x49aa81` (push at `0x49aa55`),
  `0x49ab44`, also the tooltip `0x44b763`, the worm's `0x44d135` /
  `0x44d168` / `0x44d57e` and the AI scan `0x41dfac`. Only the cursor /
  click sites (`0x443562`, `0x443b02`, `0x445683`) and EnemyUnitInRange
  (`0x4a4ac6`, `sete` of its includeCloaked argument) can skip them.
- The stealth tooltip (§1.5): verified.
