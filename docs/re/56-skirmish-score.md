# 56. The skirmish score: SCORE page, finishing places, map-toggle box

Dune 2000 1.06 US, `DUNE2000.DAT`, base 0x400000. Covers the SCORE page
shown after a skirmish (`score.uil` + `FUN_0043d7e0` / `FUN_0043dcb0`),
the finishing places `0x797b70` (`FUN_0044c520`), `FUN_004412c0`, and the
in-game score box behind sidebar control 6. All verified by decompile and
disassembly (all open points closed 2026-10-16).

Corrections to earlier docs:
- Doc 51 §6 says SCORE has the MoonSpin animation and a "group
  `scoreMP`". Wrong. `FUN_0043d7e0` **replaces the background** with
  `scoreMP` and **removes** the `dune` (MoonSpin) animation (§1.1).
- Doc 52 §5.5 calls the icon `mp_or` / doc 51 `mp_od`. The string is
  **`mp_or`** (`0x4df74c`).
- Doc 52 §5.4 says there is no SCORE after a loss and "No". Doc 51 §6
  (disassembly `0x47402d`: gGameType = 1 → push `"SCORE"`) is right: No
  → MAINMENU + SCORE.
- Doc 52 §6 table: `FUN_004412c0` is **not** a skirmish function. It
  only feeds the Westwood Online result upload (§3).

## Functions and globals

| Address | Role |
|---|---|
| `FUN_004359a0` 0x436169 | menu create: name `SCORE` → menu +0x30 = 1; gGameType 0 → `FUN_0043d820` (campaign), else `FUN_0043d7e0(menu)` |
| `FUN_0043d7e0` | `0x51625c` = menu; background `scoreMP`; remove animation `dune`; `FUN_0043dcb0` |
| `FUN_0043dcb0` | rows data, score numbers, sort, then all text boxes and icons |
| `FUN_0043ee80` | campaign count-up; returns at once when gGameType ≠ 0 |
| `FUN_0043dc60` | close (menu +0x30): campaign clean-up only (gGameType 0) |
| `FUN_0044c4f0(side)` | `if place[side] == 0xFF: place[side] = next++` |
| `FUN_0044c520` | each tick: every dead side gets a place |
| `FUN_004481b0` | `0x797b70[0..7]` = 0xFF (two dword writes), other lobby resets |
| `FUN_004412c0` | per-side score for the WOL ladder upload (§3) |
| `FUN_00425c60` | in-game score box in the radar area (§4) |
| `0x797b70[8]` | finishing place per side (u8, 0xFF = none) |
| `0x797e38` | next place (u8), 0 at game start (`FUN_004470a0` 0x4470e4) |
| `0x797c00 + side·0x3C` | score record (doc 24 §2.3 layout) |
| `0x6b9230 + side·0x3C` | lobby record +3: **network human present**. Set only by the network set-up (`FUN_004470a0`); `FUN_004481b0` clears it at every game start, so it is **0 in skirmish** |
| `0x4f4118 + side·0x1DB8 + 1` | AI active (our `ai_[s][1]`) |
| `0x6b922d + side·0x3C` | lobby record +0: colour slot (doc 51 §4.2) |
| `0x6b93f8` | gNetPlayerName |
| `0x798630 + side·0x3C` | network player name (not used in skirmish) |
| `0x51643c` | map toggle on (u8) |

## 1. The SCORE page after a skirmish

### 1.1 `score.uil` and `FUN_0043d7e0`

`score.uil` alone: background `scoreui`, ANIMATION `dune` (499,16) 75×75
MoonSpin, BUTTON `continue` (533,367) 70×22 `Continue2`,
`BackMenuAndJumpTo LOBBY_SCREEN`, `EnterActions`. `score.aud` plays (doc
04; our `top_changed`).

`FUN_0043d7e0(menu)`:
1. `0x51625c` = menu.
2. `FUN_00437b60("scoreMP")`: frees the old background and loads
   `UI_gfx\scoreMP` (`SCOREMP.TGA`, 640×400) instead of `scoreui`.
3. `FUN_00441240(menu, "dune")` finds the element named `dune` in the
   animation list (+0x624, 10 slots); `FUN_00438e00` deletes it. So
   **no MoonSpin** on this page.
