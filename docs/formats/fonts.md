# Fonts, colours and UI graphics

Code: `src/formats/font.cpp`, `src/formats/tga.cpp`, `src/ui/surface.cpp`.

## FNT (`Data\FONT.FNT`, `Data\FONTCOL.FNT`)

The loader `FUN_0042d6f0` (retail branch) reads the first 0x408 bytes
straight into the font slot `0x513b20 + font × 0x408`, then 256 glyph
records.

| Off | Size | Field |
|---:|---|---|
| 0 | 1 | loaded flag (1) |
| 1 | 1 | space width (5 in both fonts) |
| 2 | 1 | first character (`0x21`, `!`) |
| 3 | 1 | spacing added after each glyph (FONT 1, FONTCOL 0) |
| 4 | u32 | glyph height (FONTCOL 17, FONT 14) |
| 8 | 256 × u32 | runtime pointers left by the tool (overwritten) |
| 1032 | 256 records | u32 width, u32 height, width·height index bytes; (0, 0) = empty slot |

Every record of both files is read; the files end exactly after the 256th.
Each glyph becomes an 8-bit image with offsets 0 (`FUN_0042ef90`), so a
glyph is drawn exactly at the pen position.

**Font slots.** Font 0 is `font` = FONT.FNT, loaded by the UI manager
(`FUN_00470710`). Font 1 is `FontCol` = FONTCOL.FNT (slot `0x5162b0` = 1,
`FUN_00448550`), with two colour handles from FONTCOL.FPL (`0x51676c`,
`0x5175cc`).

FONT.FNT is 1-bit (indices 0/1). FONTCOL.FNT uses indices 1–14: its
stroke pixels are the FONT.FNT shapes (148 of 157 glyphs identical) plus a
dark edge (index 1).

**Character map** (`bin\FONT.BIN`, 256 bytes, `FUN_0042e450`): every text
routine looks a character up in this table first. A result of `0x20`
advances by the space width; anything else is the glyph slot
`map[c] − first character`. 1.06 maps `0x20…0x7F` to themselves, the
accented Latin-1 letters to glyphs `0x80…0xBB`, and everything else, such
as `0x92` (one credits name) and `0xAC` (`¬`, the line break mark), to a
space. The text width (`FUN_0042dfb0`) uses the same map.

## Drawing text

| Routine | Use | Pixels |
|---|---|---|
| `FUN_0042dc00` → `FUN_00431e40` | every UI element (font 0) | each nonzero glyph pixel in one solid colour |
| `FUN_0042dd90` → `FUN_00431f50` | in-game messages, timer, sidebar labels (font 1) | `handle[index]` from a 16-entry colour handle; index 0 keeps the screen pixel |
| `FUN_0042de70` → `FUN_00432060` | translucent text | per 5-bit field `(dst·(32 − a) + col·a) >> 5` |

All three skip a glyph that does not fit whole (`x + w ≥ width` or
`y + h > height`).

- **UI elements use font 0 only.** The element font byte `+0x775` is set
  only by `FUN_00482830`, to 0 (a byte-write scan of `.text` finds no other
  writer). The same function sets the list row height `+0x779` to font 0's
  slot 0 glyph height, 14.
- **Vertical position:** `top + (h − 14) / 2` (the slot 0 glyph height):
  buttons (`FUN_00485610`, one pixel lower while pressed when the button has
  commands and its two image names differ), boxes with text
  (`FUN_0042d5d0`), text boxes and edit boxes. List rows draw at the row
  top. An earlier version drew FONTCOL.FNT here with a fitted "+2"; that
  only approximated FONT.FNT's (h − 14) / 2 with FONTCOL's 17-pixel height.
- **Text boxes** (`FUN_0048bb10`): left text at x + 5, right text at
  right − width − 5, centred text at x + (w − width) / 2.
- **Colour handles** (`FUN_0042e1c0`, 16-bit): the first 16 FONTCOL.FPL
  entries (B, G, R, A bytes) through `FUN_0042f3d0` (each field truncated).
  In 1.06 entry 1 is black and the others red. For the sidebar labels
  `FUN_00426e80` overwrites entries 2–15 of handle `0x5175cc` with the
  pulse colour; the black edge stays.

## colours.uib

