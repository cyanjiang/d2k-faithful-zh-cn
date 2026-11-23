# Multistate box, value bar number, label refresh, menu tooltips (1.06)

Sources are 1.06 addresses, decompiled with GhidraMCP. The data was read
from `Skirmish.uil`, `text.uib`, `colours.uib` and `UI_GFX`. Related docs:
03 (UI scripts, checkboxes), 20 §3 (value bar), 27 (list and edit boxes),
51 (the skirmish page), `formats/ui.md`, `formats/fonts.md`.

Summary:
- MULTISTATEBOX is element **type 5**. A **left press** advances the state
  by one and wraps to 0 at the count. It draws one opaque image per state
  and has no hover or pressed look.
- VALUEBAR prints its value with `itoa` in font 0. The text is at
  **(x + w + 4, y)**, inside a 40 px area right of the bar.
- **The `crates` and `worms` labels are refreshed.** The setter callbacks
  of `gNetCrates` and `gNetWorms` rewrite them. This corrects doc 51 §2.1.
- **Menu tooltips exist.** They only work on SKIRMISH and the multiplayer
  setup pages. The box appears after 600 ms of hovering and follows the
  mouse. It is filled Black, has a Yellow frame and uses font 0.

## 1. MULTISTATEBOX

### 1.1 Parser (`FUN_0043ac50`, keyword at `0x4df350`)

The parser reads 6 body lines, then passes every line up to
`END_MULTISTATEBOX` to `FUN_0043c270` (the commands).

| Line | `wormsCheck` | Read as | Field |
|---|---|---|---|
| 1 | `442, 307` | x, y (`FUN_0048d9d0` tokens 0, 1) | +0x08, +0x0c |
| 2 | `20, 18` | w, h | +0x10, +0x14 (replaced by the image size, §1.2) |
| 3 | `DarkGrey // border colour` | token 0, colour index (`FUN_0048db90`) | border +0x77c |
| 4 | `4` | state count (u8) | +0x7d1 |
| 5 | `VDarkGrey Grey // colours` | tokens 0..count−1, one colour per state | +0x780 + 4·i |
| 6 | `Wicon_0 wicon_1 wicon_2 wicon_3 // gfx` | tokens 0..count−1, one image per state | +0x7a8 + 4·i |

- Slots `count..9` get `Transparent` (colours) and `NONE` (`0x4de68c`,
  images).
- **The binary reader does not strip comments** (`FUN_004825b0`), and the
  tokenizer `FUN_0048da20` splits on spaces and tabs only. So the colours
  line gives the 4 tokens `VDarkGrey`, `Grey`, `//` and `colours`. The last
  two are unknown colour names. `FUN_0048db90` returns **0 = `Transparent`**
  for an unknown name (index 0 of `colours.uib`). This matters only when a
  state has no image, which is not the case here.
- Our `ui::strip_comment` removes `// …` first. The result is the same
  (missing entries are treated as Transparent), as long as states without a
  colour are drawn as Transparent.

### 1.2 Object (`AddMultiStateBox` `FUN_00438290`, ctor `FUN_0048af30`, size 0x7d4)

| Offset | Field |
|---|---|
| +0x00 | type = **5** (`FUN_00482780`) |
| +0x18 | dirty flag |
| +0x77c | border colour index |
| +0x780..+0x7a4 | colour index per state (10) |
| +0x7a8..+0x7cc | image per state (10). `NONE` (`__strcmpi`) gives null. Otherwise `UI_gfx\<name>` is loaded by `FUN_00491330` |
| +0x7d0 | current state (u8), **0 at creation** |
| +0x7d1 | state count (u8) |
| +0x777 | RedrawEachTime (command 0x0D) |

- **Each loaded image sets w/h** (+0x10/+0x14). So the last state's image
  gives the size, and the hit rect is the image size. `WICON_0..3.BMP` are
  all 20×18, the same as the script.
- A menu has 10 multistate slots (`+0x3cc`). An 11th box logs
  "CInterface::AddMultiStateBox / No free slot".

### 1.3 Drawing (`FUN_0048b0e0`, called from `FUN_0043c4b0` when +0x18 is set)

