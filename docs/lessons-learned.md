# Lessons learned (for the next faithful-reimplementation project)

Started 2026-10-06, during the Dune 2000 work. Goal: at the end of this
project, condense this into a starter kit for the next one (Civilization II:
Test of Time). Add entries as they happen; keep each one short: what
happened, what we learned, what to do next time.

## 1. Process

- **Write the findings note before the code.** Every system got a
  `docs/re/NN-*.md` with exe addresses first. When something was wrong
  later, the note made the mistake easy to find and to correct in place
  (we record corrections, e.g. doc 10 §8, doc 13 §1.3).
- **Mark every guess `TODO(verify)`.** Most later bugs were in places that
  carried one, so the markers double as a bug-hunting list.
- **Parallel research, serial implementation.** Read-only research helpers
  (one per topic, each writing its own doc) worked well and in parallel.
  Implementation stayed in one thread so edits never collided. Give each
  helper a clear scope and the docs to read first, and ask for a short
  summary plus a checklist mapped to our files.
- **Small commits, both build configs.** The user ran whichever build was
  newest; a stale Debug build once looked like a broken feature
  ("can not select anything"). Always rebuild every configuration.
- **The user's reports are the best test.** Short play sessions against the
  original found what the decompile reading missed (cursor rules, stale
  map flags, crush animation, dialogs left on screen). Ask for one concrete
  repro step (who, where, from which side) rather than guessing.
- **Testing aids that leave at release: one tag for all of them.** The
  `--dev` keys were tagged `DEV-TOOLS`, but the screenshot / script
  environment variables and start-up shortcuts grew later as untagged
  "dev aid" comments in five files; removing them took a full grep of
  `D2K_` and `getenv`. Tag each aid the moment it is added, keep its list
  in one doc, and tag the last commit that has them (here
  `dev-tools-last`) before removing. A shortcut can also fake a bug: the
  front-end `--menu OPTIONS` showed sliders at 0 only because it skipped
  the in-game set-up that a player always goes through.

- **Re-read the option the user picked before writing the design.** The
  user chose "host + join by IP; only the host forwards a port", but the
  first multiplayer design copied the original's mesh, where every
  player needs an open port. A side note caught it before any network
  code existed. Quote the chosen option in the design doc.
- **Test a network game in one process first.** Several Worlds plus the
  lockstep, joined by a fake link that delays, reorders and drops
  messages, found a real deadlock the decompile reading had missed (a
  dropped player kept the others stalled until a "left at once" flag,
  doc 57 §1.7). No sockets, no second PC, and it runs in the normal test
  suite.

## 2. Reverse engineering

- **Headless Ghidra plus an HTTP/MCP bridge** (GhidraMCP, a small
  `re/ghidra_http.py` client) made `decompile <addr>` / `xrefs <addr>` a
  one-line query. Set this up in the first days next time.
- **Port small functions in full, not a simplified version.** Several bugs
  came from "subset" ports of tiny functions whose side rules mattered:
  SetState's refusal rules (a docked harvester ignores "move aside"),
  the object-in-cell lookup (only the cell owner's units), the unit draw
  (death modes pick the rotation slot). If a function is short, port all of
  it.
- **Struct fields: verify by more than one use.** We read unit +0x10 as the
  type; it is the state. Cross-check a field with at least two unrelated
  readers before relying on it.
- **Fields are often reused per object kind** (e.g. a counter that means
  "timer" for a worm and "dock frame" for a harvester). Note the kind next
  to each field.
- **The data files carry the look and sound.** Colours, fonts, sounds and
  rules came from the game's own files (`FONTCOL.FPL`, `VARS.BIN`,
  `samples.uib`, ...). Matching a screenshot by eye produced wrong guesses
  (white text instead of the palette's red).
- **Old compilers leave arithmetic traps.** The AI loads a negative
  random offset as an unsigned 64-bit integer, so half of the rolls give
  huge values whose low dword is a small or negative number (the next
  attack comes at once). Reproducing it needs 80-bit x87 emulation
  (`src/sim/x87.hpp`); a modern `long double` is only 64-bit on MSVC. Write
  a table test from the decompile before porting such formulas.
- **Read the loop and the timer first.** The main loop, its per-pass order
  and any timers decide the meaning of almost every counter.
- **The middleware DLL is part of the oracle.** The volume and pan law
  live in the game's `MSS32.DLL`, not in the exe. Importing the DLL into
  its own Ghidra project took minutes and showed which of its two mixers
  the game's preferences select (DirectSound, dB-based, far side
  `pan / 63`), where the guess had been `pan / 64` (doc 59 §7).
