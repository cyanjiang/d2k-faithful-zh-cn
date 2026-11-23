# 53: Computer player: what the port still lacks (skirmish first) (1.06)

A walk through every callee of DoAI (`FUN_0041b610`), compared with our
code (`src/sim/ai.cpp`, `ai_groups.cpp`, `ai_rebuild.cpp`, `capture.cpp`,
`economy.cpp`, `starport.cpp`). For each gap there is enough of the exe's
logic to port it from this doc alone. Skirmish comes first, because the
skirmish AI starts from an MCV and uses settings that the campaign
missions leave off.

Related: doc 22 (core, economy), doc 23 (groups, attacks, defence), doc 29
(DoProtection, DoOverrides, DoBuildCarryalls), doc 21 §4 (unit repair on
the pad), doc 30 §1.6 (DoMCVs), doc 33 (carryall ferry), doc 34 §7
(Starport buying), doc 28 §6 (upgrades), doc 39 / 41 (special weapons),
doc 31 §6.3 (the crate scan).

Conventions are those of doc 29. Offsets without a prefix are in the
side's AI block (`0x4f4118 + slot × 0x1DB8`). `P+…` is the player struct
(`FUN_004a4660(side)`). `G(g)+off` = `block + g × 0x2C + off` (0x2E
target u16, 0x30 target side, 0x34 timer, 0x38 strength, 0x3C start
strength, 0x40 idle count, 0x44 age, 0x48 job, 0x49 substate, 0x4C flags,
0x50 / 0x51 cell, 0x52 in use, 0x54 defend-area index). "Verified" =
read from the decompile and checked in the disassembly. `rand()` is the
shared MSVC LCG; "line" = the debug line number pushed with the call.
Every former TODO(verify) was settled from the exe on 2026-10-16.

## 0. Summary: DoAI callee by callee

DoAI runs every tick for every slot with +0x01 set (doc 22 §2.2). Order
verified in `FUN_0041b610`:

| # | Exe | Label | Gate | Ours | Status |
|---:|---|---|---|---|---|
| 1 | `FUN_0041cc20` | GetSelected | | inline in `World::tick_ai` | implemented |
| 2 | `FUN_00422140` | DoMCVs | | `World::ai_mcvs` (`capture.cpp`) | implemented (checked again, §1.4) |
| 3 | `FUN_0041c3e0(+0x1D3C, 0)` | GroupNewUnits | | `ai_group_new_units` | implemented |
| 4 | `FUN_0041bf00` | DoRepairs | | `ai_repairs` | implemented |
| 5 | `FUN_00421fa0` | MonitorHarvesters | | `ai_monitor_harvesters` | implemented |
| 6 | `FUN_0041bfe0` | DoBuildUnits | | `ai_build_units`, `ai_best_unit` | implemented (practice MCV roll, §4) |
| 7 | `FUN_0041c060` | DoBuildBuildings | | | empty in 1.06, nothing to port |
| 8 | `FUN_00422c20` | DoOverrides | | `ai_overrides` | implemented (doc 29 §2) |
| 9 | `FUN_00420500` | DoAttackApproachers | +0xF56 | | **dead** in 1.06, also in skirmish (§1.3) |
| 10 | `FUN_0041e650` | DoRepairUnits | +0x1D14, +0x1D46 ≠ 0xFF, `P+0x245C1[+0x1D46]` ≠ 0 | `ai_repair_units` | implemented (§2) |
| 11 | `FUN_0041c920` | DoAttackBuildings | | `ai_attack_buildings` | implemented |
| 12 | `FUN_0041c070` | DoProtection | | `ai_protection` | implemented (checked again: matches doc 29 §1; the airlift request is ported, doc 33 §2.2) |
| 13 | `FUN_00422270` | DoSpecialWeapons | | `ai_special_weapons` | implemented, all 5 cases (the function's header comment is stale) |
| 14 | `FUN_0041c270` | DoDefence | | `ai_defence` | implemented |
| 15 | `FUN_0041d070(0)`, `(0x10)` | MonitorGroups | | `ai_monitor_groups`, `ai_defend_area` | implemented (§5) |
| 16 | `FUN_0041eab0` | MonitorBuildings | | `ai_monitor_buildings` | implemented |
| 17 | `FUN_004214a0` | DoDefendChecks | | `ai_defend_checks` | implemented (§6) |
| 18 | `FUN_004203d0` | DoBuildCarryalls | | `ai_build_carryalls` | implemented |
| 19 | `FUN_0041ebc0` | RebuildBuildings | +0x1D45 | `ai_rebuild` | implemented, practice branch included (§1.5), with the base box snapshot (§1.6) |
| 20 | `FUN_00421c40` | UpgradeBuildings | +0x05 | `ai_upgrade_buildings` (`economy.cpp`) | implemented |
| 21 | `FUN_00421cf0` | BuyUnitsFromStarport | +0x04 | `ai_buy_units` (`starport.cpp`) | implemented |
| 22 | `FUN_00423850` | (auto-BESERK) | | `ai_auto_beserk` | implemented (§3) |
| 23 | inline | BESERK sell + rampage | +0x1E = 1 or 2 | `ai_beserk` | implemented (§7.1) |
| 24 | `FUN_0041cdb0` | ClearUnits | | `ai_clear_selection` | implemented |
| 25 | `FUN_0041cc90` | ResetSelected | | inline in `World::tick_ai` | implemented |

All of §10's checklist is implemented (2026-10-13; §11 lists what the
port found on the way). DoUnitFacings (`FUN_00420c80`) and the unit
counters were closed on 2026-10-14 (§11); the alliance read past the
table is an accepted deviation (§11).

Fields never read in 1.06 (no port needed): FirstAttackUnitDelay /
TimeBetweenUnitAttacks / the unit-attack timer (+0xF1C, +0xF20, +0xF24:
written only by InitAI and ActivateAI), AttackUnitStrength +0xF3C,
DefendStrength +0xF3F, MaxAttackUnitGroups +0xF41,
MaxAttackBuildingGroups +0xF42, MaxDefendGroups +0xF44. +0xF40 and
MaxGuardGroups +0xF45 are read only by the dead DoAttackApproachers.
(Operand scan of the whole code section, verified.) So **there are no
unit attacks** in 1.06, in skirmish either.

## 1. Skirmish: where the AI data comes from

### 1.1 The mission file is `_PRAC.MIS` (verified)

`FUN_004752c0` (the mission-name setter of the UI manager): with
`gGameType` `0x797e34` = 1 (skirmish) or a network game (`0x7984c4`), it
writes the map name `"%s.map"` with the chosen map into `0x6f9840` and
the mission name `"%s.mis"` with `"prac"` into `0x6dc470`. The loader
`FUN_0044f8e0` opens `"_%s"` (`_prac.mis`) in the maps directory
(`0x5136e8`, used whenever `gGameType` ≠ 0): **`Data\maps\_PRAC.MIS`**,
68066 bytes, the same layout as the campaign `.MIS` (doc 19). So every
skirmish (and network) game uses the same AI template. There is no
per-map or per-house AI data.

The loader reads the whole file (tech, credits, houses, the 0xEDC0 AI
area, …) and then, for skirmish / network:
- houses `0x8cd4f0` and the human's credits are restored to the values
  set before the read;
- for every slot: tech = the skirmish tech level `0x4e3b04`; credits =
  the skirmish credits `0x4e3b08`; in skirmish every slot **other than
  the human's** gets **7000** credits, **10500** with `0x4e8bf0` = 1 and
  **14000** with `0x4e8bf0` = 2. `0x4e8bf0` is the menu variable
  `gDifficultyLevel` (u16; the variable tables at `0x4c9ac4` and
  `0x4d5cbc` name it), and the skirmish page's Easy / Normal / Hard
  buttons write it (`Data\UI_DATA\Skirmish.uil`: `SetValueU2To
  gDifficultyLevel 0`, 1, 2); `skill.uil` does the same for the campaign.
  No exe code writes it directly (its other references are reads and the
  save / load at `0x441599` / `0x4419af`).

