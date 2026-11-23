# Initial survey of DUNE2000.DAT (1.06 and 1.00)

Reproduce with:
`re/analyze.sh 1.06 gamedata/1.06/DUNE2000.DAT` and
`re/analyze.sh 1.00 gamedata/1.00-exe/DUNE2000.DAT`.
Output goes to `re/exports/<ver>/` (gitignored). Ghidra 12.1.4 headless, default analysis.

## Binary shape (1.06)

| Section | Start | Size | Notes |
|---|---|---:|---|
| .text  | 0x00401000 | 756 KB | code |
| .rdata | 0x004be000 | 105 KB | |
| .data  | 0x004d9000 | 4.0 MB (virtual) | mostly BSS; holds the large global game state |
| .idata / .rsrc | 0x008cf000 / 0x008d1000 | | |

- Image base 0x400000. 32-bit x86 PE, built with MSVC (MSVC CRT `R6002`/`R6008` messages).
  No debug symbols.
- Ghidra finds 1,829 functions in 1.06 and 1,854 in 1.00. The 1.06 exe is 24 KB
  larger, yet has 25 fewer functions, so the patch is a real rebuild. That
  means we can't assume addresses match between the two versions.

## Imports and what they suggest

- `DDRAW` DirectDrawCreate: 8-bit/16-bit DirectDraw rendering (R8/R16 data sets).
- `DSOUND` + `MSS32` (Miles Sound System, 22 imports): audio goes through Miles.
- `WINMM` timeSetEvent / timeBeginPeriod / timeGetTime: **multimedia timer**.
  **Answered (docs/re/01, 02):** the only `timeSetEvent` (16 ms,
  `FUN_00401280`) belongs to the movie player; the game tick comes from a
  1-second `WM_TIMER` that rebuilds the tick pattern per loop pass.
- `DPLAYX`, `WSOCK32`: multiplayer (out of scope for v1).

## Source module names (from assert/log strings)

Build path: `C:\MsDev\Projects\July2000\code\` and `...\MovieCode\`.
Modules: AHANDLE, AI, DDSTUFF, DISPLAY, GRAPHLIB, IMAGE, LOADSAVE, MAIN, MAP,
MEMORY, MISSION, MODEL, MOVIES, PARTICLE, SETUP, SIDE, UIMANAGER, UNIT,
UNVQTBLC, VQA, WINMAIN, WND_FILE.

These strings let us tag each function with the module it came from: find
where a function references `"...\\UNIT.CPP"` and label it `unit::`. This will
be our first Ghidra labelling pass.

## Global variable names as strings

Strings such as `gCDVolume`, `gCallerIDOn`, and `gNetCrates` look like a
name-to-address table for globals, probably used for settings, save games,
or `bin\VARS.BIN`. **Answered:** it is the UI globals table (name, address,
type) that the menu scripts use (`re/dump_ui_tables.py`, docs/re/03 and 20).

## Data files the exe references

`%s.r8/%s.r16`, `data.r8/r16`, `mouse.r8/r16`, `uibb.r8/r16`, `ui_%s.r8/r16`,
`bin\%s.bin` (`Palette.bin`, `Colours.bin`, `Templates.bin`, `circles.bin`),
`menus.uib`, `campaign.uib`, `text.uib`, `samples.uib`, `colours.uib`,
`ui_data\%s.uil`, `ui_gfx\%s.bmp|tga`, `sound.rs`, `Palette.res`,
`%c%dV%d.mis` (missions such as `A1V1.MIS`), `%s%s.VQA`, `dune2000.cfg`,
`resource.cfg`, and the music tracks by name.
