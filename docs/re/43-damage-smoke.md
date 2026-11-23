# 43: Damage smoke and flames on units and buildings (1.06)

Covers the smoke that rises from damaged vehicles and the smoke and flames
on damaged buildings: the timers set by the damage function, the per-tick
spawns in the unit and building updates, the BUILEXP smoke-point table,
the exact `rand()` calls, and how the effects are drawn. Written because
the playtester sees no smoke at all in our game.

"Verified" means read from the disassembly (the decompile of the unit
update around `0x497e70` is garbled). Guesses are marked TODO(verify).
`rand()` is the exe's MSVC LCG `FUN_00448460` (doc 23 §1); the "line"
given for each call is its source-line argument, useful when tracing.

## 1. Functions, globals and data

| Address | Role |
|---|---|
| `FUN_0049d2e0` | DamageTiles: sets the unit timer (`0x49db5f`) and the building timer + points (`0x49d73f`) |
| `FUN_00497b90` | unit update; smoke block `0x497e81`–`0x497f4d` |
| `FUN_0049b7f0` | building update; smoke block `0x49baac`–`0x49bc33` |
| `FUN_00456df0` | create an explosion object (doc 14 §4.1) |
| `FUN_0049dd80` | explosion update (doc 14 §4.4) |
| `FUN_004282f0` | explosion draw passes (doc 14 §5.1, §5.3) |
| `FUN_0046ad80` | ReadSmokeData: loads `bin\BUILEXP.BIN` into `0x77d3a0` (called from data set-up `FUN_00466140`) |
| `FUN_00455870` | CreateUnit: unit +0x27 = 0 (`0x4559c2`) |
| `FUN_004563b0` | CreateBuilding: +0x3D = 0 (`0x4564eb`); +0x4C is not written |
| `FUN_0046bdb0` | pool free: zeroes the first 0x7C bytes, so a new building starts with +0x4C = 0 |
| `FUN_0046d2e0` | ConvertUnit: copies the 0x7C bytes, so the new copy keeps +0x27 |

| Global / table | Meaning |
|---|---|
| `0x5173f4` | game tick counter |
| `0x6d6113` | `ids[59]` of the Templates.bin ids block `0x6d60d8` = **11 = EX_SMOKE3** (unit smoke) |
| `0x77d3a0` | smoke-point table, 100 × 33 bytes, indexed by building type (+0x34) (§3.1) |
| `0x6e5c40[e·32]` / `0x6e5880[e]` | frame 0 / base image of building element e (doc 08) |

Fields: unit **+0x27** = smoke timer (u8). Building **+0x3D** = smoke
timer (u8), **+0x4C** = smoke-point bits (u32, bit i = point i).

All writers of these fields in the exe (capstone scan of `.text` for
writes to `[reg+0x27]` / `[reg+0x3d]` / `[reg+0x4c]`, then filtered by
function; verified):

| Field | Writers |
|---|---|
| unit +0x27 | `0x4559c2` CreateUnit = 0; `0x49db94` damage = rand; `0x497eb1` update = 0 above half hp; `0x497f4d` update −1; ConvertUnit block copy (`0x46d53b`) |
| bldg +0x3D | `0x4564eb` CreateBuilding = 0; `0x49d79a` damage = rand; `0x49bc30` update −1 |
| bldg +0x4C | `0x49d7aa` damage \|= rand; `0x49bc08` update clears bit i; pool free zeroes it |

(Record +0x27 written by the unit-record parser `FUN_00463990` at
`0x463ffb` is the template, not the object.)

## 2. Units

### 2.1 Damage (`FUN_0049d2e0` `0x49db5f`, verified)

In the "survives" branch (`dmg < hp`, doc 18 §2.2), after the harvester
voice and before the attacker record:

```
if kind (rec+0x2D) != 5 (worm) and hp < (maxhp >> 1):      // signed hp, unsigned max
    +0x27 = (rand() & 0x7F) + 0x40                          // rand line 0x1dba
```

Every such hit **re-rolls** the timer (it is overwritten, not maxed).
Infantry get it too (the rand is consumed) but never smoke (§2.2). The
killing hit does not touch it.

### 2.2 Unit update (`FUN_00497b90` `0x497e81`–`0x497f4d`, verified)

