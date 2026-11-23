# 19: Mission script, win / lose and the way back to the menus (1.06)

Covers the script part of the `.MIS` file (conditions and events), how
and when the game evaluates it, what each condition and event does, the
default win / lose checks of skirmish, the mission timer, and what
happens at the end of a mission: voice, banner, delay, and which menus
come next. Also the lose path (MVE_LOSE / REPLAY_MISSION) and the abort
path from the in-game menu. Decompiles come from the local GhidraMCP
server (`re/ghidra_http.py`), data from the 1.06 `MISSIONS\_*.MIS`,
`Data\bin\Templates.bin`, `Data\bin\VARS.BIN` and `Data\UI_DATA`.

Main functions:

| Address | Role |
|---|---|
| `FUN_0044f8e0` | mission loader (`.MIS` binary; text `.txt` path for development) |
| `FUN_00452490` | text loader, `[EVENTS]` section: condition and `IF … THEN` parser |
| `FUN_00453120` | text loader: one event action |
| `FUN_004501a0` | text loader, `[AI]` section (field names of the AI block) |
| `FUN_00453b90` | per tick: defeat flags, conditions, events (campaign); default checks (skirmish / network) |
| `FUN_00454120` | event executor |
| `FUN_00455290` | "has buildings" / "has units" flags for one player |
| `FUN_0044f770` | runs all 16 `FUN_00455290` checks (set-up, after load) |
| `FUN_00455480` | uncloaks a stealth unit of a player whose units are all cloaked |
| `FUN_00448550` | game loop: tick block, end of mission |
| `FUN_004762f0` | plays `S_MISSIONWON` / `S_MISSIONFAILED` / `S_MISSIONABORT` as the house's voice |
| `FUN_00425720` | adds a message line (ring at `0x504030`) |
| `FUN_0046def0` | delivery launch (doc 13 §5.2) |
| `FUN_0049e3e0` | delivery drop (doc 13 §5.3) |

"Verified" means read from the code or the data. "Inferred" is marked.
Ticks are simulation ticks (doc 02). The text names (`DEPLOY`,
`BASEDESTROYED`, …) are the exe's own keyword tables, used by the text
loader; the retail game reads only the binary file.

## Corrections to earlier docs

- **docs/formats/maps.md, .MIS table:**
  - 0xEE58: **64 event records of 0x48 bytes** (§1.3).
  - 0x10058: **48 condition records of 0x1C bytes** (§1.2).
  - 0x10728: u8 event count (`0x6b63cc`), 0x10729: u8 condition count
    (`0x6b8260`), 0x1072A: **i32 mission timer start** (`0x6b8270`,
    −1 = no timer).
  - 0x1072E, 0x2B4 bytes at `0x504030`: **the message-line ring** of
    doc 11 §2 (count, head, tail, 5 expiry ticks, 5 params, 5 × 128-byte
    texts). Not a briefing. It is all zero in every 1.06 mission.
  - 0x58, AI data, 8 × 0x1DB8: byte +0 = the slot's side, **byte +1 =
    AI active** (the AI update `FUN_0041b610` runs only when it is set).
    Field names are in §1.5.
  - 0x28..0x50 (rest of the `0x6b87c8` block) is zero in every 1.06
    mission. Inferred: unused.
  - The 8×8 alliance block holds `0` FRIENDLY, `1` HOSTILE, `2` NEUTRAL
    (`0x4e1580`), row = side, column = other side.
- **doc 13 §5.1, delivery entry +0x36:** it is the **deploy action** of
  the event (GUARD 0, ATTACK 1, RETREAT 2, STAY 3, `0x4e14e0`). The launch
  and drop code test only "= 4" (no REINFORCE voice); no other value is
  read (`FUN_0046def0`, `FUN_0049e3e0`). The free harvester uses 4.
- **doc 17 §5.6:** confirmed. `FUN_00455290` skips aircraft (unit flag
  0x40) and sandworms (unit kind 5); `FUN_00455480` the same.

## 1. The script in the `.MIS` file

### 1.1 Loading (`FUN_0044f8e0`, verified)

The binary file is read block by block into globals (docs/formats/maps.md
order). Before that the loader clears won / lost (`0x6b8274`,
`0x6b8278`). There is no other post-processing of the script: counters
and flags start as stored in the file.

Limits: 64 events and 48 conditions (array sizes; the text loader
errors at 0x40 / 0x30). The events loader refuses `[EVENTS]` in skirmish
and network maps ("Shouldn't have events in MP map").

### 1.2 Condition record (0x1C bytes at `0x6b8280 + i·0x1C`, verified)

