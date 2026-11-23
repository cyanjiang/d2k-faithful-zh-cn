# First-run data setup

Our own screen (the original has none). It replaces `tools/import_game.py`
(Python + 7-Zip) with native code, so a player only needs the game exe and
their own Dune 2000 media. Decided with the user on 2026-10-14:

- **Sources:** an ISO image of the CD, the CD itself (drive) or a folder
  with the CD's files, and an existing installed Dune 2000 folder. The 1.06
  patch `d2k106us.exe` is added when the source is not already 1.06.
- **Always copy** into the game's own data folder (one import, about
  600 MB). Afterwards the CD, ISO or old install is not needed.

## 1. What is needed

The target is the 1.06 US game (`docs/data-sources.md`). The manifest is
`docs/inventory-1.06.csv` (path, size, SHA-256; 1,318 files), built into
the exe as a table of hashes only, no data.

Our engine reads only `DUNE2000.DAT` from the root (exe tables) and the
trees `Data/`, `MISSIONS/`, `MOVIES/`, `MUSIC/`. Those **1,305 files** are
the required set. The original's own program files (DLLs, launcher,
readmes, `*.CFG`) are not copied.

## 2. Where each file can come from

The import is **hash-driven**: for every required manifest entry, any
source that holds a file with the same size and SHA-256 can supply it.
The path inside the source does not need to match exactly (case and
folder name differences are tolerated). That handles every mix without
special cases:

| Source | Supplies |
|---|---|
| CD / ISO: `SETUP\SETUP.Z` (InstallShield 3, `DUNE\` tree, PKWARE DCL implode, `docs/formats/installshield3.md`) | the 1.00 `Data/` tree and `DUNE2000.DAT`, where unchanged by 1.06 |
| CD / ISO: `MOVIES\`, `MUSIC\`, `MISSIONS\` | movies, music, missions |
| `d2k106us.exe` (a 25,600-byte PE stub with a ZIP appended; Store and Deflate) | the 1.06 `DUNE2000.DAT`, `DATA.R8/R16`, `BLOXXMAS.R8`, all `bin\*.BIN`, UI scripts |
| installed folder | whatever it holds that matches (a 1.06 install supplies `Data/` completely; old installs often lack `MOVIES`/`MUSIC`, which stayed on the CD) |

A modified file (a no-CD exe, a mod) simply does not match and is taken
from another source if one is added. A non-US CD has other hashes and is
reported as such.

## 3. The screen

Plain and functional, drawn before any game data exists: SDL's built-in
8×8 debug font (`SDL_RenderDebugText`) and filled rectangles in the
640×400 window, using the normal scaling. SDL's native pickers
(`SDL_ShowOpenFileDialog`, `SDL_ShowOpenFolderDialog`) choose the files.

```
 DUNE 2000  -  GAME DATA SETUP

 This game plays from the files of your own Dune 2000 (US) CD.
 Add your CD image, your CD drive (or a folder with its files)
 or your installed Dune 2000 folder, and the official 1.06 patch.

 [ Add CD image (.iso) ] [ Add CD drive or folder ] [ Add patch d2k106us.exe ]

 Sources
   d2k106us.exe (patch)                         340 files
   DUNE2000.iso (CD image DUNE2000)             965 files

 Game files  1305 / 1305   [==========================================]
 Everything is here. Press Install.

 [ Install ]                                                    [ Quit ]
```

"CD drive or folder" covers the CD drive, a folder with the CD's files
and an installed game: all three are scanned the same way.

- Each added source is scanned at once (hashing ~600 MB takes a few
  seconds; a progress bar shows it). The counts update after each scan.
- "Missing" names what is still needed in plain words: "the 1.06 patch",
  "movies and music (add the CD or ISO)", or "N other files", with the
  first few paths.
- **Install** is enabled when all 1,305 files are found. It copies or
  extracts into the data folder with a progress bar, hashing every file
  as it is written, then starts the game.
- Esc / Quit leaves without changes.
- At the start, the folder holding the game exe is checked for
  `DUNE2000.iso` / `d2k106us.exe` and they are added automatically.

## 4. Storage and start-up

- Data folder: `SDL_GetPrefPath("d2k-faithful", "d2k")` + `gamedata/1.06`
  (on Windows under `%APPDATA%`). The import writes to `gamedata/1.06.tmp`
  and renames it when every file checked out, so a cancelled or failed
  import never leaves a half folder that looks valid.
- Start-up check (fast): all required files present with the right sizes,
  and `DUNE2000.DAT`'s SHA-256 (as today). The full hash check runs during
  the import only. If the check fails, the setup screen opens and says
  what is wrong.
- `--data <dir>` keeps working (developers, our repo's `gamedata/1.06`)
  and skips the setup screen.

## 5. Code

- `formats/`: `zip` (central directory, Store / Deflate) with an
  `inflate` decoder, `dcl_explode` (PKWARE DCL), `installshield3`
  (SETUP.Z index, as `tools/is3extract.py`), `iso9660` (primary volume
  descriptor, directory records, file extents; 2048-byte sectors, plus raw
  2352-byte BIN images detected by the sync pattern).
- `platform/`: the built-in manifest (generated from
  `docs/inventory-1.06.csv` at build time), the source scanners, the
  matcher and the importer (runs on a worker thread, reports progress,
  can be cancelled).
- `app/setup_screen`: the screen above.
- Tests: SHA-256 vectors; inflate and DCL on known streams; the ISO, SETUP.Z
  and patch readers on the real files when present in the repo root;
  a full import from `DUNE2000.iso` + `d2k106us.exe` into a temporary
  folder, checked against the manifest (skips without the files).

## 5a. Status (2026-10-14)

Implemented: `formats/{inflate,zip,installshield3,iso9660}`,
`platform/{manifest,setup_sources}`, `app/setup_screen`, start-up in
`main.cpp`. Measured on the dev machine: the CD image alone supplies 1,255
files and the patch the other 50 (or, scanned patch first, 340 + 965);
scan plus install of all 600 MB about 6 s in the test, and about 13 s for
the whole first run (detect, read, install, boot to the main menu). The
exe now links the MSVC runtime statically (top-level `CMakeLists.txt`), so
it needs no Visual C++ redistributable.

## 6. Clean-machine check

Last step: a Windows Sandbox run with only the built game folder, the ISO
and the patch mapped in (read-only). It shows the first-run experience on
a machine with no data, no settings and no development runtime. Needs
the user (their machine).

**Passed (2026-10-14, the user, Windows Sandbox):** `build/d2k-first-run.wsb`
(not committed; it maps `build/msvc/bin/Release` and hard links to the ISO
and patch, read-only). The setup appeared on the clean machine; CD image +
patch gave 1305 / 1305; Install, then the first-launch intro and house
selection; a second start went straight to the title. The CD-drive path
(the ISO mounted as a drive, "Add CD drive or folder") also worked.

## 7. Out of scope (v1)

Non-US CDs and other versions; `.mdf` / `.nrg` / `.cue` images other than
plain BIN; downloading anything.
