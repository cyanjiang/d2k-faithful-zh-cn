# 25: Control groups, hotkeys, and the radar (1.06)

Part A covers the keyboard commands of the in-game input handler
`FUN_004433a0` (control groups, bookmarks, the letter hotkeys). Part B covers
the radar's scale, position, colours and click rules. Related: doc 10 (input,
selection, cursors, overlay digits), doc 11 §4 (radar on / off, radar image),
doc 20 §1.1 (Esc / Space), doc 21 (guard, retreat, sell, repair).

"Verified" = read from the code. Its one look question (the radar size)
was confirmed in the original (B.1).

## 0. Key arrays (verified)

The window procedure's WM_KEYDOWN / WM_KEYUP code (`0x4a3390..0x4a33aa`)
indexes two byte arrays by the **virtual-key code** (wParam, `ESI`):

| Array | Meaning | Cleared by |
|---|---|---|
| `keystate[VK]` `0x797e78` | key held (1 while down) | key up (Up / Down arrows: special case at `0x4a339a`) |
| `keydown[VK]` `0x798390` | "pressed" event | the consumer (set back to 0 after use) |

So `keystate + 0x30..0x39` = `0x797ea8..0x797eb1` (digits), `keydown +
'G'` = `0x7983d7`, etc. Ctrl = `keystate[0x11]` (`0x797e89`), Shift =
`keystate[0x10]` (`0x797e88`), Alt = `keystate[0x12]` (`0x797e8a`).

**All hotkeys below run only while no menu is open:**
`FUN_004433a0` returns early when the UI manager's +0x110 (menu open) is
set (`0x445dad`), before the hotkey block.

**`keydown[]` across a menu (verified 2026-10-16):** while a menu is open
the window procedure writes neither array for a key press except Esc and
Enter (`0x4a333b..0x4a334a`; key-up always clears `keystate`, `0x4a3468`),
and nothing clears `keydown[]` on the menu path (its only clear,
`FUN_00442540`, runs at the front-end start `0x4425fc` and the mission
start `0x448c02`). So a hotkey pressed in the same pass as the Esc that
opened the menu stays pending and runs when the menu closes; Esc / Enter
pressed in the menu have no hotkey reader. Also, `keydown` is set only on
the first press (`keystate` was 0), except the Up / Down arrows
(auto-repeat, `0x4a339a`).

## Part A: control groups and hotkeys

### A.1 Storage (verified)

The **control group is unit +0x28** (u8, 0xFF = none). It is the same
field the AI uses for its groups (doc 23); the human player and the AI sides
never share units, so there is no clash. Groups are **0..9** (keys `0`..`9`,
VK 0x30..0x39; the `0` key is group 0).

Per-group "already announced" flags `0x5165b0[10]` (u8) make the selection
voice play once per key press.

### A.2 The digit loop (`0x4460f0..0x446298`, verified)

Every loop (no menu open), for d = 0..9 in order, with Ctrl = `keystate
[VK_CONTROL]` sampled before the loop:

```
if !keystate['0' + d]:                       // key not held
    0x5165b0[d] = 0
elif Ctrl:                                   // Ctrl + d: assign
    AssignGroup(d)                           // FUN_0044b170
elif !Alt and !Shift:                        // d: select
    if SelectGroup(d, exclusive = 1) and 0x5165b0[d] == 0:   // FUN_0046cb40
        0x5165b0[d] = 1
        UnitResponse(select)                 // FUN_0049f240(0): the select voice
        for every other side s != local: deselect s's units (FUN_0046cb00)
                                         and s's buildings (FUN_0046cb20)
elif Shift (no Alt):                         // Shift + d: add to the selection
    if SelectGroup(d, exclusive = 0) and 0x5165b0[d] != 0:   // note: != 0 (quirk)
        0x5165b0[d] = 1; UnitResponse(select); deselect other sides as above
else (Alt):                                  // Alt + d: select and centre
    if SelectGroup(d, exclusive = 1):
        if 0x5165b0[d] == 0: UnitResponse(select); 0x5165b0[d] = 1
        CentreOnGroup(d)                     // FUN_0046cbd0 → camera
```

Notes:
- The keys are read from the **held** array, so a held digit re-runs the
  selection (and Ctrl + digit the assignment) **every loop**; the voice
  plays once per press thanks to `0x5165b0`.
