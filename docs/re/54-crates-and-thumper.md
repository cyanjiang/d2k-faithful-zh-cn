# 54. Crates and the Thumper (1.06)

Target: Dune 2000 1.06 US (`DUNE2000.DAT`). Read from the disassembly and
decompilation of the functions named. "Verified" means checked against the
instructions. Data values come from the player's `Templates.bin`,
`VARS.BIN`, `samples.uib` and `Skirmish.uil`. No open guesses remain
(closed 2026-10-16).

Builds on doc 31 (the cell-object table that crates share with spice
blooms), doc 16 §6.2 (map crates), doc 32 §1.8 (stealth crate), doc 17
§5.5 (worms and thumpers) and doc 37 §2 (thumper input and state). This
doc fills in the rest and corrects some points (§7).

Both systems are **skirmish / multiplayer only** in practice. Crates are
off in the campaign (`max_crates = 0`), and the thumper is not offered
in the campaign.

## 0. Functions and globals

| Address | Role |
|---|---|
| `FUN_0044ea70` | per-tick cell-object update; adds one crate when there are too few (doc 31 §3) |
| `FUN_0044e7c0` | AddRandomCrate: `FUN_0044e800`, then `FUN_0044e8e0(x, y, 5000, type, 0, 0)` |
| `FUN_0044e800` | PickCrateCellAndType (§1.2) |
| `FUN_0044e9d0` | take the object at (x, y): crates are removed, the type is returned (doc 31 §6.2) |
| `FUN_0049ebf0` | CratePickup(unit, side, index), called from MoveStep (§2) |
| `FUN_0049f1b0` | stealth crate effect (doc 32 §1.8) |
| `FUN_0041f670` | AI DoStopStartStuff: a crate scan that removes crates (doc 31 §6.3) |
| `FUN_00448550` | game set-up: sets `max_crates` (§1.1) |
| `FUN_0046c570` | MyVersionOfUnit(player, name, excludeDisallowed): name → this house's record |
| `FUN_00455870` | CreateUnit (doc 12 §8.3) |
| `FUN_0046c7b0` | add to pending cash (`player+0x24260`, doc 11 §8) |
| `FUN_0049d2e0` | damage at a point (doc 18 §2) |
| `FUN_00456df0` | spawn an explosion (doc 14) |
| `FUN_0046f540` | positional sound (doc 12 §9) |
| `FUN_0046f4f0` | "is this sample already playing" (scans 6 voices; always 0 with sound off) |
| `FUN_0045a3f0` | post order 0x2F (deploy / special action, doc 30 §1.3) |
| `FUN_00457810` case 0x1C | order 0x2F handler; kind 0xE → `SetState(u, 0x20)` |
| `FUN_00497b90` case 0x1E (`0x49b45f`) | unit update, state 0x20 (thumping, §4.3) |
| `FUN_004940d0` / `FUN_00494050` | infantry animation tick / SetInfAnim (doc 09 §7, doc 18 §3.8) |
| `FUN_0044d500` | nearest thumping thumper on sand (doc 17 §5.5) |

| Global | Meaning | 1.06 value |
|---|---|---|
| `0x548048` | u8 `max_crates` (saved with the game) | 0 campaign; 1 skirmish with crates on |
| `0x6b9834` | `gNetCrates` (UI variable, `dune2000.cfg`) | 0 / 1 |
| `0x6b9838` | `gNetWorms` (UI variable, `dune2000.cfg`) | 0..3 |
| `0x6b8859` | VARS `NumberOfCrates` (index 20) | 1 |
| `0x6b885a` | VARS `CratesPerPlayer` (index 21) | 0 |
| `0x6b8860` | VARS `CrateCash` (index 24) | 1000 |
| `0x4e0e18` | crate type thresholds, 6 bytes `"2AFK_d"` | 50, 65, 70, 75, 95, 100 |
| `0x6d60ef` | bullet CRATE (set by name in `FUN_00466140`) | 29 |
| `0x6d610d` | EX_CRATEREVEAL | 53 |
| `0x6d610e` | EX_CASH | 36 |
| `0x6d610f` | EX_CRATENOMAP | 54 |
| `0x6d6110` | EX_CRATESTEALTH | 55 |
| `0x6d6112` | EX_WHITEN1 (also the vehicle death flash, doc 37 §1.7) | 12 |
| `0x6d6127` | EX_THPUFF | 27 |
| `0x6d9b50` | unit name count | 26 |
| `0x781e5c + ex·8` | explosion sound (samples.uib index, −1 none) | see §3 |