### 1.2 Who gets an AI (`FUN_00469790`, map set-up, verified)

`FUN_00469790` runs after the `.MIS` read (`0x4488f8`, before ActivateAI
at `0x448b06`). In skirmish:

```
humans 0x7984c0 = 1
for s in 0..7: block[s].+0x01 = 0                 // nobody is active
for s in 1..7: block[s] = block[0] (0x1DB8 bytes); block[s].+0x00 = s
first = max(humans, 1)                            // 1
for s in first .. first + AIcount(0x4e3b0c) - 1:
    house[s] = rand() % 3                         // A, H or O at random
    P(s)+0x24253 = house[s]
    block[s].+0x00 = s; block[s].+0x01 = 1
```

So the AI slots are 1..N and all start from **slot 0 of `_PRAC.MIS`**
(the only active block in the file). Slot 0 (the human) has no AI. Then
each player in slot order gets a start position (slot 0: `rand() %
starts`; the others: the free start farthest from the taken ones, by
`ftol(sqrt(dx² + dy²))` of the cells, doc 52 §2.5), an MCV (the last unit record with kind 6)
and the `0x4e3b00` starting units of table `0x6b8868` for the tech
level. This set-up is not AI code; docs 51 (`51-skirmish-setup.md`)
and 52 (`52-skirmish-start-and-end.md`) cover it in full, and doc 54
covers crates (the cell objects of §7.2).

### 1.3 The `_PRAC.MIS` slot-0 block (decoded)

Differences from the InitAI defaults (doc 22 §1.2) and from the
campaign values:

| Setting | Value | Effect in skirmish |
|---|---|---|
| EnablePractice +0x03 | **1** | practice mode: §1.5, §3, §4 |
| BuyUnits +0x04 | **1** | Starport buying runs (doc 34 §7) |
| UpgradeBuildings +0x05 | 1 | |
| RebuildBuildings +0x1D45 | 1 | the ratio builder (§1.5) |
| RepairUnits +0x1D14 / RepairGuardingUnits +0x1D15 | **1 / 1** | DoRepairUnits runs (§2) |
| UnitBuildRate +0x1D2C | **80** (hard: 60) | one unit order every 80 ticks (campaign: 1000) |
| CashStash +0x1054 [9] | 1800 ×5, **2500** (BuildBuildings), **2575** (BuildUnits), 1800, 1800 | |
| FirstAttackBuildingDelay / TimeBetweenBuildingAttacks | 12000 / 15000 | |
| AttackBuildingStrength +0xF3D | 40 | |
| ProtectStrength +0xF3E | 75 | |
| MaxBaseStrength +0xF4C | 500000 | read only with +0x1D20 (0) |
| GuardBuildings +0xF56 | 1 | cleared by ActivateAI (below) |
| GuardGroupSize +0xF5C | **300** | defend area on (+0x1D = 1) |
| AirliftPercentage +0x1D1F / AirliftAndAttack +0x1D44 | 50 / **1** | DoProtection lifts and escorts |
| SquishInfantry / SquishRange | 1 / 9 | |
| Morale[AttackBuilding] | 80 | others 100 |
| ProtectUnit | {8 HARVESTER} | |
| UnitBuildPriority (by record) | LIGHT_INF 3, TROOPER 3, ENGINEER 0.01; 0 for THUMPER, HARVESTER, MCV, both CARRYALLs, ORNITHOPTER, FREMEN (21), SABOTEUR, DEATH_HAND; 1 for the rest | |
| BuildRatio (by building group) | CONSTRUCTION_YARD 1, CONCRETE 1, CONC2 1, WIND_TRAP 9.2, BARRACKS 4.3, SIETCH 1, WALL 0.01, REFINERY 5.25, MEDIUM_GUN_TURRET 3.05, OUTPOST 2.1, LARGE_GUN_TURRET 2.6, AT_HIGH_TECH 2.06, HIGH_TECH 2.06, LIGHT_FACTORY 2.05, SILO 2.7, HEAVY_FACTORY 2.11, STAR_PORT 1.1, REPAIR_PAD 0.8, RESEARCH_CENTRE 1.6, the 3 palaces 0.5, IMPERIAL_PALACE 1, SP_OUTPOST 1 | what to build (§1.5) |
| MaxBuildings (by group) | WIND_TRAP 15, REFINERY 3, MEDIUM_GUN_TURRET 4, LARGE_GUN_TURRET 4, SILO 5, every other group 1 | |
| AttackBuildingPriority | campaign-like (yard 100, wind trap 50, heavy 30, palace 20, …) | |
| HarvestersPerRefinery / CarryallsPerRefinery | 2 / 1 | |
| +0x19EC | 1 in the file | 0 after ActivateAI (below) |

(Decoded with a scratch script from file +0x58; group names from
Templates.bin `0x787e08`, unit records from `0x77e250`.)

### 1.4 ActivateAI: the parts that matter in skirmish (`FUN_0041b250`, verified)

Our `ai_activate` / `ai_activate_groups` follow doc 22 §2.1. Missing or
different:

1. **`+0x1D46` = `FUN_004a6070(6, side)`** (ours: a TODO). Between the
   first and second rand(). `FUN_004a6070(kind, side)`: the first building
   record r (0..99) with `rec+0x5C` (kind) = 6 and `rec+0x04 & (1 <<
   side)` (the house mask tested with the **side** number, quirk); returns
   `rec+0x88` (the name group, REPAIR_PAD = 17) or 0xFF. The three
   REPAIR_PAD records cover house bits 0..6, so every side but 7 gets 17.
   DoRepairUnits (§2) needs it.
2. **Skirmish / network: no GroupNewUnits at activation.** The call
   `GroupNewUnits(guard group, 1)` runs only when `gGameType` ≠ 1 and
   `0x7984c4` = 0. In skirmish the starting units are not put into the
   guarding group and get no guard order at activation; the periodic
   GroupNewUnits later puts them into the base group (doc 23 §3.4). Ours
   always calls `ai_group_new_units(s, 1, true)`.
3. **+0x19EC = 0 when practice (+0x03)** (after the two groups are made).
   DoMCVs sets it back to 1 once the side has a build list (a yard). It
   gates MonitorGroups job 0xF (§5.3).
4. **+0xF56 (GuardBuildings) = 0 unconditionally** (`0x41b4db`). So
   DoAttackApproachers is dead in skirmish too, although `_PRAC.MIS` sets
   it. Ours already clears it.
5. **FreePercentage** (ported): after the defend area,
   `n = (i32)(G(guard)+0x38 × +0xF0D) / 100` (`0x41b5c6..0x41b5e4`); if
   n ≠ 0, CreateSplinterGroup(guard → 0xFF, n) (`0x41b5ef`; the units
   leave every group; one rand() line 0x645 as usual). 0 in `_PRAC.MIS`,
   **100 in A4V1 side 4 and H4V1 sides 3 and 4** (corrected 2026-10-16:
   this said "0 in the campaign"). Group 0xFF's record lies past the
   side's block, see §11.
6. The "AI not initialised" error check is skipped in skirmish (no port
   needed).

The rest (rand() order 0xE2 / 0xEA / 0xEE, hard UnitBuildRate × 0.75,
GetInitialBuildingPositions skipped in practice, the two groups, the
retreat cell, the defend area of the base box with wanted 1000 and
period 100) is already ported.

### 1.5 What the skirmish AI builds (verified)

