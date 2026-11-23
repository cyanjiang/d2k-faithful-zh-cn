# 48: The in-game music page (SOUNDOPTIONS) and Tab in menus (1.06)

Two small menu features. Part A completes the jukebox notes of doc 15 §4.3
with the list-box commands, the selection rules and our code status.
Part B is the Tab key in menus (`FUN_0043cdf0`, VK 9).

"Verified" means read from the code or the data. No open points remain.

| Address | Role |
|---|---|
| `FUN_00476590` | `GetCDTracks` (0xD6, init): fill the list from `MUSIC\*.aud` |
| `FUN_004764b0` | `PlayCDTrack <list>` (0xD4): play file `selection − 1` of the unfiltered listing |
| `FUN_00476470` | `StopCDTrack` (0xD5): stop the music, clear the track name |
| `FUN_00476770` | n-th file (0-based) of `MUSIC\*.aud`, without `.aud` |
| `FUN_00476a00` | `ResetToggleButton <name>` (0xD8): a toggle by name to off, `gRepeatTrack` = 0 |
| `FUN_004768e0` | `GetToggleStatus` (0xD7, init): toggle state from a BF global |
| `FUN_0047efc0` | `SetValueU4Special <g>` (0xA6) on a list: `g = selection − 1` |
| `FUN_0047ef00` | `SetValueU4SpecialFrom <g> <src>` (0xA7): `g = src`, both by name |
| `FUN_00491200` | `StaySelectedSpecial` (0x2A): an emptied selection goes back to item 1, scroll to the top |
| `FUN_00487a00` | list selection setter (doc 27 §3.4) |
| `FUN_00487990` | list clear (count 0, first 0; the selection is kept) |
| `FUN_00483b80` | list-selection command dispatcher |
| `FUN_0043c270` | action-line parser: init commands run **at parse time** |
| `FUN_0048dc10` | text.uib key → index, `strcmpi`, 0 = not found |
| `FUN_0046fd90` | open and prime a music file, Miles volume 0x40 (doc 04) |
| `FUN_004700e0` | play a track, then volume `gCDVolume` in game / 100 outside |
| `FUN_00470070` | stop the music (close the file, `AIL_end_sample`) |
| `FUN_0043cdf0` | menu key-down handler (Left, Right, Backspace, Delete, Tab) |
| `FUN_004712a0` | calls it for the top menu; the window proc skips it during a transition (`mgr+0x11D`, `0x4a33b8`) |
| `FUN_00438390` | AddEditBox: first free slot of `menu+0x3F4[20]` |
| `FUN_00437840` | mouse down: a click on an edit box focuses it, unfocuses the others |

Globals: `0x797a98` current track name (doc 15), `0x4e5fbc`
`gCurrentCDTrack`, `0x795604` `gRepeatTrack`, `0x5139f0` globe reveal
done (doc 05).

## Part A: SOUNDOPTIONS

### A.1 Reaching the page (verified, UI data)

In-game OPTIONS (`options.uil`) → button **Music** (216,243) 50×20 →
`JumpToMenu SOUNDOPTIONS`. The page has **Done** (252,291) →
`BackMenu`, back to OPTIONS. Esc runs Done (first button with BackMenu,
doc 20 §1.2). The front end has no OPTIONS, so the page exists only in
game. It is a pop-up on the menu stack. In game (`gGameState` 2) the
music is not stopped by menus and the music refill keeps running (doc 15
§3), so the current track goes on playing while the page is open.

Elements (`sound.uil`): GROUPBOX (150,50) 276×278 Black/Red gfx `sound`;
TEXTBOX_UL `helpSound` (167,62) 240×20 `TitleSoundSettings` "MUSIC
SELECTION"; LISTBOX `musicList` (172,101) 229×127; BUTTON `play`
(202,247) 50×20 "Play"; BUTTON `stop` (299,247) 50×20 "Stop"; BUTTON
`done`. The headerless toggle block (270,310) "Loop"/"Single" is skipped by
the loader (doc 03), see A.6.

