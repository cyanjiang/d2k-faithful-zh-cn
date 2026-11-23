# Boot sequence and movie screens (1.06)

## What the original does

The boot function is `FUN_00442580` (1.06). After startup it opens the first
menu by name:

```
if (language_shown == 0)                // 0x004dfaf8, initialised to 1
    open_menu("LANGUAGE")               // never taken in 1.06 (dead code)
else if (<UI manager +0x54> == 0xE5) {  // no navigation pending
    if (play_full_intro)                // 0x004e7acc, initialised to 1
        open_menu("MVE_INTRO1"), play_full_intro = 0
    else
        open_menu("MVE_INTRO3")
}
```

- `0x004dfaf8` is only referenced inside this function, and it starts as 1,
  so the LANGUAGE screen never appears.
- UI manager `+0x54` is the pending navigation command
  (`FUN_00473840(id, arg1, arg2)` stores it). **0xE5 means "none"**: the
  navigation processor `FUN_004738b0` does nothing while it is 0xE5 and
  writes 0xE5 back after every command it runs (verified 2026-10-16). So
  the boot opens the intro only when no navigation is already pending.
- `play_full_intro` is **byte 0 of `dune2000.cfg`**. `FUN_00478c20` loads the
  cfg and `FUN_00478fa0` saves it. With no cfg file it stays 1. On load,
  values other than 0/1 are clamped to 1. When `DAT_00797df0` is set it is
  forced to 0.
- **`DAT_00797df0` is "no CD" (verified 2026-10-16):** `FUN_00470710`
  (0x470948) sets it to `!FUN_0042b7d0(1)`, the CD check (`FUN_00482690`:
  a CD-ROM drive with volume label `DUNE2000`). The movies live on the CD
  (RESOURCE.CFG's movies line), so without the CD the full intro is
  skipped and `FUN_0048d730` plays no movie. Ours always has the movies
  (the first-run setup copies them from the CD), so this mode never
  occurs.

The menus are data, in `Data\UI_DATA\*.uil` (see `docs/formats/ui.md`):

| Menu | UIL | Movie key → file | Then |
|---|---|---|---|
| MVE_INTRO1 | Intro1.uil | mve_intro1 → g_int1_e | QuitAndJumpToMenu MVE_INTRO2 |
| MVE_INTRO2 | Intro2.uil | mve_intro2 → g_int2_e | QuitAndJumpToMenu HOUSESELECTION |
| MVE_INTRO3 | Intro3.uil | mve_fast → t_titl_e | QuitAndJumpToMenu MAINMENU |

Movie keys resolve through `text.uib`. The movie player builds the path as
`<movie dir><name>.VQA` (`FUN_0048d730`, format string `%s%s.VQA`), and sets a
flag for the three `*_mntg_e` house montages.

**The montage flag (verified 2026-10-16):** `DAT_007984a0` = 2 for
`a_mntg_e` / `h_mntg_e` / `o_mntg_e`, else 0; the UI manager pass
(`FUN_004712c0`, 0x4712c6) counts it down by one per pass. While it is
non-zero, the DirectDraw set-up `FUN_004a4030` (0x4a4472, 0x4a44db) skips
clearing the primary and back surfaces to black, and the window
procedure's `WM_PAINT` (0x4a32f4) skips its black fill. So a display
re-initialisation right after a montage does not flash black. Ours never
re-initialises the display (one SDL renderer for the whole run) and never
clears to black on such an event, so there is nothing to reproduce.

**Result:** the first launch plays the full intro, then house selection.
Every later launch plays the short title, then the main menu.

## Movie placement

Every `MOVIE` block in the UIL files is `0, 0` / `640, 400`, drawn over a
black 640×400 group box. Movies sit at the top of the 640×480 screen.

## Not the game tick

`timeSetEvent(16 ms)` at `FUN_00401280` belongs to the MovieCode module
(the VQA player). It is not the simulation timer. The game tick is still to
be found (callers of `timeGetTime` / `GetTickCount`).

## Our implementation

`src/app/main.cpp` keeps its own `settings.ini` (`play_full_intro`) under the
user pref path. We never write the original cfg. It follows the UIL movie
blocks and resolves movie names via `text.uib`. `--reset-intro` plays the
full intro again.

Open: which inputs skip a movie, and whether a skip still follows the jump
action. The menu name → UIL mapping is hardcoded for the three intro menus
until `menus.uib` is decoded.
