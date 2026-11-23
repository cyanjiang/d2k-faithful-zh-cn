# Project: Faithful Dune 2000 Reimplementation (working name: `d2k-faithful`)

> Handoff brief from a planning chat. Put this file in the repo root as `CLAUDE.md`
> so Claude Code loads it automatically. Update it as decisions get made.

## 1. Mission

Recreate **Dune 2000 (1998, Intelligent Games / Westwood)** as a modern, native, open-source
engine that loads the player's **original game data files** and reproduces the original
experience **exactly**: boot → intro movie → original menus → full campaign flow with
briefings and videos → missions → debriefs, for all three houses.

The target feeling: *"I double-clicked it and it was my childhood."* No installer, no
patch stacks, no registry hacks, no surprises.

## 2. Why existing projects don't satisfy this

1. **Balance drift**: some projects retune units/economy for multiplayer. We do not.
2. **Incomplete flow**: intro, menus, briefings, video playback, and campaign
   transitions are approximated or missing; videos break or skew resolution.
3. **Untrustworthy setup**: opaque installers and patch systems.

## 3. Non-negotiable principles

- **The original game is the oracle.** Every number, timing, menu, transition, and AI
  behavior is derived from the original exe/data and verified against it. When in doubt,
  decompile and check; never "guess what feels right."
- **Vanilla first.** Bug fixes that change gameplay go behind an explicit, off-by-default
  option (e.g. `fixes.ini`). Pure crash/compat fixes are fine by default.
- **No original assets or code in the repo.** Players supply their own install/CD data.
  We ship only original code + tools. Do not paste large decompiled listings verbatim
  into the source; write clean reimplementations and document findings in `docs/`.
- **Zero-friction runtime.** Single executable + a folder pointer to the original data.
  A first-run screen that says "point me at your Dune 2000 folder or CD/ISO" and
  validates file hashes. Nothing else.
- **Readable and trustworthy.** No telemetry, no auto-updaters, no binary blobs.

## 4. Scope

**v1 (in scope):** single-player campaign (Atreides, Harkonnen, Ordos), skirmish vs AI,
original menus/UI, intro/briefing/ending videos, music & SFX, save/load, original
resolution plus clean integer/aspect-correct scaling to modern displays, Windows + Linux.

**Later:** multiplayer (lockstep, vanilla rules), macOS, widescreen as opt-in,
map editor support, Emperor: Battle for Dune (separate project, different approach).

## 5. Suggested tech (Claude Code may revise with justification)

- C++20, CMake, SDL3 (or SDL2 if a dependency forces it).
- Deterministic simulation matching the original's arithmetic (needed for faithful
  behavior and future lockstep multiplayer). **Update 2026-10-03:** the original uses
  float32 (e.g. `bin\SPEED.BIN`), so we reproduce its float operations rather than
  using fixed point. See `docs/re/02-timing-and-arithmetic.md`.
- Separate layers: `formats/` (file parsers), `sim/` (deterministic game logic, no
  rendering), `render/`, `audio/`, `video/`, `ui/`, `campaign/`, `platform/`.
- Reverse engineering: Ghidra (headless mode is scriptable from the terminal) on the
  user's own `DUNE2000.EXE` (exact name/version TO VERIFY). Keep a symbol/struct database
  under `re/` (exported Ghidra scripts, notes, recovered structs) so findings are
  reproducible.
- Reference runtime: the original game running under Wine or a Windows VM, used for
  side-by-side comparison.

## 6. Data formats to identify and document

These are believed to be used by Dune 2000 but **must be verified against the actual
install** before writing parsers. Document each in `docs/formats/<name>.md`.

- Video: Westwood **VQA** (intro, briefings, endings).
- Audio: Westwood **AUD** and/or WAV for SFX, voices, music.
- Graphics: **R8/R16** sprite/image files, palettes, tilesets.
- Maps & missions: **.MAP** terrain + **.MIS** mission scripts/triggers/events.
- Rules/stats: unit, building, weapon, and AI data (binary tables and/or text; TO VERIFY).
- UI/menu layout, fonts, string tables, campaign progression/map-select data.
- Any container/archive formats on the CD vs installed folder.

