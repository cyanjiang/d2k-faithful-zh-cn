# 28: Building upgrades (1.06)

How building upgrades work in 1.06: the upgrade list, the sidebar
upgrade page, the per-tick upgrade production, the two upgrade orders,
the computer player's UpgradeBuildings job, and everything that reads
the upgrade level.

Related: doc 11 (sidebar controls, pages, build lists, production, player
struct), doc 08 (Templates records), doc 22 §3.7 (AI jobs), doc 21 §2.3
(the upgrade marker on buildings).

"Verified" means read from the code or the data; no guesses remain
(closed 2026-10-16). The data tables below were read from the 1.06
`Data\bin\Templates.bin` with the layout of doc 08.

## 1. Functions and data

| Address | Label / role |
|---|---|
| `FUN_0046e7a0` | builds the Starport list and the **upgrade list** (`"ISideList"` errors) |
| `FUN_004431d0(player, name)` | **can this name be upgraded now** |
| `FUN_00446760(player)` | any entry of the upgrade list can be upgraded (control 9) |
| `FUN_004278a0(dst, tech, full)` | draws the upgrade page (page 3) |
| `FUN_004433a0` | sidebar input: page 3 clicks at `0x444287` (right press) and `0x444d9f` (left release) |
| `FUN_0044b5c0` | tooltips, page 3 case |
| `FUN_0045a350(player, name)` | posts order **0x2D** (start / resume upgrade) |
| `FUN_0045a3a0(player, record)` | posts order **0x2E** (hold / cancel upgrade) |
| `FUN_00457350` | 0x2D handler (`"ModelUpgradePick"`) |
| `FUN_00457490` | 0x2E handler |
| `FUN_004467e0` | per-tick production; the upgrade slot part is `0x446b3b..0x446cf3` |
| `FUN_00442be0(record, i, player)` | cost[i] of a building record × difficulty % / 100 |
| `FUN_00421c40` | AI `"UpgradeBuildings(AI)"` |
| `FUN_0046c450(name, 0)` | MyVersionOfBuilding: this player's record for a name (doc 08) |

**Player fields** (doc 11 §8): `+0x24544[100]` upgrade level per building
**name index**, `+0x245c1[100]` owns a building of that name,
`+0x262c8[8]` upgrade list (building **records**, −1 = empty), and the
upgrade slot:

| Off | Type | Field |
|---|---|---|
| +0x265f8 | i16 | item: building **record** (−1 = none) |
| +0x265fa | u16 | progress (0x5A00 = done) |
| +0x265fc | u16 | previous progress (drawing only) |
| +0x26600 | f32 | accumulated cost |
| +0x26604 | i32 | paid |
| +0x26608 | u8 | hold |

**Record fields used** (building record, `0x6dc540 + rec·0x10c`):
`+0x10 + i` tech for build (i = 0) and upgrade i (1..3), `+0x1c + 4i`
cost[i], `+0x2c + 4i` rate[i] (doc 08 calls it build time; it is the
progress added per tick), `+0x88` name index. Prerequisite levels: unit
record `+0x27`, building record `+0x45` (prerequisite 1) and `+0x4d`
(prerequisite 2). Doc 08 lists `+0x27`, `+0x45` and `+0x4d` as "?"; they
are these minimum upgrade levels (verified in `FUN_0046be50`).

### 1.1 Upgrade data in 1.06 (Templates.bin)

Only these names have a record with tech[1] ≠ 0xFF:

| Name (index) | Records (A / H / O) | tech[1] | cost[1] | rate[1] | Ticks | tech[2], tech[3] |
|---|---|---:|---:|---:|---:|---|
| CONSTRUCTION_YARD (0) | 0 / 1 / 2 | 6 | 1000 | 18 | 1280 | 255, 255 |
| BARRACKS (4) | 13 / 14 / 15 | 2 | 500 | 54 | 427 | 255, 255 |
| AT_HIGH_TECH_FACTORY (11) | 32 (one record) | 6 | 1500 | 12 | 1920 | 255, 255 |
| LIGHT_FACTORY (13) | 35 / 36 / 37 | 2 | 400 | 42 | 549 | 255, 255 |
| HEAVY_FACTORY (15) | 41 / 42 / 43 | 3 | 800 | 24 | 960 | 255, 5 (record 44: 255, 255) |

