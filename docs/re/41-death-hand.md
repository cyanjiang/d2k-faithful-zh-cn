# 41: Death Hand (1.06)

Written for a line-by-line port of the Harkonnen **DEATH HAND** (unit
record 24, kind 10, primary weapon bullet 20 `DHBOMB`). It is a palace
special like the airstrike (doc 35) and the saboteur / Fremen (doc 39).
The palace charges it, the player clicks a target cell, and a missile
rises from the palace. It flies straight north off the top of the map,
reappears above the (scattered) target column and flies straight south.
When it reaches the target row it bursts at 60 px altitude and throws
40 falling bomblets.

Conventions as in docs 33 / 35 / 39: `unit+…` are object fields (objects
at `player+0x30`, stride 0x94; unit layout doc 09 §2: x/y at +0x34/+0x38,
altitude +0x3C), `b+…` are building fields (x at +0x00 = left edge,
y at +0x04 = **bottom** edge, doc 12 §3), `player+…` is
`FUN_004a4660(side)`, `urec+…` is `0x77e250 + rec · 0x100`, `brec+…` is
`0x6dc540 + rec · 0x10c`. A unit slot is `player+0x26530 + i · 0x14`:
+0 item, +2 progress (0x5A00 = done), +0x10 hold, +0x11 **used**.
`DX` / `DY` are the step tables `0x6e9840` / `0x704a98` (doc 14 §1.5),
index `facing · 512 + s`, value `trunc(sin / cos(angle) · s · 0x800)`.
`rand()` is the exe's MSVC LCG (doc 06), called as `thunk_FUN_004b1810`
/ `FUN_00448460` with a source line number; the line numbers are given
so the calls can be matched to the RNG trace.

"Verified" means it was read from the decompile, and from the
disassembly where it matters. Guesses are marked TODO(verify).

Main functions:

| Address | Name used here | Role |
|---|---|---|
| `FUN_004466e0` | AutoStartSpecials | (re)starts kinds 9 / 10 / 4 / 12 every tick (doc 35 §2.1) |
| `FUN_004467e0` | DoIconBuild | slot progress, `*_DEATHREADY` |
| `FUN_00442a00` | UnitBuildFactor | Death Hand: F = 100 only at power ≥ 100 % |
| `FUN_0046be50` | RebuildBuildLists | `*_DEATHPREPPING` when DEATH_HAND first appears |
| `FUN_004433a0` | HandleGameLoopEvents | icon click → mode 5, cursor 0x1E, target click |
| `FUN_0045a440` | PostSpecialOrder | posts order 0x30 (side, kind 10, x, y) |
| `FUN_00457810` case 0x1d | ProcessOrder 0x30 | clears ready slots, `DeathHandAccuracy` scatter, launch |
| `FUN_0049e860` | LaunchDeathHand | creates the missile at the palace, `*_DEATHLAUNCHED` |
| `FUN_00455870` | CreateUnit | kind 10 branch (doc 12 §8.3) |
| `FUN_00497b90` | UnitUpdate | state 7 / 0x11 call the Death Hand step for flag 0x20000 |
| `FUN_00494cd0` | DeathHandStep | launch delay, flight, burst, 40 bomblets |
| `FUN_00456a30` | AddBullet | the bomblet (doc 18 §4.1) |
| `FUN_0049be60` | BulletUpdate | falling bomblet (doc 18 §2.3.1, §4.2) |
| `FUN_00456df0` | AddExplosion | trail and burst (doc 14 §4.1) |
| `FUN_0042a5d0` | DrawAircraft | body twice, no shadow (doc 14 §5.5) |
| `FUN_00422270` | DoSpecialWeapons (AI) | case 4 |
| `FUN_00422730` | ItemReady (AI) | doc 35 §10 |
| `FUN_00422800` | GetBestTarget (AI) | kind 10: building 15 / 128, unit 1 / 128 |
| `FUN_004544e0` | LoadVars | **DeathHandAccuracy** `0x6b8865` |

## 1. Data (Templates.bin, VARS.BIN, samples.uib; verified with dump scripts)

Unit record 24 `DEATH_HAND` (`0x6d60eb` = 24, the special index the
launcher uses; debug-loader name "DEATH HAND"):

| Field | Value | Meaning |
|---|---|---|
| +0x00 house mask | 0x02 | Harkonnen only |
| +0x02 armour | 0 | NONE |
| +0x03 | 1 | turn delay (unused: the step never turns) |
| +0x04 hp | 1500 | |
| +0x08 speed | 0x78000 | **7.5 px/tick** (step index 0x78000 >> 11 = 240) |
| +0x0C / +0x0D | 20 / 0xFF | primary DHBOMB, no secondary |
| +0x10 view | 0 | reveals nothing |
| +0x14 element | 21 | 1 frame × 2 rotations (DATA.R16 2382–2383) |
| +0x1C cost | 0 | |
| +0x20 build rate | 2 | |
| +0x24 tech | **7** | |
| +0x28 prerequisite 1 | 20 | HARKONNEN_PALACE |
| +0x2D kind | 10 | |
| +0x2F death explosion | 10 | EX_BANG14 (also the first burst explosion, §5.3) |
| +0x38 flags | 0 | |
| +0x3C..+0x5B rotation | 0 for facings 0–7 and 24–31, 1 for 8–23 | rotation 0 flying north, 1 flying south |

