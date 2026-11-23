# 42: The CREDITS screen (1.06)

Written for a line-by-line port of the scrolling credits. They are shown
after the last campaign mission (under the score screen, doc 24) and from
the Westwood logo button on the main menu.

"Verified" means it was read from the decompile, and from the disassembly
where the decompile is misleading. No open guesses remain (§10).

## 1. Functions and globals

| Address | Name used here | Role |
|---|---|---|
| `FUN_004359a0` | menu loader | for the name `CREDITS`: `menu+0x34` = 1, calls CreateCredits |
| `FUN_0043d2a0` | CreateCredits | builds the row list, resets the scroll (§3) |
| `FUN_0043d520` | CreditsDraw | draws the visible rows **and** advances the scroll (§4) |
| `FUN_0043d4c0` | DestroyCredits | frees the rows, switches input back on (§7) |
| `FUN_0043c4b0` | menu draw | calls CreditsDraw; forces input off and a full redraw (§4.1) |
| `FUN_00437040` | menu destroy | calls DestroyCredits when `menu+0x34` is set |
| `FUN_0043cb70` | menu char handler (WM_CHAR) | Esc on CREDITS queues RemoveMenu (§5) |
| `FUN_0043cdf0` | menu key handler (key down) | Tab on CREDITS queues RemoveMenu (§5) |
| `FUN_00437840` | mouse-down handler | on CREDITS calls `FUN_0043cb70(0x1B)`, but only while input is on (§5) |
| `FUN_004737d0` | queue nav command | stores `{id, arg}` only if none is pending (`mgr+0x54 == 0xE5`) |
| `FUN_00473300` | RemoveMenu (nav 0xE2) | closes the top menu, only when gGameState = 1 |
| `FUN_004706c0` / `FUN_004706f0` | input switch | doc 04 "Mouse input switch" |
| `FUN_0042e050` / `FUN_0042e110` | centred text | solid / translucent, centred in a box |
| `FUN_0042dc00` / `FUN_0042de70` | text | solid (`FUN_00431e40`) / translucent (`FUN_00432060`) glyph blits |
| `FUN_0048db90` | colour index by name | case-sensitive scan of the colours.uib table `0x7975a0`, 0 when missing |

| Global | Meaning |
|---|---|
| `0x516214` | row array (pointers to 0x10-byte objects; text pointer at +0xC) |
| `0x516218` | row count |
| `0x51621c` | scroll offset in pixels |
| `0x516220` | scroll length = pitch × row count |
| `0x516224` / `0x516228` | first / last row index drawn this update (written only) |
| `0x516230` | time the scroll reached the end |
| `0x516234` | time of the last scroll step |
| `0x51622c` | the CREDITS menu |

Layout constants (initialised data, read-only; no code writes them,
verified by xrefs):

| Address | Value | Meaning |
|---|---:|---|
| `0x4deda8` | 10 | line height |
| `0x4dedac` | 1 | pixels per scroll step |
| `0x4dedb0` | 0 | box left |
| `0x4dedb4` | 50 | box top |
| `0x4dedb8` | 640 | box right |
| `0x4dedbc` | 350 | box bottom |
| `0x4dedc0` | 5 | line gap |

So the **pitch** is 10 + 5 = **15 px**, the box is x 0..640, y 50..350
(300 px high), and the fade band is 300 / 10 = **30 px** at each edge.
`0x4f0f18` is an empty string (""), also used as the empty argument of the
RemoveMenu command.

## 2. The data (verified against 1.06 text.uib)

`text.uib` has 1,322 entries. `CreditsStart` is entry **972** and
`CreditsEnd` entry **1321** (the last one); both have empty values. The 348
`CreditEntry` rows are entries 973..1320, from `DUNE 2000` to
`SHAI-HULUD!`. 170 of the 350 values in 972..1321 are empty (spacing rows).

`credits.uil` adds only the background `credits` and the four `inf1..4`
animations. The text is not a UI element.

## 3. CreateCredits (`FUN_0043d2a0`, verified)

