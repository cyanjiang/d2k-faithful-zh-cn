# 23: Computer player, military part (teams, attacks, defence) (1.06)

Covers how a computer side (AI) groups its units, when and what it
attacks, how it defends, and the special modes (BESERK / rampage). The AI
block layout, activation details, base building, production, harvesters
and money are in **doc 22** (`docs/re/22-ai-core.md`); this doc names
fields by their `[AI]` keyword and offset in the per-side block.
Decompiles from the local GhidraMCP server (`re/ghidra_http.py`), x87
arithmetic from the disassembly, data from the 1.06 `MISSIONS\_*.MIS`.

"Verified" = read from the code. Every earlier guess was settled from
the exe on 2026-10-16 (evidence next to each point); doc 53 is the later,
more detailed walk through the same functions and wins where the two
differ.

## 1. Where it runs

- AI block per side: `0x4f4118 + side·0x1DB8` (file `.MIS` +0x58, doc
  19 §1.5). Byte +0 = side, +1 = active.
- `FUN_0041b250` ActivateAI (set-up), `FUN_0041b610` DoAI, **every tick**
  for each active side (doc 19 §2.1: before the script). All functions
  below are methods of the block (`ECX` = block) and start with a trace
  label `"<Name> AI"`; the names here are those labels.
- rand() is the exe's MSVC LCG (`thunk_FUN_004b1810` / `FUN_00448460`,
  `x = x·0x343FD + 0x269EC3; return (x >> 16) & 0x7FFF`), the same stream
  the simulation uses: **the order of the AI calls below is part of the
  random sequence** (§8).

### 1.1 DoAI order (`FUN_0041b610`, verified)

```
GetSelected (save), DoMCVs, GroupNewUnits(base group, 0), DoRepairs,
MonitorHarvesters, DoBuildUnits, DoBuildBuildings, DoOverrides,
[GuardBuildings +0xF56] DoAttackApproachers,
[RepairUnits +0x1D14 and +0x1D46 group exists] DoRepairUnits,
DoAttackBuildings, DoProtection, DoSpecialWeapons, DoDefence,
MonitorGroups(0), MonitorGroups(0x10), MonitorBuildings, DoDefendChecks,
DoBuildCarryalls, [RebuildBuildings +0x1D45] RebuildBuildings,
[UpgradeBuildings +5] UpgradeBuildings, [BuyUnits +4] BuyUnitsFromStarport,
AutoBeserkCheck (FUN_00423850),
[BESERK +0x1E = 1 or 2] sell + rampage step (§6.1),
ClearUnits, ResetSelected
```

In this doc: GroupNewUnits, DoAttackApproachers, DoAttackBuildings,
DoProtection, DoDefence, MonitorGroups, DoDefendChecks, the BESERK
parts. The rest is doc 22.

## 2. Fields used here (verified offsets; text-loader keyword in brackets)

| Off | Type | Name | Notes |
|---|---|---|---|
| +0x02 | u8 | side to attack | 0xFF = none (§4.3) |
| +0x03 | u8 | [EnablePractice] | gates the auto-BESERK check (§6.2) |
| +0x14 | u32 | [SquishRange] | text: `a b` → a·b (a squared range) |
| +0x18 | u32 | side-choice countdown | 25 calls (§4.3) |
| +0x1C | u8 | [SquishInfantry] | §5.3 |
| +0x1D | u8 | DefendArea active | cleared at activation when GuardGroupSize = 0 |
| +0x1E | u8 | BESERK state | 0 off, 1 / 2 on (event BESERK sets 1, doc 19 §3.2) |
| +0x2C + g·0x2C | 32 × 0x2C | **groups** (§3) | |
| +0xF0D | u8 | [FreePercentage] | |
| +0xF10 | u32 | [FirstAttackBuildingDelay] | |
| +0xF14 | u32 | [TimeBetweenBuildingAttacks] | event SETATTACKBUILDINGRATE |
| +0xF18 | i32 | building-attack countdown | |
| +0xF1C / +0xF20 | u32 | FirstAttackUnitDelay / TimeBetweenUnitAttacks | **dead**: only copied at activation into +0xF24, never read (§4.5) |
| +0xF3C | u8 | AttackUnitStrength | dead (not read) |
| +0xF3D | u8 | [AttackBuildingStrength] | percent |
| +0xF3E | u8 | [ProtectStrength] | percent |
| +0xF3F | u8 | [DefendStrength] | read only at activation (doc 22) |
| +0xF40 | u8 | (approachers strength) | DoAttackApproachers |
| +0xF41..F45 | u8 | [MaxAttackUnitGroups] … [MaxGuardGroups] | |
| +0xF47 | u8 | (retarget chance) | §4.4 substate 7 |
| +0xF48 / +0xF4C | u32 | [MinBaseStrength] / [MaxBaseStrength] | |
| +0xF50 | u32 | [BaseDefenceRadius] | DoAttackApproachers |
| +0xF56 | u8 | [GuardBuildings] | **cleared at activation** (§5.2) |
| +0xF57 / +0xF58 | u8 | base position | `FUN_0046e3f0` at activation |
| +0xF5C | u32 | [GuardGroupSize] | defend-area strength factor |
| +0xF64 | f32 | distance weight in GetBestBuilding | file value (1.0 in 1.06 missions) |
| +0xF6C | u8 | [AttackBuildingRandomPercentage] | |
| +0xF6E | u8 | [TimingRandomPercentage] | |
| +0xF6F | u8 | [GroupSizeRandomPercentage] | |
| +0xF70 / +0xFBC / +0x1008 | f32[19] | [SpeedWeight] / [HPWeight] / [FirePowerWeight] per group job | |
| +0x1078 | u8[19] | [Morale] per group job | |
| +0x121C | f32[100] | [AttackBuildingPriority] per building record | |
| +0x1CC8 | u8[60] | [ProtectUnit] per unit record | |
| +0x1D07 | u8 | defend-area group | 0xFF none |
| +0x1D1C | u8 | [MoraleModifier] | |
| +0x1D1F | u8 | [AirliftPercentage] | |
| +0x1D20 | u8 | [UseIdealStrength] | |
| +0x1D34 | u32 | [ProtectTicks] | |
| +0x1D38 | u32 | [DefendTicks] | |
| +0x1D3C | u8 | **base group** index | set at activation |
| +0x1D3D | u8 | **guarding group** index | set at activation |
| +0x1D44 | u8 | [AirliftAndAttack] | |
| +0x1D51 | u8 | defend-area count | |
| +0x1D52 | u8 | the base's defend area | |
| +0x1D54 + a·0x14 | rec | defend area: x0, x1, y0, y1 (+0, +1, +2, +3), +8 wanted strength, +0xC period (ticks), +0x10 | |

