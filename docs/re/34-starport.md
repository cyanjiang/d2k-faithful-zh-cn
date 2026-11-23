# 34. Starport: stock, prices, cart, purchase, frigate delivery, AI buying

Target: Dune 2000 1.06 US (`DUNE2000.DAT` image). Everything below was read
from the decompilation of the functions named; "verified" means also checked
against the instructions (`re/disasm.py`) where arithmetic, signedness or
call arguments matter. No open guesses remain (§10).

Builds on (not repeated here): doc 11 §5/§9 (sidebar page 2, controls
10/13/14, page changes), doc 13 §5 (delivery table, launch, carryall drop),
doc 14 §1–§3 (flight step, state 0x15/0x16, drop step, leaving, removal),
doc 19 §3.1 (DEPLOY / REINFORCE events), doc 21 §3.3 and §4 (removal, state
0x1a), doc 22 §3.7, doc 23 §10 (AI groups for delivered units), doc 30 §2.5
(capture). Corrections to those docs are in §9.

## 0. Functions, globals, fields

| Address | Role |
|---|---|
| `FUN_0046e7a0` | builds the Starport list (and the upgrade list), zeroes cart / stock / price (§1) |
| `FUN_0046ddc0` | per-player Starport tick: price refresh, stock growth (§2) |
| `FUN_00456fd0` | order 0x29 handler: add one unit to the cart (§3.2) |
| `FUN_004570d0` | order 0x2a handler: remove one unit from the cart (§3.3) |
| `FUN_00457140` | order 0x2b handler: purchase = queue a frigate delivery (§3.4) |
| `FUN_004572f0` | order 0x2c handler: cancel the cart (§3.5) |
| `FUN_0045a200` / `FUN_0045a260` / `FUN_0045a2b0` / `FUN_0045a310` | post orders 0x29 / 0x2a / 0x2b / 0x2c (`FUN_0045bf00`) |
| `FUN_0046eac0` | refund the Starport money in transit (`+0x24540`), then zero it |
| `FUN_0046eaf0` | zero `+0x24540` |
| `FUN_00443370` | "cart not empty": any `+0x24268[0..59]` ≠ 0 |
| `FUN_00427690` | draws sidebar page 2 (§4.1) |
| `FUN_0046def0` | delivery launch, incl. the FRIGATEIN countdown voices (§5.2) |
| `FUN_00497b90` | unit update: frigate states 7 / 0x15 (§5.3–5.4) |
| `FUN_0049e3e0` | one drop step (§5.5) |
| `FUN_0046d9a0` | primary-slot repair when a building is removed (§6.2) |
| `FUN_0044b1f0` | "set primary" order handler (§6.1) |
| `FUN_00421cf0` | AI BuyUnitsFromStarport (§7) |
| `FUN_00421e50` | AI ShouldIBuyUnit (§7) |
| `FUN_00442bb0` | unit cost after difficulty: `cost · factor / 100` (u32) |
| `FUN_004429b0` | difficulty factor: 75 easy, 100 normal, 125 hard. Single player: local player uses `0x4e8bf0`, every other player 100. Multiplayer: per-player byte `0x6b922e + p·0x3c` |
| `FUN_00448460` → `FUN_004b1810` | `rand()` (MSVC LCG, 15 bits). The two pushed args (file, line) are ignored; the line numbers below only identify the call site |

Player fields (`P = FUN_004a4660(side)`, all saved with the player block,
doc 27):

| Off | Type | Meaning |
|---|---|---|
| +0x24268[60] | u32 | cart count per unit record |
| +0x24358[60] | u32 | stock per unit record |
| +0x24448[60] | u32 | current price per unit record |
| +0x24538 | u32 | price refresh countdown (calls of §2) |
| +0x2453c | u32 | stock growth countdown (calls of §2) |
| +0x24540 | u32 | **money in the cart / in transit**: Σ prices paid for the cart; kept until the frigate finishes |
| +0x260a6 | u16 | primary Starport (object index, 0xFFFF none) |
| +0x262a4 | u8 | purchase order posted, not yet processed (blocks order 0x29 posting) |
| +0x262a5 | u8 | **delivery pending** (icons grey, clicks ignored, AI does not buy) |
| +0x262a8[8] | u32 | the Starport list (unit records, −1 = empty) |
| +0x262ec[10] | 0x38 each | delivery table (doc 13 §5.1) |
| +0x24628[60] | u32 | units owned per record (the frigate check uses record 26) |

VARS.BIN (doc 13 §7, names from the table at `0x4e2730`, verified values):

