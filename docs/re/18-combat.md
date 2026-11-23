# 18: Combat (1.06)

Covers the weapon (bullet) table, the warhead/armour table
(`ARMOUR.BIN`), the damage function, how units and turrets pick targets,
turn and fire, how bullets fly and hit, infantry fire/prone/death
animation modes, and who returns fire. The goal is a faithful first
implementation of A1V1-level combat (light infantry, troopers, trikes,
quads, medium gun turrets). Decompiles come from the local GhidraMCP
server (`re/ghidra_http.py`), float code from `re/disasm.py`, data from
`Data/bin/Templates.bin` and `Data/bin/ARMOUR.BIN`.

Main functions:

| Address | Role |
|---|---|
| `FUN_00497b90` | unit update; the switch is on state − 2 (decompiler `case N` = state N + 2) |
| `FUN_0049b7f0` | building update (turret states 2 / 4 / 5 / 6, death 0x11) |
| `FUN_00458e90` | per tick, per player: units, buildings, bullets, explosions |
| `FUN_00495b90` | unit "ShootTarget": turn, then fire one bullet |
| `FUN_00496030` | building "ShootTarget" |
| `FUN_0049e9d0` | "NewBestBullet": pick primary or secondary weapon |
| `FUN_00456a30` | "ModelAddBullet": create a bullet object |
| `FUN_0049be60` | bullet update (one tick) |
| `FUN_0049d2e0` | "DamageTiles": area damage at a pixel |
| `FUN_0049d170` | closest occupied building cell (Manhattan) |
| `FUN_00497390` | closest occupied building cell (squared distance) |
| `FUN_00496480` | unit auto-fire (keep target or search, then fire) |
| `FUN_00496790` | turret auto-fire |
| `FUN_00496b00` | "AttackUnit" step (states 3, 4) |
| `FUN_00496d10` | "AttackBuilding" step (state 5) |
| `FUN_00496ff0` | attack-ground step (states 6, 0xa) |
| `FUN_00497270` / `FUN_00497140` | "CanUnitAttackTile" / building version (range test) |
| `FUN_004970f0` | squared-distance test `dx² + dy² ≤ r²` |
| `FUN_004a4900` | "EnemyUnitInRange": spiral search for a unit |
| `FUN_004a4fd0` | spiral search for an enemy building |
| `FUN_004a5270` | object in a cell, by pixel |
| `FUN_0049cde0` | unit killed (doc 14 §4.6) |
| `FUN_0049ca10` | building destroyed |
| `FUN_0049fa50` / `FUN_0049f8f0` | infantry crushed by a vehicle |
| `FUN_0049f870` | neutral side becomes hostile |
| `FUN_004940d0` / `FUN_00494050` | infantry animation tick / set mode |
| `FUN_00428070` | bullet draw pass |
| `FUN_0046b0f0` | `ARMOUR.BIN` / `armour.dat` loader |
| `FUN_00465780` | `BullAtrb.dat` record parser (column order) |

"Verified" means read from the code or the data. "Inferred" is marked.
Ticks are game ticks. Pixel values are 16.16 fixed point unless written
as "px". `rand()` is the MSVC RNG of doc 06; every `rand()` named here
is a real call and changes the RNG stream.

## Corrections to earlier docs

- **doc 08, unit record:**
  - **+0x0E is the reload delay in ticks**, not a range. It is copied to
    the unit's reload counter (+0x1C) when it fires (`FUN_00495b90`).
    LIGHT_INF 30, TROOPER 40, TRIKE 20, QUAD 30.
  - +0x0F is the turret turn delay (doc 09 has it).
  - **+0x37 is the muzzle offset in px** along the facing (infantry 2,
    trike/quad 3, tanks 5). **+0xA8 is the muzzle flash explosion**
    (0xFF none; TRIKE/RAIDER 43 EX_TRIKEM, COMBAT_TANK 44 MUZ_TANK,
    DEVASTATOR 45 MUZ_DEVAS).
  - +0x26 = has a turret (turns the turret, not the body, to aim).
- **doc 08, building record:**
  - **+0x06 is the turret turn delay**, **+0x07 the reload delay**
    (medium turret 35, large 60). Both are copied to the object
    (+0x36 / +0x37). +0x07 is not a range.
  - **+0x0F = can fire** (turret flag). **+0x4E = needs full power to
    fire** (LARGE_GUN_TURRET 1, medium 0).
  - +0x60 / +0x61 are signed pixel offsets (muzzle flash position for
    turrets; also used as a cell offset by the repair pad code).
  - +0x8C is the muzzle flash explosion (MUZ_GTUR for the medium turret,
    none for the large one).
- **doc 08 / templates.md, block `0x782068`:** it is the **bullet
  attribute table**, 64 × 28 bytes (§1.1). `ARMOUR.BIN` holds the
  warhead table (§1.2).
- **doc 09, unit fields:** +0x1C = reload counter (−1 per tick). +0x26 =
  **target side** (with +0x0C target index). +0x2A = "ticks / 8 since
  last hit" (reset to 0 by unit flag 1). +0x27 = damage smoke timer.
  +0x30 / +0x32 = last attacker side / index. +0x56 = idle turret
  facing goal. Flag 1 = hit this tick, flag 2 = fired this tick.
- **doc 09, states:** state 6 = attack ground (order 0x16); state 0xA
  fires at the blocking enemy's cell; **state 0x18 is "go to repair
  pad"**, not an attack state.
- **doc 12, building fields:** +0x3C is the **target side** (it starts as
  the owner); +0x40 is the owner. +0x2C = target index, +0x21 = turret
  facing, +0x36 / +0x37 = turn / reload counters.
- **doc 14 §4.4 TODO (muzzle flashes):** the spawner sets the explosion
  frame (+0x01) to the shooter's facing 0..31 (body, or turret when the
  record has a turret element). The flash is **not** attached (+0x08 stays
  0xFFFF). Units spawn it at their own centre pixel, before the muzzle
  offset (§3.1); turrets at an offset (§6.2).

## 1. Data

### 1.1 Bullet table `0x782068` (Templates.bin block 3, 64 × 28 bytes, verified)

Count: `0x795530` / `0x795540` = 34. Names at `0x6db390` (64 × 50).
Column order from the text parser `FUN_00465780`: name, speed, damage,
range, warhead, anti-air, impact explosion, flag 1, flag 2, flag 4,
trail explosion, flag 0x20, flag 0x40, sound, flag 0x80, byte +0x13,
flag 0x100, flag 0x200, flag 0x1000. The name "SOUND" sets flag 0x800.

| Off | Type | Meaning |
|---:|---|---|
| +0x00 | u32 | speed, 16.16 px per tick (text value << 10) |
| +0x04 | i32 | damage |
| +0x08 | u32 | flags (below) |
| +0x0C | i32 | fire sound sample (samples.uib index), −1 none |
| +0x10 | u8 | own index; also the image set (`0x714b00[idx·32 + frame]`) |
| +0x11 | u8 | impact explosion, 0xFF none |
| +0x12 | u8 | trail explosion (sets flag 0x10) |
| +0x13 | u8 | copied to bullet +0x1B (`0x456b0d`); never read (§8) |
| +0x14 | u8 | can hit aircraft |
| +0x15 | u8 | warhead (row of `ARMOUR.BIN`) |
| +0x18 | u32 | range, px |

