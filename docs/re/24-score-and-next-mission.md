# 24: After a campaign mission: score, high scores, next mission (1.06)

What the player sees between winning (or losing) a campaign mission and the
next mission starting. The end of the mission itself (voice, banner, delay,
which menu is jumped to) is in doc 19 §4.4–4.5; the menu stack and
transitions in doc 04; the campaign map in doc 05.

"Verified" = read from the code. Every point was checked against the exe
(2026-10-16); no guesses remain. The numbers are ported in
`src/campaign/score.cpp` and tested in `tests/campaign/test_front_end.cpp`.

## 1. The menu path (verified, doc 19 §4.5 summarised)

**Won, missions 1–8:** gMissionNumber (`0x4de610`) += 1, then
QuitAndJumpToMenu `SCORE` → close all, open MAINMENU, GLOBE (or
LOADMISSION when `gUseCampaignSelection` `0x4e7ad0` = 0), then
**SCORE_SINGLE** on top. SCORE_SINGLE starts `score.aud` (doc 04).
Its Continue button is `BackMenu`, which reveals GLOBE for the **next**
mission (already incremented). GLOBE's territory reveal runs only while
it is the top menu (doc 05), so it starts when the score closes.

**Won, mission 9:** mission = 10, QuitAndJumpToMenu `MVE_<HOUSE>_END`
(the end movie script, e.g. `AMisEnd.uil`), whose movie action is
QuitAndJumpToMenu SCORE → MAINMENU, **CREDITS**, SCORE_SINGLE.

**Lost (campaign):** QuitAndJumpToMenu `MVE_LOSE` (`lost.uil`: movie
`mve_lost`, then QuitAndJumpToMenu `LOADMISSION`) → MAINMENU, GLOBE for
the **same** mission. No score screen.

**From GLOBE to the mission** (doc 05, our `MenuRunner::launch_mission`):
a click in a territory → `FUN_004752c0`: gMissionNumber / gSideId from the
mission name, JumpToMenu `MVE_<HOUSE><n>` (`AMis2.uil` …: movie
`mve_briefing_atreides2`, then `BackMenuAndJumpTo PRECAP`) → PRECAP =
the briefing book `L<A/H/O><n>MIS.uil` (doc 03 / §4 below): pages with
forward / back buttons, **done** → `SetValueECTo gGameState
GameStateMainLoop`, `QuitMenu` → the game loop starts the mission.

`menus.uib`: SCORE_SINGLE = `score_1.uid`, transitions **enter 0 (FADE),
leave 1 (FADE)**; SCORE = `score.uid` (skirmish / multiplayer, §3);
MVE_LOSE = `lost.uid` (3, 3); GLOBE = `Globe.uid` (3, 1).

## 2. SCORE_SINGLE (`score_1.uil`, verified)

### 2.1 Elements

| Element | Pos | Size | Content |
|---|---|---|---|
| BACKGROUND `scoreui` | | | |
| GROUPBOX border1..6 | x 263, y 38 / 58 / 98 / 118 / 158 / 178 | 10×10 (image `scbox` sets the size) | frames of the six bars |
| TEXTBOX_L ourUnitCasualty | (375, 40) | 50×16 | Red, `NoText`, init `EnterText ZERO` |
| TEXTBOX_L theirUnitCasualty | (375, 60) | 50×16 | same |
| TEXTBOX_L ourBuildingCasualty | (375, 100) | 50×16 | same |
| TEXTBOX_L theirBuildingCasualty | (375, 120) | 50×16 | same |
| TEXTBOX_L ourSpiceHarvested | (375, 160) | 50×16 | same |
| TEXTBOX_L theirSpiceHarvested | (375, 180) | 50×16 | same |
| TEXTBOX_L ourScore | (525, 160) | 50×16 | JHighLightText, `EnterText ZERO` |
| TEXTBOX_L theirScore | (525, 180) | 50×16 | same |
| TEXTBOX_L_UL titleUnitScore / titleBuildingScore / titleSpiceScore | (180, 20 / 80 / 140) | 200×16 | `UnitScore`, `BuildingScore`, `SpiceScore` |
| TEXTBOX_L our…Score / their…Score labels | (180, 40/60, 100/120, 160/180) | 80×16 | JNormalText, `YourScore` / `EnemyScore` |
| TEXTBOX_L_UL scoreTitle | (435, 140) | 80×16 | Red, `Total` |
| TEXTBOX_L ourSc / theirSc | (435, 160 / 180) | 80×16 | Red, `Yours` / `Enemies` |
| ANIMATION dune | (499, 16) | 75×75 | `MoonSpin` at 10 fps, `GetValueECInit AnimLoopForwardCycle` |
| BUTTON continue | (533, 367) | 70×22 | `Continue2`, rubber font, `rBut4_up/dn`; `BackMenu`; `EnterActions` (Enter runs it) |

