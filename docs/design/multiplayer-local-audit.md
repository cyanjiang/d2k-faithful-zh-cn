# Lockstep audit: local side, selection, machine-dependent inputs

Repo state: HEAD `baf36e3` (Multiplayer step 1: `sim/order.hpp`, `World::post` /
`execute` / `with_selection`). Line numbers are from that commit.
Read-only audit: no repo file was changed.

The member is `World::local_player` (`world.hpp:497`, gSideId `0x798544`), not
`local_`. There is no `local()` accessor. It is set twice: in the constructor
(`world.cpp:79`) and again by the app (`game_screen.cpp:273`).
`World::difficulty` (`world.hpp:466`, `0x4e8bf0`) is the other per-machine
input that the sim reads, so it is covered here too.

Exe checks were done with `re/ghidra_http.py decompile/xrefs`. gSideId has 312
xrefs in 101 functions. Every sim-side function in that list that we map was
checked (list at the end).

---

## 1. Every use of `local_player`

### 1A. Presentation only: no sim state, no rng draw

| file:line | What | Exe |
|---|---|---|
| placement.cpp:30 `voice()` | returns unless `p == local`; writes `last_voice_` (only for local voices) and `sounds` | FUN_0046f900/0046f820 |
| placement.cpp:44,49 `unit_response` | local house, local selected units → `sounds` (`tick_count_ % 3` read only) | FUN_0049f240 |
| orders.cpp:323 `unit_voice` | local house → `sounds` | |
| placement.cpp:141,153; capture.cpp:166 | S_BUILDCONC / S_BUILDUP sounds | FUN_00457810, FUN_00497b90 (verified voice-only) |
| orders.cpp:52 | SELL voice | FUN_00456ee0 (verified) |
| orders.cpp:337 `tick_repair` `local` | REPAIRUNIT / LOWMONEY / UNITREPAIRED voices and `pl.low_money_said` (a voice latch) | FUN_00497b90 (verified voice-only) |
| economy.cpp:172 | DEATHPREPPING voice in `rebuild_lists` | FUN_0046be50 |
| economy.cpp:401-430 `power_sounds` | `power_shown_`, `radar_said_`, speech | FUN_0044aae0 |
| economy.cpp:679 | "…Ready" message line | |
| economy.cpp:695-719 | after `if (p != local) return`: voices, `pl.shown_units/shown_buildings` (NEWOPTIONS latch), `pl.low_money_said` | FUN_004467e0 |
| starport.cpp:88 | LOWMONEY voice | FUN_00456fd0 |
| aircraft.cpp:199 | DEATHLAUNCHED voice | |
| aircraft.cpp:326 | FRIGATEIN countdown voices (`last_voice_`) | FUN_0046def0 |
| combat.cpp:622,645,665,697 | UNDERATTACK / BUILDINGLOST / HARVATTACK / UNITLOST voices | FUN_0049d2e0 |
| capture.cpp:373 `make_hostile` | message line only (the alliance write before it does not depend on local) | FUN_0049f870 |
| capture.cpp:397-412 | CAPTURE / BUILDINGLOST voices, "BuildingEngineered" message | FUN_00497b90 (verified) |
| worm.cpp:225,301 | WORMATTACK / WORMSIGN voices (WORMSIGN also reads `view_x/y` and the shroud: sound only) | |
| capture.cpp:99-100 | S_THUMPER only when on screen (`view_x/y`) and the cell is unshrouded: sound only | |
| blooms.cpp:75,81,91 | crate explosion **sound** only for the picker (the effect spawns for everyone) | FUN_0049ebf0 (verified) |
| blooms.cpp:98-119 | crates "reveal map" / "no map": local **shroud** and `radar_known` only; "no map" calls `reveal_start()`, which adds 8 to `reveal_counter_` on the picker's machine only | FUN_0049ebf0 (also sets `0x517500[gSideId] \|= 1`, a radar flag) |
| blooms.cpp:208 | S_SPICEMOUND (`local_player >= 0` is always true) | |
| shroud.cpp:85 `reveal_units` | units allied to the local side reveal; shroud only | FUN_0044cff0 |
| shroud.cpp:99 `reveal_start` | local buildings reveal | FUN_0049f4d0 |
| placement.cpp:122,221 | `reveal_building` for local concrete and buildings | FUN_00456210 / FUN_004563b0 |
| combat.cpp:888,1441 | `shooter_reveal` when the target is local: writes the shroud | FUN_00495b90 / FUN_00496030 |
| mission.cpp:95-105 `default_end_check` | `won` / `lost` = this machine's result. `game_over_` and `human_alive_` do not use local | FUN_00453b90 (verified) |
| mission.cpp:165 | ct::kHarvested uses the local side's money. Campaign script only: `tick_script` returns before `evaluate` when `!campaign` | FUN_00453b90 case 8 |

