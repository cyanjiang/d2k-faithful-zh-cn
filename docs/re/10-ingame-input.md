# 10: In-game playfield input

Covers mouse and keyboard on the playfield in 1.06: selection, orders, voices,
cursor images and how the cursor type is chosen, scrolling, and how
selection is drawn. Most of the logic is in **`FUN_004433a0`**
(HandleGameLoopEvents, called once per loop iteration). Line numbers refer
to its decompile in `re/exports/1.06/q_inp1.txt` (not committed).

## Corrections to earlier docs

- **doc 02:** the camera clamp pairs width with width and height with
  height. `x ≤ map_w·32 − [0x786d48]` (480), `y ≤ map_h·32 − [0x6dc538]` (380).
  `0x786d48` is the viewport **width** and `0x6dc538` its **height**.
- **doc 08:** the 64 crater entries are **not** interleaved. Entries
  114..145 fill `0x6d7be8 + i·8` (concrete) for i = 0..31, then entries
  146..177 fill `0x6d7bec + i·8` (sand). See `FUN_004606d0`.

## 1. Raw input (wndproc)

Jump table at `0x4a34e8`: index = msg − 0x113, index bytes at `0x4a3884`,
targets at `0x4a3860`.

| Message | Code | Effect |
|---|---|---|
| WM_MOUSEMOVE | `0x4a350d` | mouse → `0x4eb048` / `0x4eb04c`, clamped to 0..w−1 / 0..h−1; UI hook `FUN_00471090` |
| WM_LBUTTONDOWN | `0x4a3590` | only if the UI manager's +0x110 == 0 and there is no transition (+0x11d). Then `0x798520` (held) = 1, `0x798524` (down event) = 1, position → `0x798498` / `0x79849c` |
| WM_LBUTTONUP | `0x4a360e` | `0x798520` = 0, `0x798528` (up event) = 1, position → `0x798388` / `0x79838c` |
| WM_LBUTTONDBLCLK | `0x4a356a` | UI `FUN_00471050`, `0x798614` = 1, then falls into LBUTTONDOWN. `0x798614` is read only at `0x4a35e3`, so the playfield gives a double-click **no meaning** |
| WM_RBUTTONDOWN | `0x4a36b2` | `0x79852c` (held) = 1, `0x798530` (down event) = 1 |
| WM_RBUTTONUP | `0x4a36f9` | `0x79852c` = 0, `0x798534` (up event) = 1 |
| WM_RBUTTONDBLCLK | — | UI `FUN_00471070` only |

The event flags are consumed (cleared) by `FUN_004433a0`.

Key state is the byte array `keystate[VK]` at `0x797e78`:

| Key | Address | Use |
|---|---|---|
| Shift | `0x797e88` | additive select / toggle |
| Ctrl | `0x797e89` | force attack |
| Alt | `0x797e8a` | force move (crush) onto enemy infantry |
| Left, Up, Right, Down | `0x797e9d`..`0x797ea0` | see §4 |

Group keys (Ctrl+digit assigns the group stored at unit +0x28, digit
recalls it) are in the hotkey block of `FUN_004433a0` itself
(`0x44614b..0x446377`, reading the held array `keystate`); the window
procedure fills the arrays at `0x4a3390..0x4a33aa`. Details in doc 25.

## 2. Selection

### Rules

- **A building and units are never selected together.** Selecting either
  clears everything first.
- **Selectable unit** (`FUN_004a5a50`, `FUN_004a5ae0`), all of these:
  - own unit;
  - `flags(+8) & 0x140 == 0`;
  - **state** (+0x10) ≠ 0x11 (dying);
  - a unit in state 8 (leaving its factory) also needs `+0x4f ≠ 0`
    (mid-step). (An earlier version of this note read +0x10 as the unit
    type; it is the state, doc 09.)
- **Selectable building** (`FUN_004a5ab0`): type ≠ 0x11, `+0x28 & 6 == 0`
  (not being built or sold). Through clicks, only buildings whose record
  `+0x58 & 0x200` is set are selected, for any owner.
- The selected flag is unit +0x19 and building +0x35.

### Functions

