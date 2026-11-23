# 50. The object pool and `ReachedUnitLimit`

Source: DUNE2000.DAT 1.06 US. The decompiles come from the GhidraMCP server
(`re/ghidra_http.py`) and the loops from `re/disasm.py`. Status: verified.

## 1. The pool

Each side record (base `0x798870`, stride `0x26990`, returned by
`FUN_004a4660`) holds **1000 object slots** of `0x94` bytes. The player
reset `0x46ba3d..0x46ba94` builds them.

- Slot *i* starts at side `+0x30 + i·0x94` (`lea edi, [esi+0x30]` at
  `0x46ba2b`; every reader adds `+0x30 + field`, e.g. the kind `+0x90` is
  read as side `+0xc0 + i·0x94`). *Corrected: this doc said `+0xAC`.*
- The reset chains the slots through `+0x7c` / `+0x80`: slot *k*'s `+0x80`
  is slot *k−1*, its `+0x7c` slot *k+1*. The free-list head (side `+4`) is
  **slot 999** (`0x46ba61`), the tail (side `+0`) slot 0.
- The index loop (`0x46ba6c..0x46ba94`, `ebp < 0x3e8`) writes the index *i*
  as a word at `+0x8c`, `+0x84` / `+0x88` = side `+0` / `+4`, and zeroes
  `+0x00..+0x7b`. Creation returns `+0x8c` as the object id.
- The save fix-up `FUN_00441f30` walks the same 1000 slots per side; a save
  keeps the side records (lists and all) as they are.

`FUN_0046bd40` (fastcall, ECX = the side record) pops the **head** of the
free list (null when empty). `FUN_0046bd60(obj, kind)` sets `+0x90` = kind
(1 unit, 2 building, 3 bullet, 4 explosion) and appends the object at the
newest end of the side's in-use list of that kind (units side `+8`,
buildings `+0x10`, bullets `+0x18`, explosions `+0x20`; walked from the
oldest through `+0x7c`). `FUN_0046bdb0(obj)` (ECX = side) unlinks it, sets
`+0x90` = 0, makes it the **new head** of the free list and zeroes
`+0x00..+0x7b` (`rep stosd`, 0x1f dwords). `+0x8c` stays.

So the free list is **LIFO per side**: a fresh side hands out 999, 998,
997, ...; a freed slot is the next one the side hands out.

**One pool serves four kinds of object.** These are the only callers of
`FUN_0046bd40`:

| Caller | Object | On a full pool |
|---|---|---|
| `FUN_00455870` CreateUnit | unit | `ReachedUnitLimit` message, returns `0xffff` |
| `FUN_004563b0` CreateBuilding | building | `ReachedUnitLimit` message, returns `0xffff` |
| `FUN_00456a30` ModelAddBullet | bullet | returns `0xffff`, no message |
| `FUN_00456df0` explosion / effect | effect | nothing created, no message |

The "unit limit" is therefore **1000 live objects per side**: the side's
units, buildings, bullets in flight and explosions together. There is no
other unit cap in the exe. The pool belongs to the side that owns the
object. Spice-bloom effects are owned by side `tick & 7` (doc 31), so they
count against that side.

## 2. Order of the checks

- **CreateUnit:** first the ground-spawn test (a building of another owner
  on the spawn cell, doc 12 §7), then "player inactive" (`+0x24250`), then
  the pool.
- **CreateBuilding:** first "player inactive", then the pool.

When the pool is full, the message is
`AddMessage(FUN_0048dcc0(FUN_0048dc10("ReachedUnitLimit", 1, -1)))`, the
same path as the other text lines (doc 47 B). **There is no side test:**
the line appears on the local screen even when the AI's creation is the
one that fails. No sound plays.

## 3. Callers on failure

Each caller already handles a `0xffff` result. The pool failure takes the
same path as the other failures:

- **Order 0x26, place a building:** the slot is cleared before
  CreateBuilding, and `S_BUILDUP` plays for the local player either way.
  The building is lost.
- **MCV deploy:** the MCV is still removed (doc 30 §1.5).
- **Engineer capture:** the tiles are already cleared (doc 30).
- **Deviator conversion:** the old unit stays as it is (doc 40).
- **Delivery and Starport carryall / frigate:** the entry stays started
  and the delivery is lost (doc 34 §5).