**Shroud check (the key question).** No game logic reads the shroud (`Cell::state`)
or `radar_known`. All readers are listed here:

- `capture.cpp:100` (thumper sound) and `worm.cpp:300` (worm-sign voice): presentation.
- `mission.cpp:159` (ct::kRevealed): campaign only.
- `shroud.cpp` itself.
- The app: `game_screen.cpp:571,834,1094,1609`. These are UI choices (which order
  to post, what to draw). The order record then carries explicit arguments.

What does not read the shroud:

- Targeting (`target_unit_in_range`, `auto_fire`, `turret_auto_fire`).
- The AI (`ai_*`).
- Stealth (`tick_stealth` / `stealth_blocker`, which use cell occupancy bits).
- Crates (`drive_over`), blooms and the worm (`nearest_vehicle_on_sand`).

Writers: `shooter_reveal`, `reveal_*`, crates, `mission.cpp:252` (campaign
event). So the shroud is per-machine display state, and this matches the exe.

### 1B. Changes sim state or draws rng: desync risks

| # | file:line | What changes | Exe in MP | Verdict |
|---|---|---|---|---|
| B1 | **economy.cpp:59, 64, 523, 620** (`unit_cost`, `building_cost`, `upgrade_cost`, `speed_percent` in `tick_production`) | Cost and build speed: `difficulty` for the local side, 1 for the others. This reaches production progress and spending, unit repair cost (orders.cpp:398), building repair cost (orders.cpp:68), sold refund (orders.cpp:188-191), survivors count (orders.cpp:125-126), refunds in `rebuild_lists`, upgrade cost, and the AI's money tests | **Not the same.** `FUN_004429b0` (cost) and `FUN_00442960` (speed) check the network flag `0x7984c4`. In MP they use the per-side handicap byte `0x6b922e + side·0x3c` (0 → 75% cost / 125% speed, 1 → 100/100, 2 → 125/75), never gSideId. Verified in the decompile | **Must fix.** In MP, use a per-side handicap array (shared, from the session set-up). Also note that AI slots keep handicap 0 = "easy" factors in MP (doc 57 §3.4); ours gives AI 100/100 today |
| B2 | **placement.cpp:485** `create_unit` (x1 == x2) | `set_state(u, 3)` (area guard) for units created for a non-local side. This covers skirmish/MP start units (skirmish.cpp:64,76), crate units, `kAddUnits`, palace Fremen and saboteur, and survivors. On the owner's machine the unit stays idle; elsewhere it guards | **Not the same.** `FUN_00455870`: `if (side == gSideId \|\| 0x7984c4 != 0 \|\| kind in {2,8,10}) skip`. Verified. In network games no unit gets the guard state | **Must fix.** Add the network test (`!network && p != local && kind != 8`; the exe's kind list is 2/8/10, ours tests 8 because 2/10 return earlier) |
| B3 | **orders.cpp:38** `order_sell` | Refuses the sell when `p == local && lost`. In the handler, the issuer's machine refuses its own order and the others run it | **Not the same place.** The test is in the **poster** `FUN_0045a1a0` (`if (!gLost \|\| side != gSideId) PostOrder`), verified. The handler `FUN_00456ee0` uses gSideId only for the SELL voice | **Must fix.** Move the test to the UI poster (`game_screen.cpp:559`). It matters after the stalemate tie-break (B6), where a side can be "lost" while still owning buildings |
| B4 | **aircraft.cpp:429** `drop_step` | On easy (`difficulty == 0`), every odd unit of a delivery to a side that is not allied with the local side is skipped. This changes the units created | **Same as ours.** `FUN_0049e3e0` tests `0x4e8bf0 == 0` and `alliance[owner][gSideId]` with no network test (verified). The original would desync in MP when any machine has gDifficultyLevel 0, because alliances differ per local side. Difficulty is not sent in the handshake (doc 57 §3.2) | Original bug: keep as original, but neutralise in MP. Force `difficulty = 1` in network games (see B5). Then this branch never runs. Document it as a "the exe would desync" note |
| B5 | **ai.cpp:95** `ai_activate` | `difficulty == 2` changes the AI's UnitBuildRate (×0.75) | **Same as ours.** `FUN_0041b250` reads `0x4e8bf0 == 2` with no network test (verified, `0x41b3b9`). Machines with different stored difficulty would build AI differently: a latent desync in the original | Force one value in MP (e.g. normal = 1), or send it in the session set-up. TODO(verify) whether the exe's lobby writes `0x4e8bf0` (xrefs show writes only in `FUN_004418b0` / `FUN_004414d0`, the menu option handlers) |
| B6 | **mission.cpp:104** stalemate tie-break | `game_over_ = true` on every machine (deterministic). `won` / `lost` go to the local side when it is the side being checked this tick. Each machine gets its own result. The only sim reader is B3 | Same as ours (`FUN_00453b90`, verified: `if (gSideId == side) won = 1`) | Not a desync once B3 is fixed. Result-screen quirk only |
| B7 | **world.cpp:128** `add_map_unit` | `state = 3` for non-local map units | Campaign only: in skirmish/MP only worms (kind 5) go through it, and they return earlier (skirmish.cpp:81) | No MP risk. If MP ever uses map units, add the network test as in B2 |
| B8 | **combat.cpp:1497** `tick_stealth` | Clears `selected` of cloaked non-local units | Same (`FUN_0049f610`, verified) | Selection only. Harmless, provided no tick code reads foreign selection (§2) |

