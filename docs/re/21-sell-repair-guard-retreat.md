# Sell, repair, guard, retreat (1.06)

What happens after the sidebar repair / sell / guard / retreat actions.
The clicks, modes (`0x516438`) and cursors are in doc 10 §4–§5; the
controls 4 / 5 / 7 / 8 in doc 11 §5. All four actions are **orders**
posted with `FUN_0045bf00` and run by ProcessOrder (`FUN_00457810`,
`switch(order − 0x13)`) on the next order pass, like a move (doc 09).

## 1. Functions

| Address | Role |
|---|---|
| `FUN_0045a150(side, bldg)` | post order **0x22**, toggle repair |
| `FUN_0045a1a0(side, bldg)` | post order **0x23**, sell (not when the local player has lost, `0x6b8278`) |
| `FUN_00459690(side, unit)` | post order **0x21**, unit to the repair pad |
| `FUN_00459980(side)` | post order **0x18**, guard; ids = selected, not dying, armed (rec+0x0c ≠ −1), at most 64 |
| `FUN_00459ab0(side, x, y)` | post order **0x1a**, retreat to (x, y); ids = selected, not dying, at most 64 |
| `FUN_00456fb0` | order 0x22 handler: building flags `+0x28 ^= 1` |
| `FUN_00456ee0` | order 0x23 handler: start selling |
| `FUN_0044a9f0` | `DoRepairs`: per-tick building repair |
| `FUN_0049b7f0` | building update (doc 12 §7): selling countdown, survivors, removal |
| `FUN_004977a0` | release survivors (infantry) from a sold or destroyed building |
| `FUN_004979a0` | free drivable cell in the 3×3 around a cell (random pick) |
| `FUN_00458e90` | per-player update loop: refund on removal of a sold building |
| `FUN_0046e3f0` | retreat destination |
| `FUN_004a57c0` | any own unit selected |
| `FUN_004a58b0` | any own armed unit selected |

## 2. Building repair

### 2.1 Click (`FUN_004433a0`, mode 2, `0x445…`)

On an own building whose record has flag **0x200** (rec+0x58) and hp <
max (rec+0x00): post order 0x22. No sound or voice on the click
(the BUTTON sample is played by the sidebar toggle only). The handler
(`FUN_00456fb0`) **toggles** building flag 1, so clicking a building that
is being repaired stops the repair. Repair mode stays on after the click
(verified 2026-10-16: the mode-2 branch `0x4451a4`..`0x44534c` ends at
`0x445d99` without writing `0x516438`).

Details (verified 2026-10-16):

- The click and the cursor use the **current** mouse position and must be
  in the playfield; the building is `FUN_004a5640` of that cell, which
  also finds a dying building (state 0x11) until its cells are cleared.
  Cursor `0x443b44`: own, hp < rec hp, rec flag 0x200 → 0xb, else 8. The
  mode branches come before the shroud test of the cursor (`0x4434f5`
  tests the mode first), so the repair / sell cursors also show over
  shroud.
- The handler tests nothing but `FUN_004a4740(side, index)` (an active
  side, the slot holds a building): it toggles flag 1 of any building of
  the poster, also at full hit points (DoRepairs then clears it).
- A drag box released in this mode box-selects and sets mode 0 (doc 12
  §1); a release on the build lists or on the radar is handled first and
  keeps the mode.

Record flag 0x200 is the general "real building" flag: it also gates
building selection (doc 10), the repair cursor 0xb (doc 10 §5) and the
survivors (§3.4). Walls and concrete do not have it.

### 2.2 DoRepairs (`FUN_0044a9f0`, once per tick)

One player per tick: `p = tick & 7`, and only if `p <= 0x798628` (the
player count; note `<=`, so one slot past the last player is also
visited; it has no buildings). So **each player's buildings repair every
8 ticks**.

```
R = 0x6b8848                       // VARS.BIN +0x30 = 12 (1.06)
for b in player p's building list (+0x10, next +0x7c):
    if !(b.flags & 1): continue
    max = rec+0x00 (hit points)
    if b.hp >= max: b.flags &= ~1; continue
    cost = (uint)(BuildingCost(rec, 0, p) * R) >> 1;   // u32 product, then >> 1
    cost = cost / max                                  // unsigned, truncating
    if cost == 0: cost = 1
    if (uint)(cash + spice) < cost:                    // +0x2425c + +0x24254
        b.flags &= ~1                                  // stop: no voice
    else:
        Spend(cost, "DoRepairs()")                     // FUN_0046c720
        b.hp += R
        if b.hp >= max: b.hp = max; b.flags &= ~1
```