Despite their names, the six "Casualty / Harvested" boxes show **score
points**, not counts (§2.4).

### 2.2 Set-up (`FUN_004359a0` → `FUN_0043d820` "CreateScore()")

`FUN_004359a0` (menu create) marks menus SCORE and SCORE_SINGLE as score
menus (menu `+0x30` = 1) and, in the campaign (`gGameType` `0x797e34` =
0), calls `FUN_0043d820` (else `FUN_0043d7e0`, the multiplayer score). It:

1. remembers the menu (`0x51625c`);
2. `FUN_0043eee0`: loads the high-score table from **`GameSave\hs.dat`**
   (6 slots, §2.6; empty slots if the file is missing);
3. resets the state (`0x516264` our total, `0x51626c` "table shown",
   `0x516280` "pausing", `0x516270` "finished", `0x516274` "skip");
4. **`FUN_0043ec70`** computes the targets (§2.3);
5. loads the bar images by house:
   - **ours** by the local player's house (`player+0x24253`): Atreides
     `u1_b` / `b1_b` / `s1_b` (units, buildings, spice; blue), Harkonnen
     `…_r`, Ordos `…_g`, into `0x5160e0` / `0x5160e8` / `0x5160f0`;
   - **theirs** by the **enemy house of the mission**: the campaign table
     record's column 24 (`0x795704 + (mission + house·10)·0x104`, i.e.
     record `mission − 1` +0x100, our `CampaignRecord::track_house`;
     mission is already the next one): 0 → `u2_b`/`b2_b`/`s2_b`, 1 →
     `…_r`, 2 → `…_g`, into `0x5160e4` / `0x5160ec` / `0x5160f4`;