- Ticks = ceil(23040 / rate[1]); at the default 20 ticks/s that is 64 s,
  21 s, 96 s, 27 s and 48 s.
- **Only level 1 is reachable.** Every record has tech[2] = 255, so
  `FUN_004431d0` refuses the 1 → 2 step. HEAVY_FACTORY's tech[3] = 5,
  cost[3] = 200 and rate[2] = rate[3] = 50 are dead data.
- HIGH_TECH_FACTORY (name 12, Harkonnen / Ordos) has cost[1] 1500 and
  rate[1] 12 but tech[1] = 255: not upgradable. Only the Atreides
  high-tech factory has an upgrade.
- Records chosen by `FUN_0046c450` (last record whose house mask has the
  house bit, else the first of the name): Atreides 0/13/35/41,
  Harkonnen 1/14/36/42, Ordos 2/15/37/43, AT_HIGH_TECH_FACTORY 32 for
  every house (its only record).

### 1.2 What level 1 unlocks (prerequisite levels in the data)

Upgrade level of the **prerequisite 1** building (`player+0x24544[p1]`
≥ record value), checked by the build-list rebuild `FUN_0046be50`
(doc 11 §6). Units check only prerequisite 1's level; buildings check
both prerequisites' levels.

| Needs level 1 of | Units (record) | Buildings |
|---|---|---|
| BARRACKS | TROOPER (1), ENGINEER (2), THUMPER (3, skirmish only), GRENADIER (27, Atreides), SARDAUKAR (29, Harkonnen, skirmish only) | — |
| LIGHT_FACTORY | QUAD (7), STEALTH_RAIDER (28, Ordos, skirmish only) | — |
| HEAVY_FACTORY | MCV (12), MISSILE_TANK (13, not Ordos), SIEGE_TANK (15) | REPAIR_PAD, RESEARCH_CENTRE (IX) |
| CONSTRUCTION_YARD | — | CONC2 (large concrete), LARGE_GUN_TURRET (rocket turret) |
| AT_HIGH_TECH_FACTORY | ORNITHOPTER (20, the Atreides airstrike, kind 9) | — |