- `BuildingCost` = `FUN_00442be0(rec, 0, side)` = rec cost (+0x1c) ×
  `FUN_004429b0(side)` / 100. The factor is the difficulty for the local
  player in single player (0 → 75 %, 1 → 100 %, 2 → 125 %) and 100 % for
  every other side.
- So a full repair costs about **half the building's price**, at
  12 hp per 8 ticks (1.5 hp per tick).
- Nothing checks power, an attack in progress, build-up or selling:
  a building keeps repairing while it is hit. Neither the click nor
  DoRepairs tests flags 2 / 4 / state 0x11 (verified 2026-10-16): a
  building on partial concrete (hp < max) can be set repairing during its
  build-up, and so can a selling or dying one, which then pays for repair
  steps until it is removed.
- The money check uses cash + spice; Spend takes from both
  (doc 11, `FUN_0046c720`).
- `FUN_004544e0` runs once at start-up (`FUN_0044a870`, `0x44a8ee`). Its
  text branch (`%s.dat`, defaults 0x6b8848 = 12, 0x6b884c = 10) runs only
  when `0x4eb014` = 0; that byte is 1 in the exe and never written (only
  read), so the shipped game takes the other branch: `bin\vars.bin`
  (`0x4de740` "bin\%s.bin" with `0x4e3274` "vars"), 0x50 bytes read to
  0x6b8818 (verified 2026-10-16). So VARS.BIN is what the game uses (doc
  13 §7): +0x30 = 12, +0x34 = 8.

### 2.3 Repair icon (`FUN_00428e30`, `0x429c3e..`)

While flag 1 is set and `(building +0x8c (index) + tick) & 0x20` ≠ 0 —
32 ticks on, 32 off — the draw list adds fixed DATA.R16 **entry 3**
(`0x6dc468`, a wrench) centred on the building image:

```
img = the building's drawn base image (or the element's first image)
x = draw_x + (img.w / 2 − img.x_off) − icon.w / 2
y = draw_y + img.h / 2 − img.y_off − icon.h / 2
```

(all `int / 2`; `0x429c81` / `0x429cf7`). Doc 10 §8 calls entry 3 a
"flashing target marker"; it is this repair wrench. Verified 2026-10-16:
x / y is the draw item's position, which has **no** turret offset (rec
+0x60/+0x61 is added to the body and turret only), and the icon is
blitted at that top-left without its own header offsets; img is the body
image the item drew (the damaged one below half hp), else the base
element's first image (`0x6e5880[rec+0x14]`). (Our wrench added the
turret offset before.) The `0x782058` icon (DATA.R16 entry 110) at
(x+2, y+6) of the selection bracket (`0x42a12b`) is drawn for the local
player's selected building when its upgrade level for the building's name
(`+0x24544[rec+0x88]`) is non-zero, so it is the **upgrade** marker, not
a repair icon; the same overlay draws the primary marker (entry 18,
`0x6e2df4`, flag 0x80) at (x+2, y − h(right) − 4 + H) (`0x429f69`).
Both are drawn now. Which picture each entry holds comes from the data,
so the reference game is not needed for these rules.

## 3. Selling

### 3.1 Click (mode 3)

On an own building with `flags & 6 == 0` (not building up, not already
selling): post order 0x23 (`FUN_0045a1a0`). Otherwise doc 10's capture
path (dead in retail, doc 30 §0). The mode stays on after the click
(verified 2026-10-16: `0x445351`..`0x445462` never writes `0x516438`). The
cursor is 10 over any own building, else 7.

### 3.2 Order handler (`FUN_00456ee0`)

```
b = BuildingByIndex(side, id)                 // FUN_004a4740: active side, a building slot
if b and !(b.flags & 0x1000000) and b.state (+0x30) != 0x11:
    n = 0x714a98[rec] * 3                 // build-up frames × 3 (u8 arithmetic)
    b.buildup (+0x3e) = n
    if n == 0: b.flags |= 8               // no build-up animation: remove now
    else:
        b.flags |= 4                      // selling
        b.buildup = n − 1
        b+0x40 = side; b+0x44 = tick; b+0x42 = id; b+0x35 = 0
    if side == local and rec kind (+0x5c) != 0xe:
        voice *_SELL (S_SELL / H_SELL / O_SELL by house, FUN_0046f900)
```

Flag 0x1000000 blocks selling: it is set only when an engineer or
saboteur starts walking in (`0x496f5a`, doc 30 §2.3) and cleared only by
the capture (`0x46d8da`); verified 2026-10-16 by an immediate scan. Kind
0xe (no SELL voice) is the wall (records 17..19 in Templates.bin). The
handler does **not** test flags & 6 (only the click does): a second
order restarts the countdown, and the AI calls the handler directly. It
also clears +0x35, so a sold building is no longer selected (we kept it
selected before).