| Off | Type | Meaning |
|---:|---|---|
| +0x00 | u32 | INTERVAL period / TIMER time |
| +0x04 | u32 | INTERVAL countdown (file: first delay) / TIMER counter (file: 0) |
| +0x08 | u32 | count or value: INTERVAL runs left, CASUALTIES minimum losses, REVEALED runs left, HARVESTED credits, FLAG value; 1 for BASEDESTROYED / UNITSDESTROYED (not read) |
| +0x0C | i32 | REVEALED cell x |
| +0x10 | i32 | REVEALED cell y |
| +0x14 | f32 | CASUALTIES ratio |
| +0x18 | u8 | side |
| +0x19 | u8 | type (§2) |
| +0x1A | u8 | BUILDINGEXISTS building record |
| +0x1B | u8 | UNITEXISTS unit record / TIMER operator (0 `>`, 1 `<`, 2 `=`, 3 `%`) |

Records are **record indices** (Templates.bin, per-house variants), not
type ids. Example: A5V1 tests "ATREIDES owns building 14 (HARKONNEN
BARRACKS)", the captured barracks.

### 1.3 Event record (0x48 bytes at `0x6b7058 + i·0x48`, verified)

| Off | Type | Meaning |
|---:|---|---|
| +0x00 | u32 | x (cell) |
| +0x04 | u32 | y (cell) |
| +0x08 | u32 | value (cash, tech, rate, timer, flag value; unused by SHOWMESSAGE) |
| +0x0C | u8 | number of conditions n |
| +0x0D | u8 | type (§3) |
| +0x0E | u8 | unit count / REVEAL radius |
| +0x0F | u8 | side (SETFLAG: the condition index) |
| +0x10 | u8 | ALLEGIANCE: other side |
| +0x11 | u8 | ALLEGIANCE: value (0 / 1 / 2) |
| +0x12 | u8 | deploy action (GUARD 0, ATTACK 1, RETREAT 2, STAY 3) |
| +0x13 | u8[14] | condition indices |
| +0x21 | u8[14] | NOT flags, one per condition |
| +0x2F | u8[20] | unit records (the text loader allows 20) |
| +0x44 | u32 | SHOWMESSAGE: string id = entry index in `text.uib` |

The 14-entry limit per event is the record layout; the text loader does
not check it (inferred).

### 1.4 How events use conditions (`FUN_00453b90`, verified)

1. Evaluate every condition, in order, into a 48-byte result array
   (0 / 1).
2. Then for every event in order: it **runs when, for every listed
   condition k, `result[cond[k]] ≠ not[k]`**. So NOT = 0 needs true and
   NOT = 1 needs false; the list is an AND. An event with no condition
   runs every tick.
3. `FUN_00454120(event)`.

All events see the same results. A SETFLAG in this tick changes a
condition only for the next tick's evaluation.

**There is no "fired once" flag.** An event runs on every tick its
conditions hold. Scripts make one-shots with:
- a FLAG condition plus SETFLAG (the usual pattern: `IF … AND NOT Cf THEN
  X` followed by `IF … AND NOT Cf THEN SETFLAG Cf 1`);
- TIMER `=` (true on exactly one tick);
- the run counters of INTERVAL and REVEALED.
WIN, LOSE, REVEAL and SETFLAG are harmless to repeat; DEPLOY, REINFORCE,
ADDUNITS and SHOWMESSAGE are not.

### 1.5 Other mission fields (verified)

- **Tech level** per side: `0x6b87c8[8]` (file +0). **Credits** per
  side: u32 `0x6b87d0[8]` (file +8). `FUN_0046bd20` copies them into the
  player's cash at set-up (`0x448a90`). `FUN_0041b250` (AI activation)
  stops with "AI not initialised correctly" when an active AI side has
  tech 0 or credits 0; this is why 1.06 missions give unused AI sides 1
  credit.
- **House per slot**: `0x8cd4f0[8]` (file +0x50), copied to
  `player+0x24253`. The local voice set is picked from
  `0x8cd4f0[gSideId]` (`FUN_0046f900`).
- **The local player is slot `gSideId`** (`0x798544`): in the campaign
  Atreides 0, Harkonnen 1, Ordos 2, the same as the house. Every "is it the
  player" test compares a slot to it, e.g. the difficulty factors
  `FUN_00442960` (build speed) and `FUN_004429b0` (cost): the difficulty
  `0x4e8bf0` applies only when the slot is `0x798544`, any other slot uses
  normal (1). The player's slot has AI byte +1 = 0 in every .MIS, so no AI
  runs it. Verified with `_H1V1.MIS` / `_H1V2.MIS`: slot 1 (Harkonnen)
  owns the construction yard and the starting units, has 2300 credits and
  tech 0 (as the Atreides slot in `_A1V1.MIS`); Atreides (slot 0) and
  Ordos (slot 2) are the AI sides.
