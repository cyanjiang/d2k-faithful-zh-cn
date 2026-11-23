# 40: Deviator (1.06)

Written for a line-by-line port. Conventions as in docs 30 and 37: `u+…`
are unit fields (doc 09 §2), `urec+…` the unit record (`0x77e250 + rec ·
0x100`, special kind at +0x2D), `player+…` the struct from
`FUN_004a4660(side)`. `rand()` is the shared MSVC LCG
(`thunk_FUN_004b1810`). "Verified" = read in the decompile and checked in
the disassembly (`re/disasm.py`). Data values are from the 1.06
`Templates.bin`, `ARMOUR.BIN` and `samples.uib`.

## 0. Function and data map

| Address | Name / meaning |
|---|---|
| `FUN_0049be60` | bullet update; passes `deviate = bullet flag 0x40` to the damage call at `0x49bfac` (flag 0x1000 detonation), `0x49c58a` (homing impact) and `0x49c939` (normal impact). The sonic call `0x49c098` passes 0 |
| `FUN_0049d2e0` | damage (doc 18 §2); the deviate branch is `0x49dbd0`..`0x49dc57` |
| `FUN_0049d280(u)` | "on a building cell": cell flag 0x10 at the current cell, or (flag 0x10000000) at the "to" cell |
| `FUN_0046d2e0(from, to, idx, keepState)` | ConvertUnit (doc 30 §2.6) |
| `FUN_00455870` → `FUN_0046bd60` | CreateUnit; the new unit is **appended at the tail** of the side's list |
| `FUN_00497b90` | unit update; deviation countdown / revert at `0x497fcf`..`0x49808f` |
| `FUN_0049fb40(u)` | "deviation runs": `u.flags & 0x1000` and the original side `u+0x2D` still has buildings (`0x6b87c0[s]`) or units (`0x6b8268[s]`). Debug error "oldSide illegal for deviated unit" when `u+0x2D > 7` |
| `FUN_0041c3e0` | AI GroupNewUnits: flags 0x80 / 0x1000 → own Rampaging group (`0x41c5b4`..`0x41c730`) |
| `FUN_0041d070` | AI MonitorGroups: defend-area threat weight, dead kind-0x10 case (`0x41dfef`) |
| `FUN_00463990` | text record parser: kind 0x10 only for a name containing "Deviator" (case-sensitive `strstr`, `0x463b8f`) |

## 1. Data (verified)

### 1.1 Unit record 14 DEVIATOR (Ordos)

| Field | Value |
|---|---|
| houses | O (+0x00 = 0x04) |
| armour +0x02 | 3 = **WOOD** |
| hp +0x04 | 1100 |
| speed +0x08 | 80 (<<10) |
| primary +0x0C / secondary +0x0D | 16 DEVIATOR / none |
| **reload +0x0E** | **160 ticks** (8 s at 20 ticks/s) |
| view +0x10 | 3 |
| infantry +0x11 | 0 |
| base / turret element +0x14 / +0x18 | 23 / −1 (**no turret**: the body turns to aim) |
| turret flag +0x26 | 0 |
| **special kind +0x2D** | **0** (see §1.3) |
| death explosion +0x2F | EX_BANG14 |
| muzzle offset +0x37 | 4 px |
| record flags +0x38 | 0 |
| muzzle flash +0xA8 | 0xFF (none) |
| cost / build time / tech | 1000 / 20 / 6; needs HEAVY_FACTORY + RESEARCH_CENTRE |

### 1.2 Bullet 16 DEVIATOR (raw `00 00 09 00 f4 01 00 00 50 00 00 00 0a 00 00 00 10 0e 0f 01 00 01 00 00 a0 00 00 00`)

| Field | Value |
|---|---|
| speed +0x00 | 0x90000 = **9 px/tick** |
| damage +0x04 | **500** |
| flags +0x08 | **0x50** = 0x10 (trail every tick) + **0x40 (deviate)**. Not homing (no 0x20), no arc (no 0x1), no 0x1000 |
| fire sound +0x0C | 10 = **S_BIGROCKET** (`missle1`) |
| image +0x10 | 16; **32 rotation frames**, DATA.R16 entries 4114..4145 |
| impact explosion +0x11 | 14 EX_DEVIATE (no sound, explosion flags 0x18) |
| trail explosion +0x12 | 15 EX_GREENEXP (no sound, flags 0x18) |
| +0x13 | 1 |
| anti-air +0x14 | 0 |
| warhead +0x15 | 1 **W_DEVIATE** |
| range +0x18 | **160 px** (5 cells); flight ≤ 17 ticks |