The `local_player` reads that remain are A. Combined with §2 and §3, these are
the only gSideId-dependent writes found.

---

## 2. Selection (`Unit::selected`, `Building::selected`)

### 2.1 Fine (deterministic or local view only)

- **Order run.** `world.cpp:786-787` (`post` collects local ids), `:776-777`
  (`post_order` for AI closures), `:834-848` (`with_selection` sets the side's
  units from `ids`, then restores). Handlers that read unit selection inside the
  run: `order_move` world.cpp:327, `order_dock` harvest.cpp:168,
  `order_attack_unit/building/ground` combat.cpp:1126/1142/1156,
  `order_guard / retreat / repair_units` orders.cpp:200/256/303,
  `order_stop / scatter` units groups.cpp:63/73/85.
- **AI select → post → clear** on its own side. `ai_groups.cpp` (177/186/719/797/1013-1090/1166/1420/1569),
  `ai_rebuild.cpp:351`, `capture.cpp:231` (`ai_deal_with_mcv`), `ai.cpp:393`.
  `ai_groups.cpp:839` is a no-op.
- **AI "GetSelected" put-aside** (`ai.cpp:359-366`, `396-399`, FUN_0041cc20 /
  FUN_0041cc90). This reads the local machine's selection of an AI side's unit
  (one being "viewed" via `view_unit`). It clears that unit before DoAI and sets
  it again afterwards, so DoAI sees the same state on every machine.
  - It only stays equal if a machine never has two units of one AI side
    selected. The UI cannot do that: `view_unit` deselects all, and shift-select
    / box / groups take own units only.
  - The exe does the same.
  - Fine, but keep this invariant in mind when adding UI.
- **Selection writes that only touch the local view:**
  - `set_state(0x11)` deselect (world.cpp:314).
  - `ferry.cpp:251` (picked-up cargo).
  - `convert_unit` `c.selected = false` (capture.cpp:450).
  - `tick_stealth` (B8).
  - `capture_building` (capture.cpp:491-494): when the captured building is
    selected on this machine, it calls `deselect_all()` (all sides) and selects
    the new building. Local view only. Same in the exe.
- **UI-only reads:** `unit_response` (placement.cpp:49), `any_unit_selected`,
  `selection_centre`, `next_unit`, `selected_turret`.

### 2.2 Desync risks

| # | Where | Problem |
|---|---|---|
| S1 | **groups.cpp:74-78** `order_stop` (0x1d part) | Reads `b.selected` of the issuing side's **buildings** at run time. The record carries only unit ids, and `with_selection` sets only units. On other machines the issuer's building selection is unknown (usually false). Its turrets go to state 2 on the issuer's machine only |
| S2 | **groups.cpp:139-149** `order_primary` (0x20) | Same: the primary building comes from `b.selected` at run time. Changes `pl.primary[]` and building flags 0x80/0x90 |
| S3 | **combat.cpp:1273-1279 / 1286-1292** `order_turret_attack_unit / building` (0x1e / 0x1f) | Same: the selected turrets get state 4/5 and a target on the issuer's machine only |

