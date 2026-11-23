# 47: The Deviator cloud and the in-game message lines (1.06)

Two visual details that `docs/parity.md` listed as open: "the smoke draw"
(Palace specials / Deviator row) and "deviator" (Combat row), and
"message line cutting and colour" (Mission script row). Conventions as in
docs 14 and 40. "Verified" = read in the decompile and checked in the
disassembly or in the 1.06 data.

## 0. Function and data map

| Address | Name / meaning |
|---|---|
| `FUN_0049be60` | bullet update: trail spawn right after the sonic damage call (`0x49c098`); impact spawn after the normal-impact damage call (`0x49c939`) |
| `FUN_00456df0` | create an explosion object (doc 14 §4.1); no `rand()` |
| `FUN_0049dd80` | explosion update (doc 14 §4.4); no `rand()`, no damage |
| `FUN_004282f0(surf, pass)` | explosion draw pass (doc 14 §5.1 / 5.3) |
| `FUN_004340e0` | translucent blit (flag 0x10) |
| `0x6e5750[type]` | default explosion flags (from Templates.bin) |
| `0x786c48 + c·0x10` | 16 house colours of colour slot c (u16 each) |
| `0x5175d8[side]` | colour slot of a side (starts as 0..7) |
| `FUN_00425720(text, unused)` | AddMessage: copy into the 5-slot ring |
| `FUN_004255e0(surf)` | top-left text: mission timer, then the message lines; expires the oldest |
| `FUN_00425830` | drop the oldest line |
| `FUN_004255c0` | clear the ring (count, head, tail = 0) |
| `FUN_0042dfb0(text, font)` | text width |
| `FUN_0042dd90(surf, text, x, y, font, colour)` | draw text; `FUN_00431f50` per glyph |

Ring globals: `0x504030` count (0..5), `0x504034` head (next slot to
write), `0x504038` tail (oldest), `0x50403c[5]` expiry tick,
`0x504050[5]` second argument (never read), `0x504064[5][0x80]` text.
The block `0x504030`..+0x2B4 is saved in savegames and in the .MIS file
(doc 19 §1, doc 27).

## Part A: the Deviator cloud

### A.1 What "the smoke draw" was (verified from git history)

The "Open: the smoke draw" note dates from commit `529bf3c` (Deviator),
which added only the damage-smoke **timer** `rand()` calls. The drawing
of that smoke (EX_SMOKE3 puffs, building flames) was done afterwards in
`feef79f` (doc 43). So that open item is **already done**; the parity
row was not updated. The Combat row's "Open: deviator" is likewise stale
(doc 40 is implemented, `combat.cpp:668`).

The Deviator's own visible "gas" is not a separate smoke system. It is
two ordinary explosion types:

| Type | Name | Frames | DATA.R16 entries | Flags `0x6e5750` | Sound |
|---:|---|---:|---|---|---|
| 14 | EX_DEVIATE | 23 | 4415–4437 | 0x18 | none |
| 15 | EX_GREENEXP | 5 | 4438–4442 | 0x18 | none |

(Entries: EX_FLASH, type 13, ends at 4414, so type 14 starts at 4415. The pixels are
consistent: these are the only images in the range drawn in palette
240..255.)

### A.2 Creation (verified)

- **Trail:** bullet 16 DEVIATOR has flag 0x10, so every tick the bullet
  update spawns its trail explosion (+0x12 = 15 EX_GREENEXP) at the
  bullet's current pixel (x, y) and height (`+0x2C`, 16.16; the missile
  flies at 0x60000 = 6 px), flags 0, no delay, owner = bullet owner.
  Order in the update: after the 0x1000 wall detonation and the sonic
  damage, before the homing / move code. One puff per tick of flight
  (≤ 17 ticks).
- **Impact:** at the normal impact the bullet update calls the damage
  (with `deviate = 1`, doc 40 §2), plays the impact explosion's sound
  (none for EX_DEVIATE) and spawns EX_DEVIATE at the target pixel
  (`+0x1C`, `+0x1E`). The height argument is `height >> 16` passed as a
  16.16 value (a few 1/65536 px), so in practice **height 0**. The
  0x1000 detonation path passes the full 16.16 height.
