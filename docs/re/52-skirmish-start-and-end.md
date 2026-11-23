# 52. Skirmish: how a game starts and ends

Source: DUNE2000.DAT 1.06 US. The decompiles come from the GhidraMCP server
(`re/ghidra_http.py`), the loops from `re/disasm.py`, and the data from
`Data\maps\*.MAP`, `Data\maps\_PRAC.MIS`, `Data\bin\TECHPOS.BIN`,
`Data\bin\VARS.BIN`, `Data\bin\TILEDATA.BIN` and `UI_DATA\Skirmish.uil`.
Status: verified; every open point was closed against the exe on 2026-10-16.

"Skirmish" means `gGameType` (`0x797e34`) = 1 with the network flag
`0x7984c4` = 0. Most code tests "`0x7984c4` set **or** `gGameType` = 1", so
skirmish shares the multiplayer paths. Network-only branches are listed
but not described.

Main functions:

| Address | Role |
|---|---|
| `FUN_004752c0` | `StartGame`: builds the map and mission paths, opens LOADING |
| `FUN_00448550` | game loop: set-up (colours, houses, crates), tick block, end of game |
| `FUN_004470a0` | pre-game "model latency" set-up (srand in the campaign only) |
| `FUN_0044f8e0` | mission loader: `_PRAC.MIS`, then the skirmish overrides |
| `FUN_0044c6d0` | map loader (doc 06) |
| `FUN_00469790` | map objects, start points, AI houses, MCV and starting units, worms |
| `FUN_00461c00` | side colours `0x5175d8[8]` |
| `FUN_0041b250` | ActivateAI (doc 22 §2.1), skirmish differences in §4 |
| `FUN_00453b90` | per tick: the default win / lose check (doc 19 §4.3) |
| `FUN_00455290` | "has buildings" / "has units" flags and the skirmish mask |
| `FUN_0044c520` | finishing places `0x797b70[8]` |
| `FUN_0046cf10` | initial camera |
| `FUN_0043dcb0` | the SCORE screen for skirmish / multiplayer |

## Corrections to earlier docs

- **doc 19 §4.3:** `0x548048` is the **maximum number of crates**
  (doc 31 §8). The "stuck" tie-break runs only when crates are off.
- **doc 19 §2.2:** the mask is only zeroed by a counted unit. A side with
  buildings and no ground unit keeps mask 3. The rule still holds because
  the default check treats "has buildings" like mask 0.
- **doc 31 §8:** `0x6b9834` is `gNetCrates` (0 / 1). `0x6b8859` is VARS.BIN
  +0x41 = **1**, and `0x6b885a` is VARS.BIN +0x42 = **0**. So a skirmish
  with crates on allows **at most 1 crate**.
- **doc 17 §6.2:** the worm limit counts across all players' passes (§2.6).
  Map worms belong to side 7. 6PLAY5 has no worm.
- **doc 16 §6.3:** the whole-map reveal at the end (`FUN_0044c480`) runs
  only in network games. It does not run in skirmish.
- **doc 02 settings table:** `0x4e3b00` is `gNetUnitCount` (0..10,
  default 6 before the cfg load). `0x4e3b0c` is `gNetAIPlayers` (cfg clamp
  0..5). Full list in §1.1.
- **doc 22 §2.1:** in skirmish the first GroupNewUnits call (step 8,
  forced) is skipped (§4).

## 1. Settings and the start command

### 1.1 The setup screen (`Skirmish.uil`)

The UI globals (table dump by `re/dump_ui_tables.py`). Defaults come from
`FUN_00470710` (start-up), then `dune2000.cfg` (`FUN_00478c20`) overrides
them and clamps them:

| Global | Address | Start-up default | cfg clamp (out of range →) | Control |
|---|---|---|---|---|
| gNetUnitCount | `0x4e3b00` (u8) | 6 | > 10 → 0 | slider `unitCountBar` |
| gNetTechLevel | `0x4e3b04` (u8) | 7 | 0 or > 7 → 1 | slider `techLevelBar` |
| gNetStartingCredits | `0x4e3b08` (u16) | 4000 | < 2500 or > 7000 → 2500 | slider `creditBar` |
| gNetAIPlayers | `0x4e3b0c` (u8) | 0 | > 5 → 1 | slider `aiBar` |
| gNetCrates | `0x6b9834` (u8) | 0 | not 0 / 1 → 0 | toggle (CratesOff / CratesOn) |
| gNetWorms | `0x6b9838` (u8) | 0 | > 3 → 0 | 0 off, 1 low, 2 medium, 3 high |
| gNetPlayerSide | `0x6b9840` (u8) | 0 | > 2 → 0 | Atreides / Harkonnen / Ordos checkboxes |
| gNetPlayerColour | `0x6b9844` (u8) | 0 | > 7 → 0 | colour buttons **0, 1, 2, 3, 5, 6** |
| gDifficultyLevel | `0x4e8bf0` (u16) | — | — | Easy / Normal / Hard (0 / 1 / 2) |
| gNetPlayerName | `0x6b93f8` | NONAME string | — | `nameEdit` |
| gNetMap | `0x797638` | — | — | `mapList` (GetMapList) |