| Flag | Meaning (where) |
|---|---|
| 0x1 | arc (ballistic height) (`FUN_00456a30`, update) |
| 0x2 | draw a shadow at ground level; also set by flag 1 and 0x100 at creation |
| 0x4 | no reader: no effect (§8) |
| 0x10 | spawn the trail explosion every tick |
| 0x20 | homing; flight time doubled |
| 0x40 | deviator (converts the hit unit) |
| 0x80 | animated: frame cycles every tick |
| 0x100 | falls from the firer's altitude (bombs, debris) |
| 0x200 | additive blit |
| 0x400 | may bounce on landing (not set by the parser; building debris sets it) |
| 0x800 | sonic: fixed flight time from the range, damages along the way, wave blit |
| 0x1000 | detonates over a cell with flag 0x400 (walls and turrets, doc 12) |

DATA.R16: bullet `b` frame `f` is entry `3912 + Σ frames(<b) + f`
(LMG 3912, BAZOOKA 3913–3944, ROCKET 3945–3976, HMG 3977, 110MM GUN
3982, NEW ROCKET 3986–4017).

### 1.2 `ARMOUR.BIN` (2,702 bytes, loader `FUN_0046b0f0`, verified)

| File off | Exe | Size | Content |
|---:|---|---:|---|
| 0 | `0x790550` | 600 | 30 warheads × 20 bytes |
| 600 | `0x78fb30` | 1500 | warhead names, 30 × 50 |
| 2100 | `0x786d90` | 600 | armour names, 12 × 50 |
| 2700 | `0x6e2e18` | 1 | warhead count (14) |
| 2701 | `0x789190` | 1 | armour count (10) |

Warhead row (20 bytes): bytes 0..(armours − 1) = **damage percent per
armour type**; +0x0C u32 = **radius** px (text "radius", default 16);
+0x10 u8 = **infdeath** (infantry death animation, text "infdeath").

Armour types: 0 NONE, 1 WALL, 2 BUILDING, 3 WOOD, 4 LIGHT, 5 HEAVY,
6 CONCRETE, 7 INVULNERABLE, 8 CY, 9 HARVESTER. Unit armour = unit record
+0x02, building armour = building record +0x05.

| # | Warhead | NONE | WALL | BLDG | WOOD | LIGHT | HEAVY | CONCR | INV | CY | HARV | radius | infdeath |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | W_FIRE | 90 | 5 | 65 | 50 | 40 | 30 | 100 | 0 | 20 | 25 | 64 | 0 |
| 1 | W_DEVIATE | 20 | 20 | 20 | 20 | 20 | 20 | 100 | 0 | 10 | 20 | 15 | 0 |
| 2 | W_SPECIAL | 90 | 50 | 75 | 60 | 60 | 60 | 100 | 0 | 25 | 60 | 64 | 0 |
| 3 | W_WORM | 100 | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 0 |
| 4 | W_NOTHING | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 15 | 0 |
| 5 | W_BULLET | 100 | 10 | 25 | 75 | 40 | 20 | 100 | 0 | 20 | 25 | 15 | 1 |
| 6 | W_AT | 20 | 50 | 50 | 60 | 100 | 75 | 100 | 0 | 20 | 50 | 32 | 2 |
| 7 | W_GT_AT | 25 | 100 | 50 | 65 | 100 | 50 | 100 | 0 | 20 | 50 | 32 | 2 |
| 8 | W_AT_MIS | 15 | 75 | 60 | 65 | 90 | 100 | 100 | 0 | 30 | 50 | 32 | 2 |
| 9 | W_BAZOOKA | 8 | 75 | 40 | 45 | 70 | 100 | 100 | 0 | 20 | 50 | 32 | 2 |
| 10 | W_PLASMA | 50 | 100 | 75 | 60 | 100 | 100 | 100 | 0 | 40 | 100 | 32 | 2 |
| 11 | W_PLASMA_DEATH | 100 | 100 | 100 | 60 | 100 | 100 | 100 | 0 | 40 | 100 | 96 | 2 |
| 12 | W_SONIC | 100 | 50 | 60 | 100 | 100 | 60 | 100 | 0 | 20 | 50 | 64 | 3 |
| 13 | W_HE | 125 | 100 | 100 | 70 | 30 | 20 | 100 | 0 | 20 | 25 | 64 | 3 |

### 1.3 A1V1 weapons and shooters (verified from the data)

| Bullet | Speed px/tick | Damage | Range px | Warhead | Impact | Trail | Fire sound | Flags | Frames |
|---|---:|---:|---:|---|---|---|---|---|---:|
| 0 LMG | 351.5625 | 125 | 80 | W_BULLET | EX_MG (7) | – | 0 S_LMG | 0 | 1 |
| 1 BAZOOKA | 9.375 | 300 | 96 | W_BAZOOKA | EX_BAZOOKA (2) | EX_TRAIL3 (17) | 7 S_ROCKET | 0x10 | 32 |
| 2 ROCKET | 11.71875 | 250 | 112 | W_GT_AT | EX_ROCKET (1) | EX_TRAIL3 | 7 S_ROCKET | 0x10 | 32 |
| 3 HMG | 351.5625 | 180 | 96 | W_BULLET | EX_MG | – | 3 S_HMG | 0 | 1 |
| 8 110MM GUN | 28.125 | 290 | 160 | W_AT | EX_TANKGUN (6) | – | 4 S_80MMGUN | 0 | 1 |
| 12 NEW ROCKET | 9.375 | 480 | 176 | W_AT_MIS | EX_NEW ROCKET (4) | EX_TRAIL (16) | 7 S_ROCKET | 0x30 | 32 |

None of these is anti-air except NEW ROCKET. The impact explosions above
have no sound (doc 14 §4.2), so only the fire sound plays. LMG and HMG
are effectively instant (§4.1).

| Shooter | HP | Armour | Weapon | Reload | Muzzle offset | Muzzle flash | Turret | Turn delay |
|---|---:|---|---|---:|---:|---|---|---:|
| LIGHT_INF | 600 | NONE | LMG | 30 | 2 | – | no | body +0x03 = 0 |
| TROOPER | 700 | NONE | BAZOOKA | 40 | 2 | – | no | 0 |
| TRIKE | 900 | WOOD | HMG | 20 | 3 | EX_TRIKEM | no | 0 |
| QUAD | 1100 | LIGHT | ROCKET | 30 | 3 | – | no | 0 |
| MEDIUM_GUN_TURRET | 2700 | HEAVY | 110MM GUN | 35 | (§6.2) | MUZ_GTUR | turret | +0x06 = 0 |
| LARGE_GUN_TURRET | 3000 | HEAVY | NEW ROCKET | 60 | (§6.2) | – | turret | 0 |

Full-centre damage `pct × damage / 100` (§2):

| Bullet | NONE | WALL | BLDG | WOOD | LIGHT | HEAVY | CONCR | CY | HARV |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| LMG | 125 | 12 | 31 | 93 | 50 | 25 | 125 | 25 | 31 |
| BAZOOKA | 24 | 225 | 120 | 135 | 210 | 300 | 300 | 60 | 150 |
| HMG | 180 | 18 | 45 | 135 | 72 | 36 | 180 | 36 | 45 |
| ROCKET | 62 | 250 | 125 | 162 | 250 | 125 | 250 | 50 | 125 |
| 110MM GUN | 58 | 145 | 145 | 174 | 290 | 217 | 290 | 58 | 145 |

