# 49: The order queue (1.06)

Every player and AI command (move, attack, build, sell, special weapon, ...)
is built as an order record and posted with `FUN_0045bf00`, then run later
by ProcessOrder (`FUN_00457810`). Docs 09, 21, 22, 23, 29, 35 and 38 carry
an open point on the order-queue delay. This doc answers it. Conventions as
in docs 14 and 40. "Verified" = read in the decompile and checked in the
disassembly or the exe's initial data.

**Result in one line:** in single player every game order runs **2 ticks
after it is posted**, at the **start of the model update** of that tick
(before the units move), in the order it was posted. Orders store unit
**ids taken at post time**. The queue is not saved.

## 0. Function and data map

| Address | Name / meaning |
|---|---|
| `FUN_0045bf00(rec)` | PostOrder: numbers the record, network send in MP, then `FUN_0045b9b0` |
| `FUN_0045b9b0(rec)` | queue the record: game orders (type > 0x12) into a sorted list, system orders (≤ 0x12) into the misc array |
| `FUN_0045a5c0` | returns 0 (stub "drop this order?" test) |
| `FUN_0045a5d0(rec)` | duplicate check for system orders 0x0c / 0x0f / 0x10 only |
| `FUN_0045b840(rec)` | misc array full: reuse a slot or grow by 10 |
| `FUN_0045bcc0(rec)` | MP only: send the record (`FUN_0045b0a0`); not for AI sides' game orders |
| `FUN_0045d230` | ProcessOrderQueue driver: `FUN_0045a540(0)`, `FUN_0045cda0`, then `FUN_0045d120` per list |
| `FUN_0045d120(list)` | run every game order of the list whose tick = now (calls `FUN_00457810`) |
| `FUN_00457810(rec)` | ProcessOrder: `switch (type − 0x13)`, cases 0..0x21 |
| `FUN_0045cda0` | process the misc (system) array: types 9, 0x11 (MP pulse / latency / leave) |
| `FUN_0045a540` | MP latency change: raise queued ticks to at least `0x6b9810` (0 in SP: no-op) |
| `FUN_0044aca0` | model update: `FUN_0045d230` **first**, then `FUN_00458e90` (units), `FUN_0044a9f0`, `FUN_0044aae0`, `FUN_0044abe0` |
| `FUN_0045a7a0` | queue / net init (from `FUN_0045b7e0`, mission set-up `0x448614`): sets the latency |
| `FUN_0045c480` | every 8 s (1-second timer): SP sets latency = `0x4e3b24` again |
| `FUN_0045a6b0` / `FUN_0045a670` | free all lists / one list (load game only, `0x441f03`) |
| `FUN_004481b0` | reset the per-player net/sequence block (load `0x441f08`, mission start `FUN_004470a0` at `0x4470eb`) |

| Global | Meaning |
|---|---|
| `0x5173f4` | game tick counter (+1 at the top of the tick block, `0x449a5e`) |
| `0x4e3ae8` | **order latency** in ticks (static 40; set at mission start, §1.3) |
| `0x4e3b24` | latency for campaign / skirmish, static **2**; overridable by `frig.txt` |
| `0x6b9204 + p·0x3c` | head of game-order list p (linked through rec+0xa8) |
| `0x6b9210 + p·0x3c` | last sequence number posted (reset 0) |
| `0x6b9214 + p·0x3c` | next sequence number to run (reset 1) |
| `0x6b9820` / `0x4e3ae4` | misc array (0xa8 per slot) / its size (static 200) |
| `0x6b9918` | count of posted orders; read only by the debug overlay `FUN_004249b0` (`0x424df0`, "Orders: %d"), which runs only when `0x5178c4` is set, and nothing sets it: no effect |
| `0x4f4118 + s·0x1db8 + 1` | AI-active byte of side s (DoAI runs when set, `0x449a71`) |

## 1. Posting (`FUN_0045bf00`, verified)

### 1.1 The record

0xa8 bytes, built on the stack by the poster (`FUN_00459450` ...). Fields
used by the queue: +0x08 tick (poster writes `0x5173f4`; the queue
overwrites it), +0x0c sequence number, +0x18 id count, +0x23 type, +0x24
side, +0x26 up to 64 u16 unit (or building) ids, +0xa8 next pointer (heap
copy only, 0xac bytes from `FUN_004b06e0`). Order-specific fields as in
docs 09 / 18 / 38 (target side +0x22, target index +0x20, x / y +0x1c /
+0x1e, ...).