- **Check the data before porting a feature for a mission.** A4V1's
  Fremen looked like the reason to port stealth, but their record has
  special kind 0, so they never cloak. One byte read from the rules table
  saved a whole subsystem from the critical path.
- **Headless screenshots: park the mouse.** With no input the mouse sits at
  (0, 0), which is the up-left edge-scroll zone, so after a few hundred loops
  the camera has drifted to the map corner and the view looks "broken". Move
  the mouse into the playfield in the scripted run first.
- **Check invariants on the player's save, not only in clean runs.**
  "Can't build here" came from stale infantry bits in the occupancy
  map. Twenty-thousand-tick AI runs found none; loading the player's save
  and scanning every cell for bits with no owner found seven at once,
  and a targeted test (tanks driving through walking infantry) then
  reproduced it: a "subset" port of the crush function cleared one of
  the two cells a stepping soldier holds.
- **Test a UI fix through the tick, not only the click.** "Click an enemy
  to view it" was reported fixed after testing the click handler alone;
  in play the AI's ClearUnits wiped the flag on the next tick (the exe
  wraps DoAI in GetSelected / ResetSelected). A test that clicks, then
  runs the world for a few dozen ticks, would have caught it before the
  user did.
- **Index spaces: record ≠ type.** A mission dump printed unit names by
  record index; record 10 came out as "MCV" and sent us porting the AI's
  MCV site search for a mission that has no MCVs. Name every id by the
  table it indexes (`record → name_index → name`) in dump tools.
- **A TODO in the hot path shows up as a "visual" bug.** Missiles
  "flying past their target" and "missile tanks ignoring carryalls" were
  two TODOs (the homing step, the air pass of the target search), not
  drawing problems. When a report sounds cosmetic, grep for TODOs in the
  simulation code behind it first.
- **Small shared helpers hide off-by-one ticks.** Porting the capture
  exposed that destroyed buildings were removed one tick early (the exe
  decrements while non-zero and removes on the next update). Re-read the
  countdown code of every state you touch.
- **"The player is slot 0" holds for one campaign only.** It was true
  for all nine Atreides missions, so the shortcut went unnoticed: the
  game screen, the difficulty factors and the score all used slot 0. In
  H1 the player's base is slot 1 (gSideId). Name the local-player global
  (`0x798544`) from the first day and never write a literal 0 for it; a
  `TODO: the local player index` left in the code is a bug waiting for
  the second campaign.
- **When a doc and the code disagree, read the exe, not either one.**
  The front-end doc said a war declaration plays no sound; the earlier
  alliance code played S_CHATMSG for it. A few lines of disassembly
  (`0x458397`) showed the code was right and the doc wrong. The same
  check found that the sound goes through the voice queue in "mode 0",
  which neither had.
- **Copy the original's version check, then make it stricter.** The
  original compares a version number; for a rebuild a number is not
  enough, since every build can carry the same one. A hash of the
  sources and compiler at join time, and of the loaded data at the start,
  turns "mysterious out of sync" into a clear refusal.
- **"A menu pauses the game" can have exceptions by name.** The general
  rule (no game view while a menu is open) was right for every menu but
  the chat box: the exe sets its menu flag and clears it again when the
  page is GAMEMESSAGE. Read the function that sets a mode flag to the
  end, not only its first line; one string compare hid the exception
  that the first playtest found as "F6 freezes the game".