- **AI block** (file +0x58): byte +1 = AI active. Field names come from
  the `[AI]` keyword table `0x4e1e90` (69 names, `FirstAttackBuildingDelay`
  … `SpecialWeaponDelay`). Two are written by events:
  `TimeBetweenBuildingAttacks` (+0xF14) and `UnitBuildRate` (+0x1D2C).
  Doc on the AI is a follow-up.
- **Mission timer** (file 0x1072A, §4.2).
- **Starting positions** are not in the `.MIS`. Campaign units and
  buildings come from the `.MAP` specials (docs/formats/maps.md).
- **Briefing**: not in the `.MIS`. The briefing is a movie menu
  (`MVE_<HOUSE><n>`, doc 05) and the in-game "Briefing" button opens
  `RECAP`. Mission objective lines come from `text.uib` through
  SHOWMESSAGE ids.

## 2. Conditions

### 2.1 When (verified)

`FUN_00453b90` is called **once per simulation tick**, in the tick block
of `FUN_00448550` (`0x449a8d`), after the tick counter `0x5173f4` += 1,
the mission timer step and the AI updates, and before the rest of the
simulation. It starts with one defeat check:
`FUN_00455290(side = (tick & 0xF) >> 1, mode = tick & 1)`. So each side's
"has buildings" flag refreshes every 16 ticks (even ticks) and its "has
units" flag every 16 ticks (odd ticks). At set-up (`0x448d8e`) and after
loading a save (`0x441efe`) `FUN_0044f770` runs all 16 checks at once.

The whole tick block in order (`0x449a43..0x449b28`): tick counter, mission
timer, `FUN_0041b610` for every active AI slot (`0x449a7a`), the script
(`0x449a8d`), `FUN_00455480` (`0x449a92`), the end-delay counter, the cell
objects `FUN_0044ea70`, `FUN_004466e0`, `FUN_00446690`, `FUN_0044aca0`
(orders, the object pass, repairs, power, storage), `FUN_0044d0e0` (a
debug check, only when `0x4eb008` = 0), the **unit reveal**
`FUN_0044cff0` (`0x449aeb`), the production `FUN_004467e0`, the tick & 7
player's Starport / deliveries, the finishing places `FUN_0044c520`. So
the script of tick t sees the reveals of tick t − 1 (REVEALED, §2.3).
Corrected 2026-10-16: ours revealed at the start of the tick, before the
AI and the script.

The script part runs **only in the campaign**: `0x7984c4` (network) = 0
and `gGameType` (`0x797e34`) ≠ 1. Skirmish and network games take the
default branch (§4.3).

### 2.2 `FUN_00455290(side, mode)` (verified)

- **Mode 0, "has buildings"** `0x6b87c0[side]`: 1 when the side has any
  building whose kind (building record +0x5C) is not 14 (WALL).
- **Mode 1, "has units"** `0x6b8268[side]`: 1 when the side has any unit
  that is not an aircraft (flag 0x40) and not a sandworm (kind 5). Units
  being removed or dying still count (no other filter).
- Mode 1 also sets a mask `0x6b8258[side]` used only by the skirmish
  check (§4.3): 0 when the side has buildings; otherwise it starts at 3,
  any infantry clears bit 0, any vehicle clears bit 1, and an armed unit
  (record +0x0C ≠ 0xFF) or an MCV (`0x6d60e7`) clears both.

### 2.3 Condition types (`FUN_00453b90`, names from `0x4e1730`, verified)

| Type | Name | True when |
|---:|---|---|
| 0 | BUILDINGEXISTS | the side owns a building with record = +0x1A (any state) |
| 1 | UNITEXISTS | the side owns a unit with record = +0x1B (any state, aircraft included) |
| 2 | INTERVAL | see below |
| 3 | TIMER | see below |
| 4 | CASUALTIES | see below |
| 5 | BASEDESTROYED | `0x6b87c0[side]` = 0 (no building except walls) |
| 6 | UNITSDESTROYED | `0x6b8268[side]` = 0 (no unit except aircraft and worms) |
| 7 | REVEALED | runs left (+8) ≠ 0 and the cell (+0xC, +0x10) has shroud byte 0 (open, doc 16); then runs left −= 1 |
| 8 | HARVESTED | spice + cash of the **local** player (`gSideId` `0x798544`, `player+0x24254` + `+0x2425c`) ≥ +8. The side byte is ignored |
| 9 | FLAG | +8 ≠ 0 |

Any other type is a fatal error ("Unknown event type in CheckEvents()").

- **INTERVAL:** if countdown (+4) = 0: when runs left (+8) ≠ 0 the
  condition is true, runs left −= 1 and countdown = period (+0).
  Otherwise countdown −= 1. With the file's countdown = d, it is true on
  evaluation d + 1, then every period + 1 evaluations, at most +8 times.
