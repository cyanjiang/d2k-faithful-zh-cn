# 44: The target flash (units u+0x1D, buildings b+0x38) (1.06)

Written for a port of the short flash on the target of a player's order:
the clicked unit or building is drawn in grey for a few frames. Closes
the flash TODOs of doc 10 §8 and doc 38 §7.

"Verified" means it was read from the decompile, and from the disassembly
where the decompile is misleading. Guesses are marked TODO(verify).
Writers were found with a capstone sweep of `.text` for byte accesses to
`[reg+0x1d]` / `[reg+0x38]` (not ESP / EBP), plus word / dword stores
at `+0x1c`; block copies (`rep movs`) are covered by reading their callers.

## 1. Functions and fields

| Address | Name used here | Role |
|---|---|---|
| `FUN_0049f310(u)` | FlashUnit | `u+0x1D = 7` (`0x49f314`) |
| `FUN_0049f320(b)` | FlashBuilding | `b+0x38 = 7` (`0x49f324`) |
| `FUN_004433a0` | in-game mouse handler | the **only** caller of both (8 sites, §2) |
| `FUN_00428e30` | objects draw (sorted list) | draws flashed images grey, counts down (§3, §4) |
| `FUN_0042af00` | infantry pass (unit flag 0x1000000) | draws flashed infantry grey, **no** count-down (§4.3) |
| `FUN_004326b0` | BlitTImage grey ("flash") blit | the grey blit (§3.2) |
| `FUN_0042aa90` | AddToDisplayList | culls to the view ± 128 px; only listed items count down |
| `FUN_00455870` | CreateUnit | `u+0x1D = 0` (`0x4559c5`) |
| `FUN_004563b0` | CreateBuilding | `b+0x38 = 0` (`0x4564ef`) |
| `FUN_00458e90` | sync checksum (multiplayer) | zeroes `u+0x19/+0x1D/+0x28` and `b+0x35/+0x38` around the sum, then restores them |

| Field | Meaning |
|---|---|
| unit `+0x1D` (u8) | flash counter, 7 when set, 0 = off |
| building `+0x38` (u8) | flash counter, same |
| draw item `+0x21` | selected copy (unit `+0x19`, building `+0x35`) |

Other hits of the sweep are not these fields: `FUN_0041af50` /
`FUN_0041b250` (the AI block, its `+0x1D` is DefendArea, doc 22),
`FUN_004501a0` (`.MIS` parser, AI block too), and three `add byte ptr`
hits outside any function (data decoded as code).

The checksum `FUN_00458e90` treats the counters like the selection and
the group digit: user-interface state, not simulation state (verified).
Nothing in the simulation reads them.

## 2. Who flashes what (verified, all in `FUN_004433a0`)

Only the **local** player's clicks set the counter; it is set by the UI,
not by the order handler, so other players' orders never flash anything
(verified: no other callers, no other writers of value 7).

| Site | Click | Flash | Then |
|---|---|---|---|
| `0x445332` | repair mode (`0x516438` = 2) on an own unit with hp < max, not infantry (urec `0x77e261` = 0), a kind-6 pad owned (`FUN_00442910(player, 6)`) | the unit | unit voice first (`FUN_0046f820`), then flash, then order 0x21 `FUN_00459690` |
| `0x4458a9` | a unit that is not own (alliance ≠ 0) or Ctrl on any unit, **units selected** | the unit | `FUN_004596f0` (order 0x14), or the Alt crusher move `FUN_00459450`; flash in both cases; ack voice |
| `0x445960` | the same, no units selected, a selected turret in range of the clicked cell | the unit | order 0x1E `FUN_00459d20`, no voice (doc 38 §3.2) |
| `0x445a8e` | own building, no Ctrl, units selected: all harvesters and a refinery (dock `FUN_00459540`), or a repair pad with a non-infantry unit (`FUN_004595e0`) | the building | order first, then ack `FUN_0049f240(1)`, then flash |
| `0x445bb8` | enemy building or Ctrl on any building, a turret selected and in range (`FUN_00497530`) | the building | order 0x1F `FUN_00459de0`, no voice (doc 38 §3.3) |
| `0x445c5f` | enemy / Ctrl building, no turret, units selected, all saboteurs (`FUN_004a59c0(4)`) and building flag `+0x28 & 0x200` | the building | order 0x15 `FUN_00459800`, ack |
| `0x445ca3` | the same, all engineers (`FUN_004a59c0(3)`) and flag `0x400` | the building | order 0x15, ack |
| `0x445ccd` | the same, any other selection | the building | order 0x15, ack |