Three keywords are "Command not implemented yet" in the text loader
(FirstAttackUnitDelay, TimeBetweenUnitAttacks, AttackUnitStrength); the
binary `.MIS` still carries values there, unused.

Unit fields the AI uses (unit object, verified offsets):

| Off | Meaning |
|---|---|
| +0x19 | selected (the AI **posts orders by selecting its units**, §3.3) |
| +0x28 | AI group (0xFF = none) |
| +0x2A | ticks/8 since last hit (UnitRecentlyHit `FUN_0041e310`: < 12). Unit update `FUN_00497b90`: flag 1 (hit this tick) → +0x2A = 0 and flag 1 cleared (`0x497fd6`); else when `(tick & 7) == (index & 7)` and not carried (flag 0x100): +1, capped at 255, and above 100 flag 8 is cleared (`0x498007`..`0x498036`) |
| +0x2B | ticks/8 since it fired (doc 14 counter) |
| +0x2C | ticks/8 since it stopped (GroupStable: all ≥ 10). Same update, before +0x2A: when `(tick & 7) == (index & 7)` and not an aircraft (flag 0x40): +1 (cap 255) while the pixel position's cell equals the current cell +0x4A / +0x4B, else 0 (`0x497f5c`..`0x497fcb`) |
| +0x30 / +0x32 | last attacker side / index (doc 18) |
| flags 0x100000 | engaged by the AI (DoStopStartStuff) |

## 3. Groups

### 3.1 Record (32 × 0x2C, group g at block + g·0x2C; offsets relative to that)

| Off | Meaning |
|---|---|
| +0x2C | target side (byte at the next record start: `block + (g+1)·0x2C`) |
| +0x2E | u16 target index (0xFFFF = none) |
| +0x34 | i32 timer (rampage: 10000 = practically forever) |
| +0x38 | i32 strength now (§3.2) |
| +0x3C | i32 strength at creation |
| +0x40 | i32 idle-pass counter |
| +0x44 | u32 age (+1 per monitor pass) |
| +0x48 | u8 **job** (names at `0x4e1b10`) |
| +0x49 | u8 substate |
| +0x4C | u32 flags (1, 4, 8) |
| +0x50 / +0x51 | destination / target cell |
| +0x52 | u8 in use |
| +0x53 | u8 mode (delivery groups) |
| +0x54 | u8 defend-area index |

Jobs: 0 NoPurpose, 1 Base, 2 Guarding, 3 Protect, 4 Defend, 5
AttackBuilding, 6 AttackUnit, 7 GuardBuilding, 8 Harvesters, 9
Carryalls, 10 Returning, 11 Repair, 12 TempGuard, 13 Deploying, 14 (0xE)
Rampaging, 15 (0xF) DefendArea.

Max units before a group is split (`0x4be350[job]`, checked at monitor
pass 0): 200, 400, 60, 30, 30, 20, 30, 30, 60, 60, 40, 20, 60, 60, 20,
500, 20, 10, 10.

At activation (`FUN_0041b250`): group 0 is marked in use; **group 1 =
Guarding** (+0x1D3D) gets every unit on the map (forced GroupNewUnits,
§3.4) and their guard order; **group 2 = Base** (+0x1D3C, empty). Then
FreePercentage % of the guard group's strength is split off into "no
group" (0xFF). So the **initial units guard and never join building
attacks**; later units go to the base group, which feeds the attacks.

### 3.2 Strength (verified)

`GetUnitStrength(u, job)` (`FUN_0041cb70`): 0 if dying, else
`ftol(rec.max_hp(+4) · HPWeight[job]) + ftol((rec.speed(+8) >> 16) ·
SpeedWeight[job])` (record values, not current hit points; FirePowerWeight
is not used). `GetGroupStrength(g)` = sum over units with +0x28 = g.