- The screen also has a `Concrete` text and `SetValueBFTo gNetConcrete
  TOGGLE`. `gNetConcrete` is **not** in the exe's globals table. The
  control does nothing: its block is headerless, so the parser never
  creates it (doc 51 §2), and even run, `SetValueBFTo` (`FUN_0047df90`)
  only acts on a name found in the 23-entry BF table `0x4ca200`; an
  unknown name writes nothing.
- `gNetPlayerHandicap` (`0x6b9848`) is network only. In skirmish the cost
  and build-time factors use gDifficultyLevel for the local side and
  "normal" for the others (`FUN_004429b0`, `FUN_00442960`, doc 22 §4).
- The slider ranges come from `Skirmish.uil` (value-bar line 11, min max
  step default, doc 55 §2.1): units 0..10, tech 1..7, credits 2500..7000
  step 500, AI players **1..5**. `GetValueU1Init` / `U2Init` clamps the
  global itself when the page is created (doc 55 §2.3), so gNetAIPlayers
  is at least 1 whenever Start can be pressed.

### 1.2 StartGame (`FUN_004752c0`, verified)

- Map path `0x6f9840` = `"<gNetMap>.map"`, mission path `0x6dc470` =
  `"prac.mis"`. Both are opened in the skirmish directory `0x5136e8`
  (`Data\maps`). The binary loader adds `_`, so the file is
  **`Data\maps\_PRAC.MIS`** for every map.
- It opens LOADING, then the game loop runs (`gGameState` = 2).

### 1.3 No new seed (`FUN_004470a0`, verified)

- Campaign: `srand(FUN_004b1e60())` (the clock in ms), or seed 0 when the
  replay flag `0x6b98fc` is set.
- **Skirmish: no `srand`.** Only the CD check `FUN_00482650` runs. The
  RNG (`0x4ed870`, doc 06) continues from whatever state it had: the
  boot value, the last campaign mission, the menus, or the previous
  skirmish. A Restart (REPLAY_MISSION → RestartGame) runs the whole set-up
  again without a new seed, so the starts and colours are rolled again.
- Also here: `0x797e38` (next finishing place) = 0, and `FUN_004481b0`
  resets the finishing places `0x797b70[8]` = −1, the start "used" flags
  `0x782aa0[8]` = 0, and the lobby records `0x6b9208 + i·0x3c`.

## 2. Set-up, in order

All steps are in `FUN_00448550` unless named. The order matters for the
`rand()` sequence.

### 2.1 Before the map (game loop, verified)

1. `FUN_00466140`: 64 `rand() & 3` bytes and the crater jitter tables
   (doc 26 §4). The same in every game type. Not on a restart
   (`0x797e1c` set).
2. `FUN_004470a0` (§1.3).
3. The game surface is made (`FUN_00460c80`). No `rand()`.
4. **Houses** `0x8cd4f0[i]` = i for i = 0..7, then `0x8cd4f0[0]` =
   gNetPlayerSide. gSideId `0x798544` = **0**: the local player is always
   side 0.
5. **Colours**, `0x44a54e`. The lobby record byte `0x6b922d + i·0x3c`
   (i = 0..5) is the colour of side i. Side 0 gets gNetPlayerColour.
   For i = 1..5 (always all five, whatever the AI count):
   ```
   do c = rand() & 7 while c == 4 || c == 7          // rand line 0x15b1 / 0x15b4
   while (c equals the colour of a side j < i, or of side 0):
       do c = rand() & 7 while c == 4 || c == 7      // 0x15be / 0x15c1
   colour[i] = c
   ```
   So the colours are unique and come from {0, 1, 2, 3, 5, 6}, the same
   six the screen offers.
6. **Crates** (`0x44a814`): max crates `0x548048` = 0 when gNetCrates is
   0, else VARS `0x6b8859` (= 1), times the human count when VARS
   `0x6b885a` is set (it is 0). The campaign sets 0 (`0x448756`).