1. `FUN_004706c0(0)`: input off (no clicks, cursor hidden).
2. `start` = index of `CreditsStart`, `end` = index of `CreditsEnd`
   (`FUN_0048dc10`: first case-insensitive key match, 0 when missing).
3. `lead` = (350 − 50) / 15 + 1 = **21** blank rows (unsigned division).
4. `count` = lead + (end − start) + 1 = 21 + 350 = **371** rows.
5. Rows 0..20 get a copy of "" (a non-null empty string).
6. Rows 21.. get entries start..end, both markers included. An entry whose
   value is empty keeps a **null** text pointer (the row is skipped when
   drawing, but it still takes its 15 px).
7. offset = 0; length = 15 × count = **5,565 px**; last step time =
   `GetTickCount()`.

CreateCredits runs whenever the menu is created, so each opening starts
from the top.

## 4. Draw and scroll (`FUN_0043d520`, verified)

### 4.1 When it runs

The menu draw `FUN_0043c4b0` runs for every menu in the stack, every UI
update (doc 05). For a menu with `+0x34` that is **the top menu** while
**no transition runs** (`mgr+0x11d` = 0), it:

- calls `FUN_004706c0(0)` (input off) and `FUN_0043ca50` (redraw every
  element) at the start, **every update**;
- draws the background and the four animations as usual;
- calls CreditsDraw **after all the elements** (only the tooltip pass comes
  later), so the text is on top of the animations.

So nothing scrolls while SCORE_SINGLE covers the credits, nor during the
FADE in. The fade shows the background and animations **without text**
(verified): the menu loader sets `mgr+0x11d` = 1 (`0x436443`) when it
creates the menu, before any draw of it, and only the transition code
clears it when the transition ends (`0x472f03`, `0x4730d5`; the other
writers at `0x4728xx`..`0x472d90` store the transition state). Every draw
in between, the target image included, skips CreditsDraw.

### 4.2 Drawing (offset < length)

All integers. `o` = offset.

- first row = o / 15; last row = min(count − 1, 300 / 15 + first) =
  min(count − 1, first + 20).
- Row `first` is at y = 50 − (o mod 15); each next row 15 px lower.
- A row is drawn only if its text is non-null **and 50 < y < 350**
  (strict).
- Text: font 0 (`font`, FONT.FNT), colour `White` (colours.uib), centred in
  the box: x = 0 + ((640 − width) >> 1), width = `FUN_0042dfb0` (glyph
  width + spacing, space width for a space). y is the glyph top.
- **Top fade** (y < 80): d = |y − 50|, capped at 30.
- **Bottom fade** (y > 320): d = |350 − y|, capped at 30.
- Faded rows: alpha a = (d × 32) / 30 (signed division), 1..30 inside the
  bands. They are drawn by `FUN_0042e110` → `FUN_0042de70` →
  `FUN_00432060` with that alpha. (Ghidra shows `a` as a second argument of
  `FUN_0048db90`; the disassembly at `0x43d67a..0x43d6b6` shows it is the
  8th argument of `FUN_0042e110`.)
- Rows with 80 ≤ y ≤ 320: solid, `FUN_0042e050` → `FUN_0042dc00` (colour
  mode, param −1).

**Translucent glyph blit (`FUN_00432060`, 16-bit).** For every nonzero
glyph pixel, per colour field (R, G, B masks of the screen format: 555, or
565 when `0x798384` = 0xFBCE):

```
out = ((dst & m) * (32 - a) + (col & m) * a) >> 5   & m
```

`col` is the colour converted to the screen format (`FUN_0042f3d0`). There
is **no clipping** in this blit (the solid path skips glyphs that do not
fit). With y in 51..349 and the 14 px font this stays on the 640×400
screen.

### 4.3 The scroll step

After drawing, in the same call:

```
if |last_step - GetTickCount()| > 10:   last_step = now; offset += 1
if offset >= length:                    end_time = now
```

One step per update at most, and no catching up. At our 60 Hz loop every
update is more than 10 ms apart, so the credits move **1 px per update =
60 px/s**. (The original moved one pixel per refresh, capped near 90 px/s
by the 11 ms gate.)