### 1.2 Single player path

```
PostOrder(rec):
    posted_count++                                  // 0x6b9918
    rec.seq = ++last_seq[0]                         // every order, AI sides too
    Queue(rec)                                      // FUN_0045b9b0, list 0
Queue(rec), type > 0x12:
    rec.tick = now + latency                        // 0x5173f4 + 0x4e3ae8
    copy into a new heap node; insert into list 0 sorted by (tick, seq)
```

- In SP **all sides share list 0** (`FUN_0045b9b0` uses side only when
  `0x7984c4` is set). With a constant latency the list is plain FIFO.
- **No limit and no duplicate check** for game orders: one heap node per
  post, no cap, no merge (`FUN_0045a5c0` returns 0; `FUN_0045a5d0` only
  looks at types 0x0c / 0x0f / 0x10). Two identical posts run twice.
- The sorted insert never drops a record (checked all branches of
  `FUN_0045b9b0`).
- System orders (type ≤ 0x12: 7 start sync `FUN_00448230`, 9 leave
  `0x44a1a0`, 0x0c pulse `0x449924`, 0x11 rate / latency `FUN_0045c480`)
  are only posted in network games and go to the 200-slot misc array
  (grown by 10 when full, `FUN_0045b840`). Not needed for SP.
  The 0x0c post sits in the pulse block that `0x449878` / `0x449880`
  skip (`je 0x449992`) unless the network flag `0x7984c4` is set.

### 1.3 The latency (verified)

`FUN_0045a7a0` (mission set-up via `FUN_0045b7e0`, `0x448614`, every
mission and every load) switches on gGameType `0x797e34`:

| gGameType | latency `0x4e3ae8` |
|---|---|
| 0 Campaign, 1 Skirmish (switch default) | `0x4e3b24` = **2** |
| 2 Network, 3 Serial | 40 |
| 4 Modem, 5 Internet | 75 |

`FUN_0045c480` (every 8 s from the 1-second timer, doc 02) sets it to
`0x4e3b24` again in SP. Only the MP pulse code (`FUN_0045cda0`, type
0x11) changes it otherwise. `0x4e3b24` is read from `frig.txt` (two
`" %d "` numbers: latency, then `0x4e3b20`) when that file exists in the
current directory; it is a developer file, not in the 1.06 install
(`docs/inventory-1.06.csv`). We ignore it.

**Correction for doc 22 §3 and `src/sim/ai_rebuild.cpp`:** the rebuild
give-up (`FUN_0041f5c0`, `0x41f5ea`) is 10 × max(`0x4e3ae8`, 1) =
**20 passes** in SP, not 400 (the static 40 is overwritten before the
first tick). Our code uses `10u * 40u`.

### 1.4 Multiplayer path (for later)

Each side has its own list (index = rec side). Orders of AI sides
(`0x4f4118 + side·0x1db8 + 1`) get no sequence number and are not sent:
every machine runs the AI itself. Others are numbered from the local
player's counter and sent (`FUN_0045bcc0`). The run step checks the
sequence and reports "ProcessOrderQueue: Model Out Of Sync" when an
order's tick has already passed (`0x4e3be0`).

## 2. Processing (verified)

### 2.1 Where in the tick

Tick block of `FUN_00448550` (doc 33 §5):

1. `0x5173f4` += 1 (`0x449a5e`).
2. DoAI per active side (`FUN_0041b610`, `0x449a7a`).
3. Script `FUN_00453b90`, `FUN_00455480`, blooms `FUN_0044ea70`,
   `FUN_004466e0` (auto-start specials), `FUN_00446690`.
4. Model update `FUN_0044aca0` (`0x449ad8`):
   **`FUN_0045d230` = run the order queue**, then `FUN_00458e90` (every
   unit), then the rest.

So orders run once per **game tick** (not per loop pass), after the AI
and the script of that tick and before any unit moves. The UI
(`FUN_004433a0`) posts during the loop pass, before that pass's tick
(doc 02).

### 2.2 `FUN_0045d120(0)` in SP