| Global | Name | 1.06 |
|---|---|---:|
| `0x6b8824` | starportUpdateDelay | 1500 |
| `0x6b8828` | starportStockIncreaseDelay | 1500 |
| `0x6b882c` | starportStockIncreaseProb | 10 |
| `0x6b8830` | starportCostVariationPercent | 25 |
| `0x6b8834` | starportFrigateDelay | 1500 |

Templates globals: `0x6d60e6` = 26 (FRIGATE record), `0x6d60e7` = 12 (MCV
record).

## 1. The Starport list (`FUN_0046e7a0`, verified)

Called for all 8 players at mission set-up (`0x448d6d`), **not** after
loading a saved game (`0x5178bc` set). Per player:

```
list[0..7] = -1
n = 0
for r in 0 .. unitRecordCount-1:                 // record order
    if rec[r].+0x25 == 0: continue               // "starportable" byte
    if some list[i] (i < n) has the same name (+0x01) as r: continue
    if n > 8: fatal "Too many starportable units on side"   // checked before the add (allows a 9th, never happens)
    list[n++] = MyVersionOfUnit(P, rec[r].name)  // FUN_0046c570(name, 0)
upgrade list ... (doc 28)
cart[0..59] = stock[0..59] = price[0..59] = 0
```

`MyVersionOfUnit` (`FUN_0046c570`): the last record of that name whose house
mask has bit `1 << P.house`, else the first record of that name.

**No tech level, prerequisite or house restriction applies to the list.**
In 1.06 exactly 8 names have +0x25 = 1, giving for every house:

| Slot | Name | Record (A / H / O / smugglers) | Base cost |
|---:|---|---|---:|
| 0 | TRIKE | 5 (all houses, also Ordos) | 300 |
| 1 | QUAD | 7 | 400 |
| 2 | HARVESTER | 8 | 1200 |
| 3 | COMBAT_TANK | 9 A / 10 H / 11 O, smugglers (house 5) 11 | 700 |
| 4 | MCV | 12 | 2000 |
| 5 | MISSILE_TANK | 13 (all houses, also Ordos) | 900 |
| 6 | SIEGE_TANK | 15 | 700 |
| 7 | CARRYALL | 19 (both 18 and 19 match every house; the last wins) | 1100 |

(Fremen house 4 → combat tank 9, Emperor 3 → 10, Mercenaries 6 → 11.)

## 2. Prices and stock (`FUN_0046ddc0`, verified)

Called from the game loop (`0x449b1c`) once per tick for **one** player,
`tick & 7`, only when that player is active (`+0x24250`), between
`FUN_0046d180` and `FUN_0046def0`. So each active player runs it every 8
ticks, **whether or not it owns a Starport** (the rand() calls happen
anyway).

```
// price refresh
if P.+0x24538 == 0:
    if any cart[r] != 0 (r = 0..59): return       // also skips the stock part!
    for i in 0..7:                                 // list order
        r = list[i]
        base = UnitCost(r, P.index)                // FUN_00442bb0: cost·factor/100, u32
        v = (u32)(base · 25) / 100                 // u32 imul + div by magic, verified
        price[r] = (rand() % v) − v + base         // rand line 0x88a (2186), side.cpp; u32 %
    P.+0x24538 = 1500 >> 3 = 187
else:
    P.+0x24538 −= 1
// stock growth
if P.+0x2453c != 0: P.+0x2453c −= 1; return
for i in 0..7:
    r = list[i]
    if stock[r] < 5 (u32):
        if (rand() & 100) <= 10: stock[r] += 1     // rand line 0x8a3 (2211); "and eax, 0x64", unsigned compare
P.+0x2453c = 1500 >> 3 = 187
```

Consequences:

- **Prices are always below the base cost:** price ∈ [base − v, base − 1],
  v = 25 % of the difficulty-adjusted cost. MCV at normal: 1500..1999.
  Exactly 8 rand() calls per refresh, in list order.
- **Refresh period:** 188 calls = **1504 ticks** (timer 187, decremented to
  0, refresh on the next call). The first call after mission start
  refreshes at once (timers start at 0, player init `0x46bb8e`), so prices
  are set within the first 8 ticks.
- **Prices never change while the cart is not empty.** When the timer hits
  0 with a non-empty cart, every call returns early and the timer stays 0;
  the stock timer is frozen too. The first call after the cart is emptied
  refreshes the prices.