- **TIMER:** counter (+4) += 1 on **every** evaluation, then compare the
  new counter with time (+0): `>` counter > time, `<` counter < time, `=`
  equal, `%` counter mod time = 0. The first evaluation sees counter 1.
- **CASUALTIES:** lost = `player+0x24a08` (units lost, doc 17), kills =
  `player+0x24c90` (units killed, doc 18). True when lost > +8 and
  ratio > +0x14, where ratio = (float)lost if kills = 0, else
  `(lost·100 / kills) · 0.01f` (unsigned, integer division first).
  Verified at `0x453d5c`: `fild` of the quotient, `fmul dword [0x4c93e4]`
  (the float32 0.01f), `fcomp` with the float at +0x14, all on the x87
  stack, so the product is never rounded to float32. It is exact (an
  integer below 2^29 times a 24-bit mantissa), and we compute it in
  double, which gives the same bits. (Corrected 2026-10-16: we rounded
  the product to float32 and multiplied lost·100 as signed.)

Counts in the 1.06 missions (42 files): TIMER 209, FLAG 191,
BASEDESTROYED 139, INTERVAL 124, UNITSDESTROYED 123, BUILDINGEXISTS 79,
UNITEXISTS 52, REVEALED 34, HARVESTED 9, CASUALTIES 8.

## 3. Events (`FUN_00454120`, names from `0x4e1870`, verified)

| Type | Name | Effect |
|---:|---|---|
| 0 | DEPLOY | queue a **carryall** delivery (§3.1) |
| 1 | REINFORCE | if the side has a Starport (`player+0x260a6` ≠ −1): queue a **Starport frigate** delivery (§3.1); else nothing |
| 2 | ALLEGIANCE | `alliance[side][other] = value`. For FRIENDLY or NEUTRAL, `FUN_0046eb00` clears the target of every unit (+0x26 / +0xC) and building (+0x3C / +0x2C) of `side` that targets `other` |
| 3 | LEAVE | not implemented: fatal "Unknown event type %d" |
| 4 | BESERK | AI block +0x1E = 1 (§3.2) |
| 5 | PLAYSOUND | not implemented: fatal error |
| 6 | SETBUILDRATE | AI `UnitBuildRate` (+0x1D2C) = value |
| 7 | SETATTACKBUILDINGRATE | AI `TimeBetweenBuildingAttacks` (+0xF14) = value |
| 8 | SETCASH | `0x6b87d0[side]` = value. Inferred: no effect mid-mission, since that array is read only at set-up and by AI activation |
| 9 | SETTECH | `0x6b87c8[side]` = value (byte). Read by the build-list code `FUN_0046be50` |
| 10 | WIN | if not lost: won (`0x6b8274`) = 1 |
| 11 | LOSE | if not won: lost (`0x6b8278`) = 1 |
| 12 / 13 | BLOXFILE / ATTRIBFILE | text loader only: set the tileset / attribute name and store type 0x14 |
| 14 | REVEAL | `FUN_0044e3b0(x, y, radius +0xE)` on the local shroud (doc 16 §5.6) |
| 15 | SHOWTIMER | mission timer `0x6b8270` = value |
| 16 | HIDETIMER | mission timer = −1 |
| 17 | SHOWMESSAGE | play `S_CHATMSG` (`FUN_0046f820`), then add `text.uib` entry +0x44 as a message line (`FUN_00425720`, 200 ticks, doc 11 §2). +8 is not read |
| 18 | ADDUNITS | create the units directly (§3.1) |
| 19 | SETFLAG | condition[+0xF].+8 = value |
| 20 | (none) | no-op |

So WIN / LOSE: the first one set wins; the other is ignored.

Counts in the 1.06 missions: DEPLOY 304, SETFLAG 252, SHOWMESSAGE 146,
ADDUNITS 94, BESERK 83, LOSE 50, WIN 43, REINFORCE 36, ALLEGIANCE 28,
SETBUILDRATE 13, REVEAL 3, HIDETIMER 1. **Never used:** LEAVE,
PLAYSOUND, SETATTACKBUILDINGRATE, SETCASH, SETTECH, SHOWTIMER, BLOXFILE,
ATTRIBFILE.

### 3.1 Units: DEPLOY, REINFORCE, ADDUNITS (verified)

**DEPLOY / REINFORCE** take the side's first delivery entry that is not
active (`player+0x262ec`, 10 × 0x38, doc 13 §5.1). More than 10 active is
fatal ("Too many deliveries"). The entry gets:

| Entry | Value |
|---|---|
| +0 / +1 | cell (x, y) of the event, low bytes |
| +2 started | 0 |
| +3 active | 1 |
| +4 due tick | the current tick |
| +8… | the event's unit records, then 0xFF |
| +0x35 | 1 for DEPLOY (carryall), 2 for REINFORCE (frigate) |
| +0x36 | the event's deploy action |

The launch (doc 13 §5.2) handles this side every 8th tick (tick & 7).
- DEPLOY: a carryall from the map edge nearest the cell. The local
  player hears `*_REINFORCE` (action ≠ 4, so always for script
  deliveries). It drops the units one by one at the cell; each unit then
  drives to a free cell nearby (doc 13 §5.3). The deploy action is **not
  applied** to the units (nothing reads it except the voice test).
- REINFORCE: the frigate flies to the side's primary Starport, not to
  the event cell (missions use (0, 0) there). The local player hears
  `*_FRIGATEIN1` at launch. If a frigate of that side is already on the
  map, the entry waits (started = 0) and is tried again later. If the
  Starport is gone at launch time, `FUN_0046eac0` runs (doc 14).
- The REINFORCE Starport test happens when the event runs. Scripts that
  set their flag in the same tick lose the delivery for good when the
  side has no Starport (A5V1 E23/E25 is safe because its condition is
  "owns the Starport").

**ADDUNITS:** for each unit record: start from the event cell, find a
free cell with `FUN_004a60e0(&x, &y, 12)` (a spiral search for a cell
with no unit or building that infantry and vehicles can enter, up to
radius 12 inferred), then create the unit there with `FUN_00455870`.
No aircraft, no voice. The return value of the search is ignored, so
with no free cell the unit is created at the event cell (inferred).

### 3.2 BESERK (verified)

With AI +0x1E = 1, the AI update (`FUN_0041b610`) each tick:
- for each of the side's buildings not yet marked (+0x28 & 0x4000006 =
  0) a `rand() & 0x1f` is drawn (walls included, doc 53 §7.1); on 0 and
  not a wall it posts order **0x23 = sell** (`FUN_0045a1a0`, handler
  `FUN_00456ee0`, doc 21 §3) and marks it 0x4000000.
- AI group (tick & 0x1F, 0x2C bytes at AI +0x2E): if its job (+0x48) is
  not 0xE and +0x52 = 1 (in use), the group becomes **Rampaging** (job
  0xE, timer 10000, substate 4, no target; doc 23 §6.1): the all-out
  attack. Ours: `World::ai_beserk` (ai_groups.cpp).

It is used 83 times, mostly "IF the side's Construction Yard is gone
THEN BESERK side".

## 4. Win and lose

### 4.1 Campaign: only the script decides (verified)

In the campaign there is **no built-in check**. Won / lost are set only
by WIN / LOSE events (plus two debug keys, `0x797ecf` / `0x797ec4`, that
also need `0x7984d0`, never set in 1.06). Every 1.06 mission has its own
WIN and LOSE events. Typical LOSE: "BASEDESTROYED me AND UNITSDESTROYED
me".

### 4.2 Mission timer (verified)

- Start value from the file (0x1072A) or SHOWTIMER; −1 = off.
- `FUN_00448550` subtracts 1 per tick while it is not −1 (`0x449a49`),
  before the script runs. So 0 becomes −1 one tick later and the clock
  disappears.
- The only other reader is the draw (doc 11 §2, `"%02d:%02d"` of
  t / 1500 and (t / 25) % 60). **Nothing happens at 0.** The time limit
  of a mission is a separate TIMER condition with the same value.
- 1.06 uses it in A5V1 (15000 = 10:00, matching "TIMER = 15000": the
  contraband arrives) and O6V1 / O6V2 (45000 = 30:00, matching
  "TIMER > 45000": reinforcements start). A5V1 hides it with HIDETIMER
  when the player confiscates the contraband.
- It **is saved** (corrected): `FUN_004414d0` writes the 4 bytes of
  `0x6b8270` (`0x4417f5`) and the load `FUN_004418b0` reads them back
  (`0x441c38`), between the max crates `0x548048` and the mission path
  (doc 27, full saves). So a loaded game shows the clock where it was saved. Ours
  saves `World::mission_timer` (save_state.cpp).

### 4.3 Skirmish and network: the default check (`FUN_00453b90` else branch, verified)

Per side i, with B = has buildings, U = has units, M = the mask of §2.2:
- `alive(i)` = B or U.
- `stuck` = no side with M = 0 and no side with B; then `acc &= ~M[i]`
  over the sides with U (acc starts at 0xFF).
- If no two alive sides are non-friendly (alliance ≠ 0 in either
  direction), game over (`0x6b63c8` = 1).
- `0x6b7050` = 1 when an alive side has no active AI (a human remains).
- Local side not alive → lost (if not won). Else game over and not lost
  → won.