- **Put a written-down compiler decision into the build on day one.**
  The float decision (`/fp:strict`, no FMA) sat in a doc for weeks while
  the build used the compiler's default; a docs review found it. Set as
  a global flag it then broke SDL (constant float initialisers): apply
  such flags after the dependencies are added, to your own targets only.
- **One exe routine, one port: list its callers first.** HomeView had
  three ports (start camera, H key, airstrike), each with a different
  guess for one field (image height vs frame height; it is offset_y), and
  the H-only "select the MCV" flag was missing from all. Run `xrefs` on a
  routine before porting it for a second caller, and give each caller's
  arguments in the doc.
- **Read call arguments from the listing when the decompiler drops them.**
  Ghidra showed `FUN_0046f900(uVar)` for 40 voice calls because each
  argument came from another call. A 30-line script that walks the pushes
  before every call site (a call to the name lookup turns its pushed name
  into the next `PUSH EAX`) listed the mode, limit and priority of each
  site in one run. Also check ring buffers by their append test: "full
  when (tail + 1) % 5 == head" holds 4, not 5.
- **Settle "is there another writer?" with a byte-write scan.** A short
  capstone script over all of `.text` that lists every write to
  `[reg+disp]` (or an absolute address) answered several open points in
  minutes (a unit field, a cell flag bit, a global that is only ever
  written 0). It is the fallback when Ghidra scripting is off. Keep each
  helper's scripts in its own folder: a `dis.py` lying next to another
  script shadowed Python's own `dis` module and broke `import capstone`.
- **Survey every mission with the real set-up, then keep it as a test.**
  Building each campaign mission as the game does (`build_game` with VARS
  and the AI blocks) and looping over all of them answered "which missions
  use X" in seconds; a bare World silently showed no AI activity. And an
  unreachable branch is a finding: before reproducing a per-machine
  fallback, check whether any caller can reach it.
- **Check signedness on every compare of a quirk value.** One unsigned
  `JAE` turned a negative limit from "move no unit" into "move the whole
  base". And out-of-range reads are often deterministic: find the struct
  behind the address (a LIFO pool means its low slots are almost never
  used) before calling the value garbage.
- **An index into a side's own pool changes meaning when the object
  changes side.** The exe stores refinery, repair pad and carry slots as
  per-side indices and looks them up only in the owner's pool
  (`FUN_004a4740(side, idx)`); our global ids hid that a deviated
  harvester should re-pick its refinery. Before trusting a stored index,
  check which pool the exe's lookup searches.
- **A field the original never writes on creation needs its allocator
  read too.** The exe's free routine zeroes the whole object, so an
  apparent "stale byte" was an exact 0; our 0xFF default made every AI
  MCV deploy. And read an input handler as one ordered list before porting
  per-mode branches: ours had re-ordered the drag test, the sidebar and
  the radar differently in each mode.
- **Model the allocator before trusting "index & 7".** Every 8-tick
  stagger in the exe keys on the per-side LIFO pool slot, shared by units,
  buildings, bullets and explosions; a global id shifted every phase. Then
  audit stale ids by the holder: once every object is shown to spend at
  least one update dying before its slot is freed, only references that are
  never cleared (first attacker, refinery, repair pad, AI entries) need
  slot semantics. Edit Markdown with scripts, not PowerShell double-quoted
  strings: a backtick before 0 became a NUL byte and git saw the doc as binary.
- **A global the exe never resets or saves carries history across
  games.** Keep it in the session object, not in the World, or the second
  mission of a run quietly differs from the original. And read the main
  loop's call order (draw, state update, blit) before calling a one-frame
  question "on-screen only": it settles many of them from code.