4. `FUN_0043dcb0`.

The page is static: `FUN_0043ee80` (called by the menu draw each update)
does nothing unless gGameType = 0. So **no count-up, no `S_SCORETICK`, no
high-score table, no `hs.dat`**. Continue (or Enter) is the only action.

### 1.2 Which sides get a row (0x43dcf9..0x43dd28)

For side s = 0..7 (in order):

```
row(s) = lobby[s].human (0x6b9230 + s·0x3C) != 0
      || ai[s].active   (0x4f4119 + s·0x1DB8) != 0
      || (gGameType == 1 && s == gSideId (0x798544))
```

In skirmish the first term is always 0, so the rows are **side 0 (the
player) and the AI sides 1..gNetAIPlayers**. The AI active byte stays set
after a side is beaten (doc 52 §5.3), so **beaten AIs keep their row**.
`ai_flag[s]` (stack) = AI active, used for the name.

For a row side: record +0x00 = 1, +0x01 = house `0x8cd4f0[s]`, +0x02 =
alliance `0x798830[gSideId·8 + player+0x24252]` (unused here), then the
counters (player block `0x798870 + s·0x26990`):

| Rec | Player | Counter |
|---|---|---|
| +0x04 | +0x24a08 | units lost |
| +0x08 | +0x24a0c | buildings lost |
| +0x0C | +0x2477c | spice harvested |
| +0x10 | +0x24c90 | units killed |
| +0x14 | +0x24c94 | buildings killed |

Non-row sides get +0x00 = 0. n = number of rows (u8).

### 1.3 The computed score (not shown)

Totals over the rows: U = Σ +0x04, B = Σ +0x08, S = Σ +0x0C. Constants
`0x4dedc4/c8/cc` = 150 / 100 / 50. For each row (u32 products, unsigned
division, as in the campaign, doc 24 §2.3):

```
+0x20 = (U ? ((U − units_lost)·n·150) / U : 0) − units_killed
+0x24 = (B ? ((B − bldg_lost)·n·100) / B : 0) − bldg_killed
+0x28 = S ? (spice·n·50) / S : 0
sum   = +0x20 + +0x24 + +0x28
bonus = place == 0xFF ? sum·20 : (place − 1)·(20 / (n − 1))·sum   // signed
+0x38 = bonus / 100 + sum
```

Nothing reads +0x20..+0x38 on this page (the only other users are the
campaign functions, which do not run in skirmish). **No score is shown.**
We do not need to compute it. (With n = 1 and a place ≠ 0xFF the `20 /
(n − 1)` would divide by zero; it cannot happen: one row means the player
is alone and has won.)

### 1.4 Sort order (0x43df4e..0x43dfd0)

```
idx[k] = k  for k = 0..n−1           // row index, used as the side number
for i = 0..n−1:
    for j = i..n−1:
        if place[idx[i]] < place[idx[j]]: swap(idx[i], idx[j])   // u8 compare
```

- **Descending place**: 0xFF (never beaten) first, then the side beaten
  last, …, the side beaten first at the bottom.
- Ties: only 0xFF can tie (other places are unique). This exchange sort
  always pulls the **first** remaining 0xFF side to position i, so tied
  sides keep **side order** (the player, side 0, first).
- Quirk: `idx` starts as 0..n−1, not the row sides. It is right only when
  the row sides are 0..n−1. In skirmish they are (0..gNetAIPlayers), so
  nothing visible. Keep it in mind for other game types.

Examples (1 AI): won → player, AI. Lost → AI (0xFF), player. Aborted →
player, AI (both 0xFF). Lost by the stalemate tie-break (doc 52 §5.2)
while both still have units → both 0xFF → player, AI.

### 1.5 Elements built

