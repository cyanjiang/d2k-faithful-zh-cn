# 37: Devastator, Thumper deploy and Sardaukar (1.06)

Written for a line-by-line port. Conventions as in doc 30: `u+…` are unit
fields (doc 09 §2), `urec+…` the unit record (`0x77e250 + rec · 0x100`,
special kind at +0x2D), `player+…` the struct from `FUN_004a4660(side)`.
`rand()` is the shared MSVC LCG (`thunk_FUN_004b1810`); "line" numbers are
the debug line arguments pushed with each call. "Verified" = read in the
decompile and checked in the disassembly (`re/disasm.py`) where it
matters. Data values are from the 1.06 `Templates.bin`, `ARMOUR.BIN`,
`VARS.BIN` and `samples.uib`.

## 0. Function and data map

| Address | Name / meaning |
|---|---|
| `FUN_004433a0` | in-game mouse / keys: deploy cursor (`0x4435d2`), deploy click (`0x445789`, `0x4457c1`) |
| `FUN_0045a3f0(side, idx)` | posts order **0x2F** (deploy / special) for one unit (doc 30 §1.3) |
| `FUN_00457810` case 0x1C (`0x458990`..) | order 0x2F handler: kind 7 → state 0x1F, kind 0xE → state 0x20 |
| `FUN_00497b90` | unit update; switch on state − 2: case 0x1D = **state 0x1F** (`0x49b32d`..`0x49b45a`), case 0x1E = **state 0x20** (`0x49b45f`..), case 0x1F = state 0x21 |
| `0x497c3b` | unit update: **self-repair** of record flag 0x800000 (§1.5) |
| `FUN_0049deb0` | SetState (state 0x1F is sticky, §1.3) |
| `FUN_0049cde0` | KillUnit: SARDDEATH area hit for kinds 0xB and 4 (`0x49d11e`) |
| `FUN_0049d2e0` | damage (doc 18 §2) |
| `FUN_00456df0` | create an explosion (doc 14 §4.1) |
| `FUN_0041c3e0` | AI GroupNewUnits: the only AI use of the self-destruct (`0x41c6f5`) |
| `FUN_004544e0` | `VARS.BIN` loader; **DevastatorExplodeDelay** → `0x6b885b` |
| `FUN_00466140` | data set-up; debug text loader sets the special indices below |

Special indices (Templates.bin block `0x6d60d8`, 1.06 values; the names
are the strings the debug loader compares, verified at `0x4676a9`..
`0x467786` and `0x467b9b`):

| Global | Value | Name |
|---|---:|---|
| `0x6d60f0` | 28 | bullet SARDDEATH |
| `0x6d610b` | 18 | bullet DEVASTATOR (the self-destruct "bullet") |
| `0x6d6127` | 27 | explosion EX_THPUFF |
| `0x6d6129` / `612a` / `612b` | 50 / 51 / 52 | EX_DEVSPARK1 / 2 / 3 |
| `0x6d612c` | 38 | EX_DEVDEATH |

## 1. Devastator (unit record 17, kind 7, Harkonnen)

### 1.1 Record (Templates.bin, verified)

| Field | Value |
|---|---|
| hp (+0x04) | 5000 |
| speed (+0x08) | 48 (same as MCV / Sardaukar / Sonic tank) |
| armour (+0x02) | 5 HEAVY |
| body turn delay (+0x03) | 2 |
| reload (+0x0E) | 75 ticks |
| turret (+0x26) | 0: **no turret**, the body turns to aim |
| crusher (+0x35) | 1 |
| muzzle offset (+0x37) | 7 px |
| muzzle flash (+0xA8) | 45 MUZ_DEVAS (blink mask 0x0F, doc 14 §4.2) |
| weapons (+0x0C / +0x0D) | 17 DEVBULLET / none |
| death explosion (+0x2F) | 10 EX_BANG14 |
| record flags (+0x38) | **0x800000** = self-repair (§1.5) |

There is no special movement code for kind 7 (no kind-7 test in the
movement or pathing code; `0x77e27d` xref scan below). "Slow" is just
speed 48.