Not flashed (verified by the absence of calls): moves, attack ground
(Ctrl on an empty cell), guard, repair-mode clicks on buildings
(`FUN_0045a150`), sell, capture voice paths, the radar, units being hit,
AI orders, script reinforcements.

Notes:

- The saboteur / engineer refusals (flag missing) jump to `0x445d99`:
  **no flash, no order, no voice**.
- Ctrl on an **own** building with units selected needs
  `FUN_004a58b0()` (an own selected unit with a primary weapon) before
  the three sites above; otherwise nothing happens (decompile line
  "`local_cc != player || FUN_004a58b0()`"). Our `game_screen.cpp:453`
  path does not test this (side finding, separate from the flash).
- The flash is set before the order is posted, so it shows even when the
  order is later refused by the handler (e.g. `FUN_00459800` skipping
  kinds 1, 6, 0xE).

## 3. Drawing (verified, `FUN_00428e30`)

### 3.1 The test

For each sorted draw-list item, each of its three images (body
`+0x10`, turret `+0x14`, overlay `+0x18`) is tested **before** any other
blit choice:

```
if (obj.flash & 2): GreyBlit(image, same x, y as the normal blit, keyed = 1)
else: normal path (plain, stealth shimmer / dissolve, additive overlay...)
```

| Image | Building test / call | Unit test / call |
|---|---|---|
| body | `0x429584` / `0x4295d3` | `0x4295ae` / `0x4295d3` |
| turret | `0x429848` / `0x429857` (the `push 1` is at `0x42981c`) | `0x4298f1` / `0x42990a` |
| overlay | `0x429b22` / `0x429b43` | `0x429ba5` / `0x429bba` |

- Grey for counter values with bit 1 set: **7, 6, 3, 2**; normal for
  5, 4, 1, 0.
- No owner test: any side's object flashes. No local-player test in the
  draw.
- The flash wins over stealth: a cloaked / fading unit with the bit set is
  drawn grey (shimmer and dissolve skipped).
- The flash wins over the building overlay's additive blit (item flag 4,
  `FUN_004349a0`) and applies to the build-up / active animation overlays.
- Not flashed: the building foundation (`FUN_00427d50`, separate pass),
  the repair wrench (entry 3), the selection bracket, health bar, pips and
  group digit.
- Aircraft (`0x200140` units, test at `0x4291e8`) are not in the sorted list: they are never
  drawn grey and never count down (a flashed aircraft keeps 7, invisibly).
- The unit overlay call (`0x429bba`) pushes ESI as the image, as the
  non-flash path of the same item does (`0x429c18`, the same blitter
  arguments); ESI is loaded once at `0x429b0f` before both branches, so
  the grey draw uses the same image as the normal one (verified).

### 3.2 The grey blit (`FUN_004326b0`, flag 1)