## 2. Damage: `FUN_0049d2e0(x px, y px, height, bullet, attacker side, firer index, deviate)` (verified)

Called at bullet impact, by sonic bullets in flight, by building and
worm code. `deviate` = bullet flag 0x40.

```
air = height > 0x60000                        // above 6 px
if !air:
    if an active cell object of type 8, 9 or 10 is at the impact cell:
        burst it: FUN_0044ef10(cx, cy, 4 / 5 / 6); remove it   // spice bloom
        return                                // nothing else is damaged
R = radius[warhead]
for p in 0 .. player count − 1:
    if !air: for each building b of p (list p+0x10):
        skip if b.state == 0x11 or b.flags & 6 (building up / selling)
        (cx, cy) = closest occupied cell of b to the impact cell   // FUN_0049d170
        dx = x − (cx·32 + 16); dy = y − (cy·32 + 16)
        HIT(b, armour = building rec +0x05)
    for each unit u of p (list p+8):
        skip if u.flags & 0x8000000 or 0x100 (carried), or state 8, 0x11, 0x17
        ground units need !air, aircraft (flag 0x40) need air
        dx = x − (u.x >> 16); dy = y − (u.y >> 16)
        HIT(u, armour = unit rec +0x02)
craters (§2.4)
```

### 2.1 The formula

```
if |dx| < R and |dy| < R:
    d = sqrt(dx² + dy²)                       // x87; dx², dy² as int32
    if d < R:
        base = (pct[warhead][armour] · damage) / 100      // unsigned int
        dmg  = trunc(base · ((R − d) / R))                // x87, then ftol
```

Linear fall-off: full damage at the centre, 0 at the radius. The x87
order is: t = R − d, t = t / R, dmg = base · t. Use double with that
order. Last-bit differences from the x87 are accepted by the project's
float decision (CLAUDE.md §10: the formulas and order are kept, x87
precision quirks are not emulated); dx², dy² < R² ≤ 96² cannot overflow.

There is **no friendly-fire check**: every player's objects in the
radius are hit, including the attacker's own. Neutral and allied objects
too. There is no line-of-fire test.

### 2.2 Units (verified)

- flags |= 1 (hit this tick; the unit update then resets +0x2A).
- **Survives** when `dmg < hp`:
  - hp −= dmg.
  - Neutral to hostile: if the attacker side is valid, the bullet is not
    SPICEEXPL (`0x6d610a` = 21) and `alliance[p][attacker] == 2`,
    `FUN_0049f870` sets both directions to 1 (enemy) and shows
    "NeutralBecomeHostile" for the local player.
  - Harvester (kind 1) of the local player with a known firer:
    `*_HARVATTACK` voice (limit 300 ticks). A unit with flag 0x2000000
    only loses that flag instead.
  - Not a worm and hp < max/2: smoke timer +0x27 = (rand() & 0x7F) +
    0x40 (every such hit).
  - If +0x32 is 0xFFFF, the bullet is not SPICEEXPL and the attacker is
    another side: +0x30 = attacker side, +0x32 = firer index (0xFFFF if
    the firer died while the bullet flew).
  - `deviate`: flag 1 is cleared again. Own-side hits and infantry only
    set +0x2A = 1. Otherwise, unless the unit is on a building cell
    (`FUN_0049d280`), it changes side (`FUN_0046d2e0`); the new copy gets
    +0x2E = 0xF0 (`0x49dc41`) and flag 0x1000. The full deviator rules
    are in doc 40 §2.
- **Dies** when `dmg ≥ hp`:
  - Unless SPICEEXPL: neutral to hostile as above, +0x32 / +0x30 =
    firer / attacker (always written here), kill counters of the
    attacker (`+0x24c98[...]`, `+0x24c90`).
  - Infantry: animation mode = 9 + infdeath[warhead] (§3.8).
  - Not a worm: `*_UNITLOST` voice if p is the local player, loss
    counters, then `FUN_0049cde0` (death effects and state 0x11, doc 14
    §4.6). A worm gets hp = 1 instead (doc 17).

### 2.2.1 KillUnit: `FUN_0049cde0(side, idx)` (verified 2026-10-16)

In order:

1. Not an aircraft (flag 0x40) but carried (0x100): flags |= 0x400000.
   An aircraft gets explosion flag 0x1000 for steps 4.
2. Kind 2 (carryall) with flags & 0x140 = 0x140 and cargo +0x0C ≠ 0xFFFF:
   KillUnit(cargo) first (no state test).
3. Infantry with mode +0x20 outside 9..13: SetInfAnim(9 + rand() [line
   0x1bd2] & 3).
4. Kind 2: every request slot of the side's **carry request table**
   (`+0x26611`, 0x14 bytes, count `+0x2688c`; doc 33) with status 2
   whose carryall (+0x26614) is this unit: the waiting unit
   (+0x26612) gets SetState(saved state +0x2661c) and the slot status
   becomes 3.
   The **delivery table** (`+0x262ec`, doc 13 §5) is not touched. A
   displacement scan of every access to `+0x262ec..+0x26300` (direct,
   or a `lea` of an entry) finds them only in `FUN_00454120`,
   `FUN_004563b0` (queue), `FUN_00457140`, `FUN_0042a5d0`,
   `FUN_0046b990` (player init), `FUN_0046d9a0` (the Starport's
   removal), the launch `FUN_0046def0` and the drop `FUN_0049e3e0`;
   none is reached from KillUnit or the state-0x11 update.
   A delivery carryall (and the Starport frigate) cannot be shot down
   at all: the launch sets flags 0x8000b00 (`0x46e36b`), the damage
   loop skips flag 0x8000000 (`0x49d806`), and no instruction in the
   exe clears that bit of a unit (immediate scan: no AND mask with bit
   27 clear is applied to unit flags; the `0xf1ffffff` masks are cell
   flags). AA units still lock on to it and their rockets do nothing.
   So it dies only through RemoveSide, and its entry then stays active
   and started (the side is inactive, so the table is not processed
   any more). Ours does the same (`World::kill_unit`, `damage_at`;
   test `tests/sim/test_combat.cpp`). The delivery case of KillUnit is
   therefore unreachable in play.
5. Death explosion record +0x2F ≥ 0 **and the unit not already in state
   0x11**: EX_WHITEN1 at the pixel (height 0), the explosion's sound at
   the pixel's cell, the death explosion at the pixel and altitude; both
   with the aircraft move (DX, −DY)[facing][speed >> 11]. Pixel =
   x / 65536 and cell = pixel / 32, both signed divisions toward zero
   (not the +0x4a cell bytes).
6. Infantry not in state 0x17 (entering): `"S_MANDYING%d"` with
   rand() [line 0x1bfb] % 9 + 1 (`0x49cf8a`), at the pixel's cell. This
   test is separate from step 5 (no infantry record has a death
   explosion in 1.06, so the two never both run).
7. hp = 0, SetState(0x11). The removal countdown +0x2F is not written
   here: its only writers are CreateUnit (`0x4559b4` infantry 0x32,
   `0x4559ba` else 10) and the state-0x11 decrement (`0x49a09c`), so it
   still holds the creation value (the readers are the Grenadier test
   `0x499de2`, the removal test `0x499e31` and a debug dump `0x4593c5`).