### 1.2 Weapon (verified)

`FUN_00495b90` (doc 18 §3.1) has **no kind test**: the Devastator fires
**one** DEVBULLET per reload like every other unit (no double shot).
Bullet records:

| Bullet | speed | damage | warhead | range | flags | fire sound | impact |
|---|---:|---:|---|---:|---|---|---|
| 17 DEVBULLET | 600 << 10 (9.375 px/tick) | 650 | 10 W_PLASMA (radius 32) | 128 px | 0x1200 | 5 S_110MMGUN | 37 EX_DEVASTATOR (sound 24 S_LARGEEXP2) |
| 18 DEVASTATOR | 600 << 10 | 5000 | 11 W_PLASMA_DEATH (radius 96) | 96 px | 0x1200 | 5 | 37 |

Flags 0x1200 = **0x200 additive blit** and **0x1000 detonate over a cell
with flag 0x400** (walls and turrets, doc 18 §4.2 step 4): a plasma ball
that flies over a wall or turret cell explodes there. No arc (no flag 1),
not homing, not anti-air. Byte +0x13 = 5 (doc 18 open point). Bullet 18
is never fired; only its numbers are used by the self-destruct (§1.4).
W_PLASMA percentages: NONE 50, WALL 100, BLDG 75, WOOD 60, LIGHT 100,
HEAVY 100, CONCRETE 100, INV 0, CY 40, HARV 100.

### 1.3 Player input: the self-destruct order (verified)

There is **no key**: D in single player does nothing (doc 25 §A.7, it is a
WOL-only order), and `FUN_0045a3f0` has exactly six callers (four in the
input code, `FUN_0041c3e0`, `FUN_00422140`). The player triggers it with
the deploy click:

- **Cursor** (`0x4435d2`): mouse over an **own selected** unit, Shift not
  held (`0x797e88 == 0`): kind 7 → cursor **0xC** (green four-way
  arrows, "deploy") always, in every state, including while it is already
  counting down. (Doc 10 §5 already lists it.)
- **Left click** on an own unit that is already selected (`+0x19`), Shift
  not held (`0x445789`):
  ```
  if kind == 7:
      if u.state != 0x1F:
          FUN_0045a3f0(owner, idx)        // order 0x2F, this one unit only
          FUN_0049f240(0)                 // the selection voice (not the ack)
      // else: nothing (no voice, no reselect)
  ```
- Order 0x2F handler (`0x4589a1`..`0x458a19`):
  ```
  if u.+0x90 != 1 or !CanTakeOrder(u, 1): return    // FUN_0049f4a0
  if kind == 7 and SetState(u, 0x1F):
      u.+0x14 = 1                                    // follow-up state
      u.+0x29 = DevastatorExplodeDelay              // byte 0x6b885b
  ```
  `0x6b885b` = VARS.BIN byte **+0x43 = 100** in 1.06 (name table
  `0x4e2730` entry 22 "DevastatorExplodeDelay"; the text default in
  `FUN_004544e0` is not used, doc 13 §7).
- **SetState makes 0x1F sticky** (`FUN_0049deb0`): from 0x1F only 0x11 is
  accepted. Stop (S), scatter (X), move, attack, guard, retreat: all
  refused. **The countdown cannot be cancelled.** (SetState(u, 0x1F) when
  already 0x1F returns 1, which would reload the countdown, but the click
  path tests the state first and the AI posts it once, §1.6.)
- Kind 7 is in SetState's "any state" kind list (1, 3, 4, 5, 6, 7, 0xB,
  0xC, 0xD, 0xE, 0x10), so 0x1F is allowed for it.

### 1.4 State 0x1F: the countdown and the explosion (`0x49b32d`..`0x49b45a`, verified)