- **Shift + digit quirk (verified, `0x44624c` `JZ`):** the voice test is
  `!= 0`, so pressing Shift + d on its own never plays the voice and never
  sets the flag: the units are added silently. The flag is 1 only when d
  was first pressed alone and Shift is added while d stays held; then the
  voice plays **every loop** while both are held (and the other sides are
  deselected). Settled by the code; nothing for the reference.
- **The other sides are deselected only in the voice blocks** (plain d on
  its first loop, `0x4462f7..0x446356`; the Shift case above), never by
  Alt + d or by the held repeats (verified 2026-10-16; our port did it on
  every call, corrected).
- There is **no double-press centring**: centring is **Alt + digit**.
- Ctrl is tested before Shift / Alt: Ctrl + Shift + d assigns.

### A.3 SelectGroup (`FUN_0046cb40(player, d, exclusive)`, verified)

```
if no own unit has +0x28 == d and state != 8: return 0       // nothing changes
for each own unit (list order):
    if +0x28 == d and state != 8:
        if flags & 0x140 == 0: selected (+0x19) = 1            // not aircraft / carried
    elif exclusive: selected = 0
for each own building: selected (+0x35) = 0                   // buildings always deselected
return 1
```

- Members leaving a factory (state 8) are neither selected nor deselected.
- Dying units (state 0x11) are **not** excluded here (they keep their
  group until removed), so a group key can select a dying member
  (verified, `FUN_0046cb40` tests only +0x28, state 8 and flags 0x140).
  Effects follow from the other code, all ported as is: its bracket shows
  until it is removed, it counts for "a unit is selected" and can give the
  select voice (`FUN_0049f240` has no state test), and the order code skips
  it (the A.7 handlers exclude state 0x11).
- An empty group changes nothing (the current selection stays).

### A.4 AssignGroup (`FUN_0044b170(d)`, verified)

```
if no own unit is selected: return                 // a building selection does nothing
for each own unit:
    if +0x28 == d: +0x28 = 0xFF                    // the old members leave
    if selected: +0x28 = d                         // the selection joins (leaves its old group)
```

No voice, no check of state or flags. A unit belongs to one group at a time.

### A.5 Centring (`FUN_0046cbd0`, verified)

The **first unit in the side's list** with +0x28 == d (any state):

```
cam_x = unit px x (+0x34 >> 16) − 480 / 2          // DAT_00786d48 = view width 480
cam_y = unit px y (+0x38 >> 16) − 380 / 2          // DAT_006dc538 = view height 380
if cam_x < 1: cam_x = 0;   if cam_y < 1: cam_y = 0 // ((v < 1) − 1) & v
cam_x = min(cam_x, map_w·32 − 480 − 1)
cam_y = min(cam_y, map_h·32 − 380 − 1)
```

(The "−1" on the far edge differs from the radar / Home clamps below,
which use `map·32 − view`.) 380 verified: `0x6dc538` = screen height
`0x4eb02c` (400) − top bar `0x4e41dc` (20), written at `0x45febd` /
`0x461489`; `0x786d48` = 640 − `0x4e41e0` (160) = 480 (`0x45fe8d` /
`0x4614e8`).

### A.6 Drawn digit (doc 10 §8, verified there)

Own **selected** units draw `0x6e2df0[+0x28]` (DATA.R16 entries 17–26 =
digits 0–9) at (x + 2, y − 6 + H) of the selection bracket, or at −10 when
the unit shows pips (harvester load, etc.). Unselected units show no digit.

### A.7 The other hotkeys (`FUN_004433a0`, `0x445e90..0x4460e8`, verified)

Each tests `keydown[VK]` once and clears it. In code order:

| Key (VK) | Flag | Action |
|---|---|---|
| D (0x44) | `0x7983d4` | multiplayer only (Internet game, `0x797e34` = 5): an order 0x33 / 0x34 (WOL), not covered |
| S (0x53) | `0x7983e3` | **Stop** `FUN_00459c40`: order 0x1c for the selected units (not dying, ≤ 64) and order 0x1d for the selected buildings |
| A (0x41) | `0x7983d1` | multiplayer only (`0x7984c4`): `FUN_00459b50`, order 0x1B (doc 58 §6.5; `GameScreen::net_ally_key`) |
| X (0x58) | `0x7983e8` | **Scatter** `FUN_00459a20`: order 0x19 for the selected units |
| P (0x50) | `0x7983e0` | **Primary** `FUN_00459ea0`: order 0x20 for the selected buildings |
| G (0x47) | `0x7983d7` | **Guard** (doc 21 §5): if an own armed unit is selected, order 0x18 and the house voice `*_GUARDING` |
| R (0x52) | `0x7983e2` | **Retreat** (doc 21 §6): if a unit is selected and `FUN_0046e3f0` finds a destination, order 0x1a and `*_RETREAT` |
| H (0x48) | `0x7983d8` | **Home**: camera on the base (`FUN_0046cf10`, A.8) |
| E (0x45) | `0x7983d5` | **Select all on screen**: `FUN_004a5ae0(cam_x, cam_y, cam_x + 480, cam_y + 380)`, the drag-box selection over the whole view (doc 10 §2); no voice |
| N (0x4E) | `0x7983de` | **Next unit**: `FUN_004a5de0` (A.9), then the camera on the selection (`FUN_004a5c00`) |
| T (0x54) | `0x7983e4` | repair mode on (`0x516438` = 2; doc 10 / 21) |
| Y (0x59) | `0x7983e9` | sell mode on (`0x516438` = 3) |
| Home (0x24) | `0x7983b4` | camera on the selected units (`FUN_004a5c00`), else on the selected buildings (`FUN_004a5cd0`) |
| F9–F12 (0x78–0x7B) | `keystate` (held) | **bookmarks** (A.10) |

Order handlers (ProcessOrder `FUN_00457810`, case = order − 0x13):

- **0x1c Stop (case 9, `FUN_004576b0`)** per unit that takes orders
  (`FUN_0049f4a0(u, 1)`): unless the state is 0x19, 0x1a, 8, 9, 0x14,
  0x13 or 0x17: SetState(7), +0x14 = 1, **dest = its "to" cell**
  (+0x48/+0x49 = +0x4c/+0x4d): it finishes its current step and stops.
- **0x1d (case 10, `FUN_00457710`)** per building: `FUN_0049e120(b, 2)` =
  building state 2, the normal "built" state whose update is the turret
  auto-fire `FUN_00496790` (doc 38 §5.5, §6: the stored target is kept;
  states 4 / 5 are the ordered attacks it leaves).
- **0x19 Scatter (case 6, `FUN_00457580`)** per unit that takes orders,
  same state exclusions: up to 8 tries of `dir = (rand() & 7) << 2`, cell
  = the neighbour in that direction (`FUN_0045de30`); the first cell that
  passes CanUnitUseSquare mode 0 → SetState(7), dest = that cell. If none:
  `FreeAdjacentCell` (`FUN_0045dee0`) mode 0 → SetState(7) with that dest;
  else mode 2 → SetState(9) (move aside), +0x14 = 7, dest = that cell;
  else nothing. **One `rand()` per try.**
- **0x20 Primary (case 0xd, `FUN_0044b1f0`)**: the building becomes the
  primary of its kind (yard 2, light factory 7, heavy factory 8, …): the
  old primary loses flags 0x90, the slot +0x26098.. points to the new one
  (which gets flag 0x80, doc 12). Full switch (verified 2026-10-16): kinds
  2, 7, 8, 9, 10, 11 (yard, light, heavy, hi-tech, IX, barracks; slots
  +0x26098..+0x260a2) clear 0x90 on the old one; kinds 12, 13, 0x11, 0x12,
  0x13 (WOR, Starport, the three palaces; +0x260a4, +0x260a6, +0x260aa,
  +0x260ac, +0x260a8) clear 0x80; then the new one gets 0x80; any other
  kind does nothing. A missing old primary only calls the "NO_PRIMARY_..."
  assert. `World::order_primary` matches.

### A.8 Home (`FUN_0046cf10`, verified)

`HomeView(side, &x, &y, select_mcv)` (thiscall, ECX = the side's
player object). **One routine, three callers:**

| Caller | Side | select_mcv | Use |
|---|---|---|---|
| `0x448a45` mission / skirmish start (`FUN_00448550`) | gSideId | 0 | the start camera; then **all four F9–F12 bookmarks** (`0x51645c..0x516474`) are set to it (loop `0x448a5b`) |
| `0x446011` the H key (`FUN_004433a0`, `0x7983d8`) | gSideId | 1 | the camera |
| `0x458ba5` order 0x30, kind 9 (airstrike, doc 35) | the ordering side | 0 | the airstrike's start point |

Target: the primary construction yard (`+0x26098` ≠ 0xFFFF; no other
check); if none, the building with the **smallest group index**
(`0x6dc5c8[rec]`, record +0x88) in the side's building list (strict <:
the first wins ties); if no building at all, the side's unit list: the
first unit, replaced by every later MCV (kind 6), so the **last MCV**,
else the first unit. Camera:

```
building: h = offset_y (runtime image +0xc = R16 header offset_y) of
              0x6e5c40[element * 32] (element image 0) if not null, else of
              0x6e5880[element] (the base image); element = record +0x14
          cam_x = (x >> 16) − 480/2;  cam_y = (y >> 16) − h/2 − 380/2
unit:     cam_x = (x >> 16) − 480/2;  cam_y = (y >> 16) − 380/2
clamp each: a value < 1 becomes 0; then min(value, map·32 − view − 1)
          (no lower clamp after the min)
nothing:  the current camera (0x798538 / 0x79853c) is returned
```

**Checked against a capture of the original (2026-10-16):** A1V1, about
8 s after Start with the mouse over the base, the yard's 3×3 cell frame
starts at screen (240, 170); the yard is cell (9, 9) = map pixel
(288, 288), so the camera is (48, 138). HomeView gives exactly that:
x 288 − 240 = 48; y (9·32 + height 96 = 384) − 113/2 − 190 = 138
(offset_y 113 of the yard's element image 0; the image height would give
146). Ours gave the same value, but the game screen started with the
mouse at (0, 0) until the first motion event, so edge scrolling moved the
view to (0, 0) at once. The exe's mouse position (`0x4eb048` /
`0x4eb04c`) is written only by the window procedure's WM_MOUSEMOVE
(`0x4a3532` / `0x4a354a`) and never reset, so a game starts with the
mouse where the front end left it; ours now reads it at the start. The
shroud in that capture matches ours pixel for pixel once the views are
aligned (only the message line and the tooltip differ).

The `h` is the image's **offset_y**, not its height (the runtime image
object is the R16 header without the type byte: +0 width, +4 height,
+8 offset_x, +0xc offset_y, doc 18 §6.2). A null element image 0 is an
empty R16 entry (or an element with no frames).

**select_mcv** (H only): when the target is a unit and it is an MCV,
`FUN_004a5750(gSideId)` clears the selected flag of every unit (+0x19)
and building (+0x35) **of the local side only**, then the MCV gets +0x19
= 1. No voice. A first unit that is not an MCV is not selected.

Building check: the routine calls the "Oh dear / That shouldn't have
happened" assert when the chosen building's +0x90 ≠ 2 (no effect on the
result).

### A.9 Next unit (`FUN_004a5de0`, verified)

Find the first own selected unit that is valid (not state 8 / 0x11, no
flags 0x140). Deselect everything (`FUN_004a5790`). Then select the **next
valid unit after it** in the list; if there is none (or nothing was
selected), select the **first valid unit** of the list. No voice. Then
the caller centres on the selection (`FUN_004a5c00`, A.11).

### A.10 Bookmarks F9–F12 (`0x4460a0..0x4460e8`, verified)

Held keys, every loop: k = VK − 0x78 (0..3).
- **Ctrl + F9..F12**: store the camera: `0x51645c + 8k` = cam_x, `+4` =
  cam_y.
- **F9..F12**: camera = the stored values (no clamping). Until set, each
  holds the start camera (A.8: `FUN_00448550` copies the HomeView result
  into all four at `0x448a5b`; an earlier version of this note said 0, 0).

### A.11 Camera on the selection (`FUN_004a5c00` units / `FUN_004a5cd0` buildings, verified)

Mean of the selected units' pixel positions (unsigned sums / count), or of
the selected buildings' (x + width/2, y − height/2) (rec +0x87 / +0x86,
`>> 1`); then − 240 / − 190, clamped to [0, map·32 − view] (no −1 here).
Returns 0 when nothing is selected (camera unchanged).

## Part B: the radar

### B.1 Scale and position (verified: `FUN_00426000`, `FUN_00461f60`)

- The radar image is a **map_w × map_h** surface (`FUN_00461f60`: both
  radar buffers are created with the map size in cells) — **one pixel per
  cell, never scaled**.
- It is blitted **centred** in the 128 × 128 box at (497, 8):
  `x0 = 497 + (128 − map_w) / 2`, `y0 = 8 + (128 − map_h) / 2` (signed
  integer division; maps are even-sized: `FUN_004433a0` errors on odd
  sizes).
- Units: one pixel at (x0 + to_x, y0 + to_y) (doc 11 §4).
- Viewport rectangle: (x0 + cam_x/32, y0 + cam_y/32) to (+480/32 − 1,
  +380/32) = **14 × 11** px, 1-px lines (`FUN_00430d10`: four lines),
  colour `0x516690` (16-bit) / `0x5162b4` (8-bit).

