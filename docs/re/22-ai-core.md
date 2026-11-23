# Computer player AI: core (1.06)

The AI of the computer sides: its data block, when it runs, and its
economy (unit production, harvesters, rebuilding, repairs, selling,
upgrades, Starport). Groups, attacks, defence, targeting and the
script's effect on them are in **doc 23** (`23-ai-teams.md`); this doc
only names those functions.

Related: doc 19 (`.MIS` layout, events that write AI fields), doc 11
(player struct, production, power), doc 12 (placement, spawn), doc 13
(harvesting), doc 21 (sell, repair).

"Verified" means read from the code or the data. Every point that was
open has been settled from the exe (2026-10-16); none remain. Function
names in quotes are the exe's own profiler labels
(`FUN_00423830("…AI()")` at the start of each function).

## 1. The AI block

### 1.1 Where it lives

- **Runtime:** 8 blocks of **0x1DB8** bytes at `0x4f4118`, one per player
  slot (`0x4f4118 + slot × 0x1DB8`). Byte +0 = the slot's side, +1 = AI
  active.
- **File:** the binary `.MIS` (`FUN_0044f8e0`, retail path) reads
  **0xEDC0 bytes straight into `0x4f4118`** (file +0x58, after tech,
  credits and houses). So the file holds the whole runtime struct with its
  initial values, not just the 69 settings.
- `FUN_0041af50` ("InitAI") clears a block and writes the defaults
  (§1.3). It runs for all 8 slots at mission set-up, before the `.MIS` is
  read.
- `FUN_004501a0` is a **text** loader for `[AI]` sections (`{SIDE}`
  opens a block and sets +1 = 1). It is a development path: the retail
  game reads only the binary file. Its `switch` gives the offset and type
  of each setting (§1.2). Seven names have no case: their offsets come
  from the defaults and the code that reads them (marked).

### 1.2 The 69 settings (keyword table `0x4e1e90`, 0x20 per name)

