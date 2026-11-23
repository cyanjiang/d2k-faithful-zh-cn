# Reference setup: the original game, for side-by-side checks

The reference install is local only (`/reference/` is gitignored) and is
never shipped.

- **Install:** `reference/d2k106/` is a copy of `gamedata/1.06`, the 1.06
  install made by `tools/import_game.py` from the user's ISO and patch.
- **Wrapper:** `reference/d2k106/ddraw.dll` and `ddraw.ini` are cnc-ddraw
  v7.1.0.0 (GitHub FunkyFr3sh/cnc-ddraw, `cnc-ddraw.zip`, SHA-256
  `0b13ab89a64c9918189b1dadd449ef6ed3cb3b7b19cabd96d8adbd95505bb908`).
  We changed `windowed=true` only. `maxgameticks=0` (the default) emulates
  a 60 Hz vertical blank.
- **Registry:** none needed. The game reads the registry only for Westwood
  Online (`FUN_00419df0`).
- **CD check:** mount `DUNE2000.iso` before starting (it mounts as volume
  `DUNE2000`), and unmount it afterwards:

      Mount-DiskImage -ImagePath "<repo>\DUNE2000.iso"
      Dismount-DiskImage -ImagePath "<repo>\DUNE2000.iso"

- **RESOURCE.CFG** (read by `FUN_0042b8d0`) has five lines: res path, movies,
  music, missions, maps. **The first two characters of the movies line are the
  drive the CD check tests** (`FUN_00482690`: drive type CD-ROM and volume
  label = text `Dune2000CDLabel` = `DUNE2000`). The patch's default
  `data\movies\` makes the check fail, and New Game then says "This is a
  spawned game…" (text `CDRequired`). The reference uses:

      data\
      F:\MOVIES\      <- the mounted ISO's drive
      MUSIC\
      MISSIONS\
      data\maps\

  With no `dune2000.cfg` (first launch) the game also waits about 10 s at
  start-up, then polls the CD for up to 10 s more (UI manager constructor).
- **Start:** run `DUNE2000.DAT` directly, with `reference/d2k106` as the
  working directory. Windows will not shell-execute a `.DAT`, so use
  CreateProcess, e.g. .NET `Process.Start` with `UseShellExecute = false`.
  `DUNE2000.EXE` is the patch launcher (`LAUNCHER.CFG`: `RUN = . Dune2000.dat`).

## Screenshot comparisons (2026-10-16)

For "does ours look like the original" questions, compare the same scene
at the true 640×400:

- **Original:** set `width=640` / `height=400` in the `[ddraw]` section of
  `reference/d2k106/ddraw.ini` (cnc-ddraw saves the last window size there
  and stretches the picture to it), mount the ISO, start `DUNE2000.DAT`,
  and capture the window's client area (exactly 640×400). Without a
  `dune2000.cfg` the first start waits about 10 s and plays the full
  intro; Esc skips each movie.
- **Ours:** start with `--aspect square --grab 0` (the window is an exact
  2× = 1280×800), capture the client area and take every second pixel.
  Both options are saved in settings.ini: put `aspect` and `mouse_grab`
  back afterwards.
- **Compare** per pixel with a small threshold; animated backgrounds (the
  main menu's eyes and title glow, the missiles) differ by timing, so judge
  the element in question by its rectangle. Example: MAINMENU's button box
  (240,165 160×150) matched with 0 differing pixels.
- Capture only the game window's client area, never the desktop.

## Measurement notes

The original paces its main loop only with `Flip(DDFLIP_WAIT)`, so on 1998
hardware the loop ran at the monitor's refresh rate. Anything per-iteration
(TWEEN steps, globe reveal, scrolling) scaled with it. **Game ticks do not:**
a 1-second timer re-measures the frame rate and rebuilds the tick pattern, so
the speed slider is ticks per real second (docs/re/02).

`tools/measure-reference.ps1` reads the tick counter (`0x5173f4`), the pattern
slot (`0x6b9868`) and the speed (`0x4e3b18`) of the running `DUNE2000.DAT`
for 10 s and prints ticks/s and loop passes/s (read-only). Run it while a
mission is running and unpaused. 2026-10-06: speed 75 gave 19.98 ticks/s at
about 61-70 passes/s.

**Decision:** we run the loop at 60 Hz by choice (see CLAUDE.md section 10).

## GhidraMCP server (optional, faster RE queries)

[bethington/ghidra-mcp](https://github.com/bethington/ghidra-mcp) (Apache-2.0),
reviewed and built from source at commit `4c5e9429eddc` (2026-10-06). It is a dev
tool only; nothing of it ships with d2k.

- **Review notes.** No bundled binaries apart from the standard Gradle wrapper
  jar and test fixtures. No telemetry or update checks found. The server listens on a
  Unix socket by default, refuses non-loopback TCP without a token, and keeps
  script execution off unless `GHIDRA_MCP_ALLOW_SCRIPTS` is set (we never set
  it). The setup script only checks network reachability; we did not use it.
- **Install (this machine):** `~/tools/ghidra-mcp`: the built
  `GhidraMCP-7.0.0.jar`, `classpath.txt` (Ghidra 12.1.4 jars), a Python venv
  with the bridge, `token.txt` (random bearer token, not in the repo) and
  `launch_d2k_mcp.py`.
- **Project:** a copy of `re/ghidra-project` in
  `~/tools/d2k-ghidra-mcp-project`, so it never fights `re/query.sh` over
  the project lock. Re-copy it after re-analysing.
- **Running:** `launch_d2k_mcp.py` starts the headless server on
  127.0.0.1:8089 (token required) if it is not running, then runs the stdio
  bridge. Claude Code has it registered for this project only
  (`claude mcp add --scope local d2k-ghidra`). Python on Windows has no Unix
  sockets, so the bridge uses loopback TCP.
- **From scripts:** `python re/ghidra_http.py decompile <addr>` /
  `xrefs <addr>` / `get <path>` (needs the server running).
- **Stopping it:** the server is a detached Java process (heap capped at
  4 GB) that keeps running after Claude Code closes. Double-click
  `~/tools/ghidra-mcp/stop-ghidra-mcp.bat` to stop it; it stops only the
  GhidraMCP Java process and starts again when next needed.