### 3.3 Countdown (`FUN_0049b7f0`, every tick)

With flags & 6 = 4 (selling, not building up):

```
if b.buildup == 1 or (rec kind == 2 and b.buildup == 60):
    SetState(b, 0x11)                     // FUN_0049e120: refused if already 0x11
    b.flags |= 8
else:
    b.buildup -= 1
```

So the build-up animation plays **backwards** at one counter step per
tick: 3 ticks per frame, the same speed as building up (draw: doc 12
§7.1 uses `+0x3e / 3`). Kind 2 is the construction yard (`0x49b86f` /
`0x49b879`): a sold yard stops at 60, frame 20 of its MCV-unpacking
animation, every other building at 1 (verified 2026-10-16).

Later in the same call:

- **Survivors:** if `!(flags & 0x80000)`, flag 4 set, and
  `buildup / 3 == rec+0x89` (the frame from which the base is drawn):
  flags |= 0x40000; then `FUN_004977a0` runs and 0x40000 is cleared
  (§3.4). `FUN_004977a0` sets 0x80000, so it runs once.
- **Flag 8** without 0x20000: `FUN_0044b1d0` = SetState(0x11) and flags
  |= 0x20000 ("sold"), and the update returns. Nothing else this tick.

Next tick the building is in state 0x11 (the selling branch runs again
but changes nothing). The state 0x11 case: `+0x3b` (the death countdown,
0 for a sold building) is 0, so at once:

1. if rec+0x88 == `0x6d60e0` and `0x6d60ee` ≠ −1: area damage at the
   building (`FUN_0049d2e0`). Never in 1.06 (verified 2026-10-16): the
   Templates.bin ids byte `0x6d60e0` is 0xff and no building name index
   is 0xff (`0x6d60ee` = 27, BARRELHIT).
2. `FUN_0046d9a0`: each primary slot that pointed at this building gets
   `FUN_0046e740`'s pick, with flag 0x80: the first building in the
   side's list (creation order) with the **same name** (+0x88) and
   another index, whatever its state (a dying one too); 0xffff if none
   (the Starport slot then also runs the "Starport lost" code). Verified
   2026-10-16; we matched by kind and skipped dying buildings before (the
   kinds differ from the names for kind 11: BARRACKS and SIETCH).
3. if `!(flags & 0x10000)`: `FUN_0044dee0`: the player's group count
   (+0x245c1[rec+0x88]) − 1, the cells are cleared (`FUN_0044df60`), and
   for rec flag 0x200000 `FUN_0044f2f0`, the skirt tiles (doc 12 §6
   step 15).
4. returns 0: the player loop removes it.

### 3.4 Survivors (`FUN_004977a0`)

Also called for a **destroyed** building when its death countdown +0x3b
is 0x19 and `!(flags & 0x10000)`.

```
if !(rec flags & 0x200) or (b.flags & 0x2000): return
b.flags |= 0x80000
cx = b.x px >> 5;  cy = (b.y px − rec+0x86 (height px)) >> 5    // top-left cell
inf = the first unit record with the infantry byte (rec+0x11) set
count = (BuildingCost(rec, 0, side) / 3) / UnitCost(inf, side)   // unsigned
if count > 3: count = 4
tries = 0
repeat count times:
    loop:
        r = rand()
        x = cx + (r & 3);  y = cy + ((r & 0xf) >> 2)
        (fx, fy) = (x, y); found = FUN_004979a0(&fx, &fy, side)
        tries += 1
        if (footprint (rec+0x54) bit (r & 0xf)) and found
           and cell(x, y) & 0x3e8 == 0: break
        if tries >= 20: break
    if tries < 20: CreateUnit(side, inf, x, y, fx, fy)    // FUN_00455870: spawn on the cell, walk to (fx, fy)
```

`tries` is shared by all survivors, so after 20 failed tries no more
come out. `FUN_004979a0(&x, &y)`: scans rows y−1 .. y+1 (end exclusive
at min(y+2, h−1)) and columns x−1 .. x+1 (end exclusive at
min(x+2, w−1)), start clamped to 0, for cells with `flags & 0x3f8 == 0`
and drive bit 0x2000; each hit is stored, then `rand() & 1` decides
whether to stop there. Returns 1 if the stored cell changed.