W_DEVIATE (doc 18 §1.2): 20 % against every armour except CONCRETE 100,
INVULNERABLE 0, CY 10; **radius 15 px**; infdeath 0. Full-centre damage
= 500 · pct / 100 = **100** against NONE / WOOD / LIGHT / HEAVY / BUILDING /
HARVESTER, 50 against CY, 500 against CONCRETE, with the linear fall-off of
doc 18 §2.1. So **every deviator hit also deals damage**.

The bullet is never "air" (no arc, height stays 0x60000, the test is
`height > 0x60000`), so **aircraft are never hit or deviated**.

### 1.3 Special kind 0x10 is dead data

The text parser gives kind 0x10 only to a name containing "Deviator"
(case-sensitive); the record name is "DEVIATOR", and the shipped
`Templates.bin` has kind **0**. The exe has two kind-0x10 readers, both
unreachable in 1.06: SetState's kind jump table (`0x49debf`, byte table
`0x49e0ac`: kind 16 → `0x49df15`, like kinds 1, 3–7 and 11–14; kind 0 →
`0x49defe`, like kind 15) and the AI defend-area
threat weight (§5.3). Port: **treat the Deviator as kind 0**; do not add
a kind-16 path.

## 2. Hit: `FUN_0049d2e0(x, y, height, bullet, attacker, firer, deviate)` (verified)

The bullet's owner side is `attacker`. In the unit loop of doc 18 §2, per
hit unit `u` of side `p`, in this order (only the **survive** branch has
deviate code; a killing hit is an ordinary kill):

```
u.flags |= 1
if dmg < u.hp:
    u.hp -= dmg
    neutral → hostile (FUN_0049f870)                         // doc 18 §2.2
    if !(u.flags & 0x2000000):
        HARVATTACK voice for a local harvester with a known firer
    else:
        u.flags &= ~0x2000000                                // first hit after a conversion: no voice
    if notWorm and u.hp < urec.hp / 2: u+0x27 = (rand() & 0x7F) + 0x40   // line 0x1dba
    if u+0x32 == 0xFFFF and bullet != SPICEEXPL and attacker != p:
        u+0x30 = attacker; u+0x32 = firer
    if deviate and notWorm:                                  // notWorm = urec+0x2D != 5
        u.flags &= ~1                                        // the deviation hit is not a "hit"
        if attacker == p or urec+0x11 (infantry):
            u+0x2A = 1
        else if FUN_0049d280(u):                             // on a building cell
            u+0x2A = 1
        else:
            if !(u.flags & 0x1000): u+0x2D = p               // original side, kept on re-deviation
            c = ConvertUnit(p, attacker, u+0x8C, keepState 0)   // FUN_0046d2e0
            if c:
                c+0x2E = 0xF0                                // 0x49dc41
                c.flags |= 0x1000
```

So a unit is **converted** when all of these hold: it survives the
damage, it is not infantry, not a sandworm (kind 5), not an aircraft
(bullet never "air"), not carried / flag 0x8000000 / state 8, 0x11, 0x17
(skipped by the loop), not owned by the attacker, not on a building cell
(refinery dock, repair pad, …), and ConvertUnit succeeds (fails on flag
0x400000 or when CreateUnit hits the unit limit). **Harvesters, MCVs and
Devastators can be converted.** Infantry and own units only take the
damage and get `+0x2A = 1`. Buildings only take damage (the building
loop has no deviate code). The unit's own side's units in the radius are
damaged too (no friendly-fire check), never converted.

`+0x2D` is written on the **old** unit before ConvertUnit, so the copy
carries it; a unit that is already deviated keeps its first original side
and gets a fresh timer (0xF0) from a third side's deviator.