```
ai0 = AI-active byte of side 0
loop:
    find the first node with seq == next_seq[0] (or simply the head if ai0)
    if none: return
    if node.tick != now: return                    // not due yet
    if !ai0: next_seq[0]++
    ProcessOrder(node)                             // FUN_00457810
    if !ai0: FUN_0045cda0()                         // misc array, empty in SP
    unlink and free node
```

Every due order runs in this one call, in (tick, seq) order = post order.
ProcessOrder is called from nowhere else (only xref `0x45d195`).

### 2.3 Click to reaction

- UI post while the counter is T → rec.tick = T + 2 → runs in the second
  tick after the click, before the units' update of that tick. At the
  default 20 ticks/s that is 50..100 ms after the click.
- AI post during tick N (counter already N) → runs in tick N + 2, after
  that tick's DoAI. The AI therefore sees the old state for two more
  passes (units not yet moving, production slot still empty).

## 3. What goes through the queue

### 3.1 Every poster calls `FUN_0045bf00` (xrefs verified)

| Type | Poster | ProcessOrder case / handler | UI | AI callers | Ours |
|---|---|---|---|---|---|
| 0x13 move | `FUN_00459450` | 0 | yes | `0041d070` `0041e3c0` `0041f670` `0041fc80` `00420010` `00421fa0` `00422ec0` `00423100` `00423360` | `order_move` |
| 0x14 attack unit | `FUN_004596f0` (armed only) | 1 | yes | `0041d070` `0041fc80` `00420010` `00420500` | `order_attack_unit` |
| 0x15 attack building | `FUN_00459800` | 2 | yes | `0041d070` `0041fc80` `00420010` `00423440` | `order_attack_building` |
| 0x16 attack ground | `FUN_004598d0` | 3 | yes | | `order_attack_ground` |
| 0x17 dock | `FUN_00459540` | 4 | yes | | `order_dock` |
| 0x18 guard | `FUN_00459980` | 5 | yes | `0041c3e0` `0041fea0` | `order_guard` |
| 0x19 scatter | `FUN_00459a20` | 6 `FUN_00457580` (rand) | yes | | `order_scatter` |
| 0x1a retreat | `FUN_00459ab0` | 7 | yes | | `order_retreat` |
| 0x1b alliance | `FUN_00459b50` | 8 | MP | | n/a |
| 0x1c / 0x1d stop | `FUN_00459c40` | 9 `FUN_004576b0` / 10 `FUN_00457710` | yes | | `order_stop` |
| 0x1e / 0x1f turret | `FUN_00459d20` / `FUN_00459de0` | 0xb / 0xc | yes | | `order_turret_attack_*` |
| 0x20 primary | `FUN_00459ea0` | 0xd `FUN_0044b1f0` | yes | | `order_primary` |
| 0x21 repair unit | `FUN_004595e0` / `FUN_00459690` | 0xe | yes | `0041e650` | `order_repair_units` / `order_repair_unit` |
| 0x22 repair building | `FUN_0045a150` | 0xf `FUN_00456fb0` | yes | | `order_repair_building` |
| 0x23 sell | `FUN_0045a1a0` | 0x10 `FUN_00456ee0` | yes | `0041b610` (BESERK) | `order_sell` |
| 0x24 start building | `FUN_00459f30` | 0x11 `FUN_00455fc0` | yes | `0041ebc0` `0041f560` | `start_building` |
| 0x25 hold / cancel building | `FUN_00459f80` | 0x12 `FUN_004560f0` | yes | `0041ef30` `0041f5c0` | `hold_or_cancel_building` |
| 0x26 place | `FUN_00459fd0` | 0x13 | yes | `0041ef30` | `place_building` |
| 0x27 start unit | `FUN_0045a0b0` | 0x14 `FUN_00455510` | yes | `0041bfe0` `004203d0` | `start_unit` |
| 0x28 hold / cancel unit | `FUN_0045a100` | 0x15 `FUN_00455750` | yes | | `hold_or_cancel_unit` |
| 0x29..0x2c Starport | `FUN_0045a200` `0045a260` `0045a2b0` `0045a310` | 0x16..0x19 | yes | `00421cf0` (0x29, 0x2b) | `starport_*` |
| 0x2c (Starport lost) | `FUN_0045a310` from the loop `0x4494ef` | 0x19 | loop pass | | `starport_lost` path |
| 0x2d / 0x2e upgrade | `FUN_0045a350` / `FUN_0045a3a0` | 0x1a / 0x1b | yes | `00421c40` (0x2d) | `start_upgrade` / `hold_or_cancel_upgrade` |
| 0x2f deploy / action | `FUN_0045a3f0` | 0x1c | yes | `0041c3e0` `00422140` | `order_deploy` |
| 0x30 special weapon | `FUN_0045a440` | 0x1d (rand for Death Hand, `0x458a7d`) | yes | `00422270` | `order_special` |
| 0x31..0x34 | `FUN_0046ea50` ... | 0x1e..0x21 (end of MP game) | MP | | n/a |