7. `FUN_00461c00`: side colour `0x5175d8[i]` = the lobby colour byte of
   side i, for i = 0..7. There are 8 lobby records: `FUN_004481b0`
   (called unconditionally by `FUN_004470a0`, step 2) zeroes all 8, and
   no skirmish step fills records 6 and 7 (network: only with more than
   six humans), so sides 6 and 7 get **colour 0** (verified 2026-10-16;
   we had 6 and 7). The
   campaign uses the house instead (copied by the mission loader).
8. Sides 0..7 are created (`FUN_004a45d0`) with house `0x8cd4f0[i]`. All
   8 sides are active (`player+0x24250` = 1) in every game type.
9. `FUN_0041af50` per AI slot, then the mission and the map load (§2.2,
   §2.3).

### 2.2 The mission file (`FUN_0044f8e0`, verified)

`_PRAC.MIS` is read like a campaign mission (docs/formats/maps.md), then:

- The 8 house bytes of step 4 are **kept** (saved before the read,
  restored after).
- **Tech** `0x6b87c8[i]` = gNetTechLevel for all 8 sides.
- **Credits** `0x6b87d0[i]`: side 0 (gSideId) = gNetStartingCredits;
  every other side = **7000 / 10500 / 14000** for gDifficultyLevel 0 / 1 /
  2 (any other value: 7000). The AI ignores the credits slider.
- **Tileset override** by map name (`__strcmpi` with gNetMap):

  | Maps | Tileset, attributes |
  |---|---|
  | 2Play11 | `Bloxtree`, `tileatr1` |
  | 2Play12 | `Bloxbase`, `tileatr2` |
  | 6Play3 | `Bloxwast`, `tileatr4` |
  | 2Play13, 4Play9, 4Play10, 6Play4, 6Play5, 2Play14, 4Play8 | `Bloxxmas`, `tileatr7` |
  | every other map | from `_PRAC.MIS`: `Bloxbgbs`, `tileatr3` |

- An `[EVENTS]` section is an error in skirmish (text loader only).

`_PRAC.MIS` content (1.06):

| Block | Value |
|---|---|
| tech, credits | all 0 (replaced above) |
| houses | 0 1 2 3 4 5 6 7 (replaced by step 4) |
| AI block 0 | side 0, active 1, **EnablePractice (+0x03) = 1** |
| AI blocks 1..7 | side i, active 0 (replaced in §2.4) |
| alliances | every side hostile (1) to every other, friendly to itself |
| events, conditions | none; timer −1 |

**Teams:** there are none. All sides are enemies. No code writes the
alliance table in skirmish. There is no in-game ally command in
skirmish: the A key's alliance call `FUN_00459b50` has one caller
(`0x445ec7`), gated by the network flag `0x7984c4` (`0x445eb8`), which is 0
in skirmish (doc 57 §3.6).

### 2.3 The map (`FUN_0044c6d0`, `FUN_00469790` scan, verified)

The map scan (row by row, x inner) handles each cell's special value:

- 1 / 2: thin / thick spice. 0x29–0x2D: a spice bloom (`rand()` and
  `FUN_0044e8e0`, doc 31). These run in every game type, so **the bloom
  rand() calls come before the AI houses and the starts**.
- **0x17: a start point.** Its cell goes to `0x6d7ba8 + 8k` (x) /
  `0x6d7bac + 8k` (y), and `0x782aa0[k]` = 0 (unused). More than 8 is
  an error. In skirmish and network **exactly 8 are required**
  (`FUN_0042b590` otherwise). Every 1.06 skirmish map has 8, even the
  2-player maps.
- Other values index TILEDATA.BIN (`0x6d7de8`). Unit or building entries
  are collected in a list. **In skirmish only the sandworms of that list
  are created** (§2.6). Map buildings and other map units are ignored.
  In 1.06 the only listed object on skirmish maps is special 0x14 =
  SANDWORM (name 22), owner **side 7** (TILEDATA `16 00 07 00`); 2PLAY13's
  0x1CC and 0x18 are −1 (nothing).

### 2.4 Players and AI slots (`FUN_00469790` `0x469d6e`, verified)

1. `0x7984c0` (humans) = 1.
2. The "AI active" byte (+1) of all 8 AI blocks is cleared.
3. AI blocks 1..7 = a copy of block 0 (from `_PRAC.MIS`), with the side
   byte (+0) = i. So every AI uses the `_PRAC.MIS` AI values, including
   **EnablePractice** (doc 22 §3.8).
