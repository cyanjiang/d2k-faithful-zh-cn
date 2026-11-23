# 36. Sonic tank: SOUND bullet, in-flight damage, lens blit

Target: Dune 2000 1.06 US (`DUNE2000.DAT` image). Everything below was read
from the decompilation of the functions named. "Verified" means it was also
checked against the instructions (`re/disasm.py`) where arithmetic,
signedness or call arguments matter. Guesses are marked TODO(verify).

Builds on (not repeated here): doc 18 §1.1 (bullet table), §2 (damage
`FUN_0049d2e0`), §3.1 (unit fire `FUN_00495b90`), §3.2 (weapon choice),
§4.1–§4.4 (bullet creation, update, drawing), §3.8 (infantry animation
modes); doc 26 §5 (craters); doc 31 §6.1 (bloom burst).

## 0. Functions, globals, data

| Address | Role |
|---|---|
| `FUN_00456a30` | bullet creation; flag 0x800 sets the flight ticks from the range (§2.1) |
| `FUN_0049be60` | bullet update; flag 0x800 in-flight damage (§2.2) |
| `FUN_0049d2e0` | radius damage (doc 18 §2); no sonic special case (§3) |
| `FUN_00428070` | bullet drawing; flag 0x800 calls the lens blit first (§4.1) |
| `FUN_0042ada0` | `SetLensImage(x, y, image, surface)`: fills the bullet image from the screen through the lens table (§4.2) |
| `FUN_0042ace0` | builds the lens table `0x503320` for an n × n lens (§4.3) |
| `FUN_00466140` | data set-up; calls `FUN_0042ace0(16)` after the binary data load (`0x469723`), or `FUN_0042ace0(size)` when the text loader meets the bullet named "SOUND" (`0x467036`) |
| `FUN_00430770` / `FUN_0042fac0` | clipped keyed blit (16 → 16 bpp branch, §4.4) |
| `0x503320` | lens table: n·n pairs of i32 (source x, source y), row-major |
| `0x4be3e8` | double −2/3 (`0xBFE5555555555555`) |
| `0x714b00[6·32]` | the SOUND bullet image (DATA.R16 entry 3980, §1.3) |

## 1. Data (verified from Templates.bin, ARMOUR.BIN, samples.uib, DATA.R16)

### 1.1 Bullet 6 "SOUND" (table `0x782068 + 6·28`)

| Field | Value |
|---|---|
| speed (+0x00) | 393216 = 6.0 px per tick |
| damage (+0x04) | 80 |
| flags (+0x08) | 0x800 only (no 0x2 shadow, no 0x200 additive, no 0x1000 wall detonation, no 0x10 trail) |
| fire sound (+0x0C) | 8 = `S_SONIC` → `GAMESFX\sonic1.AUD` |
| image (+0x10) | 6 (one frame, `0x795340[6]` = 1) |
| impact explosion (+0x11) | 0xFF, none (so no impact sound either) |
| trail (+0x12) | 0xFF, none |
| +0x13 | 1 |
| anti-air (+0x14) | 0 |
| warhead (+0x15) | 12 W_SONIC |
| range (+0x18) | 160 px (5 cells) |

The text loader sets flag 0x800 for the bullet whose name contains
"SOUND" (doc 18 §1.1). Nothing else in the exe reads the bullet flags
from the table: the only reader of `0x782070` is `FUN_00456a30`, which
copies them to the bullet object.

### 1.2 W_SONIC (ARMOUR.BIN row 12)

Percent per armour: NONE 100, WALL 50, BUILDING 60, WOOD 100, LIGHT 100,
HEAVY 60, CONCRETE 100, INVULNERABLE 0, CY 20, HARVESTER 50. Radius
**64 px**. infdeath **3**.

Centre damage per hit, `(pct · 80) / 100`: NONE 80, WALL 40, BUILDING 48,
WOOD 80, LIGHT 80, HEAVY 48, CONCRETE 80, INV 0, CY 16, HARVESTER 40.

### 1.3 Unit record SONIC_TANK (record 16, `0x77e250 + 16·0x100`)