- No `rand()` call in the spawn, update or draw of either type (checked:
  `FUN_00456df0` and `FUN_0049dd80` call no rand). The RNG order is not
  affected by the cloud.

### A.3 Data, lifetime and movement (verified, doc 14 §4)

Plain explosion objects (type, frame, counter, flags, x, y, height). One
frame every 3 ticks; frame 0 is shown only until the first update.
EX_DEVIATE shows frames 1..22 for **66 ticks** (3.3 s);
EX_GREENEXP frames 1..4 for **12 ticks**. Flag 0x1 (rise) is not set
and there is no 0x1000 move, so the object does not drift. The visible
drift and growth come from the frames themselves: their R16 offsets change
from (18, −12) to (20, −7), (29, 5), … (−11, −4), so the cloud billows
and spreads to the lower right of the impact point.

### A.4 Draw (verified, `FUN_004282f0`)

- Pass 0 (height < 0x140000 = 20 px): both types, after the sorted
  buildings / ground units, before aircraft (doc 14 §5.1).
- **Flag 0x8 (house colours):** before the blit, if the image is 8-bit
  and has a palette, the 16 colours of slot `0x5175d8[owner]` are copied
  into the image's palette entries 240..255. Almost every pixel of these
  frames is in 240..255 (e.g. entry 4420: 650 of 896 pixels; the rest is
  index 0). So the cloud is drawn **in the firing side's house colour**
  (Ordos green normally; another colour for a Deviator that was itself
  deviated or captured).
- **Flag 0x10 wins over the others** (order 0x820, 2, 4, 0x10, ...):
  translucent blit `FUN_004340e0`: for index ≠ 0,
  `dst = (dst + pal[idx]) / 2` per channel. On a 565 surface the green
  channel is averaged at 6 bits (`(a & 0x7E0) + (b & 0x7E0)) / 2 & 0x7E0`);
  red and blue at 5 bits. Clip rectangle (0, 20)–(480, 400).
- The palette write is **in place** on the image's palette object. It
  has no lasting effect (verified 2026-10-16): the R16 loader
  `FUN_00460550` gives every type-1 entry with a palette its **own**
  palette object (the 520-byte block after its pixels; the handle in the
  file is not used for sharing), and only type-2 entries share the last
  loaded one. Entries 4415–4437 (EX_DEVIATE) and 4438–4442 (EX_GREENEXP)
  are all type 1 with their own palettes, and the next entry (4443)
  loads a new one, so no other image shares them. These two are the
  only explosion types with flag 8 in Templates.bin, and no spawner
  passes flag 8. Ours copies into a per-draw palette, which gives the
  same picture.

### A.5 Gameplay

None from the cloud. The deviation and the damage happen once, at the
impact, inside `FUN_0049d2e0` with radius 15 px (doc 40 §2). The
explosion update never damages or converts. A unit that drives into the
lingering cloud is not affected.

### A.6 Our code

