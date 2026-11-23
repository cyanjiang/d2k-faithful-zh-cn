# Skirmish: setup menu, launch, end and saves (1.06)

Sources are 1.06 addresses. Decompiles were made with GhidraMCP; the UI
tables come from `re/dump_ui_tables.py`. Related docs: 04 (menu flow),
15 (music), 19 (mission script, end checks), 20 (in-game menu), 22 / 23
(AI), 24 (score), 27 (saves).

Summary: the skirmish is the campaign engine with a fixed mission file,
`maps\_prac.mis`, and a map from `Data\maps`. The menu settings are
applied in two places: the mission loader (`FUN_0044f8e0`, tech and
credits) and the map loader (`FUN_00469790`, AI slots, houses, start
positions, MCV, starting units, worms). There is no briefing and no movie.

## 1. How the player gets there

| From | Element | Commands |
|---|---|---|
| MULTIPLAYERTYPE (`MPType.uil`) | `skirmish` | `Dune2000CdCheck`, `SetValueS2To gNetNetworkType 0`, `SetValueECTo gGameType GameTypeSkirmish`, `JumpToMenu SKIRMISH` |
| MAINMENU, no network (`DAT_00516204`) | `practice` (created by `FUN_004359a0`, doc 03) | 0xB0 `SetValueS2To gNetNetworkType 0`, 0xB6 `SetValueECTo gGameType GameTypeSkirmish`, 0xE0 `JumpToMenu SKIRMISH` (no CD check) |
| SCORE (after a skirmish) | `continue` | `BackMenuAndJumpTo LOBBY_SCREEN` → close SCORE, open **SKIRMISH** when gGameType = 1 (`FUN_004738b0` case 0xDC) |

Our `src/ui/menu_hooks.cpp` gives the `practice` button all three
commands.

`menus.uib`: SKIRMISH = `Skirmish.uid`, enter 0 (FADE), leave 1 (FADE).

## 2. `Skirmish.uil` (verified from the data)

Background `multiUI`. The file has several **headerless blocks** (doc
`formats/ui.md`): a 640×400 group box, a `woodcov` group box at (276,362),
the "Concrete" text box, the **concrete checkbox** (`gNetConcrete`), and
the difficulty **VALUEBAR** at (241,258). The exe has no `gNetConcrete`
global at all (no such string in the binary), so the concrete option is
dead in 1.06. These blocks must not be created.

### 2.1 Controls

| Element | Type, pos, size | Commands |
|---|---|---|
| `tooltip` | TEXTBOX 0,0 4×4 | none (the menu tooltip box; keys `tooltip_<element>` exist in text.uib) |
| `sideName` | TEXTBOX 272,83 100×12 | `GetStringIfValueU1Init gNetPlayerSide 0 Atreides` / `1 Harkonnen` / `2 Ordos` |
| `yourName`, `yourSide`, `yourColour`, `map`, `unitCount`, `techLevel`, `credits`, `aiPlayers` | TEXTBOX_L labels | text keys only |
| `crates` | TEXTBOX_L 465,282 | text `Crates`, `GetStringIfValueBFInit gNetCrates 0 CratesOff` / `1 CratesOn` |
| `worms` | TEXTBOX_L 465,307 | text `Worms`, `GetStringIfValueU1Init gNetWorms 0..3 WormsOff/Low/Medium/High` |
| `nameEdit` | EDITBOXFOCUS 60,53 120×25 | `NoOtherChars`, `GetStringInit gNetPlayerName`, `SetFromUIStringSpecial gNetPlayerName` |
| `mapList` | LISTBOX 130,115 385×105 | `GetMapList`, `GetMapFromList`, `StaySelected` |
| `unitCountBar` | VALUEBAR 226,272 90×12, `0 10 1 1` | `GetValueU1Init gNetUnitCount`, `SetValueU1Special gNetUnitCount` |
| `techLevelBar` | VALUEBAR 226,290, `1 7 1 1` | `GetValueU1Init gNetTechLevel`, `SetValueU1Special gNetTechLevel` |
| `creditBar` | VALUEBAR 226,309, `2500 7000 500 7000` | `GetValueU2Init gNetStartingCredits`, `SetValueU2Special gNetStartingCredits` |
| `aiBar` | VALUEBAR 226,326, `1 5 1 1` | `GetValueU1Init gNetAIPlayers`, `SetValueU1Special gNetAIPlayers` |
| `atreidesCheck` / `ordosCheck` / `harkonnenCheck` | CHECKBOX 270/305/340,50 32×32, gfx `a_small`/`o_small`/`h_small` | `SetValueU1To gNetPlayerSide 0/2/1`, `CheckIfValueU1Init` + `CheckIfValueU1`, `UnCheck` the other two, `SetStringInTextBox <House> sideName` |
| `crateCheck` | CHECKBOX 442,282 20×18, `Bicon` | `CheckIfValueBFInit gNetCrates TRUE`, `SetValueBFTo gNetCrates TOGGLE` |
| `wormsCheck` | MULTISTATEBOX 442,307 20×18, 4 states, gfx `Wicon_0`..`wicon_3` | `CheckIfValueU1Init gNetWorms` (no value: state = the global), `SetFromUIValueU1To gNetWorms` |
| `easyCheck` / `normalCheck` / `hardCheck` | CHECKBOX 207/283/358,245 72×15 | `EnterText Easy/Normal/Hard`, `SetValueU2To gDifficultyLevel 0/1/2`, `CheckIfValueU2Init` + `CheckIfValueU2`, `UnCheck` the others |
| `skill` | TEXTBOX 20,229 | `EnterDifficulty` |
| colour checks (6) | CHECKBOX 445..545,53 15×25, step 20 | `SetValueU1To gNetPlayerColour n`, `CheckIfValueU1Init/U1 n`, `UnCheck` the others; n = 0,1,2,3,5,6 |
| `start` | BUTTON 477,371 90×15, text `Continue` ("Start") | `ParseEndCommands`, `StartGame mapList` |
| `cancel` | BUTTON 286,371 | `SetValueECTo gGameType GameTypeCampaign`, `BackMenu` |
| `loadMission` | BUTTON 93,371, text `PracticeLoad` ("Load") | `JumpToMenu LOAD` (lists `P<n>.SAV`, doc 27) |
| `chosenMap` | TEXTBOX_L 170,97 400×20 | text `NoMapSelected`, `EnterText gNetMapDesc`, `RefreshValueCH gNetMapDesc` |