Bullet 20 `DHBOMB` (`0x782068 + 20 · 28`):

| Field | Value |
|---|---|
| speed | 0x60000 = 6.0 px/tick (used for the bomblet's sideways drift, §6.2) |
| damage | **225** |
| flags | 0x180 = 0x80 animated (4 frames, `0x795340[20]`) + 0x100 falls from the firer's altitude |
| fire sound | −1 |
| impact explosion | 10 EX_BANG14 (23 frames, additive, sound 19 `S_SMALLEXP4`) |
| trail | none |
| anti-air | 0 |
| warhead | 2 W_SPECIAL (radius 64 px; NONE 90, WALL 50, BLDG 75, WOOD 60, LIGHT 60, HEAVY 60, CONCR 100, CY 25, HARV 60 %) |
| range | 32 |

Special explosion indices (Templates.bin block `0x6d60d8`, names from the
debug loader `FUN_00466140`):

| Global | Value | Name | Frames | Default flags | Sound |
|---|---:|---|---:|---|---|
| `0x6d6115` | 47 | EX_DHM (the missile trail) | 6 | 0x200 additive | none |
| `0x6d6116` | 56 | EX_DEATHHAND (the burst) | 15 | 0x200 additive | none |
| urec+0x2F | 10 | EX_BANG14 | 23 | 0x200 additive | 19 S_SMALLEXP4 (not played here, §5.3) |

Harkonnen palace, building record 57: footprint 3 × 3 (`0x777`), image
height +0x86 = 96 px, +0x60/+0x61 = (0, 0), **+0x62/+0x63 = (60, −9)**
(signed bytes; used as the launch pixel offset, §4), active animation
+0x8B = 57.

VARS.BIN byte +0x4D → `0x6b8865` **DeathHandAccuracy = 3** (name table
`0x4e2730` entry 0x1A; text default also 3).

Voices (samples.uib):

| Name | File | When |
|---|---|---|
| `S_ / O_ / H_DEATHPREPPING` | ai_prep / oi_prep / hi_prep | DEATH_HAND enters the local player's unit list (§2.3) |
| `S_ / O_ / H_DEATHREADY` | ai_dhrdy / oi_dhrdy / hi_dhrdy | charge done, local player (§2.2) |
| `S_ / O_ / H_DEATHLAUNCHED` | ai_launc / oi_launc / hi_launc | **any** side launches (§4) |

The S_/H_/O_ choice is always the **local** player's house
(`FUN_0046f900`, doc 12 §9). There is no other warning voice (no
"approaching" sample exists).

## 2. Production (verified)

### 2.1 Auto-start

`FUN_004466e0` starts kind 10 like the other specials (doc 35 §2.1). It
restarts every tick after use, for every side whose unit build list
contains record 24. The build list needs an owned HARKONNEN_PALACE, house
H and mission tech ≥ 7.

### 2.2 Progress (`FUN_004467e0`, `FUN_00442a00`)

Per tick, for a slot with the Death Hand, not on hold, progress < 0x5A00:

```
factory = FUN_00442c20(24, side, …, check 0)   // HARKONNEN_PALACE: +0x260aa, then +0x260a8, +0x260ac (doc 12)
if factory == 0: slot.item = −1; continue        // no palace: charge lost, no refund
// NO factory flag 0x10 for kind 10 (the palace does not animate while charging)
F = (player.power_percent (+0x245b0) < 100) ? 0 : 100
F = Difficulty(side) · F / 100                   // FUN_00442960: local easy 125 / normal 100 / hard 75; AI 100
step = urec.rate (2) · F / 100                   // unsigned; 2, 2, 1 at full power, 0 below 100 %
pay 0 (cost 0, FUN_0046c720 accepts)
progress += step
if progress > 0x59FF:
    progress = 0x5A00
    if side == local: voice(H/O/S_DEATHREADY, mode 1, limit 0, prio 0)   // no text message
    // no flag change, no spawn
```

- Below 100 % power the slot simply stalls (step 0). Progress is kept.
- Charge time at full power: 11520 ticks (9 min 36 s at 20 ticks/s) on
  easy and normal and for the AI, 23040 ticks on hard.
- Unlike kinds 9 / 4 / 12, the ready branch shows **no text message**,
  only the voice.

### 2.3 DEATHPREPPING (`FUN_0046be50`, end of the build-list rebuild, verified)

```
if player.side (+0x24252) == local:
    if record 24 was in the OLD unit list: return
    if record 24 is in the NEW unit list (+0x261b4, count +0x261ac):
        voice(H/O/S_DEATHPREPPING, mode 1, limit 0, prio 0)
```

It plays once when the Death Hand first becomes available (palace built,
tech reached), and again each time it comes back after it left the list.

## 3. Player orders (verified)

### 3.1 Sidebar click (`FUN_004433a0`, left button up, unit column)

The same code as the airstrike (doc 35 §3.1). A slot with item 24,
progress 0x5A00 and used (+0x11) = 0 must exist; then **mode
`0x516438` = 5**. The icon click does **not** set the used byte. A click
on a special that is not ready does nothing. Right button down anywhere
sets mode 0 (cancel).

### 3.2 Cursor

While mode = 5 the playfield cursor is **0x1E (30, the skull)** for every
cell: no shroud, unit or building test (the cursor block that ends at
`LAB_00443d36`; the mode 4 branch gives 9). Outside the playfield the normal cursors are used. This
settles the doc 10 cursor table entry "30 skull, TODO(verify)".

### 3.3 Target click (left button up, mode 5, next to the mode 4 code at `0x445470`)

```
if mouse_x < 480 (0x786d48) and top (0x4e41dc) < mouse_y:      // in the playfield
    any = false
    for each slot: if item ≥ 0 and urec[item].kind == 10 and progress == 0x5A00 and used == 0:
        used = 1; any = true
    if any:
        PostSpecialOrder(local, 10, (cam_x + mouse_x) >> 5, (cam_y − top + mouse_y) >> 5)   // unsigned
    mode = 0
```

As for the airstrike: no shroud test, no sound on the click.

**Drag release (verified 2026-10-16, `FUN_004433a0`).** A left press in
the playfield sets the drag flag `0x516458` and the press point in every
mode. On release, the drag test comes **before** the mode switch: when
the flag is set and the release is at least 4 px away from the press on
both axes, the box path runs: `FUN_004a5ae0` (box select), the select
response `FUN_0049f240(0)` if it selected something, and mode = 0 in
both cases (`LAB_004455a7`). So a drag in mode 5 (or 4) selects units,
cancels the mode, marks no slot used and launches nothing; the charge
stays ready. Ours does the same (`GameScreen::handle_mouse`).

## 4. Order 0x30, kind 10 (`FUN_00457810` case 0x1d, `0x458aef`; `FUN_0049e860`; verified)

```
P = player(order.side)
cleared = false
for each slot: if item ≥ 0 and urec[item].kind == order.kind and progress == 0x5A00:
    item = −1; used = 0; cleared = true          // the used byte is not checked
if !cleared: return
// kind 10:
A  = DeathHandAccuracy (byte 0x6b8865) = 3
tx = order.x (u16) − (rand() [line 0xa7d] % (2·A)) + A       // unsigned div; x first
ty = order.y (u16) − (rand() [line 0xa7e] % (2·A)) + A       // then y
tx = clamp(tx, 0, W − 1); ty = clamp(ty, 0, H − 1)           // signed compares
LaunchDeathHand(side, tx, ty)
```

With A = 3 the target moves by **−2 … +3 cells** on each axis
(r % 6 = 0 gives +3). The scatter is drawn when the order is processed,
also for the AI, and the two rand() calls happen even if the launch then
fails (no palace). A = 0 would divide by zero (not reachable with 1.06
data).

LaunchDeathHand (`FUN_0049e860`):

```
rec = 24 (byte 0x6d60eb)
if P.+0x260aa (primary Harkonnen palace) == 0xFFFF: return      // charge already gone: lost
b  = palace object (P + 0x30 + idx · 0x94)
cx = trunc0(trunc0(b.x / 65536) / 32)            // left column
cy = trunc0(trunc0(b.y / 65536) / 32)            // b.y is the BOTTOM edge: the row below the palace
dx = (int8) brec[b.rec].+0x62 = 60; dy = (int8) brec.+0x63 = −9
idx = CreateUnit(side, 24, cx, cy, cx, cy, dx, dy)
if idx == 0xFFFF: return
u = unit idx
u.+48 = tx; u.+49 = ty                           // dest
u.+29 = 20                                       // launch delay
u.flags |= 0x200000                              // hidden while waiting
SetState(u, 7)
b.+28 |= 0x20                                    // the palace plays its active animation once
FUN_0046f900(S_, O_, H_DEATHLAUNCHED, mode 1, limit 0, prio 1)   // NO side test: every player hears it
```

CreateUnit (`FUN_00455870`) for kind 10, in order:

- ground tests skipped (kinds 2, 5, 8, 9, 10); counters `+0x24780`,
  `+0x24788[24]`, `+0x24628[24]` += 1. The two "built" counters
  (`+0x24780`, `+0x24788[]`) have no reader that affects the game or
  the score screen: only the dead debug box and the Westwood Online
  upload read them (doc 60 §3, doc 56 §3), so they are not ported.
  `+0x24628[24]` is the owned count (ours counts the unit list,
  `World::owned_record`), dropped again when the missile is removed;
- hp 1500, speed +0x00 = 0x78000, +0x1a = 1, +0x2f = 10, group 0xFF,
  target 0xFFFF, +0x26 = side, +0x29 = 0;
- **x = (cx·32 + 16 + 60) << 16, y = (cy·32 + 16 − 9) << 16**: for a
  palace with top-left cell (L, T) that is pixel (L·32 + 76,
  T·32 + 103), 76 px right of the left edge and 7 px below the bottom
  edge;
- cells +0x4a/4b, +0x4c/4d, +0x50/51 = (cx, cy); facing +0x54 = 0
  (x1 = x2);
- kind 10 branch: flags |= **0x20040** (0x40 aircraft, 0x20000 Death
  Hand), altitude +0x3c = 0x3c0000 (60 px), facing 0, +0x29 = 8,
  SetState(7), dest = (W / 2, H / 2) (signed int division);
- the initial-order block skips kind 10 (no state 3 for the AI);
- no cell occupation, no 0x800000 air flag.

The launcher then overwrites dest, +0x29 = 20 and adds 0x200000, as
above. Final flags: 0x40 | 0x20000 | 0x200000 (+ 0x80000000 from
SetState). The missile is not selectable (0x40), not in AI groups
(GroupNewUnits needs `flags & 0x840 == 0`), not in the sorted draw list,
not on the radar (only kind 9 aircraft are drawn), and reveals nothing
(view 0).

## 5. Flight (`FUN_00494cd0`, verified with the disassembly)

`UnitUpdate` (`FUN_00497b90`), after the generic top (doc 35 §5):

- state 7 (`0x498e47`): if flags & 0x20000:
  `if DeathHandStep(u, side, idx) == 0: SetState(u, 0x11)`;
- state 0x11 (`0x49a09a`): while +0x2f > 0: +0x2f −= 1, and for an
  aircraft with 0x20000 `DeathHandStep(u, side, idx)` (result ignored).
  At +0x2f = 0: `FUN_0049f330(+0x4a, +0x4b)` clears 0x800000 on the
  current cell (here the palace launch cell, never set by the missile)
  and the unit is removed (doc 14 §3.3). No explosion, no sound.

### 5.1 One step

```
DeathHandStep(u, side, idx):
    if u.flags & 0x200000:                       // waiting at the palace
        if u.+29 == 0: u.flags &= ~0x200000; return 1
        u.+29 −= 1; return 1
    if EX_DHM (0x6d6115) != 0xFF:
        AddExplosion(side, 47, trunc0(x / 65536), trunc0(y / 65536), height = u.+3c (16.16),
                     flags 0, delay 0, 0, 0)     // trail at the position BEFORE the move
    i = ((u.speed & 0x7ffffff) >> 11) + u.+54 · 512                 // 240 + facing · 512
    x += DX[i]; y −= DY[i]
    if u.+54 == 0:                               // flying north
        if trunc0(trunc0(y / 32) / 65536) < −2:  // y ≤ −96 px
            u.+54 = 16                           // now flying south
            x = u.+48 (dest x, u8) << 21          // dest.x · 32 px: the LEFT edge of the column
        return 1
    // flying south (any facing ≠ 0)
    if y ≥ u.+49 (dest y, u8) << 21 (signed) and !(u.flags & 0x8000):   // reached the TOP edge of the dest row
        u.flags |= 0x8000
        Burst(u, side, idx, i)                   // §5.3
        return 0                                 // → state 0x11
    return 1
```

- DX/DY at speed index 240: facing 0 → (0, 491520), facing 16 →
  (0, −491520), i.e. exactly 7.5 px per tick north or south, no
  sideways drift. The step never turns, never checks the map edges and
  never updates the cells +0x4a..+0x4d.
- **Waiting:** +0x29 counts 20 → 0 over 20 updates, the 21st update
  clears 0x200000, the 22nd is the first move. While 0x200000 is set the
  missile is not drawn (§7).
- **Up:** from y = T·32 + 103 px until y ≤ −96 px, so about
  (T·32 + 199) / 7.5 ticks (a palace with T = 40: 198 ticks).
- **Switch:** on the tick y passes −96 px the facing becomes 16 and x
  jumps to dest.x · 32 px. y stays (≈ −96 … −103.5 px).
- **Down:** until y ≥ dest.y · 32 px, about (dest.y · 32 + 100) / 7.5
  ticks. The burst point is therefore the **top-left corner** of the
  scattered target cell (x exactly dest.x · 32, y up to 7.5 px below
  dest.y · 32), not the cell centre.
- Altitude stays 60 px the whole time.

### 5.2 State 0x11 after the burst

`SetState(0x11)` → 10 more updates in which the step still runs: flag
0x8000 blocks a second burst, but the trail is still spawned and the
missile keeps flying south 7.5 px per tick (75 px), drawn normally. The
11th update finds +0x2f at 0 and removes it (the zero test comes before
the decrement, `0x499e31`; doc 14 §3.3, corrected 2026-10-16). It is
drawn during those updates: the aircraft draw (`FUN_0042a5d0`) tests
only flags 0x140 / 0x200000 / 0x20000, never the state (verified).

A missile that enters state 0x11 **before** the burst (killed, §8) keeps
flying for its 10 ticks; if it reaches the target row in that time it
still bursts.

### 5.3 Burst (`0x494de3`..`0x494fb8`)

```
// i = the step index of this tick (facing 16, speed 240): DX[i] = 0, DY[i] = −491520
AddExplosion(side, urec.+2F = 10 EX_BANG14, x px, y px, height = u.+3c,
             flags 0x1000, delay 0, DX[i], −DY[i])        // final flags 0x1200: additive + one move of (0, +7.5 px)
if EX_DEATHHAND (0x6d6116) != 0xFF:
    AddExplosion(side, 56 EX_DEATHHAND, x px, y px, u.+3c, 0x1000, 0, DX[i], −DY[i])
repeat 40:                                                   // counter 0x28
    r1 = rand() [line 0x5b0]; dir   = r1 & 0x1F
    r2 = rand() [line 0x5b1]; delay = r2 & 7
    r3 = rand() [line 0x5b2]; s     = r3 & 0xFF               // NOT reduced: up to 255 → DX up to ±522240
    px = trunc0(x / 65536); py = trunc0(y / 65536)
    b = AddBullet(side, urec.+0C = 20 DHBOMB, delay, firer = idx,
                  x0 = px, y0 = py, tx = px + DX[dir·512 + s], ty = py + DY[dir·512 + s],   // raw 16.16 table value added to a PIXEL
                  tidx 0xFFFF, tside −1)
    if b != 0xFFFF:
        bullet[b].+34 (vz) = rand() [line 0x5be] & 0xFFFF     // only when the bullet was created
```

- `px`, `py` are after this tick's move (the same point as the burst
  explosions).