## 4. Quirk: a bullet that never flies leaks its slot

ModelAddBullet pops its slot **before** it computes the distance. When the
distance is 0 and the bullet type has no flag `0x100`, it returns `0xffff`
**without linking the slot back** into any list. That slot is gone for the
rest of the mission. The save keeps the side record as it is, so a load
keeps the leak.

## 5. When a slot comes back: the model pass

Slots are freed in one place only, the model update `FUN_00458e90`. It goes
**side by side** (`DAT_00798628` sides): the side's units, then its
buildings, bullets and explosions, each list from the oldest object. The
next object is read (`+0x7c`) **before** each update, and an object whose
update returns 0 is freed **at once** (`FUN_0046bdb0`), before the next
object of the list updates:

| List | Update | Returns 0 when |
|---|---|---|
| units | `FUN_00497b90` | state 0x11 and `+0x2f` == 0 (`0x499e31`); `+0x2f` is 10 for vehicles, 0x32 for infantry (CreateUnit `0x4559b4` / `0x4559ba`, decremented only at `0x49a09c`), so a unit spends at least 11 updates in 0x11. This is the only return-0 path. |
| buildings | `FUN_0049b7f0` | state 0x11 with `+0x3b` == 0 (the case-0x11 tail, after the area hit, `FUN_0046d9a0` and the cell clean-up). Then `FUN_0046be50` (the lists). |
| bullets | `FUN_0049be60` | impact, off the map, a wall hit |
| explosions | `FUN_0049dd80` | the last frame / blink count |

So:

- A unit created by a unit's update (a drop, a conversion to this side) is
  updated in the same pass unless the updating unit was the newest one;
  a unit created for side *s* while an earlier side's pass runs (a
  Deviator conversion by a bullet) is updated in side *s*'s pass of the
  same tick.
- Objects created inside an update (the impact explosion of a bullet, the
  death explosion of a unit) are allocated **before** that update frees
  its own slot, so they never take it.
- **A destroyed building's slot** returns in its own side's building pass
  of the update in which `+0x3b` has reached 0: destroyed (`+0x3b` = 30,
  `FUN_0049ca10`) 31 updates after the kill; sold (`+0x3b` = 0, flag 8,
  `FUN_0044b1d0`) the update after it enters 0x11; the old object of a
  captured building (`+0x3b` = 1, `FUN_0046d6d0`) the second update. The
  survivors (`FUN_004977a0` at `+0x3b` == 0x19) are created before that,
  so they never take the building's slot.

## 6. Stored slot indices (who can see a reused slot)

A reference is a (side, index) pair; the readers look at the slot's kind
(`+0x90`) and state, never at a serial number. A new object of the same
side can take a freed slot in the very next allocation. Every object stays
in state 0x11 for at least one update of every per-tick holder before its
slot is freed (units 11+, buildings 1+: a holder later in the pass sees
0x11 in the dying tick, an earlier one in the next tick before the free),
so a holder that drops the reference on 0x11 every tick never sees reuse.
Audit of every slot dereference in the exe (`index * 0x94` addressing,
GetUnit `FUN_004a46b0`, GetBuilding `FUN_004a4740`, GetNextUnitOnTile
`FUN_004a54a0`):

