# 58. Network front end and in-game multiplayer UI (1.06)

Source: `DUNE2000.DAT` 1.06 US, decompiled with the GhidraMCP server
(`re/ghidra_http.py`), short ranges read with `re/disasm.py`; UI data from
`Data\UI_DATA\*.uil`, `menus.uib`, `text.uib` (dumped to text with a
scratch script; format in `docs/formats/ui.md`). Related: doc 03 (script
loader, headerless blocks), 04 (menu stack, navigation), 20 (in-game
menu), 47 (message lines), 51 (skirmish page), 55 (multistate box, value
bar, tooltips), 56 (SCORE), 57 (lockstep, handshake, leave kinds).
"Verified" = read in the decompile. Nothing here needed a running network
game. Points that are about DirectPlay's own behaviour are marked "not
applicable": our transport replaces it (CLAUDE.md section 10).

**Summary.**
- The LAN ("Network") path is **MULTIPLAYERTYPE → IPXLOBBY →
  HOSTHEADTOHEAD (host, `HostHTH.uil`) / CLIENTMULTIPLAYER (client,
  `ClientMP.uil`) → LOADING + FOUND_PLAYERS → game**. HOST_MP2 /
  JOIN_MP2 (`hostMP2.uil` / `joinMP2.uil`) are the **serial / modem**
  pages, not the LAN ones (corrects doc 57 §4 and the design doc §8).
- Everybody in the LAN front end sits in **one DirectPlay session named
  `-Dune Lobby-`**. A hosted "game" is only a **group number** written into
  each player's DirectPlay name. The real game session (`<host>'s Game`)
  is created only when the host presses Start.
- The host's **Start** button is disabled until **every client has pressed
  Accept with settings equal to the host's**. Any change of a setting, a
  join, a leave request, a Reject or a handicap change clears every Accept.
- A client cannot change house, colour or name after joining: they are
  chosen on IPXLOBBY. The client page has no such controls, and its option
  boxes answer "Only the host can change the options".
- In game: INGAME_MP (Options, Abort, Resume), CONFIRM_ABORT_MP (Abort =
  AI takes over, Surrender = side destroyed), MP_TIMEOUT (**no Cancel
  button in the 1.06 data**), OUT_OF_SYNC, GAMEMESSAGE (F1..F6). An abort
  goes straight back to IPXLOBBY, **without** a SCORE page.

## 0. Function and data map

| Address | Role |
|---|---|
| `FUN_004359a0` | per-menu hooks after load (doc 03): MULTIPLAYERTYPE button removal, chat focus (§2.2, §3.1) |
| `FUN_00482850` | click command switch (ids below) and the NoChangeOptions rule (§5.3) |
| `FUN_00484000` | init commands: EnterLobby 0x2e → `FUN_00479b30`, GetLobbyList 0x30 → `FUN_0047a6f0` |
| `FUN_00484cc0` | **refresh commands** of one element: RefreshLobbyList 0x31 `FUN_0047b4f0`, RefreshChatList 0x62 `FUN_0047d190`, RefreshValueS1..BF 0x63..0x69 (`FUN_0047d210`..`FUN_0047d780`) |
| `FUN_00474b60(all)` | runs the refresh commands of every element of the top menu (all = 0: list boxes only) |
| `FUN_00484e50` | double-click commands of a list: JoinSessionDbl 0x34 = `SendUIMsgToAll ClientWantsToJoin` |
| `FUN_00484ec0` → `FUN_00491c60` | single-click commands of a list: AlterHandicap 0x59 |
| `FUN_00487a00` | list row click: AlterHandicap, then on a selection change the special commands (`FUN_00483b80`) and `FUN_00474b60(0)` |
| `FUN_0048e660` | front-end network pump, called by the menu draw `FUN_0043c4b0` every frame (§4.1) |
| `FUN_0048e820` | lobby UI message handler (message id 1000, §4) |
| `FUN_0047bda0` | SendUIMsgToAll `<name>` (§4) |
| `FUN_0047bd00` | SendMsgToAll (lobby chat) |
| `FUN_00492880` | DisplayNetMessage: one line into the page's `responseList` |
| `FUN_00492110` | host: **ResetAccept** (clear all Accepts, disable Start, tell the clients) |
| `FUN_00492400` | host: enable Start |
| `FUN_004925a0` | client: clear Accepts, enable Accept |
| `FUN_00492310` | client: disable Accept (after the host confirmed it) |
| `FUN_00479b30` | EnterLobby |
| `FUN_0047ac10` / `FUN_0047ad60` | StartSession / JoinSession |
| `FUN_00479db0` / `FUN_00479d40` / `FUN_0047b470` | BreakFromSession / BreakFromLobby / ReEnterLobby |
| `FUN_00480f30` / `FUN_00481030(0)` | CreateWigNet / DestroyWigNet |
| `FUN_00479e80` | StartGame (§3.4) |
| `FUN_00491740` | builds my DirectPlay name (§1) |
| `FUN_00491a10` | first free colour in a game group (§5.2) |
| `FUN_00490560` / `0x490320` / `FUN_00491ef0` | setters of gNetPlayerColour / gNetPlayerSide / my handicap |
| `FUN_004885f0` | list box draw, with the lobby row icons and suffixes (§2.3) |
| `FUN_004810d0` | loads the lobby icons (§2.3) |
| `FUN_00492e20` / `FUN_00492eb0` | UI commands LeftGame (5) / AbortGame (6) (§6.2) |
| `FUN_0047bae0` | in-game chat send (§6.5) |
| `FUN_0045c740` | receives chat records (misc type 0x12, §6.5) |
| `FUN_00459b50` | A key: ally / war order 0x1B (§6.6) |
| `0x4580f0..0x458390` | ProcessOrder case 8: alliance lines (§6.6) |
| `FUN_00425720` | message line (doc 47 §B) |
| `FUN_0045d2c0` | WigNet error callback → WIGNET_ERROR |
| `FUN_004738b0` | navigation: LOBBY_SCREEN (case 0xDC), ABORT_MP_GAME (case 0xE1) (§3.7) |

| Global | Meaning |
|---|---|
| `0x6b93f8` gNetPlayerName (100) | my name |
| `0x6b9840` / `0x6b9844` / `0x6b9848` | gNetPlayerSide / gNetPlayerColour / gNetPlayerHandicap (defaults 0 / 0 / **1**, `FUN_00470710` 0x47082e) |
| `0x4e3b10` gNetNetworkType | 4 LAN (default), 3 serial, 1 modem, 2 Internet, 0 none |
| `0x6b97a8` gNetRejectPlayerName | the name in the selected player row |
| `0x6b9468` gNetSessionName | `%s's Game` |
| `0x6b9518` gNetMessageString | chat text being sent / received |
| `0x7978f8` gSendingToPlayer | GAMEMESSAGE label |
| `0x797768` gWigNetError | the WigNet error text (written, never displayed) |
| `0x7984c0` | number of humans in my game |
| `0x7984c4` | network flag |
| `0x4f3c78[8]` | **Accept marks**, per row of my game group |
| `0x797e24` (u8) | host: Accept counter |
| `0x797e10` | "game is starting" (set by StartGame) |
| `0x797e00` / `0x797e04` | client: waiting for the host's answer to ClientWantsToQuit, and since when |
| `0x797e18` | DirectPlay id the in-game chat goes to (0 = all) |
| `0x51620c` / `0x516210` / `0x516208` | provider available: network / internet / modem (§2.2) |

## 1. How the original LAN lobby works (verified)

All LAN players live in **one DirectPlay session**, `-Dune Lobby-`
(string `0x4e8494`; doc 57 §4 wrote `_Dune Lobby_`). EnterLobby
(`FUN_00479b30`) enumerates sessions, joins `-Dune Lobby-` when found,
otherwise creates it (max 100 players) and joins it, retrying every 2 s.

Each player's **DirectPlay name** carries his lobby state
(`FUN_00491740(host, group)`):

| Offset | Content |
|---|---|
| +0 | house letter `A` / `H` / `O` (gNetPlayerSide 0 / 1 / 2) |
| +1 | colour digit `0`..`7` (gNetPlayerColour); `9` draws DarkGrey |
| +2 | `H` = host of a game, `C` = not |
| +3..+5 | game **group** number, 3 digits; 0 = in the lobby, not in a game |
| +6 | handicap digit `0`..`2` |
| +7.. | the player name |

- **New** (StartSession, LAN): picks the lowest group number no player
  uses (`FUN_00491990`), renames me as host of it, humans = 1,
  JumpToMenu HOSTHEADTOHEAD.