Example: a construction yard (cost 3000 at 100 %) and light infantry
(cost 50 after the factor; rec 0 in Templates.bin is LIGHT_INF, the first
record with the infantry byte, verified):
3000 / 3 / 50 = 20 → 4 survivors.

### 3.5 Refund (`FUN_00458e90`, when the update returns 0)

```
if b.flags & 0x20000:                     // sold
    c = BuildingCost(rec, 0, player.house_side)
    if rec kind == 4 (refinery):
        c -= UnitCost(0x6d60e8 & 0xff, side)     // the free harvester
    Refund(c >> 1)                         // FUN_0046c7b0: pending cash +0x24260
```

So the player gets **half the price** (minus half a harvester for a
refinery), at the end of the animation, into pending cash, which then
counts up on the credits display like harvested money (doc 11). HP does
not matter. `0x6d60e8` is the harvester record (Templates.bin ids byte
0x10 = 8 = HARVESTER, verified). Both costs use `player+0x24252`, the
side's own index (as `FUN_0046d9a0` passes it to `FUN_004a4660`).

Destroyed buildings (no 0x20000) get no refund.

### 3.6 Other effects

- Power, storage, build lists: recomputed from the building lists (doc
  11), so they change when the building leaves the list.
- Production in a sold factory: no special code here. While the sold
  building counts down it is still in the list, so the slot keeps it;
  after its removal the next slot pass (`FUN_004467e0`, doc 12 §8.2)
  finds no factory of that kind and **clears the slot without a refund**,
  or uses the next factory of the kind if there is one (the primary slot
  was moved by step 2). Verified 2026-10-16.
- Docked harvesters: nothing in this path. A docked harvester (states
  0xe / 0xf) checks its refinery every update (doc 13 §1.4 / §1.5): the
  refinery slot stays valid while it counts down, so unloading goes on
  until the removal, then the harvester undocks (state 0x10). No unit is
  ever inside a building (there is no garrison).

## 4. Unit repair (repair pad, kind 6)

**Click (mode 2):** on an own non-infantry unit with hp < max, if the
player owns a kind-6 building (`FUN_00442910(side, 6)`): the unit's
acknowledge voice (`0x77e2d0[(house·3 + tick % 3) + rec·0x40]`),
`FUN_0049f310` (unit +0x1d = 7, a short flash), order 0x21.

**Order 0x21** (case 0xe): every listed unit that takes orders
(`FUN_0049f4a0(u, 0)`) and is not infantry gets SetState(0x18),
+0x14 = 1, +0x5a = the pad's index.

**State 0x18** (go to the pad, switch case 0x16):

- if +0x5a is not a kind-6 building: the nearest kind-6 building
  (`FUN_004a5f80`, as for refineries, doc 13); none → state 2 between
  steps;
- dest = pad top-left + (rec+0x60, rec+0x61); MoveStep;
- arrived (on the cell, or stepping into it): `*_REPAIRUNIT` (local
  player), `*_LOWMONEY` when cash + spice = 0, then SetState(0x19);
- far away (dist² > `0x6b8844` = VARS +0x2c = 64 cells²) and between
  steps (+0x4f = 0, no 0x10000000): carryall request. Verified 2026-10-16:
  case 0x16 jumps to the harvesters' request tail `0x49a781`
  (`FUN_0046cca0` / `FUN_0046cd00`, then state 0x12), doc 33 §2.1; ported
  as `carry_request(u, vars.repair_carry_distance)`.

**State 0x19** (on the pad, case 0x17), every tick between steps:

```
if hp == max:
    +0x14 = 2; local: *_UNITREPAIRED; SetState(0x1a)
elif the cell is an own building (FUN_004a5640):
    c = (UnitCost(rec, side) * R2) / max_hp     // R2 = 0x6b884c = VARS +0x34 = 8; u32
    c = (c & ~1) < 2 ? 1 : c >> 1
    if Spend(c):
        pad.flags |= 0x20                        // one animation cycle
        hp = min(hp + R2, max)
        local, not full: cash + spice == 0 → *_LOWMONEY once (0x516480),
                          reset when cash + spice > 99
else: SetState(0x1a)
```

So units gain **8 hp per tick** and a full repair costs about half the
unit's price.

**State 0x1a** (leave the pad, case 0x18, verified): +0x25 counts down;
a free adjacent cell (`FUN_0045dee0`, mode 0) becomes the destination
and the unit goes to **state 8** (the leave-a-building move). With none,
when +0x25 is 0: two rand() calls pick a random cell of the 3 x 3
(x first, `% 3 − 1`), the unit there (`FUN_004a5270`) of an allied side
(alliance = 0), not this unit, that takes orders gets state 9, and
+0x25 = 50.

