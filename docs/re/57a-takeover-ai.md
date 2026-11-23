# 57a. TakeOver_AI: the computer plays a departed player's side (1.06)

Appendix to doc 57 §3.8. When a network player leaves with **Abort**
(leave order 9, quit kind 0), every machine runs, in lockstep,
`FUN_0045cda0` → **LetAITakeOver** `FUN_0046e9c0` → **TakeOver_AI**
`FUN_004235e0`. This doc covers exactly what that does. Doc 22 (AI
block, ActivateAI), doc 23 (groups, MonitorGroups, Rampaging) and doc
53 §1.4 give the vocabulary.

"Verified" = read from the code (`DUNE2000.DAT` 1.06 US). The former
guesses were settled from the exe on 2026-10-16 (evidence in place).

## 1. Function map

| Address | Label / role | Notes |
|---|---|---|
| `FUN_0045cda0` | runs the leave (9) / latency (0x11) records (doc 57 §1.6) | kind 0 → `FUN_0046e9c0`, other kinds ≠ 0xFF → `FUN_0046e6b0` |
| `FUN_00457810` case 0x1F | ProcessOrder, order **0x32** → `FUN_0046e9c0` too | **no poster in 1.06**: the queue insert `FUN_0045b9b0` has one caller, `FUN_0045bf00`, and its 38 call sites post types 0x09, 0x11, 0x13–0x31, 0x33, 0x34, never 0x32 (type byte at record +0x23 read before each call). Unreachable; the case is kept |
| `FUN_0046e9c0` | LetAITakeOver | AI byte on, TakeOver_AI, `0x797dfc` |
| `FUN_004235e0` | "TakeOver_AI" | this doc |
| `FUN_00423830` | profiler label | calls `FUN_0045b830` (an empty function) when `0x7984a8` is set: **no state** |
| `FUN_004a4660(side)` | side object `0x798870 + side × 0x26990` | fatal error when side > 7 |
| `FUN_0041b250` | "ActivateAI" (doc 22 §2.1) | the full activation, unchanged |
| `FUN_0041c3a0` | "FindFirstFreeGroup" | first g in 1..31 with group +0x26 (block +0x52 + g×0x2C) = 0, **else 0** |
| `FUN_0041c340(g, job)` | "GetGroupStrength" | Σ `FUN_0041cb70(unit, job)` over the side's units with +0x28 = g |
| `FUN_0041cb70` | "GetUnitStrength" | 0 for state 0x11, else hp × HPWeight[job] + speed × SpeedWeight[job] (two `ftol`) |
| `FUN_00420a70(g, &x, &y)` | "GetAveragePositionOfGroup" | unsigned mean of `(+0x34 / 32) >> 16` and `(+0x38 / 32) >> 16` (the **pixel** position's cell), (0, 0) when empty |
| `0x448460` | `rand()` thunk (file, line) | the shared MSVC LCG `0x4ed870` (doc 57 §1.9) |

## 2. LetAITakeOver (`FUN_0046e9c0`, verified)

`ECX` = the leaving side's object. In order:

1. `blk = 0x4f4118 + side[+0x24252] × 0x1DB8` (the AI block of that
   side number). If `blk+1` (AI active) is already set: fatal error
   "LetAITakeOver - Side is already AI" (`0x4e5f98`, `FUN_0042b590`).
2. `blk+1 = 1`.
3. `TakeOver_AI(blk)` (`ECX` = the block, `0x46ea2a`).
4. If gGameType `0x797e34` is 3 (serial) or 4 (modem): `0x797dfc = 1`.
   Read only after the game (`0x44a2ae`, close the connection; set to 0
   at set-up `0x448dbb`). **Transport only, no sim effect.**

Before it, in `FUN_0045cda0`, a network game (`0x7984c4`) also calls
`FUN_0045ad30(player id)`: it frees that player's queued outgoing
messages (`FUN_0045acc0`, list `0x6b98dc` / `0x6b98e0`) and the
Internet-name record (`FUN_004a3080`, only when the transport is type 5).
**Transport only.** The player's "active" byte (`0x6b922f + p × 0x3C`)
was cleared just before (our `human_[s] = false`).

The takeover runs in the order pass of tick T (after DoAI of tick T,
doc 57 §1.1); the side's first DoAI is at tick T+1.

## 3. Which AI block and settings (verified)

The block is the side's own slot, and in a network game that is a copy of
the **`_PRAC.MIS` block 0 template**: the set-up copies block 0 to blocks
1..7 with the side byte = i and clears every AI-active byte, then
switches on only the AI slots after the humans (doc 52 §2.4, doc 57 §3.4;
our `build_game`). DoAI never ran for a human's block, and no code
outside DoAI / ActivateAI was found writing it (the script writers,
events 4 / 6 / 7, are absent from `_PRAC.MIS`). Every other reference
to the block array in the code section (operand scan for
`0x4f4118..0x502ed8`, absolute and indexed) is: the block reset
`FUN_0041af50` at set-up and load (`0x44885e`, `0x44f7e5`, `0x44f8bc`),
the save / load file copies (`0x44fb8e`, `0x44fd11`), the mission
loader (`0x4501b1`, `0x450287`), the set-up switch-on (`0x469e23`,
`0x469e2f`), the script actions (`0x454329` +0x1E, `0x45434e` +0x1D2C,
`0x454372` +0xF14), LetAITakeOver (`0x46ea0e`), reads of the AI-active
byte, a debug overlay that only reads (`0x4242b9`), and the unit
update's group feedback (`0x498660..0x4986d2`), which is guarded by
`cmp byte [blk+1], 1` at `0x498650`. Calls into the AI code from
outside go through DoAI, ActivateAI, TakeOver, that guarded unit-update
path (`0x498669`, `0x4986b7`) and helpers that write no block
(`FUN_00421800`). So during play nothing writes an inactive block.

So the taken-over side gets the **practice AI**: EnablePractice (+0x03)
= 1, UnitBuildRate (+0x1D2C) 80, TimingRandomPercentage 25,
FirstAttackBuildingDelay 12000, FirstAttackUnitDelay 6000, DefendOn
(+0x1D) 1, GuardGroupSize 300, FreePercentage 0, no defend areas yet
(+0x1D51 = 0), BESERK 0. All 32 group records of the template are zero
(checked in the 1.06 file). It is the same block a skirmish AI starts
from, but activated mid-game with the side's current units and buildings.

## 4. TakeOver_AI (`FUN_004235e0`, verified)

```
TakeOver_AI(blk):                                  // ECX = blk (EDI)
    log("TakeOver_AI()")                           // no state
    P = side object(blk[0])
    ActivateAI(blk)                                // FUN_0041b250, §5
    n = 0                                          // u8 counter
    g = FindFirstFreeGroup(blk)                    // 0 when none
    for u in P's unit list (P+0x8, next u+0x7C):   // every unit, dying ones too
        if urec[u+0x18].weapon (+0x0C) == 0xFF     // 0x77e25c[rec × 0x100]: unarmed
           or u.flags (+0x08) & 0x400140           // aircraft 0x40, carried 0x100, removed 0x400000
           or (P+0x262EA <= u.cx (+0x4A) <= P+0x262EB      // inside the base box,
               and P+0x262E8 <= u.cy (+0x4B) <= P+0x262E9): // bounds inclusive
            u+0x28 = 0xFF                          // no group
        else:
            u+0x28 = g
            n += 1
            if n >= 20:                            // `0x13 < n`
                SetUpRampage(g); G(g)+0x26 = 1     // in use
                n = 0
                g = FindFirstFreeGroup(blk)
    SetUpRampage(g)                                // the last group: NOT marked in use

SetUpRampage(g):                                   // group G = blk + 0x2C + g × 0x2C
    G+0x1C = 0x0E                                  // job Rampaging
    G+0x0C = GetGroupStrength(g, 0x0E)             // strength (Rampaging weights)
    G+0x1D = 4                                     // substate 4: pick a target
    G+0x18 = 0                                     // age
    G+0x08 = (rand() & 0x3F) + 1                   // timer 1..64; rand line 0x1C84 in the loop, 0x1C99 after it
    G+0x02 = 0xFFFF                                // no target
    (G+0x24, G+0x25) = GetAveragePositionOfGroup(g)
```

Stack offsets re-checked in the disassembly (`0x42362a..0x42379f`): the
`PUSH EBP` inside the loop shifts the locals by 4; the counter, group and
side pointer are read consistently. The group's +0x00 (target side),
+0x10 (start strength), +0x27 (mode) and +0x28 (defend index) are not
written: they keep what ActivateAI left (0 for these groups).

### 4.1 What happens to the units

- **Armed ground units outside the base box** are cut, in the side's list
  order, into **Rampaging groups of 20**. Each starts at substate 4, so
  its next MonitorGroups pass (doc 23 §4.6, job 0xE) picks the nearest
  hostile thing (`FUN_00420700`, doc 23 §6.3) and attacks; the timer
  counts the attacks left; at 0 the group returns to base.
- **Units in the base box, unarmed units (harvesters, MCVs, engineers,
  …), aircraft, carried units** get no group (0xFF). The next
  GroupNewUnits pass (tick & 0x1F = side, doc 23 §3.4) puts them in the
  Base group (+0x1D3C) with the guard order 0x18, as the start units of a
  skirmish AI. Harvesters, carryalls and MCVs are skipped there as usual.
- **The last group (1..20 units, or 0 when the count is a multiple of
  20) is never marked in use** (`0x42372a..`: no write to +0x26). Quirk,
  kept:
  - MonitorGroups does **not** test "in use" before its job switch
    (`FUN_0041d070`, only the strength-0 freeing tests it), so if its
    pass for g comes first, this group rampages like the others.
  - GroupNewUnits frees every unit whose group is not in use
    (`FUN_0041c3e0`: `+0x28 = 0xFF` when `G(+0x28)+0x26 == 0`), so at the
    side's next GroupNewUnits pass the remaining units join the Base
    group. Which comes first depends on T, the side number and g; both
    are deterministic.
  - FindFirstFreeGroup sees the group as free: the next new group (GroupNewUnits,
    attacks, splinters) may get the same number and inherit its units.
  - With 0 units: strength 0, position (0, 0); harmless.
- **No free group:** FindFirstFreeGroup returns **0**, and units go to
  group 0 (the reserved, in-use group). After ActivateAI groups 0, 1, 2
  are taken, so this needs more than 29 × 20 = 580 eligible units.
  The side's only limit is its object pool of 1000 slots (doc 50 §1,
  shared with its buildings, bullets and effects), so it is possible in
  principle but not in normal play. Our `ai_take_over` maps "no free
  group" to 0 as the exe does.