## 1. Spawning

### 1.1 How many (`FUN_00448550`, verified)

- Campaign (`0x448756`): `max_crates = 0`.
- Skirmish (game type `0x797e34 == 1`) and network (`0x7984c4 != 0`),
  `0x44a814`:

```
if gNetCrates == 0:            max_crates = 0
elif CratesPerPlayer == 0:     max_crates = NumberOfCrates
elif player count (0x7984c0) > 1: max_crates = NumberOfCrates · player count   // 8-bit imul
else:                          max_crates = NumberOfCrates
```

With 1.06 VARS (1, 0) this means **one crate on the map at a time** when
crates are on.

- `gNetCrates` is the skirmish menu checkbox `crateCheck` (Skirmish.uil:
  `CheckIfValueBFInit gNetCrates TRUE`, `SetValueBFTo gNetCrates
  TOGGLE`; the text box shows `CratesOff` / `CratesOn`). It is saved in
  `dune2000.cfg` (`FUN_00478c20` reads 1 byte and forces any value other
  than 0 / 1 to 0). The start-up reset `FUN_00470710` sets it (and
  `gNetWorms`) to 0 and then calls the loader (`0x470a68`), which keeps
  the reset values when there is no `dune2000.cfg` (`FUN_0042ba50`
  returns 0). The 1.06 install ships no `dune2000.cfg` (the game writes
  it, `FUN_00478fa0`), so on a fresh install the skirmish menu shows
  **CratesOff**. Our `settings.ini` default is `net_crates=0`.
- `max_crates` is saved and loaded with the game (`FUN_004418b0` /
  `FUN_004414d0`, 1 byte). The cell-object table is saved too (doc 31).
- The `.MAP` loader makes **no crates**. Only bloom specials 0x29–0x2D
  call `FUN_0044e8e0` (doc 31 §2). So the campaign has no crates at all.

### 1.2 When and where (`FUN_0044ea70` tail, `FUN_0044e800`, verified)

Once per tick, after the cell-object loop (doc 31 §3):

```
crates = active entries with type < 6, counted before their timers run
if crates < max_crates: AddRandomCrate()          // at most one per tick

AddRandomCrate():                                 // FUN_0044e7c0
    do:
        x = rand() % map_w                        // line 0x751
        y = rand() % map_h                        // line 0x752
    while (cell(x, y).flags & 0x73F8) != 0x6000
    r = rand() % 100                              // line 0x755
    type = first i with r < {50, 65, 70, 75, 95, 100}[i]
    AddCellObject(x, y, timer 5000, type, image 0, regrow 0)   // FUN_0044e8e0
```

- The cell test: infantry-walkable (0x4000) and vehicle-walkable (0x2000),
  no cell object (0x1000), no unit and no building (0x3F8). Spice and
  sand are allowed. Row 0 and column 0 are allowed.
- rand() order: x, y per try (any number of tries), then one for the
  type. These come after all bloom rand() calls of the same tick.
- **Table full:** the rand() calls are made, but `FUN_0044e8e0` adds
  nothing when all 30 entries are used (blooms included). Then the crate
  count stays below the maximum and the three-or-more rand() calls repeat
  **every tick**. Port this.
- **No valid cell:** the loop never ends (the game hangs). Real maps
  always have a cell. Our port: cap the tries and log (crash fix).
  Unreachable in 1.06: at the start of a skirmish every one of the 36
  maps in `Data\maps` has at least 1,923 cells passing the test (fewest:
  2PLAY7; test "every skirmish map has free cells for a crate"), and
  buildings and units cover only a small part of them later.