ConvertUnit (doc 30 §2.6, re-read): copies 0x7C bytes (hp after the
damage, flags, `+0x2D`, …); new unit: `+0x19` (selected) = 0, `+0x30` =
0xFF, `+0x32` = 0xFFFF, `+0x2A` = 0xFF, target `+0x26` = 0xFF / `+0x0C` =
0xFFFF, `+0x14` (next state) = 2, dest = its "to" cell, `+0x28` (control /
AI group) = 0xFF, SetState 7, "to" cell re-reserved for the new side,
flags |= 0x2000000. Old unit: flags |= 0x600000, SetState 0x11 (no
KillUnit: **no loss counter, no UNITLOST voice, no explosion**).

No sound, voice or message is played for a deviation (the strings table
has no deviation voice; the only "Deviator" strings are the parser name
and the debug error). The visible cues are the EX_DEVIATE impact and the
unit simply changing colour (§6).

### 2.1 Quirk: the copy can be hit again by the same bullet (verified)

CreateUnit appends to the **tail** of the attacker's list (`FUN_0046bd60`)
and the damage loop visits sides 0..7 in order. If `attacker > p`, the
loop later walks the attacker's list and finds the new unit at the same
pixel: it is damaged **again** (same dmg), `flags |= 1` then cleared,
attacker == owner → `+0x2A = 1`; it is not recorded as attacker (same
side). If that second dmg ≥ its hp it dies (counted as in §4.3). If
`attacker < p` the copy is not visited (its `+0x2A` stays 0xFF). Keep it:
our `damage_at` loops `p` then `units_` filtered by owner, which gives the
same result, provided the `Unit&` is re-fetched after `convert_unit`
(vector growth).

### 2.2 Quirk: CreateUnit failure

ConvertUnit clears the old unit's cell bits **before** CreateUnit; if
CreateUnit returns 0xFFFF (unit limit) it returns null and the old unit
lives on with cleared cell bits (and `+0x2D` written, flag 0x1000 not
set). Ours does the same: `convert_unit` returns -1 there after
`vacate` (verified against `FUN_0046d2e0`: the cell bits are cleared
before `FUN_00455870`, and the 0xFFFF return skips the copy). The unit
then stands on cells without its bits until it moves (its next step
writes them again), so other units may enter its cell meanwhile.

## 3. Countdown and revert: unit update `FUN_00497b90` (verified, `0x497fcf`..`0x49808f`)

Runs every tick for every unit, after the reload / stealth / smoke code
and before the `+0x2B` bookkeeping and the state switch:

```
if u.flags & 1:                                    // hit this tick (any bullet, any tick)
    u+0x2A = 0; u.flags &= ~1
    if FUN_0049fb40(u):                            // deviated and original side alive
        u+0x2E = (u+0x2E < 30) ? 0 : u+0x2E − 30
elif (tick & 7) == (idx & 7) and !(u.flags & 0x100):    // own 8-tick slot, not carried
    u+0x2A = min(u+0x2A + 1, 255); if u+0x2A > 100: u.flags &= ~8
    if FUN_0049fb40(u):
        if u+0x2E != 0:
            u+0x2E −= 1
        elif !FUN_0049d280(u) and !(u.flags & 0x400000):
            u.flags = (u.flags & ~0x1000) | 0x80   // before the copy, so the copy has them
            if side != u+0x2D:
                ConvertUnit(side, u+0x2D, idx, 0)  // back to the original side, state 7
```

- **Duration:** constant 0xF0 = 240 steps of 8 ticks = **1920 ticks ≈
  96 s** at 20 ticks/s, then the revert on the next slot (241st). Not in
  `VARS.BIN`; hard-coded at `0x49dc41`. Each tick in which the deviated
  unit was hit by anything (flag 1; the deviation hit itself clears it)
  **removes 30 steps (240 ticks)**, and that tick does not count down.
- The countdown **freezes** while the original side has neither buildings
  (except walls) nor units (except aircraft and worms), the flags of doc 19
  §2.2 refreshed every 16 ticks in every game mode; a side wiped out keeps
  nothing back, and the unit stays converted for good. It also freezes
  while carried (0x100).
- At 0 the revert waits while the unit stands on a building cell or has
  0x400000, retrying on each slot.
- `side == u+0x2D` (re-deviated by its own original side): only the flags
  change, no conversion.