- **No sound** is played here. EX_BANG14's sample is only played by
  spawners that call `FUN_0046f540`, which this function does not. The
  audible part is the 40 bomblet impacts (§6.3) and the launch voice.
- No screen shake from the burst itself (only destroyed buildings with
  record +0x08 ≠ 0 shake the screen, doc 18 §2.3).
- Both burst explosions sit at 60 px height, so they are drawn in the
  high explosion pass (doc 14 §5.1), 60 px above the ground point, and
  move once by 7.5 px south on their first update.
- rand() order per bomblet: dir, delay, step, then (if created) vz:
  **4 calls per bomblet, 160 per burst** (fewer if the object pool is
  full).

## 6. The bomblets (verified)

### 6.1 Creation (AddBullet, exact integer semantics)

AddBullet takes x0, y0, tx, ty as **u16**. The DH passes
`px + DX[…]`, so the target is truncated to 16 bits. The distance and
the drift are computed with 32-bit integer arithmetic that overflows
for these targets. This must be copied exactly:

```
x0 = px & 0xFFFF; y0 = py & 0xFFFF             // stored as x0 << 16
tx = (px + DX[dir][s]) & 0xFFFF                // u16 at +0x1C
ty = (py + DY[dir][s]) & 0xFFFF                // u16 at +0x1E
ddx = tx − (int)(x >> 16 rounded to 0)         // int32: tx is 0..65535, so ddx is usually large
ddy = ty − (int)(y >> 16 …)
q   = i32(i32(ddx·ddx) + i32(ddy·ddy))         // 32-bit imul/add, wraps
dist = ftol(fsqrt(fild q))                     // q < 0 → NaN → _ftol gives 0 (low dword of 0x8000000000000000)
ticks = (dist << 16) / speed                   // unused for flag 0x100
if dist == 0:  dx = dy = 0                     // flag 0x100 keeps it (doc 18 §4.1)
else:          dx = i32(speed · ddx) / dist     // 32-bit imul wraps, signed idiv (toward 0)
               dy = i32(speed · ddy) / dist
flags = 0x180 | 2 (shadow); height = firer (the missile) altitude = 0x3C0000; vz = 0
frame = rot = 0 (flag 0x80)
start delay +0x20 = r2 & 7
```

