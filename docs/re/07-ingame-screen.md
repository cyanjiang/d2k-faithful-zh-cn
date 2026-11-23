# In-game screen layout (1.06)

## Screen and viewport

The screen is 640×400 (`0x4eb028` / `0x4eb02c`). A static initializer at
`0x45fe80` computes the playfield from data-section values:

| Global | Value | Meaning |
|---|---:|---|
| `0x4e41dc` | 20 | top bar height |
| `0x4e41e0` | 160 | sidebar width |
| `0x786d48` | 640 − 160 = **480** | playfield width |
| `0x6dc538` | 400 − 20 = **380** | playfield height |

The map draw (`FUN_00423d20`) puts the playfield at (0, 20), 480×380:
15 × 11.875 cells of 32 px. **This is the visible map area. Our display
options must never enlarge it** (CLAUDE.md §10).

The text-layout loader `FUN_00461300` (`ui_<lang>.dat`) can override these
values. It runs only when the debug flag `0x4eb014` is clear; retail loads
the binary resources below instead (`FUN_00460ec0`).

## Resources

- **`Data\UIBB.R16`** (`uibb.r16`): a raw 640×400 16-bit (555) image with no
  header. It is the whole in-game backdrop: top bar with OPTIONS, the
  sidebar frame with radar box, button row and build-icon columns, and
  artwork under the playfield (which the map covers).
- **`Data\UI_<lang>.R16`** (`ui_%s.r16`, language from text key
  `language`): 15 controls of `{i32 x, i32 y, 3 R8 entries}`, then 10 R8
  entries. The images are mostly 8-bit with palettes; the third image of
  each control is 16-bit. The controls, slots per `FUN_00460ec0`, are:

| # | Pos | Size | Looks like |
|---:|---|---|---|
| 0–3 | (502,374), (531,373), (566,374), (595,373) | 28×24 | bottom sidebar buttons (TODO identify) |
| 4–8 | (493..599, 138) | 32×32 (6: 24×24) | sidebar row: repair, sell, (centre), guard, … |
| 9, 10 | (499,167), (563,167) | 62×16 | build tabs ("MAIN") |
| 11 | (0,0) | 62×20 | OPTIONS |
| 12, 13 | (499,167), (563,167) | 62×16 | second set of tabs (TODO) |
| 14 | (501,374) | 122×26 | PURCHASE (Starport) |

The extra entries are four 10×200 (scroll bars?), 133×31, 67×31 and four
128×128 (radar frames?). TODO identify.

The R8 entry reader `FUN_00460550` converts 555 pixels to 565 on a 565
screen, which confirms that R16 data is 555. A type-2 entry takes the most
recent type-1 entry's palette.