8. Kind 0xB or 4 (Sardaukar, saboteur) and SARDDEATH (`0x6d60f0`) ≠ 0xFF:
   `FUN_0049d2e0(+0x4a · 32, +0x4b · 32, 0, SARDDEATH, side, 0xFFFF, 0)`.

### 2.3 Buildings (verified)

- If the building's owner (+0x40) is not the attacker and flags & 6 is
  clear: flags |= 0x1000 (hit), and for the local player
  `*_UNDERATTACK` (limit 300 ticks). Own fire does not trigger it.
- **Survives** when `dmg < hp`, or when it is already dying
  (+0x3B ≠ 0): neutral to hostile; attacker record as for units;
  hp = max(hp − dmg, 0). If hp < max/2 and record flag 0x200: smoke
  +0x3D = (rand() & 0x7F) + 0x40 and +0x4C |= rand() (smoke points).
- **Destroyed**: `*_BUILDINGLOST` for the local player unless kind 0xE
  (wall); counters; `FUN_0049ca10(p, idx, 0)`:
  - death explosion rec+0x4F: its sound at the top-left cell; then
    2 × (occupied cells) explosions, each at a random occupied cell
    (rand() & 3, rand() & 3, redrawn until the bit is in mask +0x54) at
    (cell px + (rand() & 31), + (rand() & 31)), flags 0x20, delay =
    1 if (rand() & 31) ≠ 0 else 0 (the code converts the random value to
    a bool; copy it);
  - if `0x6d6109` (4) ≠ 0, each also throws one debris bullet (verified,
    rand() order as listed): type `0x6d60f1[rand() % 4]` (DEBRIS..DEBRIS4),
    `FUN_00456a30(side, type, delay = (rand() & 15) + 8, firer = the
    building index, from = to = the debris origin, target 0xffff, -1)`.
    The origin is the building's top-left pixel (x, y − record +0x86) plus
    half the frame of its base image (runtime image +0x1a width, +0x19
    height). Then dx = ((rand() & 31) − 16) · 0x2000, dy likewise,
    height = (rand() & 3) << 16, vz = (−2 − (rand() & 1)) << 16 and flag
    0x400 (bounce);
  - record +0x08 ≠ 0 (before the explosion loop): `0x5178d8` = 10, the
    screen shake. `FUN_00446690` runs in the tick block (after
    `FUN_00453b90` / `FUN_00455480`): while the counter is non-zero it
    counts down and, if still non-zero, sets `0x5178d4` = rand() % 5 − 2,
    else 0. `FUN_0042f800` blits the back buffer that many rows lower
    (negative: higher) and fills the uncovered rows with black;
  - +0x3B = 30, state 0x11. The building update counts +0x3B down and
    then removes the building.

### 2.3.1 Falling bullets (flag 0x100, `FUN_0049be60`, verified)

Bullets created with flag 0x100 get flag 2, vz = 0 and the firer object's
+0x3c (altitude) as height; with from = to they are still linked (other
bullets with distance 0 are not). Per tick, after the delay (+0x20)
runs out, instead of the flight counter:

1. vz += 0x2000; the target (+0x1c / +0x1e) = the current pixel;
   height −= vz.
2. Height < 1: unless the bullet has flag 0x400, rand() & 3 ≠ 1 and
   vz > 6553.6 (one rand() only when 0x400 is set), it lands: height = 0,
   impact at the target pixel as for other bullets. Otherwise it bounces:
   vz = −(vz / 2).
3. Not landed: flag 1 arc step, then x += dx, y += dy.

Debris rises 2–3 px per tick at first, so it peaks about 36 px up.

### 2.4 Ground marks (verified)

After the loops, if !air and the impact cell has flag 0x4000, no spice
(0x700000) and no building (0x10): crater byte (cell +9, doc 06) +=
`(pct[warhead][NONE] · damage / 100) / 40`, saturating at 0xFF. When it
passes 16 on a concrete cell (0x800 and 0x8000), the concrete is gone:
0x800 is cleared and the tile becomes `0x4e0af0[rand() & 7]` (tiles 552–
556, 572–574).

### 2.5 Infantry crushed by vehicles (verified)

Each tick, before anything else, an infantry unit whose current cell has
the vehicle bit 0x8 gets flag 0x1000000 and runs `FUN_0049fa50`: every
unit of the cell's owner (cell & 7) in the same current cell within
16 px (Chebyshev, pixel positions) crushes it (`FUN_0049f8f0`):
attacker record, kill counters, animation mode 13, `FUN_0049cde0`,
`S_CRUSHMAN` at the cell, flag 0x400000, and its infantry slot bits are
cleared: the current cell's (slot +0x53) **and, while the step is
reserved (0x10000000), the next cell's (slot +0x52)** (re-read
2026-10-09; our first port cleared only the current cell, so infantry
crushed mid-step left a permanent slot bit that blocked building
placement). `FUN_0049f8f0` does nothing for a unit in state 0x11 or with
flags 0x400140. `FUN_0049fa50` tests every unit of the cell owner (no
kind filter) in the same current cell at Chebyshev distance **< 16** px;
the update clears flag 0x1000000 when the cell has no vehicle bit. Only crushers (record +0x35) can enter a hostile infantry cell
(doc 09 §6), so in practice only they crush.

## 3. Firing

### 3.1 Unit fire: `FUN_00495b90(u, side, idx, tx px, ty px, target side, target idx, kind)` (verified)

`kind` 1 = unit, 2 = building, 0 = ground. Steps:

1. `(ux, uy)` = unit pixel. If it equals the target pixel, do nothing.
2. `f = Facing32(ux, uy, tx, ty)` (`FUN_0045dfe0` = `((atan + 0x40) &
   0xFFF) >> 7`, atan of doc 09 §5.3).
3. Aim facing = turret +0x55 if rec+0x26, else body +0x54. If it is not
   `f`: turn **one step** and return. Body: `FUN_00493eb0` (delay
   rec+0x03). Turret: `FUN_00493f40` (delay rec+0x0F); with flag 0x8000
   (turret locked to the body) the body turn counter is cleared and the
   body turns too.
4. Stealth: if `FUN_0049f5f0` (flag 0x10 set and (flag 4 or +0x31 > 1)),
   `FUN_0049f5c0` and return without firing.
5. Muzzle flash rec+0xA8 (if not 0xFF) at (ux, uy), height 0, flags 0;
   its frame = facing (turret facing if rec+0x18 ≠ −1, else body).
6. Muzzle point: `ux += DX[facing·512 + rec+0x37·32] >> 16`,
   `uy −= DY[...] >> 16` (step tables of doc 14 §1.5), i.e. rec+0x37 px
   along the facing. Same facing choice as step 5.
7. `dist = ftol(sqrt(dx² + dy²))` (x87, as u16) from the muzzle point to
   the target; weapon = `NewBestBullet(prim, sec, dist², target side,
   target idx, kind)` (§3.2).
8. +0x2B = 0.
9. **Scatter**: `n = dist >> 5`. If n ≠ 0:
   `tx += rand() % (8n) − 4n`, then `ty += rand() % (8n) − 4n`. So ±4 px
   per 32 px of distance.