So on a small map (A1V1 28 × 24) the radar is a 28 × 24-pixel picture in
the middle of the box, surrounded by the grille. **Our draw already does
this** (`Sidebar::draw_radar`, x0 / y0 centred, 1 px per cell). If the
original looks larger to the user, the difference must come from
somewhere else (the code leaves no other scale). **Confirmed in the
original by the user (2026-10-16):** the radar picture is small or large
with the map size (1 px per cell, centred), as here; the old size question
was this effect.

### B.2 Colours (doc 11 §4, verified there)

Per cell, from the cell flags (`FUN_00462140`): building = owner colour
`0x795518[cell & 7]`; spice (0x700000) RGB(100,100,100); sand (0x10000)
RGB(30,30,30); drivable (0x2000) RGB(60,50,50); else RGB(70,70,70).
Units: player colour; sandworm `0x5162b8` RGB(0xc6,0xb5,0xa5); only on
cells with shroud byte 0, own units or not flag 4, not aircraft (except
kind 9).

### B.3 Clicks (`FUN_004433a0`, `0x443d00` and `0x446..`, verified)

The radar area for clicks is the **map image only**, with strict bounds:
`x0 < mouse_x < 497 − off_x + 128` and `y0 < mouse_y < 8 − off_y + 128`
(off = (128 − map) / 2), and it needs a radar building (kind 1,
`FUN_00442910(side, 1)`), power ≥ 100 (`player+0x245b0 > 99`) and
`0x51643c` = 0 (map toggle off).

- **Left button held, no unit selected** (`0x798520`, every loop, drag
  works):
  ```
  cam_x = (mouse_x − x0)·32 − 240;  cam_y = (mouse_y − y0)·32 − 190
  cam_x = clamp(cam_x, 0, map_w·32 − 480);  cam_y = clamp(cam_y, 0, map_h·32 − 380)
  ```
  (the cell under the mouse becomes the view centre).
- **Units selected**: cursor 6 over the radar; on left **release** the
  selected units get a move order to the cell under the mouse
  (`FUN_00459450`, doc 10 / 11).
- **Right button**: deselects (doc 10 §2), as on the playfield.

### B.3a No attack marker (verified 2026-10-12)

The radar shows **no** "under attack" mark. `FUN_00426000` (radar on) is
the whole draw: the shown buffer `0x6d7ba0`, one pixel per visible unit
(sandworm colour for kind 5), the viewport rectangle; nothing else. The
building hit flag 0x1000 is set by the damage code (`0x49d58a`, doc 18
§2.3) and has no reader in the 1.06 listing. The only warning is the
`*_UNDERATTACK` voice (300-tick limit).

### B.4 Radar off

Already in doc 11 §4 (logo fade over 65 loops, grille on power-up).

## Implementation checklist (our code)

1. **Keys** (`src/app/game_screen.cpp`, event loop): keep a held-key array
   (VK-like: SDL keycodes `0`..`9`, F9..F12, modifiers) and a pressed-event
   array; skip all hotkeys while `menus_.ingame_active()`.
2. **Groups** (`src/sim/world.*`): `Unit::ai_group` (+0x28) already exists —
   use it for the human too; add `World::assign_group(p, d)`
   (A.4), `select_group(p, d, exclusive)` (A.3, also deselect own buildings),
   `group_centre(p, d, x, y)` (A.5); the per-group announced flags and the
   voice (`unit_response(false)`) in the game screen; Alt / Shift variants;
   deselect other sides' selections (single player: nothing to do).
3. **Overlay digit**: draw DATA.R16 entry 17 + group above selected units
   (doc 10 §8; check `render` / `game_screen` overlay code).
4. **Hotkeys**: S stop (order 0x1c / 0x1d), X scatter (one `rand()` per try),
   P primary, G / R (already wired to the sidebar buttons: reuse
   `order_guard` / `order_retreat` + voices), H home, E select on screen
   (`box_select` over the view), N next unit + centre, T / Y repair / sell
   mode, Home centre on selection, F9–F12 bookmarks (Ctrl stores).
5. **Radar** (`src/app/sidebar.cpp`, `game_screen.cpp`): scale and centring
   already match B.1; add the left-drag camera (B.3, strict bounds, radar
   building + power ≥ 100 + no selection), the move order on release with
   units selected (cursor 6), and keep the 14 × 11 view rectangle. (The
   size question: settled in B.1, the radar scales with the map size.)