6. draws the house picture **`at_score` / `hk_score` / `od_score`**
   (`UI_gfx\`) at **(16, 12)** onto the menu's background surface.

### 2.3 The numbers (`FUN_0043ec70`, verified)

For every side 0..7 with `player+0x24250` set (in the mission), a record
at `0x797c00 + side·0x3C`:

| Rec | Player field | Meaning |
|---|---|---|
| +0x00 | +0x24250 | active (1) |
| +0x01 | +0x24253 | house |
| +0x02 | alliance `0x798830[local][side]` | |
| +0x04 | **+0x24a08** | units lost |
| +0x08 | **+0x24a0c** | buildings lost (destroyed, not sold: `FUN_0049d2e0` 0x49d…, see §5) |
| +0x0C | **+0x2477c** | spice harvested (+100 per unloaded load step, doc 13 §2) |
| +0x10 / +0x14 | +0x24c90 / +0x24c94 | units / buildings killed (copied, unused here) |

Sums over **all** active sides (the local player and allies included):
U = Σ units lost, B = Σ buildings lost, S = Σ spice harvested. With L =
the local player's record and the constants `0x4dedc4` = **150**,
`0x4dedc8` = **100**, `0x4dedcc` = **50**:

```
our unit target   Tu = U ? ((U − L.units_lost) · 150) / U : 0      (u32)
their unit target    = U ? 150 − Tu : 0
our building target  = B ? ((B − L.buildings_lost) · 100) / B : 0
their building target= B ? 100 − ours : 0
our spice target     = S ? (50 · L.spice) / S : 0
their spice target   = S ? 50 − ours : 0
```

So a mission's score is **at most 300 points before the multiplier**: the
player's share of everybody else's losses (units, buildings) and of all the
spice harvested. "Enemies" means every other active side together (allies
are counted on the enemy side).

The current counters (ours at rec +0x2C / +0x30 / +0x34, theirs at
`0x516244` / `0x516248` / `0x51624c`) start at 0.

**Multiplier:** `m` = f32 at `0x795634 + (mission + house·10)·0x104` =
the campaign record `mission − 1` field **+0x30** (our `CampaignRecord::
value`, 1.0 … 5.0; mission is already incremented, so it is the record of
the mission just won), read each call.

### 2.4 The count-up (`FUN_0043ee80` → `FUN_0043f030`, verified)

The menu draw `FUN_0043c4b0` calls `FUN_0043ee80` **once per UI update**
while the score menu is the top menu and no transition runs (so after the
FADE in). In the campaign it calls `FUN_0043f030` ("count one step"):

- If a pause is running (`0x516280`) and |`GetTickCount` − start| <
  **2000 ms**: return "busy" (nothing else, no totals). Otherwise the
  pause flag is cleared and the start time set to now.
- The phase is the first of **units, buildings, spice** whose two
  counters are not both at their targets (`==` tests, no phase variable).
  In that phase the call queues **`S_SCORETICK`** (`FUN_0046f820`, the
  voice queue: it plays again whenever the speech channel is free, doc
  15) and moves **each of the two counters one point** towards its target
  (ours and theirs together; a counter at its target stays). For each
  counter that moved:
  - its text box = `"%d"` of `ftol(counter × m)` (ours divided by `d`,
    unsigned), redrawn. `FILD counter; FMUL float m; __ftol`: at the
    exe's 53-bit x87 precision the product is exact, then truncated
    (`0x43f1fa`). 10 × 1.3f gives 12, where a float32 product would give
    13;
  - its bar is drawn: `FUN_00430770(screen, clip, 265, y, image, 1, 1)`.
    The rect is a **clip rect on the screen**, (265, y) to (265 + width,
    y + 16), not a source rect: the bar image (from its own (0, 0)) is
    blitted keyed with shadow at (265, y) and cut at that width. y = 40 /
    60 (units), 100 / 120 (buildings), 160 / 180 (spice); width `counter ×
    100 / max` (max 150 / 100 / 50), so a full bar is 100 px.
- When that step brought the units or the buildings phase to its targets:
  start a **2-second pause** (`0x516280` = 1 and the time). The spice phase
  has no pause after it, and a phase whose targets are both 0 is passed in
  the same call without a pause.
- When all six counters are at target: `0x516270` = 1 ("finished") and
  return "done" (`0x43f181`), before the totals. So "done" comes one call
  after the last step.
- Every other call recomputes the totals:
  `theirScore = Σ ftol(their_counter × m)`,
  `ourScore = (Σ (ftol(our_counter × m) / d)) / d` (the parts unsigned,
  the last division signed), and writes both boxes. **d** = 1, or 2 when
  the local player lost (`0x6b8278`). In the campaign d is always 1: the
  score menus open after a win (also after mission 9: end movie, then
  CREDITS and SCORE_SINGLE), WIN and LOSE exclude each other (doc 19 §3),
  and a loss goes to MVE_LOSE without a score.

So the units phase takes up to 150 updates (2.5 s at 60 updates/s), then
2 s pause, buildings up to 100 updates, 2 s pause, spice up to 50
updates.

When `FUN_0043f030` first reports "done", `FUN_0043ee80` builds the
**high-score table** once (`FUN_0043fc90`, §2.6; `0x51626c` = 1).

### 2.5 Keys and leaving

- **Esc** (`FUN_0043cb70`, the menu key handler, `0x43cbd0`) or **Tab**
  (`FUN_0043cdf0`, VK 9, `0x43cee3`) on a score menu (+0x30) while not
  finished: `0x516270` = 1 and `0x516274` = 1 ("skip"), and nothing else.
  The next update runs `FUN_0043fb60`: every counter = its target − 1 (if
  the target > 0; also in phases already counted), then `FUN_0043f030` is
  called **in a loop until it reports done**. Because the 2-second pauses
  are inside it, a skip **freezes the screen for about 4 s** (one pause
  per non-empty units / buildings phase) before the final numbers show.
  The user saw the freeze in the reference game (2026-10-13). The table is
  built on the following update.
- The two flags are globals, reset only by the campaign set-up
  `FUN_0043d820` (`0x43d856` / `0x43d861`). The skirmish SCORE is a score
  menu too (+0x30), so its **first** Esc / Tab only sets the flags (the
  multiplayer score never reads them) and the second one leaves, unless a
  campaign score earlier in the session already finished.
- Esc again (finished): the normal Esc rule (doc 20 §1.2) runs the first
  button with `BackMenu`: **Continue**.
- **Enter**: Continue has `EnterActions`, so Enter runs it (doc 20 §1.2).
- **Click Continue** at any time: BackMenu (closes the score with a FADE)
  → GLOBE (or CREDITS after mission 9).
- There is no timer: the screen stays until the player leaves it.

### 2.6 High-score table (`FUN_0043eee0` load, `FUN_0043fc90` show)

Six slots of 0xDC bytes at `0x515bb8`. `GameSave\hs.dat` holds only the
first **5** (0x44C bytes): `FUN_0043eee0` reads them (slot 6, and every
slot without the file: unused, empty name, house 3); `FUN_0043efd0` writes
them when the score menu is destroyed (`FUN_0043dc60`, campaign only). Our
copy lives in `<user data>/GameSave/hs.dat`, same layout (an original
`hs.dat` can be copied in).

| Slot off | Meaning |
|---|---|
| +0x00 | used |
| +0x04 | rank 0..11 |
| +0x08 | name (edit box text) |
| +0xD0 | house (0 Atreides, 1 Harkonnen, 2 Ordos, 3 other) |
| +0xD4 | mission (gMissionNumber − 1, at least 1) |
| +0xD8 | score (`0x516264`, our total) |

`FUN_0043fc90`:
1. adds a frame `hs_border` (90, 233) with image `hs_table`, a title
   textbox `high_scores` (90, 220) 410×15 "SS_HighScore", and column
   headers at y 235: `SS_Name` (110, w 100), `SS_Rating` (210, w 160),
   `SS_House` (370, w 80), `SS_Mission` (450, w 40), `SS_Score` (500, w 60);
   slot numbers `slot1..6` at (90, 250 + 15·i) — all White on Transparent;
2. inserts the new score: the first slot that is unused or has a lower
   score; the slots below move down (`FUN_004b1b20`, memmove); the new
   slot gets rank = `FUN_0043fc00(score)`, an empty name, the house, the
   mission and the score;
3. per used slot i, row y = 250 + 15·i: `name<i>` (110, w 100) — **an
   edit box for the new entry** (White, with commands 0xAE
   `SetFromUIStringSpecial gHighScore<i>` and 0xDD `BackMenu`, names from
   the command table dumped by `re/dump_ui_tables.py`), a text box
   otherwise; `rank<i>` (210, w 130) text `Rank1` …
   `Rank12`; `house<i>` (370, w 80) `Atreides` / `Harkonnen` / `Ordos`;
   `mission<i>` (450, w 40) `"%d"`; `score<i>` (500, w 60) `"%d"`.

**Rank** (`FUN_0043fc00`, score after the multiplier, unsigned): < 250
→ 0 (`Rank1`), ≥ 250 → 1 (corrected: `cmp eax, 0xfa; sbb; inc` at
`0x43fc86`), > 299 → 2, > 349 → 3, > 499 → 4, > 649 → 5,
> 799 → 6, > 949 → 7, > 1099 → 8, > 1199 → 9, > 1299 → 10, > 1349 →
11 (`Rank12`). The rank names are text.uib keys `Rank1` … `Rank12`.

**Insertion details (verified):** the first slot that is unused or has a
lower score (strict) takes the entry; slots below move down by a memmove
of `0x44C − i·0xDC` bytes, so the old slot 6 drops out. House = `gSideId`
(0, 1, 2, else 3); mission = gMissionNumber − 1, 0 → 1. If no slot
qualifies the table is shown without a new row. Header / slot boxes:
`slot1..6` (90, 250 + 15·i, w 20) show the literal digits "1".."6"; the
title is centred (align 2), the rest left-aligned. Rank and house texts are
text.uib keys (`Rank1..12`, `Atreides` / `Harkonnen` / `Ordos`; house 3 shows
the literal `Error`).

**The name edit box** (CEditBox, `FUN_0048b190`; fields +0x77C text,
+0xA9C cursor, +0xAAC focus = 1 here, +0xAB0 character filter = 0,
+0xAB2 max length = 0 = none):
- characters (WM_CHAR, `FUN_0043cb70` → `FUN_0048b4a0`; only 0x20..0x7F:
  the character is compared as a signed byte at `0x43ccf0`, doc 27): no
  space at cursor 0; insert at the cursor; if the text is then wider than
  the box − 10 px (font 0), the character is removed again (`FUN_0048b3c0`);
- keys (`FUN_0043cdf0`): Left / Right move the cursor, Backspace deletes
  before it, Delete at it; both then strip leading spaces and clamp the
  cursor;
- **Enter** runs the box's commands — 0xAE `SetFromUIStringSpecial
  gHighScore<i>` (the text into the slot's name) and 0xDD BackMenu — and
  then the EnterActions buttons (Continue). **Esc** runs only the first
  button with BackMenu (Continue): the typed name is **not** stored;
- drawing (`FUN_0048b560`): text at x + 5, centred vertically, colour
  +0xAA8; with focus a 2-px underline cursor (no blink) under the character
  at the cursor, as wide as that glyph + spacing (or the space width at the
  end or on a space), at y = centred top + font height − 3 and − 4.

**All houses share the table.** The insertion compares only used and
score (`0x440764` / `0x44076d`), and the row loop shows every used slot
(`0x440868..`); nothing tests the slot's house against gSideId. So an
Ordos entry is listed (with house text `Ordos`) after an Atreides
mission, and it competes for the same six slots.

**Written for a new entry** (`0x4407de..0x440859`): used (one byte), rank
(capped at 11), name = "" (the empty string `0x4f0f18`), house, the
**low byte** of the mission, score. The rest of the slot keeps the bytes
of the entry that was there (after the memmove), which are 0 in every
reachable case (missions are ≤ 9).

**The colour calls at the start of `FUN_0043f030` have no effect.**
`FUN_0048db90("Yellow")` and `FUN_0048db90(Blue / Red / Green / White)`
(chosen by `0x515c88[gSideId · 0x37]`, i.e. high-score slot gSideId's
house) are pure lookups: the function only compares the name with the
`colours.uib` table and returns its index. Both results are dropped.

## 3. SCORE (skirmish / multiplayer)

`score.uil` with `FUN_0043d7e0` / `FUN_0043dcb0`: doc 56. `FUN_004412c0`
is the Westwood Online ladder upload, never run in skirmish (doc 56 §3).

## 4. Our implementation

- Numbers: `src/campaign/score.cpp` (`score_targets` = `FUN_0043ec70`,
  `score_step` = `FUN_0043f030`, `score_skip_counters` = `FUN_0043fb60`,
  `score_rank` = `FUN_0043fc00`, `high_score_insert` = the insertion of
  `FUN_0043fc90`, `high_scores_from_file` / `_to_file` = `FUN_0043eee0` /
  `FUN_0043efd0`). Tests: `tests/campaign/test_front_end.cpp`.
- Menu side: `MenuRunner::score_setup` (`FUN_0043d820`), `score_update`
  (`FUN_0043ee80`, once per UI update while a score menu is on top),
  `score_step` (the boxes and `S_SCORETICK`), `score_draw_bars`, `hs_build`
  (the table, the name edit box with 0xAE / 0xDD), `hs_save` when the score
  menu leaves the stack (`FUN_0043dc60` from the menu destructor
  `FUN_00437040`, campaign only; it rewrites `hs.dat` even when no table was
  built). Our `hs.dat` lives in our own `GameSave` folder.
- Known simplification: `S_SCORETICK` is played when the speech channel
  is idle instead of going through the voice queue (`FUN_0046f820` in mode
  0 keeps one queued sound, so the exe can play one more tick after the
  count stops).
- The bars are drawn every frame on top of the menu, where the exe draws
  each bar once per step into the screen; nothing redraws the background
  under them while the score is shown, so the image is the same.

## 5. What the simulation must count (src/sim/player.hpp)

| Field | Exe | Incremented by |
|---|---|---|
| units lost | +0x24a08 | have (`units_lost`) |
| units killed | +0x24c90 | have (`units_killed`) |
| **buildings lost** | +0x24a0c | +1 when a building of the side is destroyed by damage (`FUN_0049d2e0`, just before `FUN_0049ca10`); not when sold |
| **buildings killed** | +0x24c94 | +1 for the attacker's side in the same place, when there is an attacker (not `0x6d610a`); also `+0x25418[house·8 + side]` per record |
| **spice harvested** | +0x2477c | +100 per unload step of a harvester (doc 13 §2, step 2) |
| active | +0x24250 | set for the sides in the mission (doc 22 / 23 "player active") |

## 6. Implementation checklist (done; kept for history)

1. `sim::Player`: `buildings_lost`, `buildings_killed`, `spice_harvested`;
   count them as §5 (`combat.cpp` building destruction, `harvest.cpp`
   unload).
2. Hand the end-of-mission stats from `GameScreen` to `MenuRunner`
   (per side: active, house, units lost, buildings lost, spice harvested)
   before `after_mission`.
3. Menu runner: a SCORE_SINGLE hook (like the GLOBE one): set-up §2.2,
   targets §2.3 (multiplier from the campaign table of the mission just
   won), count-up §2.4 per UI update (60 Hz) with the 2 s pauses and
   `S_SCORETICK`, skip on Esc / Tab §2.5.
4. TEXTBOX runtime text (and `EnterText` init) in `ui::render`.
5. `EnterActions`: Enter runs the button.
6. High-score table §2.6: dynamic elements, rank texts, `hs.dat` in our
   own settings folder (never the original's), the name edit box (needs
   EDITBOX); can follow after the score screen itself.
7. Check the whole path in a run: win A1V1 → SCORE_SINGLE → Continue →
   GLOBE reveal → pick A2V1/A2V2 → movie → briefing → the mission.