- **Stock:** starts at 0 for every item. At each stock pass (same 1504-tick
  period, first pass at the first call) each item below 5 gains 1 with
  probability **1/4**: `rand() & 100` keeps bits 2, 5, 6 (values 0, 4, 32,
  36, 64, 68, 96, 100); ≤ 10 means bits 5 and 6 clear. (A `%` was surely
  intended: the text name is "Prob" = 10 %.) Items at 5 consume no rand().
  Max stock 5.
- Items with stock 0 are greyed and cannot be added. Expect ~1 unit per
  item per 6000 ticks.

## 3. The cart and the orders

### 3.1 Sidebar input (doc 11 §5/§9, confirmed in `FUN_004433a0`)

- Page 2 grid: index = `((y − 0x4e4200) / rowH) · 2 + (x > left col + w ? 1 : 0)`;
  no scrolling. Fatal checks "Invalid array index" (> 7) / "Invalid
  unitType" (> 0x3b).
- Left release on an icon, only if `+0x262a5 == 0`: `FUN_0045a200(local,
  list[i])` → order 0x29 (payload +0x24 side, +0x26 record). The poster
  itself refuses while `+0x262a4` is set (purchase posted). No sound.
- Right press on an icon, only if `+0x262a5 == 0`: order 0x2a. No sound.
  Right press outside the grid: deselect all (`FUN_004a5790`).
- PURCHASE (14): page 1; if the cart is not empty: `FUN_0045a2b0` (order
  0x2b, sets `+0x262a4 = 1` when posted) and `*_PORTORDER` (ai_order /
  hi_order / oi_order; `FUN_0046f900(S,O,H, 0,0,0)`).
- MAIN (13) / UPGRADE (9) from page 2: if the cart is not empty: order 0x2c
  and `*_CANCELLED`.
- Tab update each loop (`0x4494c2`): if the local player has no kind-13
  building (`FUN_00442910(local, 13)`, any building of that kind in the
  list) and page = 2 → page 1 and **order 0x2c unconditionally** (no
  cart test, no voice), controls 0–3 = 1, 13/14 = 0; control 10 = 0.
- `S_STARPORTOPTN` (ai_sport) exists in samples.uib but the exe never
  names it.

### 3.2 Add one (order 0x29, `FUN_00456fd0(side, rec)`, verified)

```
total = Σ cart[list[i]] for i in 0..7
if total >= 6: return                    // max 6 units per order
if stock[rec] == 0: return
if cart[rec] >= 5: return                // max 5 of one type (stock ≤ 5 anyway)
if Spend(P, price[rec]):                 // FUN_0046c720 "ModelStarportPick()" (doc 11 §8)
    P.+0x24540 += price[rec]
    cart[rec] += 1
    stock[rec] −= 1
elif side == local player:
    *_LOWMONEY  (FUN_0046f900(S,O,H, 1, 0, 0))
```

**Money is taken when the unit is added to the cart**, through Spend (pending
spice first, then cash, doc 11 §8). No voice on success.

### 3.3 Remove one (order 0x2a, `FUN_004570d0`, verified)

```
if cart[rec] != 0:
    cart[rec] −= 1; stock[rec] += 1
    Refund(P, price[rec])                // FUN_0046c7b0: pending cash += price
    P.+0x24540 −= price[rec]
```

The refund always goes to **cash** (pending cash), even if the purchase was
paid from spice.

### 3.4 Purchase (order 0x2b, `FUN_00457140(side)`, verified)

```
P.+0x262a4 = 0
if P.primaryStarport == 0xFFFF:
    FUN_0046eac0(P)                      // refund +0x24540, zero it
    return                               // cart counts are NOT cleared (see §8 quirk 4)
i = first delivery entry with active (+3) != 1     // checks i > 10 only after the step:
                                                    // with all 10 active it uses entry 10 = +0x2651c
                                                    // (the building production slot). Never happens in practice.
E = entry i: active = 1, started = 0, cell = (0, 0),
    due = tick + 1500 (starportFrigateDelay), type +0x35 = 2
    (+0x36 deploy action is NOT written: stale value from the entry's last use)
k = 0
for r in 0..59:                          // RECORD order, not list order
    repeat cart[r] times: E.records[k++] = r     // fatal "kMaxDeliverySize exceeded" if k > 40
E.records[k] = 0xFF
cart[0..59] = 0                          // stock is not touched (already taken in §3.2)
if k == 0: E.active = 0; return          // +0x262a5 stays as it was
P.+0x262a5 = 1
```

`+0x24540` keeps the paid total until the frigate finishes (§5.4) or the
Starport is lost (§6.2).

### 3.5 Cancel (order 0x2c, `FUN_004572f0`, verified)