- **A LAN host answers once per network it is on.** A broadcast reaches
  a PC on each of its adapters (LAN, loopback, VPN, Hyper-V), and each
  answer comes from a different address. Key a found game by an id the
  host chooses, not by the address it answered from.

## 3. Measuring the original

- **Measure instead of assuming.** We assumed the tick rate followed the
  monitor; a 10-second read of two globals from the running original
  (`tools/measure-reference.ps1`, ReadProcessMemory, read-only) showed a
  1-second timer that makes the speed setting real ticks per second. The
  wrong assumption had run the game at 60 % speed for weeks.
- **Wrappers change behaviour.** The reference runs under a DirectDraw
  wrapper (cnc-ddraw); know which of its settings affect timing before
  using the reference for timing questions.
- **Process names:** a `.DAT` executable keeps its extension in the
  process name (`DUNE2000.DAT`), unlike `.exe`.

## 4. Engine design

- **Simulation separate from rendering and input**, deterministic, with
  the original's integer and float operations in the original order
  (float32, strict, no FMA). Golden tests run the simulation headless on the
  real data and skip when the data is absent.
- **Stable object ids.** Indices into a vector shifted when units were
  erased; ids plus a lookup fixed a class of bugs.
- **Keep the original's frame structure.** The original repaints only what
  changed and relies on side effects (e.g. "redraw the game view for two
  passes after a menu closes"). Copy those rules or menus leave ghosts.
- **Reuse one menu system.** In-game menus run on the same script-driven
  menu runner as the front end, in an embedded mode, instead of a second
  implementation.
- **Dev aids pay off:** command-line jump into a mission, scripted clicks,
  headless screenshots at a given loop, env-var switches. They let the
  assistant check UI work without the user.
- **Run the test suite in Debug too, not only Release.** MSVC's Debug STL
  checks `vector[]` bounds; Release silently reads garbage. A slot cleared
  by a helper and then read again (`is_special(-1)`) went unnoticed for many
  missions because only the Release tests were run.
- **Set "who am I" before building the world.** The local player was
  assigned after the constructor had created the map's buildings, so
  creation-time rules ("the local player's buildings reveal") ran for slot
  0. Every Harkonnen / Ordos mission showed the Atreides base at the start.
  Test non-default slots for visibility, not only for scripts.
- **Check that every menu setting reaches the simulation.** The difficulty
  setting was parsed, saved and loaded, but never handed to the World, so
  every game ran on Normal until the user asked what the levels do.
- **One widget, two input loops: feed both.** The menu runner reads input
  in the front end and, embedded, in the game. Only the in-game path set
  "button held", so slider drags and list-thumb drags worked in OPTIONS
  but never on the skirmish set-up page. Scripted clicks (press and
  release in one frame) cannot catch this; a drag needs a held frame.
- **Name assets by the exe's slot, not by our helper's name.** "Font 0" in
  the docs was mapped to our `menu_font()`, which is font 1 (FONTCOL). Read
  the loader's file name (`0x4de77c` = "font") before choosing.
- **Test set-ups must load every rules file the game loads.** The
  skirmish test helper (and 20 other test worlds) never loaded ARMOUR.BIN,
  so every hit did 0 damage, and the AI tests "passed" without any unit
  ever being hurt. Found only when a Siege Tank test checked for damage.
  A shared helper is the place to load the full rule set once.
- **A fix can expose a missing partner.** Locking the Siege Tank turret
  (flag 0x8000) without the fire code's "locked: turn the hull" branch
  would have left the tank unable to aim. When porting one side of a
  flag, grep the exe for every reader of it.
- **A buffer the original updates on events is behaviour, not a cache.**
  Our radar was recomputed from the cell flags every frame; the exe
  colours it once and then writes single pixels at four call sites, so a
  removed building shows its fixed colour until a load re-colours the map.
  List every writer of such a buffer (xrefs of the pixel function) and
  keep the buffer in the simulation instead of deriving it.
- **Before calling a worm (or any AI) "stuck", map its terrain.** Worms
  wandering in a rock-enclosed sand pocket looked like a bug; a dump of the
  sand cells showed the original's rule (random destination anywhere,
  partial route, step aside) at work.