Then the DH code sets vz = r4 & 0xFFFF (0 … 0x7FFF, i.e. up to 0.5 px per
tick of initial fall speed).

What this gives (computed for all 32 × 256 (dir, s) pairs at several
burst points; the result depends a little on the absolute pixel because
of the 16-bit wrap):

- about **44 %** of the (dir, s) pairs give q < 0 → dist 0 → the bomblet
  falls **straight down** at the burst point;
- most others drift slowly (median 0.35 px per tick);
- a few drift fast because of the `speed · ddx` overflow, up to about
  52 px per tick, and fly far or off the map.

Landing offset from the burst point (5 vz values per pair): median
≈ 9 px, 75 % ≤ 26 px, 90 % ≤ 47 px, **≈ 93–94 % within 64 px** (the
W_SPECIAL radius), 99 % ≤ 175 px, rare strays up to ≈ 1550 px. So the
cluster is tight around the top-left corner of the target cell, with a
few strays. This is the vanilla behaviour and must not be "fixed".

`d2k`'s `World::add_bullet` uses 64-bit maths and no u16 truncation
today. All existing callers stay in range, so switching it to the exact
semantics above changes nothing for them (also check LMG / HMG: their
`speed · ddx` already overflows, but they have ticks 0 and impact at the
target pixel, so dx does not matter).