- If `stuck` and not game over and `0x548048` = 0: acc = 0xFD or 0xFE
  ends the game; the local side wins when it is the side checked this
  tick ((tick & 0xF) >> 1), else loses: a tie-break when only unarmed
  units remain. `0x548048` is the **maximum number of crates** (written
  only at set-up, `0x448756` / `0x44a83e..0x44a863`; read by the crate
  top-up `0x44ec34` and here; docs 31 §8, 52 §2.1), so the tie-break
  runs only with crates off. Ours:
  `World::max_crates` in `mission.cpp`.

`FUN_00455480` runs after the script every tick for side tick & 7: if
that side has no building, and every counted unit is cloaked (flag 4)
and there is at least one, the first one is uncloaked (`FUN_0049f5c0`:
clear flag 4, +0x31 = 22). So the last stealth units can be found.

`FUN_0044c520` gives every side with neither buildings nor units a
finishing place (`0x797b70`), used for network results.

### 4.4 What happens at the end (game loop, verified)

**Delay counter** `0x516540`: while the mission runs (not won, not lost,
not network), every tick after the script it is set to
`SinglePlayerDelay × 0x6b9858`. `SinglePlayerDelay` is VARS.BIN field 14
(offset 0x38) = **2**; `0x6b9858` is the number of ticks run in the last
real second (doc 02). So it holds about **2 seconds of ticks**. It is not
refreshed once the mission has ended.

**Won** (`0x6b8274` = 1, campaign or skirmish), same tick:
1. Once (`0x517400` guard): play `S_MISSIONWON` as the local house's
   voice (`FUN_004762f0` → `S_` / `H_` / `O_MISSIONWON`), queue QuitMenu
   (close any open in-game menu), then **busy-wait until the voice
   ends**. The wait only services speech and music; nothing is drawn, no
   ticks run.
2. Each following tick the game keeps running (units, script) and the
   world draw shows the "MISSION ACCOMPLISHED" banner (`FUN_00428ca0`,
   doc 11). The counter goes down by 1 per tick. `0x797e93` is
   `keystate[0x1B]` = **VK_ESCAPE** in the key table `0x797e78` (doc 10
   §1; written by the window procedure, `0x4a33a4` down / `0x4a3468`
   up; it is not a UI global): while Esc is held the counter is set to 0
   after the decrement (`0x449ca5..0x449cb9`), so the jump runs on the next
   tick. This read is in the **won** branch only (all game types); a loss
   cannot be skipped. Ours: `GameScreen` end delay (game_screen.cpp).
   The debug keys of §4.1 are `keystate['W']` (`0x797ecf`) and
   `keystate['L']` (`0x797ec4`).
3. At 0:
   - campaign mission 9 (`gMissionNumber` `0x4de610`): mission = 10,
     QuitAndJumpToMenu `MVE_ATREIDES_END` / `MVE_HARKONNEN_END` /
     `MVE_ORDOS_END` by `gSideId`;
   - other campaign missions: mission += 1, QuitAndJumpToMenu `SCORE`;
   - skirmish: QuitAndJumpToMenu `SCORE` (no increment).
   - `gGameState` (`0x4dfb08`) = 1. (The other branch, restart via
     `0x797e1c`, needs `0x7984ac`, never written.)

**Lost** (`0x6b8278` = 1):
1. Once: play `S_MISSIONFAILED` as the house's voice, set `0x5178a8` = 1
   (a "lost" flag, only cleared again when GLOBE opens), busy-wait for
   the voice. No QuitMenu.
2. "MISSION FAILED" banner while the counter runs (failed wins over
   won in the draw).
3. At 0, if REPLAY_MISSION is not already the top menu:
   - campaign: `gGameState` = 1, QuitAndJumpToMenu `MVE_LOSE`;
   - skirmish: open `REPLAY_MISSION` over the running game (§5).

**Leaving the loop:** the loop runs while `gGameState` = 2. With 1 it
ends after this pass. `0x44a0ea` stops music and speech (doc 15 §3).
Then, for `gGameState` = 1 (`0x44a440..0x44a4a2`): close all menus,
open `ABORTING` (`FUN_00470e60`, which also starts a menu transition
from `0x5178b4`), one UI update `FUN_004712c0(0x5178b4, 1)`, close all,
blit `0x5178b4` (`FUN_0042f800`). That UI update does **not** run the
queued command: `FUN_004712c0` calls `FUN_004738b0` only when the top
menu's +0x28 is set, and the menu constructor `FUN_004359a0` sets +0x28
= 0 for the menus ABORTING, WIN, LOSE, LOADING, DOWNLOADING_RESULTS,
DOWNLOADING_RESULTX, ENTERING_ONLINE, FOUND_PLAYERS, NOT_ALL_FOUND,
FAILED_MP_CONNECT, … (string compares from `0x435b38`) and 1 for all
others. The queued navigation is run by the front end afterwards.
**Checked against the original (2026-10-16, frame captures about every
17 ms):** after Abort in mission A1 the original goes from the pressed
Abort button straight to MAINMENU; no ABORTING page or other frame was
caught in between (the click was held 400 ms, so a single frame inside
that hold cannot be ruled out, but nothing stays on screen). Ours does
the same.