4. For each AI side s = 1 .. gNetAIPlayers (in order):
   ```
   house[s] = rand() % 3          // rand line 0x1608: 0 Atreides, 1 Harkonnen, 2 Ordos
   player[s]+0x24253 = house[s]
   AI block s: side = s, active = 1
   ```
   AI houses are random and may repeat the player's house. The other
   sides (n+1..7) keep houses n+1..7 and stay without AI.
5. Players = humans + AI = 1 + gNetAIPlayers, max 8 (error above 8).

### 2.5 Starts, MCV and starting units (`0x469ebf..0x46a26b`, verified)

The MCV record is found first: the last unit record with kind 6 (record
12, name 10). Then for each side p = 0 .. players − 1 (in order):

1. **Start point:**
   - p = 0 (the local player): `rand() % 8` (rand line 0x1628; the count
     of start points, always 8).
   - p > 0: farthest point. For each unused start s, d(s) = the minimum
     over used starts u of `ftol(sqrt(dx² + dy²))` (cells, integer
     squares, x87 `fsqrt`, truncation). Pick the s with the largest d(s),
     **strictly larger** wins, so the lowest index wins a tie. No rand().
   - Mark the start used (`0x782aa0[s]` = 1).
2. **MCV:** (x, y) = the start cell. `FUN_004a60e0(&x, &y, 6)` (free cell
   spiral, doc 14 §2.4). Create the side's MCV version
   (`FUN_0046c570(10, 0)`) with `FUN_00455870(p, rec, x, y, x, y, 0, 0)`.
3. **Starting units:** for c = 0 .. gNetUnitCount − 1, entry
   e = TECHPOS[gNetTechLevel][c] (5 bytes; table `0x6b8868`, loaded by
   `FUN_00454e00` from `bin\TECHPOS.BIN`, 10 tech rows × 10 entries):
   - skip when e[0] = 0xFF;
   - (x, y) = start + (e[3], e[4]) (signed), clamped to the map;
   - the unit name is e[0] / e[1] / e[2] for house 0 / 1 / 2 (another
     house is a fatal error), mapped to the side's version
     (`FUN_0046c570(name, 0)`);
   - `FUN_004a60e0(&x, &y, 6)`, then `FUN_00455870(p, rec, x, y, x, y,
     0, 0)`.
4. **Worms** (§2.6), then the next side.

There is **no construction yard and no other building** at the start. The
cash is the credits of §2.2. Each side's MCV deploys by the player's
order or, for the AI, by DoMCVs (doc 22 §3.8).

TECHPOS.BIN (1.06), name A / H / O (dx, dy). Tech 0, 8 and 9 are empty.

| c | Tech 1 | Tech 2 | Tech 3 | Tech 4 | Tech 5 | Tech 6 and 7 |
|---:|---|---|---|---|---|---|
| 0 (−1,−2) | LI | LI | LI | LI | LI | LI |
| 1 (1,2) | LI | LI | LI | LI / LI / TROOPER | MISSILE / MISSILE / COMBAT | MISSILE / MISSILE / COMBAT |
| 2 (1,−2) | TRIKE / TRIKE / RAIDER | same | same | same | same | TRIKE / TRIKE / STEALTH_RAIDER |
| 3 (−1,2) | TRIKE / TRIKE / RAIDER | QUAD | QUAD | QUAD | QUAD | QUAD |
| 4 (−2,−1) | LI | LI | COMBAT | COMBAT | COMBAT | COMBAT |
| 5 (2,1) | LI | TROOPER | TROOPER | TROOPER | TROOPER | QUAD |
| 6 (−2,1) | TRIKE / TRIKE / RAIDER | same | same | same | SIEGE | SIEGE |
| 7 (2,−1) | TRIKE / TRIKE / RAIDER | QUAD | QUAD | MISSILE / MISSILE / COMBAT | MISSILE / MISSILE / COMBAT | MISSILE / MISSILE / COMBAT |
| 8 (0,−2) | LI | LI | COMBAT | COMBAT | COMBAT | COMBAT |
| 9 (0,2) | LI | TROOPER | TROOPER | TROOPER | TROOPER / TROOPER / SIEGE | GRENADIER / SARDAUKAR / SIEGE |