| Function | Meaning |
|---|---|
| `FUN_004a5a50(owner, idx, add)` | add = 1 toggles one unit; add = 0 deselects everything, then selects it |
| `FUN_004a5ab0(owner, idx)` | deselects everything, then selects the building |
| `FUN_004a5790()` | deselects everything, all players |
| `FUN_004a5ae0(x0, y0, x1, y1)` | box select in world pixels, strict `<` / `>` against the unit's 16.16 position (+0x34 / +0x38 >> 16). Own selectable units only. **Only if at least one unit is inside** does it deselect all and select them; there is no limit on count. Buildings are never box-selected |
| `FUN_004a5830` / `FUN_004a5860` | "any building selected" / "a turret building (kind 0x10) selected" |
| `FUN_004a58f0` | "any armed unit selected" (record +0x0c ≠ 0xff) |
| `FUN_004a5270(px, py, &owner, &idx, skip_cloaked)` | unit under a world pixel (cell based, see "Hit test" below) |
| `FUN_004a5640(cx, cy, &b, &owner, &idx)` | building on a cell |

### Click or drag

- Left down inside the playfield sets the drag flag `0x516458` = 1 and stores
  the start point in world pixels (`0x516450` / `0x516454`).
- On left up it is a **box** if `|dx| ≥ 4 && |dy| ≥ 4` (both). Otherwise it
  is a click (§3).
- After a box select that selected something: select voice
  `FUN_0049f240(0)`.
- While dragging, the rectangle is drawn by `FUN_00423c50` (main loop
  `0x449031`, after the world draw and before the radar), only while
  `0x516458` and the left button `0x798520` are set: from the drag start
  (world → screen) to the mouse, four 1-px lines (`FUN_00430d10` →
  `FUN_004308f0`, ends inclusive) in `0x5162b8` = RGB(198, 181, 165)
  (16-bit; `0x5165bc` in 8-bit mode). Clip rect (0, 21, 479, 400);
  `FUN_004308f0` clamps line ends to [left, right − 1] × [top, bottom − 1],
  so the lines cover x 0..478, y 21..399 (column 479 is never drawn), and a
  line wholly outside one side is skipped.

### Hit test: what is under the mouse (verified 2026-10-16)

There are **no pixel boxes and no sprite tests**. The playfield asks two
cell-based lookups, the unit one first; the building one only when no
unit was found (click `0x445683` → `0x4459cf`; the cursor code the same).

**World pixel.** `px = cam_x + mouse_x`, `py = cam_y − 20 + mouse_y`
(`0x798538` / `0x79853c`, `0x4e41dc` = 20; `0x443476`, `0x4455e0`). The
cell is `(px >> 5, py >> 5)`.

**Unit: `FUN_004a5270(px, py, &side, &idx, skip_cloaked)`.**