`GetPercentage(g, p)` (`FUN_0041e5d0`): UseIdealStrength ? MaxBaseStrength
· p / 100 : group[g].strength · p / 100 (signed int division).

### 3.3 Orders (verified)

The AI never sets unit states itself (except SetState 2 in BESERK §6.1):
it **selects** units (+0x19 = 1; `SelectedGroup FUN_0041cce0(g, all)`
selects group g's units that are not in state 8 or 0x11, and, unless
`all`, not flagged 0x100000), posts a normal order through the order
queue (`FUN_0045bf00`), and clears the selection (`ClearUnits
FUN_0041cdb0`, `ResetSelected` at the end of DoAI):

| Function | Order | Units taken (selected, at most 64) |
|---|---|---|
| `FUN_00459450(side, x, y)` | 0x13 move | not dying |
| `FUN_004596f0(side, tside, idx)` | 0x14 attack unit | not dying, armed (rec+0x0C ≠ −1) |
| `FUN_00459800(side, tside, idx)` | 0x15 attack building | not dying, kinds ≠ 1, 6, 0xE; not 3 / 4 when attacking its own side |
| `FUN_00459980(side)` | 0x18 guard | doc 21 §5 |

So AI units attack exactly as a player's would (states 4 / 5 / 7, doc
18). The order-queue delay: every order runs 2 ticks after it is posted
(doc 49; ours too since 2026-10-13).

### 3.4 GroupNewUnits (`FUN_0041c3e0(group, force)`, verified)

Runs when `(tick & 0x1F) == side` or forced. Needs a free group (else
nothing). For every unit of the side, in list order:

- **BESERK off:**
  - a group that is no longer in use → unit group 0xFF;
  - unit with no group, not flags 0x840:
    - kinds 1 (harvester), 2 (carryall), 6 skipped; kind 4 counted (MCV
      deploy, doc 22); kind 0xC counted;
    - in state 8, 0x14, 9 or mid-step: counted as "busy" (no group yet);
    - flags 0x80 or 0x1000 (`0x41c5b4`: 0x1000 = deviated, 0x80 = just
      reverted to its own side, doc 40 §5.1; only the deviation code sets
      them): a new **Rampaging** group of its own (timer 2, substate 4,
      target none), flag 0x80 cleared; a kind 7 unit with `rand() & 1`
      (line 0x53C) gets the self-destruct order `FUN_0045a3f0` (doc 37);
      no free group → nothing, retried next pass;
    - flag 0x40000 (just delivered): cleared; frigate deliveries go to the
      base group, carryall deliveries to the group of that delivery
      (`FUN_00420480`, flag 4 on the group); harvesters clear the group
      flags;
    - otherwise: **group = the base group, selected** → after the loop
      **guard order 0x18** for all of them (area guard, home = their "to"
      cell).
- **BESERK on (1 or 2):** every ungrouped, idle unit joins one free group;
  units in state 3 get SetState(2). With BESERK = 1 that group becomes
  **Rampaging** (§6.1).
- Then the base group's strength is recomputed. If no unit was "busy",
  the first group ≥ 1 with flag 1 is started by its mode (+0x53): 0
  `FUN_0041e490`, 1 rampage from the last unit's cell, 2 return to base.

## 4. Attacks

### 4.1 Timer formula (verified, x87) — note the unsigned quirk

`DoAttackBuildings` (`FUN_0041c920`) each tick: `t = +0xF18; +0xF18 =
t − 1; if t ≥ 1 return`. Else:

```
p   = TimingRandomPercentage (u8)
off = p − rand() % (2·p)                  // u32 arithmetic (p = 0: division by zero!)
v   = (u64)(u32)off                       // FILD qword with high dword 0: unsigned!
+0xF18 = ftol((v · 0.01f − (−1.0f)) · TimeBetweenBuildingAttacks)   // x87 (53-bit precision control), then fistp qword, low dword
```

For off ≥ 0 this is `interval · (1 + off/100)`. For **off < 0** the
value is ≈ 4.29e9, and the low 32 bits of the 64-bit result give a small
or negative number: with p = 25 and interval 5000, off −1 → **150**,
−5 → −50, −15 → −550, −24 → −1000 (≤ 0 means attack again next tick).
So about half of the time (off < 0), the next building attack comes
almost at once. Exact values: 0.01f = 0x3C23D70A; FILD exact; FMUL /
FSUB / FIMUL each rounded to nearest at **53 bits**, because the C
runtime sets the precision control to `_PC_53` at start-up (`__fpmath`
`0x4b0f30` → `__setdefaultprecision` `0x4b5af0`; doc 22 §3.2; corrected
2026-10-16 from "64 bits": the table above is the same either way);
truncate; low dword (signed). `src/sim/x87.hpp` does this.

The **first** countdown is set at activation, signed (no quirk):
`ftol((p − rand() % (2p) + 100) · 0.01f · FirstAttackBuildingDelay)`
(FILD dword, 3000…5000 ticks in A1V2).