```
if u.+0x4f != 0:                         // mid-step: finish the step first
    u.+0x48, u.+0x49 = u.+0x50, u.+0x51
    MoveStep(u, side, idx)               // FUN_00494260
    return                               // no sparks, no countdown
r = rand()                               // line 0x182a
if r % 3 == 0:
    k = rand() % 3                       // line 0x182b
    FUN_00456df0(side, EX_DEVSPARK1 + k  /* 0x6d6129[k] */,
                 u.x >> 16, u.y >> 16, 0, 0, 0, 0, 0)     // no sound
if u.+0x29 != 0:
    u.+0x29 -= 1
    return
// detonate
SetState(u, 0x11)                        // hp is NOT set to 0 here
FUN_00456df0(side, EX_DEVDEATH, u.x >> 16, u.y >> 16, 0, 0, 0, 0, 0)
player(side).+0x24a08 += 1               // units lost
player(side).+0x24a10[u.record] += 1     // lost per record
FUN_0049d2e0(u.x >> 16, u.y >> 16, height = 6, bullet DEVASTATOR (18),
             attacker side = side, firer = idx, deviate = 0)
```

Consequences (all from the code above and doc 18 §2):

- **Timing:** with the 1.06 delay of 100, the unit detonates in the
  **101st** state-0x1F update that is not mid-step (100 decrements, then
  the update that sees 0). One or two `rand()` calls per update before
  that, in that order.
- It does **not** fire, turn or move during the countdown (the state
  code does nothing else); a step in progress is finished first.
- **Damage:** 5000 · pct / 100 with W_PLASMA_DEATH (NONE 100, WALL 100,
  BLDG 100, WOOD 60, LIGHT 100, HEAVY 100, CONCRETE 100, INV 0, CY 40,
  HARV 100), **radius 96 px** (3 cells), linear fall-off from the unit's
  pixel. Height 6 is a plain integer (< 0x60000), so it is a **ground**
  hit: aircraft are not hit. **No friendly-fire check**: own and allied
  units and buildings are hit too. The Devastator itself is skipped
  because it is already in state 0x11. A ripe spice bloom at the impact
  cell would absorb the whole hit (doc 18 §2 first branch). Craters as
  doc 18 §2.4.
- Kills made by the blast are credited to the Devastator's side with
  firer = the Devastator's index (doc 18 §2.2).
- **Not a KillUnit:** no EX_BANG14, no white flash, no `*_UNITLOST`
  voice, no killer credit for anyone; but the loss counters are bumped.
- **No sound** is played by this code: neither the sparks nor
  EX_DEVDEATH call `FUN_0046f540`, and explosions never play their own
  sample (doc 14 §4.2). Verified: the case calls only rand(),
  `FUN_00456df0` (AddExplosion, which calls no sound function), SetState
  and the damage function (`0x49b32d..0x49b452`); the damage function `FUN_0049d2e0` and its callees create no explosion and play no sample of their own (callees verified: the only sound / explosion calls are the `*_UNDERATTACK`, `*_HARVATTACK` (needs a firer, so never with 0xFFFF), `*_BUILDINGLOST` and `*_UNITLOST` voices, the bloom burst `FUN_0044ef10`, and the deaths of what it kills: KillUnit / `FUN_0049ca10`). So the
  sounds heard are the victims' deaths and the house voices.
- If the Devastator is **killed during the countdown** (state 0x1F is not
  skipped by the damage loop), the normal KillUnit runs: EX_BANG14 and
  the flash, **no** area blast.
- hp stays as it was; SetState(0x11) deselects (`+0x19 = 0`). The removal
  countdown is as for any state-0x11 unit (doc 14 §3.3): +0x2F still
  holds CreateUnit's 10 (`0x4559ba`), its only other writer being the
  state-0x11 decrement (`0x49a09c`; doc 18 §2.2.1).

### 1.5 Self-repair, record flag 0x800000 (`0x497c3b`, verified)

Not Devastator-only, but it is one of the users. Record +0x38 is copied
to the unit flags at creation (`FUN_00455870`, `puVar7[2] = urec+0x38`).
Every unit update, after the infantry animation tick and before the
saboteur block:

```
if (u.flags & 0x800000) and u.hp · 2 < urec.hp and (tick 0x5173f4 & 3) == 0:
    u.hp += 1
```