- **Join** (JoinSession, LAN): renames me into the game's group (not
  host).
- **Leave a game** (BreakFromSession + ReEnterLobby): back to group 0.
- Everybody learns changes from DirectPlay's player-name notifications
  and from the UI messages of §4; both make the page refresh its lists.
- House, colour and handicap changes rename me at once
  (`FUN_004a1570`). No UI message is needed for house or colour.

When the host presses Start, it leaves `-Dune Lobby-`, creates a new
session `<host name>'s Game` (max 8) and joins it; each client gets UI
message 5 with that name, leaves the lobby session and joins it (§4).
Then the pre-game handshake of doc 57 §3.2 runs there.

For us (§7) this is all replaced by the transport; what must stay is what
the players see and the rules of §5.

## 2. MULTIPLAYERTYPE and IPXLOBBY

### 2.1 MULTIPLAYERTYPE (`MPType.uil`; menus.uib enter 2 / leave 2 = TWEEN)

Group box `gb` 220,150 200×210 `box1`; title TEXTBOX_UL `gameTypeTitle`
250,165 140×30 key `TitleGameType` "SELECT GAME TYPE". Buttons, all 140×20:

| Button | Pos | Text key → text | Commands |
|---|---|---|---|
| `internet` | 250,195 | `Internet` "Internet" | Dune2000CdCheck, `SetValueBFTo gDontDoReconnect FALSE`, gNetNetworkType 2, gGameType Internet, CreateWigNet, JumpToMenu INETLOGON |
| `network` | 250,220 | `Network` **"LAN"** | gNetNetworkType 4, gGameType Network, CreateWigNet, JumpToMenu IPXLOBBY, **DisableThis** (no CD check) |
| `serialModem` | 250,245 | `SerialModem` "Modem/Serial" | Dune2000CdCheck, JumpToMenu CONNECTION_TYPE |
| `skirmish` | 250,270 | `Practice` | doc 51 |
| `cancel` | 250,295 | `Cancel` | gGameType Campaign, BackMenu |

`DisableThis` (0x0C) sets the button's disabled byte +0x776; the page is
left in the same frame, so it only guards a double click.

### 2.2 Exe hooks on MULTIPLAYERTYPE (`FUN_004359a0`, verified)

When the menu is created:
- `0x516208` = 0 (no modem): `serialModem` is deleted and a **`Null
  Modem`** button is created at its rect (gfx but1, text `NullModem`) with
  gNetNetworkType 3, gGameType Serial, JumpToMenu SERIALLOBBY.
- `0x51620c` = 0 (no network provider): the `network` button is
  **deleted**.
- `0x516210` = 0 (no internet provider): the `internet` button is
  **deleted**.

So the exe itself removes buttons of unavailable services and leaves the
gap. This is the precedent for §7.3. The flags are set at start-up
(`FUN_00470710` `0x470aed`..`0x470b68`, verified): all three 0, then, if
`FUN_00492000` says Windows NT 4 or older (platform NT, major version
≤ 4), they stay 0 (no multiplayer at all on NT 4); else CreateWigNet and a
walk over the DirectPlay service-provider list (`FUN_0049ffa0`): a node of
type 1 sets modem `0x516208`, type 2 internet `0x516210`, type 4 network
`0x51620c`; then the wrapper is destroyed (`FUN_00481030(1)`). The
provider list itself is DirectPlay's: not applicable to us.

### 2.3 IPXLOBBY (`IPXLobby.uil`; enter 0 / leave 1 = FADE)

Background `multiUI`. The 640×400 group box after it is headerless (not
created).

| Element | Type, pos, size | Text / colours | Commands |
|---|---|---|---|
| `tooltip` | TEXTBOX 0,0 4×4 | | menu tooltips (doc 55 §4) |
| `sideName` | TEXTBOX 272,86 100×12 | JHighLightText | `GetStringIfValueU1Init gNetPlayerSide 0/1/2 Atreides/Harkonnen/Ordos` |
| `yourName` | TEXTBOX 60,35 105×15 | `YourName` "YOUR NAME" | |
| `yourSide` | TEXTBOX_L 283,35 90×15 | `YourSide` "YOUR SIDE" | |
| `yourColour` | TEXTBOX_L 460,35 130×15 | `YourColour` "YOUR COLOR" | |
| `games` | TEXTBOX 26,100 50×10 | JNormalText, `Games` | |
| `playersInGame` | TEXTBOX_L 320,100 120×10 | JNormalText, `Players` | |
| `nameEdit` | EDITBOXFOCUS 60,50 120×25 | cursor JBoxOutLine, font JHighLightText | `NoOtherChars`, `GetStringInit gNetPlayerName`, `SetFromUIStringSpecial gNetPlayerName`, `SetEditFocus chat` |
| `chat` | EDITBOX 32,315 575×35 | | `SendMsgToAll responseList` |
| `atreidesCheck` / `ordosCheck` / `harkonnenCheck` | CHECKBOX 270 / 305 / 340,50 32×32, gfx `a_small`/`a_smalld` … | | as SKIRMISH (doc 51): gNetPlayerSide 0 / 2 / 1, `SetStringInTextBox` |
| colour checks ×6 | CHECKBOX 445..545,50 step 20, 15×25, `CheckBorder` / `NetColour<n>` / `NetFadedColour<n>`, no gfx | | gNetPlayerColour n = 0,1,2,3,5,6 (as SKIRMISH) |
| `gameList` | LISTBOX 32,114 285×95, font / frame JBoxOutLine, highlight JHighLightText, slider `Wslider` | | `EnterLobby` (init), `JoinSessionDbl gameList` (double click) |
| `playerList` | LISTBOX 322,114 285×95 | | `GetLobbyList gameList` (init), `RefreshLobbyList gameList` (refresh), `NoHighlight` |
| `responseList` | LISTBOX_CROPPED 32,215 575×95 | | `RefreshChatList` (refresh) |
| `join_game` | BUTTON 93,371 72×20, `rBut4_up/dn`, JRubberText | `Join` | `SendUIMsgToAll ClientWantsToJoin` |
| `cancel` | BUTTON 286,371 72×20 | `Cancel` | `BreakFromLobby`, `BackMenu` |
| `new_game` | BUTTON 477,371 72×20 | `New` | `Dune2000CdCheck`, `ParseEndCommands`, `StartSession` |

**Edit box filters** (verified 2026-10-16): init commands `NoSpaces`
0xC1, `NoLetters` 0xC2, `NoNumbers` 0xC3, `NoOtherChars` 0xC4 (command
table `0x4beb88`, entry size 0xBC) set bits 1 / 4 / 2 / 8 of the element
byte `+0xab0` (`FUN_00484000`). The character handler (`FUN_0043cb70`,
0x43cc9b) takes a character 0x20..0x7F (signed byte) only when its class
is not refused: space bit 1, `0`-`9` bit 2, `A`-`Z` / `a`-`z` bit 4, any
other printable bit 8. So the name boxes (`NoOtherChars`) take letters,
digits and spaces only.

**Focus** (`FUN_004359a0` 0x436fa9): on IPXLOBBY, HOST_MP2 and JOIN_MP2,
if gNetPlayerName differs from text `NoName` ("NONAME") the edit focus
moves from `nameEdit` to `chat`. An Enter in `nameEdit` also moves it to
`chat` (`SetEditFocus chat`, 0x0B).

**Lists** (`FUN_0047b4f0` / `FUN_0047a6f0`, draw `FUN_004885f0`):

- `gameList`: row 1 = text `Dune2000Lobby` "Dune2000 Lobby" (list font
  colour, item data 0); then one row per player whose name says host:
  `<host name>'s Game` (`'s Game`, 0x4e84d4), in the host's
  `NetColour<c>`, item data = group. Drawn with icon `icon_d2` at x + 2
  before every row but the first, the text at x + icon width + 4, then
  `" %d/%d"` = members of that group / 6 (the second number is 6 for
  gGameType 2, 2 for serial / modem, 4 for Internet).
- `playerList`: the members of the group selected in `gameList` (row 1:
  the players in the lobby, group 0). Row text = name, in the player's
  `NetColour<c>`; icon `icon_ui` before each row; when a game row
  (selection ≥ 2) is selected, `" (%s)"` with the house name follows.
  The list is not drawn while I am inside a game group.
- Selecting a `gameList` row refreshes both lists (`FUN_00474b60(0)`), the
  selection and the scroll position are kept.