- "Creating illegal crate" (fatal) cannot happen: r < 100.
- **Lifetime 5000 ticks** (250 s at 20 ticks/s). When the timer ends, the
  crate goes silently (doc 31 §3).
- **Respawn:** a crate taken in the unit update of tick T is replaced in
  the cell-object update of tick T + 1. An expired crate is still counted
  in its last tick, so the next one comes one tick later. With
  `max_crates = 1` there is almost always exactly one crate.
- The first crate appears on the first game tick.

### 1.3 Types and odds

| Type | r range | Odds | Effect (§3) |
|---:|---|---:|---|
| 0 | 0–49 | 50 % | cash |
| 1 | 50–64 | 15 % | explosion |
| 2 | 65–69 | 5 % | reveal map |
| 3 | 70–74 | 5 % | no map (re-shroud) |
| 4 | 75–94 | 20 % | units |
| 5 | 95–99 | 5 % | stealth |

All types look the same: image 0 of `Crate.bmp` = DATA.R16 entry 102,
drawn in the cell-object pass under shroud (doc 31 §7). Crates do not
show on the radar.

## 2. Pick-up

### 2.1 Who and when (`FUN_00494260` MoveStep `0x494472`, verified)

MoveStep calls `CratePickup(u, side, index)` right after GetNextSquare,
whenever a unit with no step in progress and to == cur chooses its next
cell. The test is on the **next cell** ("to", +0x4C / +0x4D), before the
passability test (doc 31 §6.2).

```
CratePickup(u, side):                       // FUN_0049ebf0
    if urec(u).kind == 5: return 0           // worms never
    if !(cell(to).flags & 0x1000): return 0
    t = TakeCellObject(to.x, to.y)           // FUN_0044e9d0: the crate is removed first
    if t == 0x0B: return 0
    switch t: 0..5 crate effects (§3); 7 nothing; 8..10 bloom burst (doc 31);
              other: fatal "Someone picked up an unsupported..." (cannot happen)
    return 1 only for type 4 (units given), else 0
```

- **Any ground unit of any side** takes it: infantry, vehicles,
  harvesters, MCVs, AI units, neutral units. Aircraft do not run
  MoveStep. Carried units do not move.
- The unit does not have to enter: the crate is taken when the step is
  **chosen**, even if the cell then turns out to be blocked.
- The effects go to the picker's **side** (`side` argument, the owner).
- The AI crate scan (doc 31 §6.3) calls `FUN_0044e9d0` on every crate in
  an 11 × 11 box around an idle AI team unit. That **removes** the crate
  with **no effect**, then orders the unit to the now empty cell. So AI
  team units mostly destroy crates rather than collect them. Keep it.

### 2.2 Back in MoveStep (verified)

```
r = CratePickup(u, side, index)
if r != 0:                                   // units were created on that cell
    u.+0x74 = 0                              // drop the cached route
    GetNextSquare(u, side, index)            // pick again (the new units block the cell)
    // the state-0x11 check is skipped on this path
else:
    if u.state == 0x11: return 1             // the picker died (explosion crate)
if to == cur: return 1                       // waiting
...                                          // normal step
```

## 3. Effects (`FUN_0049ebf0`, verified)