All are added at run time: text boxes `FUN_004384a0` (AddTextBox,
constructor `FUN_0048b9c0`), icons `FUN_00437f30` (group box,
`FUN_004860e0`). Text box arguments map to: +0x77c background, +0x780 /
+0x784 frame (and +0x780 = underline colour), +0x788 text colour, +0x78c
text key, +0x85c alignment (0 left: x + 5; 1 right: right − 5 − w; 2
centre), +0x864 underline. The box keeps the x and width passed
(`FUN_00482780`); the 5 px margin is added only when drawn
(`FUN_0048bb10`), so the name box at 113 draws its text from 118 and the
first number column (235) from 240, as the capture of the original
shows (2026-10-16; ours had added the margin twice, at 123 / 245). Font +0x775 is 0 for every element (FONT.FNT,
our `menu_font()`), text vertically centred (`FUN_0048bb10`). Transparent
background → no fill and no frame; the box saves the background under
it. An underline (`FUN_0042d450`, 1 px, text width) is drawn only with
that saved background, at y = top + centring + font height.

Colours (`colours.uib`): Transparent (index 0), White (255,255,255),
LGrey (200,200,200), NetColour0..7 = (104,128,160), (208,80,72),
(104,160,128), (120,16,136), (136,136,136), (128,56,16), (168,136,8),
(144,96,72).

**Fixed boxes** (all Transparent background, text White, frame
White/Transparent, font 0):

| Name | Text key → text | x, y | w × h | Align | Underline |
|---|---|---|---|---|---|
| `title` | `TitlePracticeScore` → "Practice Game Results" (gGameType 1), else `TitleMultiScore` → "MULTIPLAYER RESULTS" | 20, 20 | 600 × 25 | centre | **yes** (White) |
| `names` | `PlayerNames` → "Player Names" | 87, 53 | 150 × 24 | centre | no |
| `casualties` | `Casualties` | 235, 53 | 133 × 24 | centre | no |
| `buildings` | `Buildings` | 368, 53 | 124 × 24 | centre | no |
| `spice` | `Spice` | 492, 53 | 59 × 24 | centre | no |
| `unit_dest` | `Inflicted` | 235, 77 | 69 × 24 | centre | no |
| `unit_cas` | `Received` | 304, 77 | 64 × 24 | centre | no |
| `build_cas` | `Lost` | 368, 77 | 60 × 24 | centre | no |
| `build_dest` | `Destroyed` | 428, 77 | 64 × 24 | centre | no |
| `harvested` | `Harvested` | 492, 77 | 58 × 24 | centre | no |

(`Lost` is a duplicated key in text.uib; the first match, value "Lost",
is used.)

**Rows**, k = 0..n−1 in sorted order, side s = idx[k], y = 101 + 24·k:

| Name | What | x, y | w × h | Colour | Value |
|---|---|---|---|---|---|
| `icon<k>` | group box, image by house `0x8cd4f0[s]`: 0 `mp_at`, 1 `mp_hk`, 2 `mp_or` (24×24 BMPs, drawn keyed with shadow like any group box) | 87, y + 1 | 24 × 24 | Transparent / Transparent | — |
| `name<k>` | text, left | 113, y | 124 × 24 | **`NetColour<c>`**, c = lobby colour byte `0x6b922d + s·0x3C` | see below |
| `unitdest<k>` | text, left | 235, y | 69 × 24 | LGrey | `%d` units killed (+0x10) |
| `unitcas<k>` | text, left | 304, y | 64 × 24 | LGrey | `%d` units lost (+0x04) |
| `buildcas<k>` | text, left | 368, y | 60 × 24 | LGrey | `%d` buildings lost (+0x08) |
| `builddest<k>` | text, left | 428, y | 64 × 24 | LGrey | `%d` buildings killed (+0x14) |
| `spice<k>` | text, left | 492, y | 58 × 24 | LGrey | `%d` spice harvested (+0x0C) |

The colour is the slot colour, not the house: an AI with the player's
house still has its own colour. With a house > 2 the icon name buffer is
left unset (cannot happen in skirmish).

**Name text** (written into +0x790 with +0x858 = 1, dirty +0x18 = 1):
- AI active: `UI_Computer` → "Computer";
- else gGameType = 1 and s = 0: gNetPlayerName `0x6b93f8` (the current
  value, doc 51 §7);
- else: `sprintf(buf, name_of_side)` (`0x798630 + s·0x3C`, the network
  name used as a format string). Not reached in skirmish.

Counters are signed `%d`. Up to 6 rows in skirmish (y up to 221).