So these units **regenerate 1 hp every 4 ticks up to half of their
maximum** (5 hp/s at 20 ticks/s). 1.06 records with 0x800000: HARVESTER
(8), MCV (12), DEVASTATOR (17), both CARRYALL (18, 19). No other unit
record has flags except SIEGE_TANK (0x8000). Doc 08's "0x800000 seen" is
this. **Not in our code** (no reader of `initial_flags & 0x800000`).

### 1.6 Computer player (verified)

The only AI use is in **GroupNewUnits** (`FUN_0041c3e0`, doc 23 §3.4),
for an ungrouped unit with **flag 0x80 or 0x1000** (deviation ended /
deviated, doc 32 §4.3):

```
g = FindFirstFreeGroup                 // FUN_0041c3a0; none → skip
u.+0x28 = g; group g: job 0xE (Rampaging), +0x34 = 2, timer FUN_0041c340,
    in use, substate 4, +0x44 = 0, target 0xFFFF
u.flags &= ~0x80
if kind == 7 and (rand() & 1):         // line 0x53c; rand() only for kind 7
    u.+0x19 = 1
    FUN_0045a3f0(side, u.idx)          // self-destruct order
    u.+0x19 = 0
```

So the computer self-destructs **only a Devastator that it got by
deviation (flag 0x1000) or got back when the deviation ended (flag
0x80)**, with probability 1/2, and then only when GroupNewUnits sees it
ungrouped and not busy (state 8 / 0x14 / 9 or mid-step count as busy and
are skipped before this test). It never self-destructs its own
factory-built Devastators. No other AI routine reads kind 7 (xref scan,
§4).

### 1.7 Normal death (verified)

KillUnit (`FUN_0049cde0`) has no kind-7 case. A destroyed Devastator gets
the generic vehicle death: white flash (`0x6d6112` = 12 EX_WHITEN1), the
EX_BANG14 death explosion with its sound (19 S_SMALLEXP4), `*_UNITLOST`,
state 0x11. **No area damage** (only kinds 0xB and 4 get one).

## 2. Thumper (unit record 3, kind 0xE, infantry)

Shares the order path. Record: infantry, hp 375, speed 64, no weapon,
view 1, tech 6 (skirmish only, doc 17 §5.5).

### 2.1 Input (verified, `0x4457c1`)

- Cursor: own selected thumper under the mouse, no Shift → **0xC** if the
  **mouse cell** has flag 0x10000 (doc 10: sand), else **0xD**.
- Click on the selected thumper: if the **mouse cell** (not the
  thumper's own cell; same quirk as the MCV, doc 30 §1.2) has 0x10000 →
  `FUN_0045a3f0` then `FUN_0049f240(0)`; else `FUN_004a5a50(owner, idx, 0)`
  (exclusive reselect) then `FUN_0049f240(0)`. No "can't deploy" voice.
- Order 0x2F: `SetState(u, 0x20)`, `u.+0x14 = 1`. State 0x20 is **not**
  sticky: any later order ends the thumping.

### 2.2 State 0x20 (`0x49b45f`.., verified)

```
if u.+0x4f != 0: finish the step (as §1.4); return
if TurnBody(u, 8): return              // FUN_00493eb0: faces direction 8 first
if !urec.infantry: error "Invalid type for thumper"
SetInfAnim(u, 8)                       // FUN_00494050, mode 8 = frames 7..11
first = 0x4e9530[u.+0x20].first        // 7
if u.+0x1e == first + 1:               // frame 8: hold
    u.+0x29 += 1
    if u.+0x29 < 60: u.+0x25 = 0       // keeps the 3-tick frame counter at 0
    else: u.+0x29 = 0; u.+0x25 = 3    // let the frame advance
if u.+0x1e == first:                   // frame 7: the thump
    FUN_00456df0(side, EX_THPUFF, u.x >> 16, u.y >> 16, 0, 0, 0, 0, 0)
    if pixel inside the view (0x798538 .. +0x786d48, 0x79853c .. +0x6dc538)
       and (0x4dfb04 == 0 or the cell is not shrouded (0x517df8 byte == 0))
       and !FUN_0046f4f0(sample of EX_THPUFF = 12 S_THUMPER):   // not already playing
        FUN_0046f540(S_THUMPER, cell x, cell y)
```

So one thump (puff + sound) per animation cycle, with a 60-update hold on
frame 8. The exact period (72 ticks) follows from the infantry
animation tick (`FUN_004940d0`, which runs earlier in the same update at
`0x497c33`) leaving mode 8 after frame 11 and SetInfAnim clamping the
frame back to 7: doc 54 §4.4 has the tick-by-tick table and the test. Worm attraction is doc 17 §5.5 (kind 0xE in state 0x20
on sand, `FUN_0044d500`). Thumpers are excluded from the attack-building
order (`FUN_00459800` skips kinds 1, 6, 0xE).

State 0x21 (switch case 0x1F) counts `+0x29` down and then
SetState(2). **State 0x21 is never entered in 1.06** (dead code): a scan
of all 110 SetState calls (`0x49deb0`) finds no immediate 0x21; the 7
calls with a register argument pass a saved state or the follow-up
+0x14 (`0x46d251`, `0x46d2a8`, `0x498856`, `0x498f63`, `0x499278`,
`0x4992f0`, `0x49cf35`), and no instruction in the exe writes the
immediate 0x21 to a unit's +0x10 or +0x14 (the four 0x21 immediates are
order types `0x459602` / `0x4596b2`, a DirectDraw flag `0x4a41e8` and
the GRENDEATH push `0x499e1b`).