10. Fire sound (bullet +0x0C) at the muzzle cell (`FUN_0046f540`).
11. Target owned by the local player: shooter reveal (doc 16 §3.5).
12. If kind ≠ 1, the bullet gets no target (side −1, index 0xFFFF).
13. `ModelAddBullet(side, weapon, delay 0, firer idx, muzzle, (tx, ty),
    target idx, target side)` (§4.1).
14. flags |= 2 (fired); reload +0x1C = rec+0x0E.
15. Infantry (rec+0x11): animation mode 2 (fire; prone fire if prone).

Each call either turns one step or fires. Callers only call it when
reload = 0, so **turning starts only after the reload ran out**.

### 3.2 Weapon choice: `FUN_0049e9d0` (verified)

`eff(w) = pct[warhead(w)][armour] · damage(w)`, armour = the target's
record armour (unit +0x02, building +0x05), or 0 when kind is 0.

- Target is an aircraft (unit flag 0x40): the primary if it is anti-air,
  else the secondary if anti-air, else an error and the primary.
- No secondary: the primary.
- range(prim) < range(sec): the primary if `dist² ≤ range(prim)²` and
  `eff(prim) > eff(sec)`, else the secondary.
- Otherwise: the secondary if `dist² ≤ range(sec)²` and
  `eff(sec) ≥ eff(prim)`, else the primary.

### 3.3 Range test (verified)

`CanUnitAttackTile(u, cx, cy, air)` (`FUN_00497270`):
`r` = primary range; with a secondary and a ground target,
`r = max(prim, sec)`; for an air target, only anti-air weapons count
(none: false). Then `FUN_004970f0`: from the **centre of the unit's
current cell** (+0x4A/+0x4B) to the centre of (cx, cy):
`dx² + dy² ≤ r²` (inclusive). Ranges are therefore measured cell centre
to cell centre. The building version `FUN_00497140` measures from the
centre of the building's top-left cell
`((x>>16) + 16, (y>>16) − height_px + 16)`.

### 3.4 Target search (verified)

**Spiral.** Tables `0x4eb2d8` dx = (−1, 0, 1, 0), `0x4eb2dc` dy =
(0, 1, 0, −1), `0x4eb2e0` leg growth = (0, 1, 0, 1):

```
x, y = start cell; leg = 1; left = 1; dir = 0
N = (2·r) / 32 + 2
while leg < N:                          // tested after each cell
    x += DX[dir]; y += DY[dir]          // bytes; off-map cells are skipped
    left −= 1
    if left == 0: leg += GROW[dir]; left = leg; dir = (dir + 1) & 3
    test cell (x, y)
```

Legs are 1, 1, 2, 2, 3, 3, … (west, south, east, north). The unit
search does **not** test the start cell. The first hit in spiral order
wins; there is no priority by threat or distance.

`EnemyUnitInRange(px, py, side, r, …, prim, sec, includeCloaked, air,
worms)` (`FUN_004a4900`), `r = 0` means max(prim, sec range):

1. Ground pass: cells with unit bits (`flags & 0x3E8`) whose owner
   (`flags & 7`) is an **enemy** (`alliance[side][owner] == 1`; neutral
   2 is not auto-targeted). The object is `FUN_004a5270(cell centre)`:
   for infantry, the one (not cloaked unless allowed, flags 0x400140
   clear) closest to the cell centre by |dx| + |dy|; for a vehicle, the
   non-infantry unit in the cell that is not kind 2, 5, 8, 9 or 10
   (carryall, worm, frigate, ornithopter, Death Hand). It must not be in
   state 0x11 and must pass `dx² + dy² ≤ r²` from (px, py) to the cell
   centre. Returns its pixel.
2. Air pass (if `air` and a weapon is anti-air): cells with 0x800000;
   enemy aircraft except record 26 (frigate) whose current or next cell
   is there.
3. Worm pass (if `worms`): doc 17 §5.4.

`FUN_004a4fd0` (buildings): same spiral but the **start cell is tested
first**; cells with 0x10, enemy owner; not walls (kind 0xE); with
`turretsOnly`, only kind 0x10 (turrets). Returns the cell centre.

### 3.5 Unit auto-fire: `FUN_00496480(u, side, idx, &tside, &tidx)` (verified)

```
if reload: return 1
if no weapons: return 0
if a target is stored and tside != own side:
    unit target: if CanUnitAttackTile(target's pixel cell): fire (kind 1); return 1
    turret building target: closest cell (FUN_00497390); in range: fire at it (kind 2); return 1
found = EnemyUnitInRange(own cell centre, r = 0, cloaked no, air yes, worms yes)
if found: store it; if it is a worm with a unit on its pixel: return 1
          else fire (kind 1); return 1
found = enemy building search (turrets only)
if found: store it; fire (kind 2); return 1
return 0
```

The unit does not change state and does not move to the target. Firing
here can still take several calls (turning, §3.1).

### 3.6 States (verified)

- **State 2 (guard / idle).** Runs the auto-fire every tick while a
  target is stored, otherwise when `idx & 7 == tick & 7`. Harvesters do
  not scan. Guarding units never chase. **No turret wander here**
  (corrected 2026-10-14: `FUN_00494210`'s only caller is state 3,
  `0x498d9f`).
- **Turret wander `FUN_00494210`** (state 3 at its guard cell, below): a
  turreted unit (rec+0x26) whose turret reached its goal (+0x55 = +0x56)
  draws rand() (line 0x39e); below 31 it becomes the new goal (& 0x1F).
  Then `FUN_00493f40(goal)`.
- **Turret turn `FUN_00493f40`**: flag 0x8000 (record flags, SIEGE_TANK
  only) locks the turret to the body: +0x55 = +0x54 and nothing else.
  Otherwise +0x56 = the goal (every call), one step every rec+0x0f + 1
  ticks, the short way round; at exactly 16 apart it steps +1 when the
  goal is below the facing (the body's rule, `FUN_00493eb0`).
- **Moving** (`FUN_00494260`, `0x4948d7`): after the body turn, a
  turreted unit not at its destination turns the turret toward the
  destination cell (`FUN_0045e020(cur, dest)`, 8-way), every call. So
  tanks aim where they drive; a SIEGE_TANK's cannon simply follows the
  hull.
- **Firing** (`FUN_00495b90`): the turret facing must equal the target
  direction; otherwise the turret turns, and a locked turret (0x8000)
  zeroes the body's turn counter (+0x1a) and turns the hull instead.
- **State 3 (area guard / hunt; AI units start in it, doc 09 §10).**
  Guard cell = +0x50/+0x51.
  - Target stored and not mid-step: a unit target within **6 cells** of
    the unit (`((u − t) >> 21)` per axis, dx² + dy² < 36) that is not a
    worm: aircraft or carried targets are fired at directly when reload
    is 0; others go to `AttackUnit` (chase). Farther: the target is
    dropped. A building target goes to `AttackBuilding`.
  - No target: mid-step, dest = guard cell and step. Off its 8-tick slot,
    step. Else auto-fire (§3.5); if it fired, keep stepping to the
    current dest. Else search from the **guard cell centre** with
    r = 192 px (no air, worms yes); a hit is stored and `AttackUnit`
    runs. Else go back to the guard cell; on arrival (MoveStep 0) infantry
    stand, then, unless the unit can cloak (flag 0x10, `0x498d75`), the
    auto-fire runs again (`0x498d8e`) and, if it does nothing, the turret
    wander (`0x498d9f`).
- **State 4 (attack unit, order 0x14) and `AttackUnit`
  (`FUN_00496b00`).** Mid-step: step. Target gone (slot not a unit,
  state 0x11 or off map): return 0, and the state becomes 7 with
  follow-up 1 and no target (it walks to the last dest). In range
  (`CanUnitAttackTile(target's current cell)`) and not mid-step:
  **dest = own cell** (stop), and fire when reload is 0. Out of range:
  dest = the target's next cell (+0x4C/+0x4D) and step. Range is only
  checked between steps, so units stop on cell boundaries.
- **State 5 (attack building, order 0x15), `FUN_00496d10`.** Engineer
  and saboteur kinds enter (state 0x17). Armed units: closest occupied
  cell from the unit's pixel cell (`FUN_00497390`, squared distance;
  quirk: once a distance of 0 is found, every later occupied cell
  replaces it). In range: dest = next cell, fire at that cell's centre
  (kind 2) when reload is 0. Else dest = that cell; step.
