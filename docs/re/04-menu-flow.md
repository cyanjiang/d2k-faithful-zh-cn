# Menu flow: stack, navigation, transitions, music (1.06)

Sources are 1.06 addresses. The UI manager object (`CUIManager`) is created in
`FUN_0044a870` → constructor `FUN_00470710`, pointer `DAT_0079759c`.
Tables can be regenerated with `re/dump_ui_tables.py`.

## UI script tables in the exe

- **Commands** (`0x4beb88`): 229 records of 0xBC bytes,
  `{u32 id, char name[], ..., u8 init}`. `FUN_0043c270` turns an action line
  into `{id, arg1, arg2, arg3}`. Records with `init = 1` (`GetValueECInit`,
  `RedrawEachTime`, ...) run when the element is created rather than on
  click. Navigation IDs: 0xDB QuitMenu, 0xDC BackMenuAndJumpTo, 0xDD BackMenu,
  0xDE GoBackMenu, 0xDF GoBackMenuAndJumpTo, 0xE0 JumpToMenu,
  0xE1 QuitAndJumpToMenu, 0xE2 RemoveMenu, 0xE3 BackTo, 0xE4 BackToAndJumpTo.
- **Enum constants** (`0x4ca820`, 0x40-byte records `{name[60], u32}`):
  `GameStateIntro..Quit` = 0..5, `Anim*` = 0..11,
  `GameTypeCampaign, Skirmish, Network, Serial, Modem, Internet` = 0..5.
- Globals used below: `gGameState` = `DAT_004dfb08`, `gGameType` =
  `DAT_00797e34`.

## Screen size

The screen size globals `0x4eb028` / `0x4eb02c` are 640 × 400 and are never
written. Menus and movies fill the whole 640×400 screen, so there is no
640×480 frame.

## Menu stack

The manager holds 20 menu slots, and `+0x51` is the index of the top menu.

- **Open** (`FUN_004734e0`): takes the first free slot. The new menu becomes
  the top.
- **Close** (`FUN_00473130`): destroys the slot. The menu below becomes the
  top again.
- **Close all** (`FUN_004730f0`): closes all 20 slots.
- **Drawing:** every menu in the stack is drawn, bottom to top (see
  `FUN_00472730`). A screen like DIFFICULTY is therefore a pop-up over the
  menu beneath it. Input goes to the top menu only.

Whenever MAINMENU becomes the top menu, whether opened or uncovered by a
close, the manager does two things:
- restarts `options.aud` (`FUN_004700e0`);
- resets the eye and logo animation timers (`0x797580` / `0x797584`).

A SCORE* menu becoming the top starts `score.aud` instead.

## Navigation commands (`FUN_00473840` → `FUN_004738b0`)

A navigation command is not executed straight away. `FUN_00473840` stores
`{id, arg1, arg2}` at `mgr+0x54/0x58/0x94`, where 0xE5 means none.
`FUN_004738b0` executes it later in the frame, after the click handling, and
then starts a transition (`FUN_00472730`). The behaviour of each command:

| Command | Effect |
|---|---|
| QuitMenu | close all |
| BackMenu | close top |
| BackMenuAndJumpTo X | close top, open X (special cases for LOBBY_SCREEN, multiplayer only) |
| GoBackMenu N | close top N times (`atoi`) |
| GoBackMenuAndJumpTo N X | close top N times, open X |
| JumpToMenu X | open X. Special cases: an empty X does nothing; if ERROR_MESSAGE is already open it is not opened again; `LOAD` with no saved games opens `NO_SAVED_GAMES` instead (`FUN_00492750`); CONFIRM_ABORT_MP becomes ABORT_CONNECTION for serial/modem |
| QuitAndJumpToMenu X | close all, then: `HOUSESELECTION` → open MAINMENU, HOUSESELECTION. `SCORE` → open MAINMENU; in a campaign also open (mission < 10 ? (gUseCampaignSelection ? GLOBE : LOADMISSION) : CREDITS) and then SCORE_SINGLE (mission = `DAT_004de610`, flag = `DAT_004e7ad0`). `LOADMISSION` → open MAINMENU, then for a campaign mission < 10 GLOBE or LOADMISSION. `ABORT_MP_GAME` → MAINMENU plus the per-game-type lobby. Otherwise → open X |
| RemoveMenu | close top (only when gGameState = 1) |
| BackTo X | close top until X is the top |
| BackToAndJumpTo X Y | close top until X is the top, then open Y |

A target starting with `MVE_` calls `FUN_004706c0(0)`: mouse input off,
which also hides the cursor (see "Mouse input switch" below). Every menu
open and close switches it off as well; the next UI update's transition
step switches it on again.

## Transitions

`menus.uib` stores two values per menu, parsed by `FUN_00477190` from the
text `menus.uii`:
- **a = entering transition:** `FADE` → 0, `TWEEN` → 2, `SKIP` → 3.
- **b = leaving transition:** `FADE` → 1, `TWEEN` → 2, `SKIP` → 3.