## 3. Sardaukar (kind 0xB)

### 3.1 Records (verified)

Two records have kind 11, both named SARDAUKAR:

| Rec | Houses | hp | speed | cost | build | tech | needs | reload (+0x0E) |
|---:|---|---:|---:|---:|---:|---:|---|---:|
| 4 | Order 0x2F kind 7: SetState(0x1F), next 1, countdown = DevastatorExplodeDelay | `capture.cpp` `order_deploy` case 7 | done (2026-10-11) |
| 29 | H (Harkonnen) | 1000 | 48 | 200 | 70 | 6 | BARRACKS, HIGH_TECH_FACTORY | 50 |

Both: infantry, armour 0 NONE, view 3, muzzle offset 2, no muzzle flash,
no death explosion (+0x2F = 0xFF), record flags 0, not crushers, **two
weapons**: primary 30 **M_LMG**, secondary 31 **M_HMG**.

| Bullet | damage | warhead | range | fire sound | impact |
|---|---:|---|---:|---|---|
| 30 M_LMG | 125 | 5 W_BULLET | 80 px | 0 S_LMG | EX_MG |
| 31 M_HMG | 250 | 7 W_GT_AT | 112 px | 3 S_HMG | EX_MG |

Neither is anti-air (the Sardaukar cannot shoot aircraft).

### 3.2 Weapon choice (verified, generic)

There is no kind-0xB code in firing. `NewBestBullet` (`FUN_0049e9d0`,
doc 18 §3.2) picks per shot. Here range(prim) 80 < range(sec) 112, so:
**M_LMG if dist² ≤ 80² and eff(LMG) > eff(HMG), else M_HMG**, with
eff = pct[warhead][target armour] · damage (armour 0 for ground fire):

| Target armour | eff LMG | eff HMG | Choice within 80 px |
|---|---:|---:|---|
| NONE (infantry) | 12500 | 6250 | M_LMG |
| LIGHT | 5000 | 25000 | M_HMG |
| HEAVY | 2500 | 12500 | M_HMG |
| BUILDING | 3125 | 12500 | M_HMG |
| WOOD | 9375 | 16250 | M_HMG |
| WALL | 1250 | 25000 | M_HMG |

Beyond 80 px it always uses M_HMG. Target search / auto-fire range is
the larger range (112) as for every two-weapon unit (our
`combat.cpp` already uses max(range1, range2)). Reload is the record's
(40 or 50) whatever weapon fired.

### 3.3 Death: SARDDEATH (`0x49d11e`, verified)

At the very end of KillUnit, after hp = 0 and SetState(u, 0x11):