The distance square root: the exe uses x87 `fsqrt` on an int32. A double
`sqrt` truncated to int gives the same result for every q < 2³¹ (the
nearest non-square is far enough from an integer). x87 precision
quirks are not emulated (CLAUDE.md §10, the float decision), so a
24-bit precision control would not change our result either.

### 6.2 Fall (`FUN_0049be60`, doc 18 §2.3.1)

Each update after the start delay:

```
firer check: the missile is already in state 0x11 (it burst this tick) → firer = 0xFFFF
off the map (x or y < 0, or cell ≥ W / H): removed, no impact
frame = (frame + 1) % 4
vz += 0x2000; target pixel = current pixel; height −= vz
if height < 1 (raw): height = 0, impact at the target pixel
else: x += dx; y += dy
```

From 60 px with vz₀ = 0 … 0x7FFF it lands on update **27 … 31** after
its delay, at `start + (k − 1) · (dx, dy)`. New bullets are appended to
the side's bullet list, which updates after its units (doc 14 §4.4), so
the first update (and the firer = 0xFFFF reset) happens in the burst
tick.

### 6.3 Impact (doc 18 §4.2 step 11, §2)

- `DamageTiles(target px, height 0, DHBOMB, side, firer 0xFFFF, deviate 0)`:
  ground units and buildings within 64 px. Damage per bomblet =
  `pct[W_SPECIAL][armour] · 225 / 100 · (64 − d) / 64`. **No
  friendly-fire check**: the launcher's own and allied objects are hit.
  Aircraft are not hit (height 0).