- With an image for the current state: the image is drawn **opaque**
  (`FUN_00430770` flags 0,0) at (x, y).
- Without one: `FUN_0042d660(rect, fill = colour[state], light = border,
  dark = border)`. That is a filled rect with a 1 px border-colour frame
  (left and top lines in "light", bottom and right lines in "dark").
- No text, no hover look, no pressed look. Then +0x18 = RedrawEachTime.
- Draw order: multistate boxes come after checkboxes and before edit boxes
  (as in our `draw_order`).

The images are `WICON_0..3.BMP` (8-bit BMPs, drawn opaque): Off, Low,
Medium, High.

### 1.4 Click

- **WM_LBUTTONDOWN** (`0x4a3590`) → `FUN_004711a0` → `FUN_00437840`
  (UI button-down). For every multistate box whose rect contains the mouse
  (`FUN_00484f10`, a plain `PtInRect` in 16-bit mode), it calls
  `FUN_0048b0d0`: +0x18 = 1, then `FUN_00482850(0)`.
- `FUN_00482850` type-5 block (`0x482a0e`):
  `new = state + 1; if new < count → state = new; else state = 0` (and
  mark it dirty). So the cycle is **0 → 1 → 2 → 3 → 0**.
- **Only the left button.** There is no right-click or backwards cycle.
  `WM_RBUTTONDOWN` does not reach `FUN_00437840`, and nothing else calls
  `FUN_0048b0d0`.
- **It acts on the press, not on the release.** The mouse-up handler
  `FUN_00437600` handles buttons, toggle buttons, list boxes and value bars
  only. **Checkboxes work the same way** (`FUN_00437840` also calls
  `FUN_0048b0d0` for the 50 checkbox slots at `+0x304`). Doc 03 does not
  say this, and our runner toggles checkboxes on release.
- **Double-click:** the window class has `CS_DBLCLKS` (style 8,
  `0x4a3c03`). A second press within the double-click time arrives as
  WM_LBUTTONDBLCLK. That message sets `0x798614` and falls into the
  LBUTTONDOWN code, which then **skips** `FUN_004711a0` (`0x4a35e3`). So a
  fast second click does not advance the box (or toggle a checkbox): two
  fast clicks on the worms box give one step. Ours: the same (SDL's
  `clicks` count on the press, `mouse_double_`).
- The click commands then run in script order, with `param_2 = 0` (all
  non-init commands). For `wormsCheck` that is only `SetFromUIValueU1To
  gNetWorms`.
- Multiplayer only (`0x4828cd..0x48291a`): a checkbox or multistate box
  shows `NoChangeOptions` and does nothing when **all** of these hold: the
  top page is not IPXLOBBY, a WigNet session exists (`DAT_00798490`), I
  am not the host (`FUN_00491660(session + 0x418)` = 0), the network flag
  `0x7984c4` is set, the top menu has +0x38 set, and the element's **y**
  (+0x0c; corrected, it is not x) is above 100. The constructor
  `FUN_004359a0` sets +0x38 only for HOSTHEADTOHEAD, CLIENTMULTIPLAYER,
  HOST_MP2, JOIN_MP2, IPXLOBBY, INETLOBBY and CHATSERVER. So it never
  applies to SKIRMISH, whatever `DAT_00798490` holds (DestroyWigNet in
  1.06 keeps the wrapper, doc 58 §7). Ours: `MenuRunner::no_change_options`
  (menu_lobby.cpp).

### 1.5 `CheckIfValueU1Init` on a multistate box (0x7D, `FUN_0047faf0`)

Init command, run once at creation (`FUN_00484000` case 0x7d).
- Type 4 (checkbox): checked = (global == value), as in doc 03.
- **Type 5: the value argument is ignored.** The global is looked up by
  name in the U1 table `0x4c9688` (11 entries of `{name[0x3c], u32 addr,
  u32 callback}`), and **state = the global's byte**. If that is ≥ count,
  the exe logs "SetfCurrentState: new state is out of range" but still
  stores it. That box then draws slot `state` (NONE image, Transparent
  colour), so nothing is visible. gNetWorms > 3 is already reset to 0 when
  the cfg is loaded (doc 51), so this cannot happen in 1.06.