`CreateSplinterGroup` (`FUN_0041ca60(from, to, strength)`) applies the
same formula to the size: `limit = ftol((u64)(u32)(q − rand() % 2q) ·
0.01f + 1) · strength)` with q = GroupSizeRandomPercentage — same quirk.
It moves units of group `from` (list order) that are not in state 8 and
not kind 1 or 2, adding GetUnitStrength(u, job of `to`) until the sum
reaches `limit` (checked before each unit, **unsigned**: `CMP ebp,
limit; JAE` at `0x41cae5`, so a negative limit from the quirk moves every
such unit; corrected 2026-10-16). Group `to`: strength = start
strength = sum, +0x44 = 0, in use, substate 0. (`q = 0` would be a
division fault, `DIV` at `0x41ca97`; it is 20 in every 1.06 slot.)

### 4.2 DoAttackBuildings (verified)

When the countdown runs out (above):
1. g = FindFirstFreeGroup (1..31; none → stop).
2. CreateSplinterGroup(base group → g, GetPercentage(base group,
   AttackBuildingStrength)).
3. job 5 (AttackBuilding); pos = GetAveragePositionOfGroup(g) (mean of
   unit cells, unsigned division; (0, 0) when empty).
4. side = GetBestSideToAttack(pos) (§4.3), AttackBestBuilding(g, side,
   pos) (§4.4); then again with the group's target side and the new
   average position (two calls, as the code does).

An empty base group gives an empty group 5, freed by the next monitor
pass (strength 0).

### 4.3 Side choice (verified)

`GetBestSideToAttack` (`FUN_0041cdf0`): the countdown +0x18 steps down
first (when non-zero); keep +2 unless the countdown is now 0, or +2 =
0xFF, or +2 is no longer HOSTILE; then RecalculateSideToAttack and +0x18
= 25. (Called once per attack, so 25 attacks, not ticks; corrected
2026-10-16: the step comes before the test, so the recalculation is one
call earlier than "reached 0" read.)

`RecalculateSideToAttack` (`FUN_0041ce50`), sides 0..7 in order:
```
s = 0
if side ≠ me and player active (+0x24250) and alliance[me][side] == 1:
    s = units without flags 0x44 + 3 · buildings without flags 0x4000
    if s ≠ 0:
        d = ftol(sqrt((double)(dx² + dy²)))       // base centres, cells (§4.3a)
        if d > 0: s = ftol(s / (d · 0.0078125f))   // x87
if s < 2: s = 1                                   // every side, even non-hostile
w = ((rand() % 40 + 80) · s) / 100                // u32
if that side's AI is not active: w <<= 2          // humans are 4× more attractive
if w > best: best = w, choice = side              // strict: first wins ties
```
A rand() is drawn for **all 8 sides**. Note non-hostile sides (and the
side itself) get s = 1 and can win when every w is 0 or 1; the next call
then sees a non-HOSTILE +2 and recalculates. (a) Base centre = player
box `+0x262ea..eb` (x), `+0x262e8..e9` (y): `min + (max − min) / 2`
(signed division).

### 4.4 AttackBestBuilding / GetBestBuilding (verified)

`GetBestBuilding(side, x, y, f = 1.0)` (`FUN_0041bbb0`), over the side's
buildings in list order (at most 199 counted):
```
off  = q − rand() % (2q)       q = AttackBuildingRandomPercentage   (one rand per building)
prio = ((u64)(u32)off · 0.01f + 1) · AttackBuildingPriority[record]   // same unsigned quirk; stored as float32
if prio ≠ 0:
    d = ftol(sqrt(dx² + dy²))   dx = x − (b.x px >> 5), dy = y − (b.y px >> 5)   (building +0x30 / +0x34)
    keep (index, prio, d); track min and max d
score = (max_d − d) · f · AI[+0xF64] + prio     // float; best = strictly greater, start 0.0
```
With the quirk, a building whose off < 0 gets prio ≈ 4.3e7 × its
priority, so in practice a random building with a negative roll wins
(the largest (2³² + off) · priority). Priority 0 records (e.g. walls in
1.06: record 17) are never chosen.

`AttackBestBuilding(g, side, x, y)` (`FUN_0041fee0`):
- side = 0xFF → ReturnToBase(g).
- found: target = building, cell = the cell of its pixel position
  (`(x >> 16) >> 5`, `(y >> 16) >> 5`: the row below the footprint),
  substate 2, +0x40 = 0, job 5; ClearUnits, select the group (all) and
  CommenceOperation → **order 0x15 attack building** for the group.