- **State 6 (attack ground, order 0x16), `FUN_00496ff0`.** Target cell =
  dest +0x48/+0x49. Out of range or mid-step: step toward it. In range:
  wait for reload; standing on the cell itself returns 0 (state 2);
  otherwise fire at the cell centre with no target. It keeps firing.
- **State 0xA (attack the blocker, doc 09 §8).** If the dest cell still
  holds units of another side (`flags & 0x3F8` and owner ≠ own), the
  attack-ground step; else restore dest from +0x50/+0x51 and go back to
  the follow-up state.
- **Moving units (MoveStep, `0x4943f3`).** At a step boundary, if
  +0x78 ≠ 0, the unit is in its 8-tick slot or has a target, it is not
  hidden by stealth, the state is not 0xA and it is not a worm, the
  auto-fire runs. +0x78 is the GetNextSquare delay (doc 09 §8), which is
  0 while a unit moves freely; so a moving unit mainly fires while it
  waits. Verified in the disassembly: `0x4943f3 cmp word [esi+0x78], 0`,
  then the `idx & 7 == tick & 7` or target ≠ 0xFFFF test, the stealth
  test `FUN_0049f5f0`, state ≠ 0xA and the worm test before the
  auto-fire call. The test sits inside MoveStep itself, after its
  "between steps and at the destination: return 0" head and before the
  next-square pick, so it applies to **every state that steps** (all 28
  MoveStep call sites: states 3, 4, 5, 6, 7, harvester, capture, repair,
  etc.), not only to state 7. Corrected 2026-10-16: our port had it in
  the state-7 handler only (before MoveStep's head, so it also fired at
  the destination); it now sits in `move_step` (worm: rec+0x2d = 5, read at
  `0x77e27d` = record base `0x77e250` + 0x2d).

Orders (`FUN_00457810`, type − 0x13 switch): 0x14 attack unit = state 4,
+0x14 = 1, +0x26 = target side, +0x0C = target index, for each selected
unit that can take orders, is armed and is not the target itself (target
flags 0x400140 must be clear). 0x15 attack building = state 5 (same
fields). 0x16 attack ground = state 6, dest = the cell (armed units).

### 3.7 Timing (verified)

- The unit reload counter +0x1C drops by 1 at the top of every unit
  update, before the state code. With reload R and no turning, shots are
  exactly R ticks apart. Turning adds ticks after the reload ran out.
- A new target found by a guard scan (8-tick slot) is shot in the same
  call if the unit already faces it. Later calls happen every tick
  because the target is stored.
- Per player the update order is units, buildings, bullets, explosions
  (`FUN_00458e90`), side 0 first. Bullets are appended to the shooter's
  list, so a bullet fired this tick is updated in the same tick. Each
  list walk reads the next pointer before the update, so an object
  appended during the pass is updated in the same pass unless the object
  being updated was the list's last one when its update began. Objects
  of a side whose pass is over this tick (e.g. a side-0 explosion made
  during side 1's pass) wait for the next tick. Ours does the same since
  2026-10-16 (`World::tick`; before, all units ran first, then all
  buildings, bullets and explosions).

### 3.7a The head of the unit update (`FUN_00497b90`, verified 2026-10-16)

Every unit runs this head before its state switch: aircraft, worms and
units in state 0x11 too. In order:

1. Infantry only (the record +0x11 test): the crush test (cell flag 8:
   flag 0x1000000 and `FUN_0049fa50`, §2.5; else the flag is cleared),
   then the animation tick `FUN_004940d0` (§3.8). No other unit calls
   `FUN_004940d0`. A soldier crushed here goes on through the head and
   the state-0x11 case in the same update (its countdown already drops).
2. Self-repair (flag 0x800000, doc 37 §1.5).
3. Saboteur charge (kind 4, every 16th tick, doc 32).
4. Reload +0x1C −= 1 if not 0.
5. `FUN_0049f610` (stealth; clears flag 4 of a unit without 0x10).
6. Attacker check: +0x32 ≠ 0xFFFF and that object (of side +0x30) is a
   unit or building in state 0x11 → +0x32 = 0xFFFF (the side stays). A
   freed slot (type 0) only logs "Attacked by strange type" and keeps it.
7. Target check, not for carryalls (§3.7b).
8. Damage smoke (doc 43 §2.2).
9. +0x2C, not for aircraft.
10. Flag 1: +0x2A = 0, flag 1 cleared, deviator −30 (doc 40); else every
    8th tick +0x2A += 1 (not carried), flag 8 cleared above 100, the
    deviator countdown and revert.
11. Flag 2: +0x2B = 0, flag 2 cleared; else every 8th tick +0x2B += 1.

Before 2026-10-16 ours ran the crush test last (a crushed soldier lost
one countdown step), skipped steps 3–5 and 9–10 for aircraft, worms and
dying units (and step 7 for aircraft), and had no step 6.

### 3.7b Target drop each tick (verified, 0x497d93)

At the top of every unit update (not carryalls), after the infantry
animation: a unit target (+0x0c) is dropped when the target is dying
(0x11), or has flags 0x400104 (4 = a submerged worm) and the alliance
table value is not 0, or is carried (0x100 without 0x40). A building
target is dropped when the building is dying. So units stop shooting a
worm the moment it goes under.

`FUN_004a5270` (object in cell) searches only the units of the cell's
owner (owner bits 0..2): a vehicle when bit 8 is set, else that owner's
infantry nearest to the point (Manhattan, < 0x800 px). Owner bits are not
cleared when the last unit leaves a cell, so a stale owner just finds
nothing.

### 3.8 Infantry animation (verified)

Drawing (`FUN_00428e30`): image = element·35 + frame (+0x1e), rotation
= rec+0x3c[facing], **except when the mode +0x20 > 8: rotation = mode −
9**. The rotation slots of frames 22+ hold the different death / crush
animations (crush = mode 13 = slot 4).

Mode table `0x4e9530` (u32 first, last): 0 (0,0) stand, 1 (1,6) walk,
2 (7,11) fire, 3 (12,12), 4 (13,13) prone, 5 (14,16) crawl, 6 (17,21)
prone fire, 7 (12,12), 8 (7,11), 9 (22,33), 10–15 (22,29).

`FUN_004940d0` each tick: a counter +0x25 advances the frame every 3rd
tick. When the frame leaves the mode's range:
- modes 9..13 (death, crush) hold the last frame;
- just hit (+0x2A = 0): with `rand() % 5 == 0` go prone (+0x24 = 1,
  mode 4);
- prone and +0x2A < 40 (320 ticks since the last hit): stay prone;
- else stand up (+0x24 = 0, mode 0);
- +0x2A = 255 (long idle): `rand() % 400 < 2` makes an idle move: turn
  to facing 8 or 24 when the facing is below 8 or above 24, else mode
  = 14 or 15.

Firing sets mode 2 (`SetInfAnim`, +4 when prone = 6). Death sets mode
9 + infdeath: W_BULLET → 10, W_AT / W_GT_AT / W_BAZOOKA → 11, W_HE /
W_SONIC → 12, crush → 13. Modes 9–15 all use the record's one element
(+0x0C): `FUN_00428e30` (`BlitVehicles`) takes image
`0x71aca0[(element · 35 + frame) · 32 + rot]` with rot = mode − 9 when
mode > 8, so the modes differ only by the rotation slot of frames 22+
(verified; doc 45 for the idle modes 14 / 15).

## 4. Bullets

### 4.1 Creation: `FUN_00456a30(side, type, delay, firer, x0, y0, tx, ty, tidx, tside)` (verified)

Object (list type 3, player list +0x18):

| Off | Meaning |
|---|---|
| +0x00 | ticks left |
| +0x04 | flags (table +0x08) |
| +0x08 / +0x0C | dx / dy per tick (16.16) |
| +0x10 | speed |
| +0x14 | firer index (0xFFFF when it died) |
| +0x16 / +0x19 | target index / side (homing) |
| +0x18 | type |
| +0x1A | owner side |
| +0x1B | table +0x13 |
| +0x1C / +0x1E | target pixel (u16) |
| +0x20 | start delay (not drawn, not moved) |
| +0x24 / +0x28 | x / y (16.16) |
| +0x2C | height, starts at 0x60000 (6 px) |
| +0x34 | vertical speed |
| +0x44 / +0x45 / +0x46 | frame / rotation / last rotation |

```
dist  = ftol(sqrt((tx − x0)² + (ty − y0)²))      // x87, int px
ticks = (dist << 16) / speed                     // signed
if flag 0x800: ticks = (range << 16) / speed
if dist == 0: if !(flag 0x100) return 0xFFFF     // object not linked: leaked (below)
              dx = dy = 0
else: dx = speed · (tx − x0) / dist; dy likewise (int32)
if flag 1:    vz = −(ticks · 0x2000) / 2; flags |= 2
if flag 0x100: flags |= 2; height = firer altitude (unit +0x3C); vz = 0
if flag 0x20: ticks ·= 2
frame = rot = 0 if frames < 2 or flag 0x80, else Facing32(x0, y0, tx, ty) / (32 / frames)
```

The object comes from `FUN_0046bd40`, which pops the head of the side's
free list (`player+4`, `0x46bd40`); only `FUN_0046bd60` links it into a
list. The `dist == 0` return skips the link, so that object is lost to
the side's pool for the rest of the mission (verified; ours counts it in
`pool_leaked_`, doc 50).