**Details verified 2026-10-07:** state 0x18 with an invalid +0x5a takes
the nearest kind-6 building from `FUN_004a5f80` (Manhattan, u16, to the
building's far corner +0x18 / +0x19; returns the entry cell = top-left +
rec +0x60 / +0x61) and stores its index; none → state 2 between steps.
Arrival = MoveStep returned 0, or on the entry cell, or stepping into it.
State 0x19 repairs on **any own building** under the unit
(`FUN_004a5640`), not only pads; full hit points: +0x14 = 2,
`*_UNITREPAIRED` (local), state 0x1a. **Passability** (`FUN_0045d990`):
in state 0x18 the pad's own cells are allowed when no unit stands there
(the same rule lets state 5 enter the attacked building's cells, and
state 4 route through the attacked unit's cell when it is not adjacent).
The clicks: own pad with a non-infantry unit selected → `FUN_004595e0`
(order 0x21 with the pad, the selected non-infantry units); repair mode
on an own damaged vehicle with a pad owned → its acknowledge voice and
`FUN_00459690` (order 0x21, that unit, pad 0xffff = the nearest).

## 5. Guard (order 0x18, case 5)

Button 7 or its hotkey (`0x7983d7`), when an own **armed** unit is
selected (`FUN_004a58b0`): post 0x18 and play `*_GUARDING`.

For each listed unit that takes orders (`FUN_0049f4a0(u, 1)`), armed,
and accepts SetState(**3**) (area guard, doc 18): +0x14 = 1, home
(+0x50, +0x51) = its "to" cell (+0x4c, +0x4d), target +0x0c = −1.

## 6. Retreat (order 0x1a, case 7)

Button 8 or its hotkey (`0x7983e2`), when an own unit is selected
(`FUN_004a57c0`) **and** `FUN_0046e3f0` finds a destination: post 0x1a
with it and play `*_RETREAT`.

**Destination (`FUN_0046e3f0`)**, for the local player:

- No buildings: returns **false** (no retreat, no voice).
- Otherwise the centre building is the first primary slot (+0x26098), or
  the first building in the list if that slot is empty (0xffff). Its
  cell = (x px >> 5, y px >> 5) of its position (+0x30 / +0x34).
- Average the cells of every building whose cell is within distance²
  < 100 of the centre (cells as above, integer division); clamp to
  the map. If none (cannot happen, the centre counts), the centre.

**Handler** (shares the move order code, case 0/7): for each listed unit
with `FUN_0049f4a0(u, 1)`:

- harvester (kind 1) and the player owns a refinery (group count
  `+0x245c1[0x6d60df]` ≠ 0): SetState(0xd), return to the refinery;
- else SetState(7) (move) with dest = the destination.

Infantry and vehicles alike; there is no armed test.

## 7. Implementation checklist (our code)

1. **Orders** (`src/sim/world.*`): `order_repair_building(p, b)` (toggle
   `Building::flags` bit 1), `order_sell(p, b)` (§3.2),
   `order_repair_unit(p)` (§4), `order_guard(p)` (§5),
   `order_retreat(p, x, y)` + `retreat_destination(p)` (§6).
2. **DoRepairs** in `tick_buildings` / `tick_economy`: player `tick & 7`,
   §2.2, with VARS +0x30 (add `repair_points` / `unit_repair_points`
   (+0x34) / `repair_distance` (+0x2c) to `Vars`, read in `set_vars`).
3. **Selling** in `tick_buildings` (`src/sim/placement.cpp`): flag 4
   countdown, state 0x11 + flag 8 → 0x20000, removal next tick, refund
   (§3.5), primary reassignment, the voice. Draw the build-up frames for
   selling buildings (the counter runs backwards; same draw code).
4. **Survivors** (§3.4) for sold **and destroyed** buildings (destroyed:
   at +0x3b = 0x19 of the death countdown; check our dying code).
5. **Clicks** (`src/app/game_screen.cpp`, mode 2 / 3, done): repair needs
   rec flag 0x200 and hp < max; sell needs flags & 6 = 0; unit repair
   needs a kind-6 building, own non-infantry damaged unit.
6. **Sidebar** (`src/app/sidebar.cpp`): controls 7 and 8 call guard /
   retreat with the voices; 4 / 5 already toggle the modes.
7. **Draw**: the flashing wrench (fixed entry 3) on repairing buildings;
   fix doc 10 §8 naming (entry 3 = repair wrench, `0x782058` = upgrade).
8. Unit repair states 0x18 / 0x19 / 0x1a (only needed once a repair pad
   can be built; not in A1V1).
