# Parity tracker

Last full review: 2026-10-16 (after the close against the exe).

Status: `-` not started · `RE` being reverse engineered · `WIP` implemented, open points left ·
`OK` implemented from the exe, documented, with tests or a measurement · `FIX` deliberate opt-in deviation ·
`To test` = settled against the exe; needs a real-world test.

Every TODO(verify) in `docs/re/`, `docs/formats/` and `src/` was settled
against the exe on 2026-10-16. Side-by-side checks with the original use
the procedure in `docs/reference-setup.md` (screenshots at 640x400, e.g.
MAINMENU and PLAY_ATREIDES); there is no replay harness. "Playtested"
means played in our build.

| System | Status | Evidence / notes |
|---|---|---|
| Data import from CD + 1.06 patch | OK | First-run import in the game (`src/platform/setup_sources.cpp`, `docs/first-run-setup.md`): CD image or folder plus `d2k106us.exe`, every file matched by SHA-256 against the 1.06 manifest (1305 files). Also `tools/import_game.py`; inventory in `docs/inventory-1.06.csv`. Tests: `tests/platform/test_setup.cpp`, `tests/formats/test_archives.cpp` |
| Data folder validation | OK | Start-up check (`src/platform/manifest.cpp`, `game_data.cpp`): every manifest file present with its size, plus the SHA-256 of `DUNE2000.DAT`. Tests: `tests/platform/test_game_data.cpp`, `test_setup.cpp` |
| Window, 640x400 screen and scaling | OK | `src/render/display.cpp`: the exe's 640x400 screen (`docs/re/04-menu-flow.md`, `docs/re/07-ingame-screen.md`); `aspect=crt` (default) or `aspect=square`, `--aspect` / settings.ini; checked by the screenshot comparison (`docs/reference-setup.md`). The default aspect is the user's choice (crt until then), not a parity point |
| Game tick rate | OK | `sim::TickScheduler`: the exe's 100-slot pattern and 1-second timer; loop fixed at 60 Hz by decision. Measured in the original: speed 75 = 19.98 ticks/s; ours 20. `docs/re/02-timing-and-arithmetic.md` |
| Sim arithmetic | OK | The exe's float32 formulas, constants, operation order and `__ftol` truncation in IEEE float32 (CLAUDE.md §10); `src/sim/x87.hpp` only where 53-bit x87 changes a result (test); `/fp:strict` and `-ffp-contract=off`. Determinism: save continuation tests, a 6000-tick fingerprint test, network sync tests. `docs/re/02-timing-and-arithmetic.md` |
| Boot sequence | OK | First launch: g_int1 → g_int2 → house select; later: t_titl → main menu; Esc-only movie skip. Settled against the exe; playtested. `docs/re/01-boot-sequence.md` |
| Palettes / R8 / R16 sprites | OK | Parser drawn from R16 (decided); house colours from COLOURS.BIN; per-entry R16 palettes (doc 47). Golden test over every R8/R16 file (`tests/formats/test_r8.cpp`). `docs/formats/r8.md` |
| VQA video | OK | Decoder verified on all 43 movies (golden test, `tests/formats/test_vqa.cpp`); audio-synced player at 640x400; Esc-only skip as the exe. `docs/formats/vqa.md` |
| Audio playback (AUD / SOUND.RS) | OK | All 268 AUD + 57 SOUND.RS WAVs decode (golden test). Music streaming as Miles, speech rings, effect channels with the exe's pick, positional pan / volume, the 16-slot sample cache, the Miles volume and pan law from MSS32.DLL. Tests: `tests/audio/test_channels.cpp`, `test_sample_cache.cpp`, `tests/formats/test_audio.cpp`. `docs/re/59-sound-channels.md`, `docs/formats/audio.md` |
| UI menus | OK | menus.uib with FADE/TWEEN/SKIP transitions, fonts (UI text in font 0, FONT.BIN map), images with the exe's blit flags, the UIL dispatcher, menu stack, value bars, list and edit boxes, menu music, the globe. Checked against screenshots of the original (MAINMENU, PLAY_ATREIDES). Tests: `tests/ui/test_ui.cpp`, `tests/ui/test_formats_drawing.cpp`. `docs/re/03-ui-scripts.md`, `docs/re/04-menu-flow.md` |
| Campaign table | OK | campaign.uib decoded (3x10 records); territory shapes from the player's exe; golden test of the map reveal. `docs/formats/campaign.md`, `docs/re/05-campaign-map.md` |
| Maps / missions (.MAP / .MIS) | OK | `src/formats/map.cpp`, as LoadMapData (FUN_0044c6d0) and the mission loader (FUN_0044f8e0) read them. Golden test: every mission map and .MIS parses. `docs/formats/maps.md`, `docs/re/19-mission-script.md` |
| Rules tables (bin\*.BIN) | OK | Read as the exe does: Templates.bin, TILEDATA.BIN, VARS, SPEED, ARMOUR, TECHPOS, BUILEXP, CIRCLES, COLOURS; the 1.06 tables the 1.06 exe loads (`docs/data-sources.md`). Golden test (`src/formats/templates.cpp`). `docs/formats/templates.md`, `docs/re/08-templates.md` |
| Maps / terrain | OK | Map set-up, spice auto-tiling (FUN_0044ec50) with exe tables, objects, craters, the 480x380 view (`src/render/terrain.cpp`); golden test (A1V1 spice smoothing). `docs/re/06-terrain.md`, `docs/re/07-ingame-screen.md` |
| RNG | OK | MSVC rand() LCG, `src/sim/rng.hpp`; test |
| Objects (placement / drawing) | OK | Map objects via TILEDATA and template records, DATA.R16 art, house colours, building animations, the infantry-in-vehicle pass (FUN_0042af00), the concrete hit point rule for map-placed buildings, the draw-list sort key. Golden tests. `docs/re/08-templates.md` |
| Unit movement | OK | `sim::World`: MoveStep, turning, occupancy, route cache, FindPath and smoothing (`src/sim/pathfind.cpp`), SetState rules, prone and idle moves, auto-fire inside MoveStep, the order queue (orders run 2 ticks after posting). Golden tests (A1V1 moves, blocked unit, drive round a building). Orders run 2 ticks after the click, settled from the exe's order queue (doc 49). `docs/re/09-unit-movement.md`, `docs/re/49-order-queue.md` |
| In-game input | OK | Click and box selection, cell-based hit test (FUN_004a5270), edge scrolling, cursors, click puff, Alt crush move, Ctrl rules, the left-release order of FUN_004433a0. Tests: `tests/sim/test_ingame_input.cpp`, golden tests. `docs/re/10-ingame-input.md`, `docs/re/46-move-marker-alt-ctrl.md` |
| Sidebar / top bar / radar | OK | Credits, power bar, radar (event-driven pixels, doc 11 §4), build lists, icons, scroll, repair / sell modes, economy (drip, power, silos, production, hold / cancel / refund). Golden tests (A1V1 lists, production, radar). The radar scales with the map size (1 px per cell, centred), confirmed in the original by the user (doc 25 §B.1). `docs/re/11-sidebar.md`, `docs/re/25-control-groups-and-radar.md` |
| Placement and spawn | OK | Placement grid, order 0x26, concrete, build-up, primary buildings, exit cells, factory spawn, footprint conversion and skirt tiles. Tests in `tests/sim/test_placement.cpp`. `docs/re/12-placement-and-spawn.md` |
| MCV deploy, engineers, saboteurs | OK | Deploy (order 0x2F, state 0x1E), DoMCVs / DealWithMCV, engineer capture, saboteur sabotage, KillUnit death modes. Golden tests (A4V1, A6V1). `docs/re/30-mcv-engineers.md` |
| Shroud (fog of war) | OK | Cell byte, CIRCLES.BIN reveal, edge pass (table 0x4e0b18), per-tick reveal, allied reveal, start reveal, radar and click rules. Golden tests (A1V1, O8V1). `docs/re/16-shroud.md` |
| Sandworm | OK | Sleep / travel / hunt states, eating, appetite, ripples, worm sign, being shot. Golden tests (A1V1, skirmish thumper). `docs/re/17-sandworm.md` |
| Combat | OK | Weapons, ARMOUR.BIN warheads, target search, bullets, radius damage, deaths, debris, homing missiles, sonic waves, damage smoke, the per-side tick order, KillUnit in full, kill / loss counters (doc 60). Tests in `tests/sim/test_combat.cpp`, golden tests (A1V1 / A2V1 / A5V1). `docs/re/18-combat.md` |
| Mission script / end | OK | .MIS conditions and events every tick, defeat checks, message lines, end voice, banner, the end order of 0x449c9f / 0x449f36, SCORE / end movie / MVE_LOSE. Golden tests (A, H and O missions); all three campaigns played through. Checked against captures of the original (2026-10-16). `docs/re/19-mission-script.md` |
| Harvesting | OK | Harvester states 0xb-0x10, refinery choice, dock cell blocker, free harvester by carryall, explosions and aircraft passes. Golden tests (A1V1 harvester cycle, free harvester). `docs/re/13-harvesting.md`, `docs/re/14-aircraft-and-effects.md` |
| In-game music | OK | First track per house and mission from campaign.uib (skirmish / MP: arakatak), random next track, SOUNDOPTIONS jukebox with its quirks, exact Miles streaming; playtested. `docs/re/15-ingame-music.md`, `docs/re/48-music-page-and-tab.md` |
| Sell / repair / guard / retreat | OK | Building repair and selling, survivors, unit repair at a pad, guard and retreat. Tests in `tests/sim/test_sell_repair_capture.cpp`, golden tests. `docs/re/21-sell-repair-guard-retreat.md` |
| In-game menu | OK | INGAME over the frozen game, OPTIONS value bars, Abort, Restart, RECAP, SOUNDOPTIONS, Tab focus, in-game transitions. Tests: `tests/ui/test_ingame_menus.cpp`; playtested. Checked against captures of the original (2026-10-16). `docs/re/20-ingame-menu.md` |
| Computer player (AI) | OK | ActivateAI, DoBuildUnits, attacks, MonitorGroups, DoStopStartStuff, rebuild, DoProtection, DoDefence, upgrades, Starport, special weapons, base box, auto-BESERK. Tests in `tests/sim/test_ai.cpp` (A1V2, A2V2, skirmish, 12000-tick run). Known, unreproducible deviation: the patrol of an inverted base area reads unrelated memory in the exe (doc 53 §1.6). `docs/re/22-ai-core.md`, `docs/re/23-ai-teams.md`, `docs/re/53-ai-gaps.md` |
| Tooltips | OK | FUN_0044b5c0 / FUN_00423b40: build-list cells, buttons, power bar, playfield texts, Starport and upgrade pages; settled against the exe; playtested. `docs/re/11-sidebar.md` §11 |
| Score screen / next mission | OK | SCORE_SINGLE count-up ported (FUN_0043f030), high-score table and hs.dat; tests in `tests/campaign/test_front_end.cpp`; all three campaigns played through. `docs/re/24-score-and-next-mission.md` |
| Credits | OK | CREDITS menu (CreateCredits FUN_0043d2a0, CreditsDraw FUN_0043d520); golden test; playtested. `docs/re/42-credits.md` |
| Object pool / unit limit | OK | 1000 slots per side, LIFO from 999, slot staggers, stale references by (side, slot), ReachedUnitLimit, the leaked zero-distance bullet slot. `docs/re/50-object-pool.md`, `tests/sim/test_pool.cpp` |
| Unit turrets | OK | Turret turn (FUN_00493f40), aim at the destination, locked SIEGE_TANK turret, idle wander. `docs/re/18-combat.md` §3.6; tests "moving turreted", "SIEGE_TANK turret", "idle turret wander" |
| AI activation facings, unit counters | OK | DoUnitFacings (FUN_00420c80), the owned counters per record. `docs/re/53-ai-gaps.md` §11; tests "DoUnitFacings", "GuardGroupSize" |
| Skirmish setup page | OK | SKIRMISH page with the exe defaults, map list, controls, multistate box, value bars (NoChangeOptions), tooltips, StartGame; playtested. Tests in `tests/campaign/test_front_end.cpp`. `docs/re/51-skirmish-setup.md`, `docs/re/55-multistate-valuebar-tooltip.md` |
| Skirmish start and end | OK | Start set-up, AI sides, win / lose check, REPLAY_MISSION, SCORE table and in-game score box (doc 56), P<n>.SAV. Golden tests (start, AI game, finishing places, save). Checked against captures of the original (2026-10-16). `docs/re/52-skirmish-start-and-end.md`, `docs/re/56-skirmish-score.md` |
| Crates | OK | Crate spawn, life, pick-up effects, the AI crate scan; tests; playtested. `docs/re/54-crates-and-thumper.md` |
| Thumper | OK | Deploy, thumps, worm attraction; tests; playtested. `docs/re/54-crates-and-thumper.md` |
| Control groups and hotkeys | OK | Group keys, S / X / P / G / R / H / E / N / T / Y, bookmarks, radar drag-scroll and release-to-move, HomeView (FUN_0046cf10); tests. `docs/re/25-control-groups-and-radar.md` |
| Terrain damage | OK | Crater counters, broken concrete, crater overlays, spice-bloom bursts; test. `docs/re/26-terrain-damage.md` |
| Saved games | OK | SAVE / LOAD / DELETEGAME menus, S<n>.SAV / P<n>.SAV with the original header plus our state; byte-identical continuation test. The LOAD page (three saves, the inactive scroll bar) is pixel-identical to a capture of the original (2026-10-16, `tests/ui/test_screen_parity.cpp`; the list and track frames are black, FUN_0042d140). `docs/re/27-save-load-menus.md` |
| Upgrades | OK | Upgrade rules, slot, orders 0x2D / 0x2E, sidebar page 3, AI UpgradeBuildings. Tests in `tests/sim/test_economy_map.cpp`. `docs/re/28-upgrades.md` |
| Spice blooms | OK | Cell-object table, bloom timers, growth, bursts, SpiceExpl blobs, the AI scan; saved; tests. `docs/re/31-spice-blooms.md` |
| Stealth, Fremen, sietch | OK | Cloak state machine, reveals, targeting, drawing, the stealth crate; golden test (Fremen cloak). `docs/re/32-fremen-stealth.md` |
| Starport | OK | Prices and stock, cart orders, frigate delivery, AI BuyUnitsFromStarport (bugs kept), sidebar page 2. Golden test (A5V1). `docs/re/34-starport.md` |
| Carryall ferry | OK | Request table, RequestMonitor, lift and drop states, map carryalls from the edge. Golden test (A5V1). `docs/re/33-carryall-ferry.md` |
| Airstrike (ornithopter) | OK | Atreides airstrike special: charge, mode 4, order 0x30, LaunchAirstrike, bombing run, AI DoSpecialWeapons case 3. Golden test (A7V1). `docs/re/35-ornithopter.md` |
| Devastator, Sardaukar, self-repair | OK | Self-repair (flag 0x800000), Devastator plasma and self-destruct, Sardaukar weapons, the Grenadier death blast. Golden tests. `docs/re/37-devastator-sardaukar.md` |
| Turret orders, viewing enemies | OK | Selected turret (FUN_004a5860), orders 0x1E / 0x1F, building states, viewing an enemy unit, the target flash (doc 44). Golden tests (turret orders, viewed enemy, allied targets). Crash fix: the exe's "out of range" fatal error returns to state 2. A turret's Ctrl-click on an own unit (fires, no voice) and S with an enemy in range (keeps firing) confirmed in the original by the user (doc 38). `docs/re/38-turret-orders.md` |
| Palace specials, Deviator | OK | Saboteur / Fremen call-ins, the saboteur deploy, AI DoSpecialWeapons cases 0 / 1 / 2, the Deviator (ConvertUnit, timer, revert). Golden tests (A8V1, Deviator). `docs/re/39-palace-specials.md`, `docs/re/40-deviator.md` |
| Death Hand | OK | Charge, launch, flight, burst (40 DHBOMBs), AI case 4; playtested in A9; tests. `docs/re/41-death-hand.md` |
| Multiplayer (network games) | To test | Settled against the exe; real-network play with several PCs to be tested by the user. Lobby pages, lockstep, order delay, sync check, host hand-over; played with two copies on one PC and with Windows Sandbox. Deviation: normal difficulty in network games. Tests: `tests/sim/test_network.cpp`, `tests/net/`. `docs/re/57-multiplayer.md`, `docs/re/57a-takeover-ai.md`, `docs/re/58-mp-front-end.md`, `docs/design/multiplayer.md` |

## Checks against the running original

None left (2026-10-16). The look and timing checks were done against
the running original with window captures (`docs/reference-setup.md`):
main menu, house selection, the Atreides question, campaign map, LOAD,
the A1 briefing, mission A1's start view, the in-game menu, the in-game
RECAP close, the abort to the main menu, the skirmish set-up and results
pages, pixel for pixel where the screen is static. The radar size and
the turret orders were confirmed in the original by the user; the 2-tick
order delay is settled from the exe (doc 49). Multiplayer stays "To test"
with several PCs.

