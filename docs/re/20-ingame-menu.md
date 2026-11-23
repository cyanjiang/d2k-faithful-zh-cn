# In-game menu: OPTIONS, pause, abort, restart, settings (1.06)

Single-player campaign first; skirmish and multiplayer differences are
noted where they matter. Addresses are 1.06. Related: doc 04 (menu stack,
navigation, transitions), doc 03 (UIL loader), doc 11 §3 (OPTIONS
control), doc 19 §4–5 (end of mission, abort and lose menus), doc 02
(tick pacing).

## 1. Opening and closing

### 1.1 What opens INGAME

Three paths, all guarded by "the mission has not ended" (`0x517400`, the
end-voice guard of doc 19 §4.4):

1. **OPTIONS button** (top bar control 11, doc 11 §3): in `FUN_004433a0`
   (`0x444bfc`), on left-button-up (`0x798528`) while the control is
   pressed (state 2): `FUN_00473840(0xe5, "")` (clear any queued
   navigation: 0xE5 = "none", doc 04), then `FUN_00470e60(menu,
   gBackBuf)`. After the mission ended it opens nothing (MP: `ABORT_VM`).
2. **Keyboard**, in the game loop `FUN_00448550` ("Popup UI checks",
   `0x4496f6`..`0x44981e`). `0x798390[256]` is the key-down array indexed
   by virtual-key code, written by the window procedure (`0x4a33aa`).
   - `key[VK_ESCAPE]` (`0x7983ab`) or `key[0x20]` (`0x7983b0`, VK_SPACE).
     Space does open the menu: the window procedure writes `key[wParam] =
     1` for every WM_KEYDOWN (`0x4a33aa`, wParam = the virtual-key code,
     VK_SPACE = 0x20), unless the UI manager's word +0 is set and the key
     is not Esc / Enter (`0x4a333d`..`0x4a334a`); verified 2026-10-16:
     - no menu open (`mgr+0x110` = 0) and the mission not ended: queue
       0xE5, open INGAME / INGAME_SKIRMISH / INGAME_MP (by `gGameType`
       `0x797e34` = 1 → skirmish, network flag `0x7984c4` → MP);
     - else, if Esc is set: `FUN_00473840(0xdb)` = **QuitMenu** (close
       all). This only happens when the menu's own key handler did not
       consume Esc (see 1.3), e.g. a chat line (GAMEMESSAGE / PAGE_INGAME).
   - Then `key[Esc] = 0`, and `key[0x20] = 0` unless the mission ended.
3. MP only: `OUT_OF_SYNC`, `ABORT_VM`, timeouts (not covered).

`FUN_00470e60(name, surface)` = close all menus (`FUN_004730f0`), open the
menu (`FUN_004734e0`), set the **menu-open flag** (`FUN_00470f90(1)`,
`mgr+0x110`), start the opening transition with the game back buffer
`0x5178b4` as the base (`FUN_00472730`). All in-game pages are SKIP/SKIP
in `menus.uib` (table below), so nothing fades.

### 1.2 Closing