| Off | Value | Meaning |
|---:|---|---|
| +0x00 | 1 | Atreides only |
| +0x02 | 4 | armour LIGHT |
| +0x03 | 2 | body turn delay |
| +0x04 | 3000 | hit points |
| +0x08 | 0xC000 | speed 48 (×1024) |
| +0x0C / +0x0D | 6 / 0xFF | primary SOUND, no secondary |
| +0x0E | 90 | **reload delay** (ticks); doc 08's "? weapon range, SONIC 160" is wrong, doc 18 already corrected the meaning |
| +0x10 | 4 | view distance |
| +0x14 / +0x18 | 13 / −1 | body graphic element 13, **no turret** |
| +0x1C / +0x20 | 1000 / 20 | cost / build time |
| +0x24 | 6 | tech level |
| +0x26 | 0 | aims with the body |
| +0x28 / +0x30 | HEAVY_FACTORY / RESEARCH_CENTRE | prerequisites |
| +0x2F | 10 | death explosion (EX_BANG14 in the dump) |
| +0x35 | 1 | crusher (doc 18 §2.5) |
| +0x37 | 5 | muzzle offset, px along the facing |
| +0xA8 | 0xFF | no muzzle flash |

So it fires from 5 px in front of its centre, every 90 ticks at best,
with S_SONIC and no muzzle flash.

### 1.4 The image (DATA.R16 entry 3980)

Bullet image entries start at 3912; the frames before SOUND are LMG 1,
BAZOOKA 32, ROCKET 32, HMG 1, 80MM 1, 155MM 1 = 68, so SOUND is
**entry 3980**: type 1, **16 × 16, 16 bpp, offset (8, 8)**. Its pixels
are leftover memory from the authoring tool (mostly `0xFF00` with odd
values), never meant to be seen: the lens blit overwrites them before
every draw (§4). They are seen only in the stale case of §4.2.

The text loader builds the same thing: for "SOUND" it creates a blank
image (`FUN_0042ef90`) of the lens size with offset (size / 2, size / 2)
instead of loading frames, and checks that the frame count is 1.

## 2. Flight

### 2.1 Creation (`FUN_00456a30`, verified)

Standard creation (doc 18 §4.1), with one change:

```
ticks = (range << 16) / speed          // unsigned; (160 << 16) / 393216 = 26
```

The flight time does not depend on the target distance. dx / dy still
point at the (scattered) target pixel: `dx = speed · (tx − x0) / dist`.
So the wave always travels about 26 · 6 = 156 px, past a near target.
Height starts at 0x60000 (6 px) and never changes (no flag 1 / 0x100).
Fire: doc 18 §3.1 unchanged: scatter ±4 px per 32 px of distance,
S_SONIC at the muzzle cell, reload 90. There is no sonic case in
`FUN_00495b90`, `FUN_0049e9d0` (weapon choice), `FUN_00496480`
(auto-fire) or `FUN_00497270` (range test).

### 2.2 Update (`FUN_0049be60`, verified at `0x49c052`)

Order within one update (doc 18 §4.2 numbering):

```
1. firer check (firer = 0xFFFF if dead)
2. start delay
3. off the map (x or y < 0, cell >= map size): removed, no damage, no impact
4. flag 0x1000 (not set for SOUND)
5. flag 0x800:
       T = (range << 16) / speed                        // unsigned div, 26
       if (u32)(ticks << 1) < T:                        // unsigned compare
           DamageTiles(x >> 16, y >> 16,                // current pixel, BEFORE this tick's move
                       height = +0x2C (0x60000),
                       bullet type, side = the bullet's owner list,
                       firer = +0x14, deviate = 0)      // push 0 at 0x49c08d
6. flag 0x10 trail (not set)
7. flag 0x20 homing (not set)
8. flag 0x80 frame (not set)
9. no flag 0x100: if ticks == 0: impact, else ticks -= 1
10. no impact: x += dx, y += dy; return
11. impact: if the TARGET pixel (+0x1C / +0x1E) is on the map:
        DamageTiles(tx, ty, height, type, side, firer, deviate = flag 0x40 = 0)
    impact explosion 0xFF: no sound, no explosion; removed
```

Consequences (T = 26, speed exactly 6 px along an axis):

- The bullet is created and updated in the same tick (doc 18 §3.6).
  Update k (k = 0, 1, …) sees ticks = 26 − k and has moved k times.
- In-flight damage happens when `2 · ticks < 26`, i.e. ticks 12 … 0:
  **13 hits**, on updates 14 … 26, at **84, 90, …, 156 px** from the
  muzzle (k · 6). The first half of the flight does no damage.
- Update 26 (ticks = 0) does the last in-flight hit at 156 px **and** the
  impact hit at the scattered target pixel, in that order. A total of
  **14 `DamageTiles` calls** per shot.
- With radius 64 the in-flight hits cover 20 … 220 px from the muzzle.
- If the wave leaves the map before ticks reaches 0, it is removed
  silently at step 3: no further hits and no final hit at the target.

