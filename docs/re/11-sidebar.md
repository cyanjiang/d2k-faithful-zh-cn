# 11: Sidebar, top bar and radar

Covers everything outside the 480×380 playfield in 1.06: what is drawn each
loop and how, the build lists, the Starport and upgrade pages, the sidebar
buttons, tooltips, and the player fields behind them. Decompiles are in
`re/exports/1.06/q_sb1.txt` … `q_sb9.txt` and `q_inp1.txt` (not committed).

"Verified" means read from the code or the data. "Inferred" is marked.

## Additions to docs 07 and 10

- **doc 07, UI_ENG.R16 images.** Each control has three images. In file
  order they are **state 1** (normal), **state 2** (pressed) and
  **state 0** (off, 16-bit). Slots: `0x5162cc`, `0x5162d0`, `0x5162c8`.
- **doc 07, controls 0–3** are the list scroll arrows (§5).
- **doc 07, extra entries** (`FUN_00460ec0`):

  | Entry | Global | Size | Use |
  |---:|---|---|---|
  | 0 | `0x6d6130` | 10×200 | power bar, empty |
  | 1–3 | `0x6d6134`..`0x6d613c` | 10×200 | power bar red, yellow, green |
  | 4 | `0x6d8d90` | 133×31 | "MISSION ACCOMPLISHED" |
  | 5 | `0x6dc010` | 67×31 | "MISSION FAILED" |
  | 6–8 | `0x6d8d98`..`0x6d8da0` | 128×128 | radar logo, Atreides / Harkonnen / Ordos |
  | 9 | `0x782768` | 128×128 | radar grille (rows alternate index 0 and dark) |

  The banners are drawn centred in the playfield by `FUN_00428ca0`:
  x = (480 − w)/2, y = (380 − h)/2 + 20. Failed wins over won.
  The entry offsets (−50, −12) / (−84, −12) do **not** apply: the blit
  `FUN_00430770` → `FUN_0042fac0` reads only the image's width and height
  (`img[0]`, `img[1]`); no image offset is read on this path. Flags (1, 1):
  index 0 transparent, index 1 the half-bright screen pixel (verified
  2026-10-16).