- A spice bloom on the impact cell bursts instead (doc 31).
- Crater bytes and concrete damage as doc 18 §2.4 (W_SPECIAL NONE 90 %:
  +5 per bomblet).
- EX_BANG14 on the ground (additive) and its sound S_SMALLEXP4,
  positional, at the impact cell (`FUN_0046f540`, 5 channels, doc 12 §9).
- Kills credit the launching side (attacker side = bullet owner), with
  firer index 0xFFFF.
- Drawing: bomblet image `0x714b00[20 · 32 + frame]` at y − height plus
  its shadow at the ground point, after the units and aircraft (doc 18
  §4.4).

## 7. Drawing the missile (`FUN_0042a5d0`, verified)

- Not drawn while 0x200000 is set (both parts skip it): it is invisible
  for the 21 waiting updates.
- Pass 0, part (a): units with flags & 0x140 and no 0x200000. For 0x40
  with **0x20000** the body is drawn (not a shadow) at
  (x − off_x, y − (altitude >> 16 & 0xFF) − off_y), keyed blit (1, 1).
  **No shadow.**
- Part (b), pass 1 (altitude 60 ≥ 20): the body again, same place, same
  blit. Net: the body is drawn twice, once under and once over the high
  explosions; the visible result is the pass-1 copy on top.
- Image `0x71aca0[((21 · 35) + frame 0) · 32 + rot[facing]]`: rotation
  0 (DATA.R16 2382) while flying north, 1 (2383) while flying south.
  House colours 240..255.
- The first moving frame is drawn about 60 px above the launch pixel,
  i.e. over the palace image: it seems to rise out of the palace.
- Trail: one EX_DHM per moving update at the previous position, at
  60 px height (high pass), additive, frames 1–5 for 3 ticks each (doc
  14 §4.4), so about 5 trail puffs are visible behind the missile.

## 8. What can hit it (verified)

- Ground explosions cannot (aircraft are hit only at height > 6 px).
- **Anti-air:** the air pass of `EnemyUnitInRange` (`FUN_004a4900`)
  only looks at cells with 0x800000 and then accepts an enemy aircraft
  whose **cell bytes** (+0x4a/+0x4b or +0x4c/+0x4d) match that cell. The
  missile never sets 0x800000 and its cell bytes stay at the palace
  launch cell. So it is normally **not targetable**. Quirk: if another
  aircraft sets 0x800000 on that launch cell, an enemy AA shooter in
  range of that cell can lock on to the missile wherever it is, and the
  homing rocket chases its real pixel (hp 1500, armour NONE). Keep it.
  Verified statically: the missile's cell bytes are written only by
  CreateUnit (`0x455a94`), never by the Death Hand step; aircraft set
  and clear 0x800000 only in the flight step (`FUN_00494fd0`: clear at
  +0x4c/+0x4d `0x49543a`, set at the new cell `0x495482`) and clear it
  at removal (+0x4a/+0x4b, `0x499e69`). Ours does the same; test
  `tests/sim/test_combat.cpp` (lock with the cell flag, none without).
- `FUN_004a5270` skips kind 10: clicks and ground target searches ignore
  it. Sandworms ignore it (doc 17).
- If it dies (`FUN_0049cde0`): death effects at altitude (EX_WHITEN1,
  EX_BANG14 + sound), state 0x11, 10 more steps (§5.2).

## 9. AI (`FUN_00422270` DoSpecialWeapons case 4, verified)