### 4.2 rand() draws, in order

1. ActivateAI: lines 0xE2 (morale factor), 0xEA (building-attack timer),
   0xEE (unit-attack timer). FreePercentage is 0 in `_PRAC.MIS`, so no
   CreateSplinterGroup draw (line 0x645). GroupNewUnits is not called
   (§5), so no draws from it.
2. One draw (line 0x1C84) per full group of 20, in list order.
3. One draw (line 0x1C99) for the last group, **always**, even with no
   eligible units.

Total: `3 + floor(n / 20) + 1`, n = eligible units.

## 5. ActivateAI as called here (`FUN_0041b250`, verified)

Doc 22 §2.1 and doc 53 §1.4 apply unchanged; the network-relevant points:

- gGameType ≠ 1, so the "AI not initialised correctly" check runs: tech
  `0x6b87c8[side]` and credits `0x6b87d0[side]` must be non-zero.
  Network set-up gives every side gNetTechLevel and gNetStartingCredits,
  so it passes.
- Hard difficulty (`0x4e8bf0` = 2): UnitBuildRate × 0.75 (80 → 60).
  **gDifficultyLevel is each machine's own setting** (multiplayer audit
  B4/B5): the original desyncs here if the players' settings differ.
- GetInitialBuildingPositions (`FUN_0041e920`) is skipped (practice).
- Groups: the 32 records are partly cleared (+0x02 target = 0xFFFF;
  +0x0C, +0x14, +0x18, +0x1C, +0x1D, +0x20, +0x26 = 0; the other bytes are
  kept, all 0 in the template). Group 0 in use, group 1 Guarding
  (+0x1D3D), group 2 Base (+0x1D3C).