- The same function is the click command `CheckIfValueU1` (0x85). For a
  multistate box it returns 1 (continue), so it never stops the command
  list.

U1 table `0x4c9688`:

| # | Name | Address | Callback |
|---|---|---|---|
| 0 | gMissionNumber | `0x4de610` | — |
| 1 | gNetUnitCount | `0x4e3b00` | `0x4900d0` |
| 2 | gNetTechLevel | `0x4e3b04` | `0x490100` |
| 3 | gNetAIPlayers | `0x4e3b0c` | `0x490130` |
| 4 | gNetPlayerSide | `0x6b9840` | `0x490320` |
| 5 | gNetPlayerColour | `0x6b9844` | `0x490560` |
| 6 | gNetPlayerHandicap | `0x6b9848` | `0x490830` |
| 7 | gNetSerialComPort | `0x4e8bf4` | `0x490060` |
| 8 | **gNetWorms** | `0x6b9838` | **`0x490190`** |
| 9 | gMaxPlayers | `0x4e8bec` | — |
| 10 | gTournamentGame | `0x4f3d3c` | — |

### 1.6 `SetFromUIValueU1To` (0xBB, `FUN_0047e470`)

The command walks all 11 entries of the U1 table (it does not stop at the
first match). For the entry whose name matches:

| Element type | Value passed |
|---|---|
| 3 list box | selection − 1, only when selection ≠ 0 |
| 8 value bar | value (+0x7b0) |
| 5 multistate box | **state (+0x7d0)** |

A null callback writes the byte directly. Otherwise the callback gets the
value. Other types do nothing.

**gNetWorms callback `0x490190`:** unless `gGameType == 5` and
`0x4db9c8 == 0`, if the value differs from gNetWorms it stores the value
and **tail-jumps to `FUN_00493350`**, the worms label refresh (§3).
`0x4db9c8` is set by CreateWigNet (`0x480f55`) and written by the
Westwood Online code (`0x40a50e` / `0x40c54e` / `0x41698d`); the guard
only matters for gGameType 5 (Internet), which our build does not have
(doc 58 §7). So the callbacks always store in skirmish and LAN games.

The U1 callbacks for unit count, tech and AI (`0x4900d0` and others) have
the same guard and only store the value. They refresh no label.

## 2. VALUEBAR number

### 2.1 Parser (`FUN_0043b8b0`; `VALUEBAR_NOVAL` passes 0, `VALUEBAR` passes 1)

| Line | Skirmish bars | Field |
|---|---|---|
| 1, 2 | `226, 272` / `90, 12` | x, y, w, h |
| 3 | `VDarkRed // background colour` | +0x784 |
| 4 | `Red // slider colour` | +0x788 |
| 5 | `DarkRed // highlight colour` | +0x78c |
| 6 | `Black // lowlight colour` | +0x790 |
| 7 | `JHighLightText // number font colour` | **+0x77c** |
| 8 | `Transparent // number back colour` | **+0x780** |
| 9 | `scrbar // region gfx` | +0x7f0 image. Its size replaces w/h, so `SCRBAR.BMP` 92×12 gives **w = 92** |
| 10 | `scrsli // bar gfx` | +0x798 thumb image |
| 11 | `min max step default` | +0x7a4 / +0x7a8 / +0x7ac, value +0x7b0 |

The "show number" flag is at **+0x7ec** (1 for VALUEBAR). The font is
+0x775, which `FUN_00482830` sets to **0** for every element (font 0 =
FONT.FNT, our `menu_font()`). No command changes it on these pages.

### 2.2 Drawing (`FUN_0048cbf0`)

1. If the number back colour is index 0 (`Transparent`) and there is no
   saved image yet (+0x794): allocate a **40 × h** surface and copy the
   screen rect **(x + w + 1, y) to (x + w + 41, y + h)** into it. This is
   skipped in game (`gGameState == 2`). It happens on the first draw only,
   after the background and the earlier element classes are already on the
   screen. The image is kept for the life of the element.
2. The region image (flags 1,1) is drawn. Without one, a bevel box is drawn
   in the colours.