The common head (BESERK, `(tick + side) & 0x1F`, SpecialWeaponDelay,
the first `GetBestSideToAttack`) is doc 35 §10. Case
`((side + tick) >> 5) & 7 == 4`, so at most once per 256 ticks per side:

```
case 4:
    if !ItemReady(ai, 10, &slot): return                // FUN_00422730: last slot of kind 10, progress > 0x59FF
    if P.slot[slot].used (+0x11) != 0: return
    tside = GetBestSideToAttack(ai)                     // FUN_0041cdf0, the second call (may rand())
    t = GetBestTarget(tside, 10)                        // FUN_00422800
    if t == 0: return
    if t.+90 == 1:   px = trunc0(t.x(+34) / 65536); py = trunc0(t.y(+38) / 65536)   // unit: its pixel
    elif t.+90 == 2: px = trunc0(t.+00 / 65536);   py = trunc0(t.+04 / 65536)       // building: BOTTOM-left corner
    else: return                                        // (case 3 posts (0, 0) here; case 4 does nothing)
    PostSpecialOrder(side, 10, trunc0(px / 32), trunc0(py / 32))
    P.slot[slot].used = 1
```

GetBestTarget with kind 10: **building chance 15, unit chance 1** (out
of 128):

```
if tside == 0xFF: return 0
for b in player(tside).buildings (list +0x10, in order):
    if brec[b.+34].+0x5C != 14 (not a wall) and (rand() [0x19b1] & 0x7F) < 15: return b   // walls draw no rand()
for u in player(tside).units (list +8, in order):
    if (rand() [0x19bf] & 0x7F) < 1 and !(u.flags & 0x400140) and urec[u].kind != 5: return u   // rand() first
return 0
```

- Same building quirk as the airstrike: the aim cell is the building's
  left column, **one row below** the building. Then the order adds the
  −2 … +3 scatter (§4). With the burst at the cell's top-left corner and
  ≈ 9 px median spread, an AI Death Hand aimed at a building usually
  lands just below-left of it; the 64 px radius still reaches the
  building's bottom row. Copy it.
- A unit is picked only with chance 1/128 per unit, so the AI almost
  always targets a building when the enemy has any.
- The AI launches from its own primary Harkonnen palace and its launch
  plays `*_DEATHLAUNCHED` for the human (§4).