Level 0 (no upgrade needed): LIGHT_INF, Imperial SARDAUKAR (4), TRIKE (5,
mask 0x1b), RAIDER (6, Ordos), HARVESTER, the COMBAT_TANKs, DEVIATOR,
SONIC_TANK, DEVASTATOR, the CARRYALLs; all other buildings. ("Skirmish
only" = unit record +0xaa, doc 11 §6.) Building prerequisite-2 levels
are all 0 in 1.06.

## 2. The upgrade list (`FUN_0046e7a0`)

**When:** once per mission, for all 8 players, at mission start in
`FUN_00448550` (`0x448d6d`), and only when not loading a saved game
(`gLoadSavedGame 0x5178bc` = 0; a loaded game restores it from the
save). **It is never rebuilt during the mission.** It does not depend on
what the player owns, the tech level or the house (beyond the record
mapping). Verified: `FUN_0046e7a0` has one caller.

**Content:** walk the building records 0..61 in order. A record whose
tech[1] (`+0x11`) ≠ 0xFF and whose name index was not seen yet adds
`FUN_0046c450(name, 0)` (this player's record of that name) and marks
the name. The rest of the 8 entries are −1. Before each record the code
checks the count: count > 7 is a fatal error ("Invalid data for
upgrade"), so at most 8 names (1.06 has 5).

**Result in 1.06**, for every house:

| Index | Cell (x, y) | Name |
|---:|---|---|
| 0 | (501, 184) | CONSTRUCTION_YARD |
| 1 | (563, 184) | BARRACKS |
| 2 | (501, 231) | AT_HIGH_TECH_FACTORY |
| 3 | (563, 231) | LIGHT_FACTORY |
| 4 | (501, 278) | HEAVY_FACTORY |
| 5–7 | | −1 |

Cell = column i & 1 (x 501 / 563), row i >> 1 (y 184 + 47·row), as the
Starport page. A Harkonnen or Ordos player sees cell 2 empty (they own
no AT_HIGH_TECH_FACTORY) unless they capture one; then it can be
upgraded.

### 2.1 Can upgrade (`FUN_004431d0(player, name)`)

True when all hold:

1. name ≥ 0;
2. the player owns a building of this name (`+0x245c1[name]` ≠ 0);
3. level = `+0x24544[name]` ≤ 2;
4. tech[level + 1] of `FUN_0046c450(name, 0)` ≤ the mission tech level
   `0x6b87c8[player]`.

It does **not** check whether an upgrade is already running, nor money.

## 3. Sidebar

### 3.1 Control 9 (UPGRADE tab)

Each game loop (`0x449542`, after the Starport tab update):

- `FUN_00446760(local player)` = any list entry ≠ −1 with
  `FUN_004431d0` true.
- false → control 9 state = 0 (blank plate, cannot be clicked), on any
  page;
- true → if state is 0 and the page is 1, state = 1.

Mission start sets it to 0 (`0x4489d1`). There is no blinking and no
sound. While an upgrade runs the button stays (the running building
still passes `FUN_004431d0`). After the last upgrade the button goes
blank. If that happens while page 3 is open, MAIN (control 12) sets
control 9 back to 1 (doc 11 §5 table) and the next loop's tab update
sets it to 0. **That one frame shows** (verified from the loop order
of `FUN_00448550`): the input `FUN_004433a0` runs at the end of a loop
(`0x449852`); the next loop draws the sidebar and its controls
(`FUN_00427cc0` at `0x4491e0`) before the tab updates (`0x4494cd` for
the Starport tab, `0x449549` for this one) and then blits the frame, so
the UPGRADE plate is on screen for one frame before it goes blank. The
same one-frame lag applies to every tab state the update changes.

**Page changes** (doc 11 §5, verified again at `0x444948`,
`0x444b87`, `0x444bc8`): 1 → 3 by control 9 (0–3 → 0, 9 → 0, 12 → 1);
2 → 3 by control 9 (also cancels a Starport cart); 3 → 1 by MAIN 12
(0–3 → 1, 9 → 1, 12 → 0); 3 → 2 by STARPORT 10. No sound on page
changes. The upgrade page does not touch the build page state: the
build lists, building slot and unit slots keep running.

### 3.2 Drawing (`FUN_004278a0`)

Called for page 3 (`0x44915d`) with tech = `0x6b87c8[local player]` and
full = 1 unless page 3 was also the page drawn in the previous loop
(`0x516434` = 3) and `0x5178ac` is clear (`0x44912a..0x449140`). So the
first draw after opening the page is a full one, as for page 1 and its
`FUN_00426e80` call. `0x5178ac` is set only by the menu code at
`0x4732e0` / `0x4734b5` (after an in-game menu closes).

**Progress buffer.** If the upgrade slot is busy:

- full: grey copy of the slot record's icon into `0x789194`
  (`FUN_004326b0`), then wedge 0 → progress>>6;
- else: if previous progress = 0, grey copy first; then wedge
  prev>>6 → progress>>6 (`FUN_004263b0`, doc 11 §13).

**No shared-buffer artefact (corrected 2026-10-16).** `0x789194` is
also the building-slot buffer of page 1, but each page's first draw
after a page change is full (above), so the cell always starts from a
grey copy of the upgrade's own icon with the wedges 0 → progress. While
page 3 stays open only `FUN_004278a0` writes the buffer (the other
readers are the page-1 draw `FUN_00426e80` and the loaders), and a new
upgrade starts with previous progress 0, which repaints the grey icon.
The earlier note ("the building's clock picture with the newest
upgrade wedges") assumed opening page 3 was not a full draw; it is.

**Pulse colour:** counter `0x512d74` (own counter, not page 1's
`0x512d70`), +1 per call, reset to 0 when > 40; colour bytes
(c + 20, c + 210, c + 20) as on page 1.

**Each of the 8 cells:**

| Case | Drawn |
|---|---|
| −1, or the player owns no building of the name | empty cell (`0x6d60d0`) |
| no upgrade running, level < 3 and tech[level + 1] ≤ tech | icon, normal |
| no upgrade running, otherwise | icon greyed |
| this cell's record is the running upgrade | progress buffer; on hold also "Hold" (key `0x4de268`) in font 1, box at (x, y + 16), 60 wide, pulse colours |
| an upgrade runs, other cell | icon greyed |

There is no "Ready" text and no outline: an upgrade completes in the
same tick it reaches 0x5A00. There is no half-step scroll frame (the page
does not scroll). After the upgrade the icon stays, greyed (tech[2] is
255).

### 3.3 Clicks

Grid: 501 < x < 623 and 184 < y < 372; row = (y − 184) / 47, column 1
when x > 561, index = row·2 + column.

- **Left release** (`0x444d9f`): if entry ≥ 0 and
  `FUN_004431d0(local, name)`: post 0x2D with the **name index**
  (`FUN_0045a350`). Otherwise nothing, no sound. It does not check
  whether another upgrade runs (see §5.1: that resumes a held upgrade).
- **Right press** (`0x444287`): post 0x2E with the entry (**record
  index**, as a byte, so −1 becomes 255 and matches nothing) for any
  cell in the grid. Doc 11 §6: a right press also sets mode 0.
- **Right press outside the grid:** deselect all (doc 11 §10).

### 3.4 Tooltip (`FUN_0044b5c0`, page 3)

Column = min((x − 501) / 60, 1), so at x = 561 the tooltip uses the right
cell while a click uses the left one. If the entry ≠ −1 and the player
owns the name:

- line 1: the building name (`0x5175e8[name]`);
- line 2: if `FUN_004431d0` is true, `"$%d"` with **cost[level + 1]
  read straight from the record**, without the difficulty factor (on easy
  the tooltip says $1000 for the yard upgrade but 750 is charged);
  otherwise no cost line (value 0).

## 4. Orders

Orders go through the normal order queue (`FUN_0045bf00`): type byte
+0x23, player +0x24, item i16 +0x26, tick +0x08. ProcessOrder
(`FUN_00457810`) switches on type − 0x13: case 0x1A = **0x2D**, case 0x1B
= **0x2E**.

### 4.1 0x2D start / resume (`FUN_00457350`, item = name index)

1. name > 99 → fatal error.
2. `FUN_004431d0(player, name)` false → return.
3. **Slot empty** (item < 0): item = `FUN_0046c450(name, 0)` (record),
   progress = prev = 0, float = 0, paid = 0, hold = 0.
   - If spice + cash (`+0x24254 + +0x2425c`, pending money not counted)
     = 0: hold = 1 and the local player hears `*_LOWMONEY`. Return.
   - Otherwise the local player hears `*_BUILDUPGRADE`.
4. **Slot busy:** if not on hold → return (nothing, no sound). If spice +
   cash = 0 → return. Otherwise hold = 0 and `*_BUILDUPGRADE`.
   **This resumes the held upgrade whichever available icon was
   clicked** (same as buildings, doc 11 §6).

Voices: `H_` / `O_` / `S_BUILDUPGRADE` (strings `0x4e36e4` /
`0x4e36d4` / `0x4e36c4`), picked by house like the other `*_` voices,
played through `FUN_0046f900`. `*_LOWMONEY` is requested with the first
flag 1 (`FUN_0048dd90(.., 1, 0, 0)`), `*_BUILDUPGRADE` with 0, as in doc
11's production voices.

### 4.2 0x2E hold / cancel (`FUN_00457490`, item = record index)

Only when item = the slot's record:

- not on hold → hold = 1, `*_ONHOLD`;
- on hold → **cancel**: refund = progress · cost / 0x5A00 (u32 integer
  math) into pending cash (`FUN_0046c7b0`), item = −1, `*_CANCELLED`.
  The other slot fields are left as they are (reset at the next start).

There is no ready state, so unlike the building slot (order 0x25) a
running upgrade can never be cancelled with one right click: hold
first, then cancel.

**1.06 bug (verified at `0x4574c2`):** the refund cost is
`FUN_00442be0(record, level + 1, player)` with level read as
`+0x24544[record]` — indexed by the **record** number instead of the
name index. Effects in 1.06 (the real level is always 0 while an upgrade
runs, so the right cost is cost[1]):

| House / upgrade | Level actually read | Wrong when |
|---|---|---|
| any CONSTRUCTION_YARD (records 0–2) | names 0–2 (CY, CONCRETE, CONC2) | never (all 0 during the CY upgrade; CONCRETE / CONC2 have no upgrade) |
| Atreides BARRACKS (13) | LIGHT_FACTORY's level | Light Factory already upgraded → cost[2] = 0, **refund 0** |
| Harkonnen BARRACKS (14) | SILO (always 0) | never |
| Ordos BARRACKS (15) | HEAVY_FACTORY's level | Heavy Factory already upgraded → **refund 0** |
| AT_HIGH_TECH_FACTORY (32), LIGHT_FACTORY (35–37), HEAVY_FACTORY (41–43) | names ≥ 24 (none; the array is 100 bytes, always 0) | never |

Reproduce this exactly (vanilla); a fix belongs in `fixes.ini`.

## 5. Production per tick (`FUN_004467e0`, `0x446b3b`)

Runs every game tick for every player (`0x798628` sides), after the
building slot and the unit slots of that player. With item ≥ 0 and
name = record `+0x88`:

1. **Building lost:** if `+0x245c1[name]` = 0 (no building of the name
   left): refund progress · cost(record, level[name] + 1) / 0x5A00 into
   pending cash, item = −1. **No voice**, no list rebuild. (Here the level
   index is correct.)
2. Otherwise, if progress < 0x5A00 and not on hold:
   - level = `+0x24544[name]`, rec = `FUN_0046c450(name, 0)`;
   - **step = rate[level + 1] of rec** (`+0x30 + 4·level`). No factory
     count factor, **no power scaling, no difficulty speed** (no call to
     `FUN_00442b10`).
   - cost = `FUN_00442be0(rec, level + 1, player)` = cost[level + 1] ×
     (easy 75, normal 100, hard 125) / 100 (`FUN_004429b0`; in single
     player only the local player gets the difficulty, the others 100;
     in multiplayer each side's own setting `0x6b922e[p·0x3c]`).
   - inc = (float)step · (1/23040, const `0x4c93b8`) · cost;
     due = ftol(inc + acc) − paid; if step + progress ≥ 0x5A00
     (32-bit), due = cost − paid. Same float scheme as the building slot
     (doc 11 §13).
   - `FUN_0046c720(due, "DoIconBuild(BuildingUpgraded)")`. **If it fails
     (not enough money) the slot does not move this tick**; no voice, no
     hold.
   - On success: prev = progress; acc = (float)inc + acc; paid += due
     (the add masks due to 16 bits, irrelevant for real costs);
     progress = (u16)(progress + step) — not clamped, e.g. the barracks
     ends at 23058.
   - **Done** when progress ≥ 0x5A00 (u16 compare):
     `+0x24544[name]` += 1, item = −1, then `FUN_0046be50` (rebuild the
     build lists, doc 11 §6).

**Completion effects:** that is all. There is **no upgrade-done voice**
(no such string exists; the exe's only upgrade voice is
`*_BUILDUPGRADE`). The local player hears `*_NEWOPTIONS` at the end of
`FUN_004467e0` (`0x446d1a`) if the unit or building list count grew
(doc 11 §6), which an upgrade usually causes. The building's graphics,
hit points, power, armour and production speed do not change. The only
visible change on the map is the upgrade marker (§7).

**One upgrade at a time, in parallel with production.** The upgrade
slot is separate from the building slot and the 10 unit slots; none of
them checks the others. Buildings, units and an upgrade all progress and
pay in the same tick.

**Not reset:** levels are only written here (`0x446cde`) and zeroed when
the player is created at mission start (`FUN_0046b990`, memset of the
player struct, via `FUN_004a45d0` from `FUN_00448550`). **The level stays
after every building of that name is lost**: a rebuilt barracks is
already upgraded. Verified by an operand scan of `+0x24544`; indirect
writes (save loading) restore the saved value. Levels do not carry over
between missions.

Slot init at mission start (`0x448ae0`): item = −1, prev = 0.

## 6. Computer player: UpgradeBuildings (`FUN_00421c40`)

Called from the AI update (`FUN_0041b610`) when the AI flag
UpgradeBuildings (block +0x05, default 1) is set (doc 22 §2.2).

1. Only when `(tick 0x5173f4) & 0x3F == 0` (every 64 ticks) and the
   side's upgrade slot is empty (item = −1).
2. Walk its upgrade list 0..7; the **first** entry ≠ −1 whose name passes
   `FUN_004431d0(side, name)` gets order 0x2D (`FUN_0045a350`, name).
   One order, then return.

So it upgrades in list order (yard, barracks, Atreides HTF, light
factory, heavy factory), as soon as it owns the building and the mission
tech allows, one at a time. **No money check, no CashStash, no
priority**: the upgrade then competes for money with production through
the normal per-tick payment.

**AI stall (verified, consequence of §4.1):** if the AI's spice + cash is
exactly 0 when the order is processed, the upgrade starts **on hold**.
The AI only acts on an empty slot and never sends 0x2E or a second 0x2D
for a busy slot, and nothing else clears the hold byte (operand scan of
`+0x26608`: only `FUN_00457350` / `FUN_00457490` write it). So that AI
never upgrades again in the mission, unless it loses every building of
that name (then §5 step 1 cancels the upgrade and frees the slot).

## 7. Other readers of the upgrade level

Operand scan of `+0x24544` (all direct references):

| Address | Function | Use |
|---|---|---|
| `0x4431fe` | `FUN_004431d0` | can upgrade |
| `0x427b56` | `FUN_004278a0` | page 3 drawing |
| `0x44bdc3` | `FUN_0044b5c0` | tooltip cost |
| `0x446b71`, `0x446bd6`, `0x446cd3/de` | `FUN_004467e0` | production, increment |
| `0x4574c2` | `FUN_00457490` | cancel refund (record-indexed bug) |
| `0x46c125`, `0x46c148`, `0x46c1d3` | `FUN_0046be50` | building prerequisite 1 / 2 levels, unit prerequisite 1 level (§1.2) |
| `0x42a120` | `FUN_00428e30` | upgrade marker `0x782058` (DATA.R16 110) at (x + 2, y + 6) of the bracket, `FUN_00430770` flags (1, 1) at `0x42a147`, on the local player's **selected** buildings whose name has level ≠ 0; drawn with the bracket and the silo / refinery pips (doc 10 §8, doc 21 §2.3) |

Nothing in combat, movement, power, hit points or production speed
reads it.

**Not related:** the strings `UPGRADE_NEEDED` / `UPGRADE_AVAILABLE`
(`0x4dbab0` / `0x4dba9c`) are used at `0x40905d..0x409094` with the
network session object `0x79759c`; inferred to be the multiplayer
version check, not building upgrades.

## 8. Implementation checklist

- Build the upgrade list once at mission start (record order, first
  record per name with tech[1] ≠ 0xFF, mapped to the house record, max
  8); save it with the game.
- `can_upgrade(player, name)`: owns name, level ≤ 2,
  tech[level + 1] ≤ mission tech.
- Control 9: 0 when nothing in the list can be upgraded; 0 → 1 only on
  page 1.
- 0x2D carries the name, 0x2E the record. Start with 0 money → hold +
  `*_LOWMONEY`; resume any held upgrade from any available icon.
- Step = rate[level + 1], no factors; cost = cost[level + 1] ×
  difficulty %; doc 11 float payment; stall when unpaid.
- Done: level + 1, slot −1, rebuild build lists, `*_NEWOPTIONS` if a
  list grew. No other effect.
- Lost all buildings of the name: silent refund and cancel.
- Keep the record-indexed refund bug (§4.2) and the tooltip's raw cost
  (§3.4); keep the AI stall (§6).
- Run the tab update after the sidebar draw (§3.1), so the control 9
  frame shows as in the exe (`Sidebar::update_tabs`).
- Draw the upgrade marker (§7, DATA.R16 entry 110, 16 × 16, no offsets)
  at (x + 2, y + 6) of the selection bracket, flags (1, 1), on the local
  player's selected buildings whose name has a level ≠ 0
  (`GameScreen::draw_selection`).