AI orders use the same posters, so they wait 2 ticks too (doc 23 §3:
select, post, clear the selection).

### 3.2 Direct handler calls (bypass the queue, immediate, verified xrefs)

| Caller | Handler | Ours |
|---|---|---|
| MonitorBuildings `FUN_0041eab0` (`0x41eb14`) | sell `FUN_00456ee0` | `ai_monitor_buildings` → `order_sell` (keep immediate) |
| DoOverrides `FUN_00422c20` (`0x422e38`) | sell `FUN_00456ee0` | `ai_groups.cpp` DoOverrides `order_sell` (keep immediate) |
| auto-start specials `FUN_004466e0` (`0x446730`) | start unit `FUN_00455510` | `auto_start_specials` → `start_unit` (keep immediate) |
| mission set-up `FUN_00469790` | lay concrete `FUN_00456210` | not an order |

The BESERK sell (`FUN_0041b610`) **is** queued (`FUN_0045a1a0`), unlike
the two above. AI code that sets unit states itself (BESERK SetState 2,
doc 23 §6.1) is also immediate.

## 4. Consequences

### 4.1 Selection: post time (verified)

The poster copies the ids (`unit+0x8c`, the per-side slot index) of the
side's selected units (+0x19, state ≠ 0x11, ≤ 64, plus poster filters
such as "armed" for 0x14) into the record. ProcessOrder then walks those
ids: slot in use (`+0x90 == 1`), `FUN_0049f4a0(u, 1)` (can take orders),
then the handler's own tests (target still valid: in use and not
`0x400140`, etc.). Changing the selection after the click does not
change the order. A unit that dies in between is skipped. The ids are
**slot indices**, but a posted unit was not dying, and a unit spends at
least 11 updates in state 0x11 before its slot is freed (docs/re/50
section 5), so no own id can name a new unit within the 2 ticks. The
target of a record (+0x20 index, +0x22 side) can: the UI may click a
dying object, and the handler 2 ticks later acts on whatever object of
that kind holds the slot then. Ours keeps (side, slot) in the record the
same way (`World::encode_refs` / `decode_refs`, docs/re/50 section 6).

Each poster takes at most **64** ids (the loop bound `0x40`): the first
64 selected units of the side's list that are not dying, with its own
filter (0x14 armed, rec +0x0c != -1, `FUN_004596f0`; 0x21 not infantry,
`FUN_004595e0`); the stop poster `FUN_00459c40` then adds the selected
buildings to the same list while it holds fewer than 64 (doc 38 §6).

Order 0x1d still carries the unit ids before the building ids (doc 38
§6), unchanged by this doc.

### 4.2 RNG

Handlers that call `rand()` (scatter `FUN_00457580` one per try, doc 25;
Death Hand accuracy in case 0x1d `0x458a7d` / `0x458a7e`) draw at
the start of the model update of tick T + 2, **after** that tick's DoAI,
script and bloom draws. Ours draws them between ticks (UI) or inside
DoAI (AI). Every such order therefore shifts the shared `rand()` stream
relative to the exe. The queue fixes that order.

Every ProcessOrder case traced to `rand()` (`0x448460` / `0x4b1810`):
case 6 (0x19 scatter, `FUN_00457580` at `0x457fb1`), case 0x13 (0x26
place: the concrete `FUN_00456210` at `0x4587ca`, and CreateBuilding at
`0x45882f` only through that concrete), case 0x1d (0x30 special: the Death
Hand at `0x458b0f` / `0x458b3a`), case 0x1e (0x31 retire: `FUN_0046e6b0`
→ `FUN_0049ca10`) and case 0x1f (0x32: `FUN_0046e9c0` → TakeOver_AI).
CreateUnit `FUN_00455870` draws nothing, so units created in handlers
(Fremen, Saboteur) do not; neither do the start / hold / sell / repair
handlers (the sound path reaches `rand()` only when `sound.rs` failed to
load). Ours runs every handler at that same point (`run_orders`).