- **No GroupNewUnits** (`0x7984c4` set).
- **DoUnitFacings** (`FUN_00420c80`): if the side has a primary
  construction yard (P+0x26098 ≠ −1), **every unit of the side** (aircraft
  too, no state test) turns body, turret and turret goal (+0x54..+0x56)
  to face away from the retreat cell. Visible as a snap at the takeover.
- Guarding group strength = GetGroupStrength(1, job 1): it sums units
  with +0x28 = 1. There **is** one non-AI writer of unit +0x28: the
  Ctrl + digit control groups, `FUN_0044b170` (`0x44b1ad` / `0x44b1b7`),
  which act on the **local** player's units only (`0x798544`) and post
  no order. The other byte writes outside the AI code are CreateUnit's
  0xFF (`0x4559cd`) and the sync sum `0x459154..0x459198`, which zeroes
  +0x19, +0x1D and +0x28 while it sums a unit and restores them (the exe
  itself treats these fields as machine-local). The departed side was
  never the local player of a machine that runs the takeover, so on
  every such machine its units still carry CreateUnit's 0xFF and the
  Guarding strength is 0 everywhere (our `assign_group` is called for
  the local player only, so the same holds). MonitorGroups later
  frees the empty Guarding group.
- Practice: +0x19EC = 0 (DoMCVs sets it back to 1 at the next DoAI when
  the side has a build list). Retreat cell `FUN_0046e3f0` → +0xF57 /
  +0xF58. +0xF56 (GuardBuildings) = 0.
- DefendOn (+0x1D) = 1 and GuardGroupSize 300: **defend area 0** = the
  side's **current** base box (P+0x262E8..EB as RebuildLists last left
  it), wanted 1000, period 100, +0x1D64 = −1; +0x1D52 = 0, +0x1D51 = 1.
- FreePercentage 0: no splinter. Then ClearUnits (`FUN_0041cdb0`):
  **the selection byte +0x19 of every unit of the side is cleared** on
  every machine.