**Selection.**
- Opening a menu (end of `FUN_004359a0`) sets the mode to the menu's **a**.
  The rect is the full 640×400, and `+0x136 = 1`.
- Closing a menu (`FUN_00437040`) sets the mode to the menu's **b**. The
  rect is 640×400 for FADE, otherwise the menu's bounding box, and
  `+0x136 = 0`.
- The last open or close in a command wins. JumpToMenu therefore uses the
  new menu's a, and BackMenu uses the closed menu's b.
- The bounding box starts as (640,400,0,0) and grows with each element's
  rect (e.g. `FUN_00437ce0`, AddButton).

**Start** (`FUN_00472730`). Nothing starts when `gGameState` (`0x4dfb08`)
is 2 (in a mission), the screen depth `0x4eb018` is 8, or a snapshot
surface (`+0x114` / `+0x118`) is missing: then `+0x11d` = 0, input on
(`FUN_004706c0(1)`), `+0x134` / `+0x135` = 0 (`0x47273e`). So **no menu
transition ever runs in a mission** (RECAP's FADE, LOAD / SAVE's TWEEN;
capture of the original 2026-10-16: closing RECAP shows INGAME over
RECAP's remains in the next frame, no darkening; docs/re/20 section 2.5.
Ours captured the same way the same day: the same two frames, INGAME over
the remains then the game view, differing from the original's only in
the mouse pointer and the bottom pixel rows).
Skipped for mode 3 (SKIP). Otherwise:
- copy the current screen into `old` (`+0x114`);
- build `new` (`+0x118`) by copying `DAT_005178b4` and drawing all stacked
  menus into it;
- reset step `+0x11c` and flags `+0x134` / `+0x135`, and mark the transition
  active (`+0x11d`).

**Step** (`FUN_00472870`, the first call in UI update `FUN_004712c0`). While
a transition runs, the UI update returns early: no input, no redraw, no
animations. The step only runs in 16-bit mode and with `gGameState` != 2;
otherwise, and whenever no transition is pending, it clears `+0x11d`,
switches input on and returns 0 (`LAB_00472b26`). Each mode works as follows.

**TWEEN (2).** Not time-gated: one step per UI update.
- With step `s` = 0, 4, …, 28:
  `out = (new·s + old·(32−s)) >> 5`, computed per masked channel field
  (`FUN_00472b50`).
- After that the step is clamped at 32. The next update copies `new` and
  ends.
- That makes 8 blended frames, then the final one.

**FADE (0 / 1).** Gated by `GetTickCount() − last ≥ 12 ms` (`+0x138` = 12,
`+0x13c` = last).
- On the first step of an opening transition (`+0x136` set, both flags
  clear), the mode is flipped from 0 to 1. A FADE therefore always fades
  out first, then fades in.
- **Fade out (mode 1, `FUN_00472db0`):** 10 steps (s = 0..9). Each step
  halves every channel (`(p >> 1) & mask`) of half the pixels **in place on
  the screen**, in a checkerboard. The parity starts at `(s & 1) == 0`,
  flips at each row start, and flips again before each pixel.
- **Fade in (mode 0, `FUN_00472f20`):** 10 steps. Each step writes the
  checkerboard half with `new >> ((10 − s) / 2)` per channel field, i.e.
  shifts 5, 4, 4, 3, 3, 2, 2, 1, 1, 0. Then `new` is copied and the
  transition ends.

**SKIP (3).** No transition.

## Mouse input switch

`FUN_004706c0(on)` stores the flag (`0x4e7ad4`). It acts only when the
value changes, and switching on also sets a 10-count delay (`0x797594`).
`FUN_004706f0` answers "input on?":
- while the delay is non-zero it decrements it and answers no;
- otherwise it returns the flag.

It is asked by:
- the mouse-down handler (`FUN_00437840`), the mouse-up handler
  (`FUN_00437600`) and the right-release handler (`FUN_00437af0`, via
  `FUN_00471250`). Each first returns if a transition is pending (manager
  `+0x11d`), then runs only if `FUN_004706f0() != 0 || gGameState
  (0x4dfb08) == 2`. So in a game the switch never blocks the menus' mouse
  (the query still counts the delay down);
- the per-loop cursor draw (`FUN_00492a70`, front end only), which hides
  the cursor.

In a game nothing else asks; no transition runs there either (Start
above), so the in-game menus' input is never switched off. The original
closes RECAP on the first click of Done.