Runs for **every** unit in the side's list, aircraft and dying units
(state 0x11) included: the exe has no state test before this block. Its
place in the update: after the infantry crush test (`0x497bd6`), the walk
frame (`FUN_004940d0`), self-repair (`0x497c3b`), the saboteur charge,
the reload counter, `FUN_0049f610` (stealth), the attacker and target
validity checks (`0x497ce5`, `0x497d93`) and the frigate player lookup;
before the +0x2C counter (`0x497f50`) and the state switch. None of the
steps between self-repair and this block call `rand()` for a non-infantry
unit (verified 2026-10-16: `FUN_0049f610` and its helpers
`FUN_0049f5c0`, `FUN_0049f5f0`, `FUN_0049f750`, `FUN_0049f580` call no
`rand()`; their only callee chain is `FUN_004a5720` → `FUN_004a5640` →
`FUN_004a4660` / `FUN_004a5570`, none of which calls it).

```
if +0x27 != 0 and rec+0x11 (infantry flag) == 0:
    if hp > (maxhp >> 1):                 // signed hp vs unsigned max/2
        +0x27 = 0                          // healed: stop at once, no rand
    else:
        r0 = rand()                        // line 0xebb, every tick
        if (((r0 & 1) + tick) & 3) == 0:
            a = rand()                     // line 0xebc
            y = (+0x38 / 65536) + (a & 3) - 2      // y first!
            b = rand()                     // line 0xebc
            x = (+0x34 / 65536) + (b & 7) - 4
            FUN_00456df0(side, EX_SMOKE3, x, y, height = +0x3C (altitude, 16.16),
                         flags 0, delay 0, 0, 0)
        if (tick & 3) == 0: +0x27 -= 1
```

- `/ 65536` is the exe's `CDQ; AND EDX,0xFFFF; ADD; SAR 16`, a signed
  division that truncates toward zero.
- So: one rand per tick while smoking, plus two per puff. A puff comes on
  average every 4 ticks: each tick with `tick & 3` = 0 or 3 is a chance
  of 1/2, so puffs come irregularly (1, 3, 4, 5, ... ticks apart). Offsets: x −4..+3 px, y −2..+1 px around the unit centre.
- Duration: the timer 64..191 drops every 4th tick, so **256..764
  ticks** (about 13 to 38 s at 20 ticks/s), restarting on each new hit.
- **Exclusions:** infantry (rec+0x11 ≠ 0, the flag, not kind) and worms
  (the timer is never set). **Aircraft smoke** at their altitude. A unit
  that dies while smoking keeps smoking during its state-0x11 countdown
  (hp is 0, the timer is not cleared). Both follow from the code: the
  block has no state or aircraft test, and the puff takes +0x3C as its
  height (verified).
- **Stops** when the timer reaches 0, or on the first update with
  hp > max/2 (repair pad, or a heal). The self-repair of flag 0x800000
  units stops at half, so a damaged harvester smokes until the timer ends.

## 3. Buildings

### 3.1 BUILEXP.BIN, the smoke-point table (`FUN_0046ad80`, verified)

Retail (`0x4eb014` = 1) reads `Data\bin\BUILEXP.BIN` raw: 3,300 bytes =
100 rows × 33, row = building type index (the record index, +0x34). The
debug text path reads `builexp.dat` (a building name, then lines of
"explosion x y period" until a terminator).