Colour checkboxes: the names say Red / Green / Blue / Grey / Purple /
Black, and the `UnCheck` lines also name `colourYellowCheck` and
`colourDYellowCheck`, which do not exist. What is drawn is
`NetColour<n>` from `colours.uib`. Colours 4 and 7 have no checkbox, and
the AI never gets them (§4.2). The shades come from the data, not from
the names: each box has border `CheckBorder` (255,255,255), on colour
`NetColour<n>` and off colour `NetFadedColour<n>`, no image, and
`colours.uib` (B, G, R order) gives NetColour0 (104,128,160) blue-grey,
1 (208,80,72) red, 2 (104,160,128) green, 3 (120,16,136) purple, 5
(128,56,16) brown, 6 (168,136,8) gold; (4 = (136,136,136) grey, 7 =
(144,96,72)). **NetFadedColour<n> is the same RGB as NetColour<n>**, so a
box looks the same checked or not except for its frame: the checkbox draw
(`FUN_0048adc0`, doc 03) frames a checked box in the border colour (white)
and an unchecked one in the off colour. So the element names (Red, Green,
Blue, Grey, Purple, Black) do not match the shades; colour 0 is the
Atreides blue-grey.

Labels: `crates` and `worms` get their text at creation (`GetStringIf…Init`)
and again from the setter callbacks of gNetCrates / gNetWorms when the
value changes (doc 55 §3; this corrects an earlier reading).
`sideName` is refreshed by
`SetStringInTextBox` (0x12, `FUN_00474540`: finds the element by name in
the top menu and sets its text key).

### 2.2 Commands of this page (all handled by our `menu_runner.cpp` now)

| Command (id) | Exe | Semantics |
|---|---|---|
| `GetMapList` (0x27, init) | `FUN_0047a450` | §3.1. Listed in `is_init_command` but does nothing |
| `GetMapFromList` (0x28, special) | `FUN_00490bb0` via `FUN_00483b80` | §3.2 |
| `StaySelected` (0x29, init) | `FUN_004910d0` | a list with items and no selection selects row 1 and scrolls to the top |
| `StartGame` (0x2F) | `FUN_00479e80` | §5. The argument (`mapList`) is ignored |
| `SetStringInTextBox` (0x12) | `FUN_00474540` | see above |
| `GetStringIfValueU1Init` / `BFInit` (0x8F / 0x92) | init | the text box text key = arg 3 when global(arg 1) == arg 2 |
| `GetStringInit` (0x79) | init | edit box text = the string global |
| `SetFromUIValueU1To` (0xBB) | `FUN_0047e470` | global = the multistate box's state |
| `CheckIfValueU1Init` with no value on a MULTISTATEBOX | init | state = global; a left **press** advances the state by 1 and wraps to 0 at the count (doc 55 §1.4, `0x482a0e`). Ours: done |
| `SetValueBFTo … TOGGLE` | 0xB5 `FUN_0047df90` | TOGGLE flips the BF global. Our generic `SetValue` stores the literal "TOGGLE" |
| `EnterText g<Name>` on a TEXTBOX | `FUN_00480e00` type 7 | an argument that names a char global (`FUN_0048e5b0`) shows the global's string. Ours looks it up as a text key |
| `RefreshValueCH` (0x6A) | none | **does nothing in 1.06.** It is a non-init command (table flag 0), but the click executor `FUN_00482850` has no case for 0x62..0x6A (its switch falls to the default), the init executor `FUN_00484000` has none either, and the element refresh `FUN_00484cc0` (run by `FUN_00474b60` on network notifications) handles 0x31, 0x54, 0x55, 0x62 and the RefreshValue S1..BF commands 0x63..0x69, but not 0x6A. `FUN_00480a00` is `EnterText` (type 6/7 boxes), called from `FUN_00484000`. The `chosenMap` text is set by `EnterText gNetMapDesc` at creation and by `GetMapFromList` (§3.2). Ours ignores it |
| `GetValueU1Init` / `U2Init` | `FUN_00484000` 0x74 / 0x75 | **clamps the global itself** to [min, max] and writes it back. Ours clamps only the bar |
| menu tooltips (`tooltip` box, `tooltip_*` keys) | `FUN_004712c0` | doc 55 §4. Ours: done (`MenuRunner::update_tooltip`) |