- `responseList`: chat and messages. RefreshChatList appends
  gNetMessageString when set: the first byte is the sender's colour
  digit, the rest is wrapped into rows (`FUN_00486f10`) in
  `NetColour<digit>`; then the string is cleared. DisplayNetMessage lines
  (`FUN_00492880`, only on IPXLOBBY, HOST_MP2, JOIN_MP2, HOSTHEADTOHEAD
  and CLIENTMULTIPLAYER) are added with colour 0, which the row adder
  `FUN_00486d20` replaces by the list's own font colour (element +0x780),
  and the list scrolls to the end.
- `responseList` is a LISTBOX_CROPPED: the loader passes the cropped flag
  (`0x4392e5`) into +0x814, so it has no scroll bar (doc 27 §3.2) and
  every new row first drops the oldest rows while the list holds as many
  as it shows (`FUN_004876f0`, called by `FUN_00486d20` and
  `FUN_00486f10`). Only the visible rows are kept.

Icons (`FUN_004810d0`, `UI_gfx\<name>`): `icon_d2` game, `icon_ui` user,
`icon_ac` accepted, `icon_ho` host, `hand1` / `hand2` / `hand3` handicap
0 / 1 / 2; the others (`icon_ww`, `icon_sq`, `icon_oc`, `icon_uc`,
`icon_li`, `icon_prv`, `icon_op`, `icon_cl`, `icon_cop`, `icon_mop`,
`icon_tng`, `icon_tpg`) are Westwood Online only.

**Commands of the page:**

| Command | When | Effect (LAN) |
|---|---|---|
| EnterLobby (init) | page created | network flag on; join or create `-Dune Lobby-`; send UI message 2 to every lobby player (they refresh); refresh this page |
| GetLobbyList / RefreshLobbyList `gameList` | init / every refresh | the two lists above |
| RefreshChatList | every refresh | above |
| SendMsgToAll `responseList` | Enter in `chat` with text | §4 `SendMessage`: shown locally in my colour, sent to my group (in the lobby: everybody in group 0) |
| Join → `SendUIMsgToAll ClientWantsToJoin` | click | needs a game row (selection ≥ 2); fewer than 6 members, else `GameIsFull` "That game is full"; not my own group. Sends message 0x1B {version 3} to every member of that game; the host answers 0x1C; on yes the client runs **JoinSession** (§4) |
| JoinSessionDbl | double click on `gameList` | the same as Join |
| New → StartSession | click | empty name → `NoNameEntered` "You need to enter a name for yourself before you can Join or Host a game" in `responseList`; else become host of a new group, JumpToMenu **HOSTHEADTOHEAD** |
| Cancel → BreakFromLobby, BackMenu | click | UI message 3 to everybody, close the lobby session, reset the wrapper, network flag off; back to MULTIPLAYERTYPE (or MAINMENU after a game, §3.7) |

JoinSession (LAN, `FUN_0047ad60`), in order:
1. empty name → `NoNameEntered`;
2. no game row selected (selection < 2) → `NoGameSelected` "You need to
   select a game session to join";
3. 6 members or more → `GameIsFull`; my name already in that game
   (exact compare) → `NameIsUsed` "The name you have chosen is already
   used by another player";
4. if my colour is used in that game, take the **lowest free colour
   0..7** (`FUN_00491a10`; 4 and 7 included, which have no checkbox);
5. gNetAIPlayers = 0; rename me into the group; UI message 2 to the
   group; ParseEndCommands; JumpToMenu **CLIENTMULTIPLAYER**.

## 3. The game pages

### 3.1 HOSTHEADTOHEAD — the LAN host (`HostHTH.uil`; FADE)

Background `multiUI`; the 640×400 box, the `woodcovr` box and the
Concrete text / checkbox are headerless (not created; concrete is dead,
doc 51).

| Element | Type, pos, size | Text | Commands / globals |
|---|---|---|---|
| `tooltip` | TEXTBOX 0,0 4×4 | | |
| `headToHeadTitle` | TEXTBOX_L_UL 270,19 200×20 | `TitleHeadToHead` "HEAD TO HEAD" | |
| `players` / `maps` | TEXTBOX_L 32,43 60×15 / 347,43 40×15, JNormalText | `Players` / `Maps` | |
| `unitCount` / `techLevel` / `credits` / `aiPlayers` | TEXTBOX_L 35,180 / 195 / 210 / 225, 90×15 | `UnitCount` / `TechLevel` / `Credits` / `AIPlayers` | |
| `crates` / `worms` | TEXTBOX_L 445,190 / 210, 170×20 | `CratesOff/On`, `WormsOff/Low/Medium/High` | relabelled by the setters (doc 55 §3) |
| `chat` | EDITBOXFOCUS 32,325 575×25 | | `SendMsgToAll responseList` |
| `playerList` | LISTBOX 32,58 260×110, font JHighLightText | | `SetFromUIStringSpecial gNetRejectPlayerName` (on a selection change), `GetLobbyList NONE`, `RefreshLobbyList NONE`, `AlterHandicap` (single click) |
| `mapList` | LISTBOX 347,58 260×110, JNormalText | | `GetMapList`, `GetMapFromList`, `StaySelected` (doc 51 §3) |
| `responseList` | LISTBOX_CROPPED 32,250 575×70 | | `RefreshChatList` |
| `unitCountBar` | VALUEBAR 140,180, `0 10 1 1` | | `GetValueU1Init` / `SetValueU1Special gNetUnitCount`, `SendUIMsgToAll UnitCount` |
| `techLevelBar` | VALUEBAR 140,195, `1 7 1 1` | | gNetTechLevel, `SendUIMsgToAll TechLevel` |
| `creditBar` | VALUEBAR 140,210, `2500 7000 500 2500` | | gNetStartingCredits, `SendUIMsgToAll StartingCredits` |
| `aiBar` | VALUEBAR 140,225, `0 4 1 0` | | gNetAIPlayers, `SendUIMsgToAll AIPlayers`; **max = 6 − humans** at run time (below) |
| `crateCheck` | CHECKBOX 420,190 20×18 `Bicon` | | `CheckIfValueBFInit gNetCrates TRUE`, `SetValueBFTo gNetCrates TOGGLE`, `SendUIMsgToAll Crates` |
| `wormsCheck` | MULTISTATEBOX 420,210, 4 states `Wicon_0..3` | | `CheckIfValueU1Init gNetWorms`, `SetFromUIValueU1To gNetWorms`, `SendUIMsgToAll Worms` |
| `cancel` | BUTTON 286,371 72×20 | `Cancel` | `BreakFromSession`, `BackMenu` |
| `start` | BUTTON 477,371 72×20, gfx **`rb4_dis`** both states, font JRubberTextDn | `StartGame` "Start" | `DisplayNetMessage CannotStartGame` (until enabled, §5.4) |
| `reject` | BUTTON 93,371 60×15 | `Reject` | `SendUIMsgToAll Reject` |
| `chosenMap` | TEXTBOX_L 387,43 250×15 | `NoMapSelected` | `EnterText gNetMapDesc`, `RefreshValueCH gNetMapDesc` |

No name, house or colour controls: they were set on IPXLOBBY.

**Player list rows** (session pages): handicap icon `hand<h+1>` at
x + 2; the name at x + icon width + 4 in the player's `NetColour<c>`;
`" (%s)"` house name; then a mark: `icon_ho` for the host, else
`icon_ac` when the row's Accept mark `0x4f3c78[row]` is set. Row order =
DirectPlay's enumeration order of the group (`FUN_004a07a0`); not
applicable: ours lists the host, then the players in join order (= side
order). `gameList` starts with no row selected (the list constructor
`FUN_00486440` sets +0x7e8 = 0 at `0x486580`; the fill only lowers it to
the row count, `0x47a873`), which lists group 0 like row 1. Tooltips on rows (`FUN_004712c0`): over the first 20 px
(the handicap icon) `EasyHandicapLevel` / `NormalHandicapLevel` /
`HardHandicapLevel` ("Easy / Normal / Hard handicap setting"); over the
16 px after the text, when the row is host or accepted, `UserIsHost`
"User is the host" / `UserAccepted` "User has accepted settings".

**AI bar limit.** While the player list is drawn on this page
(`FUN_004885f0` 0x48a67a) and when a member joins or leaves (§4 messages
2 and 3): the bar's max = 6 − (rows = humans), gNetAIPlayers is lowered
to it, and the host sends message 0x11 if it had to lower it. So humans +
AI never exceed 6.