- The revert copy has 0x80, no 0x1000, `+0x2E` = 0, `+0x2D` unchanged,
  0x2000000, selection / groups / targets reset as in §2. The old copy
  goes to state 0x11 **inside its own update**; the rest of this update
  runs on it (state 0x11 case).
- Flag 0x80 is cleared only by AI GroupNewUnits, so a human-owned reverted
  unit keeps it (harmless: nothing else reads unit flag 0x80, scan of all
  `test …, 0x80` sites).

### 3.1 Byte +0x2E is shared

`u+0x2E` is also the **delivery entry** of a just-delivered unit (flag
0x40000, read by GroupNewUnits `0x41c5d0`) and appears in doc 30's MCV
TODO. The exe uses one byte for all. Our `Unit::delivery_entry` is +0x2E;
the deviation timer must **alias that same byte** (or the port must copy
the exact overwrite), e.g. a delivered unit deviated before its AI grouped
it: GroupNewUnits tests 0x80 / 0x1000 first, so the 0x40000 branch later
reads the deviation timer value (0 after a revert) as the delivery entry.

## 4. Other readers (verified by a full-text scan for unit flag 0x1000, +0x2D, 0x80, 0x2000000)

Unit flag 0x1000 is read only at `0x41c5bc` (GroupNewUnits), `0x49803c` /
`0x49806c` (update), `0x49da38` / `0x49dc1c` (damage) and `0x49fb48`
(`FUN_0049fb40`). Unit `+0x2D` only at `0x498074`, `0x49da3d`, `0x49da57`,
`0x49dc21`, `0x49fb4d` / `0x49fb65` (plus a debug sprintf). Unit flag
0x2000000 only at `0x49daee` (damage, the HARVATTACK skip). So there is no
draw, radar, selection, sidebar or AI-target code for deviated units.

### 4.1 Death of a deviated unit (`0x49da35`..)

The killing hit (not SPICEEXPL) counts as usual for the attacker
(`player(attacker)+0x24c98[p + rec·8]`, `+0x24c90`), but the **loss**
counters go to the original side when flag 0x1000 is set:

```
s = (u.flags & 0x1000) ? u+0x2D : p
player(s)+0x24a08 += 1; player(s)+0x24a10[rec] += 1
```

The UNITLOST voice still follows the current owner `p` (local player).

## 5. Computer player (verified)

### 5.1 GroupNewUnits `FUN_0041c3e0` (non-BESERK branch)

For an ungrouped unit (`+0x28` = 0xFF or its group no longer in use),
without flags 0x840, **not kind 1 / 2 / 6** (harvester, carryall, MCV are
skipped before the test, so a deviated harvester or MCV is never put in a
group), not kind 4 / 0xC (counted), not busy (state 8, 0x14, 9 or `+0x4F`
steps ≠ 0, counted as busy), and with **flag 0x80 or 0x1000**:

```
g = FindFirstFreeGroup()                // FUN_0041c3a0: first of 1..31 not in use, else 0
if g == 0: nothing (0x80 stays; retried next time)
else:
    u+0x28 = g
    G[g]+0x48 job = 0xE (Rampaging)
    G[g]+0x34 (gTimer) = 2
    G[g]+0x38 (gStrength) = FUN_0041c340(g, 0xE)       // includes u (its group is already set)
    G[g]+0x52 in use = 1; +0x49 substate = 4; +0x44 age = 0; +0x2E target = 0xFFFF
    u.flags &= ~0x80                     // 0x1000 stays
    if urec+0x2D == 7 and (rand() & 1):  // line 0x53c; rand() only for kind 7
        u+0x19 = 1; FUN_0045a3f0(side, u+0x8C); u+0x19 = 0     // self-destruct order 0x2F
```