```
for r in 0..59:
    stock[r] += cart[r]
    Refund(P, price[r] · cart[r])        // called for every record, also with 0
    cart[r] = 0
P.+0x24540 = 0
```

## 4. Drawing and tooltips

### 4.1 Page 2 (`FUN_00427690`, verified)

For i = 0..7, r = list[i] (no −1 check), x = 501 (even i, `0x4e41fc`) or
563 (odd, `0x4e4204`), y = `0x4e4200` + `0x4e41f8` · (i >> 1):

- icon `0x6d9260[r]`, greyed (`FUN_004326b0`) if stock[r] = 0, or
  `+0x262a5` ≠ 0, or the cart total > 5; else normal (`FUN_004305f0`);
- then **always** the cart count digit `0x6e2df0[cart[r]]` (DATA.R16
  17–26, 8×9 digits "0".."9") at (x + 4, y + 4), keyed (`FUN_00430770`,
  1, 1). So every icon shows a small "0" when none are in the cart.

Stock and price are not drawn on the page.

### 4.2 Tooltip (`FUN_0044b5c0`, doc 11 §11)

Page 2 cell: line 1 = unit name (`0x517408[rec.name]`), line 2 = `"$%d"` of
`price[list[i]]` (the current price; skipped if 0). Drawn at once, left of
the column.

## 5. The frigate delivery

### 5.1 Who queues it

- **Purchase** (§3.4): due = tick + 1500, cell (0,0), +0x36 stale.
- **REINFORCE event** (doc 19 §3.1, `0x45413f`, verified): only if
  `primaryStarport != 0xFFFF` when the event runs. First entry with active
  ≠ 1 (fatal at index ≥ 10 here, correct bound), cell = event (x, y) low
  bytes, started 0, active 1, due = **current tick**, +0x36 = event deploy
  action, type 2, records = the event's list **in event order**, 0xFF.
  `+0x262a5` and `+0x24540` are not touched.

### 5.2 Launch (`FUN_0046def0`, type 2 parts, verified)

Runs for player `tick & 7` (active players only), right after §2, after
the script (`0x453b90`) of the same tick. For each entry 0..9 in order:

```
// countdown voices: local player only, type 2, active, not started
if P is local and E.type == 2 and E.active == 1 and E.started == 0:
    T = 1500
    if E.due > tick:                        // u32 compares
        left = E.due − tick
        voice = left < T/4 (375)  ? FRIGATEIN2      // "2 minutes"  (ai_2min)
              : left < T/2 (750)  ? FRIGATEIN3
              : left < 3T/4 (1125)? FRIGATEIN4
              :                     FRIGATEIN5      // "5 minutes"
    else voice = FRIGATEIN1                         // "1 minute", at launch
    FUN_0046f900(S_, O_, H_, mode 1, limit T/2 = 750, prio 0)
// launch
if E.active == 1 and E.started == 0 and E.due <= tick:
    E.started = 1
    fatal "Delivery going off map" if E.cell outside the map; fatal if E.records[0] == 0xFF
    FUN_0049e6b0(&E.cell) (result unused for type 2)
    type 2:
      if P.owned[26] != 0:                  // a frigate of this side is on the map
          E.started = 0; continue           // retried next time (8 ticks later)
      if P.primaryStarport == 0xFFFF:
          FUN_0046eac0(P); return           // refund +0x24540. The entry stays active + started
                                            // for good (slot leak), +0x262a5 is NOT cleared,
                                            // and the remaining entries are skipped this call
      S = the primary Starport object
      (tx, ty) = (S.x >> 21) + 1, (S.y >> 21) − 2       // signed /65536 then /32 with round-to-zero
      // S.y is the BOTTOM edge (doc 12 §6), Starport height 96 px → (cx+1, cy+1) = centre cell
      facing = FUN_0049e6b0(&tx', &ty', 0)  // 4 cells past the nearest map edge (doc 13 §5.2)
      u = CreateUnit(side, 26, tx', ty', tx', ty', 0, 0)   // FUN_00455870
      if u == 0xFFFF: (entry stays started; nothing else)  // lost delivery
      else:
          P.+0x24780 −= 1; P.+0x24788[26] −= 1  // (+0x24628[26] stays incremented)
          u.x = tx' << 21; u.y = ty' << 21      // edge cell corner, 16.16
          u.dest (+0x48/+0x49) = ((S.x>>21)+1, (S.y>>21)−2) as bytes
          u.+0x54 = u.+0x55 = facing
          P.+0x262a5 = 1
    common (u valid): u.+0x2c = entry index; u.flags |= 0x8000b00
                      (0x20000000 only for type 1 with one unit); SetState(u, 7)
```