8-bit source image with its palette (house colours already copied into
palette entries 240..255 for the item's owner, as for the normal blit).
Per source pixel `i`:

- `i == 0`: transparent (flag 1; every call above passes 1).
- `i == 1`: palette[1] is first set to the half-bright screen pixel
  `(dst & mask) >> 1` (mask `0x797e6c` = 0xFBDE in 555), then treated
  like any other index (so shadows become a grey of the darkened ground).
- otherwise `c = palette[i]` (555: r, g, b 5-bit; 565: green's top 5 bits)
  and

```
v = (r + g + b) / 6 + 3            // unsigned integer division
dst = (v << 10) | (v << 5) | v     // 555; 565: (v << 11) | (v << 6) | v
```

So the flash is a **mid grey silhouette** (v ≤ 18 of 31), not white.

## 4. The count-down (verified)

### 4.1 Where

In `FUN_00428e30`, at the end of each sorted-list item (after its
images, bracket and pips):

```
if item is a building and b.flash != 0: b.flash--      // 0x42a564 / 0x42a56d
if item is a unit     and u.flash != 0: u.flash--      // 0x42a579 / 0x42a582
```

and, **in addition**, for a building whose item is selected (`+0x21`),
inside the bracket code: `if b.flash != 0: b.flash--` (`0x429f43` /
`0x429f4c`), before the own-player pips. A selected building therefore
counts down **twice per frame** (draws 7 grey, 5 normal, 3 grey, 1
normal). This only happens when the target building is also selected
(e.g. Ctrl-click an own building in a mixed unit + building selection).

### 4.2 When

`FUN_00428e30` is the world draw (`FUN_00428ca0`, called once per main
loop iteration from `FUN_00448550` at `0x448fed`, skipped while the
window is minimised). So the counter is **per drawn frame, not per sim
tick**: 8 frames, at our fixed 60 Hz loop about 133 ms (grey frames 1-2
and 5-6). While an in-game menu is open the view is redrawn only for the
two passes after a menu closes (`FUN_00473130`, already ported), so the
counter only moves then.

### 4.3 Items not in the list

- Only items that `FUN_0042aa90` adds count down: on-screen ± 128 px
  (`x − 128 < view.right && view.left < x + 128`, same for y), at most
  1000 items. An off-screen flashed object keeps its counter until it is
  next in view, then flashes.
- Infantry with unit flag 0x1000000 are skipped by the sorted list
  (`0x4291b0`: `u+8 & 0x1000000` test) and drawn by
  `FUN_0042af00`, which tests the flash (`0x42b08b`, grey blit at
  `0x42b0ae`) but **never decrements** it: such infantry stay grey (7)
  until they leave that state. Port as is (our renderer does not have this
  pass yet; doc 32 §1.7 quirk).

## 5. Save game (verified)

The original full save writes the player blocks raw (doc 27 §2.3), and
units / buildings live in them, so `u+0x1D` and `b+0x38` are saved and
restored. Capture (doc 30, `copy 0x7C bytes`) and the Deviator's
ConvertUnit copy the counter to the new object too.

Ours: `src/sim/save_state.cpp:131` (`u.flash`) and `:143` (`b.flash`)
already save both; no version bump is needed.

## 6. Our code

Historical (the plan before the port): every row is done (8 flash sites
in `GameScreen`, `SpriteBank::draw_grey`, the count-down after the
draw, the infantry pass without count-down).

| Finding | Ours | Status |
|---|---|---|
| Fields `u+0x1D`, `b+0x38` | `src/sim/world.hpp:91` / `:193` (`flash`, "TODO: the draw") | OK; update the comments |
| Created with 0 | default member init | OK |
| §2 repair mode on a unit | `game_screen.cpp:410-421`: TODO, no flash | **Missing**: `flash = 7` after the voice, before `order_repair_unit` |
| §2 Ctrl on a unit, units selected | `game_screen.cpp:448-451` | **Missing**: flash before `order_attack_unit` |
| §2 enemy unit, units selected | `game_screen.cpp:512-514` | **Missing**: flash before `order_attack_unit` (also for the Alt crusher move, once ported) |
| §2 turret on a unit | `game_screen.cpp:517` | OK |
| §2 Ctrl on a building, units selected | `game_screen.cpp:453-459` | **Missing**: flash after the 0x200 / 0x400 refusals, before the order; (side) the own-building `FUN_004a58b0` test |
| §2 enemy building, units selected | `game_screen.cpp:535-541` | **Missing**: flash after the refusals, before the order |
| §2 dock | `game_screen.cpp:545` TODO | **Missing**: flash after the ack |
| §2 repair pad | `game_screen.cpp:554` TODO | **Missing**: flash after the ack |
| §2 turret on a building | `game_screen.cpp:587` | OK |
| §3 grey draw | `render/objects.cpp`: no flash | **Missing** |
| §4 count-down per frame, in view only, double for selected buildings | — | **Missing** |
| §4.3 infantry pass without count-down | no `FUN_0042af00` pass | Missing (optional quirk) |

## 7. Implementation checklist

1. `src/sim/objects.hpp:16` `sim::Object`: add `uint8_t flash = 0;`.
2. `src/app/game_screen.cpp` `compose()` (`:1145-1186`): copy `b.flash` /
   `u.flash` into the snapshot.
3. `src/render/sprites.{hpp,cpp}`: add `SpriteBank::draw_grey(out, index,
   x, y, house_colour, clip)` = §3.2 (index 0 skipped; index 1 = grey of
   `(dst & 0xFBDE) >> 1`; else grey of the house-remapped palette colour,
   `v = (r+g+b)/6 + 3`, `dst = v<<10 | v<<5 | v`). `draw_dissolve`
   (`sprites.cpp:91`) is the nearest template (per-pixel palette with a
   screen-dependent entry).
4. `src/render/objects.cpp` `draw_objects`: add `bool flash` to `Draw`
   (`:17`), set from `o.flash & 2` for buildings and units, and in the
   final loop (`:168-176`) use `draw_grey` for every part when set, before
   the shimmer test. Keep the repair wrench part (`:107-118`) normal (mark
   it, e.g. a per-part flag). Foundations (pass 1) stay normal.
5. Count-down in `GameScreen::compose()` after `draw_objects` (the draw
   function takes a const list): for each building / unit that was put in
   the snapshot and whose screen position is within the playfield ± 128
   px (the `FUN_0042aa90` cull; compute it from the same `sx`, `sy`), do
   `if (flash) --flash`; for buildings with `selected`, do it once more
   first (§4.1). Units with flags `0x200140` are not in the list: no
   count-down. Since `compose()` runs once per drawn frame (and for the
   two redraw passes after a menu, `:2010`), this matches §4.2.
6. Set the flash at the §2 sites (table in §6), keeping the order of
   voice / flash / order per site.
7. Never read `flash` in the simulation; keep it out of any future sync
   hash (as `FUN_00458e90` does).
8. Tests (`tests/render/` or `tests/app/`): the grey formula on known
   pixels (index 1 over a known ground pixel, a house-colour index);
   counter 7 → grey frames 0, 1, 4, 5 then off; a selected building →
   grey frames 0 and 2 only; an off-screen object keeps 7.
9. Docs: doc 10 §8 "Flashing target" and doc 38 §7 / §8 point here;
   `docs/parity.md` turret row "Open: the flash draw" → done once
   implemented.

## 8. Open

Closed 2026-10-16:

- The grey look is the formula of §3.2 and the length is 7 counts per
  drawn frame (§4), both read from the code. In frames it follows the
  loop rate, which the project fixes at 60 Hz (CLAUDE.md §10), as the
  cnc-ddraw reference does; no reference check is needed.
- Infantry with flag 0x1000000 (in a cell with the vehicle bit 8) are
  never the target of a click: `FUN_004a5270` returns the vehicle of
  such a cell (doc 18 §3.7b), and every flash site of §2 goes through
  it. A soldier flashed just before it steps into such a cell keeps its
  counter while it is drawn in the infantry pass (no count-down there,
  §4.3), as ours does.