Correction to doc 37 §1.6: `+0x34 = 2` is the group **timer** (gTimer)
and `FUN_0041c340` fills **+0x38, the strength** (our names in
`ai_groups.cpp`). The group's `+0x2C` side and `+0x50/+0x51` cell are not
written here (left from the group's last use). They are **not**
overwritten before use: MonitorGroups case 0xE (`FUN_0041d070`) calls
`GetNearestHostileThing` (`FUN_00420700`) with the group's +0x50 /
+0x51 as the origin while the substate is 4, so the first search of a
deviation group measures from the stale cell of the slot's last use
(0, 0 for a slot never used since the mission start); the side byte is
an output of that search. Ours keeps the group record as the exe's
bytes (`ai_u8` / `set_ai_u8`), so the same stale cell is used (verified
2026-10-16). The function-level free group
(`[esp+0x28]` / `ebx`) is overwritten by this branch, but it is only read
again when the post-loop "start a group with flag 1" search set it anew,
so this has no effect (checked at `0x41c76f`..`0x41c7bc`).

Effects: a unit an AI deviates forms a one-unit rampage group (attacks
its former friends); a unit **returning** to an AI side (0x80) does the
same on its original side. Since 0x1000 is not cleared, a deviated unit
whose group dies out forms a new rampage group again, and a deviated
Devastator rolls `rand() & 1` again each time.

### 5.2 Targeting

`NewBestBullet` (`FUN_0049e9d0`) and the scans have no deviator case: the
Deviator fires at any enemy in range, infantry and buildings included
(damage only), and never at its own converted units (they are own side).
The AI has no special use for Deviators (kind 0).

### 5.3 MonitorGroups defend-area weight (dead in 1.06)

In the defend-area scan (`0x41dfd3`), each hostile unit found adds
`FUN_00423360` that many times: kind 4 → 6, kind 3 → 4, kind 0x10 → 6,
else 2. The Deviator has kind 0, so it weighs **2**. (This scan is not
ported yet; port it with kind 0x10 kept for exactness, it never matches.)

## 6. Player side and drawing (verified)

- No deviation voice, message or sound (§2). The deviated unit is
  deselected (`+0x19 = 0`), loses its control group (`+0x28` = 0xFF, the
  same byte as the AI group), its target and its attacker record, and
  walks to its "to" cell (state 7), then state 2 (guard), from which it
  fights for its new side through the normal scans.
- Drawing: the display code (`FUN_00428e30` and the unit draw pass) has
  no test of flag 0x1000 / 0x80 or `+0x2D`; a deviated unit is drawn like
  any unit of its **current** owner (house colour of the new side): the
  copy is a new object in the new side's list, and the draw takes the
  colour from the list being drawn (verified; ours from `Unit::owner`).

## 7. Open points

Closed 2026-10-16:

- **Harvester +0x58, repair pad +0x5A (corrected in our code).**
  ConvertUnit copies the first 0x7C bytes, so the copy keeps the old
  side's slot numbers and its load. Every reader looks them up with
  `FUN_004a4740(side, index)`, which returns only an object of the
  unit's **own** side that is a building (states 0xb / 0xd / 0xe / 0xf
  of `FUN_00497b90`, `FindSpice FUN_0044d760`, the pad case at the
  `+0x5a` read), and the callers test kind 4 / 6; if that fails the
  harvester re-picks the nearest own refinery (`FUN_004a5f80`) and the
  unit the nearest pad. `FUN_004a5e80` (least-used refinery) also counts
  only side p's harvesters. Our indices are global, so ours now treats a
  refinery / pad of another side as not found (`World::own_refinery`,
  the pad check in `World::tick_repair`) and counts only own harvesters;
  before, a deviated harvester drove back to the enemy's refinery. The
  exe's slot of the same number on the new side is a refinery only by
  chance, which we do not model. Test `tests/sim/test_combat.cpp`.
- **Carryall request (+0x29).** The request slot names the **old**
  object (`+0x26612`); the request monitor `FUN_0046d180` frees it as
  soon as that object is not in state 0x12 / 0x13 (or is dying). The
  copy (state 7) is not in a carry state, so nothing reads its stale
  byte. Ours: the same monitor port (doc 33).
- **Revert timing:** read from the code (§3): 0xF0 steps of 8 ticks
  (1,920 ticks, 96 s at 20 ticks/s) unhit, −30 steps per hit; no
  reference check is needed for the logic.

## 8. Checklist against our code

Historical (the plan before the port); every row is done now (parity row
"Palace specials, Deviator"; the defend-area weight of row 14 is in
`World` ai_groups.cpp, doc 53). The Status column is the state at writing.