Input goes **off**:
- whenever a menu opens (end of `FUN_004359a0`: `+0x11d` = 1, then
  `FUN_004706c0(0)` at `0x43644a`) or closes (end of `FUN_00437040`:
  `+0x11d` = 1, `FUN_004706c0(0)`, after the menu's destroy, so also after
  CREDITS' `FUN_0043d4c0` switched it on). SKIP included;
- at the start of a non-SKIP transition (`FUN_00472730`, never in a
  mission);
- when navigation targets an `MVE_` menu (`FUN_004738b0`);
- every update while the CREDITS menu is the top (`FUN_0043c4b0`, menu
  `+0x34`), and every update while the globe reveals territories
  (`FUN_0042c9b0`).

It goes **on** in **every UI update whose transition step is not
running**: `FUN_00472870` is called unconditionally at the start of the UI
update `FUN_004712c0` (`0x4712ee`). With nothing pending (`+0x11d` = 0), a
SKIP (mode 3: neither 0, 1 nor 2), an 8-bit screen or `gGameState` 2 it
goes to `LAB_00472b26` (`+0x11c` = 0, `+0x11d` = 0, `FUN_004706c0(1)`,
return 0); every finish path calls `FUN_004706c0(1)` too. Only a FADE step
that is not yet due or still stepping, or a TWEEN step, returns 1 and leaves
input off. Because `FUN_004706c0` acts only on a change, an update that
finds input already on does nothing; one that finds it off restarts the
10-query delay. Also on: after a movie element plays (`FUN_0048d8f0`), when
CREDITS is destroyed (`FUN_0043d4c0`) and when the globe reveal ends.

**Order within one loop pass** (the front end's loop is
`while (gGameState == 1) FUN_00492a70(1)`, `0x44276d`):
1. `FUN_004a3980`: the message pump; the window procedure runs the mouse
   handlers, which return at once while `+0x11d` is set (no query), else
   query the switch;
2. `FUN_004712c0`: the transition step (input on, see above), then the
   update's navigation (`FUN_004738b0` at `0x47259f` / `0x472647` /
   `0x4726a4`), then the menu draw (`FUN_0043c4b0` at `0x4726ee`), where a
   movie element plays (`FUN_0048d8f0`, only with `+0x11d` = 0); the
   movie's commands only queue navigation, which runs in the next update;
3. unless a movie just played (`0x797e28`, cleared there), the cursor
   draw queries the switch (`FUN_004706f0` → `FUN_00423a20`): one query per
   pass.

**Worked example** (HOUSESELECTION → Atreides → house movie →
PLAY_ATREIDES; `menus.uib`: MVE_ATREIDES SKIP / SKIP, PLAY_ATREIDES SKIP /
FADE). Pass M plays the movie (the MVE page's open switched input off; the
step of pass M switches it on). Pass M+1 runs BackMenuAndJumpTo: the close
and the open switch input off; the cursor query answers off. Pass M+2: the
handlers return (`+0x11d`); the SKIP step switches input on (delay 10); the
cursor queries of M+2..M+11 answer off and count the delay to 0. The first
click that works is in pass M+12 (if no click consumed a query before).
At 60 passes per second that is 0.2 s after the movie.
`tests/ui/test_menu_input.cpp` checks M+11 refused, M+12 accepted.

Ours before 2026-10-16 switched input on only when a pending transition
finished, did not switch it off on a SKIP open / close, asked the switch
after the step instead of before it, and ran a movie's navigation in the
movie's own pass. On this path that accepted the click one pass early
(M+11); passes cost about 3 ms headless, so this path never refused a
click for seconds. A ~4 s refusal seen live on PLAY_ATREIDES is not
explained by the input switch (old or new); `d2k.log` of such a run
(`open` / `movie` / `slow frame` lines) would be needed to find it.

Clicks on buttons act on mouse-up. The globe's territory click is in the
**mouse-down** handler.

## Menu music

- `FUN_0046fd90` opens `MUSIC\<name>.aud` and streams it with Miles at
  22,050 Hz, 16-bit mono.
- Initial volume 0x40, then `FUN_004700b0(100)` sets it to 100 (of 127)
  while not in the main game loop.
- Three 0x8000-byte buffers are refilled by `FUN_0046fcc0`. It zero-fills
  the buffer, then appends whole chunks until the buffer is exactly full or
  the file ends.
- When a refill reads nothing, the stream seeks back to offset 12 and refills
  (`FUN_0046ff30`). The music loops forever.
- **Two consequences:**
  - The tail of the last buffer stays silent, about 0.64 s for
    OPTIONS.AUD.
  - The ADPCM state is not reset at the loop point. It is reset only on
    open (`FUN_00404860`).
- When another menu becomes the top while `gGameState = 1`, the music stops
  (`FUN_00470170` → `FUN_00470070`). The whole front end runs with
  `gGameState = 1` (the boot loop is `while (gGameState == 1)`). So opening
  any pop-up over MAINMENU stops the music, and returning restarts it.
  Confirmed in the reference game on 2026-10-03.

## Answered (2026-10-16)

- `DAT_005178b4` is the screen surface the menus and the game draw into;
  a transition's new image starts from a copy of it (docs/re/55); in a
  mission no transition starts (`gGameState` 2, Start above).
- That surface persists between frames: it is the system-memory back
  surface blitted to the screen each pass, never flipped away.
- Miles volume curve: read in `MSS32.DLL`, DirectSound attenuation
  -2000 * log10(127 / v), the same as linear amplitude v / 127 to within
  1/100 dB (docs/re/59 section 7).
- Loop rate: decided at 60 Hz (CLAUDE.md section 10); TWEEN and FADE step
  per pass as in the exe.