**AlterHandicap** (`FUN_00491c60`, single click in `playerList` with the
mouse x ≤ list x + 20, i.e. on the hand icon): finds the clicked row's
player. Me → cycle my handicap 0 → 1 → 2 → 0 (`FUN_00491ef0`: rename,
UI message 0xC to the group). Another player → UI message 0xD to him; he
cycles his own handicap. Then ResetAccept. The clients' player list has
no AlterHandicap, so only the host can change handicaps, everybody's.

### 3.2 CLIENTMULTIPLAYER — the LAN client (`ClientMP.uil`; FADE)

| Element | Type, pos, size | Text | Commands |
|---|---|---|---|
| `tooltip` | TEXTBOX 0,0 4×4 | | |
| `woodBox1` | GROUPBOX 83,362 72×20 `woodcovl` | | (the `woodcovr` box is headerless) |
| `sidelogo` | GROUPBOX 398,55 128×128 `Atlogo` | | `LoadGfxIfValueU1Is Atlogo/Hklogo/Orlogo gNetPlayerSide 0/1/2` |
| `MultiGame` | TEXTBOX 20,22 600×10 | `MultiPlayerGame` "Multiplayer Game" | |
| `Players` | TEXTBOX_L 40,40 160×10 | `Players` | |
| `unitCount` … `aiPlayers` | TEXTBOX_L 35,175 / 190 / 205 / 220 | | |
| `crates` / `worms` | TEXTBOX_L 390,190 / 210 | | |
| `chat` | EDITBOXFOCUS 40,335 560×25 | | `SendMsgToAll responseList` |
| `playerList` | LISTBOX 40,55 260×110, font White | | `GetLobbyList NONE`, `RefreshLobbyList NONE`, `NoHighlight` |
| `responseList` | LISTBOX_CROPPED 40,250 560×80 | | `RefreshChatList`, `NoHighlight` |
| `unitCountBar` / `techLevelBar` / `creditBar` / `aiBar` | VALUEBAR 140,175 / 193 / 211 / 229, ranges as host | | `GetValue*Init` + `RefreshValueU1/U2 <global>` |
| `crateCheck` | CHECKBOX 360,190 | | `CheckIfValueBFInit gNetCrates TRUE`, `RefreshValueBF gNetCrates` |
| `wormsCheck` | MULTISTATEBOX 360,210 | | `CheckIfValueU1Init gNetWorms`, `RefreshValueU1 gNetWorms` |
| `cancel` | BUTTON 286,372 72×20 | `Cancel` | `SendUIMsgToAll ClientWantsToQuit` |
| `accept` | BUTTON 477,371 72×20 | `AcceptGame` "Accept" | `SendUIMsgToAll AcceptSettings` |
| `chosenMap` | TEXTBOX 17,36 605×20, JNormalText | `NoMapSelected` | `EnterText gNetMapDesc`, `RefreshValueCH gNetMapDesc` |

RefreshValue* (`FUN_0047d210`..`FUN_0047d780`): the bar / checkbox /
multistate box takes the global's value (clamped like `GetValue*Init`),
marked dirty. They run on every refresh of the page. RefreshValueCH is
not in the refresh switch: `chosenMap` is updated by the map message
(§4, message 6) directly.

The Accept button changes its own look and command:
- enabled (`FUN_004925a0`): gfx `rbut4_up` / `rbut4_dn`, font JRubberText
  / JRubberTextDn, command `SendUIMsgToAll AcceptSettings`;
- after the host confirmed my Accept (`FUN_00492310`): gfx `rb4_dis`, font
  JRubberTextDn, **no command** (clicks do nothing).

The page's player list is not drawn while my group is 0.

### 3.3 HOST_MP2 / JOIN_MP2 (serial and modem only)

Same settings and lists as §3.1 / §3.2 plus name, house and colour
controls, and `Reject`; HOST_MP2's chat starts with `WaitForCall`
"Waiting for another player to join...". Cancel = `BreakFromSession`,
`DestroyWigNet`, `BackMenu`. Not reachable in our build (§7.3).

### 3.4 Start (`FUN_00479e80`, host's Start once enabled)

The enabled Start (`FUN_00492400`) has gfx `rbut4_up/dn`, font
JRubberText / JRubberTextDn and the command `StartGame mapList`.
StartGame:
1. returns silently if my group has fewer than 2 members or no map is
   chosen (gNetMap empty);
2. serial / modem only: empty name → `NoNameEntered`; both names equal →
   `ChangeName`;
3. "starting" flag `0x797e10` = 1, ParseEndCommands;
4. LAN: humans = members; UI message 5 {`<my name>'s Game`, humans, map}
   to every member; leave `-Dune Lobby-`, create and join `<my name>'s
   Game`;
5. network flag on, gGameState 2, `FUN_004752c0(gNetMap)` (paths and
   LOADING, doc 51 §5). Then the handshake with FOUND_PLAYERS (§3.6).

### 3.5 What each player sees on changes

- Host moves a bar / toggles crates / worms / picks a map → the client's
  controls and labels follow on the next refresh, the client's Accept is
  enabled again, and every accepted mark disappears; the host's Start is
  disabled.
- A client presses Accept → his row gets `icon_ac` on every machine (the
  host confirms with message 0x18, the client broadcasts 0x17); his
  Accept button greys out. When the last client is in, the host's Start
  lights up.
- A player joins or leaves → lists refresh, Accepts are cleared, the AI
  bar max changes.
- Messages (`GameIsFull`, `CannotStartGame`, `NoChangeOptions` …) appear
  as lines in `responseList`, not as pop-ups.

### 3.6 Start-up pages (handshake, doc 57 §3.2 / §3.3)

All three use group box `gb` 200,140 240×130 `box4` and 200×20 text
boxes at x 220 (the 640×400 box before it is headerless in foundMP /
failMP, so the game picture stays visible behind). menus.uib 3 / 3
(SKIP).