u32 count (51), then counted name + 4 bytes **B, G, R, 0**. Examples:
`ButtonUpColour` (181,132,107), `LightRed` (141,41,8). Entry 0 is
`Transparent` = (0, 0, 0). Names resolve to an index with a case-sensitive
compare (`FUN_0048db90`), and an unknown name gives index 0 too: MAINMENU's
version box asks for `grey` (the table has `Grey`). Only the text box tests
for index 0 (keep the grabbed background, `FUN_0048bb10`); group boxes and
buttons without images fill with its value, black.

## UI graphics (`Data\UI_GFX`)

156 TGAs (uncompressed type 2, 16-bit, bottom-up, xRGB1555, alpha bit never
set) and 275 BMPs (8 bpp, uncompressed, bottom-up). Animations are
horizontal film strips (`LOGOANIM.TGA` 15840×100 = 72 frames of 220×100).

**Loading (`FUN_00491330`).** In 16-bit mode **and while gGameState
(`0x4dfb08`) is not 2** the exe tries `UI_gfx\<name>.tga`, then
`UI_gfx\<name>.bmp`, then `UI_gfx\error.bmp`. With gGameState 2
(GameStateMainLoop, i.e. every in-game menu: INGAME, LOAD / SAVE in a
mission, OPTIONS, the RECAP pages, ...) it skips the TGA and loads
`UI_gfx\<name>.bmp` directly (no error.bmp fallback). The image is loaded
when the element is created, so the state at menu open counts. 36 images
exist in both forms (`but1_up` .. `but4_dn`, `box1`, `Loadgame`,
`RBut4_up/dn`, `Uparrow`, `Dnarrow`, ...): in a mission the 8-bit version
is drawn, whose palette colours differ from the TGA by one 5-bit step in
places. Found by comparing INGAME with a capture of the original
(2026-10-16): its buttons show `BUT1_UP.BMP`'s colours (e.g. (10, 4, 0)
where the TGA has (11, 4, 0)), while the front-end LOAD page shows
`LOADGAME.TGA`. Every image named by a page that can open in a mission has
a BMP in 1.06 (a scan of the UIL files), so the missing-BMP case does not
arise. Ours: `ui::Assets::set_main_loop`, set per menu by the menu runner
when it opens a menu.
241 images exist **only** as BMP (e.g. `box4`, `but8_up`, `tick`), so
the 16-bit game draws many 8-bit images. In those, palette entry 0 is
magenta and entry 1 is black. A name equal to `NONE` (`__strcmpi`, so any
case) means no image. A BMP's palette is converted with `FUN_0042f3d0`
(each field truncated, `FUN_0042f430`); the 8-bit remap to the game
palette (`FUN_00431d50`) runs only in 8-bit mode.

**Blitting (`FUN_00430770` → `FUN_0042fac0`).** Each draw passes two flags:

| Flags | 8-bit image | 16-bit image |
|---|---|---|
| (0,0) | opaque palette lookup | opaque copy |
| (1,0) | index 0 transparent | `0xF81F` transparent |
| (1,1) | index 0 transparent, index 1 = shadow `(dst & mask) >> 1` | `0xF81F` transparent, `0x001F` = shadow |

The 16-bit keys are compared in the screen format, where `0xF81F` is
565 magenta; the half mask is `0xF7DE` on a 565 screen and `0xFBDE` on a
555 one (`0x4a4408`). **No 1.06 UI TGA contains magenta or pure blue, so
TGAs are effectively opaque** on either screen. Flags used by each element:
- **(0,0):** background (`FUN_0043c4b0`), checkbox and multistate images
  (`FUN_0048adc0`, `FUN_0048b0e0`).
- **(1,1):** buttons (`FUN_00485610`), group boxes (`FUN_004863a0`),
  animation frames (`FUN_0048d650`), toggle buttons, list boxes and value
  bars.

A group box with an image takes the image's size.

**Boxes without an image** (`FUN_0042d660`): fill, then the left column
(top-left colour), bottom row and right column (bottom-right colour), and
the top row (top-left colour) last, so the top-right corner pixel has the
top-left colour. Group boxes use the border colour on all four sides;
buttons the highlight / lowlight colours (swapped while pressed); filled
text boxes field 4 / field 3. Checked against the original: PLAY_ATREIDES's Yes / No
buttons (all four colours `Transparent`, images `NONE`) black out AACCT's faint
stars exactly inside 239..283 × 340..359 and leave them one pixel outside
(capture 2026-10-16; test "PLAY_ATREIDES Yes / No").

Menus use the same 640×400 top area as movies; e.g. `Title.tga` is 640×400.