Already handled: `SetValueU1To`/`U2To`, `SetValueU1Special`/`U2Special`,
`CheckIfValue*`, `UnCheck`, `SetFromUIStringSpecial`, `ParseEndCommands`
(no `…AtEnd` setters on this page), `EnterText` (labels), `JumpToMenu`,
`BackMenu`, `Dune2000CdCheck`.

## 3. The map list

### 3.1 `GetMapList` (`FUN_0047a450`, verified)

Only for a LISTBOX (type 3). For every entry of `text.uib`, **in table
order**, whose key starts with `map_` (`_strncmp` 4, `0x4e84cc`):
1. Look for the file `<gMAPS_RES_PATH><key+4>.map` (`%s%s.map` `0x4e84c0`,
   path `0x5136e8`, `_findfirst` `FUN_004b22b0`). Missing → skipped.
2. Add a row with the entry's **value** (the description, e.g. "Habbanya
   Ridge (SM 2 player)"), colour White.
3. If `key+4` equals `gNetMap` (`0x797638`, case-insensitive), remember
   the row (1-based).

Then: no match → `FUN_00493050(first listed map)` sets `gNetMap` to it and
`gNetMapDesc` (`0x797aa8`) to text("map_" + name) or `NoMapSelected`, and
selects row 1. Match → select that row and scroll it into view.

The list is not filtered by player count. The "(SM 2 player)" text is
only a description. Every 1.06 skirmish map has exactly 8 start markers
(§4.3), so any map takes 1 + 1..5 players.

1.06 order (36 rows, 36 files): 2PLAY1..2PLAY10, 3PLAY1, 4PLAY1..4PLAY7,
6PLAY1, 6PLAY2, 8PLAY1, 8PLAY6, 8PLAY2..8PLAY5, 2PLAY11..2PLAY14, 4PLAY8,
4PLAY9, 6PLAY5, 6PLAY3, 6PLAY4, 4PLAY10. There is no per-map .MIS or .INI.
The only per-map data is the `.MAP` file and the tileset table in §4.1.

### 3.2 `GetMapFromList` (`FUN_00490bb0`, verified)

Runs after a list click (special command).
- No selection, no rows: `gNetMap` = "", `gNetMapDesc` = text
  `NoMapSelected`.
- No selection, rows: select row 1 and scroll.
- Then: find the first `text.uib` entry whose **value** equals the
  selected row's text. Set `gNetMap` = key + 4 (`map_2PLAY1` → `2PLAY1`)
  and `gNetMapDesc` = the value.
- Copy `gNetMapDesc` into the `chosenMap` text box (found by name in menu
  `DAT_005161fc`) and mark it dirty.
- `gNetNetworkType` (`0x4e3b10`) 1/3, 2, 4 send the map to the other
  players. 0 (skirmish) returns.

## 4. Settings

### 4.1 Globals, defaults and limits

Defaults are set in the UI manager constructor (`FUN_00470710`,
0x4707c1..0x47082e). Then `dune2000.cfg` is read (`FUN_00478c20`, called
from the constructor at 0x470a68) and the values are checked.