### 4.3 AI behaviour that depends on the delay

- An AI condition that is checked every pass can post the same order 2
  or 3 times before the first one runs. Each copy runs (no merge), e.g.
  SetState again (clears +0x74..+0x7a, doc 09 §3).
- DoBuildCarryalls (doc 29) posts 0x27 every tick while CanBuildUnit
  passes: the copies are harmless. `FUN_00455510` returns at once when
  the record is already in a build slot and not on hold (verified), so
  the doc 29 open point is answered: no second carryall.
- Doc 22: "the slot read after `FUN_0041ef30` is still the old one" is
  confirmed, and lasts 2 ticks.
- UI: a second click within 2 ticks (e.g. special weapon, Starport
  purchase) posts a second order; the handler sees the state left by the
  first (docs 34 §6, 35 §3.4 already account for this).

### 4.4 Save, load, restart (verified)

- Not saved: no save / load code reads the lists. Loading a game frees
  all lists (`FUN_0045a6b0`, `0x441f03`) and resets the sequence block
  (`FUN_004481b0`, `0x441f08`), so orders posted in the last 2 ticks
  before a save are lost on load (in the running game they still run).
- Leaving the game loop (`0x44a1f1`, on the way to the single `ret` of
  `FUN_00448550` at `0x44a544`) frees all 8 lists and the misc array
  (`FUN_0045a6b0`); the next mission start (`FUN_004470a0`, called at
  `0x4486a9`) resets the sequence block (`0x4470eb`), and the set-up sets
  the tick counter `0x5173f4` to 0 (`0x44861e`). No order survives into
  the next mission; ours clears the queue at mission start.

## 5. Our code today

- UI: `src/app/game_screen.cpp` (`:363` place, `:384` special, `:396`..
  `:628` mouse orders, `:1597`..`:1614` keys) and `src/app/sidebar.cpp`
  (build / hold / Starport / upgrade) call `World::order_*` /
  `start_*` / `starport_*` directly between ticks; they run at once.
  Input is handled before `world_->tick()` in the pass (`:2191`), like
  the exe.
- AI: `src/sim/ai.cpp`, `ai_groups.cpp`, `ai_rebuild.cpp`, `starport.cpp`
  (AI purchase), `economy.cpp` (`ai_upgrade_buildings`) call the same
  functions inside `tick_ai()`; they run at once, before the unit loop of
  the same tick.
- `World::tick()` (`world.cpp:760`): counter, reveal, timer, messages,
  `tick_ai`, `tick_script`, `tick_cell_objects`, `auto_start_specials`,
  `tick_shake`, then the unit loop. The exe's order point is between
  `tick_shake()` and the unit loop.
- Each `order_*` reads the selection when called and mixes the poster's
  filters with the handler's tests.

Net effect today: player orders act 1 tick early, AI orders 2 ticks
early; RNG order differs for scatter / specials; AI re-post behaviour
(§4.3) is missing.

## 6. Proposed design (minimal)

```
struct PendingOrder {               // one exe record
    uint32_t tick;                  // post counter + kOrderLatency
    uint32_t seq;                   // post order (FIFO tie-break)
    uint8_t type, side;
    // order fields: x, y, target side / id / building, record, kind, ...
    std::vector<int> ids;           // ≤ 64, captured at post time
};
std::deque<PendingOrder> pending_orders_;   // World, FIFO (latency constant)
constexpr uint32_t kOrderLatency = 2;       // 0x4e3b24 (campaign / skirmish)

void World::post_order(PendingOrder o);     // FUN_0045bf00
void World::run_orders();                   // FUN_0045d230: while front.tick == tick_count_ run it
```

- Split every `order_*` into **post** (the exe poster: who is taken, the
  id list, the poster's own filters and asserts) and **run** (the
  ProcessOrder case: per-id checks, SetState, fields). Keep the public
  `order_*` names as the posters so UI and AI call sites do not change.
- Call `run_orders()` in `World::tick()` right after `tick_shake()` and
  before the unit loop.