| Off | Type | Meaning |
|---:|---|---|
| 0x00 | u8 | n = number of points (0..8) |
| 0x01 + i | i8 | x offset of point i (px, from the building's left) |
| 0x09 + i | i8 | y offset of point i (px, from the top of the image frame, §3.3) |
| 0x11 + i | u8 | explosion type of point i |
| 0x19 + i | u8 | period of point i (ticks) |

Examples from the 1.06 file (type: point = explosion x, y / period):

| Type | Points |
|---|---|
| 0 ATREIDES CONSTRUCTION YARD | FLAME 40,19/8; SMOKE3 40,19/3; FLAME 72,39/8; SMOKE3 72,39/4; SMOKE3 16,46/6 |
| 10 ATREIDES WIND TRAP | FLAME 40,2/8; SMOKE3 40,2/3; SMOKE3 12,30/4 |
| 23–25 MEDIUM GUN TURRET, 29–31 LARGE | SMOKE3 16,16/4 |
| 38–40 SILO | SMOKE3 15,5/5 |
| 41 ATREIDES HEAVY FACTORY | FLAME2 34,37/10; SMOKE3 34,37/3; FLAME 61,59/12; SMOKE3 61,59/4 |
| 50 ATREIDES REPAIR PAD | SMOKE3 51,7/4; SMOKE3 76,55/4 |
| 3, 45, 60, 61 (Imperial CY / heavy factory, special outposts) | none (n = 0) |

Only EX_SMOKE3 (11), EX_FLAME (39) and EX_FLAME2 (40) occur. The
"flames on heavily damaged buildings" are these same points: there is no
separate flame system. Every building record in the 1.06 Templates.bin
that has points also has record flag 0x200 (walls / concrete have
neither).

### 3.2 Damage (`FUN_0049d2e0` `0x49d73f`–`0x49d7aa`, verified)

After hp = max(hp − dmg, 0) in the "survives" branch:

```
if hp < (maxhp >> 1) and record flags (+0x58) & 0x200:
    +0x3D = (rand() & 0x7F) + 0x40        // line 0x1d1a
    +0x4C |= rand()                        // line 0x1d1b
```

rand() is 0..0x7FFF, so each hit switches on a random subset of points
0..7 (on average half); more hits light more points. Flag 0x200 is the
record's "selectable / normal building" bit (doc 10 §3); here it only
gates smoke.

### 3.3 Building update (`FUN_0049b7f0` `0x49baac`–`0x49bc33`, verified)

Place: after the build-up / animation counters, the reload counter, the
target check, the 0x40000 step, the **off-concrete decay** (`0x49ba01`,
−1 hp every 16 ticks above half) and the flag-8 early return
(`FUN_0044b1d0`), and after the +0x41 hit counter; before the state
switch (turret combat).

```
if +0x3D != 0 and rec.flags & 0x200 and state (+0x30) != 0x11 and (flags & 6) == 0:
    t = type (+0x34); n = BUILEXP[t].n
    img = 0x6e5c40[elem·32]  (frame 0 of element rec+0x14), or 0x6e5880[elem] if null
    for i in 0 .. n-1:
        r1 = rand()                                        // line 0x1953, always
        if ((r1 & 1) + tick) % period[i] == 0 (unsigned) and +0x4C & (1 << i):
            x = (+0x00 / 65536) + xoff[i]
            y = (+0x04 / 65536) + yoff[i] - img.u8[+0x19]   // frame height
            FUN_00456df0(owner, type[i], x, y, height 0, flags 0, delay 0, 0, 0)
        r2 = rand()                                        // line 0x1957, always
        if ((r2 & 0x7F) == 0 and +0x3D < 50) or hp > (maxhp >> 1):
            +0x4C &= ~(1 << i)                             // this point goes out
    if (tick & 3) == 0: +0x3D -= 1
```

- **Two rands per point per tick** for as long as the timer runs, even
  for points whose bit is off and even after a repair above half. With
  n = 0 there are none, but the timer still counts down.
- The x / y sums are done in 16-bit registers; only the low 16 bits reach
  the explosion (it stores px << 16), so plain int sums give the same result.
- Building +0x00 / +0x04 are the 16.16 bottom-left of the footprint
  (doc 08), so `y − frame height` is the top of the image frame.
  `img+0x19` is the R8 "frame height" byte (file offset 26): the runtime
  image object is the entry without its type byte (docs/formats/r8.md),
  which a second reader confirms, the blitters' test of `img+0x18` (file
  offset 25, bits per pixel) against 8 and 16 (doc 18 §7b, verified).
  Frame 0 of the CY element (5,
  entry 3103) has frame height 128; its base entry 3102 is empty.
- A point goes out for good when its bit clears: randomly (1/128 per tick)
  once the timer is below 50, or at once above half hp. Bits are never
  set again except by a new hit; leftover bits stay for the next episode.
- Duration: 64..191 timer steps × 4 = 256..764 ticks per hit.
- Exclusions: dying (state 0x11), building up or being sold (flags 2 / 4),
  records without flag 0x200.

## 4. Drawing (doc 14 §4.4, §5, verified)

