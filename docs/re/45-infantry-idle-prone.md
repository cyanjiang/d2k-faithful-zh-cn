# 45: Infantry prone and idle moves (1.06)

What makes infantry lie down, how long they stay down, what idle
infantry do, and how each animation is drawn. Closes the open items
"prone and idle infantry moves" (doc 09), "prone" (doc 18) and part of
"infantry sub-position choice" (doc 08). Decompiles come from the local
GhidraMCP server (`re/ghidra_http.py`). `rand()` is the MSVC RNG
(`thunk_FUN_004b1810(file, line)`); the line argument is quoted below as
an identifier, the same way our code comments do.

"Verified" means read from the code or the data. Ticks are game ticks
(20 per second at the default speed, doc 02).

## Functions

| Address | Role |
|---|---|
| `FUN_004940d0` | infantry animation tick: frame advance, **prone decision, stand up, idle move** |
| `FUN_00494050` | `SetInfAnim(u, mode)`: set mode (+4 when prone), clamp frame |
| `FUN_004954b0` | one turn step toward a goal facing (used by the idle move) |
| `FUN_00493eb0` | `TurnBody`; calls `SetInfAnim(u, 0)` while turning |
| `FUN_00497b90` | unit update: calls `FUN_004940d0` first (0x497c33), keeps +0x2a |
| `FUN_0049d2e0` | `DamageTiles`: sets unit flag 1 (hit), death mode |
| `FUN_00495b90` | unit fire: `SetInfAnim(u, 2)` at 0x495fd6 |
| `FUN_00494260` | MoveStep: `SetInfAnim(u, 1)` at 0x494977; crawl halves speed |
| `FUN_0049cde0` | unit killed: random death mode if none yet (rand line 0x1bd2) |
| `FUN_0049f8f0` | crushed: `SetInfAnim(u, 13)` at 0x49f996 |
| `FUN_00428e30` | unit draw: image = `0x71aca0[(element·35 + frame)·32 + rotation]` |
| `FUN_00455870` | create: +0x20 = 0, +0x24 = 0, +0x25 = 0, **+0x2a = 0xFF** |

## 1. Unit fields (verified)

| Off | Meaning |
|---|---|
| +0x1e | animation frame |
| +0x20 | animation mode (table §4) |
| +0x24 | prone flag (0 / 1) |
| +0x25 | frame sub-counter: the frame advances on every 3rd call |
| +0x2a | **"hit age"**: 0 when hit, then +1 at each of the unit's 8-tick slots, saturating at 255. Created as 255. |
| +0x08 bit 0 | hit this tick (set by `FUN_0049d2e0`, cleared by the unit update) |

Correction to doc 09 §2: +0x2a is not an idle counter; it counts
8-tick slots since the last hit (doc 18 already says this). "Idle" in
§3 means "not hit for 255 slots", not "standing still".

### +0x2a bookkeeping (`FUN_00497b90`, verified)

After `FUN_004940d0` has run (so the animation tick sees last tick's
value):

```
if flags & 1:  +0x2a = 0; flags &= ~1        (no increment this tick)
elif (tick & 7) == (index & 7) and !(flags & 0x100 carried):
    +0x2a = min(+0x2a + 1, 255); if +0x2a > 100: flags &= ~8
```

Flag 1 is set for every unit inside the damage radius
(`FUN_0049d2e0`, before the hit-point test), so any damaging explosion
counts, including friendly fire and 0-damage hits. The Deviator path
clears flag 1 again and sets +0x2a = **1** (not 0), so a deviator hit
can never make infantry go prone (doc 40).

## 2. Prone (verified, `FUN_004940d0`)

Nothing else reads or writes +0x24 except `FUN_004940d0` (sets 0/1),
`FUN_00494050` (reads it) and creation (0). No order makes infantry lie
down; there is no "prone" command in 1.06.

### 2.1 The animation tick

Runs for infantry (rec+0x11) every unit update, in every state,
including dying ones:

```
if ++(+0x25) <= 2: return
+0x25 = 0; frame += 1
if first[mode] <= frame <= last[mode]: return           // still inside the mode
// the frame left the mode's range ("wrap"):
if 9 <= mode <= 13: frame = last[mode]; return           // death / crush hold
if +0x2a == 0:                                           // hit recently
    r = rand()                     // line 0x354
    if r % 5 == 0: +0x24 = 1; mode = 4                   // go prone
    // else: mode unchanged (a fire / walk / prone cycle just restarts)
elif +0x24 == 1 and +0x2a < 40:  mode = 4                // stay prone
else:                            +0x24 = 0; mode = 0     // stand (also gets up)
if +0x2a == 255:                                         // §3 idle move
    r = rand()                     // line 0x37b
    if r % 400 < 2:
        if facing < 8:  TurnStep(u, 8);  frame = first[mode]; return
        if facing > 24: TurnStep(u, 24); frame = first[mode]; return
        mode = 14 + r % 400                               // 14 or 15
frame = first[mode]
```

At most one `rand()` per call: the two branches need +0x2a = 0 and
+0x2a = 255.

### 2.2 Rules with numbers

- **When.** Only at a wrap while +0x2a = 0. +0x2a is 0 from the update
  after the hit until the unit's next 8-tick slot (inclusive), so
  1..8 animation ticks; a standing unit wraps every 3 ticks, so a single
  hit gives 0..3 rolls of 1 in 5. Continuous fire keeps resetting +0x2a
  and keeps rolling. Rolls happen whatever the state: walking, firing
  and already-prone units roll too.
- **Failed roll** leaves the mode as it is (a firing unit repeats its
  fire cycle 7..11, a walker its walk cycle).
- **How long.** Prone holds while +0x2a < 40: about 40 slots = 313..320
  ticks (≈ 16 s) after the **last** hit. Every new hit restarts it.
- **Getting up.** The first wrap with +0x2a ≥ 40 sets +0x24 = 0, mode 0.
  Prone stand (mode 4) wraps every 3 ticks, so the unit stands within 3
  ticks of +0x2a reaching 40. There is no getting-up animation.
- **Speed.** MoveStep halves the step speed (`speed >>= 1`) when the
  mode is 5 (crawl) **at the start of a cell step** (doc 09 §5). A prone
  unit whose mode is 4 at that moment (its crawl cycle just wrapped)
  walks that cell at full speed. Faithful quirk; keep the `mode == 5`
  test.
- **Firing / walking while prone.** `SetInfAnim` adds 4 to modes < 3:
  walk 1 → crawl 5, fire 2 → prone fire 6, stand 0 → prone stand 4.
- **Damage.** No prone modifier: `FUN_0049d2e0` computes
  `pct[warhead][armour] · damage / 100` with radius fall-off and never
  reads +0x24; the bullet update `FUN_0049be60` does not read it either.
  Verified 2026-10-16 by a scan of every byte access with displacement
  0x24: on unit objects the only readers are `SetInfAnim` (`0x49408c`)
  and the animation tick (`0x494166`..`0x494186`); the flight step
  `FUN_00494fd0` uses the same byte for the wing animation of kinds 9 /
  15 (aircraft, never infantry, `0x495020`); the rest are CreateUnit
  (`0x4559a9`), the debug dump (`0x45930f`), the record parser and other
  structures (order records, AI groups).
- **Frames.** Prone stand 13, crawl 14..16, prone fire 17..21 (§4).

## 3. Idle moves (verified, `FUN_004940d0`)

- **When.** At a wrap with +0x2a = 255, i.e. not hit for ≥ 255 slots
  (≈ 2040 ticks, 102 s), or never hit (created at 255). It is not tied
  to state 2: moving and firing units roll too (the move step / fire
  then overrides the mode on the same or next tick).
- **Chance.** `rand() % 400 < 2`, one roll per wrap. A standing unit
  wraps every 3 ticks: on average one idle move every 200 rolls = 600
  ticks (30 s).
- **Facing 0..7 or 25..31** (the northern half: facing 0 is north, 8
  east, 16 south, 24 west, as the Death Hand's north / south flight and
  the edge facings of `FUN_0049e6b0` show, docs 41 §5.1 / 13 §5.2): no
  animation; one turn step toward facing 8 (from 0..7) or 24 (from
  25..31) through `FUN_004954b0`. Each success
  moves one step, so such a unit slowly turns to 8 or 24 over several
  idle moves, and only then plays idle animations.
- **Facing 8..24:** mode = 14 (r % 400 = 0) or 15 (r % 400 = 1). Both
  play frames 22..29 (8 frames × 3 ticks = 24 ticks) in rotation slot
  5 or 6 (§4). At the end the wrap sets mode 0 and **rolls again in the
  same call**.