| # | Finding | Our code | Status |
|---|---|---|---|
| 1 | Unit record 14 / bullet 16 values (reload 160, no turret, muzzle 4, speed 9, dmg 500, flags 0x50, S_BIGROCKET, EX_DEVIATE / EX_GREENEXP, W_DEVIATE, range 160) | `combat.cpp` `unit_fire` / `add_bullet` / `tick_bullets` (data-driven) | done (generic) |
| 2 | `deviate` argument = bullet flag 0x40 at the 0x1000 detonation, homing and normal impacts; 0 for sonic | `combat.cpp:366`, `:428`, `:498` call `damage_at` without it; `:377` sonic | **missing** (add `bool deviate` to `damage_at`, `world.hpp:277`) |
| 3 | Survive branch: flag 0x2000000 → clear it instead of the HARVATTACK voice | `combat.cpp` `damage_at` (voice always) | **missing** |
| 4 | Survive branch: smoke timer `+0x27 = (rand() & 0x7F) + 0x40` when hp < max/2, not worms; buildings 2 rands (doc 18 §2.2/2.3) | not implemented (no smoke field) | **missing** (pre-existing; changes the rand() order of every hit) |
| 5 | Deviate branch (§2): clear flag 1; own / infantry / building cell → `+0x2A = 1`; else `+0x2D` if not 0x1000, `convert_unit(p, attacker, id, false)`, copy `+0x2E = 0xF0`, flag 0x1000; worms skipped | `combat.cpp:533` "TODO: deviator" | **missing** |
| 6 | Re-fetch `Unit&` after `convert_unit`; the copy hit again when attacker > p (§2.1) | `damage_at` holds `Unit& u` across the loop body | **missing** (needed with #5) |
| 7 | `FUN_0049d280` on-building-cell test (current cell 0x10, or the to-cell when 0x10000000) | no helper | **missing** |
| 8 | Unit fields `+0x2D` original side and `+0x2E` timer (aliasing `delivery_entry`, §3.1); copied by `convert_unit` | `world.hpp:84` `delivery_entry` only | **missing** (add `orig_side`; reuse the +0x2E byte) |
| 9 | Unit update: hit → timer −30 (min 0) if `FUN_0049fb40`; slot → timer −1 or revert (flags 0x1000 → 0x80, `convert_unit(side, +0x2D, id, false)` if different), not carried, not on a building cell, not 0x400000 | `world.cpp:827`..`832` (hit_age only) | **missing** |
| 10 | `FUN_0049fb40`: flag 0x1000 and `has_buildings_[+0x2D] or has_units_[+0x2D]` | `world.hpp:664` flags exist | **missing** (helper) |
| 11 | Death of a deviated unit: loss counters to `+0x2D` | `combat.cpp:643` `kill_unit` counts `u.owner` | **missing** |
| 12 | ConvertUnit field resets (selected, group, target, attacker, `+0x2A`, next state 2, dest, state 7, 0x2000000, old unit state 0x11 with no loss) | `capture.cpp:342` `convert_unit` | done |
| 13 | AI GroupNewUnits flags 0x80 / 0x1000 → own Rampaging group (gTimer 2, gStrength, in use, sub 4, age 0, target none), clear 0x80, kind 7 `rand() & 1` → self-destruct order with `+0x19` toggled | `ai_groups.cpp:555` "TODO: flags 0x80 / 0x1000" | **missing** (the self-destruct order itself is doc 37 #4/#7) |
| 14 | Defend-area weight (kind 4 → 6, 3 → 4, 0x10 → 6, else 2) | defend-area scan not ported (`ai_groups.cpp:883` TODO) | **missing** (not deviator-specific) |
| 15 | No voice / message / special draw for deviated units | renderer draws by owner | done (nothing to add) |
| 16 | Saved state: `orig_side` and the +0x2E byte per unit, unit flags 0x80 / 0x1000 | `save_state.cpp:128` saves `delivery_entry`; `kStateVersion = 12` | **missing** (bump to 13 when #8 lands) |
| 17 | Test: Deviator hits an enemy tank → converted with hp − dmg, `+0x2E = 0xF0`, reverts after 241 slots; hit in between −30; infantry / own / docked harvester not converted; dies → loss to the original side; AI rampage group | `tests/sim/test_world.cpp` | **missing** |