Exe: the poster copies "unit (or building) ids" into the record (doc 49 §1.1:
+0x26 up to 64 u16 ids; doc 38 §6 / doc 49 §4.1: 0x1d carries unit ids, then
building ids).

Fix: add the building ids to `Order` (or a second id list) at `World::post`, and
make `with_selection` set and restore building selection as well. Alternatively,
have these handlers take the building indices from the record.

Two more points:

- **S4 (design).** `World::post` (world.cpp:783-793) **recomputes `ids` from the
  local selection** and overwrites the record's ids. Received remote records
  must be queued without that step (a separate `enqueue(const Order&)`).
  Otherwise a remote order would run with the receiving machine's view of that
  side's selection, which is empty.
- **`assign_group` (groups.cpp:16-25)** writes `Unit::ai_group` directly from
  the UI. It is not an order: Ctrl+digit is direct in the exe too, FUN_0044b170.
  - Only AI code (owner == its own side) and the UI's `select_group` /
    `group_position` read `ai_group`.
  - So the human's control groups exist only on the owner's machine. Remaining
    machines agree among themselves, also after a LetAITakeOver, because the
    leaver's machine is gone.
  - `ai_activate` resets the side's `ai_group` (ai_groups.cpp:1767).
  - No desync, but it is a state-hash difference (§4).

---

## 3. Other machine-dependent inputs

### 3.1 App → World mutations outside `tick()` and outside the order queue

| file:line | Mutation | Classification |
|---|---|---|
| game_screen.cpp:481 | `player(player_).building_slot.placing = true` before posting kPlace | Direct Player write. Sim readers: only the AI (`ai_rebuild.cpp:403,432,510`, AI sides). `place_building` clears it. Same as the exe (poster writes `+0x2652d`). No desync between remaining machines; **hash difference**. Better: keep it as UI state, or exclude it from the hash |
| sidebar.cpp:403 | the same, on a right click (hold / cancel building) | same |
| game_screen.cpp:495-499 | `unit_slots[].used = true` for a ready airstrike / Death Hand | Direct write. Sim readers: only the AI (ai_groups.cpp:424,487,498). `order_special` clears the slot. Hash difference for `latency` ticks. Same as the exe UI (doc 35 §3.3) |
| sidebar.cpp:368 | the same for saboteur / Fremen | same |
| sidebar.cpp:273 | `purchase_posted = true` (FUN_0045a2b0) | Read only by the UI (sidebar.cpp:324). The AI path sets it in the sim (starport.cpp:190) and `starport_purchase` clears it. Hash difference |
| game_screen.cpp:548,591,619,686,709,717,725,757 | `Unit/Building::flash = 7` | Presentation (no sim reader). Hash difference |
| game_screen.cpp:1448-1458 (draw) | `flash` counted down per drawn frame | Presentation, per frame rate. Hash difference |
| game_screen.cpp:2032 | `pl.money_ticked = false` (local only) | Set for every side by `drip_money`, cleared only for the local side. Hash difference |
| game_screen.cpp:447, 524, 636, 654, 663, 668, 681, 689, 741, 760, 1796, 1801, 1818-1828 | `deselect_all`, `box_select`, `select_unit`, `view_unit`, `select_building_checked`, `next_unit`, `select_group` | Local selection only (see §2). `select_group` also clears other sides' units (groups.cpp:33-35) |
| game_screen.cpp:1816 | `assign_group` writes `Unit::ai_group` | See §2.2. Hash difference |
| game_screen.cpp:2196-2197 | `view_x`, `view_y` | Read only for the worm-sign and thumper sounds (A). Hash difference |
| game_screen.cpp:474, 648, 737, 1749, 1756, 1925; sidebar.cpp:208,216,226,262,275 | `world.voice(...)` | `last_voice_` / `sounds` only |
| game_screen.cpp:1853 / 1887 | `save_state` / `load_state` | Must be disabled in MP (the exe has no MP save) |
| game_screen.cpp:245-295 | set-up: `side_colour`, `campaign`, `net_worms`, `max_crates`, `local_player`, `difficulty`, `init_players`, `skirmish_start`, `set_ai`, `set_script`, `reveal_start` | Every value except `local_player` must be identical on all machines. **`difficulty` comes from each machine's own `gDifficultyLevel` (B1/B4/B5)**. `skirmish_start` and the credits (game_screen.cpp:152-160) are the skirmish set-up, not the MP one (doc 57 §3.4) |