- none: the group becomes **Rampaging** (job 0xE, timer 10000, substate
  4, in use, age 0, target none, strength with job 0xE's weights) and
  CommenceOperation (§6.3) **without** ClearUnits or SelectGroup: the
  selection is what the caller left (corrected 2026-10-16; neither path
  clears the selection afterwards).

### 4.5 Unit attacks

There is no timed "attack units" team in 1.06: +0xF24 (the unit-attack
countdown) is never read. Job 6 (AttackUnit) and job 7 (GuardBuilding)
groups never exist in 1.06 (correction: there is no AttackBuilding
retarget path to them). A scan of every byte write to a group's job
(+0x48) in the AI code `0x41a000..0x424000` finds job 7 only at
`0x4205a3` (DoAttackApproachers, dead, §5.2) and job 6 only at
`0x420a29` inside AttackBestUnit (`FUN_00420970`), whose only caller is
MonitorGroups' job-6 case (`0x41d76a`); `FUN_0041bdb0` (its scoring) is
called only from there (`0x4209b0`). ActivateAI clears every group's job
and in-use byte (the loop after `FUN_0041e920`), so no `.MIS` value
survives either. Unreachable, not ported.

### 4.6 MonitorGroups (`FUN_0041d070(off)`, called with 0 and 0x10)

Each call: `i = (tick + off) & 0x1F`. So every tick handles groups i and
i + 16, each group once per 32 ticks.

1. **i = 0 pass:** count units per group; a group with no units that is
   in use and is not the base or defend-area group → not in use. A
   group larger than its job's maximum (§3.1) splits 50 % of its strength
   into a free group copying its record.
2. **Every call, all 32 groups:** target check (`FUN_004237a0`: a unit
   target with flags 0x400140, a worm, or dying → none); then if the
   target side is not HOSTILE, or the unit / building target is dying →
   target none and **substate 4**.
3. i = 0 → return. BESERK = 1 and i = base group → the base group becomes
   Rampaging at its average position.
4. Strength 0, not base, in use, not the defend group → free; return.
   Else age += 1, strength recomputed, then by job:

| Job | Monitor (verified unless marked) |
|---|---|
| 5 AttackBuilding | age > 10 and DoStopStartStuff(g, 8, 1) and `rand() & 0x3F ≠ 0` → becomes Rampaging at its average cell. Morale fails (§4.7) → ReturnToBase. Substate 7: GroupStable and age > 10 → if `+0xF47 < rand() % 100` new side + AttackBestBuilding, substate 8, then move to an intermediate point about 11 cells short of the target on the group's side (GetEnemyBaseWaypoint `FUN_00420b20`, verified, doc 53 §5.4); else AttackBestBuilding with the same side. Substate 8: stable and age > 10 → AttackBestBuilding. **Substate 4** (target gone): if any unit is "recently in action" (§4.7) → +0x40 = 0; else +0x40 += 1, and on the 3rd quiet pass **AttackBestBuilding again** from the average position (§4.4: no building left → Rampaging). Flag 8 on the group: defend-area hand-off (`FUN_00422230`). |
| 6 AttackUnit / 7 GuardBuilding | morale → return; quiet for 3 passes → AttackBestUnit / next target (7: `FUN_004205f0`, attack-unit order). Unreachable in 1.06 (§4.5) |
| 3 / 4 Protect / Defend | morale fails → ReturnToBase; DoStopStartStuff(g, 8, 1); substate 0: `FUN_0041e220(g, 0, 0)` fails → substate 1; substates other than 0 and 5: `FUN_0041e4e0(g, 0x10)` → Rampaging (substate 4, age 0, timer `FUN_00423820`, target none, cell `FUN_00420a70`). **Unreachable in 1.06**: no code writes job 3 or 4 (the job-write scan of §4.5 finds jobs 1, 2, 5, 6, 7, 0xA, 0xB, 0xD, 0xE, 0xF, 0x10, 0x12 only) and ActivateAI clears every job; DoProtection makes Rampaging groups (doc 29) |
| 10 Returning | DoStopStartStuff after age 10; units **inside the side's base box** leave the group (0xFF) → regrouped by GroupNewUnits |
| 0xE Rampaging | timer 0 → ReturnToBase; substate 4: GetNearestHostileThing (§6.3) → attack unit (0x14) or building (0x15), substate 2, timer −= 1 |
| 0xF DefendArea | §5.4 |
| 0x12 | special-weapon group (doc 22 / not needed) |
| others (1 Base, 2 Guarding, …) | DoStopStartStuff(g, 8, 0) (§5.3) |

### 4.7 Morale, in action (verified)

- `MoraleCheck(g)` (`FUN_0041e420`) passes when `start − now ≤ ((Morale[job]
  + MoraleModifier) · start) / 100` (signed). Failing → **ReturnToBase**.
  A1V2: Morale[AttackBuilding] = 50, modifier 0 → a building attack goes
  home after losing more than half of its strength.
- `ReturnToBase(g)` (`FUN_0041e340`): cell = GetBestBasePosition
  (`FUN_0041e180`: `FUN_0046e3f0`'s base centre; the first result is
  remembered in +0x24 / +0x28 (−1 until then), and a later (0, 0) gives
  the remembered cell; x ≥ W − 1 → W − 1, y likewise), job 10, substate
  1, target none; ClearUnits, SelectGroup(g, 0) (engaged units left out),
  CommenceOperation(g), whose job 10 case takes GetBestBasePosition again
  and calls SendGroupToPoint (ClearUnits, SelectGroup(g, 1), the cell,
  **move order 0x13**). Corrected 2026-10-16 (doc 53 §11).
- `UnitRecentlyInAction(u)` (`FUN_0041e2c0`): not dying and (+0x2A ≤ 11 or
  +0x2B < (rec+0x0E >> 4) · 3).
- `GroupStable(g)`: every unit's +0x2C ≥ 10.

## 5. Defence

### 5.1 Initial state of AI map units

ActivateAI forces GroupNewUnits on the guarding group: every non-harvester,
non-carryall map unit of an AI side gets the **guard order 0x18** (area
guard, state 3, home = its cell; doc 21 §5). Before that, CreateUnit
already gives state 3 to every unit placed with x1 = x2 that does not
belong to the local player (`0x798544`), when the network flag
`0x7984c4` is 0, except kinds 2, 8 and 0xA (harvesters get 0xC):
`FUN_00455870` `0x455c98`..`0x455cc7` (doc 12). So non-AI, non-local
sides' map units are in state 3 too, as our `add_map_unit` does
(`world.cpp`). In skirmish ActivateAI does not run this GroupNewUnits
(doc 53 §1.4), so CreateUnit's state 3 is all they get.

### 5.2 DoAttackApproachers is dead in retail

It needs GuardBuildings (+0xF56). ActivateAI stores 0 there
(`0x41b4db`, EBX = 0) and only the text loader (`0x4518bc`) writes 1, so
with the binary `.MIS` it never runs. (It would send a GuardBuilding
group, AttackApproachersStrength +0xF40 %, at an enemy unit near the
base, `FUN_004205f0`, BaseDefenceRadius.)

### 5.3 DoStopStartStuff (`FUN_0041f670(g, n, rand_retarget)`) — units that are hit

For each unit of group g (list order):
- not engaged (flag 0x100000 clear) and **recently hit** (+0x2A < 12),
  attacker (+0x30 / +0x32) set and not allied and not in state 3:
  - unarmed: move away (`FUN_0049e290` picks a cell, move order) if in
    state 2;
  - armed and the attacker is a valid unit or a building:
    **AttackObject** (`FUN_00420010`): sets the unit's target; if
    SquishInfantry and the unit is a crusher (rec+0x35) and the
    attacker's distance (`FUN_00421770`: dx² + dy² of the two units' cells,
    `(px >> 5) >> 16` with the exe's round-toward-zero shifts, verified) <
    SquishRange → **move onto it** (order 0x13); else attack-unit order
    0x14 (a building attacker: 0x15 and flag 0x100000). Aircraft
    attackers are ignored (attacker cleared).
- engaged: if the target is a unit farther than the longest weapon range
  (`0x782080[w].range >> 5`, squared) by more than VARS `0x6b885c`
  (VARS.BIN +0x44, u32, 2 in 1.06; ours `Vars::chase_give_up`, doc 53
  §7.2, which also gives the exact test) → give up: target cleared, flag
  cleared, CommenceOperation for that unit (back to the group's job).
- idle ≥ 10 (+0x2C > 10 and +0x2B > 10): with `rand_retarget` and
  `rand() % n ≠ index % n`: FindMostAppropriateTarget (`FUN_00420140`:
  the target of a group mate in action) → attack it. Every 8th 32-tick
  slot `(tick >> 5 & 7) == index & 7` with no target: scan ±5 cells for
  cells with a cell object (cell flag 0x1000: crates, spice blooms)
  whose `FUN_0044e9d0(x, y)` type is < 7 or > 11, and move there (the
  last one wins). Correction: not enemy buildings. `FUN_0044e9d0` also
  removes objects of other types as a side effect (doc 31 §6.3, doc 53
  §7.2; the cell test is the byte at cell +1, bit 0x10, in `FUN_0041f670`).
- If any unit was released, CommenceOperation for the group.

### 5.4 Defend areas (DoDefendChecks `FUN_004214a0`, DoDefence `FUN_0041c270`)

ActivateAI (when +0x1D is set, i.e. GuardGroupSize ≠ 0; all 1.06 missions
checked: 1000) adds a defend area = the **side's base box**, period 100
ticks, wanted strength 1000. MonitorGroups job 0xF later sets it to
`ftol(sqrt((f64)(i32)((y1 − y0) · (x1 − x0))) · (u32)GuardGroupSize)`
when the side has buildings, else 0 (correction: the square root of the
area; x87 FILD, FSQRT, FILD qword, FMULP, ftol; a double computation
gives the same value for every 1.06 input, doc 53 §5.3 and §11). Each tick, not BESERK, for
each area whose period divides the tick: no DefendArea group → split
20 % of the wanted strength from the base group into a new group, move it
to the area centre, job 0xF; else top it up by min(wanted − now, 20 %).

DoDefence: for each own building with `index % DefendTicks == tick %
DefendTicks` and hit recently (+0x41 < DefendTicks): flag +8 = 1 and, with
a defend area, **SendNearestUnit** (`FUN_00423360`): the nearest idle
(state 2) unit of the defend group (infantry preferred) moves to the
attacker's cell.

MonitorGroups job 0xF (with +0x19EC set): patrols — units in state 2 / 3
move to random free cells of the area (`rand() & 0x7F == 0`, idle > 200,
or outside the box); otherwise scans the area ± 4 cells for hostile
units / buildings and sends units at them (`FUN_00423440`,
`FUN_00423360`).

### 5.5 DoProtection (`FUN_0041c070`)

For each own unit whose record is flagged ProtectUnit (1.06: the
harvester, record 8) with `index % ProtectTicks == tick % ProtectTicks`,
while Protect groups (job 3) < MaxProtectGroups and the unit was hit
(+0x2A < ProtectTicks), not flagged 8, not in state 0x12: flag 8; with
`rand() % 100 ≤ AirliftPercentage` a carryall lifts it to the map centre
(`FUN_0046cca0` / `FUN_0046cd00`, doc 22); if not lifted (or
AirliftAndAttack) a **Rampaging** group of ProtectStrength % of the base
group goes to the unit's cell.

## 6. Special modes

### 6.1 BESERK (event 4, doc 19 §3.2; +0x1E = 1)

Each tick (AI update tail):
- each own building, not flags 0x4000006, not kind 0xE, with
  `rand() & 0x1F == 0`: **sell** (order 0x23, doc 21 §3) and flag 0x4000000;
- group `tick & 0x1F`: if in use and not Rampaging → Rampaging, timer
  10000, substate 4, target none, cell = its average position.
- GroupNewUnits puts every new idle unit into a free group, which turns
  Rampaging (§3.4).

### 6.2 Auto-BESERK (`FUN_00423850`, verified)

Every 8 ticks (`tick & 7 == side`), only with +0x03 (**EnablePractice**,
doc 22 §1 keyword 58; 1 only in `_PRAC.MIS`, so skirmish only) set and
BESERK off:
```
noRef   = group count of the refinery group (`player+0x245c1[0x6d60df]`, 7 = REFINERY) == 0
noMcv   = primary yard slot (+0x26098) empty (0xFFFF) and no MCV counted (+0x24628[MCV record 0x6d60e7])
noFact  = primary slots 0x2609a, 0x2609c, 0x260a2, 0x260a6, 0x260a8, 0x260aa, 0x260ac all empty
broke   = (u32)(cash + spice) < 400
BESERK = 1 if (noRef and noMcv and broke) or (noFact and noMcv) or (broke and (!player+0x245b8 or noRef))
```
`player+0x245b8` is the side's harvester count (u8; CreateUnit +1, the
free after the death countdown −1, doc 53 §11). Correction (doc 53 §3,
verified): the group tested first is the **refinery** group, not the
yard group.

### 6.3 Rampaging (`GetNearestHostileThing FUN_00420700`)

From the group's cell (+0x50 / +0x51): over sides 0..7 that are
HOSTILE: their buildings (not dying, not kind 0xE, not name group
`0x6d60e1`, which is 0xFF in 1.06 and so excludes nothing) then their
units (not dying, not worms, not flags 0x400144), nearest by the
unsigned squared distance of the **pixel cells** (`(x >> 16) >> 5` for
buildings, whose y is the row below the footprint; `(x / 32) / 65536`
for units), start 0xFFFFFF, strict `<`. Every better object writes the
target id and side at once (the side is the loop's side, not stale).
**It succeeds when some unit was the best during the scan** (a later,
nearer building keeps the success); then the group cell becomes the
final best object's cell. Nothing found leaves the target as it was
(corrected 2026-10-16: it was "success only when the nearest is a unit",
the cell was the +0x4A cell and the cell was not written).

Callers: CommenceOperation (target none: group position → +0x50, the
search, then by the target slot's type: unit → order 0x14, building →
0x15, else target none; the result is not tested), and MonitorGroups
job 0xE substate 4 (`0x41d7e4`): on success ClearUnits, SelectGroup(g,
0), timer − 1 if > 0, then unit → substate 2 and order 0x14, building →
substate 2 and order 0x15, else target none; the selection is left.

### 6.4 Not covered

Carryall ferrying / airlift (doc 22), DoSpecialWeapons (Death Hand,
air strikes: superweapons, `FUN_00422270`), DoRepairUnits, saboteurs /
engineers, Starport buying.

## 7. A1V2 (and A1V1): what is needed

`_A1V2.MIS`: side 1 (Harkonnen) is the only AI (active), tech 1, 2500
credits. Values: FirstAttackBuildingDelay 4000, TimeBetweenBuildingAttacks
5000, AttackBuildingStrength 100, TimingRandom 25, GroupSizeRandom 20,
AttackBuildingRandom 50, Morale[5] = 50 (others 100), MoraleModifier 0,
weights 1.0, GuardGroupSize 1000 (defend area on), SquishInfantry 1,
SquishRange 9, ProtectUnit {8}, MaxProtectGroups 2, ProtectTicks 4,
DefendTicks 25, FreePercentage 0, AttackSidePriority all 0,
AttackBuildingPriority: record 0 = 100, 10 = 50, 13 = 10, 20 = 14,
26 = 18, 32 = 6, 35 = 16, 38 = 8, 17 = 0, others 1. **A1V1 has the same
AI block** (side 1 active, same timings).

Map: Harkonnen have **no buildings**, 4 light infantry at (0,0), (19,0),
(0,4), (26,18) and a trike at (27,6). Script (relevant): at tick 3500
(TIMER =) DEPLOY 2 light infantry for side 1 by carryall at (18, 1)
(action ATTACK, not applied, doc 19 §3.1); the player gets carryall
reinforcements at 3200 and a free harvester. Win: 2500 harvested; lose:
no base and no units.

So the minimum for A1V2:
1. ActivateAI for active AI sides: guarding group 1 with every map unit
   (guard order), base group 2, first-attack countdown, defend area.
2. GroupNewUnits every 32 ticks: the dropped infantry join the **base
   group** with a guard order.
3. DoAttackBuildings: countdown (with the quirk), splinter 100 % (±20 %)
   of the base group, side choice, GetBestBuilding, attack-building order.
   With A1V2's base group (the 2 dropped infantry) this is the attack the
   player sees after ~3000–5000 ticks.
4. MonitorGroups for jobs 5, 10, 0xE, 0xF and the default (DoStopStartStuff
   for the guarding group: units that are shot fire back, crushers
   squish).
5. DoDefendChecks / DoDefence: with no buildings the base box
   (`FUN_0046be50`, doc 53 §1.6) is the bounding box of the side's
   units (aircraft too): in A1V2 the box of the 4 infantry and the
   trike. The defend area still pulls 20 % per 100 ticks from the base
   group.
6. rand() order as §1.1 (DoBuildUnits etc. of doc 22 also draw).

## 8. rand() use (order matters, verified call sites)

| Where | Draws |
|---|---|
| ActivateAI | `0xe2` (doc 22 field +0xF68), `0xea` first-attack countdown, `0xee` (+0xF24) |
| GroupNewUnits | kind 7 deploy `& 1` (per unit) |
| DoAttackBuildings | `0x5fc` timer; then CreateSplinterGroup `0x645`; RecalculateSideToAttack `0x75c` × 8 (only when recalculating); GetBestBuilding `0x2d7` × buildings |
| DoProtection | `0x40b` airlift |
| MonitorGroups | job 5 `0x8a3` (age > 10, after DoStopStartStuff), `0x8c6`; job 0xF `0xa4b`, `0xa57`, `0xa60`, `0xa61` |
| DoStopStartStuff | `0x103d` per idle unit |
| BESERK | `0x1c0` per building |

## 9. Implementation checklist (our code)

1. `sim/ai.cpp` (new), `World::tick_ai(side)` called each tick in the
   script's place in the tick (doc 19 §2.1: after the mission timer,
   before `tick_script`); AI block struct with the §2 fields, loaded from
   the `.MIS` (extend `formats::MissionFile` with the 0x1DB8 block or the
   fields listed).
2. Units: add `ai_group` (+0x28), `since_hit` (+0x2A, ticks/8; reset when
   hit), `since_stop` (+0x2C), flag 0x100000; keep `since_shot` (+0x2B).
   Verify the counters' update rules in `FUN_00497b90` before using them.
3. Orders: reuse `order_move` / `order_attack_unit` /
   `order_attack_building` / `order_guard` by setting `Unit::selected` on
   the AI's units (as the exe), then clearing it. Orders go through the
   order queue and run 2 ticks after they are posted (doc 49; ours
   `post_order` since 2026-10-13).
4. Exact arithmetic helpers: `ai_percent_quirk(p, value)` = the §4.1
   formula with the exe's x87 rounding (53-bit precision control, exact
   loads; `src/sim/x87.hpp`), used by
   the timer, CreateSplinterGroup and GetBestBuilding; `ftol` truncation.