| Item | Where | Status |
|---|---|---|
| Trail every tick, bullet pixel and height, before homing | `src/sim/combat.cpp:402` | done |
| EX_DEVIATE at the target pixel, height 0; deviate flag to `damage_at` | `combat.cpp:453`–`456`, `:498` | done |
| Flags = `explosion_flags[type] ^ caller` (0x18 from Templates.bin) | `combat.cpp:272`–`278` | done |
| Frame step every 3 ticks, removal | `src/sim/aircraft.cpp:597` `tick_effects` | done (doc 14) |
| Pass split at 20 px; order 0x820, 2, 4, 0x10, 0x200 | `src/app/game_screen.cpp:791`–`807` | done (the 0x40 / 0x80 tints are not drawn: no explosion type in Templates.bin has them) |
| House colours into 240..255 for flag 8 | `game_screen.cpp:803` passes `e.owner`; `src/render/sprites.cpp:157`–`172` copies into a per-draw palette | done (the `owner` index matches `0x5175d8`, identity) |
| Translucent average | `sprites.cpp:191`–`196` | done on our 555 surface (5-bit green; the exe's 565 path keeps a 6th green bit, last-bit difference only) |

## Part B: the message lines

### B.1 AddMessage `FUN_00425720(text, arg2)` (verified, disassembly `0x425720`..`0x42582e`)

```
n = min(strlen(text), 127)
copy n bytes into slot[head], terminate
arg2 → 0x504050[head]                 // never read anywhere (xrefs: this write only)
w = TextWidth(slot, font 1)
if w + 8 >= viewW (0x786d48 = 480):
    w = TextWidth(slot, font 1)        // the same value again
    while w + 8 < viewW:               // never true here: dead loop
        remove the last character; w = TextWidth(...)
expiry[head] = tick (0x5173f4) + 200
head = (head + 1) % 5
if count == 5: tail = head             // the oldest was overwritten
else: count += 1
```

- **No cutting, no word wrap.** The cut loop's condition is inverted
  (an original bug), so a line is only capped at **127 characters**.
  A wider line is drawn until a glyph no longer fits on the 640-wide
  game surface: `FUN_00431f50` skips a glyph unless
  `x + glyph_w < surface_w` and `y + glyph_h <= surface_h` (no partial
  glyphs; once one is skipped, all later ones are too).
- In the 1.06 data this never matters: of the 40 `text.uib` entries used
  by SHOWMESSAGE in the 46 .MIS files, the widest is
  `H7V1HiTechDestroyed` at **470 px** (470 + 8 = 478 < 480), then
  `H8V1ATPullOut` 469, `H7V1Intro` 461, `H8V1MercAllied` 445
  (FONTCOL widths, measured with the `FUN_0042dfb0` rule). The other
  callers' texts (below) are short.
- **No sound** in AddMessage. The callers play their own:
  SHOWMESSAGE plays `S_CHATMSG` as a UI sound (`FUN_0046f820`, doc 19);
  `BuildingEngineered` follows the `*_BUILDINGLOST` voice; the others
  play nothing.
- `arg2` is a colour or player argument in some callers (chat
  `FUN_0047bae0` passes `0x6b9844`, alliance texts −1), but it is never
  read, so **every line has the same colour**. This answers doc 19 §7's
  TODO.

Callers (all via `FUN_0048dcc0(FUN_0048dc10(key, 1, −1))` unless noted):
`ReachedUnitLimit` (CreateUnit `0x455fab`, CreateBuilding `0x4569c9`,
pool full), `AirStrikeReady` / `SaboteurReady` / `FremenReady`
(`0x446b1d`), `NeutralBecomeHostile%d` (`0x49f8d7`),
`BuildingEngineered` (`0x49a34f`), SHOWMESSAGE (`0x45442e`, entry index
+0x44), and multiplayer / network texts (`NoOneToPage`, `InPageOnState` /
`InPageOffState`, `UserPageHide`, `ModelOutOfSync`, `LostOnePlayer`,
`LostSomePlayers`, `UnableToAlly`, `BreakAlly` and alliance lines, chat).

### B.2 Draw `FUN_004255e0` (verified)

Called every drawn loop on the 640×400 game surface `0x5178b4`, after
the playfield, the radar (`FUN_00425fa0` / `FUN_00426000`), `FUN_00425860`
and the credits, and before the sidebar strips (`FUN_00426e80`).
Whether those strips would cover a line past x = 480 does not matter
in 1.06: the widest message ends at x = 8 + 470 = 478 (B.1).

- Font 1 (`0x5162b0`, FONTCOL), colour table `0x51676c` ("FontCol",
  FONTCOL.FPL), the same as the mission timer. One colour for all lines.
- Lines, newest first: slot `head − 1`, `head − 2`, … (mod 5), `count`
  lines, at x = 8, y = top (`0x4e41dc` = 20) + 4 = **24**, step
  **20 px** (0x14). While `PAGE_INGAME` or `GAMEMESSAGE` is open the
  first line is at top + 0x18 = **44**.
- **Expiry in the draw:** after drawing, if `expiry[tail] < tick`, the
  oldest line is dropped (`FUN_00425830`: count − 1, tail + 1). At most
  **one line per drawn frame**, and only the oldest is tested. A line
  added in tick T is therefore removed in the first draw with
  tick ≥ T + 201: it is on screen for **201 ticks**. Lines added in the
  same tick disappear on successive frames (3 frames apart at 60 Hz /
  20 ticks/s, hard to see).
- No fading, no scrolling animation: when the oldest line goes, the
  others keep their places (they are drawn from the newest down, so
  the bottom line simply vanishes; a new line pushes the rest down 20 px).

### B.3 Our code

| Item | Where | Status |
|---|---|---|
| 5 lines, newest first, overwrite the oldest | `src/sim/world.hpp:282`–`287`, inserts in `mission.cpp:200`, `capture.cpp:332`, `:359`, `economy.cpp:678` | done |
| x 8, y 24, step 20; font 1, FontCol colours | `src/app/game_screen.cpp:982`–`1015` | done |
| y 44 while PAGE_INGAME / GAMEMESSAGE is open | `GameScreen::draw_top_left_text` | done |
| 127-character cap | `game_screen.cpp:1007` | done |
| Cut while `width + 8 >= 480` | `game_screen.cpp:1008`–`1010` | **differs**: the exe never cuts (dead loop, B.1). Same result for all 1.06 texts (max 478 < 480) |
| Glyph skipped unless `x + w < 640` | `src/ui/surface.cpp:162` | done |
| Lifetime: 200-tick countdown in `World::tick`, all lines tested | `src/sim/world.cpp:764`–`766` | **differs**: exe stores `tick + 200`, removes only the oldest, in the draw, one per frame, when `expiry < tick` (201 ticks). One tick shorter here |
| Colour per message type | none in the exe | done (nothing to add) |
| SHOWMESSAGE plays S_CHATMSG | `mission.cpp:196`–`199` | done |
| `ReachedUnitLimit` when the object pool is full | `World::reached_unit_limit` (doc 50) | done |
| Save: the ring with absolute expiry ticks | `save_state.cpp:275` saves `messages` (countdown form) | equivalent if converted |

## Implementation checklist

Historical (the plan before the port); the items are done (the message
lifetime, y 44 and the cap in `GameScreen::draw_top_left_text`), apart
from the optional tests.

### A. Deviator cloud

1. `docs/parity.md`: drop "Open: the smoke draw" from the Palace specials /
   Deviator row and "deviator" from the Combat open list; cite doc 43 and
   this doc.
2. Optional test: fire a Deviator bullet; per flight tick one EX_GREENEXP
   (type 15) with flags 0x18 at the bullet pixel and height; at impact one
   EX_DEVIATE (type 14) with height 0; EX_DEVIATE lives 67 updates,
   EX_GREENEXP 13; no extra `rand()` draws.
3. Optional render check: render frame 4420 with owner 2 (Ordos) on a
   flat background; the result must be the translucent house green.
4. The shared-palette question is settled (A.4): no shared palette.

### B. Message lines

1. Remove the width cut in `draw_top_left_text` (keep the 127 cap); let
   `draw_text_indexed` skip glyphs at the 640 edge. (No visible change
   for 1.06 data.)
2. Store `expiry = tick + 200` per line instead of a countdown; in the
   draw (after drawing the lines), drop the **oldest** line if
   `expiry < tick`, at most one per frame. Bump the save version, or
   convert on load.
3. y = 44 for the first line while PAGE_INGAME or GAMEMESSAGE is open.
4. Emit `ReachedUnitLimit` when CreateUnit / CreateBuilding fail on a
   full pool (local message for every side, as in the exe: no side test).
   The pool is 1000 objects per side (doc 50), `World::kPoolSize`;
   done (`World::reached_unit_limit`).
5. Close doc 19 §7's TODO: `arg2` is never read.