All gameplay commands now go through `world.post(make_order(...))`. The only
other app writes are the six listed above: placing ×2, used ×2,
purchase_posted, and assign_group's `ai_group`.

### 3.2 RNG and clock

- **Seeds.** `game_screen.cpp:115` seeds the campaign with `SDL_GetTicks()`;
  skirmish keeps the running front-end state. MP needs the host's shared seed at
  this point.
- **Draws before the seed.** The thick-spice pattern and crater jitter
  (`game_screen.cpp:102-107`) draw from `rng_` **before** the seed point. In the
  exe, FUN_00466140 also runs before `srand(seed)` (FUN_004470a0), so the
  pattern differs per machine in the original too.
  - The pattern only picks `Cell::tile` for thick spice (`game_map.cpp:88`).
  - No sim code reads `tile` for logic: flags come from the attribute table at
    load. Writers that draw rng (concrete, craters, blooms) are deterministic.
  - So it is presentation, but it is in `save_state` (tile, `thick_pattern`).
    Exclude it from the hash, or send it.
- **Draws after the seed.** Skirmish colour roll (`:127`), AI houses / starts
  (`skirmish.cpp:24,36`), bloom timers at map load (`game_map.cpp:47`). These
  must all happen after the shared seed, in the same order on every machine.
- **Music.** `next_track` (`game_screen.cpp:1992`) draws from the **shared sim
  rng** when a track ends. That is wall-clock and audio dependent. Today it runs
  only in the campaign (`side_ >= 0`). It must never run in MP, or must use its
  own rng. **Answered (2026-10-16, docs/re/57):** the exe's random pick
  (`rand()`) runs only when the "track ended" flag is set, and that flag is
  set only in the campaign (0x470030), so the original never picks music with
  `rand()` in a network game either; ours keeps the same campaign-only rule.
- **Front end.** The front end shares `rng_` (game_screen.hpp:137). Reseed
  after the lobby.
- **Pacing.** `scheduler_.tick_due(SDL_GetTicks())` is pacing only. The
  lockstep layer must gate `tick()` on confirmed ticks.

### 3.3 Floating point

- **Libm.** `std::sin` / `std::cos` build integer step tables: combat.cpp:22-23
  (`step_dx/dy`), aircraft.cpp:20-32 (flight tables, `static const`),
  blooms.cpp:13 (`step_table`). These are libm results, not correctly rounded,
  so MSVC CRT and glibc can differ by 1 ulp. After ×K and truncation this can
  flip an integer at a boundary.
  - Same-platform games are fine.
  - Windows↔Linux cross-play is at risk.
  - Recommend generating the tables once (verified against the exe's x87
    tables) and embedding them as integer constants.
- **Safe.** `std::sqrt` (combat.cpp:603, 1421; skirmish.cpp:49) is IEEE
  correctly rounded. Float `cost_acc`, speed table × double, particles: same
  results with SSE2 / no FMA.
- **Compiler flags.** CMake sets `CMAKE_CXX_EXTENSIONS OFF`, so GCC uses
  `-ffp-contract=off` by default for ISO mode. No `/fp:` or `-march` options
  are set. Recommend pinning explicitly: MSVC `/fp:precise` (and never
  `/fp:fast` or `/arch:AVX2` contraction), GCC/Clang `-ffp-contract=off
  -fno-fast-math`, x86-64 only (no 32-bit x87 builds).
- **Out-of-range casts.** `ftol` = `int(v)` (world.cpp:30, economy.cpp:16) is
  UB for out-of-range values: SSE gives INT_MIN, ARM saturates. Fine on x86-64
  only.

### 3.4 Containers, statics, uninitialised fields

- **Statics.** No mutable statics in `src/sim`. All `static` are
  `constexpr`/`const`; `aircraft.cpp:32` `static const StepTables` is
  immutable once built.
- **Containers.**
  - `std::map<std::string,uint32_t> last_voice_` is keyed by string, so its
    order is deterministic, and it is presentation.
  - No unordered containers, no pointer-keyed maps, no `std::sort` in sim.
  - All iteration is over `std::vector` in index order.