## 6. Machine-local state and desync risks

Read on the TakeOver path: the side's units, buildings, base box, AI
block, `0x797e34` (gGameType, same everywhere), `0x7984c4` (network
flag), `0x4e8bf0` (gDifficultyLevel). **No read of gSideId (`0x798544`)**
in `FUN_0046e9c0`, `FUN_004235e0`, `FUN_0041b250`, `FUN_00420c80`,
`FUN_0046e3f0`, `FUN_0041c340`, `FUN_0041cb70`, `FUN_00420a70`,
`FUN_0041c3a0`, `FUN_0041cdb0`.

- **Desync:** gDifficultyLevel (§5). Use the shared value (audit B4/B5).
- The unit selection byte +0x19 is shared between the AI and the local
  UI; ClearUnits only writes it, but DoAI's GetSelected later reads the
  local selection (audit, "GetSelected put-aside").
- `0x797dfc` and `FUN_0045ad30` are transport bookkeeping.

## 7. Our code

- `World::let_ai_take_over` (`src/sim/network.cpp`) sets the AI byte and
  calls `World::ai_take_over` (`src/sim/ai_groups.cpp`, §8).
- `World::ai_activate` (`src/sim/ai.cpp`, FUN_0041b250) and
  `ai_activate_groups` (`src/sim/ai_groups.cpp`) are reused as they
  are: `campaign` is false, so GroupNewUnits is skipped; the full
  group-area clear and the unit `ai_group = 0xFF` pre-pass give the same
  result here (template groups are zero; TakeOver rewrites every unit's
  group anyway).
- `ai_free_group` returns −1 where the exe returns 0 (§4.1);
  `ai_take_over` maps it to 0.
- `ai_group_position` (FUN_00420a70, **corrected 2026-10-16**): the exe
  averages the cell of the **pixel position**, `(i32 [u+0x34] / 32) /
  65536` and the same for +0x38 (signed `cdq / and / sar` pairs at
  `0x420aa9..0x420adc`, both divisions truncating), summed in u32 and
  divided unsigned (`div` at `0x420af5` / `0x420aff`), over every unit
  of the side's list whose +0x28 equals g (char compare at `0x420aa5`).
  It is not +0x4A / +0x4B (`cur_x / cur_y`), which differs from the
  pixel cell for part of a step. This affects every caller (groups'
  positions for attacks, retreats, splinters), not only TakeOver.
- `ai_group_strength_job` (FUN_0041c340 with an explicit job),
  `base_box`, `ai_unit_facings`, `ai_clear_selection` exist.

## 8. Implementation

- [ ] `World::let_ai_take_over(s)`: keep the AI byte write (ignore or
      assert when already set: the exe's fatal error), then call a new
      `ai_take_over(s)`.
- [ ] New `World::ai_take_over(int s)` in `src/sim/ai_groups.cpp`
      (comment `FUN_004235e0 "TakeOver_AI"`, docs/re/57a):
      1. `ai_activate(s)` (3 rand draws, facings, defend area, clear
         selection).
      2. `g = ai_free_group(s)`, mapped −1 → 0.
      3. For each unit of side s in list order (`units_`, the P+8 order
         used everywhere else; skip `remove`): exclude when
         `unit_records[rec].weapon == 0xFF`, `flags & 0x400140`, or
         `cur_x` in [box x0, x1] and `cur_y` in [y0, y1] (`base_box`,
         inclusive) → `ai_group = 0xFF`; else `ai_group = g`, `++n`, and at
         n = 20: `set_up_rampage(g)`, `G(g, gInUse) = 1`, n = 0,
         `g = free group` (−1 → 0).
      4. `set_up_rampage(g)` once more, without gInUse.
      `set_up_rampage`: gJob = 0xE; gStrength =
      `ai_group_strength_job(s, g, kJobRampaging)`; gSub = 4; gAge = 0;
      gTimer = `(rng_.next() & 0x3F) + 1`; gTarget = 0xFFFF; gX / gY =
      `ai_group_position(s, g, ...)` (bytes). Keep this write order.
- [ ] Use the shared difficulty for `ai_activate`'s hard-mode rate in
      network games (audit B4/B5).
- [ ] Nothing for `0x797dfc` / `FUN_0045ad30` (transport).
- [ ] Test: a network game where one side leaves with kind 0: the rand()
      count is `3 + floor(n/20) + 1`, groups of 20 are Rampaging and in
      use, the last group is not in use and its units reach the Base
      group at the next GroupNewUnits; the determinism fingerprint is the
      same on two worlds with different `local_player`.
- [x] Order 0x32 (ProcessOrder case 0x1F) → `let_ai_take_over`, kept for
      completeness; 1.06 has no poster for it (§1), so it never runs.