### A.2 The list: GetCDTracks (verified, `FUN_00476590`)

Init command (table flag +0xB8 = 1), so it runs while the script is parsed.
In `musicList` the lines are, in order: `SetValueU4SpecialFrom gLastCDTrack
gCurrentCDTrack`, `SetValueU4Special gCurrentCDTrack`, `GetCDTracks`,
`StaySelectedSpecial`. So when GetCDTracks runs, the first two are already
stored as selection commands and StaySelectedSpecial is not yet.

1. Nothing at all if Miles is not up (`0x795608` = 0).
2. Clear the list (`FUN_00487990`).
3. `_findfirst` / `_findnext` over `<music path>*.aud`, listing order
   (the exe does not sort; NTFS and ISO 9660 list by upper-case name,
   doc 15 §2).
4. For each file: key = `music_` + file name without its last 4
   characters (`music_AMBUSH`). Look it up case-insensitively in text.uib
   (`FUN_0048dc10`). If found, append an item: text = the string, colour 0
   (the list's font colour), data 0 (`FUN_00486d20`). OPTIONS.AUD and
   SCORE.AUD have no key and are skipped.
5. If the file's base name equals the current track `0x797a98`
   (`strcmpi`), remember its 1-based item number.
6. After the loop: if a match was found, select it with `FUN_00487a00(n, 0)`
   (normal change path; the selection commands run, A.4). The list is
   **not** scrolled to show it.
7. If `gCurrentCDTrack` = 0, set it to 1.

1.06 data: 17 files, 15 items, sorted:

| Item | Label (text.uib) | File Play really opens (A.5) |
|---:|---|---|
| 1 | The Ambush | AMBUSH |
| 2 | Attack on Arrakis | ARAKATAK |
| 3 | The Atreides Gain | ATREGAIN |
| 4 | Enter the Ordos | ENTORDOS |
| 5 | Fight For Power | FIGHTPWR |
| 6 | The Fremen | FREMEN |
| 7 | Harkonnen Battle | HARK_BAT |
| 8 | Land of Sand | LANDSAND |
| 9 | Plotting | **OPTIONS** |
| 10 | Robotix (`music_risehark`, swapped label) | **PLOTTING** |
| 11 | Rise of Harkonnen (`music_robotix`) | **RISEHARK** |
| 12 | The Soldiers Approach | **ROBOTIX** |
| 13 | Spice Scouting | **SCORE** |
| 14 | Under Construction | **SOLDAPPR** |
| 15 | The Waiting Game | **SPICESCT** |

(UNDERCON and WAITGAME cannot be played from the page. Correction to doc
15 §4.3 quirk 1: the rows are in file order, so row 10 reads "Robotix" and
row 11 "Rise of Harkonnen"; doc 15 listed them as if sorted by label.) The selection at
open is the row whose *file* is the current track, so after Play on item
9 (current = `OPTIONS`) or item 13 (`SCORE`) no row is selected the next
time the page opens; after Play on item 10 (current = `PLOTTING`) the page
shows item 9 selected, and so on. With an empty name (after Stop) nothing
is selected.

The list is rebuilt every time the page opens (the elements are created
again).

### A.3 Drawing and mouse (verified: the generic CListBox, doc 27 §3)

Nothing is special to this list. Colours from the script: background
Transparent (screen captured under the box), font JHighLightText, frame
Transparent, highlight **DarkRed**; arrow buttons hi Red, lo DarkRed, up
VDarkRed, down DarkRed; gfx NONE / Wslider / Uparrow(p) / Dnarrow(p).
Font 0, row height = font 0's first glyph height, text at x + 2, rows
`229 − 16` = 213 px wide. Visible rows = 127 / row height (9 if the height
is 14: FONT.FNT's first glyph is 1×14, doc 27 §3.2), so 15 items need the
scroll bar. (Ours still draws lists in FONTCOL, 17 px: 7 rows; doc 27
§3.2.)
Mouse: left click selects a row; the arrows scroll one row on release; the
track pages; the thumb drags; no wheel, no keyboard. Selecting a row does
**not** play it, a double-click does nothing more; only Play plays.

### A.4 Selection commands (verified)

On every selection change (`FUN_00487a00` → `FUN_00483b80`), in script
order:

- `SetValueU4SpecialFrom gLastCDTrack gCurrentCDTrack` (`FUN_0047ef00`):
  the target name is looked up in the 4-entry U4 table at `0x4c9ad0`
  (`gGameState`, `gLoadFileID`, `gCurrentCDTrack`, `gGameType`).
  `gLastCDTrack` is not there, so **nothing happens**.
- `SetValueU4Special gCurrentCDTrack` (`FUN_0047efc0`): for a list with
  selection s ≠ 0, `gCurrentCDTrack = s − 1`. With s = 0, nothing.
- `StaySelectedSpecial` (`FUN_00491200`): only if the selection is 0 and
  the list has items: select item 1 (`FUN_00487a00(1, 0)`, which runs these
  commands again: `gCurrentCDTrack` = 0) and set the first visible row to
  0, with the thumb recomputed and a redraw.

Then `FUN_00487a00` sets `gCurrentCDTrack` to 1 if it is 0 (doc 27 §3.4).

Effects:
- **Right-click release on the list** (selection → 0, doc 27 §3.4):
  StaySelectedSpecial selects **item 1 (The Ambush) and scrolls to the
  top**. So this list can never stay empty once it had a selection.
- `gCurrentCDTrack` is UI-only. Its only readers are `FUN_00476590` and
  `FUN_00487a00` (xrefs); the music code never reads it. Play reads the
  list's own selection. Nothing needs to keep it, except to be exact
  about the value if something displays it (nothing does).

### A.5 Play and Stop (verified)

**Play** (`PlayCDTrack musicList`, `FUN_004764b0`): searches the top
menu's list boxes by name. If its selection s ≠ 0:
1. name = `FUN_00476770(s − 1)`: file number s − 1 of the **unfiltered**
   listing (the off-by-two after item 8 in A.2);
2. `FUN_0046fd90("<name>.aud")`: open, prime, start from the beginning,
   Miles volume **0x40**. It is not `FUN_004700e0`, so `gCDVolume` is not
   applied;
3. `0x797a98` = name.

With no selection it does nothing. It plays **at once**, replacing the
current track (no fade, the queued audio of the old track is dropped, doc
15 §2). Pressing Play on the track already playing restarts it.

What happens next is the normal rule of doc 15 §2: at the end of the
file, in a **campaign** game, the refill sets the next-track flag and
`FUN_00481460` plays a random house track (never the same name) through
`FUN_004700e0`, which also restores the volume to `gCDVolume`. In
**skirmish** the jukebox track loops forever, at 0x40, until the volume
slider changes (the loop re-applies `gCDVolume` only on a change, doc 15
§4.2). There is no "continue with the next list item".

**Stop** (`StopCDTrack musicList`, `FUN_00476470`, the argument is
unused): `FUN_00470070` closes the file and ends the Miles sample, then
`0x797a98` = "". The refill needs an open file, so no end-of-track flag
can come: **the music stays silent** until Play, a save game load (an empty
name keeps it silent, doc 15 §1), a mission restart or the next mission. A
save made now stores the empty name. The button's second command,
`ResetToggleButton repeat`, does nothing (A.6).

### A.6 Repeat toggle (verified inert)

`ResetToggleButton repeat` (`FUN_00476a00`) searches the top menu's
toggle buttons (`+0x19C`, 50 slots) by name; only on a match does it turn
it off and set `gRepeatTrack` = 0. The `repeat` toggle is never created
(header line missing), so nothing happens. `gRepeatTrack` (`0x795604`) is
only touched by UI value commands and `GetToggleStatus` (`FUN_004768e0`);
no music code reads it. Doc 15 §4.4 holds.

## Part B: Tab in menus (verified, `FUN_0043cdf0`)

Only the top menu gets keys, and not while a menu transition runs
(`mgr+0x11D`). The handler runs on every WM_KEYDOWN, so **auto-repeat
counts**. No sound is played. For VK 9, in this order:

1. **GLOBE** (`menu+0x2C`, set by the menu loader at `0x43615b` right
   before CreateGlobe): while the territory reveal is not done
   (`0x5139f0` = 0, set by the globe update at `0x42cc6b` with the input
   switch), Tab is **ignored** (return). After it, step 4 runs (no edit
   boxes: nothing).
2. **Score menus** (`menu+0x30`) while the count-up is not finished
   (`0x516270` = 0): skip (`0x516270` = `0x516274` = 1), return (doc 24
   §2.5). Once finished, step 4 runs.
3. **CREDITS** (`menu+0x34`): queue RemoveMenu (0xE2). **No return**: step
   4 runs too (CREDITS has no edit box).
4. **Edit-box focus cycle** over `menu+0x3F4[0..19]` (AddEditBox puts
   boxes in the first free slot, so the order is **script order**):
   - find the first box with focus (`+0xAAC` ≠ 0), index i; clear its focus
     (redraw). None: i = −1;
   - from i + 1 to 19, the first box without focus gets focus, its cursor
     (`+0xA9C`) goes to the end of its text, redraw; done;
   - if none was found there, **wrap**: from slot 0, the first box without
     focus gets the same treatment. This includes box i itself.

   So Tab moves the focus to the next edit box, wrapping to the first.
   With one box, Tab takes the focus away and gives it back: the only
   visible effect is that **the cursor jumps to the end of the text**.
   With no focused box, the first box gets the focus. Only the first
   focused box is moved; other focused boxes keep their focus.

Other cases: menus without edit boxes ignore Tab. Mouse: a click on an
edit box focuses it (cursor to the end if it had no focus) and unfocuses
all others (`FUN_00437840`), so at most one box has focus after a click.

**Correction to doc 20 §1.2:** `menu+0x2C` is the GLOBE flag, not "has a
focused edit box". The Esc handler `FUN_0043cb70` has the same test: on the
GLOBE, Esc is ignored until the reveal is done.

### B.1 Which menus have edit boxes (verified, UI data)

| Menu (file) | Edit boxes, script order |
|---|---|
| SAVE (`Save.uil`) | `saveEdit` (FOCUS) |
| SKIRMISH (`Skirmish.uil`) | `nameEdit` (FOCUS) |
| score high-score entry | one runtime CEditBox (doc 24 §2.6) |
| `INetLog` | `idName` (FOCUS), `idPassword` |
| `IPXLobby` | `nameEdit` (FOCUS), `chat` |
| `ModJoin` | `phoneEdit` (FOCUS), `phoneName`, `phoneNumber` |
| `PageFind` | `userName` (FOCUS), `message` |
| `hostMP2`, `joinMP2` | `chat`, `nameEdit` (FOCUS): Tab from `nameEdit` wraps to `chat` |
| GamePage, gamemsg, chat lobbies, JoinGame, Private, NewChat | one box each |

**Single player never has two edit boxes**, so the focus never visibly
moves there. The visible single-player effect is the cursor jumping to the
end of the text in SAVE, SKIRMISH and the high-score name (after the score
count-up). Moving the focus between boxes matters only for the
multiplayer menus (later scope).

## Our code (2026-10-13)

What exists:
- List boxes: state, drawing, geometry, mouse, selection with commands
  (`src/ui/render.cpp:205` `list_geometry`, `:357` drawing;
  `src/app/menu_runner.cpp:1539` `list_select`, `:1548` `list_mouse`,
  `:589` right release → selection 0). The save lists (`GetGameSaves`,
  `:447`, `:1525`) show the pattern for GetCDTracks.
- `GetCDTracks` is in the init list (`menu_runner.cpp:66`) but has no
  handler in `run_init_commands` (`:423`): the list opens empty.
- `menu_runner.cpp:472` treats every `SetValue*Special` as "element value
  → global". For a list it must be `selection − 1` and only when the
  selection ≠ 0.
- `SetValueU4SpecialFrom` falls into the generic `SetValue` branch
  (`:488`) and writes `globals_["gLastCDTrack"] = "gCurrentCDTrack"`.
  Harmless, but the exe does nothing.
- `StaySelectedSpecial`, `PlayCDTrack`, `StopCDTrack` and
  `ResetToggleButton` reach the "UI action not implemented" log.
- The headerless toggle is already inert in our parser (doc 03 rule).
- In-game music: `src/app/game_screen.cpp:225-238` first track, `:1927`
  `next_track`, `:2100` / `:2187` end-of-track check, `:1865` load,
  `track_` is the name. The in-game menus use a separate `MenuRunner`
  with its own `music_` (`menu_runner.hpp:175`), so the page needs a hook
  into `GameScreen` (like `set_game_saver`, `menu_runner.hpp:109`).
- `MusicPlayer::play` (`src/audio/music_player.cpp:22`) keeps the last
  volume; it does not reset to 0x40 as `FUN_0046fd90` does.
- `next_track` (`game_screen.cpp:1951-1955`): with an empty `track_` the
  "differs from current" loop never ends (the empty prefix always
  matches). The exe cannot get there after Stop (no refill, no flag); ours
  must not either.
- Tab: `menu_runner.cpp:1849` returns. Front-end keys only call
  `handle_keys` for non-repeat key-downs (`:957`); the in-game menu path
  (`ingame_key_event`, `:1792` → `key_event(ev, true)`) never passes Tab.
  The score skip is limited to `SCORE_SINGLE` (`:1839`); the exe uses any
  `+0x30` menu. The CREDITS branch returns (`:1843`); fine, no boxes.

## Implementation checklist

1. `GetCDTracks` init handler: list `MUSIC\*.aud` sorted by upper-case
   name; for each, `lookup_text("music_" + base)`; append found ones
   (item colour = font colour). Select (with the commands) the item whose
   base equals the game's current track, case-insensitive, without
   scrolling; afterwards `gCurrentCDTrack` = 1 if it is 0. Needs the
   current track from `GameScreen` (hook).
2. `SetValueU4Special` on a ListBox: if selection ≠ 0, global =
   selection − 1. Keep the "set `gCurrentCDTrack` to 1 if 0" quirk in
   `list_select` only if we want the exact value (UI-only).
3. `SetValueU4SpecialFrom`: only the U4 table names (`gGameState`,
   `gLoadFileID`, `gCurrentCDTrack`, `gGameType`) as target; otherwise
   nothing.
4. `StaySelectedSpecial`: if selection = 0 and items exist, `list_select
   (1)` and `first = 0`.
5. `PlayCDTrack <list>`: selection s ≠ 0 → file s − 1 of the
   **unfiltered** sorted listing (keep the off-by-two; a fix would go
   behind `fixes.ini`), play it now, set the volume to 0x40, set
   `GameScreen::track_` to the base name. Do not touch the next-track
   logic: the campaign picks a random track at the end, other modes loop.
6. `StopCDTrack`: stop the music, `track_` = "", and make sure no
   end-of-track signal follows (guard `next_track` against an empty
   name).
7. `ResetToggleButton`: no-op unless a toggle with that name exists (none
   in single player).
8. Tab (`handle_keys`): ignore on the GLOBE until the reveal is done; any
   score menu not finished → skip; CREDITS → RemoveMenu; then the focus
   cycle over the menu's edit boxes in script order (clear the first
   focused, focus the next unfocused after it or wrap to the first
   unfocused from the start, cursor to the end, redraw). Let auto-repeat
   through and route Tab from the in-game menus too (SAVE).
9. Esc on the GLOBE during the reveal: ignored (same `+0x2C` test); check
   our globe Esc path.
10. Right-click on the music list jumping to The Ambush, Play on
    "Plotting" playing OPTIONS.AUD, the half volume after Play and Tab
    moving the cursor to the end in SAVE follow directly from the code
    read above (§A.4, §A.5, part B); no reference check is needed.