```
if (kind == 0xB or kind == 4) and 0x6d60f0 != 0xFF:
    FUN_0049d2e0(u.+0x4a << 5, u.+0x4b << 5,       // top-left pixel of its cell
                 height 0, bullet SARDDEATH (28),
                 attacker side = the dead unit's side, firer 0xFFFF, 0)
```

SARDDEATH: damage 300, warhead 8 **W_AT_MIS** (NONE 15, WALL 75, BLDG
60, WOOD 65, LIGHT 90, HEAVY 100, CONCRETE 100, INV 0, CY 30, HARV 50),
**radius 32 px**, measured from the cell's **top-left corner** (so a
unit in the middle of that cell is ≈ 22.6 px away and takes ≈ 29 %).

- It runs for **every** KillUnit of a Sardaukar: shot, crushed
  (`FUN_0049f8f0`), the whole side destroyed (`FUN_0046e6b0`), killed as
  cargo of a shot-down carryall (the recursion at the top of KillUnit).
  It does not run when a worm eats it (the worm does not call KillUnit,
  doc 17 §3.2).
- No friendly-fire check: neighbouring own infantry are hit (15 % of
  300 at full strength). A neighbouring Sardaukar killed by it runs its
  own SARDDEATH: **chain reactions are possible** (recursion through
  `FUN_0049d2e0` → KillUnit).
- The damage function spawns no explosion: the bullet's impact EX_TANKGUN
  is **not** shown, and no sound is played (verified 2026-10-16: the damage function `FUN_0049d2e0` and its callees create no explosion and play no sample of their own (callees verified: the only sound / explosion calls are the `*_UNDERATTACK`, `*_HARVATTACK` (needs a firer, so never with 0xFFFF), `*_BUILDINGLOST` and `*_UNITLOST` voices, the bloom burst `FUN_0044ef10`, and the deaths of what it kills: KillUnit / `FUN_0049ca10`)).
- rand() order in KillUnit for a Sardaukar: the death-mode rand (line
  0x1bd2, only if the mode is not already 9..13), `S_MANDYING` rand (line
  0x1bfb, not in state 0x17), then the rands of the SARDDEATH damage.
- Kills made by it are credited to the dead Sardaukar's side (attacker
  side = its own side, firer 0xFFFF).

### 3.4 Nothing else

Every read of the kind byte was checked (Ghidra xrefs to `0x77e27d`,
120 sites, plus a disassembly scan for `+0x2d` reads followed by
`cmp …, 0xB`): **kind 0xB is tested only in KillUnit** and appears in
SetState's "any state" kind list (harmless: no order leads a Sardaukar
into the extra states). No immunity (no warhead or worm exception), no
special cursor, no AI routine (DoSpecialWeapons `FUN_00422730` asks for
kinds 0xA, 0xC; GroupNewUnits counts kinds 4 and 0xC), no voice code,
not crushed differently. The name "SARDAUKAR" (`0x4e4b8c`) is used only
by the record text parser `FUN_00463990`.

### 3.5 Grenadier death: GRENDEATH (`0x499de2`, verified 2026-10-16)

Not in KillUnit: the blast comes from the **state 0x11 update** (unit
update `FUN_00497b90`, switch case 0xF), every update of a unit in state
0x11, before anything else of that state:

```
0x499de2: if u.+0x2f == 0x2E
          and strcmpi(0x7891b0 + u.record * 0x1C2, "Grenadier") == 0:   // 0x4e9ce0
              FUN_0049d2e0(u.+0x4a << 5, u.+0x4b << 5,   // top-left pixel of its cell
                           height 0, bullet 0x21,        // immediate push 0x21, no special index
                           attacker side = side (EBX), firer 0xFFFF, deviate 0)
0x499e31: if u.+0x2f == 0:  free the cells, return 0  → removed (doc 14 §3.3)
0x49a09a: u.+0x2f -= 1      // after the two tests
          aircraft: flight step (FUN_00494fd0 / FUN_00494cd0 for 0x20000)
```

- The name is the record's **art name** (Templates block `0x7891b0`,
  0x1C2 bytes per record, doc 08), compared without case. In 1.06 only
  record 27 has it ("GRENADIER"); the dump tool lists the others.