### 4.4 The end: loop (verified)

When offset ≥ length, CreditsDraw draws nothing and only checks:
`|end_time − now| > 2000` → offset = 0. So the credits **repeat forever**
after a 2-second blank pause. They never close the menu by themselves.

Timeline at 60 px/s: the first text (`DUNE 2000`, row 22) appears after
31 px (≈0.5 s) at the bottom edge; `SHAI-HULUD!` (row 367) leaves the top
at offset 5,505; the end is reached at 5,565 (≈92.8 s); then 2 s of
blank, then the next run starts empty for another ≈0.5 s.

## 5. Input and leaving (verified unless marked)

- **Esc** (`FUN_0043cb70`, char 0x1B, ignored while `0x5178b8` is set;
  that flag is 1 only during a menu's own movie, see §10) and
  **Tab** (`FUN_0043cdf0`, VK 9): on a `+0x34` menu, queue
  **RemoveMenu** (0xE2). The key handlers do not ask the input switch.
  The Esc handler then looks for a button with BackMenu; CREDITS has none.
- **RemoveMenu** (`FUN_004738b0` case 7 → `FUN_00473300`): with gGameState
  = 1 it closes the top menu (CREDITS), so **MAINMENU** becomes the top:
  `options.aud` restarts and the eye/logo timers reset. The leaving
  transition is CREDITS' own: menus.uib gives CREDITS **enter FADE (0),
  leave FADE (1)**.
- **Enter**: runs focused edit boxes and default buttons; CREDITS has
  neither, so nothing happens. Other characters (Space, letters) only reach
  edit boxes: nothing.
- **Mouse click**: the mouse-down handler has a CREDITS branch that calls
  the Esc handler, but the whole handler is skipped while input is off,
  and §4.1 switches input off every update. So **a click does nothing**
  and **the cursor is hidden** while the credits show. Verified for the
  cursor too: the front-end loop draws it with `FUN_00423a20(screen,
  FUN_004706f0())` (`0x44274a`), i.e. only while input is on; the
  readers of the input switch are that call, the three mouse handlers
  (`0x43761e`, `0x43785e`, `0x437b05`) and `0x492abc`.

## 6. Music (verified from the code)

When CREDITS becomes the top menu, either by opening it from the main menu
or by closing SCORE_SINGLE above it, the stack code (`FUN_004734e0` /
`FUN_00473130`) sees a name that is neither MAINMENU nor SCORE* and, with
gGameState = 1, stops the menu music (`FUN_00470170`). **The credits play
in silence**; `options.aud` starts again when MAINMENU is uncovered.
Nothing on the credits path starts a track: CreateCredits, CreditsDraw
and the CREDITS script (`credits.uil`: the background and four
animations) have no music call.

## 7. Cleanup (`FUN_0043d4c0`, verified)

Called from the menu destroy `FUN_00437040` for a menu with `+0x34` (any
close: RemoveMenu, close-all, BackTo …). Frees every row text, every row
object and the array, then `FUN_004706c0(1)`: input on, with the usual
10-check delay (doc 04). The destroy itself then switches input off again
for its leaving transition, which switches it on when it ends.

## 8. Where CREDITS is opened

- `QuitAndJumpToMenu SCORE` after campaign mission 9: MAINMENU, CREDITS,
  SCORE_SINGLE (docs 04, 24). Closing the score reveals the credits.
- `MainMenu.uil`: button `credits` at (12, 364), 148×31, image `wwlogo`,
  no text, action `JumpToMenu CREDITS`. Not removed by the main-menu hooks,
  so it is live in retail. No other script opens CREDITS.

## 9. Implementation checklist (our code)

Existing pieces:
- `src/app/menu_runner.cpp:142` `open_menu` (has the GLOBE and SCORE_SINGLE
  special cases, line 248); `:258` `close_top`; `:274` `top_changed`
  (already stops the music for CREDITS and restarts `options.aud` for
  MAINMENU); `:362` RemoveMenu (gGameState = 1 check already there);
  `:343` the mission-9 CREDITS path; `:1810` `handle_keys`; `:985..1016` the
  per-update block where the score counts (put the credits next to it).
- `:123` `set_input` / `:129` `input_allowed` (the 10-check delay, and the
  cursor at `:820` already hides while input is off); `:667` `invalidate`
  (FUN_0043ca50); `:725` `draw_all_dirty`.
- `src/ui/assets.hpp:36` `small_font()` = FONT.FNT = font 0;
  `assets.hpp:47` `colour("White")`; `assets.hpp:29` `text().entries()`
  (ordered list, duplicates kept) for the index range.
- `src/ui/surface.cpp:89` `text_width` (= `FUN_0042dfb0`); `:100`
  `draw_text_solid` (= `FUN_0042dc00` colour mode); `surface.hpp:16`
  `to_rgb555`.

To add:
1. `src/ui/credits.{hpp,cpp}` (pure, testable): build rows from a
   StringTable (21 × "" then entries start..end, empty values → no text;
   start/end by first case-insensitive key match, 0 when missing), the
   constants of §1, and a `step(now_ms)` / `visible_rows()` pair that
   reproduce §4.2–4.4 exactly (row range, y, strict 50 < y < 350, fade
   distance, `a = d*32/30`, 10 ms gate, 2000 ms end pause, wrap to 0).
2. `Surface::draw_text_alpha(font, x, y, text, colour, a)` in
   `src/ui/surface.{hpp,cpp}`: the 5-bit per-field blend of §4.2
   (`>> 5`, weights `32 − a` and `a`), no fit test (keep a bounds guard
   only to avoid writing outside the buffer). Do not reuse
   `Surface::blend` (it divides by 255).
3. `menu_runner`: in `open_menu`, for `CREDITS` build the credits state and
   `set_input(false)`; in the per-update block, when the top is CREDITS and
   no transition owns the screen: `set_input(false)`, `invalidate(top)`,
   draw as usual, then draw the rows and step the scroll (draw first, then
   step, as in `FUN_0043d520`).
4. `handle_keys`: Esc or Tab with CREDITS on top → queue RemoveMenu (only
   if no navigation is pending). Leave clicks alone: the exe's click branch
   can never fire (§5).
5. When CREDITS leaves the stack (`close_top`): drop the state and
   `set_input(true)` before the leaving transition switches it off again.
6. Tests in `tests/ui/`: row count 371 and length 5,565 from the real
   text.uib (skip without data); y/alpha for a few offsets (e.g. o = 0 →
   nothing visible; o = 31 → `DUNE 2000` at y 349, a = 1); the wrap after
   2,000 ms; the blend formula on known pixels.
7. `docs/parity.md`: add a CREDITS row.

## 10. Closed points (2026-10-16)

- No music, hidden cursor and dead clicks: verified in the code (§5,
  §6). Esc / Tab return to the main menu with CREDITS' leave FADE
  (menus.uib, §5); the 2 s pause is the `|end_time − now| > 2000` test
  (§4.4). All of these are code or data, not timing of the reference.
- **`0x5178b8`** is "a menu movie is playing": its only writer,
  `FUN_0048d8f0` (called from the menu draw `FUN_0043c4b0` at `0x43c814`),
  sets it to 1 right before the movie player `FUN_00403460(menu + 0x77c)`
  and back to 0 after it, only for a menu with a movie (`menu+0x854`)
  and with `0x4eb008` and `0x4eb004` set. The movie
  player pumps window messages, so Esc typed during the movie is not
  taken as the menu's Esc. CREDITS has no movie, so for it the flag is
  always 0.
- **Glyph origin offsets** (`glyph+8` / `+0xC`, subtracted from x / y by
  `FUN_0042dc00` and the translucent path): always 0. Font glyphs are
  created by `FUN_0042ef90` (from both font paths of the loader
  `FUN_0042d6f0`), which writes 0 to +8 and +0xC; nothing else writes
  them. Our font code is right to ignore them. (So the "+2 px" in
  `src/ui/render.cpp` does not come from a glyph origin.)