There is no build order and no list. RebuildBuildings (doc 22 §3.4) with
practice set:
- **GetBestBuildingToBuild** (`FUN_00420d50`): over the side's building
  build list (`P+0x260BC`, `P+0x260B4` entries), group g = `rec+0x88`,
  n = `P+0x245C1[g]`, score = `f32(n + 1) / BuildRatio[g]`; candidate if
  (cash + spice > CashStash[5] or +0x09) and score < best (start
  100000) and n < MaxBuildings[g] and the house's record of g is not
  kind 0xF (concrete) and (**practice** or NeedBuilding). Lowest score
  wins (strict <, first on ties). So the side fills its buildings in
  proportion to BuildRatio up to MaxBuildings, only what the build list
  offers (tech, prerequisites).
- The place: **FindRandomBuildingPosition** (`FUN_00420e90`, 8 tries, rand
  lines 0x148B x then 0x148C y) instead of the old cell. It fails when
  the side has no building (`P+0x10` list empty).
- Then the same concrete-quarters state machine as in the campaign.

Ours (`ai_best_building`, `ai_random_position`, `ai_rebuild`) matches
this. `n` counts the side's buildings of the name group the way the
group counter `P+0x245C1[g]` (read at `0x420dd3`) moves: +1 in
CreateBuilding (`0x456680`), −1 in `FUN_0044dee0` when the building
leaves the map (`0x49bdf9`, the end of its state 0x11) or at once when
it is captured (`0x46d78c`). So buildings under construction, being sold
or dying count; the captured old object (flag 0x10000) does not
(corrected 2026-10-16; it used to count for its one or two last ticks).
The same holds for the refinery count (`owned_building_kind(s, 4)`: kind
4 is exactly name group 7 = `0x6d60df`, records 20..22). For the inverted
base box see §1.6.

### 1.6 The player base box `P+0x262E8..+0x262EB` (`FUN_0046be50`, verified)

Ours (`World::base_box`) is the bounding box of the footprint cells, all
zero without buildings. The exe computes it inside **RebuildLists**
(`FUN_0046be50`, our `rebuild_lists`), so it is a **snapshot** taken
whenever the build lists are rebuilt (building placed, destroyed, sold,
captured, upgrade done; doc 11), not a live value:

```
x0 = W; y0 = H; x1 = 0; y1 = 0
if P has no building (P+0x10 list empty): x0 = y0 = 0
for each building b of P (P+0x10 list):          // record r
    cx = (b.x >> 16) >> 5; cy = (b.y >> 16) >> 5   // cy = the bottom cell row + 1 (b.y is the bottom edge)
    x0 = min(x0, cx - 2)
    x1 = max(x1, cx + (r+0x87 >> 5) + 2)          // width px
    y0 = min(y0, cy - 2 - (r+0x86 >> 5))          // height px
    y1 = max(y1, cy + 2)
if x0 == 0 and y0 == 0:                           // no buildings (or a box at 0,0)
    x0 = W; y0 = H                                // x1, y1 keep their values
    for each unit u of P (P+8 list, aircraft too):
        ux = (u.x >> 16) >> 5; uy = (u.y >> 16) >> 5
        x0 = min(x0, ux); x1 = max(x1, ux + 1)
        y0 = min(y0, uy); y1 = max(y1, uy + 1)
+0x262E8 (y min) = max(y0, 0); +0x262E9 (y max) = min(y1, H - 1)
+0x262EA (x min) = max(x0, 0); +0x262EB (x max) = min(x1, W - 1)
```

(The signed shifts round toward zero, as everywhere.) In a fresh
skirmish base before the MCV deploys, the box is the units' box. With
no units either it is x0 = W, y0 = H, x1 = y1 = 0 (inverted).

The inverted box does reach the callers (verified 2026-10-16). With
units, x1 ≥ x0 + 1 always (the unit loop raises x1 to ux + 1), so only
two cases invert it: no buildings and no units, or a building box that
starts exactly at (0, 0) (some building with its left column at 2 and
some with its top row at 2) while the side has no units; then x0 = W,
y0 = H and x1, y1 stay the buildings'. A flat x range (x1 = x0) comes
only from units that all stand in the last column (x1 = W clamped to
W − 1). What the callers do:

- Comparisons and midpoints (return to base, TakeOver, side choice, the
  patrol's "outside the box" test) use the stored bytes as they are; our
  ports keep the same arithmetic.
- **FindRandomBuildingPosition** (`FUN_00420e90`) divides unsigned
  (`DIV` at `0x420f8b` / `0x420fad`) and adds the cell as a byte: an
  inverted range gives a huge divisor, so the try is the cell
  `(rand() + x0) & 0xFF`, `(rand() + y0) & 0xFF`, scored by
  CanAIPlaceBuilding like any other (cells off the map score 0). Ours
  does the same since 2026-10-16 (it used to skip such tries). A zero
  width (x0 − 4 = min(x1 + 2, W − 1), only possible in the inverted
  case) is an integer division fault in the exe; we skip that try.
- **Job 0xF patrol** (§5.3): unsigned `DIV` at `0x41dc33` / `0x41dc6c`,
  the cell kept as an int. A zero x width faults; an inverted area gives
  x = rand() + x0 and the exe reads the cell flags past the map rows
  (`0x547df0` row table, `0x517df4` cells), i.e. unrelated memory. Both
  tries are skipped (crash fixes). The scan branch's unused row start
  faults only for an area inverted by exactly 8 rows; not computed.
- Four 1.06 AI sides start with no building and no unit, so their box
  at activation is the inverted W..0 / H..0 (golden test): A5V1 side 6,
  A7V1 side 3, H6V2 side 3 and O9V1 side 4 (their units arrive later by
  the mission's events). With DefendOn (all but A7V1 side 3) ActivateAI
  gives them a base defend area with that box, which stays until the
  side's first building event (RebuildLists). If DoDefendChecks makes a
  defend group from units that arrived meanwhile, the exe's patrol reads
  memory past the map for its tries (above); ours skips them. That is a
  **known, unreproducible deviation**: the memory read is the BSS after
  the row table, runtime data of unrelated systems. Every other AI side
  of every campaign mission starts with x max > x min and y max > y min.

## 2. Missing: DoRepairUnits (`FUN_0041e650`, verified)

Gate in DoAI: RepairUnits (+0x1D14) set, +0x1D46 ≠ 0xFF and the side
owns a building of that group (`P+0x245C1[+0x1D46]` ≠ 0). Called as
`DoRepairUnits(0, base = +0x1D3C, guard = +0x1D3D)`. No rand().

```
+0x10 += 1
if +0x10 < UnitRepairTicks (+0x1D10, 32): return          // unsigned
+0x10 = 0
for g in 0..31: if G(g)+0x52 and G(g)+0x48 == 0xB: return  // one repair group at a time
n = 0; t = 0
for each unit u of P (P+8 list order):
    if u.group (+0x28) != base and != guard: continue
    if u+0x2A <= 12 or u+0x2B <= 12: continue               // hit / fired recently
    r = record(u+0x18)
    lim = ftol((i64)(u32)r.hp(+0x04) × UnitRepairValue[u+0x18] (+0x1AE8, f32))   // x87: FILD qword, FMUL dword
    if !(u.hp(+0x04) < lim): continue                       // signed
    if !(u.state == 2 or (RepairGuardingUnits +0x1D15 and u.state == 3)): continue
    if n >= +0x1D08 (5): continue
    if r.infantry (+0x11): continue
    t = FindFirstFreeGroup()                                // FUN_0041c3a0: 1..31, 0 = none
    if t == 0: continue
    price = UnitCost(rec, side)                             // FUN_00442bb0
    if CanSpendCash(2, (price / r.hp) >> 1):                // u32 divide
        n += 1; u.group = t
if n != 0 and t != 0:
    G(t)+0x52 = 1; G(t)+0x48 = 0xB (Repair)
    G(t)+0x38 = GetGroupStrength(t, 0xB)                    // FUN_0041c340
    G(t)+0x2E = 0xFFFF
    ClearUnits; SelectedGroup(t, 1)                         // FUN_0041cce0, engaged units too
    order 0x21 (FUN_004595e0(side, 0xFFFF))                 // the nearest pad
```

- FindFirstFreeGroup returns the same index on every call of the loop,
  because the group is marked in use only after it. `t` is the last
  value returned.
- **CanSpendCash** (`FUN_0041e8c0(job, amount)`): `m = cash + spice −
  CashStash[job]` (u32 wrap), `m = 0` when `(i32)m < 1`; returns
  `amount < m` (unsigned). Job 2 = CashStash[RepairUnits] (+0x105C).
- The order: ours `order_repair_units(s, -1)` (doc 21 §4: non-infantry
  selected units that take orders go to state 0x18, pad −1 = the
  nearest). Post it like the other AI orders.
- `+0x2A` / `+0x2B` are our `hit_age` / `counter_2b`.

**MonitorGroups job 0xB** (the only other reader, `0x41d070` case 0xB):
when the group's turn comes, `GroupAtFullHealth` (`FUN_0041e860`: every
unit of the group has hp = `record+0x04`, empty group → true) →
**ReturnToBase**; otherwise nothing (no DoStopStartStuff). ReturnToBase
makes it job 10, whose units leave the group inside the base box and
are regrouped by GroupNewUnits (doc 23). Ours sends job 0xB to the
default case (DoStopStartStuff); add the case.

## 3. Missing: auto-BESERK (`FUN_00423850`, verified)

Called every tick after BuyUnitsFromStarport. Only when `(tick & 7) ==
side`, BESERK (+0x1E) is 0 and practice (+0x03) is set. No rand().

```
noRefinery = P+0x245C1[group byte 0x6d60df (7 = REFINERY)] == 0
noHarvester = P+0x245B8 (harvester count, u8) == 0
noYardOrMcv = primary yard (P+0x26098) == 0xFFFF and P+0x24628[MCV record 0x6d60e7] == 0
noFactory  = the primary slots P+0x260A6 (Starport), +0x2609A (light),
             +0x2609C (heavy), +0x260A2 (barracks), +0x260AA (H palace),
             +0x260AC (O palace), +0x260A8 (A palace) are all 0xFFFF
broke = (u32)(spice + cash) < 400
if noRefinery and noYardOrMcv and broke: +0x1E = 1
if noFactory and noYardOrMcv: +0x1E = 1
if broke and (noHarvester or noRefinery): +0x1E = 1
```

Correction to doc 23 §6.2: the first test is the **refinery** group
(`0x6d60df` = 7, doc 13), not the yard. In our code: refinery count =
`owned_building_kind(s, 4)`, harvesters = `owned_kind(s, 1)`, the slots
= `pl.primary[2]`, `[7]`, `[8]`, `[11]`, `[13]`, `[17]`, `[18]`,
`[19]` (doc 12 §8 kinds), MCVs = `owned_units(s, 12)` (`+0x24628`
counts dying units: it only drops at the free after the death
countdown, §11 "unit counters and dying units").

Note the last rule: a skirmish AI that runs below 400 credits while its
refinery is gone, or while it has no harvester, goes BESERK at once
(sells everything over time, rampages). The BESERK tail (§7.1) and
GroupNewUnits' BESERK branch are already ported.

## 4. Partial: DoBuildUnits, the practice MCV (`FUN_0041b910`, verified)

In FindBestUnitToBuild, after the harvester test and before the ratio
pick:

```
if +0x03 (practice) and P+0x26098 == 0xFFFF (no primary yard)
   and P+0x24628[0x6d60e7 (MCV, 12)] == 0:
    if (rand() & 7) == 1:  return the MCV record        // rand() line 0x24F
```

- rand() is drawn only when the three conditions hold (short-circuit), and
  **before** the ratio pick's per-candidate draws.
- The MCV skips ShouldIBuildUnit, but DoBuildUnits (`FUN_0041bfe0`)
  always calls **CanBuildUnit** on the result before the factory test:
  `pick != -1 and CanBuildUnit(pick) and FindFactory(pick, side, …, 1)`
  → order 0x27. So the MCV needs to be in the unit build list and
  affordable. Ours calls `ai_can_build_unit` inside `ai_best_unit` only;
  for the MCV path add the call in `ai_build_units` (or return through
  the same check).
- Our `// TODO: practice mode MCV roll` in `ai_best_unit` is the place.
- The rebuilt MCV deploys through DoMCVs: no build list → deploy at
  once if `FUN_00443250` allows it, else DealWithMCV (ported).

## 5. Partial: MonitorGroups (`FUN_0041d070`, verified)

### 5.1 The size split (the `i == 0` pass)

Ours only frees empty groups. After that, still in the `(tick + off) &
0x1F == 0` pass, the exe splits groups larger than their job's maximum,
using the unit counts counted **before** the freeing loop (not updated
after a split):

```
for g in 0..31:                                    // in use or not
    if count[g] > MaxUnits[G(g)+0x48]:             // int table 0x4be350
        t = FindFirstFreeGroup()
        if t != 0:
            n = GetPercentage(g, 50)               // FUN_0041e5d0: +0x1D20 ? MaxBaseStrength×50/100 : G(g)+0x38 × 50 / 100
            CreateSplinterGroup(g → t, n)          // rand() line 0x645
            copy the 0x2C-byte group record G(g) over G(t)   // overwrites the splinter's strength / age / substate
            CommenceOperation(t)                   // FUN_0041fc80, with the selection as it is
```

MaxUnits by job (`0x4be350`, i32[19]): 0 NoPurpose 200, 1 Base **400**,
2 Guarding 60, 3 Protect 30, 4 Defend 30, 5 AttackBuilding **20**, 6
AttackUnit 30, 7 GuardBuilding 30, 8 Harvesters 60, 9 Carryalls 60, 10
Returning 40, 11 Repair 20, 12 TempGuard 60, 13 Deploying 60, 14
Rampaging **20**, 15 DefendArea **500**, 16 (engineers) 20, 17 10, 18
(saboteur) 10. Group 0 is counted too (units with group 0). With
skirmish armies, attack and rampage groups above 20 units are halved by
strength every 32 ticks. The copied record keeps the old group's
target, so both halves continue the same operation. CommenceOperation
runs with the side's selection as the earlier code left it: MonitorGroups
calls neither ClearUnits nor SelectGroup before `0x41d1a5` (its calls up
to there: CreateSplinterGroup, GetPercentage, FreeGroup lookup). So it
orders whatever units of the side are selected, often none. Ours does
the same; it depends on the selection side effects of the earlier
callees, which follow the exe (SendNearestUnit and the Rampaging monitor
leave their selection, corrected 2026-10-16).

### 5.2 Jobs 3, 4, 6, 7 and the flag-8 hand-off are dead

Job 3 / 4 / 6 / 7 groups are only made by DoAttackApproachers (job 7,
`0x4205a3`) and AttackBestUnit (`FUN_00420970`, job 6, called only from
the job 6 / 7 cases). No code sets group flag 8 (no write found), so
the job-5 substate-4 hand-off (`FUN_00422230`) never runs. No port
needed.

### 5.3 Job 0xF (DefendArea): patrol, scan, area refresh

Ours only calls DoStopStartStuff. The exe, when the group's turn comes
(after the shared prologue: age += 1, strength recomputed):

```
if +0x19EC == 0: break                              // nothing at all (practice before a yard)
a = G(g)+0x54; A = the area record +0x1D54 + a×0x14  // +0 x0, +1 x1, +2 y0, +3 y1
seen = 0
DoStopStartStuff(g, 8, 1)
if !RecentlyInAction(g) and (rand() & 1) == 0 and A.y1 != A.y0:   // rand line 0xA4B, only when not in action
    // patrol
    for each unit u of P (P+8 list):
        out = u.cx > P.xmax or u.cx < P.xmin or u.cy > P.ymax or u.cy < P.ymin
              or cell(u.cx, u.cy).flags & 0x10000 (sand)          // the PLAYER box, not the area
        if u.group != g or u.state not in {2, 3}: continue
        if (rand() & 0x7F) == 0 or u+0x2C > 200 or out:          // rand line 0xA57, per such unit
            x = rand() % (A.x1 - A.x0) + A.x0                   // line 0xA60 (A.x1 == A.x0 divides by 0)
            y = rand() % (A.y1 - A.y0) + A.y0                   // line 0xA61
            c = cell(x, y).flags
            if (c & 0x103F8) == 0 and (c & 0x2000)
               and (+0x0B == 0 or ((x < +0xF2C or x > +0xF34 or y < +0xF30) and y > +0xF38)):  // operator quirk kept
                ClearUnits; if u.state != 0x11: select u
                order move (x, y)                               // FUN_00459450, posted
else:
    // scan the area grown by 4 for enemies
    x0 = max(A.x0 - 4, 0); x1 = min(A.x1 + 4, W - 1)
    y0 = max(A.y0 - 4, 0); y1 = min(A.y1 + 4, H - 1)
    repeat 4 times:                                     // the row start computed per pass is unused
        for y in y0 .. y1 - 1: for x in x0 .. x1 - 1:
            c = cell(x, y).flags
            if alliance[side][c & 7] != 1: continue     // 0x798830, 1 = enemy
            if (c & 0x3E8) == 0:                        // no unit bits
                if (c & 0x10) and FUN_004a5640(x, y) finds building b of side s2
                   and !(b.flags & 0x4000) and b.kind != 0xE:
                    SendSomeUnitsToAttackBuilding(g, x, y, b, s2, 40)
                    return                              // whole MonitorGroups, skips the tail below
            else:
                v = FUN_004a5270(x × 32, y × 32)        // the unit on the cell
                if v and !(v+8 & 4):
                    seen = 1
                    k = 6 if v.kind == 4 (saboteur) else 4 if v.kind == 3 (engineer) else 2
                    repeat k times:
                        if SendNearestUnit(g, x, y, 0) and +0x1D and a == +0x1D52 and (tick & 0xF) == 0:
                            +0xF18 += 1                 // delays the next building attack
// tail (also after the patrol)
if +0x1D and a == +0x1D52 (the base area):
    +0x1D1D (emergency) = seen                         // the patrol branch clears it
    if (tick & 0xE0) == 0:
        if +0x1D07 == 0xFF: +0x1D07 = g
        A.x0..A.y1 = P.xmin, P.xmax, P.ymin, P.ymax   // follows the base box
        A.wanted (+8) = P has buildings ? ftol(sqrt((f64)(i32)((A.y1 - A.y0) × (A.x1 - A.x0))) × (u32)GuardGroupSize) : 0
```

- **SendSomeUnitsToAttackBuilding** (`FUN_00423440(g, x, y, b, s2,
  pct)`): ClearUnits; for each unit of group g (list order): `rand() %
  100 <= pct` (line 0x1C0E, one per unit) → select it. If any: order
  0x1F attack building `FUN_00459800(side, s2, b)`, return 1.
- **SendNearestUnit** is our `ai_send_nearest_unit`. Each call posts a
  move for the nearest idle unit; the unit stays in state 2 until the
  order runs (orders run 2 ticks after they are posted, doc 49), so the k calls and the 4
  passes pick the same unit again and post one move each (verified from
  the code; nothing in between changes the unit's state). The metric
  `FUN_00421800` is the squared distance from the unit's pixel cell
  `(x / 32) / 65536` (not +0x4A / +0x4B; corrected 2026-10-16), and the
  routine leaves the unit selected (no ClearUnits after the order).
- The wanted formula is x87: `FILD dword (signed)`, `FSQRT`, `FILD
  qword` (u32 GuardGroupSize), `FMULP`, `ftol`. The exe runs the FPU at
  53 bits (the CRT's `_controlfp(_PC_53)`, doc 22 §3.2), so a double
  computation is exact (§11).
- `+0x0B`: the only writers are InitAI (`0x41b210`, 0) and the rebuild
  placement reset (`0x41f4f6`, 0), and every `.MIS` slot carries 0, so
  the rect test never applies (unreachable; ours keeps its operators).
- `v+8 & 4` is the cloaked flag (doc 32; §11).
- The emergency flag +0x1D1D switches DoBuildUnits to EmergencyBuildRate
  (1 tick, doc 29 §2.3): while enemies stand near the base the AI orders
  a unit **every tick** it can afford.
- `+0x1D07` is set here, not in DoDefendChecks (ours sets it at group
  creation, §6). DoDefence uses `+0x1D07`.

In skirmish (+0x1D = 1, GuardGroupSize 300) this is the main base
defence. Gated by +0x19EC: zero until the first yard exists (§1.4).

### 5.4 Job 5, substate 7: the enemy-base waypoint

Ours has a TODO after `ai_attack_best_building` with the new side. The
exe (`0x41d4f0..`):

```
// substate 7, GroupStable(g) and age > 10, and +0xF47 < rand() % 100 (line 0x8C6):
(x, y) = GetAveragePositionOfGroup(g)
side = GetBestSideToAttack(x, y)                       // FUN_0041cdf0
AttackBestBuilding(g, side, x, y)                      // FUN_0041fee0: sets G(g)+0x50/0x51 to the target
G(g)+0x44 = 0; G(g)+0x49 = 8
GetEnemyBaseWaypoint(g, &x, &y, G(g)+0x50, G(g)+0x51)  // FUN_00420b20
SendGroupToPoint(x, y, g)                              // FUN_0041e3c0
return
```

**GetEnemyBaseWaypoint** (`FUN_00420b20`, verified from the disassembly):

```
a = angle12(x - tx, ty - y)                // FUN_0049e160, 0..0xFFF
a = (a + 0x40) & 0xFFF                     // the exe's abs / sign dance is a no-op for a >= 0
d = ((a >> 7) - 2) & 0x1F                  // a 32-step facing
rand()                                     // line 0x1394, result unused (keep the draw)
x = tx; y = ty
x -= trunc0(flight_dx(d, 0x160) / 65536)   // DX table 0x6e9840, index d × 512 + 352
y -= trunc0(flight_dy(d, 0x160) / 65536)   // DY table 0x704a98; about 11 cells
x = x > 0xC0 ? 0 : min(x, W - 1)           // u8 arithmetic: a wrap below 0 lands > 0xC0
y = y > 0xC0 ? 0 : min(y, H - 1)
// loop until NoHostileUnitsWithinRange(x, y, 4): FUN_00420c60 always returns 1, so once
```

`trunc0` = the exe's `(v + ((v >> 31) & 0xFFFF)) >> 16`. So the group
first moves to a point about 11 cells from the target building, on the
side facing the group (rotated by the −2 facing steps), then attacks
from substate 8.

**SendGroupToPoint** (`FUN_0041e3c0(x, y, g)`): ClearUnits;
SelectedGroup(g, 1); G(g)+0x50 = x; G(g)+0x51 = y; order move (x, y).

### 5.5 Small differences in the shared prologue

- The exe's in-use clearing keeps the base group (+0x1D3C) and **+0x1D07**
  (the base-area defend group), like ours.
- After the target check, `if (i == 0) return` — ours too.

## 6. Partial: DoDefendChecks (`FUN_004214a0`, verified)

Ours tracks one defend group in +0x1D07 and tops it up by moving units
directly. The exe:

```
if +0x1E != 0: return                                  // any BESERK value
for a in 0 .. +0x1D51 - 1:
    A = +0x1D54 + a × 0x14
    if tick % A.period (+0xC) != 0: continue
    g = GetDefendGroupId(a)                            // FUN_00422230: the LAST in-use group with job 0xF and G+0x54 == a, else 0xFF
    if g == 0xFF:
        t = FindFirstFreeGroup(); if t == 0: continue
        CreateSplinterGroup(base → t, A.wanted × 20 / 100)        // i32; rand line 0x645
        if G(t)+0x38 == 0: G(t)+0x52 = 0; continue
        ClearUnits; SelectedGroup(t, 0)
        SendGroupToPoint(A.x0 + (A.x1 - A.x0) / 2, A.y0 + (A.y1 - A.y0) / 2, t)
        G(t)+0x48 = 0xF; G(t)+0x54 = a; G(t)+0x2E = 0xFFFF
    else:
        now = G(g)+0x38; w = A.wanted
        if now >= w: continue                          // signed
        need = min(w - now, w × 20 / 100)
        t = FindFirstFreeGroup(); if t == 0: continue
        CreateSplinterGroup(base → t, need)            // rand line 0x645
        if G(t)+0x38 != 0:
            SelectedGroup(t, 0)
            SendGroupToPoint(centre of A, t)           // as above
            MergeGroups(t → g)                         // FUN_00421710, ours ai_merge_groups
        G(t)+0x52 = 0
```

- The top-up goes through CreateSplinterGroup (no harvesters /
  carryalls, state ≠ 8, the GroupSizeRandom factor and its rand()), not
  "any base unit" as ours. That rand() is part of the sequence.
- **+0x1D07 is not set here** (§5.3 sets it). Drop
  `set_ai_u8(s, kDefendGroup, g)` from DoDefendChecks and use
  GetDefendGroupId for the lookup.
- SendGroupToPoint selects with "all" (engaged units too) after the
  SelectedGroup(t, 0) call; the move is posted.

## 7. Smaller gaps

### 7.1 BESERK tail: rand() for walls

DoAI's BESERK loop (`0x41b7xx`) tests `flags & 0x4000006`, then draws
`rand()` (line 0x1C0), **then** tests kind 0xE (wall). Walls consume a
rand() draw. Ours skips walls before the draw. Fix the order in
`ai_beserk`.

### 7.2 DoStopStartStuff (`FUN_0041f670(g, n, retarget)`, verified)

Our `ai_stop_start` has four TODOs. Exe per unit of the group (list
order):

1. **Engaged** (flag 0x100000): if the unit's target (`+0x0C` index,
   `+0x26` side) is none → clear the flag. Else, when the target is a
   unit: `d = dist²(cells)` (`FUN_00421770`: cell coordinates of both,
   dx² + dy²; this answers doc 23's TODO), `R = max(range(weapon 1),
   range(weapon 2)) >> 5` (`0x782080 + w × 0x1C`, the second only when
   ≠ −1); if `R × R < d` and `VARS 0x6b885c < d − R` (unsigned): clear
   the **attacker** fields (+0x32 = 0xFFFF, +0x30 = 0xFF) and the flag;
   unless dying: select the unit, CommenceOperation(g), deselect it.
   (The test mixes d² and R, kept.) `0x6b885c` is VARS.BIN +0x44
   (`Vars::chase_give_up`, 2 in 1.06; §11).
2. **Not engaged, hit recently** (`UnitRecentlyHit` `FUN_0041e310`: not
   dying and +0x2A < 12): if the attacker side is not allied, state ≠ 3,
   attacker ≠ 0xFFFF:
   - **unarmed** (`rec+0x0C` = 0xFF): ClearUnits; select it; if state 2:
     `FindFleeCell` (`FUN_0049e290(side, &x, &y)` from the unit's cell:
     the last cell of x−1..x+1 × y−1..y+1 (clamped to ≥ 0 and < W / H)
     with no object bits 0x3F8 and drivable 0x2000; if none, the last
     cell whose owner bits = side, objects ≠ 0x10 exactly and drivable)
     then order move there; reacted = 1; **next unit**.
   - armed: if the attacker is a unit with flags & 0x400140 → nothing;
     else ClearUnits, select it, AttackObject(u, side, idx) (ours
     `ai_attack_object`; a **building** attacker gets order 0x15 there,
     doc 23 §5.3) and set the engaged flag.
   - reacted = 1. A recently hit unit never reaches step 3.
3. **Idle** (+0x2C > 10 and +0x2B > 10): `t = u+0x0C`;
   - if not engaged, or RecentlyInAction(u), or +0x2C < 11: if
     `retarget` and `rand() % n != index % n` (line 0x103D): t = −1;
     **FindMostAppropriateTarget** (below); if found and not (a unit
     with 0x400140): ClearUnits, select, AttackObject, engaged, done.
   - else (engaged and quiet): released += 1, clear engaged, ClearUnits,
     select it, done.
   - if `t == −1` and `((tick >> 5) & 7) == (index & 7)`: scan cells
     x−5..x+5 (x0 = max(x−5, 0), x1 = min(x+6, W−1), exclusive) and the
     same for y; every cell with 0x1000 (cell object) whose
     `FUN_0044e9d0(x, y)` type is < 7 or > 11 gets ClearUnits, select
     the unit, order move there (the last one wins). `FUN_0044e9d0` also
     **removes** objects of other types as a side effect; this is doc
     31 §6.3's crate-scan quirk.
   - if not done: select it, released += 1.
4. After the list: released ≠ 0 → CommenceOperation(g).

**FindMostAppropriateTarget** (`FUN_00420140(u, &idx, &side)`): over the
other units v of the same group (list order) that are
RecentlyInAction: `d = dist²(u, v)`; skip if v's target is a unit with
flags & 4 or `d >= 0xFFF`. Then: if v's attacker is set, hostile, not a
worm (`FUN_00421860`: kind 5) and not 0x400140 (`FUN_004218a0`) → take
v's **attacker**; else if v's target is set, the alliance byte of v's
**attacker side** (+0x30, a quirk; §11) is set, not a worm and not
0x400140 → take v's **target**. The last match wins (no distance
ranking). `flags & 4` is the cloaked flag (doc 32).

### 7.3 Stale notes in our code

- `ai_special_weapons`' header says only case 3 is ported; all cases are.
- `docs/parity.md`'s AI row lists DoProtection, DoDefence, carryalls,
  upgrades and Starport as open; they are implemented. Its real open
  items are this doc's.

## 8. Checked and complete (no gap found)

GetSelected / ResetSelected; DoMCVs (+0x19EC = 1 with a build list; every
64 ticks, idle MCVs that take orders: deploy if `FUN_00443250` and
(+0x2E or no build list), else DealWithMCV); GroupNewUnits; DoRepairs;
MonitorHarvesters; DoOverrides; DoAttackBuildings (the rand() line
0x5FC is drawn even when no group is free; ours only skips the group);
DoProtection (rand 0x40B before the slot search; airlift to the base
position; escort unless lifted and AirliftAndAttack is 0);
DoSpecialWeapons; DoDefence; MonitorBuildings; DoBuildCarryalls;
RebuildBuildings incl. the practice path; UpgradeBuildings;
BuyUnitsFromStarport incl. the practice MCV purchase (rand line 0x1775);
ClearUnits.

## 9. rand() call sites added by this doc

| Where | Line | When |
|---|---|---|
| FindBestUnitToBuild, practice MCV | 0x24F | practice, no primary yard, no MCV; before the ratio pick |
| MonitorGroups split (CreateSplinterGroup) | 0x645 | per split group, `i == 0` pass |
| MonitorGroups job 0xF | 0xA4B | not in action |
| | 0xA57 | per group unit in state 2 / 3 (patrol) |
| | 0xA60, 0xA61 | per unit moved (x then y) |
| SendSomeUnitsToAttackBuilding | 0x1C0E | per group unit |
| GetEnemyBaseWaypoint | 0x1394 | unused value |
| DoDefendChecks (CreateSplinterGroup) | 0x645 | creation and every top-up |
| BESERK | 0x1C0 | also for walls (§7.1) |
| FindFleeCell | none | |
| DoRepairUnits, auto-BESERK | none | |

## 10. Implementation checklist (skirmish order)

1. **Skirmish set-up prerequisites** (docs 51 / 52): load
   `Data\maps\_PRAC.MIS`; copy slot 0's block to slots 1..7 with the
   side byte; active = 1 for slots 1..N; random houses (`rand() % 3`);
   tech / credits (7000 / 10500 / 14000 for the AI); start positions,
   MCV and starting units (§1.1, §1.2).
2. **ActivateAI**: +0x1D46 (§1.4.1), skip the guarding GroupNewUnits in
   skirmish (§1.4.2), +0x19EC = 0 in practice (§1.4.3).
3. **Base box** as RebuildLists computes it, refreshed in
   `rebuild_lists` and stored per player (§1.6). Used by the practice
   building placement, so the skirmish base layout depends on it.
4. **Practice MCV roll** in FindBestUnitToBuild, with CanBuildUnit in
   DoBuildUnits (§4).
5. **Auto-BESERK** (§3).
6. **MonitorGroups job 0xF** patrol / scan / area refresh / +0x1D07 /
   emergency flag (§5.3), with SendSomeUnitsToAttackBuilding and the
   +0xF18 bump.
7. **DoDefendChecks** per area with GetDefendGroupId, CreateSplinterGroup
   top-up and the zero-strength free (§6).
8. **DoRepairUnits** and MonitorGroups job 0xB (§2).
9. **MonitorGroups size split** (§5.1).
10. **Job 5 waypoint** (GetEnemyBaseWaypoint, SendGroupToPoint, the unused
    rand()) (§5.4).
11. **DoStopStartStuff** TODOs: give-up distance, unarmed flee,
    FindMostAppropriateTarget, building attackers, the cell-object scan
    (§7.2). These also change the campaign.
12. **BESERK wall rand()** order (§7.1). Campaign too.
13. FreePercentage splitter (§1.4.5): 100 in A4V1 side 4 and H4V1 sides
    3 and 4 (ported).
14. Tests: a seeded skirmish start (one AI): the MCV deploys on the first
    DoMCVs pass (`tick & 0x3F == 0`), the first slab order on the first
    RebuildBuildings pass after the yard, unit orders every 80 ticks, the
    repair group after damage, BESERK after the refinery is lost below
    400 credits. Compare with the reference game; the rand() order of
    §9 must hold.

## 11. Implementation notes (2026-10-13)

Every item of §10 is ported (`ai.cpp`, `ai_groups.cpp`, `economy.cpp`,
`save_state.cpp` state 18). Checked again in the exe while porting:

- **Base box timing.** The mission set-up (`FUN_00448550`) runs
  RebuildLists for every player at `0x448a89`, after the map objects and
  the skirmish start units (`FUN_00469790`, `0x4488f8`) and right before
  the ActivateAI loop (`0x448b06`), and again for every active player at
  `0x448ba9`. Ours takes the box in `rebuild_lists` and again in
  `set_ai` before activating (our `init_players` runs before the
  skirmish start). So the skirmish defend area starts as the units' box.
- **CreateSplinterGroup** (`0x41ca81`) draws rand() line 0x645
  unconditionally and divides by `2 × GroupSizeRandom`: a zero setting
  would crash the exe. Its strength sum uses the **target group's job at
  that time** (DoDefendChecks sets job 0xF only afterwards).
- **MaxUnits** `0x4be350` read from the exe: 200, 400, 60, 30, 30, 20,
  30, 30, 60, 60, 40, 20, 60, 60, 20, 500, 20, 10, 10 (as §5.1).
- **Job 0xF scan**: `v+8 & 4` is the cloaked flag (doc 32). The number
  of SendNearestUnit calls is 6 for unit kinds 4 (saboteur) **and 0x10**,
  4 for kind 3, else 2 (`(kind != 0x10) - 1 & 4) + 2`).
- **Job 0xF**: with +0x19EC = 0 nothing runs, not even DoStopStartStuff.
- **DoDefendChecks** returns for any BESERK value (+0x1E ≠ 0).
- **DoStopStartStuff**: the give-up distance is VARS +0x44 (`0x6b885c`,
  u32, 2 in 1.06 VARS.BIN; `Vars::chase_give_up`). The selection is not
  cleared at the end. FUN_00421770 is the squared distance of the pixel
  cells (x / 32 / 65536, rounding toward zero).
- **FindMostAppropriateTarget**: the target branch tests the alliance
  with the unit's **attacker side** (+0x30), not the target's side. A
  unit that was never hit has +0x30 = 0xFF (CreateUnit), so the exe reads
  `0x798830 + s × 8 + 0xFF` = `0x79892F + 8·s`, past the alliance table
  (the index is the zero-extended byte, `0x420241` / `0x420299`). **Settled
  2026-10-16: the byte is 0 (not hostile), as ours, unless side 0 has
  held 999 pool objects at once.** The address lies in side 0's object
  pool: side 0's record starts at `0x798870`, slot i at record + 0x30 +
  i · 0x94 (`FUN_004a46b0`; the reset's second loop at `0x46ba66` writes
  slot + 0x84 / + 0x88 / + 0x8C from `[esi + 0xB8]`; the earlier note's
  "slots start at +0xAC" was the first slot's +0x7C link). So s = 0
  reads slot 0 +0x8F, and s ≥ 1 reads slot 1 byte +0x03 + 8·(s − 1)
  (+0x03, +0x0B, …, +0x33). The side reset `FUN_0046b990` (run for every
  side at mission start, `FUN_004a45d0`) zeroes +0x00..+0x7B of every slot
  (`rep stosd` at `0x46ba92`), and no code writes +0x8E / +0x8F (operand
  scan). The free list is LIFO with its head at slot 999 (`0x46ba61`;
  pop `FUN_0046bd40` takes the head, the free at `0x46bdb0` pushes back
  to it), so slot 1 is first used when side 0 holds 999 objects (units,
  buildings, bullets, effects and leaked bullet slots, doc 50) at the
  same time, and slot 0 at 1000. Below that every byte read is 0. Above
  it, slot 1's live bytes decide (speed, flags, state, turret counter, …
  of whatever object holds it); that case is not modelled (it needs a
  slot-exact pool, doc 50). The read only matters in the second branch
  (a member with a target and no attacker); in DoStopStartStuff the
  attacker test (`+0x32 ≠ 0xFFFF`) is in the same condition, and every
  writer of +0x30 other than CreateUnit (0xFF) and the give-up
  (`0x41f791`, 0xFF with +0x32 = 0xFFFF) is the damage code (`0x49d96c`,
  `0x49dbbd`, `0x49f91c`), which stores the side of the hitting bullet
  or explosion, an object of a side pool (0..7); so the result does not
  depend on the byte there.
- **AttackObject** (`FUN_00420010`) writes the unit's target first; an
  aircraft target only clears the attacker; the squish move goes to the
  target's pixel cell.
- **BESERK tail** (`0x41b7xx`): the group of the tick gets job 0xE, the
  timer, substate 4, no target, **side 0** and its position; the age and
  the in-use byte are not written (ours used to reset the age).
- **ActivateAI** also stores the guarding group's strength with job 1's
  weights (`FUN_0041c340(guard, 1)` into +0x38 / +0x3C; without it the
  group was freed on its first MonitorGroups turn), sets +0x1D52 = 0xFF
  when the defend area is off and the area's +0x10 to 0xFFFFFFFF.
  FreePercentage splits to group 0xFF, whose record lies past the block:
  block(s) + 0xFF · 0x2C + off = block(s + 1) + 0xE48 + (off − 0x2C).
  CreateSplinterGroup reads the "job" at block(s + 1) + 0xE64 for every
  unit's strength and writes the strength (+0xE54, +0xE58), age (+0xE60),
  substate (+0xE65) and in-use (+0xE6E) there: the next side's
  initial-building entries 138 and 139 (16 bytes each from +0x5AC), which
  nothing reads below the count (at most the side's non-wall buildings,
  far below 139; GetInitialBuildingPositions rewrites the entries it
  uses). The job byte is 0 in all three 1.06 cases (A4V1 side 4 → block
  5, H4V1 sides 3 and 4 → blocks 4 and 5) and nothing writes it, so the
  weights of job 0 apply, which ours uses (golden test). Its loop test is
  unsigned like every CreateSplinterGroup (below).
- **CreateSplinterGroup's loop test is unsigned** (`CMP ebp, limit; JAE`
  at `0x41cae5`): the limit from the percentage quirk (doc 23 §4.1) is
  often negative (off < 0: e.g. −50 for off = −5 and 5000), and then
  **every** eligible unit of the source group moves. Ours compared signed
  and moved none (corrected 2026-10-16). This affects every split: the
  building attacks (DoAttackBuildings), DoDefendChecks, DoProtection, the
  halving in MonitorGroups and FreePercentage.
- **FindGroupForDelivery** (`FUN_00420480`) with no free group and no
  Deploying group of the entry keeps 0xFF and indexes the record with
  `MOVSX` (`0x4204c6`), i.e. at block − 0x2C: its "in use" byte is +0x26
  (byte 2 of the remembered x +0x24, so 0 once GetBestBasePosition has
  stored a cell, 0xFF before), and when it is 0 the set-up writes the
  side's own header: +0x26 = 1, +0x1D (DefendOn) = entry, +0x1C (Squish)
  = 0xD, +0x0C = 100, u16 +0x02 = 0xFFFF (side to attack, and
  EnablePractice +0x03 = 0xFF). It returns 0xFF: the unit gets no group,
  and the callers' flag writes at group 0xFF (`0x41c623`, `0x49868b`) land
  in block(s + 1) +0xE68 / +0xE6F (entry 139 again). Ours does all of
  this since 2026-10-16 (it used to pick group 0); only side 7's
  group-0xFF writes, which fall past the eight blocks (`0x503D40`), are
  dropped. It needs all 31 groups in use.
- **GetBestBasePosition** (`FUN_0041e180`) remembers its first result in
  +0x24 / +0x28 (−1 from InitAI `0x41b1f1`; stored at `0x41e1d9` while
  +0x24 is −1, also a first (0, 0)); a later (0, 0) result gives the
  remembered cell's low bytes. Then x ≥ W − 1 → W − 1 (y likewise; the
  bytes are never negative). Ours never stored it and read −1 (corrected
  2026-10-16).
- **GetBestSideToAttack** (`FUN_0041cdf0`) steps the countdown +0x18
  first (when non-zero) and recalculates when it is then 0 (or no side,
  or the side is not an enemy), restarting at 25: one call earlier than
  ours did (corrected 2026-10-16).
- **GetNearestHostileThing** (`FUN_00420700`): see doc 23 §6.3
  (corrected 2026-10-16: pixel cells, the success rule, the group cell).
- **ClearUnits points** (`FUN_0041cdb0`, all 28 call sites listed with
  xrefs, checked 2026-10-16). DoAI ends with one (`0x41b831`), so a
  selection never outlives the side's turn, but within it the routines
  see what the earlier ones left. Corrected in ours: ReturnToBase and
  AttackBestBuilding cleared again after their order (the exe does not);
  ReturnToBase selected engaged units (the exe passes 0 to SelectGroup)
  and posted its own move instead of CommenceOperation → SendGroupToPoint;
  AttackBestBuilding's "no building" path selected and cleared (the exe
  does neither); SendNearestUnit and the Rampaging monitor cleared after
  their order. SelectUnit (`FUN_0041cd50`) skips a dying unit (state
  0x11) and is applied as such at every port of it; in GroupNewUnits'
  BESERK branch it runs whether or not SetState(2) succeeded (`0x41c4eb`
  → `0x41c4fd`).
- **MonitorHarvesters** (`FUN_00421fa0`): the crusher that was hit by
  infantry gets ClearUnits, is selected and receives a **posted move
  order** to the attacker's pixel cell (`FUN_00459450`), not a direct
  state change; the attacker test is the slot type (a dying attacker
  still counts) and the crusher must not be dying. The harvester rules
  are two separate tests (idle → 0xB; then hit and hp < max >> 2 →
  0xD), not "else". Corrected 2026-10-16.
- **GetAveragePositionOfGroup** (`FUN_00420a70`) averages the pixel cells
  `(x / 32) / 65536` (`0x420aa9..0x420adc`), not +0x4A / +0x4B (doc 57a).
- **Done (2026-10-14): `FUN_00420c80` "DoUnitFacings"** (`0x41b464`,
  after the guarding GroupNewUnits, campaign and skirmish). With a primary
  yard (`+0x26098` ≠ 0xFFFF), every unit in the side's list (dying and
  carried ones too) gets +0x54 / +0x55 / +0x56 = `FUN_0049e160((tx ·
  0x200000 − x) / 65536, (y − ty · 0x200000) / 65536) / 128` (divisions
  truncated), with the base centre `FUN_0046e3f0` (our
  `retreat_destination`, cell · 32 = the cell's top-left corner).
  `FUN_0049e160` takes dx = from.x − to.x, dy = to.y − from.y (doc 09), so
  "from" is the base and "to" the unit: **every unit faces away from the
  base**, outward like a sentry. (The first version of this note said
  "toward"; corrected 2026-10-14 after the H8 playtest.) Units that the
  mission's events add after activation (H8: 14 Ordos units on the first
  ticks) are not turned and keep CreateUnit's facing 0 (north). In
  skirmish the AI has no yard at activation, so it does nothing there.
  Port: `World::ai_unit_facings`.
- **Done (2026-10-14): unit counters and dying units.** `+0x24628[rec]`
  (CreateUnit `0x455af6`), harvesters `+0x245b8` and carryalls `+0x245c0`
  only drop at the free after the death countdown (`FUN_00458e90`,
  `0x458f33` / `0x458f47` / `0x458f6d`): **dying units count**. The
  carryall count leaves out delivery carryalls (flag 0x800: the delivery
  code `FUN_0046def0` undoes its +1 at `0x46e136`, and the free skips
  them). `owned_units` / `owned_kind` now do the same (they used to skip
  state 0x11 and count delivery carryalls).
- **Done (2026-10-14): the defend area's wanted strength** `ftol(sqrt(area)
  · GuardGroupSize)` is the same in double as on the x87: the product is
  exact for a square area, otherwise at least 1/(2k) from any integer k,
  far above the rounding error while k < 3·10⁷. Area ≤ 255², and the
  largest GuardGroupSize in the 44 mission files is 1000 (test).
- **The division guards** (settled 2026-10-16). Each one is either a
  case the 1.06 data never produces or an integer division fault (a
  crash) in the exe, so the guard is a crash fix (CLAUDE.md §3):
  - the defend period (DoDefendChecks `tick % period`): 1000 in the
    A4V1 / H4V1 areas, 100 for ActivateAI's base area (`0x41b579`), and
    no other binary-mission code writes it: never 0 (golden test);
  - ProtectTicks (DoProtection): 4 in InitAI (`0x41b0b7`) and every
    slot; GroupSizeRandomPercentage (CreateSplinterGroup `DIV 2q` at
    `0x41ca97`) and +0x0A (FindBestUnitToBuild) are 20 everywhere;
  - the job 0xF patrol, FindRandomBuildingPosition and the scan's unused
    row start: only with a flat or inverted base box, see §1.6 (the
    inverted FindRandomBuildingPosition case is ported, not guarded).