## 2. Finishing places (`0x797b70`, `FUN_0044c520`)

- **Reset** at every game start, all game types (campaign, skirmish,
  restart): `FUN_004470a0` sets `0x797e38` = 0 and calls `FUN_004481b0`
  (all 8 places = 0xFF).
- **Assigned** by `FUN_0044c520`, called **once per game tick** at the
  end of the tick block (`0x449b28`, after the tick & 7 player's
  `FUN_0046d180` / `FUN_0046ddc0` / `FUN_0046def0`):

  ```
  for s = 0..7:
      if has_buildings[s] (0x6b87c0) == 0 && has_units[s] (0x6b8268) == 0:
          if place[s] == 0xFF: place[s] = next++      // FUN_0044c4f0
  ```

  The flags are those of doc 52 §5.1 (all 16 checks at set-up, then one
  side/mode per tick). So a side is placed in the tick its last refresh
  shows it dead. Two sides placed in one tick get side order.
- **Unused sides** (no units, no buildings) are dead from the set-up, so
  on the first tick they get places 0, 1, … in side order. With 1 AI:
  sides 2..7 get 0..5; the next beaten side gets 6.
- Places never change back: the only writers of `0x797b70` are the
  reset `FUN_004481b0` (`0x4481ba` / `0x4481c7`) and `FUN_0044c4f0`, which
  writes only a 0xFF place. So a beaten side keeps its place even if it
  ever had a counted unit again; whether that can happen does not matter
  for the places.
- Places keep running after the end: the game runs on behind the end
  banner / REPLAY_MISSION, so the page shows the state when SCORE opens.

Local player:

| End | Player's place | AIs |
|---|---|---|
| Won | 0xFF (still alive) | all beaten, places in the order beaten |
| Lost | the next place when the player's flags go to 0 (the same tick the loss is detected) | survivors 0xFF |
| Lost by stalemate tie-break | 0xFF (units remain) | survivors 0xFF |
| Aborted | 0xFF (if alive) | survivors 0xFF, beaten ones placed |

Users: the SCORE sort (§1.4), the map-toggle box (§4, hides beaten AIs),
the WOL upload (§3). Not saved: no save code touches `0x797b70` /
`0x797e38` (only the xrefs above). **The load resets them**: the game
start `FUN_004470a0` (called at `0x4486a9` before the load at `0x448bc4`)
sets `0x797e38` = 0 and calls `FUN_004481b0`, and the load
`FUN_004418b0` calls `FUN_004481b0` again (`0x441f08`). So a loaded
skirmish places every dead side (beaten or unused) again on its first
tick, in side order, and the beaten order is lost. Ours does the same
(the places are not in our save; golden test in
`tests/campaign/test_front_end.cpp`).

## 3. `FUN_004412c0`: the Westwood Online ladder scores