## 5. Pitfalls with the tools

- **Upscaled movies without touching the flow.** Replace only the picture
  and keep decoding the original movie for sound and clock: timing,
  skipping and the movie's end stay exact, and frame-interpolated 60 fps
  files need no special case (each picture has its time stamp). Try
  upscaling models on the worst part of the source (here the VQA block
  pattern on faces) before a full run: the models differ far more than
  their names suggest (docs/enhanced-movies.md).
- **Topaz Video's ffmpeg prints the account's sign-in tokens on stderr**
  at every start, whatever `-loglevel` says. Send its stderr to `nul`
  through cmd, never into a log or a tool result. In PowerShell 5 a
  native program's stderr line stops a script with
  `$ErrorActionPreference = "Stop"` even under `2>$null`, so run such
  programs through cmd with `2>nul`.
- **PowerShell 5 and cmd:** its pipe corrupts binary data and it mangles
  quotes it passes to `cmd /c`; write the command line to a `.cmd` file
  and run that.
- **Bit readers and C++ evaluation order.** `(decode() << n) + bits(n)`
  may call `bits` first: the operands of `+` (and `|`) are unsequenced.
  Python evaluates left to right, so a straight port of a working Python
  decoder broke. Read every stream field in its own statement.
- **CMake `if(... MATCHES ...)` overwrites `CMAKE_MATCH_n`.** Copy the
  captures into variables before any further `MATCHES` test.
- **A second compiler is cheap insurance.** The first GCC build (Ubuntu in
  Docker, no WSL distro needed) compiled and passed every test, and a
  state fingerprint (SHA-256 of a saved game after 6000 skirmish ticks)
  matched MSVC bit for bit. Strict IEEE float32 and our own `rand` make
  that possible; checking it early keeps lockstep multiplayer realistic.
- **Test CI steps on a fresh clone, not the dev folder.** The dev folder
  has the game data, so golden tests run there; a clone shows that they
  skip cleanly and that nothing depends on ignored files. Also note the
  local Visual Studio version (here 2019 Build Tools) differs from the CI
  runner's (2022): the workflow names the runner's generator.
- **Look at CI after every push, and build the GCC job locally before a
  run of merges.** MSVC accepts a missing `#include` that GCC rejects
  (`std::sqrt` without `<cmath>`, `ptrdiff_t` without `<cstddef>`). The
  Linux job was red for 30 pushes in a row (2026-10-16) while the local
  Windows builds were green. A failure also hides the next one, so build
  locally with `ninja -k 0` (the Docker image in `tools/ci`) to see every
  error at once.
- **Check what the exe imports before calling it "one file".** The MSVC
  default runtime (`/MD`) needs the Visual C++ redistributable, which a
  clean Windows lacks; `CMAKE_MSVC_RUNTIME_LIBRARY` (set before the
  dependencies) makes it self-contained.

- **Windows PowerShell 5.1 corrupts UTF-8 files on a round trip.**
  `Get-Content` reads them as the ANSI code page and `Set-Content
  -Encoding utf8` writes a BOM, so `·` and `→` in parity.md came back as
  `Â·` and `â†’`. Edit text files with the editor tool, or read / write
  them with explicit UTF-8 in a script, and check `git diff` for
  unexpected lines after any shell rewrite.
- **A command that waits on stdin looks like a slow build.** A stray
  `python -` (script from stdin) at the head of a chained command sat
  waiting forever, and the assistant blamed the compiler without checking.
  The user's "are you sure?" caught it. When something takes longer than
  it did before, look at the running processes before explaining it.
  It happened a second time even with `< /dev/null` added (Git Bash on
  Windows): redirection does not make `python -` safe. Never type it, not
  even as a placeholder; write a script file and run that.