- **When:** +0x2F is set once at creation (`FUN_00455870`: 0x32 for
  infantry, 10 for vehicles) and decremented in state 0x11 (no other
  writer: a scan of every write with displacement 0x2F finds on unit
  objects only `0x4559b4` / `0x4559ba` (CreateUnit) and `0x49a09c` (the
  decrement); doc 18 §2.2.1), so a Grenadier enters
  state 0x11 with 0x32. The test sees 0x32, 0x31, 0x30,
  0x2F, 0x2E: the blast is in the **5th** state-0x11 update (4
  decrements after the death), once.
- **Which deaths:** every way into state 0x11 with the counter at 0x32:
  KillUnit from damage (shot, a neighbour's GRENDEATH / SARDDEATH, a
  Devastator blast), crushed (`FUN_0049f8f0`), the side destroyed
  (`FUN_0046e6b0`), cargo of a shot-down carryall. Infantry cannot be
  eaten by a worm (doc 17 §3.2). Since the code tests the art name, not
  the kind, nothing else in the death path matters; the Grenadier still
  gets the normal infantry death (death animation, `S_MANDYING`).
- **Bullet 33 GRENDEATH** (Templates.bin, 1.06): damage **150**, warhead
  13 **W_HE** (NONE 125, WALL 100, BLDG 100, WOOD 70, LIGHT 30, HEAVY 20,
  CONCRETE 100, INV 0, CY 20, HARV 25), **radius 64 px**, ground (height
  0: aircraft are not hit), measured from the cell's **top-left
  corner**. Only these numbers are used; the bullet is never fired.
- **No friendly-fire check**, own and allied units and buildings are hit;
  the dead Grenadier is skipped (state 0x11). A second Grenadier killed
  by it blasts in its own 5th state-0x11 update: **chain reactions**. If
  it comes later in the unit list, its first such update is in the same
  tick, so the next blast is 4 ticks later.
- Kills are credited to the dead Grenadier's side, firer 0xFFFF (doc 18
  §2.2), as SARDDEATH.
- **No explosion, no sound**: the damage function draws nothing and plays
  nothing (doc 18 §2; the bullet's impact explosion 8 and sound 16 are
  not used). What the player sees is the death of whatever it kills.
  Verified statically 2026-10-16: the damage function `FUN_0049d2e0` and its callees create no explosion and play no sample of their own (callees verified: the only sound / explosion calls are the `*_UNDERATTACK`, `*_HARVATTACK` (needs a firer, so never with 0xFFFF), `*_BUILDINGLOST` and `*_UNITLOST` voices, the bloom burst `FUN_0044ef10`, and the deaths of what it kills: KillUnit / `FUN_0049ca10`).
- The rand() calls are those of the damage (smoke timers, deaths of the
  hit units), in that update, in the unit list order.

Order of the removal counter (same code): the zero test comes **before**
the decrement, so a unit stays **0x2F + 1** updates in state 0x11 (51
for infantry, 11 for vehicles and aircraft), and an aircraft makes one
flight step per non-final update (doc 14 §3.3).

## 4. Kind-test inventory (how "nothing else" was checked)

Sites reading `urec+0x2D` (`0x77e27d`) that compare with 7, 0xB or 0xE:
`0x4435d2` (cursor 6 / 7 / 0xE / 4), `0x445789` (click, kind 7),
`0x4457c1` (click, kind 0xE), `0x41c6f5` (AI, kind 7), `0x44d592`
(nearest thumper, kind 0xE + state 0x20), `0x459879` (attack-building
order skips 0xE), `0x49d124` (KillUnit, 0xB), `0x4589c7` chain (order
0x2F: 6 / 7 / 0xE / 4) and the SetState jump table (`0x49debf`).

## 5. Checklist against our code