- **Protection.** `SetInfAnim(u, 0)` (state 2 every tick, TurnBody)
  does not interrupt modes ≥ 14; a non-zero mode (walk 1, fire 2, death)
  does.
- **No sub-cell shuffle.** State 2 for infantry is `SetInfAnim(u, 0)`
  (0x498129) and the auto-fire scan; it makes no `rand()` call and never
  moves the unit. The only infantry sub-cell move is "move aside"
  (state 9, `FUN_00497af0` / `FUN_0045dee0`, doc 09 §8), asked by a
  blocked friendly unit. The turret wander `FUN_00494210` only runs for
  turreted records (rec+0x26).

`TurnStep` = `FUN_004954b0(goal, u)`: if facing = goal, return. If the
body turn counter +0x1a is 0: reload it with rec+0x03 and step facing
by +1 when `(goal − facing) & 31 < (facing − goal) & 31`, else −1
(& 31). Otherwise +0x1a −= 1 and no turn. No `rand()`.

## 4. Drawing (verified, `FUN_00428e30`)

Mode table at `0x4e9530` (u32 first, last), read from the exe:

| Mode | Frames | Use | Set by |
|---|---|---|---|
| 0 | 0 | stand | wrap, `SetInfAnim(0)` |
| 1 | 1–6 | walk | MoveStep `SetInfAnim(1)` |
| 2 | 7–11 | fire | fire `SetInfAnim(2)` |
| 3 | 12 | unused (no caller passes 3) | – |
| 4 | 13 | prone stand | wrap, 0 + 4 |
| 5 | 14–16 | crawl | 1 + 4 |
| 6 | 17–21 | prone fire | 2 + 4 |
| 7 | 12 | unused | – |
| 8 | 7–11 | thumper deploy (state 0x20) | 0x49b4bd |
| 9 | 22–33 | death (rotation slot 0) | damage / killed |
| 10–12 | 22–29 | deaths (slots 1–3) | `9 + infdeath`, or `9 + rand() & 3` |
| 13 | 22–29 | crushed (slot 4) | crush |
| 14, 15 | 22–29 | **idle animations (slots 5, 6)** | §3 |

Image: `0x71aca0[(element·35 + frame)·32 + rot]`, element = rec+0x14
(base image), `rot = rec+0x3c[facing]`, **except mode > 8: rot = mode −
9**. So frames 0..21 are drawn per facing (8 rotations for infantry),
and frames 22+ use the rotation slot as an animation number. Light
infantry is 34 frames × 8 slots (doc 08: entries 206..477). Slot 7 of
frames 22..29 is never drawn. What slots 5 and 6 depict does not
matter for the port: the code draws them for modes 14 / 15 whatever
they show, and ours draws the same entries (`render/objects.cpp`).

`SetInfAnim(u, m)` (`FUN_00494050`), infantry only: skip when m = 0 and
(mode ≥ 14, or mode is 2 / 6 and the unit has a target). Else mode = m,
+4 if prone and m < 3, and **only if the frame is outside the new
mode's range** set it to the first frame. +0x25 is not touched.

## 5. Our code (as of cefad5d)