Only caller: `FUN_0040d2d0` ("Scores", `SendGameResultsToServer`) at
`0x40d994`. That runs only at the end of an **Internet game**
(gGameType 5, `0x44a30b` with `0x4dfaf4` set, between "Starting
SendGameResultsToServer" and "DONE ..."). **Never in skirmish.**

What it does: the same record fill as §1.2 with a different filter
(`(gGameType ≠ 1 && human) || (gGameType == 1 && (AI active || s ==
gSideId))`, so its skirmish branch is dead), plus +0x18 = player
+0x24780 and +0x1C = +0x24784, and the §1.3 numbers without the place
bonus, stored in +0x34 and `0x798810[s]` (0 for non-row sides). The
upload then sends per side `SCR<n>` = place rank, +0x1C, +0x08, +0x14,
+0x18, +0x04, +0x10, +0x0C. We skip all of it.

+0x24780 / +0x24784 are inferred **units built / buildings built** (the
debug box header `"s, ub bb, ul bl, uk bk"`, §4.4).

## 4. The in-game score box (sidebar control 6, `FUN_00425c60`)

### 4.1 Toggle (`FUN_004433a0` 0x44471f)

On a left press, if not (gGameType = 0 and `0x4eb008` set; `0x4eb008`
is 1 in retail, so the campaign ignores it) and control 6 is hit
(`FUN_00443040(6, 1)`, the usual state-1 / pixel test, doc 11 §5):
`0x51643c = !0x51643c`. No sound in this branch. It is cleared at every
mission start (`0x448987`). While on, radar clicks are off (doc 25 B.3).

### 4.2 Draw (game loop `0x449036`, step 5 of doc 11 §2)

In the game view block (no menu open), instead of the radar:

```
if (tick 0x5173f4 & 0x10) == 0:            // redrawn 16 ticks of every 32
    blit grille 0x782768 opaque at (497, 8)  // flags 0,0: as radar-on step 1
    text "Name"  (UI_Name)  at (497, 8)
    text "Score" (UI_Score) right-aligned, right edge 497 + 128 = 625, y 8
    y = 28
    for s = 0 .. 0x798628 − 1 (player-block count):
        if s != gSideId && ai[s].active != 1 && !lobby[s].human: continue
        colour = 0x795518[s]   (16-bit player colour; 8-bit: 0x786ff0[s])
        skirmish (0x7984c4 == 0):
            s == gSideId               → name = gNetPlayerName
            ai active && place == 0xFF → name = "Computer"
            otherwise                  → no line (y unchanged)
        network: ai active && place 0xFF → "Computer";
                 else place 0xFF → network name 0x798630 + s·0x3C;
                 else no line
        text name at (497, y) in colour
        text "%d" of (units killed +0x24c90 + buildings killed +0x24c94)
             right-aligned to 625, y, in colour
        y += 12
    0x51647c = 1                            // the radar counter (doc 11 §4)
```

- Text by `FUN_0042dc00` (left) / `FUN_0042dd50` (right: x − width),
  **font 0**, no clipping. In-game, font 0 is `font` (FONT.FNT, loaded by
  `FUN_00470710` at `0x470865`, name at `0x4de77c`); font 1 is FontCol.
  `FUN_00431e40` writes every nonzero glyph pixel **solid** in the colour
  (no FONTCOL shading). Our `small_font()` + `draw_text_solid`. The header
  uses colour −1 with RGB `0x4ddf38` = `00 C8 C8 DC` = **(200, 200, 220)**,
  converted by `FUN_0042f3d0`. Rows use the player colour value directly.
  (The first port used FONTCOL with its palette; fixed after the playtest
  found the text hard to read.)
- Order is **side order**, not by place. The player's row is always
  there (also after a loss). **A beaten AI disappears** from the list.
- The score is just kills: units killed + buildings killed.
- During the 16 ticks with `tick & 0x10` set nothing is drawn, so the
  box keeps its last image. Quirk: turning it on in that phase leaves
  the radar / logo on screen for up to 16 ticks.
- Turning it off: the counter is 1, so the radar-on path redraws the
  grille and the map, or the radar-off path starts the logo fade from 1
  (doc 11 §4).
- Strings: `UI_Name` "Name", `UI_Score` "Score", `UI_Computer`
  "Computer". `0x798628` = the player-block count: **8 in every game
  type**. The set-up clears it (`0x448811`) and creates sides 0..7 with
  `FUN_004a45d0` (thunk `0x455500`, loop `0x448826..0x448835`), which adds
  1 each (`0x4a4654`); the load restores it (`0x441a3f`).

### 4.3 Tooltip

Control 6's tooltip key is `MapToggle` (doc 11 §5), shown only outside
the campaign.

### 4.4 Debug variant (dead)

When byte `0x5178c4` is set: header `"s, ub bb, ul bl, uk bk"` and per
row `"%d, %2d %2d, %2d %2d, %2d %2d"` = side, +0x24780, +0x24784,
+0x24a08, +0x24a0c, +0x24c90, +0x24c94, left at x = 497. Nothing writes
`0x5178c4` (it is in .bss), so retail never shows this.

## 5. Per-side stats the sim must track

| Exe | Meaning | Changed by | Used by | Our `Player` / `World` |
|---|---|---|---|---|
| +0x24a08 | units lost | a unit dies, counted for its owner (the original side of a deviated unit) (doc 24 §5) | SCORE "Received" | `units_lost` (combat.cpp, capture.cpp, worm.cpp) — have |
| +0x24c90 | units killed | the attacker's side of a dying unit | SCORE "Inflicted", box score | `units_killed` — have |
| +0x24a0c | buildings lost | a building destroyed by damage, not sold (`FUN_0049d2e0`) | SCORE "Lost" | `buildings_lost` — have |
| +0x24c94 | buildings killed | the attacker's side, same place | SCORE "Destroyed", box score | `buildings_killed` — have |
| +0x2477c | spice harvested | +blob value per harvester unload step (doc 13 §2) | SCORE "Harvested" | `spice_harvested` (harvest.cpp:303) — have |
| `0x797b70[8]`, `0x797e38` | finishing places, next place | `FUN_0044c520` each tick (§2) | sort, box | **missing** |
| `0x6b87c0` / `0x6b8268` | has buildings / has units | doc 52 §5.1 | places | `has_buildings_` / `has_units_` — have |
| AI block +1 | AI active | set-up (doc 52) | rows, names | `ai_[s][1]` — have |
| `0x6b922d + s·0x3C` | lobby colour slot | skirmish set-up | NetColour | `World::side_colour` (= the slot in skirmish) — have |
| `0x795518[s]` | 16-bit player colour | colour set-up | box text | radar colours in `sidebar.cpp` — have |
| +0x24780 / +0x24784 | units / buildings built | — | WOL upload, debug box only | not needed |

## Implementation checklist

1. `src/sim/world.hpp` / `src/sim/mission.cpp`: add `std::array<uint8_t,8>
   place_` (0xFF) and `uint8_t next_place_` (0); reset at mission start
   (every game type, also restart). Add `assign_places()` = §2 loop.
2. `src/sim/world.cpp` `World::tick()`: call `assign_places()` at the end
   of the tick block, after the tick & 7 Starport / delivery block
   (`0x449b28`). Done: `World::tick` calls `assign_places()` right after
   that block, as `FUN_00448550` (the whole order: doc 19 §2.1).
3. `src/sim/save_state.cpp`: do not save them (§2: the exe resets them
   on load and re-places on the first tick). Done.
4. `src/app/menu_runner.hpp` `SideStats`: add `row` (§1.2 filter),
   `ai`, `colour` (slot), `units_killed`, `buildings_killed`, `place`.
   `GameScreen::score_stats()` (`game_screen.cpp:1751`) fills them; it is
   already called after `game.run()` in `main.cpp:206`.
5. `src/app/menu_runner.cpp` `open_menu` / SCORE (not SCORE_SINGLE) with
   gGameType 1: a `score_mp_setup(Menu&)` = §1.1 (background `scoreMP`,
   remove `dune`) + §1.4 sort + §1.5 boxes and icons (runtime text boxes
   with alignment, underline for the title, NetColour / LGrey text). No
   count-up, no high scores. Continue = existing `BackMenuAndJumpTo
   LOBBY_SCREEN` (doc 51).
6. `src/ui/*`: runtime TEXTBOX alignment 0/1/2 and the underline flag
   (+0x864), if not yet supported; GROUPBOX with an image drawn keyed
   with shadow.
7. `src/app/sidebar.cpp:275` control 6: when gGameType ≠ 0, toggle a
   `map_toggle_` flag on left press; clear it at mission start; block
   radar clicks while on (`game_screen.cpp:474` `radar_cell`).
8. `src/app/sidebar.cpp` `draw_radar` (l. 521): when `map_toggle_`, draw
   §4.2 instead (only when `(tick & 0x10) == 0`, else leave the pixels),
   and set the radar counter to 1.
9. Tests: places with 2 AIs (beaten order, unused sides 3..7 placed on
   tick 1); sort for won / lost / abort; the SCORE rows' text and
   positions for a fixed stats array.
10. Settled from the code: the row order after a loss (§1.4), the places
    after loading a P-save (§2), the map-toggle box phase (§4.2: drawn
    only when `(tick & 0x10) == 0`). Compared with a capture
    of the original's skirmish SCORE (2026-10-16): title, underline,
    headers and grid match; the row texts were 5 px too far right (the
    margin applied twice, §1.5) and now start at the original's x
    (name 118, numbers 240), golden test `tests/ui/test_score_mp.cpp`.