Historical (the plan before the port); every row is done now (parity
rows "Devastator, Sardaukar, self-repair", "Thumper", "Palace specials,
Deviator"). The Status column is the state at writing, except rows 7
and 16, updated 2026-10-16.

| # | Finding | Our code | Status |
|---|---|---|---|
| 1 | DEVBULLET record values, single shot, MUZ_DEVAS flash, 7 px muzzle | `combat.cpp` `unit_fire` (generic) | done (data-driven) |
| 2 | Bullet flag 0x1000: detonate over a wall / turret cell (DEVBULLET, DEVASTATOR) | `combat.cpp:349` "TODO detonation over walls (0x1000)" | **missing** |
| 3 | Bullet flag 0x200 additive blit | `game_screen.cpp:769` | done |
| 4 | Order 0x2F kind 7: SetState(0x1F), next 1, countdown = DevastatorExplodeDelay | `capture.cpp` `order_deploy` case 7 | done (2026-10-11) |
| 5 | VARS.BIN +0x43 DevastatorExplodeDelay (100) | `Vars::devastator_explode_delay`, `set_vars` | done |
| 6 | State 0x1F sticky in SetState | `world.cpp:308` | done |
| 7 | State 0x1F update: finish step, sparks (2 rands), countdown, EX_DEVDEATH, loss counters, damage(height 6, bullet 18, firer = idx), no hp = 0, no sound | `capture.cpp` `tick_self_destruct` | done (golden test: 101 ticks, own and enemy units in 96 px die); the per-record losses (doc 60) and the +0x2F countdown (CreateUnit's 10, §1.4) are done |
| 8 | Cursor 0xC for own selected kind 7; 0xC/0xD for kind 0xE by mouse cell 0x10000 | `game_screen.cpp` cursor | kind 7 done; kind 0xE **missing** (kind 4 is doc 32) |
| 9 | Click on selected Devastator (state ≠ 0x1F) → order + select voice; thumper click by mouse cell | `game_screen.cpp` click | Devastator done; thumper **missing** |
| 10 | AI: GroupNewUnits flags 0x80 / 0x1000 → own Rampaging group, clear 0x80, kind 7 `rand() & 1` → deploy order | `ai_groups.cpp:480` "TODO: flags 0x80 / 0x1000" | **missing** (needs the deviator, `combat.cpp:516` TODO) |
| 11 | Self-repair: flag 0x800000, hp·2 < max, tick & 3 == 0 → hp + 1 (harvester, MCV, Devastator, carryalls) | not implemented | **missing** (affects campaign units now) |
| 12 | Devastator normal death = generic (no area hit) | `combat.cpp` `kill_unit` | done |
| 13 | Order 0x2F kind 0xE: SetState(0x20), next 1 | `capture.cpp` default "TODO: 0xE" | **missing** |
| 14 | State 0x20: turn to 8, anim mode 8, hold 60 on frame 8, EX_THPUFF + S_THUMPER (view / shroud / not-playing test) | `world.cpp` (none); worm side done in `worm.cpp` `nearest_thumper` | **missing** (skirmish only) |
| 15 | Sardaukar two weapons, NewBestBullet choice | `combat.cpp` `choose_weapon`, `weapon2` ranges | done (check M_LMG / M_HMG table §3.2 in a test) |
| 16 | SARDDEATH at the cell's top-left pixel, height 0, own side, firer none; for kinds 0xB and 4 | `combat.cpp:655` | done, with firer 0xFFFF (−1) and the rand order death-mode, S_MANDYING, damage (`World::kill_unit`) |
| 17 | Sardaukar not eaten-with-SARDDEATH (worm path skips KillUnit) | `worm.cpp` | check that the worm path does not call `kill_unit` |
| 18 | Grenadier GRENDEATH in the 5th state-0x11 update (§3.5) | `world.cpp` state 0x11 block | done (2026-10-16, golden test) |
| 19 | Thumper excluded from attack-building order; cursor rules | `combat.cpp:1020` comment | check |

Suggested tests: a Devastator self-destruct in a seeded map (detonation
tick = order tick + 101 updates, damage on a ring of units at 0 / 48 /
95 / 96 px, own units hit, aircraft not); a Sardaukar killed next to a
second, wounded Sardaukar (chain); self-repair of a harvester from 40 %
to 50 % (1 hp per 4 ticks, stops at max/2).