Index tables used by the array fields: **group purposes** `0x4e1b10`
(19: NoPurpose, Base, Guarding, Protect, Defend, AttackBuilding,
AttackUnit, GuardBuilding, Harvesters, Carryalls, Returning, Repair,
TempGuard, Deploying, Rampaging, DefendArea, 3 unused), **jobs**
`0x4e1d70` (9: NoJob, RepairBuildings, RepairUnits, BuildHarvesters,
BuildCarryalls, BuildBuildings, BuildUnits, 2 unused), **sides**
`0x4e1630` (8), **units** (60 records), **buildings** (100 records),
**building groups** (`0x787e08`, 0x32 per name, count `0x6e5850` = 24;
the record's +0x88 name index).

| # | Name | Offset | Type | Default | Use (this doc / doc 23) |
|---:|---|---|---|---|---|
| 0 | FirstAttackBuildingDelay | +0xF10 | u32 | 6000 | first building-attack timer (§2.2) |
| 1 | TimeBetweenBuildingAttacks | +0xF14 | u32 | 5000 | doc 23; event 7 writes it |
| 2 | FirstAttackUnitDelay | +0xF1C (no text case) | u32 | 6000 | first unit-attack timer (§2.2) |
| 3 | TimeBetweenUnitAttacks | +0xF20 (no text case) | u32 | 5000 | **dead** (§1.5) |
| 4 | AttackUnitStrength | +0xF3C (no text case) | u8 | 15 | **dead** (§1.5) |
| 5 | AttackBuildingStrength | +0xF3D | u8 | 15 | doc 23 |
| 6 | ProtectStrength | +0xF3E | u8 | 15 | doc 23 |
| 7 | DefendStrength | +0xF3F | u8 | 15 | doc 23 (+0xF40 = 15 too, unnamed) |
| 8 | SpeedWeight | +0xF70 | f32[19 purposes] | 1.0 | doc 23 |
| 9 | HPWeight | +0xFBC | f32[19] | 1.0 | doc 23 |
| 10 | FirePowerWeight | +0x1008 | f32[19] | 1.0 | doc 23 |
| 11 | CashStash | +0x1054 | u32[9 jobs] | 1800 | money kept back per job (§3) |
| 12 | Morale | +0x1078 | u8[19] | 100 | doc 23 |
| 13 | AttackSidePriority | +0x19ED | u8[8 sides] | 0 | doc 23 |
| 14 | UnitBuildPriority | +0x19F8 | f32[60 units] | 1.0 | unit mix (§3.2) |
| 15 | UnitRepairValue | +0x1AE8 | f32[60] | 0.5 | unit repair threshold (§3.6) |
| 16 | ProtectUnit | +0x1CC8 | u8[60] | 0 | doc 23 |
| 17 | BuildingRepairValue | +0x13AC | f32[100 buildings] | 0.66 | building repair threshold (§3.5) |
| 18 | DefendBuildingPriority | +0x108C | f32[100] | 1.0 | doc 23 |
| 19 | AttackBuildingPriority | +0x121C | f32[100] | 1.0 | doc 23 |
| 20 | MinBaseStrength | +0xF48 | u32 | 0 | doc 23 |
| 21 | MaxBaseStrength | +0xF4C | u32 | 0 | doc 23 |
| 22 | BaseDefenceRadius | +0xF50 | u32 | 64 | doc 23 |
| 23 | HarvestersPerRefinery | +0x1D04 | u8 | 2 | §3.2 |
| 24 | CarryallsPerRefinery | +0x1D05 | u8 | 1 | §3.3 |
| 25 | RecogniseThreat | +0x1D06 | flag | 0 | doc 23 |
| 26 | MoraleModifier | +0x1D1C | u8 | 0 | doc 23 |
| 27 | MaxMorale | no text case, no field (§1.5) | | | |
| 28 | MinMorale | +0x1D1E | u8 | 25 | doc 23 |
| 29 | AirliftPercentage | +0x1D1F | u8 | 25 | doc 23 |
| 30 | UseIdealStrength | +0x1D20 (no text case) | u8 | 0 | GetPercentage `FUN_0041e5d0` (§1.5) |
| 31 | UnitBuildRate | +0x1D2C | u32 | 1000 | ticks between unit orders (§3.2); event 6 writes it |
| 32 | RepairUnits | +0x1D14 | flag | 0 | §3.6 |
| 33 | RepairGuardingUnits | +0x1D15 | flag | 0 | §3.6 |
| 34 | BuildingRepairTicks | +0x1D0C | u32 | 8 | §3.5 |
| 35 | UnitRepairTicks | +0x1D10 | u32 | 32 | §3.6 |
| 36 | ProtectTicks | +0x1D34 | u32 | 4 | doc 23 |
| 37 | DefendTicks | +0x1D38 | u32 | 25 | doc 23 |
| 38 | AirliftAndAttack | +0x1D44 | flag | 0 | doc 23 |
| 39 | BuildBuildingRate | no text case, no field (§1.5; +0x1D28 is the unit-order counter) | | | |
| 40 | RebuildBuildings | +0x1D45 | flag | 0 | §3.4 |
| 41 | TimeBeforeRebuilding | +0x153C | u32[100] | 850 | §3.4 |
| 42 | AttackBuildingRandomPercentage | +0xF6C | u8 | 50 | doc 23 |
| 43 | BuildUnitRandomPercentage | +0xF6D | u8 | 50 | **dead** (§1.5; §3.2 uses +0x0A) |
| 44 | TimingRandomPercentage | +0xF6E | u8 | 25 | §2.2 |
| 45 | GroupSizeRandomPercentage | +0xF6F | u8 | 20 | doc 23 |
| 46 | GuardBuildings | +0xF56 | flag | 0 | doc 23 (DoAttackApproachers) |
| 47 | SquishInfantry | +0x1C | flag | 0 | doc 23 |
| 48 | SquishRange | +0x14 | i32 = r × r | 9 | doc 23 |
| 49 | MaxAttackUnitGroups | +0xF41 | u8 | 2 | doc 23 |
| 50 | MaxAttackBuildingGroups | +0xF42 | u8 | 2 | doc 23 |
| 51 | MaxProtectGroups | +0xF43 | u8 | 2 | doc 23 |
| 52 | MaxDefendGroups | +0xF44 | u8 | 2 | doc 23 |
| 53 | MaxGuardGroups | +0xF45 | u8 | 2 | doc 23 |
| 54 | BuildRatio | +0x16CC | f32[24 groups] | 1.0 | §3.4 |
| 55 | MaxBuildings | +0x185C | u32[24 groups] | 1 | §3.4 (recomputed at activation) |
| 56 | DefendArea | +0x1D54 + n × 0x14, count +0x1D51 (max 5) | 4 × u8, u32, … | | doc 23 |
| 57 | DefendAreaTimeout | no text case | | | |
| 58 | EnablePractice | +0x03 | flag | 0 | skirmish behaviour (§3.4, §3.8) |
| 59 | BuyUnits | +0x04 | flag | 0 | Starport (§3.7) |
| 60 | UpgradeBuildings | +0x05 | flag | 1 | §3.7 |
| 61 | DefaultBuildingRepairValue | fills +0x13AC[100] | f32 | | |
| 62 | DefaultUnitRepairValue | fills +0x1AE8[60] | f32 | | |
| 63 | IR | writes the house byte `0x8cd4f0[side]` | side name | | |
| 64 | GuardGroupSize | +0xF5C | i32 | 1000 | doc 23; 0 clears +0x1D |
| 65 | DontSell | +0x07 | flag | 0 | §3.8 |
| 66 | FreePercentage | +0xF0D | u8 | 0 | doc 23 (activation splits the base group) |
| 67 | EmergencyBuildRate | +0xF28 | u32 | 1 | §3.2 |
| 68 | SpecialWeaponDelay | +0x1D18 | u32 | 0 | doc 23 |

### 1.3 Runtime fields (not settings)

| Offset | Meaning |
|---|---|
| +0x00 / +0x01 | side / AI active |
| +0x02 | 0xFF (TODO) |
| +0x06 | "broke": cash + spice = 0 (DoOverrides) |
| +0x08 / +0x09 | storage full (spice = capacity) / "may spend on buildings" (DoOverrides, RebuildBuildings) |
| +0x0A | 20: the random range of the unit choice (§3.2) |
| +0x0C | u32 production-wait counter (RebuildBuildings) |
| +0x10 | u32 unit-repair tick counter (§3.6) |
| +0x18 | u32 1 (TODO) |
| +0x1D | 1 (cleared when GuardGroupSize = 0) |
| +0x1E | **BESERK** (event 4 sets 1; practice mode sets 1, §3.8) |
| +0x1F | "concrete laid, start the building next" (§3.4) |
| +0x24 / +0x28 | −1 (TODO) |
| +0x2C + g × 0x2C | **32 groups** of 0x2C bytes (doc 23). +0x1C purpose, +0x1D state, +0x26 active, +0x24/+0x25 cell |
| +0x5AC + i × 0x10 | **initial buildings** (§3.4): +0 x, +1 y (top-left cell), +2 u16 building index, +4 u32, +8 u32 timer, +0xC record, +0xD alive |
| +0xF0C | count of initial buildings (max 149) |
| +0xF18 / +0xF24 | building / unit attack countdowns (§2.2) |
| +0xF2C..+0xF38 | u32 rect of the building being rebuilt (x0, y0, x1, y1) |
| +0xF54 | "harvesters wanted" (§3.2) |
| +0xF57 / +0xF58 | retreat cell (`FUN_0046e3f0`, doc 21) |
| +0xF60 / +0xF64 | f32 1.0 (TODO) |
| +0xF68 | f32 (60 + rand % 30) × 0.01 (doc 23) |
| +0x19EC | 1; 0 in practice mode (TODO) |
| +0x1D07 / +0x1D3D | 0xFF; +0x1D3C = the Base group (job 1), +0x1D3D = the Guarding group (job 2) (doc 23; corrected per doc 29, `0x41b427` / `0x41b49f`) |
| +0x1D08 | 5: max units sent to repair at once (§3.6) |
| +0x1D1D | emergency flag (set by MonitorGroups, doc 23) |
| +0x1D22 / +0x1D23 | 0xFF (TODO) |
| +0x1D28 | u32 unit-order counter (§3.2) |
| +0x1D40 | u32 unit picked by the human with the mouse (GetSelected) |
| +0x1D46 | building group of the house's repair pad (kind 6, `FUN_004a6070`) |
| +0x1D47 / +0x1D48 | cell of the building being rebuilt; +0x1D4A its record |
| +0x1D49 | rebuild in progress; +0x1D4C u32 its placement counter |
| +0x1D50 | concrete quarters still to lay: 1 TL, 2 TR, 4 BL, 8 BR |
| +0x1D52 | 0xFF or the defend-area index added at activation |

### 1.4 The 1.06 A1V1 and A1V2 values

Decoded from the files (`aidump.py`-style, file +0x58):

- Only **slot 1** (Harkonnen) is active in both. The two AI blocks are
  **byte-identical**. A1V1 gives slot 1 tech 1 and **1 credit**; A1V2
  gives it tech 1 and **2500 credits**. Slot 2 has 1 credit in both
  (inactive, doc 19).
- Differences from the InitAI defaults: FirstAttackBuildingDelay 4000;
  AttackBuildingStrength 100; Morale[AttackBuilding] 50; MinMorale 25,
  AirliftPercentage 50; RepairUnits 1, RepairGuardingUnits 1;
  RebuildBuildings 1; GuardBuildings 1; SquishInfantry 1 (range 3);
  ProtectUnit[HARVESTER] 1; UnitBuildPriority 0 for ENGINEER, THUMPER,
  HARVESTER, MCV, CARRYALL, ORNITHOPTER, FREMEN, SABOTEUR, DEATH HAND;
  AttackBuildingPriority for the Atreides buildings: CONSTRUCTION YARD
  100, WIND TRAP 50, HEAVY FACTORY 30, PALACE 20, OUTPOST 18, LIGHT
  FACTORY 16, STAR PORT 15, REFINERY 14, BARRACKS 10, SILO 8, RESEARCH
  CENTRE 7, HIGH TECH 6, REPAIR PAD 4, WALL 0.
- Everything else is at its default: UnitBuildRate 1000,
  TimingRandomPercentage 25, CashStash 1800 for every job, BuildRatio
  1.0, MaxBuildings 1, HarvestersPerRefinery 2, CarryallsPerRefinery 1,
  UpgradeBuildings 1, BuyUnits 0, EnablePractice 0, DontSell 0, no
  defend areas.

### 1.5 Settings without a text case (verified 2026-10-16)

The text loader's `switch` (`FUN_004501a0`) has no case for indices 2,
3, 4, 27, 30, 39 and 57: they fall into "Command not implemented yet".
The retail game reads the binary block, so only the readers matter. An
operand scan of the whole exe (every instruction with the displacement,
and the slot-0 absolute addresses `0x4f4118 + off`) gives:

- **+0xF1C / +0xF20 / +0xF3C** (FirstAttackUnitDelay,
  TimeBetweenUnitAttacks, AttackUnitStrength): written by InitAI
  (`0x41b094`, `0x41b09a`, `0x41b14a`). +0xF1C is read once, at
  `0x41b38d` in ActivateAI, to fill +0xF24, and +0xF24 is written there
  (`0x41b3b3`) and read nowhere. +0xF20 and +0xF3C have no reader. The
  names follow from the layout (the building-attack trio +0xF10 / +0xF14
  / +0xF18 and its defaults 6000 / 5000 repeat at +0xF1C / +0xF20 /
  +0xF24, and +0xF3C sits before AttackBuildingStrength +0xF3D with the
  same default 15). They have no effect in 1.06 (doc 23 §4.5).
- **MaxMorale**: no field. Morale reads only MoraleModifier +0x1D1C and
  MinMorale +0x1D1E; the byte between them, +0x1D1D, is the emergency
  flag (cleared at `0x41b236`, set / cleared by MonitorGroups at
  `0x41dd1c` / `0x41dd25`, read by ShouldIBuildNow `0x41beaf` and
  ShouldIBuildUnit `0x4203b1`).
- **UseIdealStrength** = **+0x1D20** (u8, 0 after InitAI's clear): its
  only reader is GetPercentage (`FUN_0041e5d0`, `0x41e5df`): set, the
  share is taken of MaxBaseStrength instead of the group strength (doc
  53 §2). The name is assigned by that use (the only unnamed setting
  byte with a reader); the value is 0 in every slot of all 46 `.MIS`
  files, so the branch never runs in 1.06.
- **BuildBuildingRate**: no field. +0x1D28 is the unit-order counter
  (`0x41beca`..`0x41bedd`), and DoBuildBuildings is empty (§2.2).
- **BuildUnitRandomPercentage** +0xF6D: written by InitAI (`0x41b1a8`)
  and the text case 0x2B (`0x4517b3`), never read. The unit choice uses
  the runtime byte **+0x0A** (§3.2), which is 20 (0x14) in InitAI and in
  every active slot of the 45 `.MIS` files and `_PRAC.MIS`, so its
  `rand() % +0x0A` never divides by zero in 1.06.

## 2. When the AI runs

### 2.1 Activation (`FUN_0041b250` "ActivateAI")

Once per active slot, in the mission set-up of `FUN_00448550`, after the
players and deliveries are reset. In order (rand = the shared MSVC
`rand()`, `0x448460` / `0x4b1810`):

1. Unless skirmish (`gGameType` ≠ 1): error "AI not initialised
   correctly" when the slot's tech or credits is 0.
2. `r1 = rand()`; +0xF68 = f32(r1 % 30 + 60) × 0.01f.
3. +0x1D46 = the repair-pad building group of the house.
4. `r2 = rand()`; t = TimingRandomPercentage; **+0xF18 =
   ftol(f32(t − r2 % (2t) + 100) × 0.01f × FirstAttackBuildingDelay)**
   (x87: `FILD int`, `FMUL f32 0.01`, `FIMUL int`). t = 0 would divide by
   zero; every 1.06 mission uses 25.
5. `r3 = rand()`; **+0xF24 = ftol(FirstAttackUnitDelay (as i64) ×
   (f32(t − r3 % (2t) + 100) × 0.01f))**.
6. Hard difficulty (`0x4e8bf0` = 2): **UnitBuildRate = ftol(UnitBuildRate
   × 0.75)** (double constant `0x4be3c0`).
7. `FUN_0041e920` "GetInitialBuildingPositions" (§3.4).
8. Groups: all 32 cleared; the first free group becomes the Base group
   (+0x1D3C, job 1), the other the Guarding group (+0x1D3D, job 2)
   (corrected per doc 29; the code was already right); GroupNewUnits (doc 23) runs once at once; retreat cell; the
   defend-area set-up; FreePercentage splitter. Details: doc 23.

### 2.2 The update (`FUN_0041b610` "DoAI")

Called for every active slot (0..7, in order) **every simulation tick**,
inside the tick block of `FUN_00448550`: after the tick counter
(`0x5173f4`) and the mission timer step, **before the mission script**
(`FUN_00453b90`) and the defeat checks (`FUN_00455480`), and so before
the unit / building updates. Many sub-functions stagger themselves by
side and tick (below). Our `World::tick()` order: AI after the timer and
before `tick_script()`.

Sub-functions, in call order (doc 23 = teams):

| Function | Label | Role |
|---|---|---|
| `FUN_0041cc20` | GetSelected | the AI unit the human has selected, if any (kept aside, +0x1D40) |
| `FUN_00422140` | DoMCVs | every 64 ticks: deploy an idle MCV (§3.8) |
| `FUN_0041c3e0(+0x1D3C, 0)` | GroupNewUnits | doc 23: put new units in groups (every 32 ticks by side) |
| `FUN_0041bf00` | DoRepairs | flag damaged buildings for repair (§3.5) |
| `FUN_00421fa0` | MonitorHarvesters | every 32 ticks: harvester and crusher reactions (§3.3) |
| `FUN_0041bfe0` | DoBuildUnits | unit production (§3.2) |
| `FUN_0041c060` | DoBuildBuildings | **empty in 1.06** |
| `FUN_00422c20` | DoOverrides | money / power emergencies, selling (§3.8) |
| `FUN_00420500` | DoAttackApproachers | if GuardBuildings: doc 23 |
| `FUN_0041e650` | DoRepairUnits | if RepairUnits and the side owns a repair pad (§3.6) |
| `FUN_0041c920` | DoAttackBuildings | doc 23 |
| `FUN_0041c070` | DoProtection | doc 23 |
| `FUN_00422270` | DoSpecialWeapons | doc 23 |
| `FUN_0041c270` | DoDefence | doc 23 |
| `FUN_0041d070(0)`, `(0x10)` | MonitorGroups | doc 23 (groups 0..15, 16..31) |
| `FUN_0041eab0` | MonitorBuildings | dead initial buildings, queued sells (§3.4) |
| `FUN_004214a0` | DoDefendChecks | doc 23 |
| `FUN_004203d0` | DoBuildCarryalls | §3.3 |
| `FUN_0041ebc0` | RebuildBuildings | if RebuildBuildings (§3.4) |
| `FUN_00421c40` | UpgradeBuildings | if UpgradeBuildings (§3.7) |
| `FUN_00421cf0` | BuyUnitsFromStarport | if BuyUnits (§3.7) |
| `FUN_00423850` | (no label) | practice mode: decide to go BESERK (§3.8) |
| BESERK block | | if +0x1E is 1 or 2 (§3.9) |
| `FUN_0041cdb0` | ClearUnits | clear the selection flag of all own units |
| `FUN_0041cc90` | ResetSelected | give the human's selection back |

**How the AI acts:** it uses the same orders as the human. It sets the
unit "selected" byte (+0x19) on its own units, posts an order (move
`FUN_00459450`, build `FUN_0045a0b0`, place `FUN_00459fd0`, …) for the
side, and clears the selection again. Orders go through the normal order
queue (`FUN_0045bf00`) and run on the next order pass (doc 09). GetSelected
/ ResetSelected protect a unit the human clicked on (single player:
selecting an enemy unit is allowed, doc 10).

## 3. Economy

### 3.1 Money

The AI gets **no free money** in any of these functions: no write to
cash (+0x2425C) or spice (+0x24254) was found in the AI code. It earns
like the human (harvesters, the mission's starting credits, deliveries)
and spends through the normal production (pay as the progress advances,
doc 11). Every decision compares **cash + spice** with a **CashStash**
amount, the money the AI keeps back for a job:

| Job | CashStash field | Used by |
|---|---|---|
| BuildCarryalls (4) | +0x1064 | DoBuildCarryalls: build only if cash + spice ≥ it |
| BuildBuildings (5) | +0x1068 | RebuildBuildings: +0x09 = 0 when cash + spice > it; GetBestBuildingToBuild needs cash + spice > it or +0x09 |
| BuildUnits (6) | +0x106C | ShouldIBuildUnit: needs cash + spice > it + the unit's cost, unless storage full (+0x08) or emergency (+0x1D1D) |
| index 8 | +0x1074 | BuyUnitsFromStarport: keep this much |

Prices are the normal `FUN_00442bb0` / `FUN_00442be0` costs. The
difficulty cost factor (`FUN_004429b0`: 75 / 100 / 125 %) applies to
the **local player only**; every other side pays 100 % (doc 21 §2.2).

### 3.2 Units: DoBuildUnits (`FUN_0041bfe0`)

Every tick:

1. **ShouldIBuildNow** (`FUN_0041bea0`): rate = EmergencyBuildRate
   (+0xF28) if the emergency flag +0x1D1D is set, else UnitBuildRate
   (+0x1D2C). `+0x1D28 += 1`; if `+0x1D28 >= rate` (unsigned): reset it to
   0 and go on; else stop. So one unit order every UnitBuildRate ticks
   (1000 in the 1.06 missions: 50 s at 20 ticks/s; 750 on hard).
2. **FindBestUnitToBuild** (`FUN_0041b910`) returns a record or −1:
   - **Harvesters first:** +0xF54 = (harvester count +0x245B8 <
     refineries owned (`+0x245C1[group 0x6d60df]`) × HarvestersPerRefinery).
     If set, ShouldIBuildHarvester (always true) and the harvester record
     (`0x6d60e8`) is affordable (CanBuildUnit) → the harvester.
   - Practice mode (+0x03), no primary construction yard (`+0x26098` =
     −1), no MCV owned (`+0x24628[0x6d60e7]` = 0) and `rand() & 7 == 1` →
     the MCV.
   - Otherwise a **ratio pick** over the 60 records. A record is a
     candidate when it is in the side's unit build list (InIconList
     `FUN_00422e60`, `+0x261B4[+0x261AC]`). Sums over the candidates: P =
     Σ UnitBuildPriority, N = Σ owned count (`+0x24628[rec]`, u32). For
     each candidate in record order:
     ```
     if UnitBuildPriority[r] / P == 0: score = 100000
     else: score = ((f32)(count[r] / N) / (UnitBuildPriority[r] / P))
                   × f32(rand() % +0x0A + 100) × 0.01        // +0x0A = 20
     keep the smallest score (strict <); start 100000
     ```
     So the AI builds the unit type furthest below its priority share,
     with a ±(0..19 %) random factor. **One `rand()` per candidate with a
     non-zero priority**, in record order (determinism).

     The x87 code (`0x41ba8e`..`0x41bb33`, verified; constants `0x4be3bc`
     = 0.01f, `0x4be3c8` = 0.0f, `0x4be3cc` = 100000.0f):
     - P is summed with `FLD f32; FADD f32; FSTP f32` per candidate, so
       it is an f32 sum in record order. N is a u32 sum.
     - share = `FLD UnitBuildPriority[r]; FDIV P`; it is stored once as
       f32 (`FST`), but the **zero test** (`FCOMP 0.0f`, `TEST AH,0x40`)
       is on the unrounded quotient, and C3 is also set for an
       **unordered** result: with P = 0 (every candidate's priority 0)
       the share is 0 / 0 = NaN and takes the 100000 branch, **without
       a `rand()`**.
     - own = `FILD qword count; FILD qword N; FDIVP`, stored as f32. For
       counts below 2²⁴ the quotient rounds to the same f32 from 53 or
       64 bits, so a double divide gives the exe's value.
     - score = `FLD own; FDIV share(f32); FIMUL (rand % +0x0A + 100);
       FMUL 0.01f`, **not rounded**, then `FCOM best(f32)`; `TEST AH,1`
       (C0) takes it when it is less **or unordered**, and `FSTP` stores
       it as the new f32 best. The CRT start-up sets 53-bit precision
       (`__fpmath` → `__setdefaultprecision` `0x4b5af0`:
       `_controlfp(0x10000 _PC_53, 0x30000 _MCW_PC)`; the other `FLDCW`s
       are ftol's rounding switch and restore at `0x4b1856` / `0x4b185c`
       and `_controlfp` itself at `0x4b8c71`), so these steps are
       exactly double operations (ours: `World::ai_best_unit`).
     - **N = 0** (the side owns none of the candidate types, e.g. a new
       barracks and no infantry): own = 0 / 0 = NaN, the score is NaN
       and is taken; the best is then NaN, and every later candidate
       (its score compares unordered, 100000 included) is taken too. So
       with N = 0 the pick is the **last candidate in record order**,
       provided one candidate has a non-zero share; the `rand()` calls
       are drawn as usual.
   - The pick must pass ShouldIBuildUnit (`FUN_00420350`, §3.1) and, when
     harvesters are wanted, CanBuildUnit.
3. **CanBuildUnit** (`FUN_0041b850`): `FUN_004430d0(side, rec, 1)` (not
   blocked by another queued item), cash + spice ≥ the price, and the
   record is in the unit build list.
4. **Factory:** `FUN_00442c20(rec, side, …, check 1)` (doc 12 §8). If a
   factory is found: **order 0x27 = start unit** (`FUN_0045a0b0` →
   `FUN_00455510`), the same order as a sidebar click (doc 11): a
   production slot, paid as it progresses, spawned at the factory when
   ready (doc 12). The AI does not hold or cancel.

### 3.3 Harvesters and carryalls

**DoBuildCarryalls** (`FUN_004203d0`), every tick: if cash + spice ≥
CashStash[BuildCarryalls] and carryalls owned (+0x245C0) < refineries ×
CarryallsPerRefinery, and the house's carryall (`FUN_0046c570(0x6d60d8,
0)`) passes CanBuildUnit and ShouldIBuildCarryall (`FUN_00420320`), order
0x27 for it. ShouldIBuildCarryall (`FUN_00420320`, verified) returns
the side's harvester counter byte `P+0x245B8` ≠ 0: a carryall is only
built while the side owns a harvester (dying ones count, doc 53 §11).

**MonitorHarvesters** (`FUN_00421fa0`), when `(tick + side) & 0x1F == 0`,
for each own unit:
- an **unarmed crusher** (rec+0x0C = −1, rec+0x35 set) hit less than 10
  ×8 ticks ago (+0x2A < 10) by a unit that is **infantry**, and that takes
  orders and is not dying: select it and order a **move to the
  attacker's cell** (run it over). This is the harvester squash reaction.
- a **harvester** (kind 1) not carried (flag 0x100): idle (state 2) →
  SetState(0xB) (harvest); hit recently (+0x2A < 10) with hp < max / 4
  and not already returning → SetState(0xD) (back to the refinery).

### 3.4 Buildings: rebuild only

**There is no base-building plan in 1.06: `DoBuildBuildings` is an
empty stub.** The AI only **rebuilds** buildings it had at the start.

**GetInitialBuildingPositions** (`FUN_0041e920`, at activation, unless
EnablePractice): MaxBuildings[24] is **zeroed**, then for each own
building that is not a wall (kind 0xE): append {top-left cell x = pos px
>> 5, y = (pos y px >> 5) − (rec+0x86 >> 5), building index, record,
alive = 1} to the list at +0x5AC, and MaxBuildings[rec+0x88] += 1. So
MaxBuildings from the file is overwritten by the starting counts.

**MonitorBuildings** (`FUN_0041eab0`), when `tick & 7 == side`:
- sells buildings flagged 0x2000000 (and not 0x4000000) at once with the
  sell handler `FUN_00456ee0`, then flags them 0x4000000 (doc 21);
- for each initial building whose building is in state 0x11 (dying) and
  still alive: alive = 0, +4 = 0, timer +8 =
  TimeBeforeRebuilding[record] (850). The timer is **never read**
  (verified): the copy at `0x41eb92` is the only access to entry +8
  (`+0x5B4` displacement) and the only reader of TimeBeforeRebuilding
  (`0x41eb8b`) in the whole exe; the list walkers of NeedBuilding /
  FindNewPositonForBuilding (`0x421908`, `0x421994`, `0x421a11`) read
  only +0x0C, +0x0D, +0 and +1. So a dead building can be rebuilt at
  once.

**RebuildBuildings** (`FUN_0041ebc0`), if RebuildBuildings is set, when
`(tick + side) & 0xF == 0`, and only if the side has a build list
(+0x260B4 ≠ 0, i.e. a construction yard):
1. If cash + spice > CashStash[BuildBuildings]: +0x09 = 0.
2. **Not rebuilding** (+0x1D49 = 0): +0x1F = 0;
   **GetBestBuildingToBuild** (`FUN_00420d50`): over the build list, group
   g = rec+0x88, owned n = `+0x245C1[g]`, score = f32(n + 1) /
   BuildRatio[g]; a candidate needs (cash + spice > CashStash[BuildBuildings]
   or +0x09), score < best (start 100000), n < MaxBuildings[g], the
   house's version of g is not kind 0xF, and (practice or **NeedBuilding**
   (`FUN_004218e0`): an initial building of group g that is not alive).
   Then the position: FindNewPositonForBuilding (`FUN_00421950`) tries
   the **old cell** of each dead initial building of that record with
   CanAIPlaceBuilding (`FUN_00421000`); if none fits, FindAlternativePlace
   (`FUN_00421b60`) scans the 3×3 around a cell for a free drivable cell
   (last one found wins). Practice mode uses `FUN_00420e90` instead.
   If a place was found and the building slot is empty (+0x2651C = −1):
   remember cell and record, +0x1D49 = 1, **order 0x24 = start building
   with the first build-list item** (`+0x260BC[0]`, the **concrete
   slab**), +0x1D50 = 1 (TL), +2 TR when rec+0x87 > 64 px wide, +4 BL when
   rec+0x86 > 64 px high, +8 BR when both. Rect +0xF2C.. = the footprint
   (+2 per extra quarter).
3. **Rebuilding:** KickUnitsOutofArea (`FUN_00423100`): one own idle or
   guarding unit standing in the rect is moved to a free cell around it.
   `FUN_0041f5c0`: if the building slot stays empty for more than 10 ×
   max(`0x4e3ae8`, 1) passes, give up (cancel, +0x1D49 = 0). When the
   slot's item is done (progress 0x5A00) and not yet placed (+0x2652D =
   0), `FUN_0041ef30`: lay the concrete on the next pending quarter
   (order 0x26, `FUN_00459fd0`) or, once all quarters are done, place the
   building itself (CanAIPlaceBuilding at the remembered cell, order
   0x26) and reset. Between them `FUN_0041f560` starts the next item:
   concrete while quarters remain, else the building record. If the
   building cannot be placed for 0x33 tries, a new place is searched.

#### 3.4.1 Details (verified, implemented in `src/sim/ai_rebuild.cpp`)

- **Initial list** entry (16 bytes at +0x5AC, count +0xF0C, at most 0x96):
  +0 x, +1 y, +2 u16 object index, +4 u32 (zeroed on death), +8 u32
  timer (TimeBeforeRebuilding, never read), +0xC record, +0xD alive. The
  list follows the side's building list (creation order).
- **CanAIPlaceBuilding** (`FUN_00421000`, side in ECX): every cell of the
  record's 0x50 mask must be inside with x > 0 and y > 0; around each
  (x − 2 .. x + 2 and y − 2 .. y + 2, clamped, low ends below 1 become 0)
  `FUN_00428c30` sets bit 1 for an own building cell, bit 2 for own
  concrete (concrete owner bits 17–19; row 0 skipped), and counts cells
  without a building. Needs bit 1 (an own *building* nearby; own concrete
  alone is not enough). Then no mask cell may have 0x13F8 bits or lack
  buildable 0x8000. Returns 1 + the count, so callers can rank places.
- **FindNewPositonForBuilding** (`FUN_00421950`): pass 1 tries the old cell
  of every dead initial entry of the record, the last that fits wins.
  Pass 2 (if none) calls FindAlternativePlace on the probe cell left by
  pass 1 — the probe moves on each call — and checks again.
  FindAlternativePlace takes the **last** cell of x − 1 .. x + 1 ×
  y − 1 .. y + 1 with no unit / building and drivable (0x2000).
- **FindRandomBuildingPosition** (`FUN_00420e90`, also the relocation after
  0x33 failed placements): 8 tries, rand() for x then y, x in
  [xmin − 4, xmax + 2), y in [ymin − 4, ymax + 2) of the base box
  (+0x262E8 y min, +0x262E9 y max, +0x262EA x min, +0x262EB x max),
  the highest CanAIPlaceBuilding score wins.
- **KickUnitsOutofArea**: the first own unit (unit list order) whose pixel
  cell lies in the inclusive rect, that takes orders (`FUN_0049f4a0(u, 0)`:
  not state 0x12, 8, 0x14, no 0x400140) and is in state 2 or 3: the
  selection is cleared, the unit selected, GetPlaceToMoveTo
  (`FUN_00423220`: the spiral of doc 18, legs up to 18, the first cell
  outside the rect grown by 1 with `flags & 0x63F8 == 0x6000`; else its
  own cell) and a move order. The exclusion argument (0) is compared with
  object pointers and excludes nothing.
- **Quarters** (`FUN_0041ef30`), TL (x, y), TR (x + 2, y), BL (x, y + 2),
  BR (x + 2, y + 2), concrete = build list item 0: the first pending
  quarter with (CanAIPlaceConcrete or ConcreteAlready) and
  NoFriendlyUnits is taken: unless ConcreteAlready, the slab is placed
  there (order 0x26, +0x2652D = 1); its bit is cleared and +0x1F = 1
  (start the next item). If nothing was placed, the first pending quarter
  that cannot take concrete is dropped; when none remain, order 0x25
  cancels the finished slab (and, as +0x1F stays 0, nothing new starts
  until the give-up counter runs out: quirk).
  - CanAIPlaceConcrete (`FUN_004212a0`): an own building or own concrete
    within 2 cells of the slab's 0x54 cells, and one slab cell (y > 0)
    is buildable without concrete.
  - ConcreteAlready (`FUN_00421a90`): the 2 × 2 is all concrete; false
    when x ≥ W − 2 or y ≥ H − 2.
  - NoFriendlyUnits (`FUN_00423500`): no buildable slab cell has unit or
    building bits with the cell owner = side.
- **All quarters done**: CanAIPlaceBuilding at the cell; if it fits, the
  building is placed when it is the slot item and not yet sent, then
  +0x1D49 = 0, order 0x25 on the slot item (a no-op after the placement),
  +0x1D50 = +0x1F = +0x0B = 0. If not, +0x1D4C += 1; from 0x33 on, every
  pass relocates with FindRandomBuildingPosition.
- **Give up** (`FUN_0041f5c0`): with the slot empty, +0x0C += 1 per pass;
  above 10 × max(`0x4e3ae8`, 1) = 20 passes in single player (latency 2, corrected by doc 49 §1.3),
  +0x1D49 = 0 and the counters reset.
- The orders are queued, so within one pass the slot read after
  `FUN_0041ef30` is still the old one: the next item starts one pass
  (16 ticks) after a slab is placed.

### 3.5 Building repair: DoRepairs (`FUN_0041bf00`)

Every tick, for each own building with `index % BuildingRepairTicks ==
tick % BuildingRepairTicks` (+0x1D0C, 8): if the record has flag 0x200,
`hp < ftol((i64)max_hp × BuildingRepairValue[rec])` (0.66 by default)
and cash + spice > 10: set **flag 1** (repairing). The repair itself is
DoRepairs of doc 21 §2.2 (the normal per-player repair, which also
stops when full or broke).

### 3.6 Unit repair: DoRepairUnits (`FUN_0041e650`)

Only if RepairUnits and the side owns its repair pad group (+0x1D46).
Counter +0x10 += 1; every UnitRepairTicks (32) ticks, if no group is
already a Repair group (purpose 0xB): own **non-infantry** units of the
base or guard group (+0x28 = +0x1D3C or +0x1D3D), not hit for more than
12 ×8 ticks (+0x2A and +0x2B > 12), with `hp < ftol(max × UnitRepairValue)`
(0.5), in state 2 (or 3 when RepairGuardingUnits), at most +0x1D08 (5),
and if CanSpendCash `FUN_0041e8c0(2, (cost / max_hp) >> 1)` (verified:
cost = `FUN_00442bb0(record, side)`, max_hp = unit record +4
(`0x77e254 + rec × 0x100`), u32 divide; CanSpendCash: m = cash + (spice
− CashStash[2]), 0 when m < 1 as a signed value, and true when amount <
m, unsigned):
join a new Repair group, which gets the unit-repair order (0x21,
`FUN_004595e0`, doc 21 §4).

### 3.7 Upgrades and Starport

- **UpgradeBuildings** (`FUN_00421c40`), every 64 ticks (`tick & 0x3F ==
  0`), if no upgrade is in progress (+0x265F8 = −1): the first of the 8
  upgrade icons (+0x262C8) whose group can be upgraded (`FUN_004431d0`)
  gets the upgrade order (`FUN_0045a350`).
- **BuyUnitsFromStarport** (`FUN_00421cf0`), every 128 ticks, if the
  side's primary Starport slot +0x260A6 is set (verified: the placement
  switch on the building kind, table `0x4569e0`, stores kind 13 in
  +0x260A6 at `0x4567aa`; doc 12 §6 step 8) and no order is pending
  (+0x262A5): for each of the 8 Starport items that `FUN_00421e50`
  wants (at most 6 types): buy while cash + spice − price > CashStash[8]
  and stock remains; then place the order (`FUN_0045a2b0`). Not used in
  the 1.06 campaign's early missions (BuyUnits = 0).

### 3.8 Overrides, selling, MCVs, practice

**DoOverrides** (`FUN_00422c20`), when `tick & 7 == side` and DontSell is
0:
- +0x08 = +0x09 = 0, then both 1 when spice = capacity (storage full);
- no build list → stop;
- cash + spice = 0: +0x06 = 1; stop if a refinery is owned or no
  harvester;
- power produced (+0x245A8) < power used (+0x245AC): flag every damaged
  power producer (rec+0x3C < 0, hp < rec+0) for repair (flag 1, an OR);
  +0x09 = 1. If none needed it and cash + spice < 400, **sell one
  building**, picked over the side's building list `P+0x10` (verified,
  `FUN_00422c20`, `0x422d99`..`0x422e4f`):
  1. the first whose record byte +0x4E (needs power to fire) is set;
  2. else the first with **hp < max × 0.1f** and +0x41 (ticks/8 since
     hit) < 10. The test is `FILD hp; FILD qword max; FMUL f32 0.1f
     (0x4be3b8); FCOMPP; TEST AH,0x41`: the product is not rounded to
     f32, so hp = max / 10 exactly (max a multiple of 10) **is** below
     (1000 × 0.1f = 100.0000015), and the 53-bit product is exact for
     any 1.06 hit point value;
  3. else the one with the largest record byte +0x5D (strict >, the
     first on ties; none when every value is 0).

  If the pick has none of the flags 0x400000E (building up, selling),
  it is sold at once with the sell handler `FUN_00456ee0` and flagged
  0x4000000; otherwise nothing is sold this pass.

**DoMCVs** (`FUN_00422140`): +0x19EC = 1 when the side has a build list.
Every 64 ticks, each own idle MCV (kind 6, state 2) that takes orders:
if its cell allows deploying (`FUN_00443250`), deploy it
(`FUN_0045a3f0`) when +0x2E is set or the side has no build list; else
move it (`FUN_00422ec0`).

**Practice mode** (EnablePractice, skirmish): `FUN_00423850` (when `tick
& 7 == side`) sets BESERK when the side is beaten: no refinery, no
construction yard and no MCV, and cash + spice < 400; or the u16
primary slots +0x2609A, +0x2609C, +0x260A2, +0x260A6, +0x260A8, +0x260AA
and +0x260AC are all empty and no yard / MCV; or broke (< 400) and no
harvester or no refinery. The slots are those of the building kinds
(placement switch, table `0x4569e0`, verified): +0x2609A light factory
(7), +0x2609C heavy factory (8), +0x260A2 barracks (11), +0x260A6
Starport (13), +0x260AA Harkonnen palace (17), +0x260AC Ordos palace
(18), +0x260A8 Atreides palace (19): every building that makes units.
The two high tech slots (kinds 9, 10) are not tested.

### 3.9 BESERK (+0x1E = 1 or 2)

Set by the script event 4 (doc 19) or practice mode. Each tick:
- every own building that is not selling/sold (flags 0x4000006) and
  not a wall, with `rand() & 0x1F == 0` (one `rand()` per building, line
  0x1C0), is **sold** (`FUN_0045a1a0`) and flagged 0x4000000;
- group `tick & 0x1F`, if active and not already Rampaging, becomes
  **Rampaging** (purpose 0xE), with its time, state 4, target −1 and its
  average position (doc 23).

## 4. Difficulty

`0x4e8bf0` (gDifficultyLevel, 0 easy, 1 normal, 2 hard), single player:
- **AI:** only **hard** changes the AI: UnitBuildRate × 0.75 at
  activation (§2.1).
- Local player only: cost factor 75 / 100 / 125 % (`FUN_004429b0`) and
  build-time factor 125 / 100 / 75 % (`FUN_00442960`); every other side
  gets 100 %.
- Easy: deliveries to sides hostile to the local player skip the units
  whose drop flag bit 0 is set (`FUN_0049e3e0`, doc 13).

## 5. Implementation checklist (our code)

1. **Data:** parse the 0xEDC0 AI area of `.MIS` into an `AiBlock` per
   slot (`src/formats/map.*`: settings of §1.2 plus the runtime fields of
   §1.3 that the file pre-sets). Keep the file values; do not re-apply
   InitAI.
2. **`sim/ai.cpp`** with `World::tick_ai(slot)` called for each active
   slot in `World::tick()` after the mission timer and before
   `tick_script()`; `ai_activate(slot)` at mission start (after
   `init_players`, with the three `rand()` calls of §2.1 in order).
3. **Selection-based orders:** the AI issues orders the way the exe does
   (select own units, `order_move` etc., deselect). Our orders take the
   selected units of a side; keep the AI's selection separate from the
   human's (`Unit::selected` is shared; GetSelected / ResetSelected).
4. **DoBuildUnits** (§3.2) on top of `World::start_unit`,
   `unit_allowed`, `find_factory`, `unit_cost`; needs per-record owned
   counts (`+0x24628`) and the harvester / carryall / refinery counts.
5. **MonitorHarvesters**, **DoBuildCarryalls** (§3.3).
6. **Rebuild** (§3.4): initial-building list, MonitorBuildings,
   RebuildBuildings state machine with concrete quarters, on top of
   `start_building`, `placement_check` / `place_building`.
7. **DoRepairs** (§3.5) sets `Building::flags |= 1`; our `do_repairs`
   does the rest. **DoOverrides** (§3.8), **UpgradeBuildings**,
   **BuyUnitsFromStarport** later (not needed for A1V2: UpgradeBuildings
   needs tech, BuyUnits = 0).
8. **BESERK** (§3.9) and the script event 4 (+0x1E), events 6 / 7
   (UnitBuildRate, TimeBetweenBuildingAttacks).
9. Groups, attacks, defence, DoAttackApproachers, DoRepairUnits'
   group: doc 23.
10. The `rand()` call order must match: activation (3 calls),
    FindBestUnitToBuild (one per candidate, plus the practice MCV roll),
    BESERK selling (one per building), and doc 23's calls.