Countdown timeline for a purchase (calls every 8 ticks; the limit is per
sample, so each voice plays once): FRIGATEIN5 at the first call (~1500
ticks left), FRIGATEIN4 when < 1125 left, IN3 < 750, IN2 < 375, IN1 at the
launch. The voices say "5 minutes" … "1 minute" but the whole wait is
1500 ticks (75 s at 20 ticks/s). A REINFORCE event plays only FRIGATEIN1.
No `*_REINFORCE`, `*_UNITREADY` or other voice is played anywhere in the
frigate path (launch, drop, finish).

### 5.3 Flight in (state 7)

Flight as doc 14 §1 (record 26 speed 0x48000 = 4.5 px/tick, altitude 60
px). Extra, in the state-7 case (`0x4983xx`, verified): each tick, if the
unit is kind 8, has 0x100 (loaded) and the side has a primary Starport,
that Starport gets **flags |= 0x20** (one cycle of its active animation,
doc 12 §7.3). The flag is re-armed every tick of the flight, so the
Starport animates continuously while the loaded frigate flies, and the
last cycle completes after it stops. Records 46–48: 8 frames × rate 5 = 40
ticks per cycle, only at hp ≥ max/2. **Record 49 (smugglers' Starport) has
no flag 0x40 (record flags 0x700) and rate 0, so it never animates** —
also after capture, since the captor's object copies the old flags (doc
30 §2.5).

Arrival (FlightStep returns 0) → state 2 → delivery branch (doc 14 §2.1):
0x100 set, 0x4000 clear, "to" = dest → state 0x15.

### 5.4 State 0x15 for the frigate (`0x498433…`, verified)

```
if kind == 8:
    if P.primaryStarport == 0xFFFF and !(flags & 0x4000):
        flags = (flags & ~0x200) | 0x4000; speed = rec speed; SetState(0x16); done   // leave
    if P.primaryStarport != 0xFFFF and !(flags & 0x4000):
        c = (S.x>>21)+1, (S.y>>21)−2  (bytes)
        if c != (u.+0x4c, u.+0x4d):         // the frigate's next cell
            SetState(0x16); speed = rec speed; dest = c; done   // re-route (primary changed)
// kind 8 skips the passability test of the carryall
if altitude > 10 px: altitude −= 1 px; done          // 50 ticks from 60 px
r = DropStep(u, side)                               // §5.5
if r == 0:
    if kind == 8: P.+0x262a5 = 0; P.+0x24540 = 0    // FUN_0046eaf0: the money is now spent
    SetState(0x16); flags = (flags & ~0x300) | 0x4000
    if AI side active (0x4f4118 + side·0x1db8 + 1 == 1):
        g = FindGroupForDelivery(u.+0x2c); group flags |= 1; group mode = E.+0x36   (doc 23 §10)
```

Then state 0x16 climbs 1 px/tick to 60 px (50 ticks), state 2 sends it to
the nearest edge (doc 14 §3.2, including the left/top edge quirk) and it
is removed there; `+0x24628[26]` −= 1 at removal, which frees the next
frigate launch.

### 5.5 Drop step for type 2 (`FUN_0049e3e0`, verified)

```
if tick % 8 != 0: return 1                     // global tick
cell = (u.+0x4c, u.+0x4d)                      // = the Starport centre cell
if cellFlags(cell) & 0x3e8:                    // any unit bits; building bit 0x10 ignored
    u.+0x2b += 1 (byte)
    if u.+0x2b < 81: return 1
    E.active = 0; return 0                     // give up: the rest is lost, money too
u.+0x2b = 0
skip records == 0xFE; if 0xFF: E.active = 0; return 0
k = position of the record
if difficulty == 0 (easy, 0x4e8bf0) and k odd and alliance[side][local] != 0:
    goto mark                                  // skipped on easy for non-allies
n = CreateUnit(side, rec, cell, cell, 0, 0)    // fails only on pool full / inactive / foreign building
if n != 0xFFFF:
    v.+0x54 = u.+0x54                          // body facing of the frigate
    if kind(rec) == 1: v.state = v.+0x14 = 0xb; v.+0x58 = FUN_004a5e80(side)
    else: v.+0x14 = 2
    if kind(rec) != 2: SetState(v, 0x1a)       // leave the building (doc 21 §4)
    v.flags |= 0x40000 (0xC0000 if the next record is 0xFF); v.+0x2e = entry
mark: record = 0xFE; return 1
```