5. ActivateAI (§3.1, doc 22 for the rest), GroupNewUnits (§3.4),
   DoAttackBuildings + GetBestSideToAttack + GetBestBuilding (§4),
   MonitorGroups (§4.6: jobs 5, 10, 0xE, 0xF, default), MoraleCheck,
   ReturnToBase, DoStopStartStuff (§5.3), DoDefendChecks / DoDefence
   (§5.4), DoProtection (§5.5), BESERK (§6.1), auto-BESERK (§6.2).
6. Combat (`src/sim/combat.cpp`): AI units use the existing states; the
   initial state 3 shortcut in `world.cpp` should become the activation
   guard order (§5.1).
7. Tests: A1V2 headless run — the 2 dropped infantry form a group and
   issue an attack-building order between ticks ~3500 and ~5000; a
   seeded check of the §4.1 table (p 25, interval 5000: 6249 / 5749 /
   5049 / 5000 / 150 / −50 / −550 / −1000).

## 10. Delivered units (added 2026-10-06, verified)

When an AI side's carryall (or frigate) finishes its drops (`FUN_0049e3e0`
returns 0, unit update `0x4989xx`): `g = FindGroupForDelivery(entry)`
(`FUN_00420480`: the in-use Deploying group, job 0xD, whose substate is the
entry, else the first free group, set up as Deploying, strength 100);
group flags |= 1 ("start") and group +0x53 (mode) = the delivery entry's
+0x36 = the script's **deploy action** (GUARD 0, ATTACK 1, RETREAT 2).

Each dropped unit has flags 0x40000 (0xC0000 for the last record) and
+0x2E = the entry. GroupNewUnits (once the unit is no longer busy: state
8 / 0x14 / 9 or mid-step) clears 0x40000 and puts it in that group (a
frigate's units go to the base group), group flag 4; no guard order.
When a GroupNewUnits pass finds no busy unit, the first group 1..31 with
flag 1 is started by its mode: 0 guard (`FUN_0041e490`), 1 **rampage**
(cell = the last unit of the side's list, job 0xE, substate 4, timer
`FUN_00423820`), 2 ReturnToBase; then SelectedGroup and CommenceOperation.

So in A1V1 / A1V2 the 2 infantry dropped at tick 3500 (action ATTACK)
attack the nearest enemy as soon as they have stopped (our test: tick
3712), as in the reference game. GroupNewUnits recomputes the strength of
the group it was called for (not always the base group).