- **R16 palettes.** The 8-bit images in UI_ENG.R16 store their embedded
  palette as **555**. Verified in the resource loader `FUN_00460550`: the
  palette (and 16-bit pixels) are used as stored on a 555 surface, and only
  on a 565 surface (`0x798384` = 0xfbce, set by `FUN_004a4030` from the
  surface's red mask) converted with `(v & 0xffc07fc0) << 1 | v & 0x1f001f`.
  `src/formats/r8.hpp` says 555.
- **doc 10, drag box** (`FUN_00423c50`): drawn while dragging with the left
  button held, from the drag start to the mouse, colour `0x5162b8` =
  RGB(0xc6, 0xb5, 0xa5), clipped to (0, 21)–(479, 400). Drawn with the
  4-line rectangle `FUN_00430d10`.
- **doc 10, health bar colours** (`FUN_0044acc0`): green `0x516690` =
  RGB(0, 200, 0), yellow `0x516538` = RGB(220, 220, 0), red `0x51653c` =
  RGB(200, 0, 0). Colours are given as 0xRRGGBB and packed by
  `FUN_0042f3d0` to 555 as `(r>>3)<<10 | (g>>3)<<5 | b>>3`.
- **doc 10, radar click with units selected:** on left **release** over the
  radar (radar building, power ≥ 100, units selected), the units get a move
  order to that cell, `FUN_00459450`.
- **doc 08, "build time"** (unit +0x20, building +0x2c..): it is the
  progress added **per tick**, out of 23040. Bigger is faster (§7).

## 1. The screen surface

`FUN_00460c80` creates the 640×400 game surface (`0x5178b4`) and fills it
with `UIBB.R16`. **The backdrop is never redrawn.** Everything below draws
into this surface. Parts that are not redrawn keep what was there before.

| Part | Redrawn |
|---|---|
| playfield, build lists, credits, radar map | every loop |
| radar logo fade | only while fading (§4) |
| power bar | only the rows that changed (§3) |
| buttons and tabs | only when their state changes (§5) |
| tooltip, cursor | drawn last, restored at the start of the next loop |

## 2. Draw order (`FUN_00448550`, game loop)

At the start of each loop: `FUN_004239f0` puts back the cursor background
and `FUN_004239b0` the tooltip background.

The game view block runs when no menu is open, or when `0x5178ac` is set
(set for 2 loops after a menu closes, `FUN_00473130` / `FUN_00473300`):

1. `FUN_00423d20` map.
2. `FUN_00428ca0` objects, placement grid, the end banners.
3. Debug overlays when `0x5178c8` (off in retail).
4. `FUN_00423c50` drag box.
5. Radar (§4):
   - map toggle on (`0x51643c`): player list `FUN_00425c60`;
   - otherwise radar building (kind 1) and power ≥ 100: `FUN_00426000`;
   - otherwise the logo fade `FUN_00425fa0`.
6. `FUN_00425860` power bar.
7. `FUN_00425560` credits.
8. `FUN_004255e0` top-left text: draw-offer text, mission timer, messages.
9. Clamp the list scroll (§6), then the page by mode `0x516430`:
   - 1 build lists `FUN_00426e80`;
   - 2 Starport `FUN_00427690`;
   - 3 upgrades `FUN_004278a0`.
10. `FUN_00427cc0` buttons and tabs.

Then, every loop: the UI manager (menus) `FUN_004712c0`, the tooltip
`FUN_0044b5c0` (only when no menu) and `FUN_00423b40`, the cursor
`FUN_00423a20`, and the blit to the screen.

### Top-left text (`FUN_004255e0`)

All in font 1 (`0x5162b0`) with colour table `0x51676c` ("FontCol").

- **Mission timer** when `0x6b8270 ≠ −1`: `"%02d:%02d"` of
  `t / 1500` and `(t / 25) % 60`, at (420, 24). The counter is decremented
  once per game tick, so the clock assumes 25 ticks per second.
- **Messages** (`FUN_00425720` adds one): up to 5 lines, newest first, at
  x = 8, y = 24 + 20·i (y = 44 + 20·i while the in-game page or
  GAMEMESSAGE is open). Each line lasts 200 ticks. Lines are cut to fit in
  480 − 8 px.

## 3. Top bar

### Credits (`FUN_00425560`, `FUN_004254a0`)

- Value = spice (`player+0x24254`) + cash (`player+0x2425c`), capped at
  9,999,999.
- 8 digits, 10 px apart, from x = 390 (`0x4e4214`). y = `0x795560`. Only the
  debug text loader writes `0x795560`, so **y = 0 in retail** (it matches
  the dark 8-cell box in UIBB at x 388–470).
- Digit images are DATA.R16 entries 27–36 (`0x6e5720[0..9]`, 10×14).
  Leading zeros use entry 37 (`0x6e5748`, an empty cell). The last digit is
  always a number.
- Drawn every loop. The number rolls because the money itself drips (§8).
  There is no separate "displayed" value.

### OPTIONS (control 11)

Left down sets it pressed (state 2). Left up while pressed opens INGAME
(INGAME_SKIRMISH in skirmish, INGAME_MP in multiplayer). After the mission
ended it opens nothing, or ABORT_VM in multiplayer. Then it goes back to
state 1.

### Power bar (`FUN_00425860`)

- Position (485, 172) (`0x4e420c` / `0x4e4210`); images 10×200.
- Value `v` = power percent `player+0x245b0`, capped at 200 (the image
  height). **The bar fills from the bottom, 1 px per percent**, so 100 %
  fills half of it.
- Colour level: v < 51 red (image 1), v < 100 yellow (2), else green (3).
- Incremental draw against the last drawn value `player+0x245b4`:
  1. If it is 0xFFFF (new mission or loaded game), draw the empty image
     whole and treat the old value as 0.
  2. Same value: nothing.
  3. Lower: copy the empty image back over rows 200−old .. 200−new.
  4. Same level and higher: draw only the new rows of the level image.
     Level changed: draw the level image from row 200−new to the bottom.
  5. Store the new value.
- Tooltip: "POWER" text + `" %d%%"` (`"%s %d%%"`).

## 4. Radar

Origin (497, 8) (`0x4e41e4` / `0x4e41e8`), box 128×128 (the logo size).

### Radar off (`FUN_00425fa0`, `FUN_004259f0`)

- Counter `0x51647c`: set to 1 at mission start, reset to 0 when the radar
  map is drawn.
- While the counter ≤ 64: blend the house logo over what is in the box,
  then add 1. Blend per 555 channel:
  `out = (logo·a + old·(64 − a)) >> 6`, a = counter. Every pixel is
  blended, index 0 too. So the logo fades in over 65 loops, from the
  grille at mission start or from the last radar map.
- Logo = extra entry 6 + house, house = byte `0x8cd4f0[player]` (> 2 → 0).

### Radar on (`FUN_00426000`)

Each loop:

1. If the counter ≠ 0: draw the grille (extra 9) opaque once, counter = 0.
2. Draw the radar image `0x6d7ba0` (map_w × map_h, **one pixel per cell**)
   centred: x0 = 497 + (128 − map_w)/2, y0 = 8 + (128 − map_h)/2.
3. Units: one pixel at (x0 + cell_x, y0 + cell_y) (object +0x4c / +0x4d),
   for every player, when all hold:
   - the cell's shroud byte +8 (`0x517df8`) is 0 as a whole byte: no
     shroud and no shroud edge on the cell (doc 16 §1);
   - own unit, or the unit's flag +8 & 4 is clear (inferred: not cloaked);
   - flag +8 & 0x40 clear, or record kind 9 (ornithopter).

   Colour: the player colour `0x795518[player]`; sandworms (kind 5) use
   `0x5162b8`.
4. Viewport: rectangle from (x0 + cam_x/32, y0 + cam_y/32) to
   (+14, +11) (480/32 − 1, 380/32), colour `0x516690` (green).

### Radar image

Two buffers from `FUN_00461f60`: `0x781e54` (true colours) and `0x6d7ba0`
(shown). `FUN_00462140` fills them per cell from the cell dword +4:

| Cell | Colour |
|---|---|
| flag 0x10 (building) | player colour `0x795518[cell & 7]` |
| flag 0x700000 | RGB(100, 100, 100) |
| flag 0x10000 | RGB(30, 30, 30) |
| flag 0x2000 | RGB(60, 50, 50) |
| otherwise | RGB(70, 70, 70) |

(Colour globals, 16-bit: building `0x795518[owner]`, spice `0x517898`,
sand `0x5173f0`, drivable `0x5165c0`, other `0x516530`.)

**The true buffer is not recomputed from the cells.** `FUN_00462140` runs
only at mission start (`0x469b12` in `FUN_00469790`, after the spice and
bloom set-up, before any building is created) and after a saved game is
loaded (`0x441db6` in `FUN_004418b0`, after the cell array was read: the
saved cell flags are re-coloured, the old true buffer is not saved).
After that, the only writers are the pixel functions, called from four
sites (all xrefs of `FUN_004620b0` / `FUN_00462010`, verified 2026-10-16):

| Site | When | Cells | Colour |
|---|---|---|---|
| `0x44de4f` `FUN_0044dbd0` | building placed (`FUN_004563b0`: mission start, built, MCV deploy, capture re-creates it) | footprint (`rec+0x04` mask) | owner `0x795518[owner]` |
| `0x44e0a9` `FUN_0044df60` | building cells freed (`FUN_0044dee0`: destroyed / sold removal `0x49bdf9`, capture `0x46d78c`) | footprint | **always** `0x5165c0` RGB(60, 50, 50), also for records with flag 0x400000 whose cells are not restored |
| `0x499500` unit update, harvester | spice level 1 → 0 (doc 13 §3.1) | the harvester's cell (+0x4a, +0x4b) | sand `0x5173f0` RGB(30, 30, 30) |
| `0x49c8ac` `FUN_0049be60` | a bloom blob lands, thin branch only (doc 31 §5) | target cell | spice `0x517898` |

Every other cell change leaves the radar as it was: spice levels that stay
above 0, a thick-spice blob (the cell already had spice), terrain damage
and craters (doc 26), wall frames, unit occupancy. So the radar can
disagree with the cells (e.g. a removed building on non-drivable ground
stays RGB(60, 50, 50)) until the next load re-colours everything.

- `FUN_004620b0` writes a pixel to both buffers, but leaves the shown one
  alone while it is black (`0x5174f8`, shroud).

The flags are tested in this order in `FUN_00462140` (verified): 0x10
building (owner in the low 3 bits), then 0x700000 the spice level (any
spice), 0x10000 the TILEATR "sand" bit, 0x2000 the TILEATR "vehicles can
drive" bit, otherwise any other tile. The shroud's copy of pixels from the true buffer into
`0x6d7ba0` is in doc 16 §4.3.

### Map toggle (control 6, `FUN_00425c60`)

Skirmish and multiplayer only. The box shows the player names and scores
instead of the map. It is redrawn only while `tick & 0x10` is 0. It sets
the counter to 1, so turning it off starts the logo fade or the grille.

### Radar sounds (`FUN_0044aae0`, each tick)

They compare the new percent with the last drawn bar value `+0x245b4`:

- drop below 100 → `S_POWERDOWN`;
- rise to ≥ 100 → `S_RADARONLINE` if a radar building exists, else
  `S_POWERUP`;
- a radar building appears while powered → `S_RADARONLINE` once (flag
  `0x516481`).

## 5. Buttons and tabs

### Control table (`0x5162c8`, 15 × 0x18 bytes)

| Off | Field |
|---:|---|
| +0x00 | image, state 0 (off) |
| +0x04 | image, state 1 (normal) |
| +0x08 | image, state 2 (pressed) |
| +0x0c, +0x10 | x, y |
| +0x14 | state (byte) |
| +0x15 | last drawn state |

**Drawing (`FUN_00427cc0`):**

1. First every control whose state became 0 draws its off image.
2. Then every control with a nonzero, changed state draws that image.

At mission start all states are 1 and "last drawn" is 3, so everything draws
once. Then controls 9, 10, 12, 13 and 14 are set to 0.

**Hit test (`FUN_00443040(n, press)`):**

- the state must be 1;
- the mouse is inside (x, y, off-image size);
- the state-1 pixel under the mouse is not 0, so transparent corners miss.

With press = 1 it sets state 2. Any left release sets every state 2 back to
1. A right release does so for controls 0–3 only.

### Controls

| # | Image | Pos | Left down | Tooltip key |
|---:|---|---|---|---|
| 0 | up arrow | (502, 374) | left list −1 | |
| 1 | down arrow | (531, 373) | left list +1 | |
| 2 | up arrow | (566, 374) | right list −1 | |
| 3 | down arrow | (595, 373) | right list +1 | |
| 4 | wrench | (493, 138) | repair mode toggle (`0x516438` 2 ↔ 0), `BUTTON` | REPAIR |
| 5 | $ | (518, 138) | sell mode toggle (3 ↔ 0), `BUTTON` | SELLBUILDING |
| 6 | orb | (549, 141) | map toggle; ignored in the campaign | MapToggle (not in campaign) |
| 7 | shield | (573, 138) | guard (doc 10) | GUARD |
| 8 | retreat | (599, 138) | retreat (doc 10) | RETREAT |
| 9 | UPGRADE | (499, 167) | upgrade page | |
| 10 | STARPORT | (563, 167) | Starport page | |
| 11 | OPTIONS | (0, 0) | see §3 | |
| 12 | MAIN | (499, 167) | back from the upgrade page | |
| 13 | MAIN | (563, 167) | back from the Starport page | |
| 14 | PURCHASE | (501, 374) | Starport purchase | |

- Controls 4–8 and 11 stay in state 1 all game, so their off images are
  never used.
- The off images of 9/10/12/13 are a blank plate; that of 14 and 0–3 is
  the grille.
- Controls 0–3 react only in page 1.
- The UIBB button row shows other icons (shield at 493, wrench at 573), but
  the state-1 images cover them.

### Scroll buttons (page 1)

**Left click:**

- down: if 4 + scroll < count, scroll + 1 and `S_ICONSCROLL`;
  else `S_ENDSCROLL`;
- up: if scroll ≥ 1, scroll − 1 and `S_ICONSCROLL`; else `S_ENDSCROLL`.

**Right click** (a page):

- down: if 4 + scroll < count:
  - if fewer than 4 items are left below, scroll = count − 1 (the clamp
    then makes it count − 4);
  - otherwise scroll + 4.
- up: if scroll < 4, scroll = 0; otherwise scroll − 4.
- The sounds are as for a left click.

**Keys:** Down (`keystate 0x797ea0`) scrolls both lists +1 per loop while
held. Up (`0x797e9e`) scrolls both −1, only if Down is not held. No sound.
Page 1 only.

**Clamp** (every drawn loop): if count < 4 + scroll, then
scroll = max(count − 4, 0). Scroll globals: `0x516440` (left) and
`0x516444` (right).

### Page changes (left down)

| From | Control | To | States set |
|---|---|---|---|
| 1 | 9 UPGRADE | 3 | 0–3 → 0, 9 → 0, 12 → 1 |
| 1 | 10 STARPORT | 2 | 0–3 → 0, 10 → 0, 13 → 1, 14 → 1 |
| 2 | 14 PURCHASE | 1 | 0–3 → 1, 13, 14 → 0. If the cart is not empty: send the purchase (`FUN_0045a2b0`, order 0x2b) and play `*_PORTORDER` |
| 2 | 13 MAIN | 1 | 0–3 → 1, 10 → 1, 13, 14 → 0. If the cart is not empty: cancel it (`FUN_0045a310`, order 0x2c) and play `*_CANCELLED` |
| 2 | 9 UPGRADE | 3 | 9 → 0, 12 → 1, 13, 14 → 0, 10 → 1; cancels a non-empty cart as above |
| 3 | 12 MAIN | 1 | 0–3 → 1, 9 → 1, 12 → 0 |
| 3 | 10 STARPORT | 2 | 10 → 0, 13 → 1, 14 → 1, 9 → 1, 12 → 0 |

"Cart not empty" is `FUN_00443370`: any `player+0x24268[i]` ≠ 0.

The game loop also updates the tabs each loop:

- **Starport:** without a kind-13 building, control 10 is 0. If page 2 was
  open, it goes back to page 1, cancels the cart and sets 0–3 to 1 and
  13, 14 to 0. With a Starport, control 10 becomes 1 when it is 0 and the
  page is 1.
- **Upgrade:** control 9 is 0 when `FUN_00446760` finds no available
  upgrade in the upgrade list (`FUN_004431d0`, §10). Otherwise it becomes 1 when it is 0 and the page is 1.

## 6. Build lists (page 1)

### Layout (data globals)

| Global | Value | Meaning |
|---|---:|---|
| `0x4e41fc`, `0x4e4200` | 501, 184 | left (buildings) column |
| `0x4e4204`, `0x4e4208` | 563, 184 | right (units) column |
| `0x4e41f0` | 60 | cell width (and text box width) |
| `0x4e41f8` | 47 | row step |
| `0x4e41ec` | 4 | visible rows |

Row r of a column is at (x, 184 + 47·r). Each column is clipped to
(x, 184)–(x + 60, 372).

### Icons

60×47, 8-bit, from DATA.R16 (doc 08):

| Image | Source |
|---|---|
| building icon | `0x787c78[building record]` |
| unit icon | `0x6d9260[unit record]` |
| empty cell (past the end of a list) | entry 4 `0x6d60d0` |
| building progress buffer | `0x789194` |
| unit progress buffers, one per slot | `0x6e5858[10]` |

### Which items are listed (`FUN_0046be50`)

It is rebuilt at mission start, when a building is created
(`FUN_004563b0`) or removed (`FUN_00458e90`), and after an upgrade. Let
`owned[g]` be the OR of the house masks (+0x04) of the player's buildings
whose name index (+0x88) is g.

**Buildings**, only while the player owns a building with tech level 9 (the
construction yard; without one the list is emptied). A record is
**marked available** when all hold:

- prerequisite 1 (+0x40) ≠ −1 and `owned[p1] & mask1(+0x44)`;
- tech (+0x10) ≤ the mission tech level `0x6b87c8[player]`;
- upgrade level of p1 (`player+0x24544[p1]`) ≥ +0x45;
- if prerequisite 2 (+0x48) ≠ −1: `owned[p2] & mask2(+0x4c)` and upgrade
  level of p2 ≥ +0x4d.

Marks (`player+0x24718[record]`) are only set, never cleared while a
construction yard exists. **A building stays buildable after its
prerequisite is lost.** The list is the marked records in record order,
each mapped to this house's record (`FUN_0046c450(name, 1)`). A name is
listed **once**: with its second argument 1, `FUN_0046c450` returns 0xff
when a listed record already has the same name index (+0x84), and the add
`FUN_0046c680` ignores 0xff (verified). The mapping picks the last record
with that name whose house mask has this house's bit, else the first
record with the name. `FUN_0046c570(name, 1)` does the same for units.

**Units**, in unit record order, when all hold:

- prerequisite 1 (+0x28) ≠ −1 and `owned[p1] & mask(+0x2c)`;
- upgrade level of p1 ≥ +0x27;
- if prerequisite 2 (+0x30) ≠ −1: `owned[p2]` ≠ 0;
- tech (+0x24) ≤ mission tech;
- if +0xaa ≠ 0 (thumper, grenadier, stealth raider, Harkonnen
  sardaukar): not in the campaign (skirmish or multiplayer only); a
  thumper also needs `0x6b9838`.

Each is mapped with `FUN_0046c570(name, 1)`.

**After the rebuild:**

- An item that disappeared while in production is cancelled with a full
  refund of what was paid.
- `FUN_004467e0` plays `*_NEWOPTIONS` when a list count grew.
- `*_DEATHPREPPING` plays when DEATH_HAND first appears.

### Drawing (`FUN_00426e80`)

**Pulse colour.** Counter `0x512d70` goes +1 per call and wraps after 40
(1, 2, …, 40, 0, …). The colour is RGB(c + 20, c + 210, c + 20), a green
sawtooth. It is written into entries 2–15 of the text colour table
`0x5175cc`.

**Progress buffers.** If the building slot is busy, and this is a full
redraw (page just opened, or `0x5178ac`) or its previous progress is 0:

1. Copy the icon greyed into `0x789194` (`FUN_004326b0`).
2. `FUN_004263b0` copies the full-colour icon back into the buffer as a pie
   wedge: from angle prev/64 to angle progress/64 degrees (0..360). The
   wedge is centred on the icon and uses tangent tables at `0x6d91a8`.

The unit slots do the same with `0x6e5858[slot]`. So **a building or unit
in production shows as a grey icon that fills with colour like a clock**.

**Left column (buildings), each visible row:**

| Case | Drawn |
|---|---|
| no item (−1) | empty cell |
| nothing in production | icon, normal colours |
| this item is in production | the progress buffer |
| another item is in production | icon greyed |

On the item in production, while not in the half-step frame (below):

- if on hold (`+0x2652c`): text "Hold" (string key);
- if progress ≥ 0x5A00: text "Ready" and a pulse-colour outline. The
  outline is drawn by `FUN_0042d140` on the rect {X, Y, X + 59, Y + 46}
  (X + progress-buffer width − 1, Y + height − 1). `FUN_0042d140` treats
  right and bottom as exclusive: vertical lines at x = X and X + 58,
  horizontal lines at y = Y and Y + 45, each spanning X..X + 58 /
  Y..Y + 45 (verified, `0x427200` / `0x4275c0` and `FUN_0042d450`,
  `FUN_0042d520`).

Both texts are drawn in a box at (X, Y + 16), 60 px wide, centred:
`FUN_0042e0b0` draws at X + (60 − width) / 2 (unsigned shift; width from
`FUN_0042dfb0`). Font 1 (FONTCOL) through the colour handle `0x5175cc`:
each frame `FUN_00426e80` writes the pulse colour into handle entries
2..15 (`0x426f71`, REP STOSD of 7 dwords); entries 0 (transparent) and 1
(the black edge from FONTCOL.FPL) stay. The glyph blit `FUN_00431f50`
writes `handle[index]` for every index ≠ 0. The texts are skipped in the
half-step frame (`y0 == 184` test). Verified 2026-10-16.

**Right column (units), each visible row:**

| Case | Drawn |
|---|---|
| no item (−1) | empty cell |
| "busy" and a slot is building this item | its progress buffer, plus Hold / Ready like the left column |
| "busy" and no slot has it | icon greyed |
| not busy | icon, normal colours |

"Busy" means one of these:

- another slot is building a unit with the same prerequisite 1 (the same
  factory type), and that slot is not a special;
- the item itself is a special: kind 9 airstrike, 10 Death Hand,
  4 saboteur, 12 Fremen.

So while one factory type works, all its other units are grey. A special
that is not in a slot is grey.

**Half-step scroll frame.** When the scroll changed since the last drawn
loop (`0x516448` / `0x51644c`):

- the column is drawn 5 rows from y = 184 − 23;
- the first row is scroll − 1 if the scroll went up in index (down the
  list), else scroll.

This gives one in-between frame, clipped to the column. Texts and outline
are skipped in that frame.

### Mouse (page 1)

Columns for clicks: left = 501 < x < 561, right = 563 < x < 623,
184 < y < 372, row = (y − 184) / 47, item = list[row + scroll].

**Left release** (a click, not a box drag; any x in 561..623 counts as the
right column):

- **Building column:**
  - nothing in production, or progress < 0x5A00: send order 0x24
    (`FUN_00459f30`) for the item;
  - ready, and `+0x2652d` is clear: enter placement, mode `0x516438` = 1.
- **Unit column:**
  - normal unit: send order 0x27 (`FUN_0045a0b0`);
  - special: find a slot with this item at 0x5A00 and not on hold. If
    found:
    - kinds 4 and 12 set that slot's hold byte to 1 (consumed);
    - kind 9 → mode 4 (airstrike target);
    - kind 10 → mode 5 (Death Hand target);
    - kind 4 → `FUN_0045a440(player, 4, 0, 0)`;
    - kind 12 → `FUN_0045a440(player, 0xc, 0, 0)` (order 0x30).

**Right press** (it always also sets mode 0 and ends a drag):

- building column, item ≥ 0: set `+0x2652d` = 1, send order 0x25
  (`FUN_00459f80`);
- unit column, normal unit: send order 0x28 (`FUN_0045a100`). Specials
  ignore right clicks;
- otherwise the scroll buttons (page step);
- otherwise, on the playfield or the radar: deselect all.

### Production orders (handlers)

**0x24 start building (`FUN_00455fc0`):**

- Nothing in production: start this item at progress 0.
  - If spice + cash = 0, it starts on hold and plays `*_LOWMONEY`.
  - Otherwise it plays `*_BUILDING`.
- Something in production: if it is on hold and money > 0, resume it and
  play `*_BUILDING`. **This resumes the current item even when another icon
  was clicked.**

**0x25 hold or cancel building (`FUN_004560f0`):** only for the item in
production.

- Not on hold and not ready: put it on hold, `*_ONHOLD`.
- On hold or ready: cancel. Refund progress·cost/0x5A00 into pending cash,
  play `*_CANCELLED`.

**0x27 start unit (`FUN_00455510`):**

- A slot already has this unit:
  - on hold: resume if money > 0 or the cost is 0;
  - otherwise: nothing.
- Else, if `FUN_004430d0` allows (no non-special slot uses the same
  prerequisite 1; specials always allowed): take the first free slot of
  10.
  - Money = 0 starts it on hold with `*_LOWMONEY`.
  - Voices: specials none; infantry (+0x11) `*_TRAINING`; others
    `*_BUILDING`.

**0x28 hold or cancel unit (`FUN_00455750`):**

- Hold if not on hold and not ready, otherwise cancel with refund.
- It searches only slots 0–7, so a unit in slot 8 or 9 cannot be held or
  cancelled.

**Ready:**

- **Building:** `*_BUILDREADY`. It waits for placement.
- **Unit:** spawns at a factory exit (`FUN_00442c20`, `FUN_00455870`).
  The slot frees and `*_UNITREADY` plays. Without a free exit it retries
  every tick at 0x5A00.
- **Specials:** text message `AirStrikeReady` / `SaboteurReady` /
  `FremenReady`, or `*_DEATHREADY`.

While a unit slot works, the factory object gets flag +0x28 | 0x10 (not for
Death Hand).

## 7. Production rate and cost (`FUN_004467e0`, once per game tick)

Progress is a u16 where **0x5A00 (23040) = done**. Per tick, for a slot that
is not done and not on hold:

- **step**, by slot:
  - building: record build rate (+0x2c) × F / 100;
  - unit: record rate (+0x20) × F / 100;
  - upgrade: +0x30 + 4·level, no F.
- **F (building)** (`FUN_00442b10`): construction yards (kind 2) owned:
  1 → 100, 2 → 150, 3+ → 200.
- **F (unit)** (`FUN_00442a00`): the same count of buildings whose name
  index is the unit's prerequisite 1. One equivalent name also counts
  (`0x6d60dc` byte 2 → byte 1).
- **Power scaling** of F: percent < 10 → F/8, < 50 → F/4, < 100 → F/2.
- **Specials:** kinds 9, 4 and 12 use F = 100. Death Hand uses 100 if power
  ≥ 100, else 0.
- **Difficulty speed** (`FUN_00442960`): easy 125, normal 100, hard 75
  (%). Computer players always use 100 in single player.
- **Cost** (`FUN_00442be0` buildings and upgrades, `FUN_00442bb0` units):
  record cost × (easy 75, normal 100, hard 125) / 100.
- **Payment:**
  1. Add step/23040 × cost to the float `+0x26524`.
  2. The amount due is ftol of that minus the paid int `+0x26528`.
  3. On the last step it pays cost − paid instead.
  4. `FUN_0046c720` takes the money. **If the player cannot pay, the slot
     does not move this tick.** There is no voice for this.

Example: LIGHT_INF rate 180 at F = 100 needs 128 ticks.

## 8. Money, power and player fields

### Player struct

`FUN_004a4660(p)` = `0x798870 + p·0x26990`, 8 players.

| Off | Field |
|---|---|
| +0x08 / +0x10 | unit / building list heads |
| +0x24252 | player index byte (indexes `0x6b87c8`, the .MIS tables) |
| +0x24253 | house |
| +0x24254 | spice |
| +0x24258 | spice capacity |
| +0x2425c | cash |
| +0x24260 | pending cash (drips into cash) |
| +0x24264 | pending spice |
| +0x24268[60] | Starport cart count per unit record |
| +0x24358[60] | Starport stock |
| +0x24448[60] | Starport price |
| +0x24544[100] | upgrade level per building name index |
| +0x245c1[100] | "owns a building of this name" |
| +0x245a8 | power produced |
| +0x245ac | power used |
| +0x245b0 | power percent |
| +0x245b4 | last drawn power bar value (0xFFFF = redraw) |
| +0x24718[100] | building record available marks |
| +0x260b4 / +0x260b8 | building list count / previous count |
| +0x260bc[60] | building list (records) |
| +0x261ac / +0x261b0 | unit list count / previous count |
| +0x261b4[60] | unit list |
| +0x262a5 | Starport order sent (icons grey, clicks ignored) |
| +0x262a8[8] | Starport units |
| +0x262c8[8] | upgrade list (building records) |
| +0x2651c | building slot: item (i16, −1 none) |
| +0x2651e | building slot: progress |
| +0x26520 | building slot: previous progress |
| +0x26524 | building slot: float cost |
| +0x26528 | building slot: paid |
| +0x2652c | building slot: hold |
| +0x2652d | building slot: placement already sent |
| +0x26530 | 10 unit slots × 0x14: item, +2 progress, +4 prev, +8 float, +0xc paid, +0x10 hold |
| +0x265f8 | upgrade slot: item |
| +0x265fa | upgrade slot: progress |
| +0x265fc | upgrade slot: previous progress |
| +0x26600 | upgrade slot: float |
| +0x26604 | upgrade slot: paid |
| +0x26608 | upgrade slot: hold |

**Start credits:** `FUN_0046bd20` sets cash = .MIS credits
`0x6b87d0[player index]` at mission start, with no drip.

### Cash drip (`FUN_0046c7d0`, each tick, all players, via `FUN_0044aca0`)

1. If pending cash (PC) and pending spice (PS) have opposite signs, the
   smaller one in size goes through the real values (corrected
   2026-10-16, from the decompile of `FUN_0046c7d0`; we used to cancel
   the two pendings against each other without touching cash or spice):
   - |PC| < |PS| (`0x46c894`): PS += PC; spice −= PC; cash += PC; PC = 0.
   - otherwise: PC += PS; cash −= PS; spice += PS; PS = 0.
   The total (cash + spice + PC + PS) does not change; only its split
   between cash and spice does (a spend paid out of pending cash while
   spice is coming in moves that much from cash to spice).
2. step = max(10, (|pending cash| + |pending spice|) / 16).
3. Pending cash ≠ 0: move up to `step` of it into cash. Same for spice.
   Each one plays `S_MONEYTICK` for the local player, at most once per
   15 ticks (`FUN_0046f820` arg 3).

- **Spend** (`FUN_0046c720`): fails if cash + spice + both pending < amount.
  Otherwise it takes from pending spice first (up to spice + pending
  spice), the rest from pending cash.
- **Refund** (`FUN_0046c7b0`): pending cash += amount.
- **Capacity** (`FUN_0044abe0`, each tick, also from the capture
  `FUN_0046d6d0` at `0x46d853`): per side, first the drip above, then
  capacity = 2000 per refinery (kind 4) + 1500 per silo (kind 5) in the
  side's building list. If capacity < spice (unsigned): pending spice
  < 0 → pending spice += spice − capacity (the cut part pays the pending
  spend first); else pending spice = 0 (incoming spice is lost); then
  spice = capacity. (Corrected 2026-10-16: we only cut the spice; the
  capture also ran no drip.)
- **Order in the tick** (`FUN_0044aca0` then `FUN_00448550`): power of
  every side and the power sounds (`FUN_0044aae0`), drip + capacity of
  every side (`FUN_0044abe0`), the unit reveal (`FUN_0044cff0`,
  `0x449aeb`), production of every side (`FUN_004467e0`, `0x449af0`).

### Power (`FUN_0046ca00`, each tick, via `FUN_0044aae0`)

- produced = Σ over buildings with power < 0 of
  |power| · (hp·100 / max_hp) / 100. **Damage reduces output.**
- used = Σ of positive power.
- If produced ≠ 0, both get + 200.
- percent = produced·100 / used, or 200 if used = 0.
- A player with no buildings has 0.

### Voices from `FUN_004467e0` (local player)

| Voice | When | Repeat limit |
|---|---|---|
| `*_LOWPOWER` | percent < 100 and produced ≠ 0 | 2000 ticks |
| `*_SILOSNEEDED` | capacity < spice + 100 and the player owns the group at `0x6d60dc` byte 3 (inferred: refinery) | 2000 ticks |
| `*_LOWMONEY` | cash + spice reaches 0 | once; re-armed at ≥ 100 |

## 9. Starport page (mode 2, `FUN_00427690`)

**Grid:** item i at column i & 1 (x 501 / 563), row i >> 1. Units are
`player+0x262a8[i]`. All 8 are drawn (no −1 check).

**Icon:** greyed if any of these, otherwise normal:

- stock `+0x24358[u]` is 0;
- an order was sent (`+0x262a5`);
- the cart total is > 5.

**Count:** the cart count `+0x24268[u]` is drawn as a small digit
(`0x6e2df0[n]`, DATA.R16 17–26) at (x + 4, y + 4), transparent.

**Clicks** (only while no order is pending):

- left release: add one (order 0x29, `FUN_0045a200`);
- right press: remove one (order 0x2a, `FUN_0045a260`);
- right press outside the grid: deselect all.

The add limit (`FUN_00456fd0`) and the price / stock changes
(`FUN_0046ddc0`) are read from the code in doc 34 §2 and §3.2.

## 10. Upgrade page (mode 3, `FUN_004278a0`)

**Grid:** as the Starport page. The list is `player+0x262c8[i]` (building
records), drawn with the building icons.

**Drawing:**

| Case | Drawn |
|---|---|
| −1, or the player owns no building of that name | empty cell |
| no upgrade running, upgrade available | normal |
| no upgrade running, not available | greyed |
| the running upgrade | progress buffer `0x789194` (shared with page 1, re-filled from the upgrade icon on every entry to page 3: third argument 1 unless the last page was 3, `0x44912a`), plus "Hold" in the pulse colour (counter `0x512d74`) drawn **solid in font 0** (`FUN_0042e050` → `FUN_0042dc00` with a colour, not page 1's FONTCOL handle); no "Ready" |
| any other item while an upgrade runs | greyed |

"Available" means level < 3 and the next level's tech (+0x11 + level) ≤
the mission tech (`FUN_004431d0`).

**Clicks:**

- left release on an available item: start (order 0x2d, `FUN_0045a350`);
- right press: hold or cancel (order 0x2e);
- right press outside the grid: deselect all.

When done the level goes up, the slot clears and the lists are rebuilt.

## 11. Tooltips (`FUN_0044b5c0`, drawn by `FUN_00423b40`)

- **Build list cells** show at once:
  - line 1 is the name (buildings `0x5175e8[name]`, units
    `0x517408[name]`);
  - line 2 is `"$%d"` with the cost after difficulty, if nonzero;
  - Starport cells show the current price, upgrade cells the next
    level's cost;
  - anchor: the cell centre (x + 30, 184 + 47·row + 23).
- **Buttons 4–8 and the power bar** need the mouse to be still:
  - **The delay counter quirk:** `0x51756c` counts loops in which the
    mouse world position (screen + camera) equals the last loop's. It
    must pass 17 (18 still loops). It resets only when a shown tooltip
    disappears.
  - Button anchor: (button x, 160). Power bar anchor: the mouse.
- **The box:** text at anchor x + 3.
  - It is moved up if it passes y = 400.
  - It is moved left so its right edge is ≤ a limit. The limit is the
    column's x (501 / 563), the button's x, or 485 for the power bar.
  - **So sidebar tooltips appear to the left of what they describe.**
- **Colours:** `0x5175c0` = `0x5174fc` = bytes (0x1e, 0xe6, 0xe6), which is
  RGB(230, 230, 30): `FUN_0042f3d0` packs byte 2 as red, byte 1 as green,
  byte 0 as blue (verified).
  `FUN_0042d660` fills the box with colour 0 and draws its border in that
  colour; the text uses it too. The cost line gets its own box and a
  colour-0 line.
- **Edges (verified, `FUN_0044b5c0` end and `FUN_00423b40`):** the rects
  are {left, top, right, bottom} with right / bottom exclusive
  (`FUN_0042d1f0` fills rows top..bottom − 1, columns left..right − 1;
  `FUN_0042d660` then draws lines at left, right − 1, top, bottom − 1).
  Main box: right = x + width + 5, bottom = y + font-0 height + 2. Cost
  box: top = main bottom − 1, right = x + cost width + 5, bottom = top +
  font height. The colour-0 line is at (x + 1, cost top), length
  min(cost w, main w) − 2. Draw order: main box, cost box, line, cost
  text, main text; texts at (box x + 3, box top).
- **Power bar hit test:** 485 ≤ x ≤ 485 + 10 (the width of `0x6d6130`,
  UI_ENG.R16 extra 0 is 10×200) and 172 ≤ y ≤ 400, all inclusive. Text
  `"%s %d%%"` (`0x4e0a04`) with the POWER string.
- **Background:** the screen under the box is saved before drawing and put
  back at the start of the next loop.

## 12. Sounds used here

| Sample | Where |
|---|---|
| `BUTTON` | repair / sell toggles |
| `S_ICONSCROLL`, `S_ENDSCROLL` | list scrolling |
| `S_MONEYTICK` | cash drip (≤ 1 per 15 ticks) |
| `S_POWERDOWN`, `S_POWERUP`, `S_RADARONLINE` | §4 |
| `*_BUILDING`, `*_TRAINING`, `*_ONHOLD`, `*_CANCELLED`, `*_LOWMONEY` | production orders |
| `*_BUILDREADY`, `*_UNITREADY`, `*_DEATHREADY`, `*_DEATHPREPPING`, `*_NEWOPTIONS` | production |
| `*_LOWPOWER`, `*_SILOSNEEDED` | §8 |
| `*_PORTORDER` | Starport |

`*_` is the house-prefixed voice (H_ / O_ / S_, doc 10). There is **no
sound** for a click on a build icon besides these voices, and none for a
stall from lack of money.

## 13. Follow-up findings (implementation)

- **Font 1** is `FontCol` (FONTCOL.FNT): the game loop loads it with
  `FUN_0042d6f0("FontCol", [0x5162b0] = 1, …)` at `0x448b4d`. Fonts live at
  `0x513b20 + i·0x408` (the .FNT layout). `FUN_0042e0b0` centres the text:
  x + (w − width)/2 (`FUN_0042dfb0` is the width). "Hold" is the string key
  at `0x4de268`.
- **Grey** (`FUN_004326b0`, 555 screen): per pixel
  v = (r + g + b)/6 + 3 on the 5-bit channels, written as (v, v, v); with
  flag 0 every pixel, index 0 included.
- **Progress wedge** (`FUN_004263b0`): per quadrant, clockwise from 12
  o'clock. Rows run outwards from the icon centre; a row r copies the
  columns [r·tan(a)>>8, r·tan(b)>>8) (clipped to half the width) from the
  centre, with 8.8 tangents at `0x6d91a8` (`FUN_0044b090`: entry i =
  ftol(tan(i·6.283185308/360)·256) for i = 0..89, entry 90 = entry 89).
  Quadrants 1 and 4 (upper half) run rows c − k, k = 0..h/2−1, with
  multiplier k; quadrants 2 and 3 run rows c + k, k = 0..h/2−2, with
  multiplier k + 1. So the centre row **is** coloured (by quadrants 2/3),
  and only the top row and the bottom rows (45, 46 of a 47-row icon) stay
  grey. (An earlier version of this note said the centre row stays grey;
  that was wrong.)
- **Payment** (`0x44687c`): inc = (float)step · (1/23040) · (float)cost,
  stored as float; due = ftol(inc + acc) − paid; on success acc += inc.
- **Production speed and cost** include the difficulty in `FUN_00442a00` /
  `FUN_00442b10`: F' = speed·F/100, step = rate·F'/100. Single player: the
  local player uses the difficulty `0x4e8bf0`, the others normal.
- **Player colour** `0x795518[p]` = the u16 at `0x786c58 + colour·0x20`
  (`FUN_00461fb0`), colour = `0x5175d8[p]`. **Verified:** `FUN_00461d10`
  reads `bin\Colours.bin` (8 × 16 × u16) to `0x786c48`, so `0x786c58` is
  **shade 8** of each ramp (e.g. red (208,72,64); shade 0 is near black).
  (The 8-bit path uses palette index colour·16 + 0x88.) Used by the radar dots and the skirmish score box. (The port
  first used shade 0, which made both very dark.)
- `FUN_0046c450(name, 1)` skips a name already in the building list, so a
  name is listed once.