| Setting | Global | Size | No cfg | Cfg check (bad → value) | Bar range |
|---|---|---|---|---|---|
| Unit count | gNetUnitCount `0x4e3b00` | u8 | 6 | > 10 → 0 | 0..10 |
| Tech level | gNetTechLevel `0x4e3b04` | u8 | 7 | 0 or > 7 → 1 | 1..7 |
| Credits | gNetStartingCredits `0x4e3b08` | u16 | 4000 | outside 2500..7000 → 2500 | 2500..7000 step 500 |
| AI players | gNetAIPlayers `0x4e3b0c` | u8 | 0 | > 5 → 1 | 1..5 |
| Crates | gNetCrates `0x6b9834` | BF | 0 | not 0/1 → 0 | checkbox |
| Worms | gNetWorms `0x6b9838` | u8 | 0 | > 3 → 0 | 4 states |
| Name | gNetPlayerName `0x6b93f8` | 100 chars | "" | none | edit box |
| House | gNetPlayerSide `0x6b9840` | u8 | 0 (Atreides) | > 2 → 0 | 3 checkboxes |
| Colour | gNetPlayerColour `0x6b9844` | u8 | 0 | > 7 → 0 | 6 checkboxes |
| Difficulty | gDifficultyLevel `0x4e8bf0` | u16 | 1 | **not in cfg** | 3 checkboxes |
| Map | gNetMap `0x797638` / gNetMapDesc `0x797aa8` | string | "" | **not in cfg** | list |

Opening the page clamps the four bar globals into range
(`GetValue*Init`), so with no cfg the AI count becomes 1 at once. doc 02's
cfg table gives the cfg fallbacks (1 / 2500 / 1), not these defaults.
The cfg is written by `FUN_00478fa0`, with the same fields.

### 4.2 Houses, colours, teams (`FUN_00448550`, 0x44a54e, verified)

At every mission start in skirmish (also on restart):
- House table `0x8cd4f0[i]` = i, then `[0]` = gNetPlayerSide.
  **gSideId = 0**: the human is always slot 0.
- Player records (`0x6b922d`, stride 0x3C, 6 records; byte 0 = colour):
  record 0 = gNetPlayerColour. Records 1..5: `rand() & 7`, retried while it
  is 4 or 7, equal to an earlier record's colour, or equal to the
  player's (the shared `rand`, `FUN_004b1810`).
- `FUN_00461c00`: side colour `0x5175d8[i]` = record i's colour in
  skirmish (side index in the campaign), for i = 0..7. There are 8 lobby
  records: `FUN_004481b0` (from `FUN_004470a0` at every set-up) zeroes the
  colour byte of all 8, and no skirmish step fills records 6 and 7, so
  sides 6 and 7 get colour 0 (doc 52 §2.1 step 7). Side 7 owns the map
  worms (doc 52 §2.6). Ours: `sim/game_setup.cpp`.
- AI houses (`FUN_00469790`): for slots 1..gNetAIPlayers, house =
  `rand() % 3`, so AI can repeat each other's house or the player's.
  These are written to `0x8cd4f0` and the player's `+0x24253`.
- Teams: none. `_prac.mis`'s alliance table (`0x798830`) has 1 (enemy)
  everywhere except the diagonal, so it is every side for itself.

**Random seed:** `FUN_004470a0` seeds `rand` (`FUN_004b1800`) for the
campaign (time) and the network games. **Not for skirmish**: the sequence
continues from whatever ran before. **Verified (2026-10-14):** `rand`
(`FUN_004b1810`) is one global LCG state at `0x4ed870`, initial value
**1** in the file (no per-thread CRT state). The only other `srand`
caller is `FUN_00479b30` (network). Every `rand` caller (the thunk at
`0x448460` and the direct calls) is in-game code, network code, or the
network-join colour re-roll (`FUN_0047ad60`); none runs in the
single-player front end. So the first skirmish after a launch, with the
same settings, rolls the same colours, AI houses and starts every time;
after a campaign mission (clock seed) or an earlier game it differs.
Ours does the same (`app::GameSession::rng` starts at 1, lives for the
process). A reference check is optional: launch, Practice with fixed
settings, note the starts / colours, quit, launch again, repeat.

### 4.3 Map set-up (`FUN_00469790`, verified, register args partly lost)

Map special values (the second u16 per cell): **23 (0x17) = start
point** (`0x6d7ba8` / `0x6d7bac` x/y, up to 8; anything other than 8 in
skirmish logs an error), 41..45 = spice bloom variants, 1 / 2 = spice
fields. Other values index the object table `0x6d7de8`. In the campaign
these objects are placed. **In skirmish only the worms are**: the objects
whose unit kind (`0x77e27d[type * 0x100]`) is 5 (sandworm; map value 20
in 1.06), at most gNetWorms of them in all.

Then:
1. Number of humans `0x7984c0` = 1. All AI records (`0x4f4118`, stride
   0x1DB8) get +1 (active) = 0. Records 1..7 are copied from record 0 (the
   `_prac.mis` template: active 1, EnablePractice +3 = 1, BuyUnits +4 = 1,
   UpgradeBuildings +5 = 1), with +0 = the slot.