The puffs and flames are ordinary explosion objects in the owner's list
(appended), updated by `FUN_0049dd80` and drawn by `FUN_004282f0`; nothing
is special:

| Type | Frames | Default flags | Look |
|---|---:|---|---|
| EX_SMOKE3 (11) | 8 | 0x3: rises 2 px per tick, darken blit | frames 1–7, 3 ticks each (22 updates); rises ~42 px |
| EX_FLAME (39) | 11 | 0x200 additive | frames 1–10, 30 ticks |
| EX_FLAME2 (40) | 12 | 0x200 additive | frames 1–11, 33 ticks |

- Explosions below 20 px height are drawn after every building and ground
  unit (not y-sorted), those at 20 px or more after low aircraft. A smoke
  puff therefore moves from pass 0 to pass 1 as it rises. Aircraft smoke
  starts at the aircraft's altitude.
- No sound (the smoke and flame types have sample −1, and these spawners
  play none).
- The exe updates explosions per side after that side's units and
  buildings, so a puff spawned this tick advances to frame 1 in the same
  tick and frame 0 is never shown.

## 5. Our code (2026-10-13)

Historical (the state before the port; everything below is done now,
parity row "Combat"). The per-side tick order (§6 item 6) was done on
2026-10-16: `World::tick` runs units, buildings, bullets and explosions
side by side as `FUN_00458e90` (verified), with the exe's linked-list
rule for objects appended during a pass; test
`tests/sim/test_combat.cpp`.

What exists:

- `src/sim/world.hpp:98` `Unit::smoke` (+0x27) and `:194-195`
  `Building::smoke` / `smoke_points` (+0x3D / +0x4C); saved in
  `src/sim/save_state.cpp:132`, `:143` (state version 13).
- `src/sim/combat.cpp:609-611` (building) and `:645` (unit) set them
  exactly as §2.1 / §3.2, rand order included.
- `src/sim/capture.cpp:401` ConvertUnit copies the whole unit (keeps the
  timer, as the exe).
- `src/sim/combat.cpp:254` `spawn_effect_at` (= `FUN_00456df0`, height
  16.16), `src/sim/aircraft.cpp:597` `tick_effects` (rise flag 1, frames),
  `src/app/game_screen.cpp:776` `draw_effects` (darken 2, additive 0x200,
  the 20 px passes). These already handle EX_SMOKE3 / EX_FLAME correctly.

What is missing (why nothing smokes):

1. **No reader of the timers.** Nothing in `src/sim/world.cpp` (unit loop
   `World::tick`, ~`:770-937`) or `src/sim/placement.cpp:274`
   (`World::tick_buildings`) counts them down or spawns anything. The
   "TODO: the draw" comments in world.hpp:98 / :194 are misleading: it is
   the update, not the draw, that is missing.
2. **No BUILEXP.BIN table.** No parser or field exists (grep finds no
   `builexp`).
3. **No frame height of frame 0** per building element.
   `World::building_frame` (`world.hpp:259`, filled at
   `game_screen.cpp:175-178`) holds the **base** image's frame size
   (entry `e.base`), which is empty (0) for most buildings (e.g. CY 3102).
4. Order details (pre-existing, affect smoke timing and RNG sync):
   - `World::tick` updates all units, then bullets, effects, then all
     buildings (`world.cpp:938-942`); the exe goes per side: units,
     buildings, bullets, explosions. Building smoke spawned after
     `tick_effects` would show frame 0 for one tick, and its rands are
     interleaved differently with other sides' units.
   - In `tick_buildings` the off-concrete decay runs last
     (`placement.cpp:317`); the exe decays before the smoke block, which
     can matter for the `hp > max/2` test.

## 6. Implementation checklist

1. **Formats:** parse `Data/bin/BUILEXP.BIN` (exactly 3,300 bytes, 100 ×
   33; reject other sizes) into
   `struct SmokePoints { uint8_t n; int8_t x[8], y[8]; uint8_t type[8], period[8]; }`,
   indexed by building record index. Load it with Templates (game_screen
   or the Templates loader) and hand it to the World. Golden test: row 0
   = 5 points as in §3.1; rows 3 / 45 / 60 / 61 empty; skip without data.
