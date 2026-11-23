# Save / Load / Delete menus and save files (1.06)

Covers the LOAD, SAVE and DELETEGAME menus, their pop-ups, the list box
element they use (LISTBOX / LISTBOXEMPTYSLOT), and how save files are
named, listed, written, loaded and deleted. Related: doc 03 (UIL loader),
doc 04 (menu navigation), doc 20 (in-game menu), doc 24 §2.6 (CEditBox).

"Verified" means read from the decompiled code at the cited address.
Things not checked are marked TODO(verify).

## 1. Where the menus are opened

| From | Button | Command | Notes |
|---|---|---|---|
| INGAME / INGAME_SKIRMISH | Load / Save / Delete Mission | `JumpToMenu LOAD` / `SAVE` / `DELETEGAME` | in a mission (gGameState `0x4dfb08` = 2) |
| GLOBE (`Globe.uil`) | `load` (182,331), `save` (277,331), 60×60 | `JumpToMenu LOAD` / `SAVE` | between campaign missions |
| MAINMENU | `loadMission` "Load Mission" | `Dune2000CdCheck`, `JumpToMenu LOAD` | |
| Skirmish setup (`Skirmish.uil`) | "Load" (`PracticeLoad`) | `JumpToMenu LOAD` | |

DELETEGAME is reachable only from the in-game menus.

**NO_SAVED_GAMES (verified, `FUN_004738b0` → `FUN_00492750`):** a
`JumpToMenu LOAD` opens NO_SAVED_GAMES instead of LOAD when slot 0
(`GameSave\S0.SAV`, or `P0.SAV` in skirmish) cannot be opened. SAVE and
DELETEGAME have no such check, so they open with an empty list.

Menu definitions (`menus.uib`; a = entering, b = leaving transition):

| Menu | Script | a, b |
|---|---|---|
| SAVE / LOAD | `Save.uil` / `Load.uil` | 2, 2 |
| DELETEGAME | `delete.uil` | 3, 3 |
| CONFIRM_DELETE | `ConfirmD.uil` | 3, 3 |
| CONFIRM_OVERWRITE | `Overrite.uil` | 3, 3 |
| SAVE_DESC | `savedesc.uil` | 3, 3 |
| NO_FREE_SPACE | `NoSpace.uil` | 3, 3 |
| NO_SAVED_GAMES | `NoSaved.uil` | 3, 3 |

## 2. Save files

### 2.1 Names and slots (verified)

- Campaign: `GameSave\S<n>.SAV`; skirmish (gGameType `0x797e34` = 1):
  `GameSave\P<n>.SAV`. `<n>` is a decimal number from 0, no padding
  (format strings `0x4e7d84` / `0x4e7d70`). The path is relative to the
  current directory (the open helper `FUN_0042ba50` prefixes an empty
  string).