1. cell = `(px >> 5, py >> 5)`, each capped at 0x7f; `f` = its map flags;
   side = `f & 7` (the cell's **owner bits**), idx = −1.
2. `f & 0x3f8 == 0` → none (side = 0xff).
3. **Vehicle bit `f & 8`:** walk the owner's unit list (list order) for
   the first unit that is **on the cell** (`+0x4a/+0x4b` = cell, or flag
   0x10000000 set and `+0x4c/+0x4d` = cell, i.e. stepping into it), not
   infantry (record +0x11), kind not 2 / 5 / 8 / 9 / 10 (carryall,
   sandworm, frigate, ornithopter, Death Hand) and no flag 0x400140
   (airborne / carried). None → none (side = 0xff). Found but cloaked
   (`+8 & 4`) with `skip_cloaked` → none (side stays the owner). There
   is no position or distance test among vehicles: the first in list
   order wins.
4. **Else, infantry bits `f & 0x3e0`:** of the owner's infantry on the
   cell (same "on the cell" test, no flag 0x400140, and not cloaked when
   `skip_cloaked`), the one with the **smallest Manhattan distance**
   `|x − px| + |y − py|` from the pixel (unsigned, start 0x800, strict
   `<`: the first in list order wins ties). So the **sub-cell position**
   matters: of five soldiers in a cell, the one nearest to the mouse
   point is hit.
5. Else (only bit 0x10, a building cell) → none.

Consequences: a unit drawn half into the next cell is hit only on the
cell(s) it occupies (`cur`, or `to` while stepping); a unit of another
side standing on a cell whose owner bits are stale (doc 18 §3.4) is not
found; aircraft and carried units are never hit.

**Callers in `FUN_004433a0`:**

| Site | Pixel | skip_cloaked | After |
|---|---|---|---|
| `0x445683` click (mode 0) | mouse | 1 if `cell & 7 ≠ local` and `cell & 0x3e8`, else 0 | a cloaked unit (`+8 & 4`) of a non-allied side (`0x798830[local·8 + side] ≠ 0`) counts as none (doc 32 §1.5) |
| `0x443562` cursor, units selected | mouse | as the click | the same cloak test |
| `0x443b02` cursor, nothing selected, enemy unit bits | cell · 32 (corner) | 1 | only "found or not" |
| `0x443bb1` cursor, repair mode (2), no building | cell · 32 | 0 | 0xb if a repair pad is owned (`FUN_00442910(local, 6)`), side = local, hp (+4) < record hp (unit record +4, `0x77e254`), not infantry; else 8 |
| `0x445292` click, repair mode | mouse | 0 | the repair order (doc 21 §4) |

**Building: `FUN_004a5640(cx, cy, &b, &side, &idx)`.** Needs cell flag
0x10; walks the list of the cell's owner (`cell & 7`), skips record kind
0xF (concrete) and takes the first building whose footprint covers the
cell (`FUN_004a5570`: inside the box +0x16..+0x18 / +0x17..+0x19, then
the 4 × 4 footprint bits of record +0x54, `0x6dc594`). No pixel test either.

## 3. Orders (left-button release, mode 0)

The cell (cx, cy) and world pixel (px, py) come from the release point.
"Owner" is the owner of the cell (`cell & 7`), checked against the
alliance table `0x798830[player·8 + owner]` (0 = friendly).

### Shroud, units selected

Move: `FUN_00459450(player, cx, cy)` plus the marker
`FUN_00456d50(px, py)`.

### No unit and no building under the cursor

- **Ctrl with an armed unit selected:** attack ground `FUN_004598d0` plus the
  marker.
- **Otherwise, units selected:** move plus the marker.
- The ack voice `FUN_0049f240(1)` plays in both cases.

### Own building, no Ctrl

1. **All selected units are harvesters, and the building is kind 4
   (refinery):** dock with `FUN_00459540`.
2. **The building is kind 6 (repair pad) and a non-infantry unit is
   selected:** `FUN_004595e0`.
3. After 1 or 2: `FUN_0049f240(1)`, and flash the building with
   `FUN_0049f320`.
4. **Neither, and the record has flag 0x200:**
   - **already selected and can be primary** (`FUN_004432f0`): set primary
     with `FUN_00459ea0` and play `H_PRIMARY` / `O_PRIMARY` / `S_PRIMARY`
     (one per house);
   - **otherwise:** select it with `FUN_004a5ab0`.

### Friendly (allied) building

Select it if the record has flag 0x200.

### Enemy building, or Ctrl on any building

- **A turret building is selected:** attack it with `FUN_00459de0` if
  `FUN_00497530` says the turret can; otherwise select the clicked building.
- **Units are selected:**
  - a saboteur (kind 4) needs building flag 0x200;
  - an engineer (kind 3) needs flag 0x400 (corrected in docs/re/30 section 0);
  - then attack or enter with `FUN_00459800(player, owner, idx)`, flash, and
    ack voice.
- **Nothing selected:** select it if flag 0x200.

### A unit that is not own, or Ctrl on any unit

- **Allied, no Ctrl:** select with `FUN_004a5a50(owner, idx, 0)`.
- **Enemy, units selected:** flash the target with `FUN_0049f310`, attack
  with `FUN_004596f0`, then ack voice. With **Alt on enemy infantry**, a
  crusher selected (record +0x35) and a drivable cell, it moves instead.
- **Enemy, nothing selected:**
  - a turret building is selected and the target is in range
    (`FUN_00497140`): `FUN_00459d20`;
  - otherwise select the enemy unit, which only views it.

### Own unit

Correction (2026-10-16, `0x4456cf..0x445864`): every path below except the
MCV and a Devastator in state 0x1f ends with the select voice
`FUN_0049f240(0)` (`0x445d91`), and the unit tests are those inside
`FUN_004a5a50` (not 0x140, not dying, state 8 only mid-step).

- **Shift:** toggle with `FUN_004a5a50(…, 1)`, then the select voice.
- **Clicked unit is already selected (special actions):**

  | Record kind (+0x2d) | Action |
  |---|---|
  | 6 MCV | deploy with `FUN_0045a3f0` if `FUN_00443250` allows; otherwise `*_CANTDEPLOY` |
  | 7 | `FUN_0045a3f0` (unless type +0x10 == 0x1f) |
  | 0xe (thumper?) | `FUN_0045a3f0` on sand (cell & 0x10000) |
  | 4 | `FUN_0045a3f0` if +0x29 ≥ 0xa0 |

  A deploy (kinds 7, 0xe, 4) is followed by the select voice; kind 0xe off
  sand and kind 4 below 0xa0 select the unit alone, then the voice
  (`0x44583b`); **any other kind** clicked while selected is selected alone
  with the voice (`0x445852`).
- **Otherwise:** exclusive select, then select voice `FUN_0049f240(0)`.

### Other modes (`0x516438`)

| Mode | Left click |
|---|---|
| 1 place building | `FUN_00459fd0`; if placement is invalid, `*_CANTBUILD` |
| 2 repair | own building: `FUN_0045a150`; own unit: `FUN_00459690` plus voice |
| 3 sell | own building: `FUN_0045a1a0`. The capture path (`FUN_0046d6d0` plus `*_CAPTURE`) needs `0x4eb008 == 0` and is dead in retail (doc 30 §0) |
| 4 / 5 | Starport or special placement (record kinds 9 / 10): `FUN_0045a440` |

### Sidebar buttons (left down, `FUN_00443040(n, 1)`)

| n | Action |
|---|---|
| 4 | repair toggle, BUTTON sample |
| 5 | sell toggle, BUTTON sample |
| 7 | guard: `FUN_00459980` plus `*_GUARDING` |
| 8 | retreat: `FUN_00459ab0` plus `*_RETREAT` |
| 0xb | options menu |

### Right button down (`0x798530`)

- Mode `0x516438` = 0, and the drag is cancelled.
- On a build icon: cancel or hold production (`FUN_0045a260` /
  `FUN_0045a3a0`).
- **In the playfield: deselect everything** (`FUN_004a5790`). Right-click
  never issues an order (classic left-click scheme).

## 4. Scrolling (`FUN_004433a0`, ~l. 410–500)

The speed is `gScrollSize` `0x4eb030` in pixels per loop iteration
(options 4..40, default 20). The viewport is 480×380 (`0x786d48` ×
`0x6dc538`).

| Condition (screen mouse) | Effect |
|---|---|
| y ≥ 399 | y += speed, clamped to `map_h·32 − 380` |
| y < 1 | y −= speed, clamped at 0 |
| x ≥ 639, or x > 480 **while dragging** (`0x516458`) | x += speed, clamped to `map_w·32 − 480` |
| x < 1 | x −= speed, clamped at 0 |

- Vertical is tested first, then horizontal, so **diagonals work**.
- Only the screen edges scroll (the window is 640×400), except the
  drag-past-the-playfield case on the right.
- **Arrow keys:** they scroll only if `DAT_004eb008 == 0`. That byte is
  initialised to 1 and never written, so **in retail the arrows never move
  the map**. When the sidebar list mode `0x516430 == 1`, Up/Down scroll the
  sidebar lists (`0x516440` / `0x516444`).
- **Radar click:** holding the left button (`0x798520`) over the radar
  centres the view on that cell (clamped). The radar origin is
  `0x4e41e4` / `0x4e41e8` = (497, 8), its size is `0x6d8d98`, and the map
  is centred inside it. All of these must hold:
  - the player has a kind-1 (radar) building;
  - power (`player+0x245b0`) > 99;
  - **no unit is selected**.

  With units selected, the radar shows cursor 6, and a left release over it
  gives them a move order to that cell (verified, doc 25 B.3).

### Edge cursors

These are set only in mode 0 and when not dragging, by the scroll code
itself (`0x443d3e..0x443fb0`), over the cursor chosen before it. "At the
limit" = this pass's step went past the edge and was clamped
(`max < cam + speed`, `cam − speed < 0`). The vertical part sets up / down
(0x0e / 0x12, blocked 0x16 / 0x1a); the horizontal part then reads that
value (verified 2026-10-16):

| Horizontal | vertical cursor 0x12 / 0x0e | 0x1a / 0x16 (blocked) | none |
|---|---|---|---|
| right, moving | 0x11 / 0x0f | **0x10** | 0x10 |
| right, at the limit | **kept** (0x12 / 0x0e) | 0x19 / 0x17 | 0x18 |
| left, moving | 0x13 / 0x15 | **0x14** | 0x14 |
| left, at the limit | **kept** | 0x1b / 0x1d | 0x1c |

So a diagonal shows the blocked image only when both axes are at their
limit; with one axis blocked it shows the other axis's plain arrow.

The table below lists the images:

| Edge | Cursor | At the limit |
|---|---|---|
| up | 0x0e | 0x16 |
| down | 0x12 | 0x1a |
| right | 0x10 | 0x18 |
| up-right | 0x0f | 0x17 |
| down-right | 0x11 | 0x19 |
| left | 0x14 | 0x1c |
| up-left | 0x15 | 0x1d |
| down-left | 0x13 | 0x1b |

## 5. Cursor

### Images

**`DATA\MOUSE.R16`** is read sequentially by `FUN_004626a0`. **Entry =
type·8 + frame** (33 types × 8 frames = 264), stored in
`0x6dc048[type·8 + frame]`. Entry 264 is the 48×48 background save buffer
(`0x781e50`). All cursors are 8-bit with an embedded palette, drawn with
palette `0x6e5620`. Render a sheet with `re/mouse_sheet.py`.

The meanings below are from the images (frame 0):

| Type | Image | Use |
|---|---|---|
| 0 | arrow | normal |
| 1 | green ring | move |
| 2 | red ring | attack / harvest |
| 3 | crossed ring | can't move there |
| 4 | green down arrow | enter / dock / sabotage |
| 5 | white brackets | select |
| 6 | small green dot | radar with units selected |
| 7 | crossed $ | sell, not allowed here |
| 8 | crossed wrench | repair, not allowed here |
| 9 | blue ring | mode 4 placement |
| 10 | $ | sell (valid) |
| 11 | wrench | repair (valid) |
| 12 | green four-way arrows | deploy / special / set primary |
| 13 | crossed four-way arrows | can't deploy |
| 14–21 | white arrows | scroll (§4) |
| 22–29 | red-white arrows | scroll blocked |
| 30 (0x1e) | skull | mode 5, the Death Hand target, on every playfield cell (doc 41 §3.2) |
| 31 (0x1f) | engineer figure | capture |
| 32 (0x20) | grey ring | infantry-only move (walkable but not drivable) |

### Animation and drawing (`FUN_00423a20`)

- The frame `0x512d68` advances every 3 calls (counter `0x512d6c`) and
  cycles 0..7. It is called once per loop, so at 60 Hz it shows 20 fps.
- Draw position = mouse − 24 − image offset, so the hotspot is entry offset
  + 24. Example: the arrow has offset (−24, −24), so its hotspot is at its
  top-left corner.
- It saves and restores a 48×48 background, and draws only when input is
  enabled.
- `FUN_0044c420(type)` stores the type in `0x798540`. The type is forced to
  0 while a menu is over the game.

### Type choice (each loop; mode `0x516438`)

- **Outside the playfield:**
  - radar with units selected → 6;
  - otherwise 0 (or an edge cursor).
- **Shroud** (`0x4dfb04` set and cell byte+8 & 0xf == 1):
  - with units selected → 1;
  - otherwise 0.
- **Mode 2 (repair):**
  - own building, damaged, record flag 0x200 → 0xb;
  - own non-infantry unit, damaged, and the player owns a kind-6 building →
    0xb;
  - otherwise 8.
- **Mode 3 (sell):**
  - own building → 10;
  - otherwise 7.
- **Mode 1:** cursor 0 (the arrow) everywhere but the radar
  (`0x443d34`, doc 12 §0); the edge cursors are not set (mode ≠ 0).
- **Mode 4** → 9.
- **Mode 5** → 0x1e.

#### Mode 0, no units selected

- **A turret building is selected:**
  - **enemy cell, or Ctrl:**
    - empty cell → 2 if in range (`FUN_00497140`), else 0;
    - building → 2 if `FUN_00497530` and it is not the selected building
      itself.
  - **friendly cell:**
    - building of kind 0x10 → 5;
    - other building → 0;
    - unit → 5.
- **Another building is selected, on an occupied cell:**
  - unit → 5;
  - other building → 5 if kind 0x10, else 0;
  - the selected building itself → 0xc if it can be primary
    (`FUN_004432f0`) and is not primary yet (`+0x28 & 0x80` clear), else 0.
- **Nothing selected** (re-checked in `FUN_004433a0`), on a cell with
  `cell & 0x3f8 ≠ 0` (a unit or a building):
  - no 0x400 bit: 5 when the cell owner is the local player or the cell has
    no unit bits (`& 0x3e8 == 0`, i.e. any building cell, enemy ones
    included); else (enemy units) 5 if `FUN_004a5270(…, 1)` finds one,
    else 0;
  - 0x400 (wall or turret top-left cell): 5 for a turret (kind 0x10), else 0.
  An empty cell gives 0.

#### Mode 0, units selected

**No unit under the cursor** (an airborne enemy counts as none):

| Under the cursor | Cursor |
|---|---|
| empty cell, drivable and walkable | 2 if there is spice and **all** selected units are harvesters, else 1 |
| empty cell, walk-only, infantry selected | 0x20 |
| empty cell, otherwise | 3 |
| empty cell + Ctrl, armed unit selected | 2 |
| own building, no Ctrl | 4 if kind 6 and a non-infantry unit is selected; 4 if all harvesters and kind 4; else 5 (on a 0x400 cell: 5 for a turret, else 3) |
| allied building, no Ctrl | 5 (on a 0x400 cell: 5 for a turret, else 3) |
| enemy building, or Ctrl | 2 if armed; else 0x1f for a saboteur (kind 4) on flag 0x200; 4 for an engineer (kind 3) on flag 0x400; else 3 (corrected in docs/re/30) |

**A unit under the cursor:**

- **Not own, not selected, or Ctrl:**
  - allied, no Ctrl → 5;
  - Alt on enemy infantry → 1 if a crusher is selected and the cell is
    drivable, else 2 if armed, else 3;
  - otherwise → 2 if armed (`FUN_004a58f0`), else 3.
- **Own selected unit, no Shift:**
  - MCV (kind 6) → 0xc if it can deploy (`FUN_00443250`), else 0xd;
  - kind 7 → 0xc;
  - kind 0xe → 0xc on sand, else 0xd;
  - kind 4 → 0xc if +0x29 ≥ 0xa0, else 0xd;
  - otherwise → 0.
- **Own unit with Shift, or not selected:** 5.

## 6. Voices

### Select / acknowledge (`FUN_0049f240(ack)`)

- Among the selected units it picks the one with the highest record +0xa4
  priority (on ties the later unit wins).
- sample = record +0x5c (select) or +0x80 (ack) + (house·3 + (`0x5173f4` % 3))·4.
- house is the MIS house of the player and must be ≤ 2.
- It plays through `FUN_0046f820(sample, 0, 0, 0)`.
- `0x5173f4` is the mission tick counter: 0 at the mission start
  (`0x44861e`, `XOR EBP, EBP` at `0x4485b5`), + 1 per game tick
  (`0x449a5e`); no `rand()`. The "·4" is the dword size: record +0x5c is 9
  dwords (sample numbers) indexed house·3 + tick % 3, and +0x80 the 9 ack
  samples (`0x77e2ac` / `0x77e2d0` = record table `0x77e250` + 0x5c /
  0x80). Our `World::unit_response` uses `tick_count_ % 3`.

### Event voices

`FUN_0048dd90` builds the house prefix (`H_`, `O_` or `S_` + name) and
`FUN_0046f900` plays it. The names are PRIMARY, CANTDEPLOY, CANTBUILD,
CAPTURE, GUARDING and RETREAT.

## 7. Move marker (`FUN_00456d50`)

- There are 10 slots at `0x516490`, each (x, y, frame, ?) in world pixels.
- The animation is an explosion: type `0x6d6110` byte 1, frame counts from
  `0x6d5fd0`.
- A new marker takes a free slot; when all 10 are busy, the slot with the
  largest frame (doc 46 §2.2, which replaces this section).

## 8. Selection visuals

### Fixed DATA.R16 entries 0..205 (`FUN_004606d0`)

| Entries | Stored at | Meaning |
|---|---|---|
| 0 | `0x786fe8` | ? |
| 1 | `0x795440` | ? |
| 2 | `0x6e5854` | ? |
| 3 | `0x6dc468` | flashing target marker |
| 4 | `0x6d60d0` | ? |
| 5 | `0x7951c8` | bracket, left side |
| 6 | `0x795338` | bracket, right side |
| 7 | `0x78fb28` | bracket, bottom-left |
| 8 | `0x78fb2c` | bracket, bottom-right |
| 9–14 | `0x789198[6]` | bracket top, one per size class 0..5 |
| 15, 16 | `0x6d8d88`, `0x6d8d8c` | empty pip, filled pip |
| 17–26 | `0x6e2df0[10]` | group digits 0–9; `[1]` (`0x6e2df4`) also serves as the primary marker |
| 27–37 | `0x6e5720[11]` | ? |
| 38–101 | `0x795028[64]` | ? |
| 102–109 | `0x7951a8[8]` | cell objects (crates, blooms…) |
| 110–113 | `0x782058[4]` | `[0]` repair icon, `[2]` Fremen icon |
| 114–145 | `0x6d7be8 + i·8` | craters on concrete |
| 146–177 | `0x6d7bec + i·8` | craters on sand |
| 178–193 | `0x786d50[16]` | animation by `(obj+0x8c + tick) & 0xf` |
| 194–205 | `0x6dc018[12]` | ? |

The size class is unit record +0x36 or building record +0x5f. The bracket
height per class (`0x4e3e48`) is **[19, 24, 32, 64, 96, 32]**.

### Draw (in the draw-list renderer `FUN_00428e30`)

Selection is drawn per draw-list item where +0x21 (selected) is set; s =
+0x22 (class). (sx, sy) is the object's screen position, H = height[s].

**Origin:**

- **Unit:** x = sx − w(top[s])/2; y = sy − (H·5 >> 3), and 8 more up for
  infantry (record +0x11).
- **Building:** x = sx; y = sy − base_image.offset_y, 6 more up for kind 5,
  12 more up if it has a turret.
  Correction (2026-10-12, `0x428e30` decompile): the image is the draw
  item's image (`item+0x10`, the body frame; below half hp and not
  building up / selling, the damaged image `0x7907a8[...]` when it is not
  null). Only when the item has **no** image is the base image used
  (`0x6e5880[element]+0xc`). Empty R16 entries load as null, and the three
  Starports' body frames (DATA.R16 3323, 3597, 3871) are empty, so their
  bracket uses the base image (offset_y 95/96). Using the empty entry's
  offset 0 put the bracket ~95 px too low.

