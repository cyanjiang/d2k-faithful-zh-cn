# Campaign table (`Data\UI_DATA\campaign.uib`)

Code: `src/campaign/campaign_table.cpp`. RE: `docs/re/05-campaign-map.md`.

A raw dump of the exe's table at `0x795708`: 3 houses (Atreides, Harkonnen,
Ordos = `gSideId` 0..2) × 10 records × 0x104 bytes = 7,800 bytes. The exe
can instead build the table from a text `Campaign.txt` (`FUN_00477790`), and
that loader names the columns. Record 0 is the start of the campaign;
records 1–9 are missions.

| Off | Type | Column | Meaning |
|---:|---|---:|---|
| 0x00 | u8 ×2 | 0, 1 | selectable territories (1–27, 0 = none) |
| 0x02 | char[5] ×2 | – | mission names, built as `a%dv1` / `a%dv2` (`h…`, `o…`), or empty |
| 0x0C | i32 ×2 | 2, 5 | arrow direction, North..NorthWest = 0..7 (unused by the 1.06 globe code) |
| 0x14 | i32 ×4 | 3, 4, 6, 7 | arrow positions x1, y1, x2, y2 (0,0 = territory centre) |
| 0x24 | u8 ×3 ×3 | 8–16 | territories Atreides / Harkonnen / Ordos hold after this record |
| 0x30 | f32 | 17 | column 17 × 0.01 (1.0 … 5.0, `FUN_00477790`). The campaign score multiplier: read only by `FUN_0043f030` (`0x43f10d`, `fld [0x795634 + (mission + house·10)·0x104]` = record mission − 1, the mission just won; docs/re/24 §2.3) |
| 0x34 | i32 ×3 | 18–20 | houses, in the order their new territories are revealed |
| 0x40 | char[60] ×3 | 21–23 | samples.uib speech for each of those houses, or `NONE` |
| 0xF4 | char[12] | 25 | the music track that starts the mission (e.g. `atregain`), docs/re/15-ingame-music.md |
| 0x100 | i32 | 24 | the mission's enemy house: read only by `FUN_0043d820` (`0x43d88a`, record mission − 1), which picks the score's "their" bar images (docs/re/24 §2.2) |

A scan of every access into `0x795708 … 0x797580` finds readers for
+0x00/+0x01 (choices), +0x14…+0x2C (arrows, held territories), +0x34…+0x3C
(reveal order) and the speech / music fields (`FUN_0042bc60`,
`FUN_0042c400`, `FUN_0042cf10`, 0x448c98), and none for +0x0C, +0x30 or
+0x100.

Mission names map to `MAPS\<name>.map` / `.mis`; the briefing movie is
`MVE_<HOUSE><n>` (`FUN_004752c0`).

## Territory shapes (exe only)

The 27 territories' shapes are not in any data file. They are a table in
the exe at `0x4be3f4` (1.06). Each 0x48-byte record holds:
- `i32 x, y`: where `UI_GFX\Regions\{a,h,o}<n>.bmp` is drawn;
- `u32 count`: the point count, 4–7;
- `POINT points[7]`: the click polygon.

`src/campaign/regions.cpp` reads the table from the player's own
`DUNE2000.DAT` (`formats::PeImage`), so nothing is copied into the repo.