Existing community knowledge (OpenRA's d2k mod, Dune 2000 modding forums and wikis)
is useful for *understanding* formats. Respect licenses: do not copy GPL code into this
project unless we deliberately choose a GPL-compatible license. Decide the project
license early (Milestone 0).

## 7. Milestones

**M0: Setup and inventory**
- Repo skeleton, CMake, CI, license decision, coding conventions.
- Script to inventory the user's data folder: list every file, size, hash, guessed type.
- Identify exact game version(s) the user owns; pin the target exe by hash.

**M1: Format toolkit (command-line tools)**
- Parsers + extractors for every format above; round-trip tests where possible.
- `d2k-tool view` / `extract` / `info` subcommands. Golden tests against the user's files
  (tests skip gracefully if data isn't present; never commit data).

**M2: Boot-to-menu**
- Window, aspect-correct scaling, VQA playback with correct aspect/letterboxing,
  intro sequence, original main menu with real assets, music. Compare screenshots
  to the original.

**M3: Map rendering and simulation core**
- Load and render a mission map, units, buildings. Deterministic tick loop matching the
  original tick rate (TO VERIFY from exe).

**M4: Gameplay parity**
- Harvesting/spice economy, construction, power, movement/pathfinding, combat,
  sandworms, spice blooms, reinforcements, Starport, superweapons, house differences.
  Each system derived from decompiled logic with documented sources (function addresses).

**M5: Mission scripting and campaign flow**
- Triggers/events, win/lose conditions, briefings, map select screen, debriefs,
  full campaign playthrough for all three houses.

**M6: AI parity, save/load, polish**
- Original AI behavior, save/load, settings, edge-case fixes, packaging.

## 8. Verification strategy

- **Format tests:** parse every original file without errors; byte-accurate re-encode
  where feasible.
- **Visual parity:** screenshot comparison harness (original under Wine vs ours) for
  menus and fixed scenes.
- **Simulation parity:** where possible, record inputs in the original and replay in ours;
  compare unit positions/credits/HP at checkpoints. Instrument the original via a debugger
  or a small hook DLL if needed to dump state.
- **Campaign smoke test:** scripted run through every mission start/briefing/transition.
- Track parity in `docs/parity.md`: system → status → evidence.

## 9. Working agreements for Claude Code

- Before implementing a game system, write a short findings note (what the original does,
  with exe addresses/evidence) in `docs/re/`, then implement.
- Flag uncertainty explicitly. Mark guesses with `// TODO(verify):`.
- Small, reviewable commits. Keep `docs/parity.md` and this file current.
- Ask the user before anything that needs their machine (running the original game,
  providing files, capturing reference footage).

## 10. Decisions (answered 2026-10-03)

- **Target version:** Dune 2000 **1.06 US**. Source data: `DUNE2000.iso` (original CD) +
  `d2k106us.exe` (official 1.06 patch). Both live in the repo root and are gitignored.
- **Dev OS:** Windows 11 (Linux later). Reference runtime: original game on Windows
  (DirectDraw wrapper if needed — reference setup only, never shipped).
- **License:** GPL-3.0 (non-commercial, private sharing; lets us study OpenRA d2k).
- **QoL policy:** nothing that changes gameplay. UI/scaling/resolution options are OK
  but must **never reveal more map area** than the original viewport — visible area
  is treated as a gameplay property.
- **Autonomy:** Claude Code may install tooling and set up the environment as needed.
- **Graphics set:** render from the 16-bit (R16) data.
- **Float arithmetic = modern-hardware compat (on by default):** keep the original's
  formulas, constants, operation order and float→int truncation exactly, but compute
  with standard IEEE float32 (SSE2, strict FP, no FMA). Do **not** emulate 1998 x87
  precision quirks. Goal: identical results on every modern PC (needed for lockstep
  multiplayer); accepted cost: rare last-bit rounding differences vs the original.
  Same category: a fixed real-time loop rate instead of the original's refresh-bound
  pacing. **Loop rate = 60 Hz (decided 2026-10-03).** The original has no timer of its
  own (it waits on the monitor refresh via DDFLIP_WAIT), so its speed varied with the
  monitor. The reference install's cnc-ddraw also imposes 60 Hz, so it cannot measure
  this. Use the reference game for layouts, click behaviour, sounds, and what appears
  on screen, not for per-loop timing.
  **No loop-rate setting (decided 2026-10-03):** the game pace stays fixed at
  60 Hz for everyone, because a per-player rate would break future lockstep
  multiplayer. **Correction (measured 2026-10-06):** the game speed does not
  depend on the loop rate. A 1-second timer re-measures frames per second
  and rebuilds the tick pattern, so the speed buckets are ticks per real
  second (default 75 = 20 ticks/s, measured 19.98 in the original). We do
  the same; see `docs/re/02-timing-and-arithmetic.md`.

- **Screen and aspect:** the original screen is 640x400 (exe globals). Two
  display modes exist, `aspect=crt` (stretched to 4:3, as on a 1998
  monitor, sharp-bilinear) and `aspect=square` (integer scaling). Set with
  `--aspect` or in settings.ini. The user will compare them and pick the
  default; `crt` is the default for now.
- **Menu music restart** after closing pop-ups is kept, because the exe
  does it. The user agreed: "if the original code says that, then its that".
- **Multiplayer (decided 2026-10-14):** the game rules, lockstep, order
  delay and sync check follow the original exactly (`docs/re/57-multiplayer.md`).
  The original's transport (DirectPlay IPX / TCP / modem / serial and the
  Westwood internet lobby) is replaced: one player hosts, the others join
  by IP address or from a list of games found on the LAN. No server.
  Only d2k-faithful plays against d2k-faithful; no wire compatibility with
  the original game. Traffic goes through the host (star); **when the host
  leaves or drops, the lowest remaining human becomes the new host** as in
  the original (decided 2026-10-15); if nobody can reach the new host
  (internet without an open port), the game ends for those players.
  **Front end (decided 2026-10-15, docs/re/58):** one added address box
  on IPXLOBBY for join by IP (the host's game then shows in the normal
  game list); the Internet / Modem / Serial buttons are removed from
  MULTIPLAYERTYPE (as the exe does for unavailable services); a LAN
  presence announcement makes IPXLOBBY's player list and lobby chat work
  between PCs on the same network as in the original. Name, house and
  colour stay on IPXLOBBY, fixed after joining, as the original (asked
  after the first playtest, 2026-10-16: "Keep the original").
- **Repository:** GitHub repo `Kyubey64/d2k-faithful` (created
  2026-10-14, public since 2026-10-17 with release 1.0.0, the
  single-player release; multiplayer included but not yet tested between
  PCs). CI in `.github/workflows/build.yml`. Pushing to it is allowed.
  Commits use the GitHub noreply address, never a personal email.

### Original open questions (kept for history)

1. Which version/language of Dune 2000 do you own (CD, patch 1.06?, other)? Hashes of the exe.
2. Primary dev OS? (Affects Wine vs VM for the reference setup.)
3. License preference (GPL-3.0 for compatibility with community tools, or MIT/BSD clean-room)?
4. Any must-have quality-of-life options (e.g. higher-res as opt-in), kept off by default?

## 11. First session plan

1. Read this file; ask the open questions.
2. Create the repo skeleton (M0).
3. Write the data inventory script and run it on the user's Dune 2000 folder.
4. Set up Ghidra headless analysis of the exe; export function list and strings.
5. Propose the order of format parsers based on what the inventory shows.