3. **Only if max (+0x7a8) ≠ 0:** the thumb, then the number when +0x7ec is
   set:
   - number rect = (x + w + 1, y, 40, h).
   - Back: a saved image is blitted back opaque at (x + w + 1, y), which
     erases the previous number. With a non-Transparent back colour, the
     rect is filled and framed in that colour instead.
   - Text: `itoa(value, 10)` (+0x7b0, plain decimal, no separators, e.g.
     `7000`). It is drawn by `FUN_0042dc00` at **(x + w + 4, y)** (rect
     left + 3, top of the bar, not centred vertically), in font +0x775 (0)
     and colour +0x77c (`JHighLightText` = RGB 207,167,111). The text is
     not clipped.
4. +0x18 = RedrawEachTime.

**When it is redrawn:** whenever the bar is dirty (+0x18), as part of the
whole bar. That happens on every value change: mouse down left or right of
the thumb, auto-repeat every 200 ms, drag (doc 20 §3), init, and a
full-menu redraw. **The number updates live while dragging,** because the
drag writes +0x7b0 and marks the bar dirty. The global is only written on
release.

Skirmish bars: x + w + 4 = 226 + 92 + 4 = **322**, at y = 272 / 290 / 309 /
326. The values are 0..10, 1..7, 2500..7000 and 1..5.

### 2.3 `GetValueU1Init` / `U2Init` clamp (`FUN_00484000` 0x74 / 0x75), confirmed

Only for type 8. The code gets the global's address (`FUN_0048e430` u8,
`FUN_0048e4b0` u16), then:
```
if (max < *g) *g = max;      // unsigned compare
if (*g < min) *g = min;
v = clamp(*g, min, max), then clamp(v, +0x7b4, +0x7b8)
if (v != value) { value = v; thumb rect recomputed; dirty }
```
So **the global itself is clamped and written back**. Our
`run_init_commands` already does this (`menu_runner.cpp:510-514`). The
callback is not called, because this is a direct write. S1/S2/S4/U4
(0x71/0x72/0x73/0x76) work the same way.

### 2.4 Clicks and drag (`FUN_0048c640`, `FUN_0048c450`, `FUN_0048c9c0`, verified 2026-10-16)

The region rect (+0x7cc..+0x7d8) is (x, y) .. (x + w, y + h) with the
final w (the region image's width, set at `0x48c176` before the rect at
`0x48c2f0`). The thumb rect is +0x7bc..+0x7c8, width w / 5.

- **Press** (`FUN_0048c640`): network client pages first show
  `NoChangeOptions` (the same gate as §1.4, but for **any** y; IPXLOBBY
  excluded), Internet client pages too. Then only inside the region:
  on the thumb → drag, grab offset +0x7e8 = mouse x − thumb left; left of
  the thumb → value − step; **right of the thumb's right edge** (+0x7c4)
  → value + step; on that edge pixel → nothing.
- **Steps** (press and the repeat `FUN_0048c450` every > 199 ms; the
  repeat timer is reset before the inside test) work in **unsigned**
  arithmetic, then clamp to [min, max]: value − step below 0 would wrap
  to the **maximum**, but it cannot be reached: with min 0 and value 0
  the thumb sits at the left end, so no press inside the region is left
  of it. The thumb is recomputed (x + (w − w/5)·(value − min) / (max −
  min), unsigned) only when the value changes.
- **Drag** (`FUN_0048c9c0`), X = mouse x − grab offset:
  - X ≤ x: value = min; X ≥ x + w − w/5: value = max; the thumb is
    recomputed only if the value changed, else it stays where it was;
  - between: the thumb's left edge = X (it follows the mouse, it does
    not snap), and
    `value = ftol(min − ((X − x) / (w − w/5)) · 100.0f · n · step ·
    (−0.01f))`, n = (max − min) / step (unsigned), evaluated on the x87
    at 53-bit precision in that order (`0x48cb13..0x48cb8c`; constants
    `0x4d7018` = 100.0f, `0x4d701c` = −0.01f), then rounded down to a
    multiple of step (`v − v % step`, absolute) and clamped. Because
    100.0f · (−0.01f) is not exactly −1, an exact multiple comes out a
    hair low and truncates one lower: on the unit-count bar (w 92, n 10)
    X − x = 37 gives 4, not 5.