### 4.5 Which menus come next (verified from `FUN_004738b0` and UI_DATA)

- **QuitAndJumpToMenu SCORE**, campaign: close all, open MAINMENU, then
  (mission < 10 ? (gUseCampaignSelection `0x4e7ad0` ? GLOBE :
  LOADMISSION) : CREDITS), then SCORE_SINGLE on top. SCORE_SINGLE starts
  `score.aud` (doc 04). Closing it reveals GLOBE for the **next**
  mission (already incremented). Skirmish: MAINMENU, SCORE.
- **End movies** (`AMisEnd.uil` etc.): the movie, then QuitAndJumpToMenu
  SCORE. Mission is 10, so the stack is MAINMENU, CREDITS, SCORE_SINGLE.
- **MVE_LOSE** (`lost.uil`): movie `mve_lost`, then QuitAndJumpToMenu
  LOADMISSION → MAINMENU, then GLOBE (or LOADMISSION) for the **same**
  mission number. The player picks the territory again.

**Campaign progress** lives in UI globals only: `gMissionNumber`
`0x4de610`, `gSideId` `0x798544`, `gUseCampaignSelection` `0x4e7ad0`.
`dune2000.cfg` (`FUN_00478fa0`) does not store them. Save games do
(`FUN_004414d0` refers to `0x4de610`). So quitting the game loses the
campaign position except through a save.

## 5. Lose and abort menus (verified from UI_DATA)

- **REPLAY_MISSION** (`Replay.uil`, skirmish only): text "ReplayMission";
  Yes → `RestartGame`; No → `SetValueECTo gGameState GameStateBriefing`,
  `QuitAndJumpToMenu LOADMISSION` (skirmish: MAINMENU, then SCORE;
  `FUN_004738b0` `0x47402d`, see doc 52 §5.4).
- **Abort** (campaign): OPTIONS button (doc 11 §3) → `INGAME`
  (`InGame.uil`) → "AbortMission" → `JumpToMenu CONFIRMABORT`
  (`confirmA.uil`, "AreYouSure"), three buttons:
  - **Abort**: `PlaySample S_MISSIONABORT` (the mouse-up handler
    `FUN_00437600` sends it through `FUN_004762f0`, so it plays the house
    voice `*_MISSIONABORT`), `SetValueECTo gGameState GameStateBriefing`,
    `QuitAndJumpToMenu ABORT_MP_GAME`. For the campaign this opens only
    MAINMENU. The mission number is unchanged. For skirmish it opens
    MAINMENU, MULTIPLAYERTYPE, then SCORE (`FUN_004738b0`, ABORT_MP_GAME
    branch: gGameType 1 pushes MULTIPLAYERTYPE and jumps to the string
    `0x4def94` = "SCORE"; gGameType 0 opens MAINMENU only). Ours:
    `MenuRunner::process_navigation`.
  - **Restart**: `JumpToMenu REALLY_RESTART` (`con_rst.uil`): Restart →
    `SetValueBFTo gRestartGame TRUE`, `RestartGame`; Cancel → BackMenu.
  - **Cancel**: BackMenu.
- No banner, no MISSIONFAILED voice and no score on abort.
- After the mission ended, the OPTIONS button opens nothing (doc 11 §3).

## 6. Worked example: `_A1V1.MIS` (verified from the data)

Header: tileset `BLOXTREE`, attributes `TILEATR1`, timer −1, 13 events,
16 conditions. Tech: Atreides 0, Harkonnen 1, Ordos 1. Credits: Atreides
2300, Harkonnen 1, Ordos 1, others 0. Houses per slot `0 1 2 3 3 5 6 7`.
AI active: Harkonnen only. Alliances: each side friendly with itself,
hostile to all others.

Conditions:

| # | Type | Parameters |
|---:|---|---|
| C0 | INTERVAL | period 2000, first 2000, 5 runs (not used by any event) |
| C1 | TIMER | = 100 |
| C2 | TIMER | % 2000 |
| C3 | TIMER | = 3200 |
| C4 | TIMER | = 3500 |
| C5 | BUILDINGEXISTS | ATREIDES, record 10 (ATREIDES WIND TRAP) |
| C6 | BUILDINGEXISTS | ATREIDES, record 20 (ATREIDES REFINERY) |
| C7 | BUILDINGEXISTS | ATREIDES, record 0 (ATREIDES CONSTRUCTION YARD) |
| C8 | BUILDINGEXISTS | ATREIDES, record 38 (ATREIDES SILO) |
| C9 | UNITEXISTS | ATREIDES, record 8 (HARVESTER) |
| C10 | TIMER | % 500 |
| C11 | FLAG | 0 ("has had a harvester") |
| C12 | BASEDESTROYED | ATREIDES |
| C13 | HARVESTED | 2500 |
| C14 | FLAG | 0 ("won") |
| C15 | FLAG | 0 ("lost") |

Events:

| # | IF | THEN |
|---:|---|---|
| E0 | !C12 & !C9 & C10 & C11 | DEPLOY ATREIDES (10, 11), GUARD: HARVESTER |
| E1 | !C12 & !C9 & C10 & C11 | SETFLAG C11 = 0 |
| E2 | C9 & !C11 | SETFLAG C11 = 1 |
| E3 | C3 | DEPLOY ATREIDES (11, 3), ATTACK: 3 × LIGHT INF |
| E4 | C4 | DEPLOY HARKONNEN (18, 1), ATTACK: 2 × LIGHT INF |
| E5 | C1 | SHOWMESSAGE 596 `M1Intro1` "Build a concrete foundation before placing your buildings." |
| E6 | C2 & !C5 | SHOWMESSAGE 597 `M1Intro2` "Build a Wind Trap for power." |
| E7 | C2 & C5 & !C6 | SHOWMESSAGE 598 `M1Intro3` "Build a Refinery to collect Spice." |
| E8 | C2 & C5 & C6 & !C8 | SHOWMESSAGE 599 `M1Intro4` "Build a Silo to store additional Spice." |
| E9 | C13 & !C12 & !C14 & !C15 | WIN |
| E10 | C13 & !C12 & !C14 & !C15 | SETFLAG C14 = 1 |
| E11 | !C6 & !C7 & !C14 & !C15 | LOSE |
| E12 | !C6 & !C7 & !C14 & !C15 | SETFLAG C15 = 1 |

In words:
- Tick 100 (4 clock seconds): the concrete hint. Every 2000 ticks: the
  next build hint for what is missing (wind trap, refinery, silo).
- Tick 3200: three Atreides light infantry are carried in near (11, 3).
  Tick 3500: two Harkonnen light infantry are carried in near (18, 1).
- Harvester replacement: once the player has a harvester, C11 is set.
  If it is lost and the base stands, on the next multiple of 500 ticks a
  carryall brings a new one to (10, 11) and C11 is cleared until the new
  harvester exists, so only one is sent at a time.
- **Win:** spice + cash ≥ 2500 while the base stands.
- **Lose:** no refinery and no Construction Yard. Note that losing all
  units does not lose A1V1, and that "refinery" means record 20 only.

The decoder used for this (and for the counts above) reads the records
exactly as §1.2 / §1.3; it is not committed.

## 7. For the reimplementation

- Keep the 0x1C / 0x48 records as data and evaluate exactly as §1.4:
  all conditions first, then events in order, every tick, campaign only.
- Keep the side-by-tick schedule of `FUN_00455290`: BASEDESTROYED and
  UNITSDESTROYED lag by up to 16 ticks.
- TIMER and INTERVAL count evaluations, i.e. ticks. With the fixed
  60 Hz loop (CLAUDE.md §10) their real-time length depends on the game
  speed, as in 1998.
- The end delay uses ticks-per-last-second. In our fixed loop that is
  the speed setting's tick rate, so about 2 s of real time; plus the
  blocking voice.
- LEAVE and PLAYSOUND are fatal in the exe and unused; treat as a load
  error.

## 8. Open points

Settled 2026-10-16 from the exe:
- BESERK: order 0x23 is the sell order, job 0xE is Rampaging (§3.2).
- `0x548048` is the maximum number of crates (§4.3).
- CASUALTIES multiplies by the float 0.01f at double precision (§2.3).
- The mission timer is saved and loaded (§4.2).
- `0x797e93` is the Esc key; holding it skips the rest of the won
  delay (§4.4). The ABORTING update does not run the queued navigation
  (§4.4). The skirmish abort jumps to "SCORE" (§5).
- `FUN_00425720`'s second argument from SHOWMESSAGE is **−1**
  (`push -1` at `0x454420`, left on the stack under the text pointer by
  the `add esp, 8` after the string lookup). It is stored at
  `0x504050[line]` (`0x42577d`) and never read (the only other reference
  to `0x504048..0x504063` is the text copy), so it has no effect.

Still open:
- Not covered: the AI block fields beyond their names (docs 22, 23).