**Bracket:**

| Piece | Position |
|---|---|
| top[s] | (x, y) |
| left side | (x, y + 4) |
| right side | (x − w(right) + w(top), y + 4) |
| bottom-left | (x, y − h(right) + 4 + H) |
| bottom-right | the same y, right-aligned like the right side |

**Health bar:**

- len = ((w(top) − 3)·hp) / max_hp. Two 1-px lines at y+1 and y+2, from x+1
  to x+1+len.
- Colour, 16-bit, by hp:

  | hp | Colour | Global |
  |---|---|---|
  | < max/3 | red | `0x51653c` |
  | < max/2 | yellow | `0x516538` |
  | otherwise | green | `0x516690` |

  The values come from `FUN_0044acc0` (16-bit, `FUN_0042f3d0(0xRRGGBB)`):
  green 0x00C800 = RGB(0, 200, 0), yellow 0xDCDC00 = RGB(220, 220, 0),
  red 0xC80000 = RGB(200, 0, 0) (doc 46 §2.2 for the red's black bug).

**Own buildings:**

- primary marker `0x6e2df4` (flag 0x80) at (x+2, y − h(right) − 4 + H);
- spice pips, stepping 3 px from x+2 at y − 1 + H: filled `0x6d8d8c` first,
  then empty `0x6d8d88`:
  - refinery (kind 4): 16 pips, filled = spice·16 / capacity (player
    +0x24254 / +0x24258);
  - silo (kind 5): 8 pips, same rule;
- upgrade marker `0x782058` at (x+2, y+6) when the player's upgrade level
  for the building group is non-zero (corrected by doc 21 §2.3; the repair
  wrench is fixed entry 3, centred, blinking).

**Own selected units:**

- harvester: 7 pips, filled = +0x29 (load);
- kind 4: 5 vertical pips, filled = +0x29 >> 5;
- group digit `0x6e2df0[group]` (+0x28, 0xff = none) at (x+2, y − 6 + H),
  or at −10 when pips are shown;
- **state** (+0x10) 0x12, waiting for a carryall (corrected 2026-10-16;
  not "Fremen type 0x12"): icon `0x782060` (DATA.R16 112) at (x+2, y+6),
  after the digit (`0x42a45e`; the infantry pass `0x42b53b` the same).

**Flashing target:** a building with flag 1 draws `0x6dc468` centred on its
image while `(+0x8c + tick) & 0x20` is set. Units flash through
`FUN_0049f310`: the grey target flash of doc 44 §3 (counter 7, grey
while bit 1 is set).

**Draw order (verified 2026-10-16):** the bracket, bar, pips, digit and
icons of an item are drawn right after the item's own images, inside the
item loop of `FUN_00428e30` (and of the infantry pass `FUN_0042af00`), so
items later in the sorted list cover them, and the effects, aircraft,
bullets and click puffs (drawn after the list) go over them.

Correction (2026-10-10): the cursor rules with a selected turret are in
docs/re/38 section 2 ("empty cell -> 2 if in range" is really a cell with
unit bits; friendly non-wall buildings give 5).

## 9. Draw list (`FUN_0042aa90`)

- Items are culled to the playfield ± 128 px.
- The list is sorted ascending by key = screen y. The key is y − 32 for
  buildings whose record has flag 0x200000 while obj +0x90 == 2.
- A new item is inserted **before the first item whose key ≥ its own**, so
  on equal keys the later-added item draws first.
- The list holds at most 1000 items.
- Item fields:

  | Offset | Field |
  |---|---|
  | +0, +4 | x, y |
  | +8 | type (1 building, 2 unit) |
  | +0xc | object |
  | +0x10, +0x14, +0x18 | main image, turret image, overlay |
  | +0x1c | next |
  | +0x20 | owner |
  | +0x21 | selected |
  | +0x22 | class |
  | +0x24, +0x28 | hp, max hp |
  | +0x2c | key |
  | +0x30 | flags |

`src/render/objects.cpp` inserts the same way (before the first key ≥
its own), so equal keys draw later-added first, as in the original.

## Notes (2026-10-08)

- Box select (`FUN_004a5ae0`, both call sites in `FUN_004433a0`): no
  Shift test. A box with at least one own unit always replaces the
  selection; Shift only toggles single clicks. Implemented as is.
- Force attack (Ctrl, `0x797e89`) implemented: any unit (order 0x14), any
  building (0x15, the engineer / saboteur flags apply), an empty cell with
  an armed unit selected (0x16, `FUN_004598d0`: selected, not dying,
  weapon != 0xFF; ProcessOrder case 3: CanTakeOrder(u, 1), SetState 6,
  +0x14 = 1, dest = the cell), else a move. Cursors as in the table above.