- **Release** (`FUN_00437600`): after a drag (+0x7dc) the thumb is put
  back at the value's place, then the bar's commands run; after steps
  (+0x7e0) the commands run.

Ours: `ui::valuebar_drag_value` (the formula), `ui::valuebar_drag` (the
ends and the thumb), `ui::valuebar_step` (src/ui/render.cpp) and
`ElementState::drag_thumb_x`; tests in `tests/ui/test_ingame_menus.cpp`
and `tests/campaign/test_front_end.cpp`.

## 3. Label refresh for `crates` / `worms` (corrects doc 51 §2.1)

Doc 51 says the labels are not refreshed. **That is wrong.** The refresh
happens in the special-setter callbacks:

| Click | Setter | Callback | Refresh |
|---|---|---|---|
| `crateCheck` | `SetValueBFTo gNetCrates TOGGLE` (0xB5, `FUN_0047df90`). The BF table `0x4ca200` (23 entries), entry 2 | `0x490240(!gNetCrates)` | `FUN_004931c0` |
| `wormsCheck` | `SetFromUIValueU1To gNetWorms` (0xBB) | `0x490190(state)` | `FUN_00493350` |

Both callbacks only act when the value changed, and they have the same
network guard as above. The refresh function:
1. Finds the first open menu of SKIRMISH, HOSTHEADTOHEAD,
   CLIENTMULTIPLAYER, HOST_MP2, JOIN_MP2, HOSTINETGAME, CLIENTINETGAME
   (`FUN_00470f10`). Then it finds the text box `crates` / `worms` in it
   (`FUN_004797d0`).
2. Picks the key: `CratesOff` / `CratesOn` by gNetCrates, or `WormsOff` /
   `WormsLow` / `WormsMedium` / `WormsHigh` by gNetWorms 0..3 (other
   values: no change).
3. If the box text (+0x790) differs from the translated string, it copies
   the string to +0x790, sets +0x858 = 1 (show +0x790 as is) and marks the
   box dirty.

The labels are transparent TEXTBOX_L. The text box draw (`FUN_0048bb10`)
restores the background it saved at the first draw (+0x860) before it
prints, so the old text disappears.

The `SetValueBFTo` handler compares the argument with `TRUE` (`0x4e45fc`),
`FALSE` (`0x4e45f0`) and `TOGGLE` (`0x4e8700`). TOGGLE passes `!*addr` to
the callback, or writes it directly when there is no callback.

The crates label refreshes immediately on the click, so "Crates off"
becomes "Crates on" (the callback runs inside the click command, and the
label box is marked dirty there).

## 4. Menu tooltips (`FUN_004712c0`, the UI manager, every loop)

### 4.1 When they are active

- `gGameState == 1` (front end), **and the top menu** (`FUN_00411e70`) is
  one of: HOSTINETGAME, CLIENTINETGAME, INETLOBBY, IPXLOBBY,
  HOSTHEADTOHEAD, CLIENTMULTIPLAYER, HOST_MP2, JOIN_MP2, **SKIRMISH**.
- **and** the top menu has a text box named `tooltip` (`FUN_004797d0`).
  These menus have one: `CGameSes`, `ClientMP`, `HGameSes`, `HostHTH`,
  `IPXLobby`, `IntLobby`, `Skirmish`, `hostMP2`, `joinMP2`.
- There is no option switch. The in-game tooltip flag `0x517568` (doc 20)
  is not read here.

### 4.2 Which element is hovered

The key is `"tooltip_%s"` (`0x4e7c2c`) + the element name. It counts only
if it exists in text.uib (`FUN_0048dc70`, case-insensitive). The scan
order is below. **Within a class every element is tested, so a later hit
overrides an earlier one.** A hit whose key does not exist clears the
result. For buttons the name stays in the buffer, which has no effect.

| # | Class | Slots |
|---|---|---|
| 1 | buttons | `+0x5c` (80) |
| 2 | checkboxes | `+0x304` (50) |
| 3 | multistate boxes | `+0x3cc` (10) |
| 4 | value bars | `+0x5d4` (20) |
| 5 | list boxes (`playerList` / `WOLlist` have per-row MP texts) | `+0x2b4` (20) |
| 6 | edit boxes | `+0x3f4` (20) |