2. Slots 1..gNetAIPlayers: house (above), +0 = slot, **+1 = 1** (AI on).
3. MCV type = the unit type with kind 6 (the last one found).
4. For each player p = 0 .. gNetAIPlayers (human first):
   - start: p = 0 → `rand() % 8` (number of starts). Others → the unused
     start whose smallest distance to the used starts is largest:
     `ftol(sqrt(dx² + dy²))` in cells, strictly larger wins, so the lowest
     index wins a tie (doc 52 §2.5, `0x469ebf..`).
   - the MCV at the start (`FUN_004a60e0` cell → position,
     `FUN_00455870` CreateUnit);
   - for c = 0 .. gNetUnitCount − 1: TECHPOS entry
     `0x6b8868[(c + gNetTechLevel × 10) × 5]` = {type Atreides,
     Harkonnen, Ordos, dx, dy}; type 0xFF = none. The unit for the slot's
     house goes at start + (dx, dy). A house > 2 logs an error.

TECHPOS is `Data\bin\TECHPOS.BIN` (500 bytes = 10 tech rows × 10 entries
× 5 bytes, loaded by `FUN_00454e00`; text form `Techpos.dat`). Rows 1..7
are filled, 0 / 8 / 9 are empty. Example: tech 1 = LIGHT_INF ×2, TRIKE
(Ordos RAIDER), TRIKE, LIGHT_INF ×2, TRIKE, TRIKE, LIGHT_INF ×2. Tech 7,
entry 9 = GRENADIER / SARDAUKAR / SIEGE_TANK.

### 4.4 Tech and credits (`FUN_0044f8e0`, LAB_0044fff4, verified)

After `_prac.mis` is read, for all 8 sides: tech `0x6b87c8[i]` =
gNetTechLevel and credits `0x6b87d0[i]` = gNetStartingCredits. **Every
side other than gSideId** then gets **7000 (easy), 10500 (normal) or
14000 (hard)**. The AI's money does not follow the Credits bar. Hard also
scales the AI build rate (doc 22 §2.1).

### 4.5 Crates and worms in the game

- Crates: at mission start, `0x548048` = VARS `NumberOfCrates` (1 in
  1.06) when gNetCrates is on, else 0 (×humans if `CratesPerPlayer` and
  more than 1 human; 0 in 1.06). `FUN_0044ea70` (each tick of the game
  loop) keeps the number of live crates topped up to it (`FUN_0044e7c0`
  spawns one). So doc 19 §4.3's tie-break (`0x548048` = 0) only works with
  crates off. Crate contents: doc 54 (`CrateCash` = 1000).
- Worms: gNetWorms caps the map's worms (§4.3) and is read again in the
  game (`FUN_0046be50`, the worm logic; doc 17 §6.2: the Thumper is
  offered only when it is non-zero).

## 5. Start → mission (verified)

1. `ParseEndCommands` (none here), then `StartGame` → `FUN_00479e80`:
   gGameType = 1 → **gGameState = 2** and `FUN_004752c0(gNetMap)`. The
   network part that follows returns at once (`DAT_00798490` = 0). There
   is **no check** that a map is chosen or a name entered (only network
   games check names).
2. `FUN_004752c0` for gGameType 1: map path `0x6f9840` = `<gNetMap>.map`,
   mission path `0x6dc470` = `prac.mis`. With compiled missions
   (`DAT_004eb000`) it opens `_prac.mis`, under the maps path `0x5136e8`
   (the campaign uses the missions path `0x513650`). If the mission file
   exists but the map does not, error "File gMissionMap %s does not
   exist" and nothing starts. If the mission file itself is missing, the
   campaign asks for the CD (`FUN_00492c40`) and the skirmish skips the map
   check and continues (the mission loader then fails). This cannot happen
   with a 1.06 install: `_PRAC.MIS` is in our start-up manifest
   (`src/platform/manifest_106.inc`), so the game does not start without
   it. Then
   **JumpToMenu LOADING** (unless already open) and the navigation runs
   at once. The campaign instead jumps to `MVE_<HOUSE>_<n>` (briefing
   movie): skirmish has no briefing, no PRECAP, no movie.
3. The front end ends (gGameState 2). `FUN_00448550` starts the mission:
   houses / colours (§4.2), `FUN_0044f8e0("_prac.mis")`, then
   `FUN_0044c6d0(map, 1)` and `FUN_00469790` (§4.3).
4. `FUN_0044f8e0` in skirmish keeps the house table and the player's
   credits from before the file read, so the file's houses `0..7` do not
   apply. Tileset: the file's `Bloxbgbs` / `tileatr3`, replaced by map
   name (case-insensitive, `gNetMap`):