LI = LIGHT_INF, COMBAT = COMBAT_TANK (the house's version), MISSILE =
MISSILE_TANK, SIEGE = SIEGE_TANK. The dx, dy of each column is the same in
every tech row.

Floats: the sqrt is of an integer below 2·128², so float32 and double give
the same truncated value. Our port may use `std::sqrt(float)`.

### 2.6 Worms (verified)

Inside each side's pass, after its units, the collected map objects are
walked again. Only kind-5 units (worms) are considered. A counter W
(shared by all passes, starting at 0) goes up by 1 for each worm entry.
The worm is created (owner from TILEDATA, side 7) while **W ≤ gNetWorms**.

So the first gNetWorms worm entries of the repeated walk are created. With
3 map worms and gNetWorms = 3, all three appear in side 0's pass. With 1
map worm and gNetWorms = 3 (and ≥ 3 players), that one spot gets 3 worms.
gNetWorms = 0 creates none, and then the THUMPER is not buildable
(doc 11, §3).

### 2.7 After the map (game loop, verified)

1. `FUN_0041b250` ActivateAI for each slot with AI active: sides 1..n
   (side 0 has none). Skirmish differences in §4.
2. `FUN_0049f4d0(gSideId)`: the start reveal around the local side's
   units, as in the campaign. **The whole map starts shrouded**
   (doc 16 §2). There is no other reveal in skirmish.
3. Music: `arakatak.aud` (doc 15).
4. `FUN_0044f770`: all 16 defeat checks (§5.1).
5. **Initial camera** (called from the set-up before the AI activation,
   `FUN_0046cf10`):
   - If the side has a building, centre on the primary one
     (`+0x26098` ≠ 0xFFFF), else on the building with the smallest
     group index (record +0x88 = the name group `0x6dc5c8[rec]`; x −
     480/2, y − offset_y/2 − 380/2). HomeView in full: doc 25 A.8.
   - Else centre on a unit: the first unit, replaced by every later MCV
     in the unit list (so the last MCV wins). At a skirmish start this is
     the player's MCV.
   - Clamp to 0 .. map·32 − view − 1.

## 3. Tech level

One tech level for all sides: gNetTechLevel (1..7) in `0x6b87c8[i]`.
It gates, exactly as in the campaign:

- **Buildings** (`FUN_0046be50`, doc 11): record tech (+0x10) ≤ tech.
- **Units** (`FUN_0046be50`): record tech (+0x24) ≤ tech, and the
  **skirmish-only flag** (unit record +0xAA ≠ 0: THUMPER, GRENADIER,
  STEALTH_RAIDER, the Harkonnen SARDAUKAR record 29) is allowed only when
  `0x7984c4` is set or gGameType = 1. A **THUMPER** (kind 14) also needs
  gNetWorms ≠ 0 (`0x46c22d`).
- **Upgrades** (`FUN_004431d0`, doc 28): tech[level + 1] of the building
  record ≤ tech.
- **Palace specials** (doc 39): they are units of the palace, so the
  record tech applies (saboteur 5, Fremen 6, Death Hand 7; the Atreides
  airstrike needs the AT_HIGH_TECH_FACTORY upgrade at tech 6).
- **Starport**: the building needs its tech (5). The Starport list itself
  has no tech check (doc 34 §1).
- The AI uses the same checks for what it can build.
- The tech level also picks the TECHPOS row of starting units (§2.5).

## 4. The AI in skirmish (verified)

- ActivateAI (`FUN_0041b250`): no "AI not initialised" check (tech or
  credits 0) in skirmish. The forced GroupNewUnits call at activation
  (`FUN_0041c3e0(group, 1)`, `0x41b2f6`) runs only when gGameType ≠ 1 and
  not network, so **skirmish skips it**. The three activation `rand()`
  calls (doc 22 §2.1) run per AI side, in side order.
- Every AI has EnablePractice (from `_PRAC.MIS` block 0):
  - GetInitialBuildingPositions is skipped (doc 22 §3.4);
  - the practice building choice `FUN_00420e90` (doc 22 §3.4);
  - auto-BESERK when beaten (`FUN_00423850`, doc 22 §3.8).
  The practice base-building plan is in doc 22 §3.4 and doc 53
  (FindRandomBuildingPosition `FUN_00420e90`, ported as
  `World::ai_random_position`).
- Hard (gDifficultyLevel 2): UnitBuildRate × 0.75 for every AI (doc 22
  §2.1). The AI credits depend on the difficulty (§2.2).

## 5. The end of the game

### 5.1 What counts as alive (`FUN_00455290`, verified)

Refreshed per side every 16 ticks (doc 19 §2.1): B ("has buildings",
`0x6b87c0`, any building except walls) on even ticks, U ("has units",
`0x6b8268`) on odd ticks. U counts every unit that is **not an aircraft**
(flag 0x40) and **not a worm** (kind 5). Dying units still count.

**The skirmish mask** `0x6b8258[side]` (written by the U pass):

```
M = 3
for each counted unit:
    if B (from the last building pass): M = 0
    elif the unit is a vehicle (record +0x11 == 0):
        M &= ~2                                  // has a vehicle
        if armed (record +0x0C != 0xFF) or record == MCV (0x6d60e7): M = 0
    else (infantry):
        M &= ~1                                  // has infantry
        if armed: M = 0
```

| M | Meaning (side with units, no buildings) |
|---:|---|
| 0 | an armed unit or an MCV (or buildings) |
| 1 | only unarmed vehicles (harvesters; aircraft are not counted) |
| 2 | only unarmed infantry (engineers, thumpers, saboteurs) |
| 3 | no counted unit |

The MCV test compares the record with the one MCV record (12, byte
`0x6d60e7` of the Templates.bin block `0x6d60d8`).

### 5.2 The default check (`FUN_00453b90` else branch, verified)

Each tick, in this order (doc 19 §4.3, restated with the meanings):

1. `0x6b7050` = 0. For each side i: alive(i) = U(i) or B(i).
   - If M(i) = 0 or B(i): `fight` = true.
   - Else if U(i): `acc &= ~M(i)` (acc starts at 0xFF).
   - If alive(i): if any alive j has alliance[i][j] ≠ 0 or
     alliance[j][i] ≠ 0, `enemies` = true. If side i has no active AI,
     `0x6b7050` = 1 (a human is still alive).
2. `enemies` false → **game over** (`0x6b63c8` = 1). In skirmish
   (all hostile) that means at most one side is alive.
3. The local side (0) not alive → **lost** (unless already won). Else if
   game over and not lost → **won**.
4. **Stalemate tie-break**, only when no side can fight (`fight` false),
   **crates are off** (`0x548048` = 0) and the game is not over:
   - acc = 0xFD (every remaining side has only unarmed infantry) or
     acc = 0xFE (only unarmed vehicles): game over. The local side
     **wins if it is the side checked this tick** (`(tick & 0xF) >> 1`),
     else it **loses**. Inferred: an arbitrary tie-break; with only side 0
     and side n left it depends on the tick.
   - Mixed (some sides with only infantry, some with only vehicles):
     acc = 0xFC, nothing happens. The game runs on until something
     changes.
5. `FUN_00455480` (side `tick & 7`): a side without buildings whose
   counted units are all cloaked gets its first one uncloaked.

Sides that never played (n+1..7) are dead from the first check. Worms of
side 7 and aircraft keep nobody alive.

**With 0 AI players** only side 0 is alive, so the first check says game
over and the player wins. Unreachable from the page: the AI slider's
minimum is 1 and its init clamps the global (§1.1).

### 5.3 What happens to a defeated side (verified)

Nothing is removed. A side is "dead" only when it has no building except
walls and no ground unit, so all that can remain are its **walls** and
**aircraft** (carryalls, ornithopters, a frigate). They stay on the map.
Its AI keeps running (AI active is not cleared). `FUN_0044c520` gives it
the next finishing place (`0x797b70[side]` = `0x797e38`++), once, used by
the score screen and the map-toggle score box (doc 11 §4). Unused sides
get their places on the first tick, in side order. There is no message
or voice for an eliminated AI in skirmish (`LostOnePlayer` is network
only).

### 5.4 The end sequence (`FUN_00448550` `0x449b2c..`, verified)

The delay counter `0x516540` (2 s of ticks) and the banners are as in
the campaign (doc 19 §4.4). Skirmish differences:

**Won:**
1. Once: `S_MISSIONWON` in the local house's voice (`FUN_004762f0`),
   close any in-game menu, busy-wait for the voice.
2. The "MISSION ACCOMPLISHED" banner while the counter runs down (the
   game keeps running).
3. At 0: QuitAndJumpToMenu **SCORE** (no mission increment);
   gGameState = 1. The menus become MAINMENU, SCORE (doc 19 §4.5).

**Lost:**
1. Once: `S_MISSIONFAILED` voice, busy-wait.
2. Each tick the counter goes down by 1, **and is set to 0 when the game
   is over or no human is alive** (`0x6b7050` = 0). In skirmish the only
   human is side 0, so after a loss the counter is 0 on the next tick:
   **no 2-second banner**. (In the campaign `0x6b7050` stays 1 and the
   game is never "over", so the full delay runs there.)
3. At 0: open **REPLAY_MISSION** over the game (not if already on top),
   with `FUN_00470e60` (`0x448xxx`, the `REPLAY_MISSION` compare). That
   call sets the menu manager's pause flag (`FUN_00470f90(1)`), so in
   skirmish (`0x7984c4` = 0) **the game pauses behind it**, as under any
   single-player menu (doc 20 §1.3). (An earlier version of this note
   said the game keeps running; corrected 2026-10-14.)
   - Yes → `RestartGame`: the whole set-up again (§1.3, new starts and
     colours, no new seed).
   - No → `SetValueECTo gGameState GameStateBriefing`, `QuitAndJumpToMenu
     LOADMISSION`. The front end's jump (`FUN_004738b0`, `0x473fb5`) opens
     MAINMENU, then for gGameType 1 **SCORE** (`0x47402d`–`0x474037`). So
     a loss also ends on the score screen. (Corrected 2026-10-14; this
     note and doc 19 said "MAINMENU only".)

**Abort** (INGAME_SKIRMISH → CONFIRMABORT → Abort): MAINMENU,
MULTIPLAYERTYPE, SCORE (doc 19 §5).

### 5.5 The SCORE screen (`score.uil`, `FUN_0043dcb0`, brief)

Title `TitlePracticeScore` (skirmish) / `TitleMultiScore`. One row per
side with a network human (lobby record +3, 0 in skirmish), an active
AI, or the local side in skirmish (`0x43dcf9..0x43dd28`, doc 56 §1.2),
ordered by finishing place. Each row:
colour (`NetColour%d` of the lobby colour), house icon (`mp_at`,
`mp_hk`, `mp_or`), name (`UI_Computer` for an AI), casualties
(units destroyed / received), buildings (received / destroyed), spice
harvested. The full page: doc 56.

## 6. Every game-type branch (gGameType `0x797e34` xrefs)

Sim and in-game branches. "MP" = `0x7984c4` or gGameType ≥ 2.

| Where | Campaign (0) | Skirmish (1) |
|---|---|---|
| `FUN_004470a0` | srand(clock) | no srand, CD check (§1.3) |
| `FUN_00448550` set-up | houses from the .MIS; colour = house; crates 0 | §2.1 (houses, colours, crates) |
| `FUN_0044f8e0` | tech / credits / houses from the file | §2.2 overrides, tileset override, events forbidden |
| `FUN_0044c6d0`, `FUN_0044f790`, `FUN_00491b40` | `MISSIONS` directory | `Data\maps` (`0x5136e8`) |
| `FUN_00469790` | all map objects; a map without worms is fatal | 8 starts required, MCV + TECHPOS units, AI houses, worms only (§2.3–2.6) |
| `FUN_0041b250` ActivateAI | tech / credits check; forced GroupNewUnits | neither (§4) |
| `FUN_00453b90` per tick | the script (doc 19 §2) | the default check (§5.2) |
| `FUN_0046be50` build lists | skirmish-only records hidden | allowed; THUMPER needs worms (§3) |
| `FUN_00448550` end | voice + 2 s; next mission / MVE_LOSE | voice + 2 s → SCORE; lose → REPLAY_MISSION at once (§5.4) |
| `FUN_00448550` music | the mission track | `arakatak.aud` (doc 15) |
| `FUN_0046ff30` music stream | sets `0x795600` at the track end | not set (doc 15) |
| `FUN_004433a0` / `FUN_0044b5c0` | sidebar control 6 ignored (when `0x4eb008`) | control 6 = the score box toggle (doc 11 §4) |
| `FUN_004433a0`, `0x449780` | OPTIONS → INGAME | INGAME_SKIRMISH (doc 20) |
| `FUN_00492750`, save / load | `S<n>.SAV` | `P<n>.SAV` (doc 27) |
| `FUN_004359a0`, `FUN_0043ee80`, `FUN_0043dc60` | campaign score (doc 24) | SCORE (`FUN_0043d7e0` / `FUN_0043dcb0`) |
| `FUN_004412c0` | — | the Westwood Online ladder scores, called only from the Internet result upload (`0x40d994`); never in skirmish (doc 56 §3) |

Network / Internet only (game type 2..5), not used by skirmish:
`FUN_0045a7a0` (timing presets; skirmish takes the default branch:
`0x4e3af4` = 2.0f), `FUN_00418480`, `FUN_0045a4d0`, `FUN_0045ad80`,
`FUN_0045b510`, `FUN_0045b0a0`, `FUN_0045c740`, `FUN_0045cda0`,
`FUN_00457810` cases 0x1e / 0x1f, `FUN_0046e9c0`, `FUN_00437040`,
`FUN_0043b080`, `FUN_00492b60` / `e20` / `eb0`, `FUN_004900d0` /
`FUN_004901c0` (UI setters), `FUN_0048e820` (lobby), the network pulse in
the loop. The order latency (doc 49) is 2 in both campaign and skirmish.
The difficulty factors (`FUN_00442960`, `FUN_004429b0`) do not test the
game type.

## 7. Implementation checklist

| # | Item | Our file | Status |
|---:|---|---|---|
| 1 | Skirmish set-up entry: map from `Data/maps/<name>.MAP`, mission `Data/maps/_PRAC.MIS`; gSideId 0 | `src/app/game_screen.cpp` (~l. 153, the `MISSIONS/` paths) | done (2026-10-14..16; see parity.md) |
| 2 | No reseed in skirmish (keep the RNG state; a restart does not reseed) | `src/app/game_screen.cpp`, the RNG owner | done (2026-10-14..16; see parity.md) |
| 3 | Colour roll for sides 1..5 (§2.1 step 5), before the map load | `src/app/game_screen.cpp` / `src/sim/economy.cpp` `init_players` | done (2026-10-14..16; see parity.md) |
| 4 | Mission overrides: tech = gNetTechLevel for all; credits local / 7000·10500·14000; houses kept; tileset override table | `src/app/game_screen.cpp`, `src/sim/economy.cpp:34` | done (2026-10-14..16; see parity.md) |
| 5 | Map scan: start points 0x17 (exactly 8), blooms keep their rand order; in skirmish create only worms from the object list | `src/sim/objects.cpp` `place_map_objects` | done (2026-10-14..16; see parity.md) |
| 6 | AI houses `rand() % 3` per AI side, AI blocks 1..n copied from block 0, active | `src/sim/ai.cpp` `set_ai` | done (2026-10-14..16; see parity.md) |
| 7 | Starts (rand % 8, then farthest-first, strict >), MCV, TECHPOS units with spiral r = 6, worm counter W ≤ gNetWorms, in per-side order | new `src/sim/skirmish.cpp` (or `placement.cpp`) | done (2026-10-14..16; see parity.md) |
| 8 | Max crates (`0x548048`): 0 or 1 | `src/sim/blooms.cpp:155` | done (2026-10-14..16; see parity.md) |
| 9 | ActivateAI: skip the tech/credit check and the forced GroupNewUnits in skirmish | `src/sim/ai.cpp` | done (2026-10-14..16; see parity.md) |
| 10 | Skirmish-only records and the THUMPER worms gate | `src/sim/economy.cpp:138` | done (2026-10-14..16; see parity.md) |
| 11 | The mask M in `defeat_check` mode 1 | `src/sim/mission.cpp:31` | done (2026-10-14..16; see parity.md) |
| 12 | The default check (§5.2) incl. `0x6b7050` and the tie-break | `src/sim/mission.cpp:117` | done (2026-10-14..16; see parity.md) |
| 13 | Finishing places `0x797b70` (each tick, `FUN_0044c520`) | `src/sim/mission.cpp` | done (2026-10-14..16; see parity.md) |
| 14 | Lose path: counter → 0 when game over or no human; REPLAY_MISSION over the running game | `src/app/game_screen.cpp`, `src/app/menu_runner.cpp:883` | done (2026-10-14..16; see parity.md) |
| 15 | Initial camera (§2.7 step 5) | `src/app/game_screen.cpp:220` | done (2026-10-14..16; see parity.md) |
| 16 | `arakatak.aud`, INGAME_SKIRMISH, P-saves, control 6 | `game_screen.cpp`, `menu_runner.cpp`, `save_games.cpp`, `sidebar.cpp:275` | done (2026-10-14..16; see parity.md) |
| 17 | Save the new state (colours, finishing places, worm setting, max crates) | `src/sim/save_state.cpp` | done (2026-10-14..16; see parity.md) |
| 18 | Test: a fixed RNG state + 2PLAY1 gives the starts, houses, colours and units | `tests/sim/test_world.cpp` (golden "skirmish 2PLAY1 start") | done; the rand order is the exe's (rand lines above). A capture from the original is optional (doc 51 §4.2) |