- Shell heredocs with mixed quotes broke edits repeatedly; writing edit
  scripts to a scratch file and running them was reliable.
- `cat > file` with no input blocks a shell call; never leave a stray
  stdin reader in a command.
- Python edit scripts: `\\n` inside a triple-quoted string becomes a real
  newline in the C++ output; use raw strings or check the result.

- **Restoring a file from a backup copy can skip the rebuild.** An
  experiment (switch a feature off, compare the state hash, copy the
  originals back) left the build on the experiment's objects, because
  `Copy-Item` keeps the backup's older time stamp and MSBuild saw nothing
  newer. Touch restored files (or use a temporary commit) before building.
- **Find a field's readers before porting it.** A scan of every
  instruction whose displacement falls in the player struct's counter
  block (also as absolute addresses) showed the per-record kill / loss
  arrays are written in ten places and read in none; it also showed
  that KillUnit counts nothing, which fixed three counting differences
  in our totals that the score and CASUALTIES do read.
- Never name a scratch script like a standard module (`dis.py`,
  `inspect.py`, ...): Python puts the script's folder first on the path, so
  every later `python` run from that folder breaks with an unrelated error.

- Hard-to-reproduce play bugs: add an in-game state dump early (here it
  was `D2K_DEBUG=1` + F8 → `d2k_debug.log`, removed at release with the
  other dev tools: selected units, every unit with a
  target, bullets, the cells around them, the cell under the mouse). Ask
  the player to press it *while* the bug is visible; a dump after the fact
  showed nothing. One capture found a bug three verbal reports had not.
- Pseudo-code in our own RE notes can hide the bug: doc 09 wrote the
  infantry occupancy as `f |= slot | side`, but the exe clears the owner
  bits first. When a symptom points at a documented step, re-read the
  decompiled code, not the note.

- Split big features into "reverse-engineer the UI behaviour" (a
  background agent writing only a findings doc, no code) and "build the
  engine side" (in parallel). For save/load the agent mapped ~15 menu
  functions while the state serializer and its determinism test were
  written; neither blocked the other.
- Saved games: one template walks every field for both save and load,
  and a test loads a save into a world started from a *different* seed
  and checks the two stay byte-identical for thousands of ticks. That
  catches forgotten fields, which a plain round trip cannot.

- Do not ship a "close enough" stand-in for a core algorithm. Our first
  route search (line + generic wall-follow) caused weeks of vague "units
  get stuck / move dumb" reports; the exact port of FUN_0045eaf0 and its
  smoothing fixed them at once, and the player noticed units "moving
  like the original". If a stand-in is unavoidable, list it as a known
  gap and port it before playtesting that area.
- A saved game from the moment a bug happens is the best repro: load it
  after the fix and the bug either is there or is not.

- When one object serves two modes (our menu runner hosts both the front
  end and the in-game menus), reset every mode flag and pending state at
  each hand-over. A flag left set after a mission made later menus replay
  stale transition snapshots: a "sometimes" UI bug that only appeared
  after playing.
- Before adding AI behaviour for the next mission, dump that mission
  (tech, objects, script events, which AI flags are on). For A3 it showed
  that upgrades and walls were needed and the repair pad and Starport
  were not.

## 6. Decisions worth making on day one

From CLAUDE.md §10, which all held up:
- exact target version, by hash;
- licence (affects which community code you may read);
- "no gameplay changes" and what counts (visible map area did);
- float handling (keep the formulas, standard float32);
- a fixed loop rate, and how the original's timing maps onto it;
- display aspect options (CRT 4:3 vs square pixels).

## 7. Notes for Civilization II: Test of Time (to fill in)

- Settle the target version and its data layout first; scenario files and
  the events language will matter as much as the rules tables.
- Save games are a primary format there (turn-based, no live state), so
  plan a save/load test harness early.
- Turn-based: determinism and the AI are the hard parts; timing much less.