- An AI only charges a Death Hand with record 24 in its unit build list
  (Harkonnen, palace, mission tech ≥ 7). In the 1.06 campaign that is
  side 1 of A9V1, A9V2, O8V1, O9V1 and O9V2 from the start; side 1 of
  A8V1 has the house, tech and a yard and gets it only after building
  a palace. No Harkonnen mission and no other AI side (test
  `tests/sim/test_combat.cpp`, which rebuilds every AI's lists).
- Nothing else in the AI touches the missile: GroupNewUnits skips it
  (0x40), and no AI code reads kind 10 units.

## 10. Saved state

Missile: x, y, altitude, speed, facing +0x54, dest +0x48/49, launch
delay +0x29, cells +0x4a..+0x4d (the palace cell), flags (0x40, 0x20000,
0x200000, 0x8000), state, countdown +0x2f. Slot: item, progress, hold,
used. Bomblets: the normal bullet fields (delay, height, vz, dx, dy,
frame, firer), so a save in the middle of a burst restores exactly.

## 11. Open points

Closed 2026-10-16: the flight and draw after the burst (§5.2), the
counters (§4: no effect), the drag release (§3.3), the AI missions
(§9) and the AA lock-on quirk (§8, test).

- Order timing: the exe runs order 0x30 on the next order pass; our
  `order_special` runs at once. For the AI this moves the 2 scatter
  rand() calls (and the 160 burst rand() calls are unaffected) earlier
  in the RNG stream than in the exe. Same open issue as doc 35 / 39.

## 12. Checklist: our code

Historical (the plan before the port). Every row is done now (parity
row "Death Hand"); the Status column below is the state at writing.

| # | Finding | Where | Status |
|---|---|---|---|
| 1 | `DeathHandAccuracy` = VARS.BIN byte +0x4D (1.06: 3) | `sim/world.hpp` `Vars`, `World::set_vars`, `save_state.cpp` | **missing** (add `death_hand_accuracy = 3`) |
| 2 | AutoStart for kind 10 | `World::auto_start_specials` (`is_special`) | done |
| 3 | Charge: F = 100 only at power ≥ 100 %, × difficulty; no factory flag 0x10 | `World::tick_production` (`economy.cpp` ≈ l. 646–648) | done |
| 4 | `*_DEATHREADY` for the local player at 0x5A00, no text | `tick_production` (`voice(p, "DEATHREADY")`, local-only) | done |
| 5 | `*_DEATHPREPPING` when record 24 enters the local player's unit list | `World::rebuild_lists` end (TODO "*_NEWOPTIONS / *_DEATHPREPPING", `economy.cpp` l. 166) | **missing** |
| 6 | Sidebar click on a ready, unused Death Hand → mode 5 (used not set) | `app/sidebar.cpp` `left_up` ("TODO: Death Hand (mode 5)") | **missing** (add `mode::kDeathHand = 5` in `sidebar.hpp`) |
| 7 | Mode 5 cursor 0x1E (30) on every playfield cell; right button cancels | `app/game_screen.cpp` cursor choice (next to `kAirstrike` → 9) | **missing** |
| 8 | Mode 5 target click: in-playfield test, mark every ready kind-10 slot used, order 0x30 kind 10 with (cam + mouse) >> 5, mode 0 | `GameScreen` left-up (copy of the `kAirstrike` block) | **missing** |
| 9 | Order 0x30 kind 10: clear slots, 2 × rand() % (2·A) scatter (x first), clamp, launch | `World::order_special` ("TODO: Death Hand (10)") | **missing** |
| 10 | LaunchDeathHand: primary Harkonnen palace (+0x260aa only), cell from the bottom-left corner, `create_unit(24, cx, cy, cx, cy, dx = 60, dy = −9)`, dest, +0x29 = 20, 0x200000, state 7, palace flag 0x20, `*_DEATHLAUNCHED` for the local player whoever launched (prio 1) | `sim/aircraft.cpp` (new `launch_death_hand`) | **missing**; `create_unit` needs the pixel offsets dx / dy and `voice` needs a "local house, any side" variant |
| 11 | `create_unit` kind 10: flags 0x20040, altitude 0x3c0000, facing 0, +0x29 = 8, state 7, dest = map centre; no occupation; no AI state 3 | `sim/placement.cpp` `create_unit` (TODO "aircraft / Death Hand starts") | **missing** (ground test already skips kind 10) |
| 12 | State 7 / 0x11 dispatch: flag 0x20000 → `death_hand_step`; return 0 → state 0x11; in 0x11 the step runs while the countdown runs | `World::tick_aircraft_state` (kMove → `flight_step`, 0x11 → `flight_step`) | **missing** |
| 13 | DeathHandStep: launch delay, trail EX_DHM before the move, 7.5 px straight steps, the y ≤ −96 switch to facing 16 and x = dest.x · 32, burst at y ≥ dest.y · 32 once (0x8000) | `sim/aircraft.cpp` (new) | **missing** |
| 14 | Burst: EX_BANG14 + EX_DEATHHAND at 60 px, caller flag 0x1000 with move (DX, −DY); no sound; 40 × (rand dir, rand delay, rand s, AddBullet, rand vz) | new, uses `spawn_effect_at`, `add_bullet` | **missing** |
| 15 | AddBullet exact semantics: u16 x0 / y0 / tx / ty, int32-wrapping squares and `speed · ddx`, q < 0 → dist 0, signed division | `World::add_bullet` (`combat.cpp` l. 291, 64-bit maths) | **differs** (needed for the DH; harmless for other callers) |
| 16 | AddBullet start delay argument (DH: r2 & 7) and vz override after creation | `add_bullet` has no delay parameter; set `b.delay` / `b.vz` on the returned bullet | **missing** (caller side) |
| 17 | Falling bullet update: firer reset, off-map removal, frame cycling (0x80), vz / height, land at height < 1, x += dx otherwise | `World::tick_bullets` | done for ornithopter bombs / debris; check dx ≠ 0 drift for flag-0x100 bullets |
| 18 | Impact: W_SPECIAL radius 64, no friendly-fire check, bloom burst, craters, EX_BANG14 + S_SMALLEXP4 | `combat.cpp` damage path | done (generic) |
| 19 | Draw: hidden while 0x200000; no shadow for 0x20000; body in pass 0 part (a) and again in pass 1; rotation table 0 / 1 | `GameScreen` aircraft draw (≈ l. 779–808) | mostly done: hidden and no-shadow are correct; the part (a) body copy for 0x20000 is missing (no visible difference) |
| 20 | Not on the radar (kind 9 only), not selectable, ignored by clicks / worms / ground AA | radar, `GameScreen::unit_at`, `World::unit_in_cell`, worm | done for radar / worm; check that `unit_at` / `unit_in_cell` skip kind 10 |
| 21 | AA air pass matches by cell bytes; the missile's cells stay at the palace cell and it never sets 0x800000 | `World::target_unit_in_range` air pass; `death_hand_step` must not touch `cur_x/to_x` or `kAirOver` | **check** when porting |
| 22 | State 0x11 removal clears `kAirOver` on the current cell (palace cell for the DH) | `tick_aircraft_state` case 0x11 (uses `to_x/to_y`) | done (same cell for the DH) |
| 23 | AI DoSpecialWeapons case 4: ItemReady(10), used, second GetBestSideToAttack, GetBestTarget(…, 10) with 15 / 1, unit pixel / building bottom-left, nothing for other object types, order, used = 1 | `World::ai_special_weapons` (`ai_groups.cpp`, `default: return`) | **missing** (`ai_best_target` already has the 15 / 1 chances) |
| 24 | Save state: launch delay (+0x29, reuse `bombs`), flags 0x20000 / 0x200000 / 0x8000, bullet delay / vz / dx / dy | `save_state.cpp` | **check** (bump the saved-state version if a field is added) |