- **All units appear on the Starport's centre cell**, one per 8-tick
  boundary, but only when no unit stands on that cell. Each one (except a
  carryall) is put in state 0x1a: it takes a free adjacent cell
  (`FUN_0045dee0`) and drives there (state 8), then goes to its +0x14
  (harvester: harvest, 0xb; others idle). With no free neighbour, state
  0x1a counts +0x25 down and makes a random allied neighbour move aside
  (two rand() calls at unit.cpp lines 0x16cc / 0x16cd, `% 3 − 1` for x then
  y; doc 21 §4). So the next drop happens on the first 8-tick boundary
  after the previous unit left the centre cell.
- A delivered carryall stays at its created state (an aircraft at 60 px)
  with +0x14 = 2.
- The give-up counter shares +0x2b with the common update (doc 14 §2.3:
  +1 every 8th tick, at `tick & 7 = index & 7`, saturating at 255, reset
  only by a shot, which a frigate never fires). Exact give-up time
  (verified from both writers): the counter is 255 on arrival (it starts
  at 0xff, `0x4559dc`, and saturates), or 0 after the last successful
  drop. A blocked check adds 1 (255 wraps to 0), and between two checks
  the common update adds exactly 1. So after a first blocked check the
  value is 0 and after the n-th further one 2n; the check that reaches
  ≥ 81 is n = 41 (82). The frigate gives up **41 × 8 = 328 ticks** after
  the first blocked check of an arrival (42 blocked checks), or 328 ticks
  after its last drop when the centre cell stays blocked from the next
  check on (41 blocked checks; 0 after the drop, then 2, 4, … 82). Our
  port runs both writers (`aircraft.cpp` drop step, the common counter).
- The units of a purchase come out in record order (trike, quad,
  harvester, combat tank, MCV, missile tank, siege tank, carryall); a
  REINFORCE in event order.

### 5.6 A5V1 timeline (data from `_A5V1.MIS`)

- Player (side 0, Atreides, tech 4, 7000 credits) can capture the
  smugglers' Starport (side 5, record 49). C11 = BUILDINGEXISTS side 0
  record 49, C12 = the same for side 5, C1 = TIMER = 15000 (the 10:00
  clock), C23 = flag.
- E23 (C11, !C23): REINFORCE side 0, records **12, 7, 7, 9, 9, 9** (MCV, 2
  quads, 3 Atreides combat tanks); E24 message, E25 SETFLAG C23, E26
  HIDETIMER, all in the same tick. Capture → CreateBuilding fills the
  player's empty `+0x260a6` (doc 30 §2.5), so E23 queues the entry; it
  launches on the first tick with `tick & 7 == 0` at or after the event
  (same tick if the event tick is a multiple of 8), with FRIGATEIN1.
- E27 (C12, C1, !C23): REINFORCE side 5, records 7, 7, 5, 5 at tick 15000
  if the smugglers still own the Starport; E29 then sets C23, so a later
  capture gives the player **no** reinforcement.
- If the player captures while the smugglers' frigate is pending or
  flying: the smugglers' old object runs `FUN_0046d9a0` one tick later
  (doc 30), their slot becomes 0xFFFF, their type-2 entries are
  deactivated and a flying frigate leaves (§5.4). E23 then fires for the
  player only if C23 is still clear (it is not: E29 set it at 15000).
- Smugglers are AI-active (slot 5 active = 1), BuyUnits = 0 in every slot
  of A5V1, so no AI Starport buying here. Their delivered units go through
  GroupNewUnits (doc 23 §10).

## 6. Starport building rules (kind 13)

Records 46 (A, F), 47 (H, E), 48 (O, S, M), 49 (smugglers only): hp 3500,
tech 5, cost 1500, power 150, 3×3 drawn (mask 0x777), occupied mask 0x277
(rows 0–1 full, row 2 only the centre), prerequisites name 9 (OUTPOST)
and 15 (HEAVY_FACTORY).

### 6.1 Primary slot `+0x260a6`

- Set by CreateBuilding when empty (first Starport, also a captured one),
  with flag 0x80 — but a captured building loses 0x80 by the copy (doc 30).
- "Set primary" (order handler `FUN_0044b1f0`, case 0xd): fatal if the slot
  is empty; old primary flags &= ~0x80, slot = new, new |= 0x80. The click
  (`FUN_004432f0`) allows it only when the player owns ≥ 2 buildings of
  that name (kind 13 is in the allowed list). A frigate already flying
  re-routes when it reaches state 0x15 (§5.4).