| Maps | Tileset | Attributes |
|---|---|---|
| 2Play11 | Bloxtree | tileatr1 |
| 2Play12 | Bloxbase | tileatr2 |
| 6Play3 | Bloxwast | tileatr4 |
| 2Play13, 4Play9, 4Play10, 6Play4, 6Play5, 2Play14, 4Play8 | Bloxxmas | tileatr7 |
| all others | Bloxbgbs | tileatr3 |

   `Bloxxmas` exists only as `BLOXXMAS.R8` in 1.06 (no R16). The 16-bit
   loader special-cases it and converts the R8 tiles; see §9.
5. `_prac.mis` (68,066 bytes, the normal compiled layout of doc 19):
   tech 0, credits 0, houses 0..7, all enemies, 0 events, 0 conditions,
   timer −1. An `[EVENTS]` block in an MP / skirmish map would be an
   error ("Shouldn't have events in MP map"). The AI checks for tech /
   credits 0 are skipped in skirmish (doc 22 §2.1).
6. Music: `arakatak.aud` (doc 15), not the campaign table.
7. Every tick the script step runs the skirmish default win / lose check
   (doc 19 §4.3), not .MIS conditions.

## 6. In-game menu and end of game

**INGAME_SKIRMISH** (`InGameSK.uil`, 3/3): image `box2a`. Load / Save /
Delete / Settings (`JumpToMenu OPTIONS`, comment "was MP_OPTIONS") /
Abort (`CONFIRMABORT`) / Resume. Like INGAME without the briefing
button. Our game screen already opens it (`game_screen.cpp`, gGameType 1).

End paths (`FUN_004738b0`, verified by disassembly):

| Event | Navigation | Stack opened |
|---|---|---|
| Won (doc 19 §4.4) | QuitAndJumpToMenu SCORE | MAINMENU, **SCORE** |
| Lost | REPLAY_MISSION over the game (`Replay.uil`) | Yes → `RestartGame`; No → `QuitAndJumpToMenu LOADMISSION` → MAINMENU, **SCORE** (0x47402d: `push "SCORE"`) |
| Abort (CONFIRMABORT) | QuitAndJumpToMenu ABORT_MP_GAME | MAINMENU, MULTIPLAYERTYPE, **SCORE** (0x473e0d / 0x473e19) |
| Restart (REALLY_RESTART) | `RestartGame` | the same map again; houses, colours, starts are rolled again (the set-up runs again) |
| SCORE Continue | BackMenuAndJumpTo LOBBY_SCREEN | SCORE closed, **SKIRMISH** opened |

These correct doc 19 §5: "No" after a skirmish loss opens MAINMENU and
SCORE (not MAINMENU only), and the abort SCORE string is confirmed.

So after any skirmish the player sees SCORE, then the skirmish setup
again. Cancel there leaves to MAINMENU (or MULTIPLAYERTYPE after an
abort) and sets gGameType back to 0.

**SCORE** (`score.uil`, enter 0 / leave 1; `score.aud` starts as for any
SCORE* menu, doc 04): background `scoreui`, the MoonSpin animation,
`continue`. The rest is built by `FUN_0043d7e0` → `FUN_0043dcb0` when
the menu is created (`FUN_004359a0`):
- group `scoreMP`; title `TitlePracticeScore` ("Practice Game Results")
  in skirmish, else `TitleMultiScore`; headings PlayerNames, Casualties,
  Buildings, Spice, Inflicted, Received, Lost, Destroyed, Harvested.
- one row per side that is a human, has an active AI, or is the local
  side, **sorted by finishing place** `0x797b70` (`FUN_0044c520`). Rows
  start at y = 101, 24 pixels apart. Per row: a house icon (`mp_at` /
  `mp_hk` / `mp_od`) in `NetColour<colour>`, the name (gNetPlayerName for
  the human in skirmish, `UI_Computer` for an AI), units destroyed
  (`0x797c10`), units lost (`0x797c04`), buildings lost (`0x797c08`),
  buildings destroyed (`0x797c14`), spice (`0x797c0c`).
- a score per row is also computed from shares of the totals (constants
  `0x4dedc4/c8/cc`) plus a place bonus, and **never shown**: nothing on
  the page reads it (doc 56 §1.3). The full page: doc 56.

## 7. P<n>.SAV skirmish saves

Doc 27 covers the file. The skirmish-relevant fields are already in the
common body: gGameType, gSideId, difficulty, houses `0x8cd4f0`, the side
colours `0x5175d8`, the AI records `0x4f4118` (practice flags, active
slots), tech / credits `0x6b87c8`, the tileset and map / mission paths,
`0x548048` (crate target) and gNetWorms `0x6b9838`.

**Not saved:** gNetMap, gNetMapDesc, gNetPlayerName, gNetStartingCredits,
gNetTechLevel, gNetUnitCount, gNetAIPlayers, gNetCrates, gNetPlayerSide,
gNetPlayerColour, and the player records `0x6b922d`. Consequences (from
the code):
- the SCORE after a loaded skirmish shows the current gNetPlayerName (the
  row name is read when SCORE is built, doc 56 §1.5);