| Page | File | Texts | Buttons |
|---|---|---|---|
| FOUND_PLAYERS | `foundMP.uil` | `mp_connecting` y 170: `ConnectingPlayers` "Connecting %d players" (humans); `mp_found` y 190: `FoundPlayers` "Found %d players" (client 1, host the count so far); `mp_countdown` y 210: `MPCountdown` "Countdown .. %d seconds" = (60 000 − elapsed ms) / 1000 | none |
| FAILED_MP_CONNECT | `failMP.uil` | `ConnectError` "Network Problem", `FailedToConnect` "Players failed to connect with each other.. Exiting game" | none; the game loop closes all menus after the handshake if it is open (`0x4486ae`). **Never opened in 1.06**: its name is referenced only by that test and the per-menu hook `FUN_004359a0` (`0x435d31`), and no `.uil` file names it (only `menus.uib`'s page table) |
| NO_FIND_PLAYERS | `noPlayer.uil` | `CantFindPlayers` "You failed to connect to the other players in the game. Returning to lobby." | `ok` 250,225 140×20: BackMenu. Shown modally (a small UI loop runs until it closes), then QuitAndJumpToMenu ABORT_MP_GAME |
| NOT_ALL_FOUND | `notAll.uil` | `CantFindAllPlayers` "Could not find all players for this game. Going into the game without them." | none. **Never opened in 1.06**: referenced only by the hook `FUN_004359a0` (`0x435cfd`) and `menus.uib` |

### 3.7 Routing back to the lobby (`FUN_004738b0`, verified)

| Navigation | gGameType 2 (LAN) result |
|---|---|
| `QuitAndJumpToMenu ABORT_MP_GAME` | close all; MAINMENU, MULTIPLAYERTYPE, CreateWigNet, gNetNetworkType 4, **IPXLOBBY**. **No SCORE** (skirmish shows SCORE, doc 51 §6) |
| `QuitAndJumpToMenu SCORE` (end of game) | close all; MAINMENU, SCORE (title "MULTIPLAYER RESULTS", §6.8) |
| `BackMenuAndJumpTo LOBBY_SCREEN` (SCORE Continue) | close SCORE; CreateWigNet, gNetNetworkType 4, **IPXLOBBY** (over MAINMENU only; Cancel there lands on MAINMENU) |

Serial: MULTIPLAYERTYPE, CONNECTION_TYPE, then HOST_MP2 / JOIN_MP2 again
(re-using the open connection); modem: the same; Internet: INETLOBBY.
LOBBY_SCREEN is not a menu file.

### 3.8 Error pages

| Page | File | Text | Button | Opened by |
|---|---|---|---|---|
| WIGNET_ERROR | `netError.uil` (box4) | `DirectPlayError` "There has been a problem with the network. You may try again" (static; gWigNetError is never shown) | `ok` 250,225: **`WigNetError`** (`FUN_00493190`): gGameState 2 → 1, QuitAndJumpToMenu **MAINMENU** | the wrapper error callback `FUN_0045d2c0`, and "Model Out Of Sync" (doc 57 §1.4); opened at once, once |
| WRONG_VERSION | `wrongVer.uil` | `WrongNetworkVersion` "You have a different version of the game to the host. You can't join this game." | OK: BackMenu | serial / modem settings check (§4, 0x18). On LAN the same text only goes into `responseList` |
| ERROR_MESSAGE | `Error.uil` | `EnterText gLastUIError` | OK: BackMenu | generic |

## 4. Lobby UI messages (message id 1000, 0x1A4 bytes; verified)

Layout: +8 type, +0xC text (400), +0x19C byte, +0x19E u16, +0x1A0 byte.
"Group" = every player of my game group, me included; "all" =
DirectPlay broadcast. Sent by `FUN_0047bda0` unless noted; handled by
`FUN_0048e820`, which ends with a refresh of the page.

| Type | Sent by | To | Payload | Receiver |
|---|---|---|---|---|
| 0 / 1 | `Searching` / `Hosting` | all | | refresh only. Not used by any script |
| 2 | EnterLobby, JoinSession, `Joining` | lobby / group | | **host**: humans + 1; AI max = 6 − humans (lower gNetAIPlayers and send 0x11 if needed); send 0x18 {0, settings} and 6 {map} to the joiner; ResetAccept. **Client**: AI max; enable Accept |
| 3 | BreakFromSession (group), BreakFromLobby / `Break` (all) | | +0x1A0 = sender was host | **client**: if the host left → BreakFromSession + BackMenu (back to IPXLOBBY, no message). **Host**: humans − 1 on any type 3, even from a lobby player outside the game (verified: the host branch at `FUN_0048e820` case 3 tests nothing about the sender), AI max. Not applicable to us: the quirk needs DirectPlay's one shared lobby session, where a player leaving IPXLOBBY sends type 3 to everybody |
| 4 | `Reject` | group | name = gNetRejectPlayerName | the named player: BreakFromSession + BackMenu (no message). Others: enable Accept. Sender: ResetAccept. Not sent when the selected name is my own |
| 5 | StartGame | group | `%s's Game`, +0x19C humans, map | client: humans; leave the lobby session, join the game session; StartGame path (`FUN_004752c0`) |
| 6 | GetMapFromList (host), client without the map | group | map name | gNetMap / gNetMapDesc (`FUN_00493050`), `chosenMap` text; enable Accept (`FUN_004925a0`, client pages only). If the map file is missing here (`FUN_00491b40`): an empty map is sent to the group and the own map is cleared. Every receiver, the host too, takes the empty map ("No map selected"); the host's Accept marks and Start look stay, and StartGame refuses until it picks another map (verified; ours: `LobbyClient::no_map`) |
| 9 | `PlayerName` | all | name | refresh |
| 0xA | `PlayerSide` | all | house | host: ResetAccept; client: enable Accept |
| 0xB | `PlayerColour` | all | colour | refresh |
| 0xC | `PlayerHandicap`, `FUN_00491ef0` | group | handicap | client: enable Accept |
| 0xD | AlterHandicap (host) | the row's player | | cycle my handicap, enable Accept |
| 0xE / 0xF / 0x10 / 0x11 | `TechLevel` / `UnitCount` / `StartingCredits` / `AIPlayers` (value bars) | group | +0x19C (credits: u16 +0x19E) | client: set the global, enable Accept; 0x11 asserts humans + AI ≤ 6. Sender: ResetAccept |
| 0x12 / 0x13 | `Crates` (checkbox) / `Worms` (multistate) | group | state | client: set, relabel (`FUN_004931c0` / `FUN_00493350`), enable Accept. Sender: ResetAccept |
| 0x15 | `SendMessage` (chat) | group | `"%d%s : "` colour digit, my name, then the text | copied to gNetMessageString; shown by RefreshChatList |
| 0x16 | `AcceptSettings` | group | `"%d %d %d %d %d %d %s"` = credits, AI, worms, crates, units, tech, map | **host** only (§5.4) |
| 0x17 | ResetAccept (host, +0x1A0 = 0); a confirmed client (+0x1A0 = 1) | all | | non-host members of my group: 0 → clear all marks, enable Accept; 1 → mark the sender's row |
| 0x18 | host | one client | +0x1A0: 1 = Accept confirmed; 0 = settings `"3 %d %d %d %d %d %d %s"` | 1: disable Accept, send 0x17 {1}, mark my row. 0: first number must be **3** (version), else `WrongNetworkVersion` + leave; set credits, AI, worms, crates, units, tech, map; enable Accept |
| 0x19 | `ClientWantsToQuit` | group | | host answers 0x1A {1} and ResetAccept, or {0} while starting |
| 0x1A | host | the client | ok | 1: BreakFromSession, BackMenu (to IPXLOBBY). 0: stay. A client with no answer after **10 s** leaves anyway (`FUN_0048e660`) |
| 0x1B | `ClientWantsToJoin` | the game's members | +0x19E = 3 (version) | host answers 0x1C {1} (and ResetAccept) when the version is 3 and not starting; {0} on a wrong version; nothing while starting |
| 0x1C | host | the joiner | ok | 1: JoinSession; 0: `WrongNetworkVersion` line |

`GameSpeed` (`SendUIMsgToAll GameSpeed` from a value bar) posts order
0x0B instead (doc 57 §1.8).

### 4.1 Refresh timing

There is no periodic refresh. The front-end pump (`FUN_0048e660`, every
menu draw) drains all messages: id 1000 → the handler above (which
refreshes at its end), DirectPlay system messages (10, 0x14, 0x1E, 0x28,
0x32, 0x3C, 0x46, 0x50, 0x5A: player created / deleted / renamed …) → a
refresh flag that the UI update turns into `FUN_00474b60(1)` once.

## 5. Lobby rules

### 5.1 Who may change what

- **Before joining** (IPXLOBBY): name, house, colour; free choice. No
  uniqueness in the lobby.
- **Host page**: the host changes the map, units, tech, credits, AI
  players, crates, worms and **every player's handicap**; he cannot change
  his own name / house / colour any more (no controls).
- **Client page**: nothing but Accept, Cancel and chat. Clicking a
  checkbox or multistate box with **y** > 100 (element +0x0c; corrected
  2026-10-16, doc 55 §1.4) shows `NoChangeOptions` "Only
  the host can change the options" in `responseList` (`FUN_00482850`, any
  network page except IPXLOBBY, when I am not host). A value bar's press
  (`FUN_0048c640`) has the same rule for any y, so a client cannot move
  a bar at all (doc 55 §2.4; ours: `no_change_options`).

### 5.2 Colours and houses

- Colours with a checkbox: 0, 1, 2, 3, 5, 6. Houses: any, repeats
  allowed.
- On JoinSession a taken colour is replaced by the **lowest free colour
  0..7** of that game (4 and 7 possible). There is no other uniqueness
  check (the lobby colour setter takes any value on IPXLOBBY).
- In game the human colours are kept; the AI slots get the colour roll
  of doc 57 §3.4.

### 5.3 Players

- Max **6 humans** (Join refuses at 6, JoinSession refuses at 6) and
  **humans + AI ≤ 6** (AI bar max 6 − humans; assert in message 0x11).
- The game session of the start allows 8 DirectPlay players.

### 5.4 Accept and the start rule (`CannotStartGame`), verified

- Each client's Accept sends his view of the seven settings (message
  0x16). The host:
  1. adds 1 to the Accept counter `0x797e24`;
  2. compares the seven values with its own;
  3. equal → if counter = members − 1, **enable Start**; confirm with
     0x18 {1} and set the client's mark;
  4. different → send 0x18 {0} with its settings (the client takes them
     and may Accept again).