- Player init (`0x46bafa`): slot = 0xFFFF, `+0x262a4` = `+0x262a5` = 0,
  `+0x24538` = `+0x2453c` = `+0x24540` = 0.

### 6.2 Removal: destroyed, sold or captured (`FUN_0046d9a0`, verified)

When the object is removed (state 0x11 end, doc 21 §3.3; for a capture the
victim's old object one tick later), if it is the primary Starport:

```
slot = FUN_0046e740(name 16, side, idx)   // first other building of the same NAME in the list
if slot != 0xFFFF: that building |= 0x80
else:
    P.+0x262a5 = 0
    FUN_0046eac0(P)                        // refund the money in transit (+0x24540) as cash
    for each delivery entry with type == 2: active = 0     // purchases AND pending REINFORCEs
```

A frigate already flying then leaves at its next state-0x15 check (§5.4),
also mid-drop: units already dropped are kept **and the whole price is
refunded** (§8 quirk 2). Cart counts are not touched here.

## 7. AI buying (`FUN_00421cf0` / `FUN_00421e50`, verified)

Called from the AI update (`0x41b738`) when AI block +0x04 (BuyUnits) is set.

```
BuyUnitsFromStarport(ai):
  if (tick & 0x7f) != 0: return             // every 128 ticks
  if P.primaryStarport == 0xFFFF or P.+0x262a5: return
  types = 0; bought = false
  for i in 0..7:
      r = list[i] (byte)
      if !ShouldIBuyUnit(ai, r) or types >= 6: continue
      types += 1                             // counts wanted types, bought or not
      n = (u8) stock[r]
      if rec[r].kind (+0x2d) == 12 and n >= 1: n = 1
            // BUG: compares the KIND with the MCV RECORD number (0x6d60e7 = 12);
            // kind 12 is FREMEN, so the "one MCV" limit never applies
      while (u32)(spice + cash − price[r]) > ai.+0x1074 (CashStash) and n != 0:
            // u32: when spice + cash < price the difference wraps and passes
          post order 0x29 (side, r)          // FUN_0045a200; money is only taken when processed
          n −= 1; bought = true
  if bought: P.+0x262a5 = 1; post order 0x2b (sets +0x262a4)

ShouldIBuyUnit(ai, r):
  cost = UnitCost(r, side)                   // u32
  f = ai.+0xF68 (f32, 0.60..0.89, doc 22)
  if P.unitListCount (+0x261ac) < 4: f = 1.2f   // 0x4be3ac
  if spice >= capacity − 200 (u32): f = 5.0f    // 0x4be3e0, overrides the previous
  if (x87) (double)cost · f < (double)price[r]: return 0
  if rec[r].+0x0c (primary bullet) != 0xFF: return 1           // any armed unit
  if kind == 1 (harvester) and ai.+0xF54 ("harvesters wanted"): return 1
  if r == 12 (MCV) and P.+0x26098 == 0xFFFF (no primary yard) and P.owned[12] == 0
     and cart[12] == 0 and ai.+0x03 (EnablePractice):
      if (rand() & 7) == 5: return 1         // rand AI.cpp line 0x1775 (6005)
  return 0
```

Float: `fild` of the u32 cost and price (as 64-bit, exact), `fmul` by the
f32 factor in x87 extended precision, `fcompp`, "less" → no. The product
of an integer < 2^24 and a float has at most 48 significant bits, so a port
gets the identical result with `(double)cost * (double)f >= (double)price`;
a float32 product could round differently.

The adds are posted in one go and processed later, each with the full §3.2
checks (total ≤ 6, stock, Spend). The order count can exceed what is
affordable or the 6-unit cap; the excess just fails (no voice for an AI).

## 8. Quirks to keep (vanilla)

1. Stock growth uses `rand() & 100` (25 % chance), not `% 100` (§2).
2. Losing the last Starport (destroyed, sold, captured) refunds the whole
   `+0x24540`, even when the frigate already dropped some units (§6.2).
3. **Double refund:** if the last Starport is lost while the local player
   has a non-empty cart on page 2, `FUN_0046d9a0` refunds `+0x24540`, then
   the tab update posts order 0x2c, which refunds `price × cart` again
   and puts the stock back. Verified: `FUN_0046d9a0` (`0x46dc82`) calls
   `FUN_0046eac0` (refund `+0x24540`, zero it via `FUN_0046eaf0`) and does
   not touch the cart; the tab update (`0x4494cd`, `FUN_00442910(local,
   13)` false, page 2) posts 0x2c; `FUN_004572f0` refunds price × cart for
   each record. Every step is deterministic simulation; the test
   "losing the last Starport with a full cart refunds it twice" runs it.
4. A purchase processed after the Starport is gone (§3.4) refunds
   `+0x24540` but leaves the cart counts; a later cancel refunds again.
5. Launch with no Starport (§5.2) leaves the entry active and started for
   ever (one of the 10 delivery slots is lost) and does not clear
   `+0x262a5`; reachable only if the Starport is lost between queueing and
   launch without `FUN_0046d9a0` deactivating it, i.e. practically never
   for purchases; for a REINFORCE the event already requires a Starport.
6. AI: the MCV limit compares the wrong value (§7); the cash test wraps
   when broke. If every queued add of an AI fails (e.g. it is broke),
   the purchase finds an empty cart, deactivates the entry and leaves the
   AI's own `+0x262a5 = 1` set: **that AI never buys from the Starport
   again** until it loses its last Starport. Verified by a byte-write scan
   of `+0x262a5`: the writers are the AI post (`0x421e32`, 1), the
   purchase (`0x4572dc`, 1, after the empty-cart return), the launch
   (`0x46e34d`, 1), the player init (`0x46bb3c`), the last-Starport loss
   (`0x46dc82`, 0) and the frigate's last drop (`0x49860a`, 0). With no
   entry launched, nothing clears it.
7. The purchase delivery's +0x36 (deploy action, used by AI groups) is
   stale from the entry's previous use (§3.4).
8. A frigate can wait at most one frigate per side: a second delivery
   waits until the first frigate has left the map.
9. Refunds always go to cash, even if the units were paid from spice.

## 9. Corrections and additions to other docs

- **doc 11 §8 table:** `+0x262a5` is "delivery pending" (set when the
  purchase is processed or a frigate launches, cleared when the frigate
  finishes its drops or the last Starport is lost), not "order sent".
  Add `+0x262a4` (purchase order posted), `+0x24538` / `+0x2453c` (price /
  stock countdowns), `+0x24540` (money in transit).
- **doc 11 §9 TODO:** answered: add limits (§3.2), prices (§2). The count
  digit is drawn for every icon, also "0" (§4.1).
- **doc 13 §5.4:** the cell is (x/32 + 1, y/32 − 2) with y = the bottom
  edge, i.e. the Starport's centre cell (cx + 1, cy + 1). FRIGATEIN5 is
  played first and FRIGATEIN1 at launch (§5.2); they are "5 … 1 minutes".
  Dropped harvesters are in state 0x1a with follow-up 0xb.
- **doc 13 §7:** VARS.BIN offsets 0x0c–0x1c are the Starport values (§0).
- **doc 14 §7:** the flag 0x20 on the Starport during the loaded flight is
  confirmed as the active-animation trigger; it is not set during descent
  or unloading, and record 49 cannot animate.
- **doc 19 §3.1:** after "If the Starport is gone at launch time,
  `FUN_0046eac0` runs": the entry then stays active and started for good
  (§8 quirk 5). A REINFORCE entry pending when the last Starport is lost is
  deactivated by `FUN_0046d9a0`.
- **doc 22 §3.7:** `+0x260A6` is the primary Starport (confirmed). "At most
  6 types" counts wanted types, not bought ones; add the MCV-compare bug,
  the u32 cash wrap and the lock-out (§7, §8 quirk 6). `ShouldIBuyUnit`
  details in §7.

## 10. Closed points (2026-10-16)

- The double refund (quirk 3) and the AI lock-out (quirk 6): verified in
  the code (§8); the give-up time: 328 ticks (§5.5).
- **Frigate speed near its dest:** the flight step `FUN_00494fd0` tests
  the unit kind only for 9 and 15 (wings, bombing run); there is no
  frigate case. The frigate launches with flags 0x8000b00 (0x800 and
  0x100 set, no 0x10000), so the generic rule of doc 14 §1.3 applies: it
  slows by 5 % per tick within d² ≤ 16 cells of its dest, down to
  1 px/tick, and snaps back to 4.5 px/tick otherwise.
- **FRIGATEIN5 at the purchase:** the countdown voice is requested in mode
  1, limit 750, ring 0 (`FUN_0046f900(.., 1, 750, 0)`, §5.2). Mode 1 queues
  behind the voice that is playing (doc 59 §3), so it is heard after
  `*_PORTORDER` (mode 0, played at the click), on the first launch call
  (the first tick with `tick & 7` = the side after the purchase order is
  processed). The repeat limit blocks it only if the same sample passed
  less than 750 ticks before (an earlier purchase's countdown).