**Same unit hit repeatedly.** There is no hit list. Every call damages
everything within 64 px of that point (doc 18 §2), so a unit near the
path takes up to 13 in-flight hits plus the final one. Illustrative sum
for a LIGHT unit on the line of fire, no scatter, distance D from the
muzzle (fall-off as doc 18 §2.1, truncated per hit):

| D (px) | in-flight sum | + final hit (D = target) |
|---:|---:|---:|
| 32 | 22 | 102 |
| 64 | 228 | 308 |
| 96 | 602 | 682 |
| 128 | 707 | 787 |
| 156 | 465 | 545 |

**Friendly fire.** `FUN_0049d2e0` has no side check (doc 18 §2.1): the
attacker's own and allied units and buildings in the radius are damaged
too. The firing tank is never hit by its own wave in practice: the first
damaging point is 84 px from the muzzle (89 px from the tank's centre),
beyond the 64 px radius. There is no firer exclusion in the code.

**Height 6 px** = not "air" (`air = height > 0x60000`), so aircraft are
never hit and buildings are. No 0x1000 flag: the wave passes over walls
and damages them (WALL 50 %).

### 2.3 Side effects of every in-flight `DamageTiles` call (verified, `FUN_0049d2e0`)

- **Spice bloom burst**: if the wave's current cell holds an active
  bloom object (types 8, 9, 10), it bursts and the call returns: no unit
  or building damage on that tick (doc 31 §6.1).
- **Craters** (doc 26 §5 / doc 18 §2.4): on a craterable cell (0x4000,
  no spice 0x700000, no building 0x10) the crater byte grows by
  `(pct[W_SONIC][NONE] · 80 / 100) / 40 = 2` per call. At 6 px per tick
  that is about 10 per cell crossed in the damaging half. Concrete
  (0x800 and 0x8000) is destroyed once the byte passes 16.
- Attacker record (+0x30 / +0x32 = side, firer), neutral-to-hostile,
  kill counters, under-attack voices, harvester-attack voice: as for any
  hit (doc 18 §2.2 / §2.3), once per call.
- The final impact goes through the same function at the target pixel.

## 3. No other sonic special cases (verified)

- `FUN_0049d2e0`: the only bullet type it compares against is SPICEEXPL
  (`0x6d610a`). The warhead row supplies pct, radius and infdeath; nothing
  depends on flag 0x800.
- Infantry killed by W_SONIC get animation mode `9 + infdeath = 12`
  (`FUN_00494050`, call at the death path of `FUN_0049d2e0`). The draw
  (`FUN_00428e30`, line `if 8 < mode: rotation = mode − 9`) has no mode
  12 case: rotation slot 3 of frames 22–29, holding the last frame
  (doc 18 §3.8). Same as W_HE. No warhead-specific death sound: the
  death effects (`FUN_0049cde0`) do not read the warhead.
- Sound: only S_SONIC when firing (bullet +0x0C). No impact sound
  (impact explosion 0xFF).

## 4. Drawing: the lens

### 4.1 `FUN_00428070` (verified)

For each bullet with no start delay, in player order, list order:

```
img = 0x714b00[rec.image · 32 + frame]          // SOUND: entry 3980, 16×16, offset (8, 8)
sx  = (x >> 16) − img.off_x − scroll_x          // top-left of the image
sy  = (y >> 16) − img.off_y + top(20) − scroll_y
h   = height >> 16                              // 6 for SOUND
if flags & 0x800: SetLensImage(sx, sy − h, img, surface)   // FUN_0042ada0
if flags & 2:     shadow ...                    // not for SOUND
if flags & 0x200: additive ... else BlitClip(surface, clip, sx, sy − h, img, 1, 1)
clip = (0, 20) – (480, 400)                     // the playfield
```

The bullet pass runs after terrain, buildings, units and explosions
(`FUN_00428e30`) and before `FUN_00428220` and `FUN_00423fb0` in
`FUN_00428ca0` (doc 14 / doc 16 pass list), so the lens magnifies
everything drawn so far and is itself covered by the shroud.

### 4.2 `SetLensImage(x, y, img, surf)` = `FUN_0042ada0` (verified)

```
if img == NULL or img.w > 16 or img.h > 16: error "Invalid lensImage"
if x < 0 or y < 0: return                       // image NOT updated
n = img.w                                       // 16; used for rows AND columns
if x + n > surf.w or y + n > surf.h: return     // whole surface, not the playfield clip
if img.bpp == 16 and surf.bpp == 16:
    k = 0
    for row in 0..n−1: for col in 0..n−1:
        (lx, ly) = LENS[k]; k += 1
        img.pixels[row·n + col] = surf.pixels[(y + ly) · surf.w + (x + lx)]
elif both 8 bpp: same with bytes
else: error "invalid operation"
```

- It reads the **screen** (the surface being composed) and writes the
  **bullet image**; the screen is not modified here. The keyed blit
  right after draws the image back over the same 16 × 16 square.
- Pitch = `surf.w` (pixels); 16-bit pixels are copied as raw u16,
  no colour conversion.
- The image is **shared** by every SOUND bullet (one image per bullet
  type). Each fill is followed at once by its blit, so bullets do not
  disturb each other, except in the early-return cases: then the blit
  draws whatever the image last held (the previous lens capture, possibly
  of another sonic bullet elsewhere on screen, or the DATA.R16 leftovers
  if no capture happened yet). This happens when the image top-left is
  left of / above the surface (the bullet within 8 px of the left edge,
  or of the top of the surface after the 6 px height), or less than
  16 px from the right / bottom edge of the **surface**.
  The surface is the 640 × 400 game surface `0x5178b4` (verified: the
  world draw `FUN_00428ca0` passes its argument down to
  `FUN_00428070` and on to `FUN_0042ada0`, and its only caller
  `0x448fed` pushes `[0x5178b4]`; doc 11 §1). The stale square is what
  the code draws, so we keep it (`GameScreen::draw_lens` keeps the last
  capture per bullet image).
- Because the bounds are the surface's, not the playfield's, a wave
  near the playfield's top edge (y < 20) or right edge (x ≥ 480 − 16)
  reads pixels of the top bar / sidebar area as they are at that moment
  in the back buffer; the blit is clipped to the playfield anyway.
  The game surface is never cleared and the top bar and sidebar are
  drawn after the world (doc 11 §1 and §2), and the loop first puts back
  the cursor and tooltip backgrounds; so during the world draw those
  pixels are the top bar and sidebar of the previous loop. Ours is the
  same (`screen_` starts as UIBB, is never cleared, and the sidebar is
  drawn after `draw_bullets`). Only the lens rows inside the playfield
  are blitted, so this only feeds pixels into the magnified circle.

### 4.3 The lens table: `FUN_0042ace0(n = 16)` (verified, `0x42ace0`)

```
r = n >> 1                                      // 8, compared as an x87 integer
for j in 0..n−1:                                // rows
    dy = j − r
    for i in 0..n−1:                            // columns
        dx = i − r
        if sqrt(dx² + dy²) > r:                 // fild int, fsqrt, fcomp; je when neither C0 nor C3
            LENS[j·n + i] = (i, j)              // identity outside the circle
        else:                                   // inside, including sqrt == r
            LENS[j·n + i] = (r − ftol(dx · (−2/3)),  r − ftol(dy · (−2/3)))
```

`ftol` truncates toward zero, so the source offset is `trunc(2·d / 3)`:
a **1.5× magnifying lens** of radius 8 px centred on the image centre
(8, 8). 195 of the 256 entries are inside the circle. Per offset d
(−8 … 7) the source offset is:

| d | −8 | −7 | −6 | −5 | −4 | −3 | −2 | −1 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| src | −5 | −4 | −4 | −3 | −2 | −2 | −1 | 0 | 0 | 0 | 1 | 2 | 2 | 3 | 4 | 4 |

The x and y offsets are independent (separable) inside the circle.
Row 0 (dy = −8) has only column 8 inside; column 0 only row 8.

Precision note: `(±3) · (−2/3)` and `(±6) · (−2/3)` are 2.0 / 4.0 only when
the product is rounded to 53 bits (IEEE double, and x87 with the default
53-bit precision control the MSVC runtime sets). With 64-bit x87
precision they would be 1.999… / 3.999… and truncate to 1 / 3. The table
above uses double, which matches our float policy (CLAUDE.md §10: x87
precision quirks are not emulated), so this is settled by that
decision.

The table is built once at data load; n = 16 in the shipped (binary)
path.

### 4.4 The blit after the lens (`FUN_0042fac0`, 16 → 16, keyed + shadow, verified)

`BlitClip(..., 1, 1)` on a 16-bit image, per source pixel `c`:

- `c == 0xF81F`: skipped (transparent; the unmagnified screen pixel stays);
- `c == 0x001F`: "shadow", verified at `0x42fe9b`:
  `dst = ((dst >> 1) & (u32[0x797e6c] >> 1)) & 0xFF` (`and dl, al`;
  `movzx dx, dl`): only the low byte survives, an original quirk of
  this blit path;
- else: copied.

So a screen pixel that happens to have one of those two values is not
magnified but left / darkened. This is a side effect of reusing the
keyed sprite blit; reproduce it with our screen's equivalents of those
two key values. Verified: the keys are fixed immediates (`cmp ax,
0x1f` at `0x42fe9b`, `cmp ax, 0xf81f` at `0x42feb8`), whatever the
surface format; the shadow mask `0x797e6c` is set once at `0x4a443a`
from the surface's red mask (0x7C00, i.e. 555: 0xFBDE; else 565:
0xF7DE). On a 565 surface (the usual 16-bit mode, and the cnc-ddraw
reference) 0xF81F is magenta and 0x001F full blue. Our surface is
RGB555, so we use the same colours there (0x7C1F, 0x001F); the shadow
keeps the low byte, `((d >> 1) & 0xEF)`, which is the same for both
masks. On a 555 surface the exe would never match 0xF81F (bit 15), a
hardware-dependent difference we do not model.