- **ResetAccept** (`FUN_00492110`, host on HOSTHEADTOHEAD / HOST_MP2):
  counter 0, all marks 0, message 0x17 {0}, Start back to the disabled
  look with `DisplayNetMessage CannotStartGame` ("You need to wait for
  players to join and accept before starting a game"). It runs after:
  every setting message the host sends (0xE..0x13), a map selection, a
  join (2), a join request (0x1B), a quit request (0x19), a Reject (4), a
  handicap change, and host-side 0xA.
- So **Start is enabled only when all clients have accepted the current
  settings**. StartGame then also needs ≥ 2 members and a map (§3.4).
- Quirk (verified, `FUN_0048e820` case 0x16): the counter goes up
  before the comparison (`0x48f5c3`), so it also counts Accepts that did
  not match, and Start is enabled only when it is **exactly** members − 1
  at a matching Accept (`0x48f665`). With 3 players, client A's
  mismatched Accept plus his corrected Accept make the counter 2 and
  enable Start while client B has not accepted; with 2 players, a
  mismatched then a matching Accept make it 2 ≠ 1, and Start stays off
  until the next ResetAccept. Ours keeps the quirk (`LobbyHost`: an
  Accept of an older settings revision is the mismatch).

### 5.5 Reject (kick)

The host selects a row (this copies the row's name into
gNetRejectPlayerName) and presses Reject. The named client is sent back
to IPXLOBBY without a message. Rejecting oneself does nothing.

### 5.6 Leaving

- **Client Cancel**: ask the host (0x19); with its yes the client
  returns to IPXLOBBY; the host's count drops, Accepts are cleared. Not
  possible once the host pressed Start. 10 s without an answer: the
  client leaves anyway.
- **Host Cancel**: BreakFromSession + BackMenu; every client of the game
  is sent back to IPXLOBBY at once, without a message.
- **Lobby Cancel**: back to MULTIPLAYERTYPE (or MAINMENU after a game).
- A client that vanishes (crash) only disappears from the lists through
  DirectPlay's player-deleted notification, which only refreshes the page
  (§4.1); the host's human count is not lowered. Not applicable to us: our
  host drops a client silent for 10 s and clears the Accepts.

## 6. In game

### 6.1 INGAME_MP (`InGameMP.uil`, 3 / 3)

Opened by the Options key / sidebar button whenever the network flag is
set (`0x444c34`, all network types), like INGAME (doc 20). Group box `gb`
200,140 204×170 `box3`; title TEXTBOX_UL `gameOptionsTitle` 220,165
162×20 `TitleOptions` "OPTIONS".

| Button | Pos, size | Text | Commands |
|---|---|---|---|
| `gameControls` | 233,185 140×20 but1 | `GameSettings` "Game Settings" | JumpToMenu OPTIONS (comment "was MP_OPTIONS") |
| `abort` | 233,210 140×20 but2 | `AbortMission` "Abort Mission" | JumpToMenu CONFIRM_ABORT_MP (serial / modem: ABORT_CONNECTION) |
| `resume` | 233,235 **70**×20 but3 | `ResumeMission` "Resume Mission" | BackMenu |

A press and release outside the box closes it, as INGAME
(`FUN_004710d0` / `FUN_004711a0` test INGAME_MP too). The game keeps
running behind it (doc 57 §1.1).

### 6.2 CONFIRM_ABORT_MP (`abortMP.uil`, 3 / 3)

Group box `gb` 115,200 170×60 `Barbtton`; TEXTBOX_UL `abortTitle`
190,220 170×20 `AreYouSure` "Are You Sure?". Buttons with gfx `but8`:

| Button | Pos, size | Text | Commands |
|---|---|---|---|
| `yesAbort` | 158,240 53×10 | `Abort` | PlaySample S_MISSIONABORT, **AbortGame**, gGameState Briefing, QuitAndJumpToMenu ABORT_MP_GAME |
| `surrender` | 240,240 55×10 | `Surrender` | PlaySample S_MISSIONFAILED, **LeftGame**, `SetValueBFTo gLose TRUE`, QuitMenu |
| `no` | 322,240 57×10 | `Cancel` | BackMenu |

- AbortGame (`FUN_00492eb0`): chat `0xAC PlayerLeft` to all, quit kind
  0 (AI takes over), no order. Then the game loop ends, posts the leave
  order (doc 57 §3.8) and the front end opens MAINMENU, MULTIPLAYERTYPE,
  IPXLOBBY (§3.7).
- LeftGame (`FUN_00492e20`): chat `0xAC PlayerRetired` to all; order
  **0x31** (retire), quit kind 1 (`FUN_0046ea50(0)`); in gGameState 3/4
  `0x797de4` = 1. The game goes on: the side is destroyed by 0x31, the
  player has lost, watches with the map revealed, and gets SCORE at the
  end (doc 57 §3.9).

### 6.3 MP_TIMEOUT (`mTimeout.uil`, 3 / 3)

Box4 at 200,140; `timeoutTitle` y 160 `MPTimeout` "Connection lost..";
`timeoutCounter` y 180, text set every pass (`0x449307`) to
`MPRetry` "Retrying .. %d seconds" with `0x6b9460 − seconds` (6 on LAN).

**The Cancel button's `BUTTON` header line is missing in the 1.06 file**
(the block follows `END_TEXTBOX` directly), so no button is created
(doc 03 loader rule). Corrects doc 57 §1.7: the box has no Cancel; the
player can only wait for the reconnect or the drop. Opened (JumpToMenu +
immediate navigation) when the timeout seconds counter becomes non-zero,
closed with **QuitMenu** (all menus) when it returns to 0 (`0x449402`),
which also closes an in-game menu that was open.

### 6.4 OUT_OF_SYNC (`oosync.uil`, 3 / 3)

Box4; `finished` y 180 `OutOfSync` "Connection failure : Game is out of
sync"; `resume` 250,225 140×20 `OK`: QuitAndJumpToMenu ABORT_MP_GAME,
gGameState Briefing. Opened as an overlay (`FUN_00470e60`) on the pass
after the mismatch while the end status is not 8 (doc 57 §2.2). OK →
MAINMENU, MULTIPLAYERTYPE, IPXLOBBY. The message line "Game is out of
sync" (`ModelOutOfSync`) comes first.

### 6.5 Chat: F1..F6 and GAMEMESSAGE (verified)

**Keys** (end of `FUN_004433a0`). Only in a network game with
no menu open (`mgr+0x110 == 0`) and the dev flag `0x7984d0` clear (it
always is: nothing writes it, doc 57 §3.7):
- F1..F5 (`0x798400..0x798404`), not after a loss (gLose `0x6b8278`):
  n = active humans (`+0x37`) among records 0..humans − 1, me included.
  Key k (0-based) works only if k < n − 1. The target is the **k-th
  active human other than me, in side order**. gSendingToPlayer = his name
  + `" : "`, target id `0x797e18` = his DirectPlay id. Players who left
  drop out, so the keys shift.
- F6 (`0x798405`): gSendingToPlayer = text `ToAll` "To all : ", target 0.
  **No gLose test**: a defeated player can still chat to all.
- Then GAMEMESSAGE opens as an overlay (`FUN_00470e60`).

**GAMEMESSAGE** (`gamemsg.uil`, 3 / 3):

| Element | Pos, size | Font | Commands |
|---|---|---|---|
| `playerName` TEXTBOX_L | 10,22 120×20 | Green | `RedrawEachTime`, `GrabBgdEachTime`, `EnterText gSendingToPlayer` |
| `msgEdit` EDITBOXFOCUS | 140,22 280×20 | Green | `MaxCharacters 64`, `RedrawEachTime`, `GrabBgdEachTime`; on Enter: `SetFromUIStringSpecial gNetMessageString`, `SendInGameMsgTo`, `BackMenu` |

While GAMEMESSAGE (or PAGE_INGAME) is open the message lines move down by
20 px (`FUN_004255e0` 0x4256a0; ours: `game_screen.cpp:1141`).

**GAMEMESSAGE does not stop the game** (verified, found in the first
playtest 2026-10-15, where ours froze the picture):
- `FUN_00470f90(1)` sets the menu flag `mgr+0x110`, then clears it again
  when the opened page is GAMEMESSAGE or PAGE_INGAME. So in the game loop
  (`FUN_00448550`) the game view is drawn, the tooltip runs and
  `FUN_004433a0` (mouse orders, edge scrolling) runs as with no menu; the
  UI manager draws the box over the view every loop.
- Keys: the window procedure (0x4a333b) writes the key-down array
  `0x798390` only while menu slot 0 is empty, **except Esc (0x1B) and
  Enter (0x0D)**. So letter hotkeys, F-keys and arrows do not reach the
  game while the box is open; the typed characters go to the edit box.
- Esc with GAMEMESSAGE or PAGE_INGAME open: command 0xDB QuitMenu
  (0x4497f7), so the box closes without sending.

**Send** (`FUN_0047bae0(target)`): wire text = `<my name> : <text>`;
shown locally as a message line; record type 0x12 sent reliably to the
target or to all. Texts starting with `0xAC` (system lines) are sent as
`<my name> 0xAC<key>` and **not** shown locally.

**Receive** (`FUN_0045c740`, misc type 0x12, during the UI pass): play
`S_CHATMSG`; a text with `0xAC` is cut there and the translated key
appended: `<name> ` + `" has left the game."` → "Name  has left the
game." (two spaces: the sender's wire text is `<name> ` + `0xAC<key>`,
`FUN_004b08b0` is `strtok` with `"\xAC"` (`0x4e3bb4`), and the
`text.uib` values of PlayerLeft / PlayerRetired / PlayerDefeated start
with a space; verified); then a message line (doc 47: 5 lines, 200
ticks).