`P = (to.x·32 + 16, to.y·32 + 16)`, the crate cell centre in pixels.
"Local" = the picker's side is the local player (`0x798544`).
Explosions are spawned by `FUN_00456df0(side, ex, P.x, P.y, 0, 0, 0, 0,
0)`: owner = the picker's side, height 0, no flags. Everyone sees them.
The sound is the explosion's sample (`0x781e5c + ex·8`), played
positionally at the crate cell, **only when local** (an AI or other
player's pick-up is silent).

| Type | Explosion | Frames | Sound |
|---:|---|---:|---|
| 0 cash | EX_CASH (36) | 8 | 9 S_CRATEREVEAL (`sonic3`) |
| 1 explosion | EX_WHITEN1 (12) | 8 | none (−1) |
| 2 reveal | EX_CRATEREVEAL (53) | 18 | 9 S_CRATEREVEAL |
| 3 no map | EX_CRATENOMAP (54) | 18 | 9 S_CRATEREVEAL |
| 4 units | none | | none |
| 5 stealth | EX_CRATESTEALTH (55) | 18 | 9 S_CRATEREVEAL |

There is **no voice** for any crate.

### 3.1 Type 0: cash

```
AddPendingCash(side, CrateCash)              // FUN_0046c7b0, 1000
if EX_CASH != 0xFF: explosion; if local: sound
```

The 1000 goes to **pending cash**, so it drips into the credits like a
refund (doc 11 §8, `S_MONEYTICK`). It is not limited by silo space.

### 3.2 Type 1: explosion

```
explosion EX_WHITEN1 (no 0xFF test); if local: its sound (none)
if CRATE bullet (0x6d60ef) != 0xFF:
    Damage(P.x, P.y, height 0, CRATE, attacker = side, firer 0xFFFF, deviate 0)   // FUN_0049d2e0
```

CRATE bullet 29: damage **10000**, warhead 0 **W_FIRE** (radius 64 px,
NONE 90 %, WALL 5, BLDG 65, WOOD 50, LIGHT 40, HEAVY 30, CONCRETE 100,
INV 0, CY 20, HARV 25). Only the damage call is used. The bullet's own
impact explosion (EX_155MMGUN) and fire sound (20) are not played.

- Linear fall-off (doc 18 §2.1). The picker stands one cell away: about
  32 px straight (half damage) or 45 px diagonal (about 30 %). A LIGHT
  vehicle 32 px away takes 4000 · 0.5 = 2000. **Infantry next to the
  crate die** (9000 · 0.5 = 4500 > 1000).
- The attacker is the picker's own side, and there is no friendly-fire
  test: own, allied, enemy and neutral objects in range are all hit.
  Neutral units hit this way turn hostile to the picker (doc 18 §2.2).
- Craters and the bloom check at the impact cell as for any damage call
  (doc 18 §2.4, doc 31 §6.1).

### 3.3 Type 2: reveal map

Explosion (if not 0xFF), then if local: sound, `FUN_0044c480` (whole map
unshrouded) and the true radar copied to the shown radar
(`FUN_004305f0`, doc 16 §6.2). Not local: only the explosion. The AI
ignores the shroud anyway.

### 3.4 Type 3: no map

Explosion, then if local: sound, shown radar cleared to black
(`FUN_00430860`), every shroud byte set to 1, `FUN_0049f4d0(side)`
re-reveals around the side's units and buildings (doc 16 §6.2). The
anti-cheat checksum (doc 16 §6.3) is checked before and stored after.
Not local: only the explosion.

### 3.5 Type 4: units

```
owner = side
do:                                          // first roll
    do:
        name = rand() % 26                   // line 0x22e0
        rec  = MyVersionOfUnit(owner, name, 0)   // FUN_0046c570
    while urec(rec).kind == 5                // sandworm
while urec(rec).kind in {8, 9, 2, 10}        // frigate, ornithopter, carryall, Death Hand
if !urec(rec).infantry:
    CreateUnit(owner, rec, to.x, to.y, to.x, to.y, 0, 0)    // one vehicle
else:
    repeat 5 times:                          // five infantry, each rolled again
        do:
            name = rand() % 26               // line 0x22eb
            rec  = MyVersionOfUnit(owner, name, 0)
        while !urec(rec).infantry
        CreateUnit(owner, rec, to.x, to.y, to.x, to.y, 0, 0)
return 1
```

- `MyVersionOfUnit(p, name, 0)`: the first record with that name, or the
  last one whose house mask has the player's house bit (doc 08). With 0
  it does not check the disallowed list, so **any unit can come out**,
  including other houses' specials.
- **The first roll:** 21 of the 26 names are allowed, each 1/21:

  | Result | Names |
  |---|---|
  | one vehicle (12/21 = 57 %) | TRIKE, RAIDER, QUAD, HARVESTER, COMBAT_TANK (house version), MCV, MISSILE_TANK, DEVIATOR, SIEGE_TANK, SONIC_TANK, DEVASTATOR, STEALTH_RAIDER |
  | five infantry (9/21 = 43 %) | LIGHT_INF, TROOPER, ENGINEER, THUMPER, SARDAUKAR, FREMEN, NSFREMEN, SABOTEUR, GRENADIER |

- **The five infantry** are each re-rolled from the 9 infantry names
  (1/9 each). The name of the first roll is not kept. SARDAUKAR gives
  record 29 (MP Sardaukar) to Harkonnen, record 4 to the others.
- rand() count: unbounded (re-rolls). They run inside the unit update, in
  the picker's MoveStep.
- CreateUnit with x1 = x2 (doc 12 §8.3): full hit points, body facing 0,
  no move. A harvester starts in state 0xC (it looks for spice). Units of
  a non-local side in a non-network game start in **state 3** (area
  guard / hunt); the local player's stay in state 2. FREMEN and
  STEALTH_RAIDER get the cloak flag 0x10 as usual.
- Each CreateUnit can fail: pool full (`ReachedUnitLimit`, doc 50), side
  inactive. The other calls still run.
- **Occupancy:** a vehicle takes the cell (bit 8). Five infantry fill the
  five sub-slots exactly. CreateUnit stops the game with "Tile already
  occupied by unit" if the cell already has bit 8, and with "Couldn't
  find free slot" if an infantry finds no slot. Neither should happen
  (the crate cell had no unit when it was made, and a unit stepping there
  would have taken the crate). Our port: log and skip that unit (crash
  fix). **It can happen through a carryall drop** (checked in the code):
  the type-1 drop step `FUN_0049e3e0` tests only `flags & 0x3f8` of its
  cell, not the cell-object bit 0x1000, so a carryall (the free harvester
  of a new refinery, a reinforcement) can set a unit down on a crate. The
  crate stays under it (a unit never "chooses" the cell it stands on).
  If another ground unit then picks that cell as its next step while
  the dropped unit is still there, and the crate is type 4, CreateUnit
  meets the occupied cell and the exe stops with one of the two fatal
  errors. Factory exits cannot: a new unit starts on the factory's own
  cells (building bit 0x10, never a crate cell) and takes a crate on its
  way out like any mover. The frigate drops on the Starport's centre
  cell (a building cell). So the crash fix is a real (rare) difference
  from the exe, allowed as a crash fix.
- No sound, no explosion, no voice, no shroud reveal at creation (doc 16
  §2).

### 3.6 Type 5: stealth

`FUN_0049f1b0(to.x, to.y, side)` (doc 32 §1.8): every unit of the side
with `dx² + dy² < 9` cells (its current cell +0x4A / +0x4B; a 5 × 5 box)
gets the cloak ability (flag 0x10, +0x31 = 0) unless it is already
stealthed. A saboteur gets a full charge (+0x29 = 0xA0) instead. The
picker itself is one cell away, so it is always included. Then the
explosion (if not 0xFF) and, if local, the sound.

## 4. The Thumper

### 4.1 Record (Templates.bin, verified)

Record 3, name 3 THUMPER, all houses (mask 0x7F), **kind 0xE**,
infantry, hp 375, armour NONE, speed 64, no weapon, cost 200, view 1,
skirmish flag +0xAA = 1. Built at the BARRACKS after its upgrade (doc 28
§1.2). Graphic element "THUMPER": walk frames 1–6, thump frames 7–11.

### 4.2 Who can build it (sidebar rebuild, `FUN_0046be50` `0x46c20a`, verified)

A unit record with +0xAA ≠ 0 (THUMPER, GRENADIER, STEALTH_RAIDER, MP
SARDAUKAR) is listed only when `0x7984c4 != 0` (network) or game type
`0x797e34 == 1` (skirmish). A record of kind 0xE also needs
`gNetWorms != 0`. So: **no thumper in the campaign, and none in a
skirmish with worms off.** A thumper can still come from a crate (§3.5),
even with worms off; it then deploys normally but attracts nothing.

The AI never deploys a thumper: `FUN_0045a3f0` is called only by player
input (`0x4457fc`), by DoMCVs (`FUN_00422140`, kind 6) and by the
Devastator path of `FUN_0041c3e0` (kind 7). **It never builds one
either** (verified): the only AI unit choice is FindBestUnitToBuild
(`FUN_0041b910`, doc 22 §3.2). Its harvester and MCV branches name
fixed records; the ratio pick gives a record with UnitBuildPriority 0
the score 100000, which never beats the start value 100000 (strict <).
The skirmish AI block (`_PRAC.MIS`, doc 53 §1.3) has THUMPER priority 0,
and no campaign mission offers the thumper (§4.2). An AI side can only
get one from a units crate.

### 4.3 Deploy (doc 37 §2.1, verified here again at `0x4457c1`)

- Cursor: own selected thumper under the mouse → 0xC (deploy) when the
  **mouse cell** is sand (0x10000), else 0xD.
- Click on the already selected thumper: mouse cell sand → order 0x2F
  (`FUN_0045a3f0(side, unit index)`), then the select voice
  `FUN_0049f240(0)`. Otherwise an exclusive reselect. No "can't deploy"
  voice.
- Order 0x2F, kind 0xE (doc 30 §1.4): `if SetState(u, 0x20): u.+0x14 = 1`.
  No cell test. Order latency as every order (doc 49).
- SetState (`FUN_0049deb0`) allows 0x20 for kind 0xE and refuses it for
  aircraft and kind-0 units. **0x20 is not sticky**: any later order
  (move, attack, retreat…) replaces it.

### 4.4 State 0x20 (`0x49b45f`, verified; the period is new)

```
TickThump(u, side):                          // unit update, after FUN_004940d0
    if u.steps (+0x4F) != 0:                 // finish the current step first
        u.dest = u.home (+0x50/+0x51); MoveStep(u); return
    if TurnBody(u, 8): return                // face direction 8 first
    if !urec.infantry: fatal "Invalid type for thumper"
    SetInfAnim(u, 8)                         // mode 8 = frames 7..11 (prone ignored, m ≥ 3)
    first = InfModes[u.mode].first           // 7
    if u.frame == first + 1:                 // frame 8: the pause
        u.+0x29 += 1
        if u.+0x29 < 60: u.+0x25 = 0         // hold the frame
        else: u.+0x29 = 0; u.+0x25 = 3       // release: frame 9 next update
    if u.frame == first:                     // frame 7: the thump
        Explosion(side, EX_THPUFF, u.x >> 16, u.y >> 16, 0, 0, 0, 0, 0)
        if pixel inside the view (0x798538 .. +0x786d48, 0x79853c .. +0x6dc538)
           and (0x4dfb04 == 0 or shroud byte of the thumper's cell == 0)
           and !SampleIsPlaying(S_THUMPER):           // FUN_0046f4f0
            PlayAt(S_THUMPER, cell of u.x, u.y)       // FUN_0046f540
```

**The cycle**, with the infantry animation tick (`FUN_004940d0`: +0x25 + 1
each update, frame + 1 when it passes 2) running first in the same
update:

| Updates | Frame | What happens |
|---|---:|---|
| A … A+59 | 8 | +0x29 counts 1 … 60; +0x25 held at 0 |
| A+60 … A+62 | 9 | |
| A+63 … A+65 | 10 | |
| A+66 … A+68 | 11 | |
| A+69 … A+71 | 7 | frame 12 is out of mode 8: the tick resets the mode (0, or 4 / 14 / 15 by its rolls) and the frame; SetInfAnim(8) clamps back to 7. **EX_THPUFF on each of the 3 updates**; S_THUMPER on the first (then it is playing) |
| A+72 | 8 | next cycle |

So one thump every **72 ticks (3.6 s at 20 ticks/s)**, with **three
overlapping EX_THPUFF** effects one tick apart (5 frames each, sound 12
S_THUMPER = `thumper1`). Verified that all three are drawn: the spawn
`FUN_00456df0` has no duplicate test (it takes a free slot from the
effect pool, `FUN_0046bd40`, and links it), so three effects at the same
pixel run their frames one tick apart and the explosion pass draws
each one.

Notes:

- +0x29 is **not reset** by the deploy order. It starts at 0 (creation)
  and keeps its value when the thumping is interrupted, so the first
  pause after a re-deploy can be shorter. Port as is.
- The first thump comes as soon as the thumper faces direction 8:
  SetInfAnim(8) clamps a walk / stand frame to 7.
- The wrap in `FUN_004940d0` draws rand() like any infantry: `rand() % 5`
  when +0x2A == 0 (just hit), `rand() % 400` when +0x2A == 255 (2040
  ticks unhurt, so every cycle after about 100 s). Facing 8 is neither
  < 8 nor > 24, so a hit of the idle roll sets mode 14 / 15, which the
  SetInfAnim(8) of the same update overrides. Only the rand() calls
  matter. This needs doc 45's wrap logic to be ported.
- The sound needs the thumper's pixel in the view and its cell
  unshrouded (when `0x4dfb04` is set). Positional volume as doc 12 §9.
- The puff is spawned whatever the view or shroud (draw-time clipping
  only).

### 4.5 Worms (doc 17 §5.5)

- In decide (state 0x1C) the worm first looks for the **nearest thumper
  in state 0x20 on a sand cell** (`FUN_0044d500`, whole map, no range
  limit, any owner). State 0x20 is set at the order, so a thumper that is
  still turning or finishing its step already counts. A thumper on rock
  does not (the click tests the mouse cell, not the thumper's cell, so
  this can happen).
- `FUN_0044d500` asks `FUN_004a5270` for the infantry of each cell, which
  returns the infantry **nearest to the cell's top-left corner**. A
  thumper sharing its cell with nearer infantry is not found. Verified
  in the code: `FUN_0044d500` calls `FUN_004a5270(x · 32, y · 32, .., 0)`
  for every sand cell with infantry bits (0x3e0) and keeps the cell only
  when the unit returned is kind 0xE in state 0x20; nearest by
  |dx| + |dy| (u16, strict <). Our `World::nearest_thumper` does the same
  through `unit_in_cell`.
- The worm then goes for a vehicle on sand within 4 cells (|dx| < 5,
  |dy| < 5) of the thumper if there is one, else to the thumper's cell.
  The thumper branch has **no appetite test**: a fed worm still comes.
- **The worm does not eat the thumper** (infantry are never eaten, doc 17
  §3.1). On arrival it surfaces, decides again and finds the same
  thumper, so it stays at the thumper's cell, surfaced (shootable),
  eating any vehicle that comes within that box, until the thumping
  stops. Sleeping worms (state 0x1D) are not woken.
- With `gNetWorms = 0` there are no worms, so a crate thumper attracts
  nothing.

### 4.6 How it ends

Thumping never stops by itself. It ends when:

- the player gives the thumper another order (any SetState);
- the thumper dies (weapons, explosion crate, crushing; worms do not eat
  it);
- the game ends.

There is no "used up" counter and no self-destruction.

## 5. Other skirmish-only gameplay noticed

- **Skirmish-flag units** (+0xAA): THUMPER, GRENADIER (Atreides),
  STEALTH_RAIDER (Ordos), MP SARDAUKAR (record 29, Harkonnen) (§4.2).
- **`gNetWorms` 0..3** (Off / Low / Medium / High, multistate box
  `wormsCheck`) limits the map worms created at set-up (doc 17 §6.2) and
  gates the thumper.
- **CreateUnit state 3** for every non-local side in a non-network game
  (doc 12 §8.3) affects crate units of the skirmish AI.
- `FUN_00453b90` (mission conditions) reads `max_crates` only in the
  campaign path (`0x7984c4 == 0` and game type ≠ 1), where it is always 0.
  No skirmish effect.
- The "Worms %d Crates %d Credits %d Techlevel %d" string
  (`FUN_0040d2d0`) is the Westwood Online score upload. Not needed.

## 6. Save state

| What | Where |
|---|---|
| cell-object table (crates and blooms, 30 × 12 bytes) | doc 31 §1; saved |
| `max_crates` (`0x548048`, 1 byte) | saved / loaded (`0x441c26`, `0x4417e3`) |
| thumper state 0x20, +0x29 pause counter, +0x25, frame, mode | unit fields, saved with the unit |

`gNetCrates` and `gNetWorms` are settings, not game state; their effect
is already in `max_crates`, the worm count and the build lists.

## 7. Corrections to earlier docs

- **doc 31 §8:** `0x6b9834` = `gNetCrates` (skirmish checkbox),
  `0x6b8859` = VARS `NumberOfCrates` (1), `0x6b885a` = `CratesPerPlayer`
  (0). Pick-up effects are §3 here.
- **doc 37 §2.2:** the TODO on the period is resolved: 72 ticks, and the
  puff is spawned on all three updates of frame 7 (§4.4).
- **doc 35 §10:** DoSpecialWeapons case 0 is the **ENGINEER** group (unit
  kind 3), not the thumper. The thumper has kind 0xE and no AI use.
- **doc 17 §5.6:** the random-unit crate re-rolls worms **and** frigates,
  ornithopters, carryalls and Death Hands (`0x49eee4..0x49eef6`).
- **doc 16 §6.2:** add that types 2 and 3 still spawn their explosion for
  a non-local picker (seen by all), with no sound and no shroud change.

## 8. Implementation checklist

1. **`src/sim/world.hpp` / `save_state.cpp`:** add `uint8_t max_crates_`
   (`0x548048`), set at set-up (0 campaign; skirmish rule of §1.1 from
   `gNetCrates` and VARS indices 20, 21, 24), saved and loaded (bump the
   save version; old saves load with 0).
2. **`src/sim/blooms.cpp` `tick_cell_objects`:** count active types < 6
   before their timer decrement; after the loop, if `count < max_crates_`,
   `add_random_crate()` with the exact rand() order of §1.2, including
   the full-table case. Cap the cell search (crash fix, logged).
3. **`src/sim/blooms.cpp` `drive_over`:** take the side, return int.
   Cases 0–5 of §3: `refund(side, crate_cash)`; damage call with bullet
   `0x6d60ef`; reveal / no-map via the shroud functions (doc 16); the
   unit roll with `templates_.unit_record(name, house)` (no disallowed
   test) and `create_unit`; stealth (doc 32 §1.8). Explosions for all,
   sound only for the local side, positional (`sounds.push_back`).
4. **`src/sim/world.cpp` `move_step`:** after `drive_over`, on 1: clear
   `route_count`, call `next_square` again, skip the state test; on 0:
   `if (u.state == 0x11) return 1`.
5. **`src/sim/capture.cpp` `order_deploy`:** case 0xE:
   `if (set_state(u, 0x20)) u.next_state = 1;`. Check `set_state` allows
   0x20 only for the kinds of §4.3 and does not make it sticky.
6. **`src/sim/world.cpp` unit update:** state 0x20 → new
   `tick_thump(u)` (§4.4), after the infantry animation tick. Needs the
   doc 45 wrap rolls for rand() parity.
7. **`src/sim/worm.cpp`:** `nearest_thumper` already tests kind 0xE /
   state 0x20; check it uses the "nearest infantry to the corner" lookup
   (§4.5).
8. **`src/sim/economy.cpp:138`:** `skirmish_only` records allowed when
   skirmish or network; kind 0xE also needs `gNetWorms != 0`.
9. **`src/app/game_screen.cpp`** (cursor at ~829, click at ~543): kind
   0xE: cursor 0xC / 0xD from the mouse cell's sand bit; click posts
   `order_deploy` on sand, else reselect; no CANTDEPLOY voice.
10. **Skirmish menu (future):** `crateCheck` → `gNetCrates`,
    `wormsCheck` → `gNetWorms`, both kept in our settings file like
    `dune2000.cfg`.
11. **Tests:** a crate spawn rand() sequence on a fixed map; one test
    per crate type (cash pending, damage values at 32 / 45 px, unit roll
    with a seeded RNG, stealth box); the thumper cycle (puffs at
    A+69..71, sound once, period 72); the worm parking at a thumper.