LMG and HMG have speed 351 px/tick, so `ticks = 0` within their range:
they hit on their first update without moving and are never drawn.
A bazooka at 96 px flies 10 ticks (ticks = 10, impact on the 11th
update).

### 4.2 Update: `FUN_0049be60` (verified)

1. Firer check: if the firer slot is not a live unit or building
   (state 0x11), firer = 0xFFFF.
2. Start delay: if +0x20 ≠ 0, decrement and return.
3. Off the map (x or y negative, or cell ≥ map size): removed, no impact.
4. Flag 0x1000 and the current cell has 0x400: damage at the current
   pixel and height, impact explosion, removed.
5. Flag 0x800: if 2 · ticks < (range << 16) / speed, damage at the
   current pixel (every such tick, no explosion).
6. Flag 0x10: trail explosion at the current pixel, at the bullet's
   height.
7. Flag 0x20: homing step (§4.3), return.
8. Flag 0x80: frame = (frame + 1) % frames.
9. Flag 0x100: vz += 0x2000; target pixel = current pixel; height −= vz.
   At height < 1: bounce (vz = −vz / 2) if flag 0x400 and
   `rand() & 3 ≠ 1` and vz > 6553.6 (rand() is only called with 0x400);
   else height = 0 and impact.
   Without 0x100: impact if ticks = 0, else ticks −= 1.
10. No impact: flag 1: vz += 0x2000, height −= vz. Then x += dx,
    y += dy. Return.
11. Impact (bullet still on the map): SPICEEXPL marks spice around the
    target cell. If the **target pixel** (+0x1C/+0x1E) is on the map:
    `DamageTiles(tx, ty, height, …, deviate = flag 0x40)`. Then the
    impact explosion's sound at the target cell and the explosion at
    (tx, ty) with height `height >> 16` passed into a 16.16 field, so it
    sits on the ground (original quirk; keep it). Removed.

Non-homing bullets always land on the (scattered) target pixel, not on
their own position. Arc height after k moves:
`h = 0x60000 + 0x1000 · k · (ticks − k − 1)`.

### 4.3 Homing (flag 0x20, verified)

- Target known: target pixel = the target unit's pixel, goal height =
  its altitude. If it is in state 0x11, the target is forgotten (the
  last pixel stays).
- `d2` = squared cell distance bullet → target point. Turn steps per
  tick: d2 ≥ 17: 1; 15–16: 2; 13–14: 3; 11–12: 4; 9–10: 6; 7–8: 8;
  < 7: 32.
- Goal rotation = `atan(x − tx, ty − y) / (0x1000 / frames)`; turn up to
  that many steps the short way round. If the rotation changed by
  exactly 16 (a U-turn) and ticks ≠ 0, ticks = 1.
- Move by the step tables at facing `rot · (32 / frames)` and step
  `speed · 32 >> 16`. Off the map: removed.
- Height moves half-way to the goal height, at most ±2 px per tick.
- ticks −= 1. While ticks was ≠ 0 and the bullet is not in the same
  8 px block as the target (`x >> 19`, `y >> 19`), continue. Otherwise
  impact **at the bullet's own pixel and height**, explosion at that
  height (16.16).

### 4.4 Drawing: `FUN_00428070` (verified)

Per player, per bullet with no start delay: image
`0x714b00[type·32 + frame]`; screen x = (x >> 16) − off_x − scroll_x;
y = (y >> 16) − off_y + top − scroll_y − (height >> 16). Flag 0x800:
sonic wave blit `FUN_0042ada0`. Flag 2: shadow (`FUN_00434620`) at the
ground position (no height). Then flag 0x200: additive blit, else the
normal keyed blit (1, 1). The world draw calls it right after
`FUN_00428e30` (pass list in doc 16, corrections), so bullets are drawn
over units, explosions and aircraft, and under the shroud.

## 5. AI and return fire (verified unless marked)

- Being hit records the attacker (+0x30/+0x32) but a unit's own state
  code never reads it. Human units fight back only through the scans of
  §3.6: a guarding unit hit from outside its weapon range does not
  react.
- The AI reads it in `FUN_0041f670` ("DoStopStartStuff_AI"): for its
  team units not in state 3 with a non-allied attacker, an armed unit
  gets an attack order on the attacker (`FUN_00420010`, flag 0x100000);
  an unarmed unit in state 2 is sent away (`FUN_0049e290`, then a move
  order). It drops a chase when the target is out of range by more than
  `0x6b885c`. The AI side (DoStopStartStuff, the chase drop, the
  teams) is in doc 53 §7 and docs 22 / 23.