**Empty Enter** (verified): Enter runs the focused edit box's commands
with no test (`FUN_0043cb70` `'\r'`), and SendInGameMsgTo (`0x48317e`)
calls `FUN_0047bae0` at once, so an empty box still sends and shows
"<name> : ".

**Colour:** every caller passes a colour (chat: the sender's lobby
colour) but `FUN_00425720` never reads it (doc 47 §B.1). **All chat and
system lines use the normal message-line colour.** Corrects doc 57 §3.7.
A private message looks the same as one to all on the receiving side.

### 6.6 Alliances (A key; verified)

`FUN_00459b50(gSideId)`, network only, not after a loss: for sides
0..player blocks − 1 except me, the first **selected** unit of that side
(unit +0x10 ≠ 0x11), else the first selected building (+0x30 ≠ 0x11),
decides the target.
Nothing selected of another side → nothing happens. Order 0x1B {side me,
+0x26 target}.

ProcessOrder case 8 (runs on every machine in lockstep, so **every
player sees the lines**):
- I am hostile to the target, the target is not an AI side and the game
  would not end (`FUN_004553e0`): play `S_CHATMSG`, line `"%s %s %s"` =
  `<my name> has allied with <target name>` (`Ally`), one-way alliance,
  my units drop targets on that side. Otherwise: line `UnableToAlly`
  "Unable to ally." (no sound).
- I am allied: `<my name> declares war on <target name>` (`BreakAlly`),
  and `<target name> declares war on <my name>` if he was allied to me;
  hostile both ways. **S_CHATMSG plays here too** (`0x45838e` /
  `0x458397`, after the lines; corrected 2026-10-15, the first version of
  this section said no sound).
- The sound goes through the voice queue `FUN_0046f820(sample, 0, 0, 0)`:
  mode 0, so it only plays when no voice waits or plays. Received chat
  lines use the same sound.

Names are the network names (`0x798630 + side·0x3C`); outside a network
game the exe would use gNetPlayerName and text `0x4de1e8`.

### 6.7 Other in-game lines

| Event | Line | Shown on |
|---|---|---|
| a player drops after the timeout (doc 57 §1.7) | `LostOnePlayer` "You have lost connection with one of the players" | the dropping machine(s) |
| start sync: someone missing after 20 s (doc 57 §3.3) | `LostSomePlayers` "You have lost connection with one or more players" | each machine that misses someone |
| Abort | chat `<name>  has left the game.` | the others |
| Surrender | chat `<name>  has retired from the game.` | the others |
| defeat (loss path `0x449f24`) | chat `<name>  has been defeated.` (`PlayerDefeated`) | the others |
| mismatch | `ModelOutOfSync` "Game is out of sync" | the machine that saw it |

`PlayerEliminated` " has been eliminated from the game." is never used:
the key is in `text.uib` only (no string in the exe, no `.uil` names it).

### 6.8 MULTIPLAYER RESULTS (SCORE after a network game)

Same page and code as doc 56 §1, with these differences:
- title `TitleMultiScore` "MULTIPLAYER RESULTS" (underlined);
- rows: every side with the lobby "human present" flag (`0x6b9230 +
  s·0x3C`, set by the handshake) or an active AI; a human who left keeps
  his row;
- name: AI active → "Computer"; else the network name (used as a
  `sprintf` format at `0x43e5ef`, but the name box takes no `%`,
  `NoOtherChars`). The AI flag is the AI block's +1 read when SCORE is
  built (`FUN_0043dcb0`), so a side taken over by the AI after an Abort
  shows **"Computer"** (verified);
- colour = the lobby colour byte; sorting by finishing place as doc 56
  §1.4 (the `idx = 0..n − 1` quirk is harmless while the row sides are
  0..n − 1: humans first, then AIs);
- Continue → IPXLOBBY (§3.7). No SCORE after Abort or OUT_OF_SYNC.

The in-game score box (sidebar control 6) uses the network branch of doc
56 §4.2.

## 7. Where our connection step fits

Our transport (design doc §6) replaces DirectPlay: one host is the hub,
others join from a LAN list or by address. The game rules of §5, the
pages and the texts must stay; the `-Dune Lobby-` session and DirectPlay
names do not exist.

### 7.1 Mapping the original model

| Original | Ours |
|---|---|
| `gameList` rows = hosts in `-Dune Lobby-` | games answering our LAN probe (`LobbyClient::games()`), plus games found at a typed address (§7.2). Row: `<host>'s Game` + `" n/6"`, host colour, `icon_d2` |
| row 1 "Dune2000 Lobby" and its player list (people in the lobby) | keep row 1. Its player list = other IPXLOBBY users on the LAN, if we add a small LAN presence beacon (name, house, colour), else only me. **Decision for the user** |
| lobby chat to group 0 | LAN broadcast of chat lines from IPXLOBBY (with the presence beacon), else local only. **Decision** |
| ClientWantsToJoin / 0x1C / JoinSession | `LobbyClient::join` + the version / manifest check (`WrongNetworkVersion`) and the same refusals (`GameIsFull`, `NameIsUsed`, `NoGameSelected`, `NoNameEntered`) and the lowest-free-colour fix |
| messages 0x16..0x18, ResetAccept | `LobbyPlayer::ready` with the §5.4 rule; `LobbyHost::can_start` = all clients ready on the current settings, ≥ 2 humans, a map |
| Reject | `LobbyHost::kick` |

### 7.2 The address entry (the original has none)

The original has no control for an address. Options, least intrusive
first:

1. **Saved addresses only, no new control.** Addresses come from
   `settings.ini` (and a `--join host[:port]` option). IPXLOBBY probes
   them whenever it refreshes; a host that answers appears in `gameList`
   exactly like a LAN game. The page looks 100 % original. Weak point:
   the address must be typed outside the game.
2. **One added edit box on IPXLOBBY** (recommended): an `EDITBOX
   address` in the free strip above the game list, e.g. 100,78 217×20
   with a TEXTBOX_L label left of it (nothing original is drawn at x
   26..317, y 76..98; built: label at 32,81, box at 100,78 217×20,
   `ui/menu_hooks.cpp`). Enter
   in it probes that address; when the host answers, its game appears in
   `gameList` and is joined with the normal Join / double-click flow, so
   every original message and rule stays. Typed addresses are remembered
   in `settings.ini` and probed again next time (option 1 included). The
   label needs one string of ours (no `text.uib` key fits: there is no
   "address" text; `UI_PhoneNumber` belongs to the modem page).
3. Re-using the MODEM_JOIN page (`ModJoin.uil`: phone edit box, saved
   list, Save / Delete) as an address book behind the Modem/Serial
   button. Faithful controls, but every label says "phone" / "Dial", and
   it moves joining off IPXLOBBY. Not recommended.

**Recommendation: option 2 (one edit box + label on IPXLOBBY, addresses
remembered, which also gives option 1).** This is a user decision.

### 7.3 Original parts that are unused in our build

| Part | Our build |
|---|---|
| MULTIPLAYERTYPE `internet` (Westwood Online is gone) | **removed**, as the exe does when no internet provider exists (§2.2) |
| MULTIPLAYERTYPE `serialModem` (and the Null Modem replacement) | **removed** the same way (we have no serial / modem). The gap stays, as in the exe |
| `network` button text "LAN" | kept; it also covers internet play by address |
| pages INETLOGON, INETLOBBY, HOSTINETGAME, CLIENTINETGAME, CHATSERVER, HOSTGAME, HOSTPRIVATE, JOINGAME, INTERNET_CONNECTING, ERROR_CONNECTING, AWAITING_RESPONSE, DOWNLOADING_RESULTS, ENTERING_ONLINE, INT_TIMEOUT, PAGE_INGAME, NO_INTERNET … | unreachable; no code |
| CONNECTION_TYPE, MODEM_JOIN, SERIALLOBBY, HOST_MP2, JOIN_MP2, SERIAL_CONNECTING, MODEM_DIALING, MODEM_PROBLEM, SERIAL_PROBLEM, ABORT_CONNECTION, AWAITING_HOST, NO_NUMBER, WRONG_VERSION page | unreachable once `serialModem` is gone; no code |
| `Dune2000CdCheck`, CD_REQUIRED, FAILED_CD_CHECK | no-op (data folder validated at start) |
| F7 / F8 paging, ladder orders 0x33 / 0x34, `SendGameOptions` (0x5C, HOST_INFO) | Internet only; never reached |
| `CloseSession` (0x3D), `ReEnterLobby` (0x35), `DestroyWigNet` | transport calls of ours, or no-ops (`DestroyWigNet` in 1.06 does not even destroy the wrapper: `FUN_00481030(0)`) |
| strings `DirectPlayError`, `ConnectError` | kept as the texts of WIGNET_ERROR / FAILED_MP_CONNECT |