- a Restart after loading runs the set-up again with the **current** menu
  settings (houses, colours, AI count, units, tech, credits, crates,
  worms). The map comes from the saved map path `0x6f9840`. Every restart
  sets `0x797e1c` (RestartGame `0x44c472`, also `0x449d46` / `0x44a005`),
  so `FUN_0044c6d0` skips the tileset load (`0x44c6e9`) and the loaded
  tiles stay. The tileset **name** and the attribute file are chosen again
  by `FUN_0044f8e0` from **gNetMap** (`0x44fdea..`). So if the menu's map
  (gNetMap) has another tileset than the saved map, the restarted game
  shows the saved map's tiles with the menu map's attribute file (and a
  later save stores that name). Ours restarts the saved map with its own
  tileset and attributes (`gMissionMap` from the save); this quirk is not
  reproduced.

Our save must store the same blocks. Our extra simulation state must
cover the crate timers and the AI practice state.

## 8. Open questions

- Done (2026-10-16): the multistate box click cycle and the crates /
  worms labels (doc 55 §1.4, §3).
- Done (2026-10-14): the unseeded `rand` in skirmish (§4.2), from the
  exe (one global state, initial 1, no front-end caller).
- Done: the start-point distance (doc 52 §2.5), the menu tooltips (doc 55
  §4), the worms' owner (side 7, doc 52 §2.6), Bloxxmas (§9),
  `RefreshValueCH` (no effect, §2.2), sides 6 and 7's colours (§4.2).
- The SCORE (skirmish / MP) page in full, and crate contents: separate docs.

## Implementation checklist

1. `menu_hooks.cpp`: give `practice` the two missing commands
   (`SetValueECTo gGameType GameTypeSkirmish`, `JumpToMenu SKIRMISH`).
2. Menu globals: add the gNet* defaults of §4.1 (6 / 7 / 4000 / 0 / off /
   0 / Atreides / 0) and read / write them in our settings with the cfg
   checks. Difficulty and map are not persisted.
3. Do not create headerless blocks (concrete box, difficulty bar), so
   `gNetConcrete` never exists.
4. Menu commands (§2.2): GetMapList, GetMapFromList, StaySelected,
   SetStringInTextBox, GetStringIfValue*Init, GetStringInit,
   SetFromUIValueU1To with a clickable MULTISTATEBOX, BF `TOGGLE`,
   `EnterText` / `RefreshValueCH` with a global name, and the GetValue*Init
   write-back clamp.
5. `StartGame`: gGameState 2, map `<gNetMap>.map` and `_prac.mis` from
   `Data\maps`, straight to LOADING (no briefing / movie), gGameType 1.
6. Mission set-up for gGameType 1: gSideId 0, house table, colour roll
   (rand & 7, not 4 / 7 / repeats), AI slots and `rand() % 3` houses, AI
   records copied from the `_prac.mis` template, tech and credits for all
   sides (AI 7000 / 10500 / 14000), the tileset table of §5.
7. Map loader for skirmish: start markers (value 23), the first start
   `rand() % 8`, the farthest-start rule, MCV, TECHPOS units with (dx,
   dy), worms capped by gNetWorms, no other pre-placed objects.
8. Crates: `0x548048` from VARS when on; the top-up spawner (needs crate
   RE).
9. Music arakatak; the default win / lose check (doc 19 §4.3) instead of
   the script.
10. End flow: win → MAINMENU + SCORE; loss → REPLAY_MISSION (Yes restart,
    No → MAINMENU + SCORE); abort → MAINMENU + MULTIPLAYERTYPE + SCORE;
    SCORE Continue → SKIRMISH. Restart rolls the set-up again.
11. SCORE page (skirmish variant) with the per-side rows (§6).
12. P<n>.SAV: same body as the campaign; keep the not-saved gNet* globals
    out of it. Test: save, load, restart.
13. Parity tests: setup page against reference screenshots; a fixed-seed
    skirmish start (houses, colours, starts, units) once the seed
    question is answered.

## 9. The Bloxxmas tileset in 16-bit mode

Verified in the 1.06 exe. The game has a hard-coded exception for this
tileset. It does not fail and does not change the display mode.

### 9.1 Who loads the tileset

- `FUN_0044c6d0` (LoadMapData) calls `FUN_00462d10(0x5166a0)` (LoadTileset)
  with the mission's tileset name, unless `0x797e1c` (restart) is set.
- `FUN_00462d10` picks the branch on `0x4eb018` (current display depth),
  not on the setting `0x4eb01c`.
- Retail runs with `0x4eb014` = 1, so the `.bmp` / `.tga` and RES-creation
  branches (`0x7984cc`) are not used. Only the retail branches are below.