Net visual effect: every render frame, a 16 × 16 square centred on the
wave (6 px above its ground position) shows the screen beneath it
magnified 1.5× inside a radius-8 circle; the corners outside the circle
are copied unchanged. No colour of its own, no animation, no frame
change: the "wave" is only this moving magnifier.

Frame rate: the lens is recomputed at every render (60 Hz in our
loop) from the current screen; the bullet position changes only per
sim tick (no interpolation in `FUN_00428070`).

## 5. Implementation checklist (our code)

Historical (the plan before the port). Every row is done now (parity
row "Combat": sonic waves, the lens draw, the 0x1000 detonation); the
Status column is the state at writing.

| Finding | Where | Status |
|---|---|---|
| SOUND bullet values (speed 6, dmg 80, 0x800, range 160, W_SONIC, S_SONIC, image 6, no impact / trail) | `formats/templates.cpp` (bullet table) | already loaded from Templates.bin; nothing to add |
| Sonic tank record (reload 90, muzzle 5, no turret, no flash) | `formats/templates.cpp`, `World::unit_fire` (`src/sim/combat.cpp` ~line 780) | generic code covers it; nothing sonic-specific |
| ticks = (range << 16) / speed | `World::add_bullet` (`src/sim/combat.cpp:309`) | **done** (uses int64 signed; exe is unsigned 32-bit, same result for these values) |
| In-flight damage: `if (u32(ticks) << 1) < (range << 16) / speed: damage_at(x >> 16, y >> 16, height, type, owner, firer)`, after the off-map check, before the trail / homing / tick decrement / move | `World::tick_bullets` (`src/sim/combat.cpp:349`) | **missing** (TODO comment there). Also add the 0x1000 wall check before it (step 4) for the order |
| Final impact at the target pixel, no explosion / sound | `World::tick_bullets` impact block | done (generic; `br.impact < 0` skips the explosion) |
| No deviate on in-flight hits | `World::damage_at` | no deviate parameter yet; pass 0 when the deviator lands |
| Bloom burst / craters / provoke / attacker record per in-flight call | `World::damage_at` | done (generic, called per hit) |
| W_SONIC infantry death = mode 12, rotation slot 3 | `combat.cpp:614`, `render/objects.cpp:118` | done (generic) |
| Lens table (16 × 16, r = 8, −2/3, trunc, `>` r identity) | new helper, e.g. `src/render/` or `app/game_screen.cpp` | **missing** |
| Lens blit: before the keyed blit, for flag 0x800: copy the 16 × 16 screen square at (sx − 8, sy − 6 − 8) through the table into a persistent 16 × 16 buffer, bounds against the whole surface (x, y ≥ 0, x + 16 ≤ W, y + 16 ≤ H), else keep the old buffer; then blit the buffer clipped to the playfield with the 0xF81F / 0x001F key rules | `GameScreen::draw_bullets` (`src/app/game_screen.cpp:767`) | **missing** (TODO comment there). The buffer is one per bullet type, initialised from DATA.R16 entry 3980's pixels. `draw_blit` cannot be used as is (it draws the R16 pixels); needs a draw of a runtime 16 × 16 u16 buffer |
| Draw order: bullets after units / explosions, before shroud | `draw_bullets` call site | done |
| Parity doc | `docs/parity.md` Combat row ("Open: sonic bullets") | update when implemented |
| Test idea | `tests/` | A sonic shot at a LIGHT unit 128 px away on an axis with no scatter possible (rand seeded) or a ground target: 13 in-flight hits on updates 14–26 at 84…156 px, plus the final hit; check HP after each tick, and that the firer is untouched and a friendly unit near the path is damaged |