## 8. What our MenuRunner lacks (checked in `src/app/menu_runner.cpp`)

Supported already: navigation (except LOBBY_SCREEN / ABORT_MP_GAME for
gGameType 2), `SetValue*`, `*Special` setters, `CheckIfValue*`,
`UnCheck`, `SetStringInTextBox`, `GetStringIfValue*Init`,
`GetStringInit`, `SetFromUIStringSpecial`, `SetFromUIValueU1To`, BF
`TOGGLE`, `GetMapList` / `GetMapFromList` / `StaySelected`,
`ParseEndCommands`, `EnterText g<Name>`, tooltips, multistate boxes,
`PlaySample`, double-click detection, LISTBOX_CROPPED parsing.

Missing:

| Item | Needed for |
|---|---|
| `EnterLobby`, `GetLobbyList`, `RefreshLobbyList` (listed as init, no action) | IPXLOBBY, host / client pages |
| `RefreshChatList`, `SendMsgToAll`, `DisplayNetMessage` | all lobby pages |
| `SendUIMsgToAll <name>` (ClientWantsToJoin / Quit, AcceptSettings, Reject, UnitCount, TechLevel, StartingCredits, AIPlayers, Crates, Worms) | all lobby pages |
| `JoinSessionDbl` (double-click commands of a list) | IPXLOBBY |
| `AlterHandicap` (single click, x ≤ list x + 20) | host page |
| `StartSession`, `JoinSession`, `BreakFromSession`, `BreakFromLobby`, `CreateWigNet`, `DestroyWigNet` | lobby pages |
| `StartGame` for gGameType 2 (today it is the skirmish path) | host Start |
| `RefreshValueU1/U2/BF/CH` and a refresh pass (`FUN_00474b60`) on lobby events | client page |
| `SetEditFocus`, `DisableThis`, `LoadGfxIfValueU1Is`, `NoHighlight` behaviour | IPXLOBBY, client page |
| `WigNetError`, `SendInGameMsgTo`, `LeftGame`, `AbortGame` | WIGNET_ERROR, GAMEMESSAGE, CONFIRM_ABORT_MP |
| `MaxCharacters`, `RedrawEachTime`, `GrabBgdEachTime` (listed as init; check they act) | GAMEMESSAGE |
| per-row list colour (`NetColour<c>`), row icons (`icon_d2`, `icon_ui`, `hand1..3`, `icon_ac`, `icon_ho`) and the drawn suffixes `" %d/%d"` / `" (%s)"` | lobby lists (`ElementState::item_white` only knows white) |
| a button whose gfx, font colours and commands change at run time (Start, Accept) | §3.2, §5.4 |
| per-row tooltips in `playerList` | doc 55 §4 mentions them |
| the NoChangeOptions click rule | client page |
| MULTIPLAYERTYPE hook: remove `internet` / `serialModem` (§7.3) | `ui/menu_hooks.cpp` |
| QuitAndJumpToMenu ABORT_MP_GAME / SCORE and BackMenuAndJumpTo LOBBY_SCREEN for gGameType 2 (§3.7) | after a game |
| INGAME_MP in the outside-click close rule | `handle_input` |

## 9. Implementation checklist

`src/net/lobby.hpp` / `lobby.cpp`:
- [ ] Defaults from the exe: credits 4000 (gNetStartingCredits, not
      5000), units 6, tech 7, AI 0, crates off, worms 0, handicap 1.
- [ ] Join refusals in the original order: no name → `NoNameEntered`, no
      game → `NoGameSelected`, 6 humans → `GameIsFull`, same name →
      `NameIsUsed`, version / manifest → `WrongNetworkVersion`.
- [ ] Colour fix on join: lowest free colour 0..7 in that game.
- [ ] House / colour / name fixed after joining (`set_me` only before).
- [ ] `ready` cleared on every ResetAccept trigger of §5.4; `can_start`
      = every client ready on the current settings + ≥ 2 humans + a map;
      the counter quirk kept (§5.4).
- [ ] Humans + AI ≤ 6: lower `ai_players` on joins.
- [ ] Host leaves → every client back to IPXLOBBY without a message;
      Reject → the client back to IPXLOBBY without a message.
- [ ] Client quit refused while starting; leave anyway after 10 s.
- [ ] Optional (§7.1 decision): LAN presence beacon and lobby chat for
      the "Dune2000 Lobby" row.

`src/app/menu_runner.cpp` / `.hpp`, `src/ui/render.cpp`, `ui/menu_hooks.cpp`:
- [ ] The commands and list features of §8.
- [ ] IPXLOBBY lists (§2.3) fed from `LobbyClient`; HOSTHEADTOHEAD /
      CLIENTMULTIPLAYER lists from `LobbyState`.
- [ ] Start / Accept button states (§3.2, §3.4).
- [ ] The address edit box of §7.2 (after the user's decision).
- [ ] MULTIPLAYERTYPE: remove `internet` and `serialModem`.
- [ ] Routing of §3.7; NO_FIND_PLAYERS (modal, then ABORT_MP_GAME),
      FOUND_PLAYERS texts, WIGNET_ERROR (OK → MAINMENU).
- [ ] Focus: chat focused when the name is set (§2.3).

`src/app/game_screen.cpp`:
- [ ] Options opens INGAME_MP in a network game; outside click closes it.
- [ ] CONFIRM_ABORT_MP: Abort = chat PlayerLeft + kind 0 + ABORT_MP_GAME;
      Surrender = chat PlayerRetired + order 0x31 + kind 1 + gLose, game
      goes on.
- [ ] MP_TIMEOUT without a Cancel button; counter text each pass; QuitMenu
      when the timeout state ends.
- [ ] OUT_OF_SYNC overlay after the `ModelOutOfSync` line.
- [ ] F1..F6 → GAMEMESSAGE (§6.5 target rule, F6 allowed after a loss),
      64 characters, local echo, S_CHATMSG on receipt, `0xAC` keys, one
      message-line colour for all.
- [ ] A key → order 0x1B with the first-selected-object rule; the ally /
      war / unable lines, S_CHATMSG for "has allied with" and for war.
- [ ] Defeat → chat PlayerDefeated to all.

`src/app/sidebar.cpp`:
- [ ] Score box (control 6) network names (doc 56 §4.2).
- [ ] Options button → INGAME_MP in a network game (if the button path is
      separate from the key).

SCORE (`menu_runner.cpp` score set-up, doc 56 checklist item 5):
- [ ] "MULTIPLAYER RESULTS", network names, "Computer" for AI-taken
      sides, Continue → IPXLOBBY.

## 10. Open points

None left (closed 2026-10-16). How each former point was settled:

- Provider flags: the NT 4 test and the provider walk (§2.2); DirectPlay's
  list is not applicable.
- DisplayNetMessage colour: the list's font colour; `responseList` keeps
  only its visible rows (§2.3).
- Player row order: DirectPlay's enumeration, not applicable; ours join
  order (§3.1).
- The Accept counter quirk: verified and kept (§5.4).
- Host human count on a type-3 message and on a client crash: verified;
  both need DirectPlay's shared lobby session, not applicable (§4, §5.6).
- A client without the map: verified and built (§4, message 6).
- FAILED_MP_CONNECT and NOT_ALL_FOUND: never opened in 1.06 (§3.6).
- Value bars on the client page: a client cannot move them; the press
  shows NoChangeOptions (§5.1, `FUN_0048c640`).
- The double space in received system lines: verified (§6.5).
- An empty GAMEMESSAGE Enter sends "<name> : " (§6.5).
- "Computer" for AI-taken sides on SCORE: verified (§6.8).
- Escape in GAMEMESSAGE: QuitMenu, nothing sent (§6.5).
- The corrections listed for doc 57 and the design doc are made there.