- **Fields.** All `Unit` / `Building` / `Bullet` / `Effect` / `Delivery` /
  `Particle` fields have initialisers (world.hpp:34-203).
- **Data from the app.** The sim reads sprite metrics set by the app
  (`building_frame`, `element_frame_w`, `element_image_h`, `element_frame0_h`)
  for muzzle flash, debris and smoke points. These are effects that occupy pool
  slots (`pool_full` counts effects), so they are deterministic only if every
  machine has identical data files. Validate the data hashes at session start
  (also SPEED/ARMOUR/VARS/CIRCLES/BUILEXP/TECHPOS and the map).
- **Closures.** `PendingOrder::run` (`std::function`, AI closures) is
  deterministic because every machine runs the AI. It is not serialisable, so
  it cannot be sent or saved; that is fine.

---

## 4. `save_state` contents that are presentation-only or per-machine

Exclude these from any state hash (or move them out of `World`):

**Map cells**
- `Cell::state` (shroud), `Cell::radar_known`.
- `Cell::tile` and `thick_pattern` (pre-seed rand, §3.2).

**Units and buildings**
- `Unit::selected`, `Building::selected`.
- `Unit::flash`, `Building::flash`.
- `Unit::ai_group` **of human-controlled sides** (control groups via
  `assign_group`).

**Players and production slots**
- `ProductionSlot::placing` and `ProductionSlot::used` (UI-written for the
  local side).
- `Player::purchase_posted`.
- `Player::shown_buildings`, `shown_units`, `low_money_said`, `money_ticked`
  (`last_money_tick` is deterministic and can stay).

**World members**
- `reveal_counter_` (shroud only; crate "no map" adds to it on one machine).
- `last_voice_`, `power_shown_`, `radar_said_`.
- `won`, `lost` (per-machine result).
- `messages` (content gated on local).
- `view_x`, `view_y`.
- `local_player`.
- `difficulty` should be forced identical; it differs today.

**Deterministic and can stay**

`mission_timer`, `game_over_`, `human_alive_`, `shake_ticks`/`shake_y` (drawn
from rng in the sim), `particles`, `effects`, `side_colour`, `unit_mask_`.

Note: the exe's own sync check compares only one `rand()` value per tick
(doc 57 §2). A full-state hash is stricter; it is useful, but only after the
exclusions above.

---

## 5. Summary of required changes

1. **B1.** Per-side handicap instead of `p == local ? difficulty : 1` when
   networked (economy.cpp:59,64,523,620).
2. **B2.** `create_unit`: skip the guard state 3 in network games
   (placement.cpp:485).
3. **B3.** Move the "lost" sell refusal to the UI poster (orders.cpp:38 →
   game_screen.cpp:559).
4. **B4/B5.** Force `difficulty` to one shared value in MP (aircraft.cpp:429,
   ai.cpp:95). Note both as "the original would desync".
5. **S1-S3.** Carry building ids in `Order` and apply them in `with_selection`
   (order_stop buildings, order_primary, turret orders).
6. **S4.** A queue entry for received records that keeps their `ids`
   (`World::post` overwrites them).
7. **Sin/cos tables.** Use integer constants for cross-platform play; pin the
   FP flags.
8. **Startup order.** Seed after the thick pattern and before map load; no
   music `rand()` in MP; disable save/load in MP; validate data-file hashes.
9. **State hash.** Use the exclusion list in §4.

## Exe gSideId users checked (sim side)

- **Same as ours, voice/shroud/UI only:** 004429b0, 00442960 (MP: handicap),
  00455870 (MP: no guard), 0045a1a0 (poster lost-test), 00456ee0,
  0049f610, 0049e3e0 (difficulty + gSideId, no MP test), 00455510, 00455750,
  00455fc0, 004560f0, 00457350, 00457490, 00457810 (sounds; cases 0x20/0x21
  set the UI flag 0x4f3d24 for MP end orders), 0046c7d0 (MONEYTICK), 00497b90,
  0049ebf0, 00453b90, 0041b250 (difficulty).
- **Shroud / start view / UI loop:** 0044cff0, 0049f4d0, 0044e330, 00456210,
  004563b0, 0049f870, 0046def0, 00495b90, 00496030, 0049d2e0, 0046be50,
  004467e0, 0044aae0, 0044b170, 00448550.
- **Not inspected:** UI/menu functions (004433a0, 0042xxxx draw,
  0047xxxx/0048xxxx menus, 004a57c0..004a5de0 selection helpers).