| What | Where | Status |
|---|---|---|
| Unit fields `anim_mode`, `prone`, `hit_age`, `timer` (+0x25) | `src/sim/world.hpp:47,80,81,65` | present, saved (`save_state.cpp:125,128`) |
| +0x2a reset on hit / +1 per slot | `src/sim/world.cpp:829-838` | matches |
| Animation tick called before the state code | `src/sim/world.cpp:796` | matches (exe runs the crush check `FUN_0049fa50` before it; ours runs `crush_check` after, `world.cpp:862`, minor) |
| Wrap logic | `src/sim/combat.cpp:1127-1144` | **missing**: prone roll (`rand() % 5`), "failed roll keeps the mode", stay prone < 40, idle roll (`rand() % 400`), idle turn, modes 14/15. Today it always sets `prone ? 4 : 0`, and nothing ever sets `prone = true`. |
| `SetInfAnim` | `src/sim/combat.cpp:1117-1125` | matches `FUN_00494050` |
| Fire | `src/sim/combat.cpp:865` | **differs**: uses `set_inf_mode(u, prone ? 6 : 2)` (frame and +0x25 reset); exe calls `SetInfAnim(u, 2)` (clamp only, +0x25 kept) |
| Death by damage | `src/sim/combat.cpp:673` | **differs**: `set_inf_mode`; exe `SetInfAnim(9 + infdeath)` (0x49d9d5) |
| Crush | `src/sim/combat.cpp:1487` | **differs**: `set_inf_mode(13)`; exe `SetInfAnim(13)` |
| Killed | `src/sim/combat.cpp:694` | matches (rand line 0x1bd2) |
| TurnBody | `src/sim/world.cpp:435` | **bug**: `u.anim_mode = 0` raw; exe calls `SetInfAnim(u, 0)` (prone → 4, keeps fire-with-target and idle modes, clamps the frame) |
| State 2 `SetInfAnim(u, 0)` every tick (0x498129) | `src/sim/combat.cpp:949-954` | **missing** |
| State 3 arrival `SetInfAnim(u, 0)` (0x498d6d, after MoveStep returns 0) | `src/sim/combat.cpp:994` | **missing** |
| Crawl speed `mode == 5` | `src/sim/world.cpp:489` | matches |
| Draw rotation `mode > 8 ? mode − 9 : facing` | `src/render/objects.cpp:124` | matches; modes 14/15 will draw once the sim sets them |
| Mode table | `src/sim/combat.cpp:28` | matches `0x4e9530` |

The missing state-2 / TurnBody calls matter for the RNG, not only for
looks: they decide when a unit is in mode 0 (wraps every 3 ticks) versus
a longer cycle, and so when the prone / idle `rand()` calls happen.

## 6. Implementation checklist

1. `tick_infantry_anim` (`combat.cpp:1127`): implement §2.1 exactly.
   Order inside one call: frame advance; death hold; then **either**
   `rng_.next()` (line 0x354) when `hit_age == 0` **or**, after the
   prone / stand decision, `rng_.next()` (line 0x37b) when
   `hit_age == 255`. On a failed prone roll leave `anim_mode` unchanged.
   Use the same `r` for `r % 400 < 2` and `14 + r % 400`.
2. Add `turn_step(u, goal)` = `FUN_004954b0` (§3) for the idle turn;
   do not reuse `turn_body` (it has a different counter order and calls
   `SetInfAnim(0)`).
3. `turn_body` (`world.cpp:435`): replace `u.anim_mode = 0` with
   `set_inf_anim(u, 0)`, called only when the facing differs from the
   goal (as `FUN_00493eb0`).
4. State 2 (`combat.cpp:949`): for infantry call `set_inf_anim(u, 0)`
   at the start, every tick, before the harvester return and the
   auto-fire.
5. State 3 (`combat.cpp:994`): when the final `move_step` returns 0
   (arrived at the guard cell), `set_inf_anim(u, 0)` (infantry), then
   the exe continues with the auto-fire (0x498d79). Done 2026-10-14:
   the tail now runs that auto-fire and the turret wander (doc 18 §3.6).
6. Fire (`combat.cpp:865`), death (`:673`), crush (`:1487`): call
   `set_inf_anim(u, 2 / 9 + infdeath / 13)` instead of `set_inf_mode`
   (no frame / +0x25 reset). Keep `set_inf_mode` only where the exe
   writes +0x20 directly.
7. Keep +0x2a semantics: created 255, reset by flag 1 in the next
   update (after the animation tick), deviator hits set 1.
8. RNG order per unit update (infantry): [crush check: no rand] →
   animation tick (≤ 1 rand: 0x354 or 0x37b) → (non-infantry smoke
   rands 0xebb/0xebc do not apply) → state code (fire scatter 0x8a0 /
   0x8a1, etc.). Units are updated in player order, list order
   (doc 18 §3.7), so every infantry unit of every side, the AI's too,
   consumes these calls.
9. Tests: (a) a never-hit standing soldier facing 16 calls `rand()`
   once every 3 ticks and enters mode 14/15 when `r % 400 < 2`;
   (b) a soldier facing 0 turns to 1 on a success; (c) hit at tick T,
   standing: prone iff a roll at a wrap in (T, next slot] gives
   `r % 5 == 0`; stands up at the first wrap with `hit_age ≥ 40`;
   (d) prone + move order: crawl frames 14..16, half speed on cells
   started in mode 5.
10. Update doc 09 §2 (+0x2a meaning), doc 09 §7 / doc 18 §3.8 (link
    here), `docs/parity.md` (unit movement / combat open items).