Text boxes, group boxes, toggle buttons and animations have no tooltips.
The hovered element's rect (left, top, right, bottom) is kept for the
placement.

Skirmish keys in text.uib: `tooltip_unitCountBar` ("Set number of
starting units"), `…techLevelBar`, `…creditBar`, `…aiBar`, `…crateCheck`
("Set crates"), `…wormsCheck` ("Set worm frequency"), `…atreidesCheck`,
`…harkonnenCheck`, `…ordosCheck`, the 6 `…colour*Check`, `…mapList`,
`…nameEdit`, `…easyCheck`/`normalCheck`/`hardCheck`, `…start`, `…cancel`,
`…loadMission`.

### 4.3 Delay

There are two globals: the remembered key `0x7956a0` (char[], initially
empty, survives menu changes) and the timer `0x7975c8`.

Each loop:
1. If a key K is hovered and (remembered is empty or remembered ≠ K):
   timer = `GetTickCount()` (`0x471de1`).
2. **Show** if K is hovered, remembered == K, and
   **|timer − now| > 600 ms** (`cmp eax, 0x258` at `0x471e1e`, strict).
3. Otherwise: if the buffer holds an existing key, remembered = that key.
   Then **hide**.

Consequences:
- The first loop over an element starts the timer and remembers the key.
  The box appears after just over 600 ms.
- Moving to another element with a tooltip restarts the delay.
- **Quirk:** leaving to an area without a tooltip does not forget the key
  or reset the timer. **Coming back to the same element shows the tooltip
  at once** (when more than 600 ms passed since the first hover). This
  follows from the code above (the key and the timer are only replaced by
  a different hovered key). Ours: the same (`MenuRunner::update_tooltip`).
- The `strcmpi(remembered, K) != 0` branch inside "show" is unreachable.
  It would set the text alignment (+0x85c) by screen half.

### 4.4 Show (every loop while the conditions hold)

```
text  = text.uib[remembered]
w     = width(text, font 0) + 10           // FUN_0042dfb0(text, 0)
h     = 20
y     = (elem_top + elem_bottom) / 2 < 200 ? mouse_y + 20 : mouse_y - 25
x     = max(mouse_x, 0);  y = max(y, 0)
if (x + w > 640) x = 639 - w
if (y + 20 > 400) y = 379
```
The `tooltip` text box gets: position (x, y), size (w, 20), background
+0x77c = **Black**, frame +0x780 / +0x784 = **Yellow** (`colours.uib`:
Black 0,0,0; Yellow RGB 222,99,0), text +0x790 = the string with +0x858 =
1, and its saved background is freed (`FUN_0048bae0`). It is marked dirty
if the text changed.

Then `FUN_0043ca50` marks **the whole top menu dirty**: the background and
every element. So while a tooltip is visible the menu is redrawn every
loop, and **the box follows the mouse**.

### 4.5 Hide

If the box is not already hidden (its background is not Transparent), it
is reset: colours Transparent, text "" (`0x4f0f18`), position (0,0), size
4×4, saved background freed, the whole menu dirty (`FUN_0043ca50`). An
already hidden box is left alone (`goto 0x472414`).

### 4.6 Drawing (`FUN_0048bb10`, text box)

`FUN_0043c4b0` draws the text box named `tooltip` **last**, after all
other elements and animations. The normal text box pass skips it by name
(`0x4df55c`). The script's `TEXTBOX` gives alignment 2 (centre). With a
non-Transparent background no back image is saved, and the box is drawn by
`FUN_0042d5d0`:
- `FUN_0042d660`: fill the rect Black, then a 1 px frame: left and top in
  +0x784, bottom and right in +0x780. Both are Yellow.
- Text centred: x = left + (w − text_w)/2 = **left + 5**, y = top + (20 −
  font-0 first-glyph height)/2. Colour = the script's font colour field
  (+0x788): `JHighLightText` (207,167,111) on Skirmish. Font 0.

The hidden box (Transparent, 4×4 at 0,0) saves and restores the 4×4
screen corner, so it draws nothing visible.

A shown tooltip when a pop-up opens over the page (e.g. `loadMission` →
LOAD): the gate (top menu) fails, so nothing touches the box; it keeps
its last state in SKIRMISH, which is not redrawn while the pop-up is on
top. When SKIRMISH is the top menu again, the first loop either shows it
for the hovered element or hides it (§4.5). Ours: the same
(`update_tooltip` returns when the top menu is not a tooltip page and
leaves `tip_` as it was).

## 5. Implementation checklist

`src/app/menu_runner.cpp`:
- [ ] `first_action`: `MultiStateBox` → 6 (pos, size, border, count,
      colours, gfx). Today it falls to `default` (no actions), so
      `CheckIfValueU1Init` and `SetFromUIValueU1To` never run.
- [ ] `handle_input`: MULTISTATEBOX is clickable. On a **left press**
      inside its rect: `state = state + 1 < count ? state + 1 : 0`, mark it
      dirty, then `execute(actions)`. Not on release, not with the right
      button.
- [ ] Checkboxes also act on the **press** (`FUN_00437840` →
      `FUN_0048b0d0`). Today they act on release (`pressed_ == hovered_`).
      Move them to the press path. The button / list / value-bar release
      logic stays.
- [ ] Optional (faithful): ignore the second press of a double-click
      (`CS_DBLCLKS`, §1.4) for checkboxes and multistate boxes. Use SDL's
      `clicks == 2` on the button-down event.
- [ ] Init: keep `CheckIfValueU1Init` on a multistate box as state =
      global, ignoring the value argument (even when one is given).
- [ ] Setter callbacks: after `SetValueBFTo gNetCrates …` and
      `SetFromUIValueU1To gNetWorms`, **if the value changed**, set the
      `crates` / `worms` text box key (`CratesOff`/`CratesOn`,
      `WormsOff`/`Low`/`Medium`/`High`). Then mark it dirty with
      `background_dirty` + all dirty, like `SetStringInTextBox`.
- [ ] `SetFromUIValueU1To` on a list box (selection − 1, only when
      selected) and on a value bar (value), for completeness.
- [ ] Tooltips: in the per-loop update, when the top menu is SKIRMISH (or
      one of the MP names) and it has a `tooltip` text box: scan buttons,
      checkboxes, multistate boxes, value bars, list boxes and edit boxes
      in that order (last hit wins). Then apply the remembered-key and
      timer logic of §4.3 (keep the "instant on return" quirk, > 600 ms,
      `SDL_GetTicks`). Compute the box as in §4.4 every loop while shown,
      and redraw the whole menu every loop while it is shown and once when
      it hides. The remembered key is global (not per menu).

`src/ui/render.cpp`:
- [ ] `MultiStateBox` case: the image `field(e, 5)` token `state` (split
      on spaces), drawn **opaque** at (x, y). Without an image, fill with
      token `state` of `field(e, 4)` and frame in `field(e, 2)`. A missing
      token means Transparent. No text, no hover or pressed state.
      `ElementState::value` holds the state.
- [ ] `ValueBar` case: if the header starts with `VALUEBAR ` (not
      `VALUEBAR_NOVAL`) and max ≠ 0, draw `std::to_string(value)` in font 0
      at (x + w + 4, y) with `field(e, 6)`. Use w = the region image width
      (92 on Skirmish) and the same glyph-origin convention as the other
      `FUN_0042dc00` calls: it is the same function as for the list rows,
      so it takes the same offset (our +2 of `draw_text_in`). The 40×h area
      at x + w + 1 must show the background
      again before the new number: with a Transparent back colour
      (`field(e, 7)`), redraw the menu background under that rect.
      Otherwise fill and frame it in that colour. Remove the TODO at
      `render.cpp:466`.
- [ ] Tooltip box: a `tooltip` text box with a runtime rect, Black fill,
      1 px Yellow frame, text centred (left + 5), drawn **after every other
      element**. Hidden = draw nothing.
- [ ] `draw_order`: the `tooltip` text box goes last, after animations and
      movies (`FUN_0043c4b0`).

Docs to update after implementing: doc 51 §2.1 ("Labels are not
refreshed" is wrong; see §3), doc 51 §2.2 rows for 0xBB / tooltips, doc 03
(checkbox clicks act on the press), `docs/parity.md` (menu tooltips).