### 9.2 8-bit display (`0x4eb018` = 8)

- `wsprintf("%s.r8")` (`0x4e45e8`) → `FUN_00462c80`: open, read the whole
  file into `0x795570`, decode 800 tiles into `0x6d9b58..0x6da7d8`.
- Same for every tileset, Bloxxmas included. Tiles stay 8-bit.
- The 8-bit screen palette is `bin\Palette.bin`: the display reset
  `FUN_00474820` (8-bit branch) calls `FUN_0046abe0` (retail `0x4eb014` =
  1: reads `bin\Palette.bin` into `0x7984e0`), then `FUN_004a3f50`, which
  shifts each 6-bit channel left by 2, creates the DirectDraw palette
  (`IDirectDraw::CreatePalette`, vtable +0x14, flags 0x4C) and sets it on
  the primary surface (vtable +0x7C). Ours renders 16-bit only (CLAUDE.md
  §10), so this mode is not used.

### 9.3 16-bit display (`0x4eb018` = 16, the default)

- `_strcmpi(name, "bloxxmas")` (string `0x4e45cc`, used at `0x462f6d`).
  Case-insensitive, so the table's `Bloxxmas` matches.
- Any other name: `"%s.r16"` (`0x4e45d8`) → `FUN_00462c80`. Done.
- `bloxxmas`: it never tries `BLOXXMAS.R16`.
  1. `"%s.r8"` → `FUN_00462c80` loads `BLOXXMAS.R8` (8-bit tiles).
  2. If `0x7984e0` is null: allocate 0x300 bytes and call `FUN_0046abe0`,
     which reads `bin\Palette.bin` (768 bytes, 6-bit VGA RGB).
  3. Build a 256-entry u16 table (`FUN_0042f120(0x200)`, a "TImPal").
     For each entry `r = p[0] << 2`, `g = p[1] << 2`, `b = p[2] << 2`
     (plain shift, no `| v >> 4`), then `FUN_0042f3d0` packs to the
     screen format `0x798384`:
     - `0xcaca` (555): `(r>>3)<<10 | (g>>3)<<5 | (b>>3)`
     - else (565): `(r>>3)<<11 | (g>>2)<<5 | (b>>3)`
  4. Allocate one block of 0x195aa0 bytes. For each of the 800 tiles:
     make a 32x32 16-bit image (`FUN_0042ef90(0x20,0x20,0x10,0)`), set the
     R8 tile's palette pointer (+0x14) to the table, blit it with
     `FUN_0042fac0(dst,0,0,tile,0,0,0)`, copy the header (7 dwords) and
     the 2048 pixel bytes into the block, point the tile slot at the copy,
     free the temp image.
  5. The blit is the plain 8→16 branch: `dst = table[index]` for every
     pixel. Index 0 is not transparent here.
  6. Free the table and the R8 file buffer, set `0x795570` to the new
     block, free and clear `0x7984e0`.
- The palettes embedded in the R8 file are not used; `Palette.bin` is the
  only colour source.
- No error string is involved. A missing `BLOXXMAS.R8` would hit the normal
  `FUN_0042b730` "file not found" message box from `FUN_00462c80`.

### 9.4 Depth is global, not per mission

- `0x4eb01c` (gGameBitsPerPixel) is the player's setting. Nothing in the
  mission or tileset changes it.
- Menus force 16-bit: `FUN_00442580` at `0x442596` sets `0x4eb018 = 16`
  if it was 8, re-inits the display (`FUN_00474820`), sets `0x7984d4 = 1`.
- Mission start: `FUN_00448550` at `0x44859f` copies `0x4eb01c` into
  `0x4eb018` when `0x7984d4` is set and they differ, and re-inits.
- So a Bloxxmas mission in 8-bit mode uses the R8 tiles as they are; in
  16-bit mode it uses the converted R8 tiles above.

### 9.5 Data present

- `gamedata/1.06/Data/BLOXXMAS.R8` and `reference/d2k106/Data/BLOXXMAS.R8`.
  No `BLOXXMAS.R16` anywhere in the repo (no extracted ISO copy found).
- `Data/bin/TILEATR7.BIN` exists (TILEATR1..7), and `Data/bin/PALETTE.BIN`
  (768 bytes).

### 9.6 What our port does

- For tileset `bloxxmas` (any case): load `BLOXXMAS.R8`, colour every pixel
  (index 0 included) through `PALETTE.BIN` with `v << 2`, then truncate to
  the 16-bit format as in step 3. Do not look for `BLOXXMAS.R16`.
- Our screen format is **555** (`0x798384` = `0xcaca`): the Bloxxmas
  conversion (`game_screen.cpp`) and the particle darkening use the 555
  formula, so the 555 branch of step 3 applies.
