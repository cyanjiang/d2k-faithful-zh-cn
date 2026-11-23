# UI script loading and per-menu code (1.06)

## Loader

`FUN_004390e0(menu_file)` opens `UI_data\<file>` and reads the line count
(u32), then one counted line at a time (`FUN_004825b0`). A `.uid`
plain-text path (`FUN_0048d980`) exists for development builds
(`DAT_004e8bdc`).

Each line is compared with `strncmp(line, KEYWORD, strlen(KEYWORD))`,
case-sensitively, in this order:

BACKGROUND, TOGGLEBUTTON, BUTTON, GROUPBOX, LISTBOX_MULTI, LISTBOXEMPTYSLOT,
LISTBOX_CROPPED, LISTBOX, CHECKBOX, MULTISTATEBOX, EDITBOXFOCUS, EDITBOX,
TEXTBOX_L_UL, TEXTBOX_R_UL, TEXTBOX_UL, TEXTBOX_L, TEXTBOX_R, TEXTBOX,
VALUEBAR_NOVAL, VALUEBAR, ANIMATION, MOVIE

- **Unmatched lines are skipped.** This is why the "headerless" blocks in the
  data (bodies whose header line was stripped) have no effect.
- `TEXTBOX_C` is not a keyword. It matches `TEXTBOX`, whose handler call
  passes alignment 2. TEXTBOX_L passes 0, TEXTBOX_R passes 1. `_UL` sets an
  underline flag.
- The element parsers read a fixed number of lines, then pass every line up
  to their `END_…` to `FUN_0043c270` (commands/actions). The fixed counts:
  BUTTON 11, TOGGLEBUTTON 12, GROUPBOX 5, LISTBOX 16, CHECKBOX 7,
  MULTISTATEBOX 6, EDITBOX 5, TEXTBOX 7, VALUEBAR 11, ANIMATION 3, MOVIE 2.
  Every 1.06 block has at least that many lines, so no `END_` line is ever
  read as a field. Examples:
  `FUN_00439670` (BUTTON) and `FUN_0043a000` (GROUPBOX). The header
  contributes only its 2nd token (the name). The trailing number in
  `BUTTON name 0` is not read.

BUTTON field order (from the parser): pos, size, text, highlight, lowlight,
up colour, down colour, font-up colour, font-down colour, up gfx, down gfx,
then actions.

## Per-menu code: `FUN_004359a0`

After a menu loads, this function modifies some screens by name. MAINMENU:
- `startNewGame` is deleted (`FUN_00438e00` = remove element) unless
  `DAT_007984d0` is set. Nothing writes that flag, so the button is
  developer-only.
- If `DAT_00516204` is set, `startMultiplayer` is deleted and a new
  button `practice` is created at its rect: text "Practice", colours
  LightRed/VDarkRed/Red/DarkRed, ButtonUp/DownColour, gfx but4_up/but4_dn,
  and command 0xB0 with `gNetNetworkType` = "0". `DAT_00516204` = 1 when
  `FUN_00492000()` reports no usable network (set in `FUN_00470710`).
  Until we support multiplayer, we use this original fallback.

TODO: port the hooks for other menus as we implement them (the function is
large and handles many screens).

## Animations (CAnimation)

- Constructor `FUN_0048cec0`: the ANIMATION body line `gfx N` gives the
  film strip and **N = frames per second**; the period is `1000 / N` ms,
  measured with `GetTickCount`. The frame count is strip width / element
  width. The mode field (+0x77c) starts at **0 = stopped**.
- Modes (`FUN_0048d2b0`): 0 stopped, 1 loop forward, 2 loop backward,
  3 ping-pong, 4 forward once, 5 backward once, 6 rewinds to frame 0 and
  then stays (`AnimLoopToggleOnce`), 7–11 step one frame and stop
  (`AnimStepForwardCycle`, `…BackwardCycle`, `…ToggleCycle`,
  `AnimStepForwardToEnd`, `AnimStepBackwardToStart`; the "ToEnd/ToStart"
  ones do nothing at the end). The values are the enum constants at
  `0x4ca820`. No 1.06 script names modes 3–11 (the scripts use
  `AnimLoopForwardCycle` 5 times and `AnimLoopBackwardCycle` once) and the
  code starts only 1 and 4, so we implement 0–5.
- Update `FUN_0048d2b0`: when a full period has elapsed, step one frame and
  restart the period. The "once" modes stop on the end frame; the parser
  always passes hold = 1, so that frame stays visible.
- Set mode `FUN_0048d1f0` / `FUN_0048d240`: only acts if the mode changes.
  Modes 1/3/4/6 rewind to frame 0, modes 2/5 jump to the last frame. The
  frame timer is not reset.
- Scripts start an animation with `GetValueECInit Anim...` in its body
  (`credits.uil`, `score*.uil`). MAINMENU has none: the UI manager
  `FUN_004712c0` starts them from code. It plays `l_eye` and `r_eye` once
  (mode 4) every **300,000 ms (5 minutes)**, and `duneLogo` once every
  **20,000 ms**. The timers (`0x797580` / `0x797584`) reset when the menu
  opens (`FUN_00473130`, `FUN_004734e0`, `FUN_00473300`). So on the main
  menu the eyes only move after 5 minutes of idling. That is original
  behaviour, not a bug.

## Checkboxes (CCheckBox, ctor `FUN_0048ab10`, draw `FUN_0048adc0`)

**Body:** pos, size, border colour, on colour, off colour, checked gfx,
unchecked gfx, then commands. The parser `FUN_0043a900` reads these seven
lines in this order and passes them on in the same order (`FUN_00438180`,
AddCheckBox).

**State:** `+0x794`, checked or not. The text key `+0x798` defaults to
`NoText` (`0x4e8c30`) and is set by `EnterText`.

**Drawing:**
- With an image for the current state, the image is drawn opaque.
- Otherwise, checked draws an on-colour fill with a border-colour frame;
  unchecked draws an off-colour fill and frame.
- The text is centred, in font 0. Its colour is the border colour when
  unchecked and the on colour when checked.

**Click** (`FUN_00482850`, click case):
- A checkbox with an `UnCheck` command (a radio group) becomes checked.
- Any other checkbox toggles.
- The click commands then run in order. A `CheckIfValue*` sets checked to
  (global == value), and a mismatch **stops** the remaining commands.
  `UnCheck X` clears X (`FUN_00474250`).

**Init commands:** commands flagged init in the command table
(`EnterText`, `Check`, `*Init`, ...) run once at creation (`FUN_00484000`)
and are never stored as click commands.

## Text boxes: line splitting (`FUN_004384a0`, AddTextBox)

If the text is at least as wide as the box, AddTextBox wraps it greedily.
- Words are added while the line, including its trailing space, stays
  narrower than the box.
- `0xAC` (`¬`, used 439 times in text.uib) forces a line break.
- Leading and trailing spaces and `¬` are trimmed from each line.
- The first line replaces the box's text. Each further line becomes a new
  TEXTBOX at `y + k·height`, with the same x and size, and grows the menu's
  bounding box.

## Answered (2026-10-16)

- `menus.uib` per-menu values (a, b): docs/formats/ui.md.
- Element draw code (bevels, text placement, colour keys): docs/formats/ui.md,
  docs/formats/fonts.md and docs/re/55; button sounds and menu music:
  docs/re/04, 15 and 48.
- Command semantics (JumpToMenu, QuitAndJumpToMenu, BackMenu and the menu
  stack): docs/re/04 and 20.
