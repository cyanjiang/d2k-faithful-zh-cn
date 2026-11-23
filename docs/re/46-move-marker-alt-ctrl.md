# 46: Move marker (click puff), Alt crush move, Ctrl on own buildings (1.06)

Written for a port of three details of the in-game left-click handler
`FUN_004433a0`: the small puff drawn where a move / attack-ground order
is given, the Alt "crush" move onto enemy infantry, and Ctrl on an own
building with units selected. Extends doc 10 §3, §5, §7 and doc 14 §5.4,
and corrects both in places (§6).

"Verified" means it was read from the decompile and checked against the
disassembly. No open points remain (§8).

## 1. Functions and data

| Address | Name used here | Role |
|---|---|---|
| `FUN_00456d50(px, py)` | AddClickPuff | start a puff at a world pixel; **one caller**, `0x445d87` in `FUN_004433a0` (§2.1) |
| `FUN_00428220(surface)` | DrawClickPuffs | draws **and advances** the puffs; called from the world draw `FUN_00428ca0` at `0x428d51` |
| `FUN_00448470()` | ResetClickPuffs | sets every slot's frame to the frame count (all free); called at mission start, `0x448b22` in `FUN_00448550`, only if `0x797e1c == 0` |
| `FUN_004310c0` → `FUN_004310f0` | darken blit | per channel `max(dst − idx, 0)` (doc 14 §5.2, flag 2) |
| `FUN_004a57c0()` | AnyUnitSelected | any own unit with `+0x19` |
| `FUN_004a58b0()` | AnyArmedSelected | any own selected unit whose record `+0x0c` (primary weapon) ≠ 0xff |
| `FUN_004a58f0(u)` | AnyArmedSelectedExcept | the same, skipping unit `u` (the cursor's target) |
| `FUN_004a5a10()` | AnyCrusherSelected | any own selected unit whose record `+0x35` ≠ 0 |
| `FUN_004a5940()` | AnyNonInfantrySelected | any own selected unit whose record `+0x11` == 0 |
| `FUN_004a59c0(k)` | AllSelectedOfKind | at least one selected unit, and **all** of kind `k` (record `+0x2d`) |
| `FUN_004a57f0(k)` | AnySelectedOfKind | any selected unit of kind `k` (cursor only) |
| `FUN_00459450(p, cx, cy)` | order move | doc 10 |
| `FUN_004598d0(p, cx, cy)` | order attack ground | doc 10 |
| `FUN_004596f0` / `FUN_00459800` | order attack unit / building | doc 10 |
| `FUN_0049f240(1)` | acknowledge voice | doc 10 §6 |
| `FUN_0049f310` / `FUN_0049f320` | flash unit / building | doc 44 |

| Global | Meaning |
|---|---|
| `0x516490` | puff slots, 10 × 16 bytes: `+0` x, `+4` y (world pixels), `+8` frame (u32), `+0xc` draw counter (u32). End `0x516530`. |
| `0x6d6111` | puff explosion type = Templates.bin special-id byte `0x39` of the block `0x6d60d8` (set by the data set-up `FUN_00466140`, debug text loader `0x46728d`). In 1.06: **26 = EX_PUFF** |
| `0x6d5fd0[t]` | explosion frame counts; EX_PUFF = **5** |
| `0x6e2e20[t·40 + f]` | explosion images; EX_PUFF = DATA.R16 entries **4524–4528** |
| `0x51653c` | the packed **red** health-bar colour (doc 11), sits right after the slot table: see §2.2 |
| `0x797e88` / `0x797e89` / `0x797e8a` | Shift / Ctrl / Alt held |

The EX_PUFF numbers were read from `Data\bin\Templates.bin` (1.06):
special id `0x39` = 26, name "EX_PUFF", 5 frames, first entry
4273 + Σ frames(types < 26) = 4524 (agrees with doc 14 §6).

## 2. The click puff

### 2.1 Callers (verified)

`FUN_00456d50` has a single call, `0x445d87`, reached by three paths of
the left-release code (mode 0) in `FUN_004433a0`, always with the
release point in world pixels (`scroll_x + mouse_x`,
`scroll_y − 20 + mouse_y`), not the cell centre:

| Path | Condition | Sequence |
|---|---|---|
| shroud | cell shrouded (`0x4dfb04` set and cell `+8 & 0xf == 1`), a unit selected | move `0x445d7d` → puff → voice 1 `0x445d91` |
| empty cell, no Ctrl | no unit / building found, a unit selected | move → puff → voice 1 |
| empty cell, Ctrl | as above, `FUN_004a58b0` (armed) | attack ground `0x445d51` → puff → voice 1 |

No other order makes a puff: not the radar click (doc 10, `FUN_00459450`
from another site), not the Alt crush move (§3), not attacks, docking,
repair or the building orders. With no unit selected the shroud and
empty-cell paths do nothing (`0x445d5f` / `0x445d6a` → `0x445d99`).

### 2.2 Slot choice (`FUN_00456d50`, verified)

1. The first slot (0..9) whose frame ≥ `0x6d5fd0[EX_PUFF]` (= 5, free)
   gets x, y, frame 0, counter 0.
2. If none is free: the slot with the **largest** frame (strict `>`, so
   the first of equal ones; slot 0 if all are 0) gets x, y, frame 0. The
   counter is **not** cleared: the code clears `+0xc` of index 10 (the
   loop counter `EAX` instead of `EDI`, `0x456da5` / `0x456dba`), which is
   the global `0x51653c` = the red health-bar colour.

So the original bug writes **0 (black)** into the red health-bar colour.
It stays black until the next mission start (`FUN_0044acc0`, called at
`0x4485d7` in the mission loop, rebuilds it). `0x51653c` is read by the
16-bit health bars (`FUN_00428e30` `:617`, `FUN_0042af00` `:121`),
`FUN_004245e0` and `FUN_00493590` case 3. Only the health bars are
reachable (corrected 2026-10-16): `FUN_004245e0` is not the radar but a
debug cell-grid overlay on the playfield, called with `FUN_004241e0` and
`FUN_004243e0` at `0x449005..0x449022` only when `0x5178c8` ≠ 0, a byte
nothing writes. And `FUN_00493590` copies a
selected unit's route into a debug overlay that `FUN_004243e0` draws
(colour `0x503318`), and its two callers (`0x493b4a` / `0x493de7` in
`FUN_004936a0`) run only when `0x7984d0` ≠ 0 or `0x4eb008` = 0; neither
is ever written (`0x7984d0` has no writer, `0x4eb008` stays 1), so the
overlay never draws. The reused slot keeps its
old counter (0..2), so its first frame may show for 1 or 2 draws
instead of 3.

Reaching it needs 10 live puffs: 10 move / attack-ground clicks within
15 drawn frames (§2.3), about 0.25 s at 60 Hz. Not practical by hand, so
it is a curiosity; see §7 for what to do.

### 2.3 Animation (`FUN_00428220`, verified)

There is **no** tick update. The draw does it, once per call of the world
draw (`FUN_00428ca0`, from the main loop at `0x448fed` when the game view
is blitted):

```
for slot in 0..9:
    if frame < frames(EX_PUFF):
        draw image(EX_PUFF, frame)
        counter += 1
        if counter > 2: counter = 0; frame += 1
```

So each of the 5 frames shows for 3 drawn frames, 15 draws in all
(0.25 s at our 60 Hz), then the slot is free. It runs at the render
rate, not the 20 ticks/s game rate, and freezes when the world is not
drawn. In the main loop the world draw (`0x448fed`) comes **before** the
input handler (`0x449852`) in each iteration (loop head `0x448eb9`, back
jump `0x44a0b9`), so a puff added by a click is first drawn in the next
iteration, and frame 0 still gets its full 3 draws (15 in all). Our
loop handles input before the draw, so every input effect (puffs,
selections, orders' feedback) shows one 16-ms pass earlier than in the
exe; the puff's life is the same.

### 2.4 Draw (verified)

- Position: `x − img.off_x − scroll_x`, `y − img.off_y + 20 − scroll_y`
  (image `+8` / `+0xc` are the R8 header offsets, doc 14 §5.3), i.e. the
  same rule as an explosion at height 0.
- Blit: `FUN_004310c0` (darken, the flag-2 explosion blit) regardless of
  EX_PUFF's own explosion flags.
- Clip: the playfield, (0, 20)–(480, 400).
- Order: slot order 0..9.
- Layer: in `FUN_00428ca0` after `FUN_00428e30` (objects, explosions,
  aircraft) and `FUN_00428070` (bullets), **before** the shroud
  `FUN_00423fb0` and the banners. A puff on a full-shroud cell is
  covered by the black shroud; on an edge cell it is darkened with the
  edge (doc 16 §5.1).
- No house colour, no height.

### 2.5 State (verified unless marked)

- Not simulation state: no `rand()` call, no sim object, no effect on
  orders.
- Not saved: no reference to `0x516490..0x51653c` in the save code, and
  the range is not in the save block list of doc 27 §3.
- Reset only by `FUN_00448470` (its one caller `0x448b22` in the mission
  start `FUN_00448550`), skipped when `0x797e1c` ≠ 0. `0x797e1c` is the
  **RestartGame** flag: set by `FUN_0044c460` (command 0xDA, `0x44c472`);
  the other writers `0x449d46` / `0x44a005` / `0x44a0d1` need `0x7984ac`,
  never written; cleared at `0x448d50` later in the set-up. So (verified
  2026-10-16):
  - a **restart** keeps the live puffs (frozen while the menu was open)
    and they finish in the restarted mission at the same world pixels;
    the red colour is rebuilt (`FUN_0044acc0` at `0x4485d7`, after the
    restart's jump target `0x4485b7`);
  - an **in-mission load** calls `FUN_004418b0` at once (doc 27 §5) and
    neither `FUN_00448470` nor `FUN_0044acc0` runs: the puffs go on and a
    blackened red colour stays black;
  - a load from the menus goes through the mission start: both reset.
  Ours: `GameSession::carry` hands the puffs (and, on a load, the red
  flag) to the next `GameScreen`.

## 3. Alt crush move (verified)

Branch at `0x4458a4`–`0x445938` of the left-release code. It is inside
"a unit is under the cursor and (it is not own, or Ctrl is held)", after
"allied and no Ctrl → select it" (`0x445874`–`0x445892`), so it is
reached by enemy / neutral units, and by any unit with Ctrl.
(The unit search `FUN_004a5270` treats a stealthed, non-allied unit,
flags `+8 & 4`, as no unit.)

```
if AnyUnitSelected():                                   // 0x445897
    FlashUnit(target)                                   // 0x4458a9, always
    if Alt and record(target)+0x11 != 0                 // target is infantry
       and target.owner != player                       // 0x4458d1
       and AnyCrusherSelected()                         // 0x4458d7, record +0x35
       and cell(cx, cy).flags & 0x2000:                 // 0x4458e4, vehicles may enter
        order move FUN_00459450(player, cx, cy)         // 0x4458fa
    else:
        order attack unit FUN_004596f0(player, owner, idx)
    voice FUN_0049f240(1)                               // 0x445d8f
```

- **Crushers:** **any** one selected unit with record `+0x35` ≠ 0 is
  enough (`FUN_004a5a10`); the move order still goes to **all**
  selected units (infantry included), as an ordinary move.
  In 1.06 Templates.bin `+0x35` is set on records 8–17 only (names by
  record `+0x01`): HARVESTER, the three COMBAT_TANK records, MCV,
  MISSILE_TANK, DEVIATOR, SIEGE_TANK, SONIC_TANK, DEVASTATOR. Trike,
  raider, quad, stealth raider, aircraft, worm and infantry have 0. Our
  parser already has it (`formats::UnitRecord::crusher`, +0x35).
- **Infantry test:** the **target's** record `+0x11` ≠ 0 (the same byte
  `FUN_004a5940` reads as "infantry").
- **Drivable:** only the clicked cell's terrain flag `0x2000` (doc 06,
  "vehicles"); no occupancy test.
- **Cell:** the order goes to the clicked cell (cx, cy), the same cell
  as the ground move.
- **No puff** (the branch jumps to `0x445d8f`, past the puff call). The
  target infantry **flashes** even though it is not attacked. The
  acknowledge voice plays.
- Own infantry with Ctrl + Alt: the owner test fails → attack (normal
  Ctrl force-fire). Allied infantry with Ctrl + Alt and a crusher → move.

### 3.1 Cursor (verified, `0x443650` area)

For the same target (unit under the cursor, not own-selected or Ctrl,
not "allied without Ctrl"): if Alt, target infantry, not own:
`AnyCrusherSelected && cell & 0x2000` → **1** (move), else
`FUN_004a58b0` → 2, else 3. Without Alt: `FUN_004a58f0(target)` → 2
(armed, **not counting the target itself**), else 3.

### 3.2 Alt elsewhere

`0x797e8a` has three readers, all in `FUN_004433a0`: the cursor
(`0x443650`), the click (`0x4458ae`) and the group keys (`0x446191`,
Alt + digit selects and centres the group, doc 25). Alt does nothing
for buildings or the ground.

## 4. Ctrl on a building with units selected (verified)

Branch "enemy building, or Ctrl on any building" (`0x445b98`), with the
cell's building found by `FUN_004a5640`:

```
if t = selected turret (FUN_004a5860):          // doc 38 §3.3
    ...turret rules, return
elif AnyUnitSelected():                          // 0x445c18
    if owner == player and not AnyArmedSelected():   // 0x445c25 / 0x445c33
        nothing (0x445c3a → 0x445d99: no order, no flash, no voice)
    elif AllSelectedOfKind(4) (saboteurs):       // 0x445c42
        if b+0x28 & 0x200: flash, FUN_00459800, voice 1 else nothing
    elif AllSelectedOfKind(3) (engineers):       // 0x445c86
        if b+0x28 & 0x400: flash, FUN_00459800, voice 1 else nothing
    else:
        flash, FUN_00459800, voice 1              // 0x445cc8
else:
    select it if record flag 0x200 (FUN_004a5ab0)   // 0x445cf3
```

- `FUN_004a58b0` is "an armed unit is selected" (record `+0x0c` ≠
  0xff). Doc 10 §3 calls `FUN_004a58f0` the armed test; that one is the
  cursor's variant that skips the target unit.
- The armed test applies to **own** buildings only. Allied buildings
  with Ctrl go straight to the saboteur / engineer / attack rules.
- When it fails nothing happens: it does **not** fall through to the
  own-building rules (no dock, repair pad, select or primary).
- Engineers and saboteurs are unarmed (`+0x0c` = 0xff), so a selection
  of only engineers or only saboteurs never acts on an own building
  with Ctrl. A mixed selection with an armed unit passes the armed test
  and fails "all of kind", so it gets the plain `FUN_00459800` (all
  selected units, engineers and saboteurs included).
- With **no** unit selected (and no turret), Ctrl on any building only
  selects it (flag 0x200): Ctrl on the own selected building does
  **not** set it primary.

### 4.1 Cursor for the same case (verified, `FUN_004433a0` l. 255–270)

Ctrl on a building with units selected: `FUN_004a58b0` → 2; else any
saboteur (`FUN_004a57f0(4)`) → 0x1f if the building is **not own** and
`+0x28 & 0x200`, else 3; else any engineer (`FUN_004a57f0(3)`) → 4 if
not own and `+0x28 & 0x400`, else 3; else 3. (The cursor uses "any",
the click "all", as doc 30 already says.)

### 4.2 Ctrl on the ground

Ctrl on an empty cell with units selected but none armed: **nothing**
(`0x445d3a` → `0x445d99`), no move, no voice. The cursor still shows the
normal move cursor (1 / 2 / 0x20 / 3) in that case, because the cursor
code falls back to the move rules when `FUN_004a58b0` fails.

## 5. Our code

| Item | Original | Ours | Status |
|---|---|---|---|
| Puff on shroud move | move, puff, **voice 1** | `game_screen.cpp:445` move only | puff and voice missing |
| Puff on ground move | move, puff, voice 1 | `:586-588` move, voice | puff missing |
| Puff on Ctrl ground | attack ground, puff, voice 1 | `:476` attack ground, voice | puff missing |
| Ctrl ground, nothing armed | nothing | `:477` move + voice | **wrong**: must do nothing |
| Puff state / draw | 10 slots, per-draw advance, darken blit, after bullets, before shroud | none (`compose()` `:1236-1238`) | missing |
| Alt crush move (click) | §3, in both the Ctrl and non-Ctrl unit paths | `:452-458` (Ctrl) and `:521-526` attack only | missing |
| Alt cursor | §3.1 | `:745`, `:760` | missing |
| Ctrl unit cursor, armed | `FUN_004a58f0(target)` (skip target) | `:745` `armed` counts the target | small mismatch |
| Ctrl own building, units, none armed | nothing | `:466-470` engineers with 0x400 / saboteurs with 0x200 act, others attack | **wrong** |
| Ctrl building cursor (saboteur / engineer) | 0x1f / 4 only if not own | `:749-750` no owner test | **wrong** on own buildings |
| Ctrl own selected building, nothing selected | select only | `:575-579` sets primary | **wrong** |
| Red health colour bug | 10 live puffs blacken red bars until mission end | `:1062` constant | see §7 |
| Group keys Alt | doc 25 | done | ok |

The Ctrl block at `:451` runs before the unit / building tests, so the
Alt rule has to be added there too (Ctrl + Alt on allied / enemy
infantry with a crusher is a move).

## 6. Corrections to other docs

- **doc 14 §5.4:** the out-of-table clear is not harmless: it zeroes the
  red health-bar colour `0x51653c` (§2.2). The type is the special id
  `0x6d6111`, not a fixed EX_PUFF (26 in 1.06).
- **doc 10 §3:** the shroud move also plays the acknowledge voice;
  "Ctrl with an armed unit" is `FUN_004a58b0`; Ctrl on the ground with
  no armed unit does nothing; Ctrl on an own building needs an armed
  unit; the saboteur / engineer rules need **all** selected units of
  that kind; the Alt crush move has no puff and still flashes the target.
- **doc 10 §7:** replaced by §2 here.

## 7. Implementation checklist

1. **Puff state** in `GameScreen` (UI state, not `sim::World`, never
   saved, never hashed): `struct ClickPuff { int x, y; unsigned frame,
   counter; }` × 10, all `frame = frames(EX_PUFF)` at mission start and
   after a load.
2. **Puff type** from Templates.bin special id `0x39` (the block our
   `sim/` already reads for `0x6d6113` / `0x6d6115`); frames from the
   explosion frame-count table; skip if the id is 0xff.
3. **Add** (`FUN_00456d50`): first slot with `frame >= frames`, else the
   largest frame (first on ties, slot 0 if all 0). Set x, y, frame 0;
   counter 0 for a free slot only (keep the old counter on reuse).
4. **Health colour bug:** when the reuse path runs, set a
   `red_bar_black_` flag (cleared at mission start) and draw the red
   health bars in black while it is set (no radar reader: §2.2). It is
   vanilla behaviour and does not change gameplay; ported as vanilla.
5. **Draw** in `compose()` between `draw_bullets()` (`:1236`) and
   `draw_shroud()` (`:1238`) (after `draw_selection()` is fine only if
   the brackets end up drawn with the objects, doc 44): for each slot in
   order with `frame < frames`, `draw_blit(entry(EX_PUFF, frame), x −
   scroll_x + kPlayfieldX, y + kPlayfieldY − scroll_y, -1, Darken,
   kPlayfield)`, then `if (++counter > 2) { counter = 0; ++frame; }`.
   Only advance when the world is composed (also the redraw passes,
   like the flash counters of doc 44).
6. **Callers:** at `:445` (shroud, plus `unit_response(true)`), `:476`
   (Ctrl attack ground) and `:587` (ground move), with the world pixel
   (wx, wy), order → puff → voice.
7. **Ctrl ground:** remove the move at `:477`; with no armed unit do
   nothing.
8. **Alt crush** (`SDL_KMOD_ALT`): in both unit paths (`:452` and
   `:521`), after the flash: target record infantry, target owner ≠
   player, any selected record `crusher` (+0x35), and `map_->at(cx, cy).flags & 0x2000` →
   `order_move(player_, cx, cy)` (no puff), else attack; then
   `unit_response(true)`.
9. **Cursor:** Alt rule of §3.1 at `:745` / `:760`; the non-Alt armed
   test skips the target unit (`FUN_004a58f0`); the Ctrl building
   saboteur / engineer cursors need "not own" (`:749-750`).
10. **Ctrl own building** (`:459-470`): own building and no armed unit
    selected → return (no flash, no voice) before the saboteur /
    engineer test; allied buildings skip that test.
11. **Ctrl, no units selected, no turret:** select the building (flag
    0x200) and never take the primary path (`:575`).
12. Tests: slot choice when full (largest frame, counter kept); 15-draw
    life; a Ctrl click on an own building with only engineers selected
    gives no order; Alt on enemy infantry with a tank selected gives a
    move to the cell and a flash; Ctrl on the ground with only
    harvesters gives no order.
13. Docs: doc 10 §3 / §7 and doc 14 §5.4 point here; `docs/parity.md`
    input row.

## 8. Closed (2026-10-16)

- Puff look and length: from the code (EX_PUFF images, the darken blit,
  15 draws, §2.3 / §2.4); the click / draw order: §2.3.
- `FUN_00493590` case 3: an unreachable debug route overlay (§2.2).
- `0x797e1c`: the RestartGame flag (§2.5).