| Stored ref | Holder's check | Verdict |
|---|---|---|
| unit target `+0x0c` / side `+0x26` (not carryalls) | head `0x497d93`, every update of every unit (aircraft and dying included): kind 1 in 0x11 / flags, kind 2 in 0x11, other kind → `0xffff` | safe |
| unit attacker `+0x32` / side `+0x30` | head `0x497ce5`: kind 1 / 2 in 0x11 → `0xffff`; other kind logged ("Attacked by strange type") and kept. Set only from live firers (below) | safe |
| bullet firer `+0x14` (own side) | bullet head `0x49be92`, every update before the delay: dying or not a unit / building → `0xffff`; the impact passes it as the attacker | safe |
| bullet homing target `+0x16` / `+0x19` | the homing block: target in 0x11 → `0xffff` | safe |
| carryall cargo `+0x0c`, ferry requests side `+0x26611..` | the 8-tick monitor `FUN_0046d180` cancels on a dying cargo within its 10+ dying updates; KillUnit cancels a dying carryall's entries | safe |
| building turret target `+0x2c` / side `+0x3c` | building head `0x49b90e..0x49b983`, every update | safe (an inactive target side keeps it: `remove_side` only) |
| primary buildings side `+0x26098 + 2·kind` | reassigned by `FUN_0046d9a0` before the free | safe |
| AI group target `+0x2c` / `+0x2e` | MonitorGroups clears dying targets every DoAI pass | safe |
| AI `+0x1d40` selected index | set and used in one DoAI pass | safe |
| effect `+0x08` | always `0xffff` in 1.06 (only writers: create `0x456e70`, the update's clear) | unreachable |
| order record unit ids (2-tick latency) | posters skip 0x11 units; 11+ dying updates > 2 ticks | safe |
| **building first attacker `+0x50` / `+0x52`** | never cleared (capture resets it); set by the damage `0x49d65e` (first non-kill hit by another side, any firer index, a turret's too) and on the kill. DoDefence `0x41c2e8` reads the slot: kind 1 and no flag 0x40, **no state test**, sends a defender to that slot's cell | **reuse observable**: after the first attacker is gone, the defence goes to whichever unit of that side holds the slot; a turret first attacker blocks the defence until a unit gets its slot |
| **harvester refinery `+0x58`** (own side) | never cleared; GetBuilding + record kind 4, no state test (`0x4997d2`, `0x499a54`, `0x499ba7`, `0x44d7c3`, least-used `0x4a5f00`) | **reuse observable**: a dying refinery still counts; a new own refinery in the freed slot is used instead of the nearest search |
| **unit repair pad `+0x5a`** (own side) | never cleared; GetBuilding + kind 6 (`0x49a4a8`), passability `0x45daa4` | **reuse observable**, as the refinery |
| **AI initial building entry** (`+0x5ae` index, `+0x5b9` alive) | written once at activation (`0x41ea10`); MonitorBuildings (`0x41eb59`, `tick & 7 == side`) tests the slot's `+0x30` dword == 0x11, no kind test | **reuse observable**: a sold / captured building is dying for only one or two DoAI passes and is mostly missed; the entry then fires for whichever building of the side holds the slot when that one dies |
| order record target / building ids | handlers test the kind only (0x14 / 0x15 / 0xd / 0xf / 0x10 ...); a UI click on a dying object (the lookups `FUN_004a5270` / `FUN_004a5640` do not skip 0x11) can name a slot freed before the order runs | theoretical: needs a new object of that kind in the slot within the 2 ticks (e.g. a place order earlier in the same batch). Ours names the old object (a freed one does nothing); the dock (0x17) and repair (0x21) records go through the slot as the exe |
| alliance byte read with side `0xff` (AI, docs/re/53 §11) | `0x79892f + 8·s` lies in side 0's slots 0 / 1 | byte 0 unless side 0 holds 999+ objects at once (not modelled) |

## 7. Our port

- `World::pool_free_` (one stack per side, back = head; `fresh_pools`
  999..0), `pool_pop` (FUN_0046bd40) at every creation (`create_unit`,
  `add_map_unit`, the delivery carryall, `create_building`, `add_bullet`,
  `spawn_effect` / `spawn_effect_at`), `pool_push` (FUN_0046bdb0) right
  after the update that ends the object. `pool_full` = empty stack. A
  never-flying bullet pops and never pushes.
- `World::tick()` runs side by side: the side's units (creation order, the
  "next read before the update" rule), then `tick_buildings(s)`,
  `tick_bullets(s)`, `tick_effects(s)`; each frees the slot of an object
  whose update ended it at once.
- `Unit::slot` / `Building::slot` (+0x8c) drive every stagger (`& 7`,
  `% T`, the repath waits, the AI tick slots, the shimmer / repair-blink
  phase). `Unit::index` stays our unique id for references that are safe
  (§6).
- The observable references store the slot and resolve it like the exe:
  `Building::attacker` (+0x52, with `attacker_side`), `Unit::refinery`,
  `Unit::repair_pad` (GetBuilding: `refinery_of` / `repair_pad_of`), the AI
  entry bytes (`building_in_slot`).
- The heads: unit attacker / target (`tick_unit`), bullet firer
  (`tick_bullet`).
- Saves (version 21) keep every slot and the free lists; older saves get
  fresh slots per side (`upgrade_pools`).
- Tests: `tests/sim/test_pool.cpp`.