- The choice between S and P depends only on gGameType **at the time of
  the action**. The main menu's Load button does not set gGameType, so it
  lists whatever the last game type was (0 at start-up). In practice the
  main-menu Load after a skirmish lists **`S*.SAV`** (verified from the
  1.06 scripts 2026-10-16): the exe writes gGameType only through menu
  commands and the save loader; a finished or aborted skirmish returns
  to MAINMENU, MULTIPLAYERTYPE, SCORE, so the player reaches MAINMENU
  only through MULTIPLAYERTYPE's Cancel (`MPType.uil`: `SetValueECTo
  gGameType GameTypeCampaign`, BackMenu), or Esc, which runs that same
  first BackMenu button with all its commands (doc 20 §1.2). The skirmish
  set-up's Cancel does the same (`Skirmish.uil`).
- Slots are contiguous. The list stops at the first missing number.
- **Slot 0 is always the most recent save.** Saving shifts the other files
  up by one (§4.3), and deleting shifts the later files down (§6). The list
  is therefore newest first. There is no sorting step and no date.
- There is no maximum number of saves in the code. The only limit is disk
  space (§4.3).

### 2.2 Header (0xD8 = 216 bytes, verified from `FUN_004414d0`)

| Off | Size | Field |
|---:|---|---|
| 0x00 | u32 | header size, always 0xD8 |
| 0x04 | u8 | **kind**: 1 = saved during a mission (full state), 0 = saved between missions (no state) |
| 0x05 | 3 | padding (uninitialised stack bytes) |
| 0x08 | u32 | version, always 0x28F (655) |
| 0x0C | u8 | player-block count `0x798628` (number of 0x26990-byte player blocks that follow) |
| 0x0D | char[203] | description, NUL-terminated (`strcpy` of the edit box text; the bytes after the NUL are uninitialised) |

The loader `FUN_004418b0` checks only size == 0xD8 and version == 0x28F.
It does not read the kind byte. The caller decides how to load (§5).

### 2.3 Body (high level, verified order)

The same in the writer `FUN_004414d0(path, full, desc)` and the reader
`FUN_004418b0(path, full)`. Every save has:

1. gMissionNumber `0x4de610` (u8), gSideId `0x798544` (u8), gGameType
   `0x797e34` (u32), difficulty `0x4e8bf0` (u16), the current music name
   `0x797a98` (10 bytes), scroll speed `0x4eb030` (u32), game speed
   `0x4e3b18` (u16), the volumes `0x4e5fc4` / `0x4e5fc8` (u32 each).

   A between-missions save ends here (248 bytes in all).

2. Full saves only (kind 1). These are raw memory dumps:
   - the player blocks `0x798870`, `count × 0x26990` bytes. Before writing,
     pointers are converted to indices (`FUN_00442170`), then back
     (`FUN_00441f30`). The loader converts them back.
   - the map/cell block `0x517de8` (0x30008 bytes), the tick `0x5173f4`,
     the sidebar state `0x516430` (0x54), 15 single bytes from `0x5162dc`
     (stride 0x18), the scroll position `0x798538` / `0x79853c`, the mission
     tables (tech `0x6b87c8` 0x50, conditions `0x6b8280` 0x540, events
     `0x6b7058` 0x1200, counts `0x6b8260` / `0x6b63cc`), `0x798830` (0x40),
     the tick again, the cell objects `0x517b98` (0x168), the per-player
     stats `0x4f4118` (0xEDC0), gSideId again, the houses `0x8cd4f0` (8),
     three 200-byte path strings (`0x5166a0`, `0x5165c8`, the map path
     `0x6f9840`), the player colours `0x5175d8` (8), `0x548048` (1), the
     mission timer `0x6b8270` (4), the mission path `0x6dc470` (200), the
     message ring `0x504030` (0x2B4), the worm count `0x6b9838` (1).
   - an 80-byte trailer: `wsprintf("%s", text "Loading")`. In the English
     text.uib that string is **"V 1.02"**. If the loader reads a different
     string it only logs "Game data invalid" (`LoadSave.cpp`); the load
     still goes ahead. Saves from another language version cause this log
     message.

   With 8 player blocks a full save is about 1.53 MB (0x175B28 bytes). This
   matches the 0x190000 free-space check in §4.3.

Our reimplementation will use its own format for the simulation state. To
reproduce the menus it needs: the description, the kind
(in-mission / between missions), mission number, side, game type, and the
"newest is slot 0" numbering.

### 2.4 Quick-save (not reachable)

`FUN_00448550` writes a full save to `FastSave.sav` with the description
"QuickSave" every 250 ticks when `0x7984e8` is set. Nothing writes that
flag, so this is a developer feature. Do not implement it.

## 3. List box element (CListBox)

### 3.1 Parsing (verified, `FUN_0043a2c0`, ctor `FUN_00486440`, 0x81C bytes, element type 3)

The keyword picks three flags: LISTBOXEMPTYSLOT = empty-slot, LISTBOX_MULTI
= multi-select, LISTBOX_CROPPED = cropped (no scroll bar, word-wrapped
text). Plain LISTBOX has none. The body lines are read in this order:

| # | Line (Save.uil example) | Field |
|---:|---|---|
| 1 | `192, 75` | position |
| 2 | `256, 225` | size |
| 3 | `Transparent` | background colour → +0x77C |
| 4 | `JHighLightText` | font colour (default item colour) → +0x780 |
| 5 | `Transparent` | frame colour → +0x800 |
| 6 | `LightRed` | highlight colour (selected row) → +0x784 |
| 7–10 | button hi / lo / up / down colours | the two arrow buttons |
| 11 | `NONE` | scroll-region (back) image → +0x80C/+0x808 (`NONE` = none) |
| 12 | `Wslider` | scroll-bar (thumb) image → +0x810/+0x804 |
| 13–14 | `Uparrow` / `Uparrowp` | up-arrow button images (up / pressed) |
| 15–16 | `Dnarrow` / `Dnarrowp` | down-arrow button images |
| … | commands until `END_LISTBOX` | e.g. `GetGameSaves`, `CopyToEditBox saveEdit` |

Colour names are matched case-sensitively (`FUN_0048db90`). An unknown
name gives index 0 (Transparent). `delete.uil` writes `vDarkRed` as its
button lo colour; it falls back to Transparent (verified 2026-10-16:
`FUN_0048db90` is a byte-wise strcmp over colours.uib's names and returns
0 when none matches; colours.uib has only `VDarkRed`, and its entry 0 is
`Transparent`). It only affects the arrow buttons' bevel.

Runtime fields: +0x788 visible rows, +0x78A item count, +0x78C array
capacity, +0x790 item strings, +0x794 item data, +0x798 item colours,
+0x79C first visible item, +0x79E last visible item, +0x7A0 "scroll bar
active" (count > visible rows), +0x7E8 **selection, 1-based, 0 = none**,
+0x7E4 thumb being dragged, +0x7EC / +0x7F0 up / down arrow buttons,
+0x7F4 empty-slot flag, +0x7F8 multi flag, +0x814 cropped flag.

### 3.2 Geometry (verified)

- Font 0 (`font` = FONT.FNT, loaded first in `FUN_00470710`). The row
  height is +0x779 = the height of font 0's first glyph (`FUN_00482830`).
  It is **14** (verified 2026-10-16: `FUN_00482830` copies byte +4 of
  glyph 0's image, `*(0x513b28) + 4`; FONT.FNT's glyph 0, right after the
  0x408-byte header, is 1×14). So the 225-px lists have **16 visible
  rows** (`h / row height`, integer division). The row texts use the same
  slot (`FUN_004885f0` passes `+0x775` = 0 to `FUN_0042dc00`, solid
  colour). Ours matches since 2026-10-16: `menu_font()` is font 0
  (FONT.FNT), `ui::list_geometry` takes the row height from its first
  glyph (14), and the list text is drawn with it in one colour.
- With a scroll bar (h ≥ 36 and not cropped) the rows are `w − 16` wide.
  The 16-px column on the right holds the up button (top 16 px), the down
  button (bottom 16 px) and the track in between.
- Thumb: height = `trackH · visible / count`, at most trackH and at least
  5. Top = `trackTop + ((last − visible + 1) · trackH) / count`, i.e.
  `trackTop + first·trackH/count` when the list is full. It is clamped so
  that it does not pass the bottom of the track (`FUN_0040b7c0`; the same
  formula is inlined in `FUN_00486d20` / `FUN_00487420`).

### 3.3 Drawing (verified, `FUN_004885f0`)

1. Background: if the background colour is Transparent (index 0) and there
   is no back image, the screen under the box is captured once into a back
   image (+0x808) and blitted on every redraw. Otherwise the box is filled
   with the background colour.
2. **Empty slot** (LISTBOXEMPTYSLOT only): the first row is always the text
   `EmptySlot` ("Empty slot"), at x + 5 in the font colour. It is drawn
   even when the list is scrolled. When nothing is selected (+0x7E8 = 0)
   that row is drawn highlighted. Items then start at index
   `first + 1` (item 0 is a NULL placeholder; see 3.6).
3. Items, one per row, top to bottom, until the visible row count is
   reached:
   - the selected row (`selection == index + 1`) is filled with the
     highlight colour, with a 1-px border in the background colour
     (`FUN_0042d660(rect, fill, top/left, bottom/right)`). Unselected rows
     are filled with the background colour only when there is no back
     image (so on these Transparent lists they are not filled).
     The border of a Transparent list is **black**: `FUN_0042d660` hands
     the colour's RGB (0, 0, 0) to the line functions `FUN_0042d450` /
     `FUN_0042d520`, which write it as is (no transparency test;
     verified 2026-10-16). Fixed in `ui::render` (it drew no border).
   - text at x + 2, at the row's top y, in the item's own colour (+0x798).
     `FUN_0042dc00` does **not clip**: a description wider than the row
     overflows. In practice the edit box's width limit (§4.1) keeps
     descriptions narrower than the row.
4. Scroll bar (when it exists): the track is framed in the frame colour.
   When the bar is active, the thumb is drawn with the `Wslider` image at
   the thumb rect. Otherwise the slider image is drawn at the top of the
   track. Then the two arrow buttons are drawn, and finally (label
   `LAB_0048aa70`, reached with or without a scroll bar) a frame around the
   whole box in the frame colour. Both frames go through `FUN_0042d140`
   (left, bottom, right, top lines via `FUN_0042d520` / `FUN_0042d450`)
   with the colour's RGB and no transparency test, so the **Transparent
   frame of LOAD / SAVE / DELETE is black**. Verified against a capture of
   the original LOAD page (2026-10-16, three saves, inactive bar): the
   1-px black frame at 192..447 × 75..299 and the black track frame at
   x 432..447, y 91..283; the inactive `Wslider` image and the arrows match
   our drawing pixel for pixel. Ours drew neither frame for Transparent
   until then (fixed in `ui::render`; test `tests/ui/test_screen_parity.cpp`).

### 3.4 Mouse (verified)

Window messages: doc 10 §1. The UI calls are `FUN_004711a0` → `FUN_00437840`
(left down), `FUN_004710d0` → `FUN_00437600` (left up),
`FUN_00471090` → `FUN_00437520` (move), `FUN_00471050` → `FUN_00437480`
(left double-click), `FUN_00471070` → `FUN_004374d0` (right double-click),
and `FUN_00471250` → `FUN_00437af0` (right up).

- **Left click on a row** (`FUN_00487af0`): the row under the cursor is
  found by stepping `row height` from the top (x from the left edge to
  `w − 16`). With the empty-slot flag, **the first visible row always
  deselects** (selection 0), whatever the scroll position. This matches
  the drawing, which always shows "Empty slot" there. Any other row selects
  item `first + row`, stored as selection = item + 1. Clicks below the last
  item do nothing.
- **Selection change** (`FUN_00487a00`, single-select): only if the value
  changes, it stores it, runs the list's selection commands
  (`FUN_00483b80`, e.g. `CopyToEditBox`) and redraws. Clicking the row
  that is already selected does nothing. Quirk: every change also sets
  `gCurrentCDTrack` `0x4e5fbc` to 1 if it was 0.
- **Double-click** (left): the same hit test, with the "force" flag. The
  selection is set directly **without** running the selection commands.
  The first click of the double-click has already run them. There is **no
  "double-click loads / saves" action.**
- **Right button**: on release over a list the selection is set to 0
  (normal change path, so the commands run; on SAVE this clears the edit
  box, §4.2). A right double-click selects the row like a left click.
- **Scroll arrows**: the press is shown on mouse-down. On mouse-up over the
  same arrow the list scrolls by **one row** (`FUN_004883b0`). There is no
  auto-repeat while the button is held: the only caller of `FUN_004883b0`
  is the mouse-up handler `FUN_00437600` (`0x437740`), and the move
  handler only redraws the arrows (`FUN_00488360`); verified 2026-10-16.
- **Track**: a click above the thumb pages up by the visible row count. A
  click below it pages down. A click on the thumb starts a drag (+0x7E4,
  grab offset = mouse y − thumb top). While dragging (`FUN_004880f0`):
  first = `(thumbY − trackTop) · (count − visible) / (trackH − thumbH)`,
  clamped. At the ends it snaps to 0 or `count − visible`.
- First visible item is always clamped to `[0, count − visible]`.
- **No mouse wheel**: the window procedure ignores WM_MOUSEWHEEL (0x20A
  is above its switch range, `0x4a34e8`). **No keyboard navigation** of
  lists: no key handler calls the selection functions.

### 3.5 Adding / removing items (verified)

- `FUN_00486d20(text, colour, data)` appends an item. Colour 0 means the
  list's font colour. It updates the visible range and the thumb.
- `FUN_00487420` removes the selected item and shifts the rest up. If the
  selection is now past the end, the last item is selected. Otherwise the
  selection index stays, so it now points at the item that moved into that
  row.

### 3.6 Empty-slot list indexing

The constructor sets count = 1 with item 0 = NULL (the "Empty slot"
placeholder). GetGameSaves then appends saves as items 1..n. So in the
SAVE list, **item i (i ≥ 1) is file `S<i−1>.SAV`**, and selection s
(1-based) is file `s − 2`. The empty slot itself can never be selected
(selection 1 does not occur). In LOAD and DELETEGAME (plain LISTBOX),
item i is file i and selection s is file `s − 1`.

## 4. SAVE menu (`Save.uil`)

| Element | Rect | Details |
|---|---|---|
| GROUPBOX gb | (170,25) 300×330 | Black / Red, image `Loadgame` |
| TEXTBOX_UL saveTitle | (180,35) 280×30 | `TitleSave` "SAVE MISSION", JHighLightText |
| LISTBOXEMPTYSLOT saveListBox | (192,75) 256×225 | colours §3.1; button up VDarkRed, lo DarkRed; `GetGameSaves`, `CopyToEditBox saveEdit` |
| EDITBOXFOCUS saveEdit | (202,307) 240×20 | bg VDarkGrey, outline JBoxOutLine, text JHighLightText; commands `UICheckSaveGame saveListBox saveEdit`, `ForceButtonDown saveGame` |
| BUTTON saveGame | (217,339) 50×15 | "Save", rubber font, RBut4; `ParseEditBox saveEdit` |
| BUTTON cancel | (352,339) 50×15 | "Cancel"; `BackMenu` |

Command ids (UI command table `0x4beb88`): 0xC9 UISaveGame, 0xCA
UICheckSaveGame, 0xCB LoadMission (globe, not save-related), 0xCC
UILoadGame, 0xCD GetGameSaves (init), 0xCE ParseEditBox, 0xCF
CopyToEditBox, 0xD0 CopyToEditBox_All, 0xD1 DeleteGame, 0xD2
LoadSavedGame (**empty function** `FUN_004762e0`), 0x19 JumpIfSelected,
0x1D EditBoxEmpty, 0x1E ForceButtonDown. Dispatchers: `FUN_00482850`
(action commands), `FUN_00484000` (init commands), `FUN_00483b80`
(list-selection commands).

### 4.1 GetGameSaves (0xCD, init, `FUN_00475a00`)

This runs every time the menu opens, so the list always reflects the disk.
For n = 0, 1, 2, …: open `S<n>.SAV` (or `P<n>`) and read the 0xD8-byte
header. Stop at the first file that cannot be opened. Each file appends
one item: text = the description (header +0x0D), data = n, colour =
**White** if the kind byte is non-zero (in-mission save), otherwise the
list's font colour (JHighLightText) for a between-missions save. Nothing
else is shown: no date, no mission, no house.

The header is read with `fopen(..., "r")` (text mode). A 0x1A or CR LF in
the first 0xD8 bytes would change what is read. Only the padding bytes or
the bytes after the description's NUL could contain them. Harmless in
practice. In the exe those padding bytes are uninitialised stack
(`FUN_004414d0` builds the header in a local buffer and copies only the
description string), so it cannot be settled statically; it does not
apply to d2k-faithful, which writes and reads its own save format.

### 4.2 Edit box and CopyToEditBox

- The edit box is a CEditBox (doc 24 §2.6) with focus from the start,
  character filter 0 and max length 0. Nothing in Save.uil sets them (the
  init commands that would are in `FUN_00484000` at `0x484a10..0x484a79`).
  The only limit is width: a character is rejected if the text would
  become wider than box − 10 = 230 px in font 0. A space is not accepted
  at position 0. **Correction to doc 24:** `FUN_0043cb70` compares the
  character as a *signed* byte (`cmp bl, 0x20; jl` at `0x43ccf0`), so
  characters 0x80–0xFF are rejected. Accepted: 0x20–0x7F.
- **CopyToEditBox saveEdit** (0xCF, `FUN_00475c10`, run on a selection
  change):
  - selection 0 on an empty-slot list (clicking "Empty slot", or a right
    click) → the edit text is cleared and the cursor goes to 0;
  - otherwise the selected item's text is copied into the edit box
    (whole string, not truncated to the box), and the cursor goes to the
    end.
  So clicking a save fills in its description, ready to overwrite.
- **Enter** in the edit box (`FUN_0043cb70`) runs the edit box's commands
  (`UICheckSaveGame`, then `ForceButtonDown`). **Esc** runs the first
  button that has `BackMenu` (Cancel).
- **Save button**: `ParseEditBox saveEdit` (0xCE, `FUN_00475b60`) runs the
  edit box's commands if its text is not empty **or the top menu is
  SAVE**. On the SAVE menu it therefore always does the same as Enter.
- **ForceButtonDown saveGame** (0x1E, `FUN_00492f70` → `FUN_004852e0`):
  this only has an effect when run from an edit box (element type 6). It
  shows the `saveGame` button pressed for 500 ms (+0x7AD = 1, +0x7B4 =
  500, +0x7B0 = GetTickCount). It is purely visual and runs no command.

### 4.3 UICheckSaveGame (0xCA, `FUN_00474cf0`) and UISaveGame (0xC9 / direct, `FUN_00474e20`)

**Check** (args: list, edit box):
1. Empty text → open **SAVE_DESC** ("You need to enter a description
   string for the save file", OK → BackMenu).
2. Otherwise, if the text equals (case-insensitive, `_strcmpi`) any item
   1..count−1, **or** a row is selected → open **CONFIRM_OVERWRITE**
   ("Are you sure you want to overwrite this game save?";
   Yes → `UISaveGame saveListBox saveEdit`, No → BackMenu).
3. Otherwise save at once (`FUN_00474e20(list, edit, 0)`).

**Save** `FUN_00474e20(list, edit, fromConfirm)`. The list and edit box
are looked up in the SAVE menu (`FUN_00470f10("SAVE")`), so this works
from the confirm pop-up.
1. Empty text → return.
2. Target list index t = selection − 1 if a row is selected, else count
   (a new slot). Then **a description match overrides it**: every item
   1..count−1 whose text equals the edit text (case-insensitive) sets t
   to that index. The last match wins. With the empty-slot flag, t −= 1.
   So t is the file number (new save: t = number of files).
3. Disk space (`GetDiskFreeSpaceA(NULL)`, the current drive):
   - in a mission: needed = more than 0x190000 bytes free (computed in
     clusters), **or** the file being replaced is already ≥ 0x190000
     bytes;
   - between missions: needed = more than 0x200 bytes free, or the file
     being replaced is ≥ 0x200 bytes.
   - Otherwise open **NO_FREE_SPACE** ("Warning : No space on drive to save
     the game", OK → BackMenu) and stop. The SAVE menu stays open below.
4. Renumber so that the new save becomes slot 0:
   - new slot (t + 1 == count): rename `S<k>` → `S<k+1>` for
     k = count−2 down to 0. Missing files are logged ("Save file %s
     missing") and skipped;
   - overwrite (t < files): delete `S<t>`, then rename `S<k>` → `S<k+1>`
     for k = t−1 down to 0.
5. Write `S0.SAV` (`FUN_004414d0(path, gGameState == 2, text)`). In a
   mission this is a full save (kind 1). Elsewhere (the globe) it is a
   between-missions save (kind 0).
6. Append the text to the list (not visible, the menu closes). Play the
   house voice `*_GAMESAVED` (`S_` / `H_` / `O_` by house, doc 12;
   `FUN_0046f900`).
7. Close: direct save → `BackMenu` (closes SAVE). From the confirm pop-up
   → `GoBackMenu 2` (closes CONFIRM_OVERWRITE and SAVE). Either way the
   player is back on the menu that opened SAVE (the in-game menu or the
   globe), **not** back in the game. No message box.

Effects of these rules (all from the code above):
- Overwriting moves that save to the top of the list.
- Selecting a row and typing a new description still overwrites the
  selected save. The confirm question is asked first.
- Typing the description of an existing save overwrites that save, even
  when a different row is selected.

## 5. LOAD menu (`Load.uil`)

Layout as SAVE, without the edit box: TEXTBOX_UL loadTitle `TitleLoad`
"LOAD MISSION"; plain **LISTBOX loadListBox** (192,75) 256×225 (highlight
LightRed, buttons LightRed / VDarkRed / Red / DarkRed) with `GetGameSaves`
only; **Load** (217,339) 50×15 → `UILoadGame loadListBox`; **Cancel**
(352,339) → BackMenu.

**UILoadGame** (0xCC, `FUN_00475740`), for the named list in the top menu:
- **Nothing selected → nothing happens.** There is no message.
- File = `S<sel−1>.SAV` (`P` in skirmish). In the campaign only, if the CD
  check is active (`0x4e7ae0` set, `0x797e30` clear) and the Dune 2000 CD
  is not found (`FUN_004a61d0`), it returns without doing anything.
  The two flags (verified 2026-10-16): `0x4e7ae0` is a constant 1 in
  `.data` (only read, by `FUN_00482650`, `0x482620` and here): the CD
  check is on. `0x797e30` is never written (no write and no data
  reference): always 0. So in 1.06 the campaign Load always needs the CD
  (`FUN_004a61d0`). d2k-faithful reads the player's data folder and has no
  CD check (CLAUDE.md §3), so this branch does not apply.
- The kind is taken from the **row colour**: White = in-mission save,
  anything else = between-missions save.

| Current state | White row (full save) | Other row (between missions) |
|---|---|---|
| In a mission (gGameState 2) | free the map surfaces (`FUN_0046a410`, `FUN_0046a960`), gLoadSavedGame `0x5178bc` = 0, `FUN_004418b0(path, 1)` loads at once, `QuitMenu` (all menus close, the game continues from the loaded state), voice `*_GAMELOADED` | gUseCampaignSelection `0x4e7ad0` = 1, queue `QuitAndJumpToMenu LOADMISSION`, gGameState = 1 (the mission ends), read only header + mission + side from the file |
| Not in a mission (menus, globe) | copy the path to `0x516548`, gLoadSavedGame = 1, gGameState = 2, `QuitMenu`. The game loop (`FUN_00448550`) then skips the normal mission load (`.mis` / map / `game01.dat`), calls `FUN_004418b0(0x516548, 1)` and plays `*_GAMELOADED` | gUseCampaignSelection = 1, `FUN_004418b0(path, 0)` (header + common block: mission, side, game type, difficulty, music, speeds, volumes; restarts that music), `QuitAndJumpToMenu LOADMISSION` |

`QuitAndJumpToMenu LOADMISSION` opens MAINMENU and then, for a campaign
mission < 10, the GLOBE of the saved house at the saved mission number
(doc 04). That is where a between-missions save resumes.

The full loader ignores its own header check result here. If size or
version is wrong it reads only the common block and the game starts with
stale state. Do not copy this.

## 6. DELETEGAME (`delete.uil`) and CONFIRM_DELETE (`ConfirmD.uil`)

- DELETEGAME: like LOAD. Title `TitleDelete` "Delete Mission"; plain LISTBOX
  deleteListBox (highlight **DarkRed**); **Delete** (217,339) 50×15 →
  `JumpIfSelected deleteListBox CONFIRM_DELETE`; Cancel → BackMenu.
- **JumpIfSelected** (0x19, `FUN_00480dc0`): opens the menu only if the
  list has a selection. Otherwise nothing happens.
- CONFIRM_DELETE: GROUPBOX (215,200) 210×60 image `Delbox`; TEXTBOX_UL
  (230,220) 180×20 `AreYouSure` "Are You Sure?"; **Yes** (239,240) 70×15
  (but8) → `DeleteGame deleteListBox`; **No** (321,240) 70×15 → BackMenu.
- **DeleteGame** (0xD1, `FUN_004760c0`): looks up the list in the menu
  **below** the top one (DELETEGAME, under the pop-up). If a row is
  selected: delete `S<sel−1>.SAV`, then rename each later file down by
  one (`S<k+1>` → `S<k>`) until a number is missing, and remove the row
  (`FUN_00487420`, §3.5). Always ends with `BackMenu` (closes the pop-up;
  DELETEGAME stays open with the updated list). No sound.

## 7. Pop-ups and texts

| Menu | Layout | Text key → English |
|---|---|---|
| SAVE_DESC | GROUPBOX (200,140) 240×130 `box4`; TEXTBOX (220,170) 200×20; OK (250,225) 140×20 But6 → BackMenu | `HaveToEnterSaveDesc` → "You need to enter a description string for the save file" |
| NO_FREE_SPACE | same layout | `NoFreeSpace` → "Warning : No space on drive to save the game" |
| NO_SAVED_GAMES | same layout | `NoSavedGames` → "There are no saved games to choose from." |
| CONFIRM_OVERWRITE | GROUPBOX (199,148) 240×130 `box4`; TEXTBOX (220,172) 200×20 (lo colour Black); Yes (234,233) / No (329,233) 70×20 But8 | `OverwriteGame` → "Are you sure you want to overwrite this game save?" |
| CONFIRM_DELETE | §6 | `AreYouSure` → "Are You Sure?" |

Other keys: `TitleSave` "SAVE MISSION", `TitleLoad` "LOAD MISSION",
`TitleDelete` "Delete Mission", `EmptySlot` "Empty slot", `Save`,
`Load`, `Delete`, `Cancel`, `ConfirmYes` "Yes", `ConfirmNo` "No", `OK`
"Ok", menu buttons `LoadGameFile` / `SaveGameFile` / `DeleteGameFile`
("Load / Save / Delete Mission"), skirmish `PracticeLoad` "Load". The
pop-up text boxes and buttons use JHighLightText and ButtonUp/DownColour.
The three list menus use the rubber font colours.

## 8. Implementation notes for us

- Keep the slot behaviour (newest = slot 0, renumbering on save and
  delete) and the "kind" distinction. Rows are coloured from the kind:
  White for in-mission saves, JHighLightText for globe saves. We may use
  our own file format and folder, as long as the menus show the same list
  in the same order.
- Reproduce the list box quirks that a player can see: the "Empty slot"
  row always at the top of the SAVE list, the selected row highlight, no
  wheel and no keyboard scrolling, one-row arrow steps on release, paging
  on the track.
- After Save the player returns to the menu that opened SAVE. After Load
  of a full save all menus close.
- Do not reproduce the header-check bypass in §5 or the text-mode read in
  §4.1. These are crash/robustness fixes that do not change gameplay.

## 9. Our implementation

- `src/app/save_games.*`: the files. The 0xD8 header and the 32-byte
  common block are written exactly as above (so the list, kinds and
  slot numbering behave the same, and an original save's description
  shows in the list); after the common block an in-mission save holds
  `D2KF`, our version, the mission file name and `GameScreen::save_blob()`
  (the simulation state of `src/sim/save_state.cpp` plus camera, crater
  jitter, bookmarks, sidebar page / scrolls, music track). An original
  exe save is listed but cannot be loaded (logged).
- Folder: `<user data>/GameSave`.
- `src/app/menu_runner.cpp`: LISTBOX / LISTBOXEMPTYSLOT (drawing, rows,
  arrows on release, track paging, thumb drag, right release clears),
  EDITBOXFOCUS, and the commands GetGameSaves, CopyToEditBox,
  UICheckSaveGame, UISaveGame, ForceButtonDown, ParseEditBox, UILoadGame,
  JumpIfSelected, DeleteGame, SetFromUIStringSpecial; JumpToMenu LOAD
  checks slot 0. A full save loaded in a mission or from the menus starts
  the saved mission and restores the state (`GameScreen::load_saved`,
  `*_GAMELOADED`, the saved speeds and volumes).
- Not reproduced: the CD check, the header-check bypass, text-mode reads.