- AI-owned units start in state 3 and hunt within 6 cells of their guard
  cell (§3.6).

## 6. Buildings with weapons (verified)

### 6.1 Update (`FUN_0049b7f0`)

- Reload +0x37 drops by 1 every tick.
- Target check: a unit target in state 0x11, with flags 0x200004, or
  carried (0x100 without 0x40) is dropped; a building target in state
  0x11 too.
- State 2: no primary weapon: nothing. Off the 8-tick slot
  (`idx & 7 ≠ tick & 7`) with no target: nothing. Else
  `FUN_00496790`:
  ```
  if reload: return
  stored enemy unit target in range (FUN_00497140, its pixel cell): fire
  stored enemy turret target in range (closest cell): fire at that cell
  else EnemyUnitInRange(top-left cell centre, r = 0, air yes, worms yes)
       else building search (turrets only when the owner's AI byte
       0x4f4118[side·0x1DB8 + 1] is 0: "AI active", doc 22 §1; read at
       `0x4967b9`, set to 1 only by `0x469e2f` and `0x46ea0e`)
  fire, store the target
  ```
- State 4 (ordered attack on a unit, `FUN_004975d0`): in range (air
  when the target has flag 0x40): fire when reload is 0; out of range
  or gone: state 2. State 5 likewise for buildings (`FUN_004976b0`).
  State 6: fire at the cell +0x14/+0x15 when in range (`FUN_00497090`).

### 6.2 Building fire: `FUN_00496030` (verified)

1. Origin = centre of the top-left cell `((x>>16) + 16, (y>>16) −
   height_px + 16)`; `f = Facing32(origin, target)`.
2. Fires only if record +0x0F ≠ 0 and (power percent `player+0x245b0`
   > 99 or record +0x4E = 0). So the **medium turret works without
   power**; the **large turret neither turns nor fires below 100 %**.
3. Turret facing +0x21 ≠ f: turn one step (`FUN_00493fd0`: counter +0x36
   reloaded from record +0x06; 0 = one step per tick; the short way,
   ties as in doc 09 §5.2) and return.
4. Weapon choice with the stored target (+0x3C side, +0x2C index,
   kind 2 for the armour lookup), scatter as for units (§3.1 step 9).
5. Muzzle flash record +0x8C (if not −1) at
   `x = (x>>16) + (i8)rec+0x60 + h`, `y = (y>>16) + (i8)rec+0x61 − h`,
   h = byte +0x1A of the turret image `0x6e5c40[rec+0x18·32 +
   rec+0x64[facing]]` >> 1; frame = turret facing. +0x1A is the frame
   width (§7b).
6. Fire sound, shooter reveal (doc 16), bullet from the origin (no
   muzzle offset), reload +0x37 = record +0x07.

There is no "fired" flag and no animation for buildings.

## 7. Unit and building fields used by combat (verified)

| Unit off | Meaning |
|---:|---|
| +0x0C / +0x26 | target index / side (0xFFFF / 0xFF none) |
| +0x1C | reload counter |
| +0x24 | prone (infantry) |
| +0x25 | animation tick counter (infantry) |
| +0x27 | damage smoke timer |
| +0x2A | ticks / 8 since last hit (0..255) |
| +0x2B | ticks / 8 since last shot |
| +0x2D / +0x2E | deviator: original side / timer |
| +0x30 / +0x32 | last attacker side / index |
| +0x56 | idle turret goal facing |
| flag 1 / 2 | hit this tick / fired this tick (cleared by the next update) |

| Building off | Meaning |
|---:|---|
| +0x21 | turret facing |
| +0x24 | hit points |
| +0x2C / +0x3C | target index / side |
| +0x30 | state |
| +0x36 / +0x37 | turret turn counter / reload counter |
| +0x3B | death timer (30) |
| +0x3D / +0x4C | smoke timer / smoke points |
| +0x40 | owner |
| +0x41 | ticks / 8 since last hit (flag 0x1000 resets it) |
| +0x50 / +0x52 | last attacker side / index |

## 7b. Notes (2026-10-09)

- **Air pass details** (`FUN_004a4900`, re-read): the air and worm
  passes run only when the caller passes r = 0. A caller with a fixed
  radius (guard, r = 192) gets the ground pass only, no worms either.
  Air pass range: both weapons anti-air → the larger range; only the
  primary → its own range; only the secondary → the ground pass's
  max(prim, sec) (quirk, kept). A cell needs flag 0x800000; the sides
  are scanned 0..7 (enemies only), each unit list in order; an aircraft
  (flag 0x40, not the frigate record 0x6d60e6) matches when (+0x4c or
  +0x4a) equals the column and (+0x4b or +0x4d) equals the row, so
  mixed current / next pairs count. No state or cloak test.
- **Homing step** (§4.3) ported in full, including: speed reloaded from
  the table; the move by the flight step tables at facing
  `(32 / frames) · rot`, step `speed · 32 >> 16`; the off-map removal
  after the move; the height step only while a target altitude is known
  (≠ 0); impact damage and explosion at the bullet's own pixel and
  16.16 height. Trail explosions get the bullet's 16.16 height (the
  plain impact still passes `height >> 16`, so it sits on the ground).
- **Unit death effects** (§4.6 of doc 14): aircraft explosions get flag
  0x1000 and move once by (DX, −DY)[facing][speed >> 11]; the death
  explosion sits at the unit's altitude.
- **Turret muzzle flash** (§6.2): the runtime image object is the R16
  entry header without the type byte (+0x08/+0x0C offsets, +0x14
  palette, +0x18 bits per pixel, which the draw code compares with 8),
  so +0x1A is the **frame width**. The flash is at
  `x + (i8)rec+0x60 + w/2`, `y + (i8)rec+0x61 − w/2`, w = frame width
  of turret image `rec+0x64[facing]`; its frame (+0x01) = the turret
  facing.

## 8. Open points

Closed 2026-10-16:

- Bullet flag 4 and table byte +0x13 have **no effect**. The table's
  flags column (`0x782070`) is read only by AddBullet (`0x456af5`, a
  scan of every access to the table); in the bullet object, flag 4 is
  tested nowhere in the creation (`FUN_00456a30`), the update
  (`FUN_0049be60`) or the draw (`FUN_00428070`). Bullet +0x1B is written
  at `0x456b0d` and no bullet code reads it (the other `[reg+0x1b]`
  reads are unit fields and the unit debug dump `0x459327`).
- Auto-fire while moving: verified (§3.6).
- Infantry modes 9–15: verified (§3.8).
- Deviator: doc 40. Building debris arguments: verified in
  `FUN_0049ca10` (`(side, DEBRIS type, (rand() & 15) + 8, building
  index, origin, origin, 0xFFFF, −1)`, the dx / dy / height / vz
  rand() calls only when AddBullet returned an object). The `dist == 0`
  bullet: leaked from the pool (§4.1).
- AI return fire and teams: doc 53.
- KillUnit and the delivery table: §2.2.1.
- Not covered: sonic tank wave blit, Death Hand, ornithopter bombs,
  Sardaukar/Fremen two-weapon cases beyond §3.2, crates (`FUN_0049ebf0`).