- **Resume** button: `BackMenu`.
- **Click outside the box:** the UI mouse handlers check whether the top
  menu is INGAME, INGAME_MP, INGAME_SKIRMISH or ABORT_VM. Mouse-down
  outside the top menu's bounding box (`menu+0x40..0x4c`, grown by every
  element, doc 04; `PtInRect`, so right / bottom exclusive) sets
  `0x797590` (`FUN_004711a0`, after the menu's own down handler). A
  mouse-up outside the box with that flag set queues **QuitMenu**
  (`FUN_004710d0`, after the menu's own up handler). Neither handler ever
  clears the flag: a press inside leaves it, and the release does not
  reset it. It is cleared only by (verified 2026-10-16, all writers of
  `0x797590`):
  - `FUN_00471250`, the **right**-button-up handler (window procedure
    `0x4a373a`), called on every right release whether a menu is open or
    not, before any check of the top menu;
  - `FUN_004433a0` `0x444832`: a left press on the OPTIONS control
    (control 11, after the guard / retreat tests);
  - `FUN_004433a0` `0x444c97`: the OPTIONS release (whether or not it
    opened a menu).
  `FUN_00470e60` (open) does not clear it. So after a press outside and
  a release inside, the flag stays set, and a later left release outside
  INGAME closes the menus even when its press was inside; the same holds
  for INGAME opened again with Esc / Space.
  Sub-pages (OPTIONS, CONFIRMABORT, …) do not close on outside clicks.
- **Esc inside any menu** (`FUN_0043cb70`, the menu key handler; Esc is
  ignored while `0x5178b8` is set, which is only during a MOVIE element's
blocking playback, `FUN_0048d8f0`): it clears `key[Esc]` unless
  GAMEMESSAGE / PAGE_INGAME is the top menu. Then:
  - if the menu has a focused edit box (`menu+0x2c` set and `0x5139f0`
    clear) the edit box takes it (not relevant here);
  - otherwise the **first button (script order) with a `BackMenu`
    command** runs all its commands (`FUN_00482850(0)`, plus the pressed
    look `FUN_00485550(0)`). INGAME → Resume; OPTIONS → Done
    (ParseEndCommands + BackMenu); CONFIRMABORT → Cancel ("no");
    REALLY_RESTART → Cancel. A page without such a button ignores Esc.
- **Enter** (`0x0d`) runs the focused edit box and any button flagged as
  default (`+0x7ac`). None of the single-player in-game pages has one.

Every menu close (`FUN_00473130`) recomputes the flag:
`FUN_00470f90(0)`, then `FUN_00470f90(1)` if any menu is still in the
stack. Opening GAMEMESSAGE or PAGE_INGAME on top resets it to 0 (chat does
not pause). The close also sets `0x5178ac = 1` and `0x5178b0 = 1`: the
game view is redrawn for the next two loop passes even if a menu is still
open (the loop decrements `0x5178b0`, then clears `0x5178ac`), so a closed
sub-page is painted over by the (frozen) game view before the remaining
menus are drawn again. `FUN_00473300` (open into a slot) sets the same
two flags.

### 1.3 Pause semantics (single player)

The loop copies the flag each pass: `cStack_2c5 = mgr+0x110` (`0x449xxx`,
"Update_UI_Sound_Q" area). While it is set:

| Part of the loop | With a menu open |
|---|---|
| Music buffers (`FUN_0046ff30`) and the music volume change check | run |
| Speech queue (`FUN_0046faa0`) | runs (queued voices keep playing) |
| Frame counter `0x6b9860` | **set to 0 every pass** (so the 1-second timer's `FUN_0045b570(0)` keeps the old base, doc 02) |
| Game view, sidebar, radar, power bar, top-left text (`FUN_00423d20` … `FUN_00427cc0`) | **not drawn** (except while `0x5178ac`): the last game frame stays in `gBackBuf` under the menus |
| UI manager `FUN_004712c0(gBackBuf, 1)` | runs: menus drawn over the frozen frame, input to the top menu |
| Tooltips | off (`0x517568 = 0`) |
| Cursor (`FUN_00423b40`, `FUN_00423a20`) | drawn |
| In-game input `FUN_004433a0` (scroll, orders, hotkeys) | **skipped** |
| Simulation tick ("Game AI section") | **skipped**: the `else` branch calls only `FUN_0044c420(0)` (clears the "mouse in playfield" globals `0x798540` / `0x51789c`) |
| Mission timer, script, production, money drip | stop (they are in the tick) |

So single player **pauses completely**, music and queued speech
excepted. In multiplayer (`0x7984c4`) the `else` branch jumps back into
the networking and tick code: the game keeps running under the menu
(only the in-game input is skipped).

After the last menu closes, the first timer second has counted only the
frames since the close, so `FUN_0045b570` sees a smaller frame count and
rebuilds the tick pattern with that base for one second (doc 02). We get
the same effect if our scheduler zeroes its frame count while paused.

**Kept quirk (decided with the user 2026-10-09):** the 1-second timer
runs `FUN_0045c480` (cap = base) *before* `FUN_0045b570` stores the new
frame count (`0x4a37a9..0x4a3823`). When the 9th-second cap update
falls on the second right after a menu closes, the cap becomes that
short second's frame count, and the game runs at min(speed, cap) ticks
per second for up to 9 s (seen in play as "time slows down after
pause", roughly 1 unpause in 25). Kept as in the original.
**Multiplayer TODO:** in multiplayer the timer takes the network branch
(`FUN_0045d390`, and `FUN_0045c480` negotiates the rate over the
network) and the game does not pause under menus; re-check this
interaction when multiplayer is implemented, not before.

## 2. Pages (single player)

`menus.uib` (a = entering, b = leaving transition; 3 = SKIP, 2 = TWEEN):

| Menu | Script | a, b |
|---|---|---|
| INGAME | `InGame.uil` | 3, 3 |
| INGAME_SKIRMISH | `InGameSK.uil` | 3, 3 |
| INGAME_MP | `InGameMP.uil` | 3, 3 |
| OPTIONS | `options.uil` | 3, 3 |
| SOUNDOPTIONS | `sound.uil` | 3, 3 |
| CONFIRMABORT | `confirmA.uil` | 3, 3 |
| REALLY_RESTART | `con_rst.uil` | 3, 3 |
| LOAD / SAVE | `Load.uil` / `Save.uil` | 2, 2 |
| DELETEGAME | `delete.uil` | 3, 3 |
| NO_SAVED_GAMES | `NoSaved.uil` | 3, 3 |
| ABORTING | `abort.uil` | 0, 1 |
| RECAP | `IG<A/H/O><n>MIS.uil` (built by name, doc 03 / our runner) | 0, 1 |

All buttons below use the colours LightRed / VDarkRed / Red / DarkRed;
"std" = font ButtonUpColour / ButtonDownColour, "rubber" = JRubberText /
JRubberTextDn.

### 2.1 INGAME (`InGame.uil`)

GROUPBOX (200,65) 208×205, Black/Red, image `box2`. TEXTBOX_UL
(220,90) 164×20 "TitleOptions" (JHighLightText). Buttons (std font):

| Button | Pos, size | Text key | Gfx | Action |
|---|---|---|---|---|
| loadMission | (233,110) 140×20 | LoadGameFile | but1 | JumpToMenu LOAD (NO_SAVED_GAMES if there are no saves, doc 04) |
| saveMission | (233,135) 140×20 | SaveGameFile | but2 | JumpToMenu SAVE |
| deleteMission | (233,160) 140×20 | DeleteGameFile | but3 | JumpToMenu DELETEGAME |
| gameControls | (233,185) 140×20 | GameSettings | but4 | JumpToMenu OPTIONS |
| abort | (233,210) 140×20 | AbortMission | but1 | JumpToMenu CONFIRMABORT |
| briefing | (233,235) 70×20 | Briefing | but2 | JumpToMenu RECAP |
| resume | (233,260) 70×20 | ResumeMission | but3 | BackMenu |

Note: the group box is 205 high (ends at y 270) but Resume ends at 280;
the bounding box used for "click outside" grows to include it.

**INGAME_SKIRMISH** (`InGameSK.uil`): image `box2a`, no briefing button,
Resume at (233,235). Otherwise identical (OPTIONS → OPTIONS, "was
MP_OPTIONS").

### 2.2 OPTIONS (`options.uil`)

GROUPBOX (186,86) 228×205, image `Settings`. TEXTBOX_UL (200,101)
195×20 "TitleGameSettings". Four TEXTBOX_L labels (JNormalText) at x 200:
"GameSpeed" (y 121, 90×12), "ScrollSpeed" (151, 90×12), "MusicVolume"
(181, 90×20), "SFXVolume" (211, 90×20).

Four `VALUEBAR_NOVAL` at x 290, y 121 / 151 / 181 / 211, nominal 90×20,
colours VDarkRed / Red / DarkRed / Black / White / Black, region image
`hzhole`, thumb image `hzbttn`, then `min max step default`:

| Bar | min max step default | Init command | On release |
|---|---|---|---|
| gameSpeed | 15 100 5 100 | GetValueU2Init gLimitedModelPct | SetValueU2Special gLimitedModelPct |
| scrollSpeed | 4 40 2 4 | GetValueS2Init gScrollSize | SetValueS2Special gScrollSize |
| cdVol | 0 100 5 0 | GetValueS4Init gCDVolume | SetValueS4Special gCDVolume |
| fxVol | 0 100 5 0 | GetValueS4Init gSFXVolume | PlaySample BUTTON, SetValueS4Special gSFXVolume |

Buttons (rubber font, gfx `Rbut4`): **Music** (216,243) 50×20 →
JumpToMenu SOUNDOPTIONS; **Done** (313,243) 50×20 → ParseEndCommands,
BackMenu.

Globals (UI globals table, `re/dump_ui_tables.py`): gLimitedModelPct =
`0x4e3b18` (u16, game speed), gScrollSize = `0x4eb030`, gCDVolume =
`0x4e5fc8`, gSFXVolume = `0x4e5fc4`.

Note the game-speed slider can only reach 15..100 in steps of 5, so the
doc 02 buckets reachable from the menu are 10 (15–23), 12, 15, 20, 25 and
"cap" (95, 100) ticks per second. The default 75 is on the grid.

### 2.3 CONFIRMABORT (`confirmA.uil`)

GROUPBOX (139,200) 170×60, image `Barbtton`. TEXTBOX_UL (159,220)
290×20 "AreYouSure". Buttons (std font, gfx `but8`, 10 px high):

| Button | Pos, size | Text | Action |
|---|---|---|---|
| yesAbort | (177,240) 53×10 | Abort | PlaySample S_MISSIONABORT (house voice, doc 19 §5), SetValueECTo gGameState GameStateBriefing (1), QuitAndJumpToMenu ABORT_MP_GAME |
| restart | (264,240) 55×10 | Restart | JumpToMenu REALLY_RESTART |
| no | (351,240) 57×10 | Cancel | BackMenu |

Abort flow (doc 19 §4.4–5): gGameState = 1 ends the loop after this pass;
music and speech stop (`0x44a0ea`); ABORTING (black, "ReturnToMainMenu"
in Yellow on Red at (240,200) 160×25; its TEXTBOX header line is missing
in the file: the line after `END_GROUPBOX` is the comment " // Static
Text boxes" and then "240, 200", so by the loader rule of doc 03 (a block
without its element line is skipped, as for sound.uil's TOGGLEBUTTON and
the MP Cancel button, doc 58 §6.3) only the group box exists and the text
never shows) is opened and closed; then the queued QuitAndJumpToMenu
ABORT_MP_GAME runs: campaign → **MAINMENU only**, mission number
unchanged; skirmish → MAINMENU, MULTIPLAYERTYPE, SCORE. No banner, no
MISSIONFAILED voice, no score in the campaign.

### 2.4 REALLY_RESTART (`con_rst.uil`)

GROUPBOX (183,174) 240×130, image `box4`. TEXTBOX (203,200) 200×20
"ConfirmRestart". **Restart** (218,259) 70×20 (gfx but8): `SetValueBFTo
gRestartGame TRUE`, `RestartGame`. **Cancel** (310,259) 70×20: BackMenu.

`RestartGame` (command 0xDA, `FUN_0044c460`): `0x797e1c = 1`
(gRestartGame), queue QuitMenu. On the next pass the game loop sees
`0x797e1c` and jumps to its start (`LAB_004485b7`): the per-mission state
is reset (won/lost flags, tick counter `0x5173f4 = 0`, `FUN_0045b7e0`)
and the mission is set up again (`FUN_004470a0`, map / objects), but the
graphics and UI resources are not reloaded (`0x797e1c` skips
`FUN_0044acc0`, `FUN_00460ec0`, `FUN_00466140`, `FUN_00454e00`). No
briefing, no menus. Verified 2026-10-16: `0x797e1c` is cleared at the end
of that set-up (`0x448d50`), right after the music start. The restart runs
the same set-up as a first start (`0x448b79`..`0x448d50`), so the music
**restarts**: with music on (`0x4eb00c`) and not a loaded game, the
campaign plays the mission's track (`0x7957fc[side·10 + mission]`, via
`FUN_004700e0`), other game types the fixed track at `0x4e0720`. The
loaded-game flag `0x5178bc` is cleared at `0x448d7d` of the first set-up,
so restarting a loaded game starts the mission fresh (it does not reload
the save). Other writers of `0x797e1c`: `0x449d46`, `0x44a005`,
`0x44a0d1` set it to 1 when `0x7984ac` is set; `0x7984ac` is never
written (doc 19 §4.4), so only RestartGame restarts a mission.

### 2.5 RECAP (in-game briefing)

`IG<side><n>MIS.uil`, e.g. IGA1MIS: GROUPBOX (160,40) 300×330 `ingbrief`;
TEXTBOX (180,50) 280×30 "Briefing"; **LISTBOX** (182,90) 256×225 with
scroll images Wslider / Uparrow(p) / Dnarrow(p) and two `EnterText`
lines (AM1Text1, IG_AM1Text2) — a scrolling text list; **Done** (274,342)
50×15 → BackMenu. Opened over INGAME (INGAME stays below).

No transition runs in a mission (verified 2026-10-16, exe and capture).
RECAP is FADE / FADE (0 / 1) and LOAD / SAVE TWEEN (2 / 2) in
`menus.uib`, but `FUN_00472730` returns at `0x47273e` without starting
anything while `gGameState` (`0x4dfb08`) is 2 (it only clears `+0x11d`
and switches input on), and the step `FUN_00472870` skips the same way
(`0x472887`). So opening and closing RECAP, LOAD and SAVE in a mission
neither fade nor tween: the menus are drawn on the next UI update. The
capture of the original (A1V1, frames ~12 ms apart) shows, after Done:
one frame with INGAME drawn over RECAP's remains (no darkening), then the
game view under INGAME. That is the close redraw of section 1.2:
`FUN_00473130` sets `0x5178ac` = `0x5178b0` = 1 and flags the remaining
menus for a full redraw, so the closing pass draws INGAME over the old
picture; after the UI manager `0x449429` counts `0x5178b0` down to 0; the
next pass draws the game view (`0x448fb7`: paused, drawn only while
`0x5178ac` is set) before the UI manager, which redraws every menu over it
(`0x4726d4`: `0x5178ac` and `gGameState` 2 -> `FUN_0043ca50`); after it
`0x5178b0` is 0, so `0x5178ac` is cleared. Ours: `MenuRunner::
start_transition` and `ingame_frame` drop the transition while
`gGameState` is 2; `GameScreen` keeps the two flags (`redraw_view_`,
`redraw_count_`) in the same order.

`EnterText` on a list box (`FUN_00480e00`, element type 3) looks the key
up in text.uib and adds it with `FUN_00486f10` (verified, implemented in
`ui::wrap_list_text`):

- limit = box width − 20 (`+0x10 − 0x14`), widths in the list's font;
- a text narrower than the limit becomes one row **as is** (0xAC kept);
- otherwise words are added while the row including its trailing space
  stays narrower than the limit; 0xAC forces a break (so `¬¬` gives an
  empty row); a first word wider than the limit is cut to fit; a
  remainder without spaces that is too wide is cut to the longest prefix
  that fits;
- each row is trimmed of spaces and 0xAC at both ends and added with
  `FUN_00486d20` (data 0, normal colour).

### 2.6 SOUNDOPTIONS (`sound.uil`), LOAD / SAVE / DELETEGAME

- SOUNDOPTIONS: GROUPBOX (150,50) 276×278 `sound`; LISTBOX musicList
  (172,101) 229×127 of CD tracks (`GetCDTracks`, `SetValueU4Special
  gCurrentCDTrack`, `StaySelectedSpecial`); Play (`PlayCDTrack`), Stop
  (`StopCDTrack`, `ResetToggleButton repeat`), a TOGGLEBUTTON whose
  header line is missing (inert, doc 03), Done → BackMenu. Selects the
  in-game music track (doc 15). Low priority.
- LOAD / SAVE / DELETEGAME: save-game list boxes (`GetGameSaves`,
  `UILoadGame`, `ParseEditBox`, `UICheckSaveGame`, `JumpIfSelected …
  CONFIRM_DELETE`): doc 27.

## 3. How the settings are applied

**Value bar** (CValueBar, ctor `FUN_0048c030`, AddValueBar
`FUN_00438ad0`, element type 8):
- Fields: min `+0x7a4`, max `+0x7a8`, step `+0x7ac`, value `+0x7b0`
  (the script default, clamped to [min, max]), clamp copies `+0x7b4` /
  `+0x7b8`. The `Get…Init` command then sets the value from the global.
- Size: the **region image** (`UI_gfx\hzhole`) replaces the script size
  when present. Thumb width = region width / 5; thumb x = bar x +
  ((w − w/5)·(value − min)) / (max − min) (integer division), y = bar y,
  height = region height.
- Draw (`FUN_0048cbf0`): region image at (x, y), thumb image (`hzbttn`)
  at the thumb rect. Without images it draws filled rects in the script
  colours. `VALUEBAR` (not _NOVAL) also prints the value (`itoa`) to the
  right of the bar (x + w + 4); _NOVAL does not.
- Mouse down (`FUN_0048c640`): on the thumb → start dragging (grab
  offset `+0x7e8`); left of the thumb → value −= step; right of it →
  value += step (clamped), then **auto-repeat every 200 ms**
  (`FUN_0048c450`, `GetTickCount`, `> 199`) while the button stays down
  inside the bar.
- Drag (`FUN_0048c9c0`, called by the mouse-move handler `FUN_00437520`
  while `+0x7dc` is set), verified 2026-10-16. Let tx = mouse x − grab
  (`+0x7e8`), the bar x0 = `+0x7cc` (= x), x1 = `+0x7d4` (= x + w), the
  thumb width tw = w/5.
  - x0 < tx < x0 + w − tw: the thumb is put **at tx** (it follows the
    mouse, not the value) and redrawn; the value is, on the x87 at 53-bit
    precision (doc 02),
    `ftol(min − ((((tx − x0) / (x1 − x0 − tw)) · 100.0f) · n · step) ·
    (−0.01f))` with n = (max − min) / step (unsigned integer division)
    (`0x48cb13`..`0x48cb8c`; `0x4d7018` = 100.0f, `0x4d701c` = −0.01f =
    0xbc23d70a, slightly above −0.01). Then, if v % step ≠ 0, v = v / step
    · step (absolute, not relative to min), and clamped to [min, max]
    and the copies `+0x7b4` / `+0x7b8`. **Quirk:** because the float
    −0.01 is not exact, a thumb whose exact value is a whole number gives
    a little less, which `ftol` truncates to one below; on a step
    multiple that drops a whole step. With `hzhole` (92 px: tw 18, track
    74) it happens only at tx − x0 = 37: a volume bar gives 45 instead of
    50, the scroll bar 20 instead of 22; the game-speed bar (85/74) never
    lands on a whole number.
  - tx ≤ x0: value = min (or max if max < min), clamped; tx ≥ x0 + w −
    tw: value = max, clamped. Only when the value changes is the thumb
    moved, to the value's place; otherwise it stays where the last move
    left it.
  - The release (`FUN_00437600`) puts the thumb back at the value's place.
- Mouse up (`FUN_00437600`) runs the element's commands, including the
  "Special" setters through `FUN_00483b80` (the value bar's current value
  is `+0x7b0`). So **each setting is applied when the button is
  released**, not when Done is pressed. `ParseEndCommands` (0xC8,
  `FUN_004749c0`) only runs the `…AtEnd` setters (0x94–0x9B) of the other
  elements; OPTIONS has none, so Done is effectively BackMenu.

**Special setter tables** (`{name[0x3c], u32 address, u32 callback}`; a
null callback writes the value directly):

| Table | Entries |
|---|---|
| U2 `0x4c9978` (5) | gNetStartingCredits `0x4e3b08` (cb `0x4901c0`), gMyCurrentFrameRate `0x4e3b14` (cb `FUN_0045b570`), gLimitedModelRate `0x6b9864` (cb `FUN_0045b830`, a no-op), **gLimitedModelPct `0x4e3b18` (cb `0x45b590`)**, gDifficultyLevel `0x4e8bf0` |
| S2 `0x4c94a8` (4) | gBitsPerPixel, gGameBitsPerPixel (cb `0x491bc0`), **gScrollSize `0x4eb030`**, gNetNetworkType |
| S4 `0x4c95b8` (3) | gSideId `0x798544`, **gSFXVolume `0x4e5fc4`**, **gCDVolume `0x4e5fc8`** |

- **Game speed** callback `0x45b590` (code with no Ghidra function): if
  the value differs, store it in `0x4e3b18`, clamped to 100; build a
  system order type 0x0b (value at +0x14, due tick `0x5173f4 +
  0x4e3ae8`) and queue it (`FUN_0045b9b0`); multiplayer also sends it
  (`FUN_0045bcc0`). In single player nothing rebuilds the tick pattern
  at once: it is rebuilt by the next `FUN_0045b610` call, i.e. at the
  next 1-second timer when the frame count changed (always the case
  right after a menu closes, §1.3) or at the 9th-second cap refresh
  (doc 02). So the new speed takes effect about one second after
  resuming. Order 0x0b is **not consumed in single player** (verified
  2026-10-16): the order pass that applies it is `FUN_0045c740` (the
  "GetNetworkOrders" step,
  applies the speed and calls `FUN_0045b610`), whose only call
  (`0x4498a7`) sits under the network flag `0x7984c4` test at `0x449880`.
- **Scroll speed**: direct write; used by `FUN_004433a0` each loop
  (doc 10 §4), so it applies on resume.
- **Music volume**: direct write; the loop compares `0x4e5fc8` with its
  cached copy every pass and calls `FUN_004700b0` (Miles volume) when it
  changed — applies immediately, even while paused.
- **SFX volume**: direct write; used for every later sample (e.g.
  `FUN_0046faa0`, `FUN_004762f0`: `gGameState == 2 ? gSFXVolume : 100`).
  The bar's `PlaySample BUTTON` previews it on release.
- Saving: the values are written to `dune2000.cfg` by `FUN_00478fa0`
  (doc 02 table) **on exit only** (verified 2026-10-16): its only caller
  is the UI manager teardown `FUN_00470b90`, called only from the
  shutdown `FUN_0044a870`, which WinMain (`FUN_004a3bb0`, `0x4a3ee0`)
  runs after the message loop. A crash loses the changes. We write
  settings.ini when a value changes (a crash-safety difference with no
  effect on play).

## 4. UIL commands and widgets our runner does not support yet

Supported today (src/app/menu_runner.cpp, src/ui): BUTTON, GROUPBOX,
CHECKBOX, TEXTBOX*, ANIMATION, MOVIE, BACKGROUND; navigation commands;
`SetValue…To`, `CheckIfValue…`, `UnCheck`, `PlaySample`, `EnterText`
(single line), `Dune2000CdCheck`, RECAP/PRECAP script names.

Needed for the in-game pages:

| Item | Where | Semantics |
|---|---|---|
| VALUEBAR / VALUEBAR_NOVAL draw + input | OPTIONS | §3 |
| `GetValueU2Init` / `GetValueS2Init` / `GetValueS4Init` (0x75 / 0x72 / 0x73, init) | OPTIONS | value bar value = global (u16 / s16 / s32) |
| `SetValueU2Special` / `SetValueS2Special` / `SetValueS4Special` (0xA4 / 0x9E / 0xA0) | OPTIONS | on mouse-up: table lookup by name, callback or direct write (§3) |
| `ParseEndCommands` (0xC8) | OPTIONS Done | run the `…AtEnd` setters of the other elements (none here) |
| `RestartGame` (0xDA) + `SetValueBFTo gRestartGame TRUE` | REALLY_RESTART | restart the mission in place (§2.4) |
| `PlaySample S_MISSIONABORT` as a house voice | CONFIRMABORT | the mouse-up handler routes it through `FUN_004762f0` (doc 19 §5) |
| QuitAndJumpToMenu ABORT_MP_GAME (campaign: MAINMENU only) | CONFIRMABORT | check the runner's branch for gGameType 0 |
| LISTBOX with several `EnterText` lines, scroll slider/arrows | RECAP | multi-line text list (also the briefing book) |
| Esc → first button with BackMenu; click-outside closes INGAME | all | §1.2 |
| Opening a menu stack over the running game, frozen game view under it | game screen | §1.1, §1.3 |

Later (not needed for A1V1): SOUNDOPTIONS (LISTBOX of tracks, `GetCDTracks`,
`PlayCDTrack`, `StopCDTrack`, `SetValueU4Special(From)`,
`StaySelectedSpecial`, `ResetToggleButton`), LOAD / SAVE / DELETEGAME
(`GetGameSaves`, `UILoadGame`, `ParseEditBox`, `UICheckSaveGame`,
`CopyToEditBox`, `ForceButtonDown`, `JumpIfSelected`, LISTBOXEMPTYSLOT,
EDITBOXFOCUS), INGAME_MP / chat (`SendInGameMsgTo`, `ReturnPageMessage`).

## 5. Implementation checklist

1. Game screen: host a MenuRunner-style menu stack inside the game loop;
   draw it over `screen_` (the game surface) each pass.
2. Open INGAME (INGAME_SKIRMISH for skirmish) on OPTIONS release and on
   Esc and Space (§1.1) unless the mission ended; queue-clear
   first.
3. While any non-chat menu is open: no ticks, no in-game input, no
   tooltips, no game-view redraw (keep the last frame), but music and the
   speech queue run; the tick scheduler's frame count is held at 0 and
   `tick_due` is not walked (no slot advance): the only call of
   `FUN_0045b790` (`0x4499a8`) is past the paused single-player branch,
   which jumps from `0x449840` straight to `0x44a068` (verified
   2026-10-16).
4. After any menu close, redraw the game view on the next pass (the
   0x5178ac / 0x5178b0 flags, section 2.5).
5. Close on: Resume / BackMenu chain, QuitMenu, click outside INGAME,
   Esc → first BackMenu button.
6. OPTIONS: value bars (§3), applied on release; game speed →
   `settings.game_speed` + scheduler (rebuild at the next 1-s timer as the
   exe; our scheduler already rebuilds when the frame count changes),
   scroll speed (add `gScrollSize`: our edge scroll uses a constant
   today), music and SFX volume (audio mixer gains); save to
   settings.ini.
7. CONFIRMABORT → Abort: S_MISSIONABORT house voice, stop music and
   speech, return to MAINMENU (campaign; mission unchanged). Restart:
   REALLY_RESTART → rebuild the mission (new GameScreen / World with the
   same mission, no briefing).
8. RECAP: LISTBOX text rendering with the scroll arrows (done 2026-10-08).
9. Leave LOAD / SAVE / DELETEGAME and SOUNDOPTIONS as "not implemented"
   (buttons present, log the action), until save games (M6).