- Keep the three bypasses (§3.2) as direct calls of the run part.
- Run part resolves ids with `find_unit(id)` and must also check
  `owner == side` (a converted unit changed side; in the exe the old
  side's slot is freed).
- Clear `pending_orders_` at mission start and on load; do not save it
  (as the exe). Bump nothing in the save format.

## 7. Implementation checklist

1. Add `PendingOrder`, `pending_orders_`, `post_order`, `run_orders`,
   `kOrderLatency = 2` (comment: `0x4e3b24`, `FUN_0045a7a0`; MP values
   40 / 75 for later).
2. `World::tick()`: `run_orders()` after `tick_shake()`, before the unit
   loop (`FUN_0044aca0` → `FUN_0045d230` first).
3. Route through the queue (post / run split), types 0x13..0x30 of §3.1:
   `order_move`, `order_attack_unit`, `order_attack_building`,
   `order_attack_ground`, `order_dock`, `order_guard`, `order_scatter`,
   `order_retreat`, `order_stop` (0x1c and 0x1d as two records),
   `order_turret_attack_unit`, `order_turret_attack_building`,
   `order_primary`, `order_repair_units`, `order_repair_unit`,
   `order_repair_building`, `order_sell`, `start_building`,
   `hold_or_cancel_building`, `place_building`, `start_unit`,
   `hold_or_cancel_unit`, `starport_add` / `remove` / `purchase` /
   `cancel` (and the Starport-lost cancel), `start_upgrade`,
   `hold_or_cancel_upgrade`, `order_deploy`, `order_special`.
4. Keep direct (run part, no queue): MonitorBuildings sell, DoOverrides
   sell, `auto_start_specials` start, BESERK SetState. BESERK **sell**
   goes through the queue.
5. Check each poster's filters against the exe poster before moving
   code (e.g. 0x14 armed only, 0x15 kinds 1 / 6 / 0xE and own-side 3 / 4,
   0x21 not infantry `0x77e261`, 0x2b / 0x29 UI guards `purchase_posted`).
   Things the UI tests before posting (cursor rules, doc 46) stay in the
   UI.
6. Fix the rebuild give-up to 10 × 2 = 20 passes (`ai_rebuild.cpp`,
   `10u * 40u`) and the "400 passes (40 in single player)" line in doc 22.
7. UI feedback stays at post time (voice `unit_response`, flash, click
   puff), as the exe does them in `FUN_004433a0`. But some feedback is in
   the handlers and so moves 2 ticks later: 0x27 start unit plays
   `?_TRAINING` / `?_LOWMONEY` for the local player inside `FUN_00455510`
   (verified), case 0x13 (0x26 place) plays a sound (`FUN_0046f540`).
   The handlers that reach a sound or voice: 0x1b ally (chat voice),
   0x24 / 0x25 / 0x27 / 0x28 (start / hold, `FUN_00455fc0` / `4560f0` /
   `455510` / `455750`), 0x26 place (S_BUILDCONC or S_BUILDUP, local test at
   `0x4587da`), 0x23 sell (`FUN_00456ee0`), 0x29 / 0x2d / 0x2e
   (`FUN_00456fd0` / `457350` / `457490`), 0x2f deploy of kind 4
   (positional S_STEALTHUP at `0x458a6a`, no local test), 0x30
   (`FUN_0049e860`), 0x31 (through the destruction). Ours plays each inside
   the same handler, so 2 ticks after the click as well.
8. Tests: `tests/sim/test_world.cpp` calls orders 45 times and expects
   them at once. Add a helper that posts and runs 2 ticks (or a test-only
   `flush_orders()`), and update the counted-tick expectations (golden
   A1V1 move, A1V2 AI attack / A2V2 rebuild timings shift by 2 ticks).
   New tests: a click acts in tick T + 2 and not T + 1; a selection
   change after posting does not change the order; a unit killed before
   the run is skipped; AI double post runs twice; load clears pending.
9. Clear the queue in mission set-up and load; no save-format change.
10. Docs: point the open points in docs 09 §4, 22 §2, 23 §3, 29, 35 §3.4 here;
    `docs/parity.md` (movement, AI rows: "order queue delay").

## 8. Open

- Settled from the exe (2026-10-16): a move click starts the units 2
  ticks later (the order is posted for tick + the queue latency and the
  unit steps in that tick's object pass; ours does the same, order-queue
  tests). A one-tick difference (50 ms at 20 ticks/s) cannot be told
  from screen captures, so the code reading stands as the evidence.