2. **Frame-0 height per building element:** in `game_screen.cpp` next to
   `building_frame`, store `frame_height` of entry `e.base + 1` when
   `e.frames > 0` and that entry is not empty, else of entry `e.base`
   (the exe's `0x6e5c40[e·32]` else `0x6e5880[e]`).
3. **Unit smoke** (`world.cpp`, unit loop): right after the self-repair
   block and **before** the aircraft branch (`if (u.flags & kAircraft)`)
   and the state-0x11 / worm `continue`s, so aircraft and dying units are
   included:
   ```
   if (u.smoke && !r.infantry) {
       if (u.hit_points > int(uint32_t(r.hit_points) >> 1)) u.smoke = 0;
       else {
           uint32_t r0 = rng_.next();                                  // 0xebb
           if ((((r0 & 1) + tick_count_) & 3) == 0) {
               int y = u.y / 65536 + int(rng_.next() & 3) - 2;         // 0xebc, first
               int x = u.x / 65536 + int(rng_.next() & 7) - 4;         // 0xebc, second
               spawn_effect_at(ids[59] /*EX_SMOKE3*/, x, y, u.owner, 0, 0, 0, u.altitude);
           }
           if ((tick_count_ & 3) == 0) --u.smoke;
       }
   }
   ```
   (`r` is needed before the aircraft branch; `ids` = Templates block
   0x6d60d8. Keep `tick_count_` equal to the exe's `0x5173f4` here, as the
   other `& 3` tests in this loop do.)
4. **Building smoke** (`placement.cpp` `tick_buildings`): for a building
   that is not dying, after the hit-age block and the selling early-out,
   before `tick_building_combat`; move the off-concrete decay up to before
   it:
   ```
   if (b.smoke && (r.flags & 0x200) && !(b.flags & 6)) {
       const auto& sp = smoke_points[record];
       for (int i = 0; i < sp.n; ++i) {
           uint32_t r1 = rng_.next();                                  // 0x1953
           if (((r1 & 1) + tick_count_) % sp.period[i] == 0 && (b.smoke_points & (1u << i)))
               spawn_effect_at(sp.type[i], (b.x >> 16) + sp.x[i],
                               (b.y >> 16) + sp.y[i] - frame0_h[r.base_element], b.owner, 0, 0, 0);
           uint32_t r2 = rng_.next();                                  // 0x1957
           if (((r2 & 0x7f) == 0 && b.smoke < 50) || b.hit_points > int(uint32_t(r.hit_points) >> 1))
               b.smoke_points &= ~(1u << i);
       }
       if ((tick_count_ & 3) == 0) --b.smoke;
   }
   ```
   Guard `period[i] != 0` only as a crash fix (the 1.06 data has none).
5. **RNG call order per tick** (what must match the exe):
   - damage: unit hit below half → 1 call; building hit below half (flag
     0x200) → 2 calls (timer, then points);
   - unit update, per smoking non-infantry unit at ≤ half hp: 1 call, +2
     when it puffs (y offset, then x offset), at the position in step 3;
   - building update, per smoking building: 2 calls per point i = 0..n−1
     (spawn test, then extinguish test), always.
6. **Tick order** (done 2026-10-16, see §5): the exe's per-side order
   (units, buildings, bullets, explosions of side 0, then side 1, ...),
   verified in `FUN_00458e90`; every existing golden test still passes.
7. The comments on `Unit::smoke` / `Building::smoke` are fixed (the draw
   needs nothing new). No save-format change is needed.
8. Tests (`tests/sim/`): a vehicle damaged below half gets a timer in
   64..191 and spawns EX_SMOKE3 within 8 ticks at the expected offsets
   for a seeded RNG; repairing it above half clears the timer with no
   rand call; infantry consume the damage rand but never puff; a CY below
   half with bit 0 set spawns EX_FLAME at (x + 40, y − 128 + 19); the
   rand counter advances by 2·n per tick per smoking building.
9. `docs/parity.md`: add a "Damage smoke" row; playtest a damaged CY,
   turret and tank against the reference game (positions, rate, duration).

## 7. Open

Closed 2026-10-16: the frame height `img+0x19` (§3.3), smoke on dying
units and aircraft (§2.2) and the stealth helpers (no `rand()`, §2.2)
are verified from the code; the per-side tick order is done (§5).
