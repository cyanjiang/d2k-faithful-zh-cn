# 57. Multiplayer: lockstep, sync check, session and front end (1.06)

Source: `DUNE2000.DAT` 1.06 US, decompiled with the GhidraMCP server
(`re/ghidra_http.py`), loops and switch tables read with `re/disasm.py`,
UI data from `Data\UI_DATA\*.uil`, `menus.uib`, `text.uib` and
`re/dump_ui_tables.py`. Related docs: 02 (tick pattern), 04 (menu flow),
49 (order queue), 51 / 52 (skirmish set-up and end), 55 (lobby setters).
"Verified" = read in the decompile and checked in the disassembly or data.
Every value here is read from the code; none needed a running network
game. Points about DirectPlay's own behaviour are marked "not applicable":
our transport replaces it (CLAUDE.md section 10).

**Summary.** Dune 2000 multiplayer is a peer-to-peer **lockstep** of the
single-player engine. Every machine runs the whole simulation, including
all AI sides. Only human orders travel: each order is stamped with an
**execution tick = now + latency** and a per-player **sequence number**,
broadcast to every peer, and run by everybody at that tick, side by side
in side order. The latency starts at 40 ticks (LAN / serial) or 75
(modem / Internet) and is **adapted every 9 seconds** by the host from
measured round-trip times, through a latency order that is itself
scheduled in lockstep. A machine that runs ahead of a peer's confirmed
tick stops ticking (the game freezes, UI still alive); after a grace
period a "Connection lost.. Retrying" box counts down and the silent
player is dropped. The **sync check** compares one `rand()` value per
tick between machines; a mismatch shows "Game is out of sync" and ends
the game. The host picks the shared `srand` seed (clock in ms) and sends
it with the game settings before the start.

## 0. Function and data map

| Address | Name / role |
|---|---|
| `FUN_00448550` | game loop (doc 02). MP parts: start sync call `0x448deb`, "Network Timeout section" `0x449209`, popups `0x4496b2`, GetNetworkOrders / ProcessPulseSignal `0x449888..0x4498a0`, pulse send `0x449982`, per-tick sync record `0x449a15`, quit sequence `0x44a114..` |
| `FUN_004470a0` | "SetModelLatency": pre-game handshake, shared seed (§3.2) |
| `FUN_00448230` | start sync ("Starting Wait", order 7) (§3.3) |
| `FUN_0045a7a0` | net / queue init per game: latency presets (§1.5) |
| `FUN_0045bf00` | PostOrder (doc 49): sequence number, send, queue |
| `FUN_0045bcc0` | broadcast one record (orders, pulse 0x0c, latency 0x11) |
| `FUN_0045be00` | send one record to one player (pong 0x0d, 0x10, chat, handshake) |
| `FUN_0045b0a0` | message send with optional reliability (§5) |
| `FUN_0045b390` | receive pump and resend of un-acked messages |
| `FUN_0045ad80` | accept one incoming message: ack, duplicate filter |
| `FUN_0045b510` | pop one received message |
| `FUN_0045c0e0` | **GetNetworkOrders**: decode messages into order records |
| `FUN_0045bfa0` | per-player contiguous sequence tracking |
| `FUN_0045bb50` | queue a received record (per-side list, or misc array) |
| `FUN_0045c740` | **ProcessPulseSignal**: misc array (pulse, pong, leave, speed, rate, chat) + stall detection |
| `FUN_0045d230` / `FUN_0045d120` | run due orders, per side (doc 49 §2) |
| `FUN_0045cda0` | run system orders 9 (leave) and 0x11 (latency / rate) at their tick |
| `FUN_0045a540` | raise queued ticks to the floor `0x6b9810` after a latency change |
| `FUN_0045d390` | every 9 s: report loop rate and ping to the host |
| `FUN_0045c480` | host: compute new rate cap and latency, post order 0x11 |
| `FUN_0045d350` | ping value reported (quirk, §1.5) |
| `FUN_0045aab0` | **sync check** (§2) |
| `FUN_0045aa90` | clear the sync table (from `FUN_0045b7e0`) |
| `FUN_004249b0` | developer overlay (`0x5178c4`): OOS tick, ORDER / PULSE STALL, net stats |
| `FUN_00459b50` | ally / break-ally order 0x1b (key A) |
| `FUN_0047bae0` | in-game chat send (record type 0x12) |
| `FUN_00492e20` / `FUN_00492eb0` | UI commands LeftGame (5) / AbortGame (6) |
| `FUN_0046ea50` | post order 0x31 (retire) or not; sets the quit kind `0x797df8` |
| `FUN_0046e9c0` | LetAITakeOver (side AI-active = 1) |
| `FUN_0046e6b0` | destroy all units (not worms) and buildings of a side, side inactive |
| `FUN_0048e820` | lobby UI message handler (settings, join, start) |
| `FUN_004a16a0` / `FUN_004a2710` / `FUN_004a07a0` | DirectPlay wrapper: open session, Send, EnumPlayers |

| Global | Meaning |
|---|---|
| `0x7984c4` | network game flag (set on EnterLobby, `FUN_00479b30`) |
| `0x797e34` | gGameType: 2 Network (IPX/TCP lobby), 3 Serial, 4 Modem, 5 Internet |
| `0x798490` | WigNet (network wrapper) object; byte +0 = **I am host**; +0x920 my DirectPlay id |
| `0x797f78` | DirectPlay id of the host |
| `0x7984c0` | number of human players |
| `0x798544` | gSideId (local side; host = 0) |
| `0x5173f4` | game tick |
| `0x4e3ae8` | order latency (ticks) |
| `0x6b9810` | latency floor tick (queued orders never run earlier) |
| `0x4e3b1c` | resend timeout (ms) |
| `0x5179c0` | sync value of the current tick (u16) |
| `0x6b8af0` | sync table: 150 × {tick, value, latency}, −1 = empty |
| `0x4e3b2c` | first out-of-sync tick (−1 = none) |
| `0x4db9e0` | end status (8 = none yet, §2.3) |
| `0x6b9888` | tick-pattern stall adjust (200 = stalled) |
| `0x6b97a0` / `0x6b93e8` | passes stalled / passes before the timeout state |
| `0x6b98a0` / `0x6b93f0` / `0x6b93d8` / `0x6b9460` | timeout state / suspect player / seconds in it / seconds before drop |
| `0x6b9830` | passes until the next pulse |
| `0x798514` / `0x798518` | loop passes this second / in the last second |
| `0x797df8` | quit kind sent in my leave order (0 AI takes over, 1 remove, 0xFF won) |
| `0x798830[8][8]` | alliance table, 1 = hostile (doc 52 §2.2) |

**Per-player record** `0x6b91f8 + p·0x3c` (p = side, humans only),
reset by `FUN_004481b0`:

| Offset | Address (p = 0) | Meaning | Reset |
|---|---|---|---|
| +0x00 | `0x6b91f8` | last **confirmed tick** of p (from its pulses) | 0 |
| +0x04 | `0x6b91fc` | p's loop passes per second (report) | 60 |
| +0x08 | `0x6b9200` | p's ping in ms (report) | 2000 |
| +0x0C | `0x6b9204` | head of p's order list (doc 49) | 0 |
| +0x10 | `0x6b9208` | p's DirectPlay id | 0 |
| +0x18 | `0x6b9210` | local p: last sequence posted; remote p: highest contiguous sequence received | 0 |
| +0x1C | `0x6b9214` | next sequence to run | 1 |
| +0x20 | `0x6b9218` | last sequence run | 0 |
| +0x24 / +0x28 | `0x6b921c` / `0x6b9220` | out-of-order sequences received (array, count) | 0 |
| +0x2C | `0x6b9224` | tick when p was last heard | 0 |
| +0x30 | `0x6b9228` | tick of p's leave | −1 |
| +0x34 / +0x35 / +0x36 | `0x6b922c` / `0x6b922d` / `0x6b922e` | house / colour / handicap | 0 |
| +0x37 | `0x6b922f` | p is an active human in this game | 0 |
| +0x3A | `0x6b9232` | p has left (leave message received) | 0 |

Names are a separate array, `0x798630 + p·0x3c`.

## 1. In-game lockstep

### 1.1 One loop pass in a network game (verified, `FUN_00448550`)

1. Draw, menus, "Network Timeout section" (§1.7), dev overlay.
2. **UI input** `FUN_004433a0`: the player's clicks post orders
   (PostOrder, §1.2). Not while a menu covers the game.
3. **GetNetworkOrders** `FUN_0045c0e0` (§1.3).
4. **ProcessPulseSignal** `FUN_0045c740` (§1.6, §1.7).
5. **Pulse send** when the pulse counter reaches 0 (§1.6).
6. **At most one tick**, if the tick pattern allows it (doc 02, §1.8).
   In MP the tick block starts with:
   `FUN_0045aab0(tick, sync, latency)` (record this tick's sync value),
   then **`sync = rand()`** (debug line 0x19f8), then the 9-second rate
   update `FUN_0045c480` when the host has all reports (`0x6b9874`), then
   `tick += 1` and the normal tick (DoAI, script, model update with the
   order run first, doc 49 §2.1).
7. `0x798514 += 1` (loop pass count for the 1-second timer).

Unlike single player, **the game never pauses under a menu** in MP:
steps 3 to 6 run even when a menu is open (the `else` branch at the end of
the pass jumps back into them; doc 20 §1.3). Only step 2 is skipped.

### 1.2 Posting and sending an order (verified)

PostOrder `FUN_0045bf00` (doc 49 §1) in MP:

- **AI sides' game orders** (type ≥ 0x13 and the side's AI-active byte
  set): no sequence number, not sent. Every machine runs the same AI, so
  it produces the same orders.
- **Human game orders** (type ≥ 0x13) and the system orders **9 (leave)**
  and **0x11 (latency / rate)**: `rec.seq = ++0x6b9210[gSideId]` (one
  counter per player, shared by these three kinds), then
  `FUN_0045bcc0` broadcasts the record: for type > 0x12 it first writes
  `rec.tick = now + latency`, message id 1000, size `0x28 + 2·count`,
  **reliable**. Then it is queued locally (`FUN_0045b9b0`, the same tick).
- Other system types are not numbered.

Queueing in MP uses **one list per side** (`0x6b9204 + side·0x3c`),
sorted by (tick, seq); system orders ≤ 0x12 go to the shared misc array
(doc 49 §1.2).

So the order delay in MP is **the current latency** (40 / 75 at start,
then adapted), not 2, and it applies to **AI orders too** (AI sides post
through the same queue with `tick = now + latency`).

### 1.3 GetNetworkOrders (`FUN_0045c0e0`, verified)

Drains every received message (`FUN_0045b510`) and turns it into a
record by message id:

| Id | Record | Handling |
|---|---|---|
| 1000 | the sent record | type 9 or > 0x12: drop if the side is not an active human (`+0x37`); `0x6b9224[side] = now`; `tick = max(tick, 0x6b9810)`. Types > 0x34 dropped. `+0x10` := sender's DirectPlay id (not for 9). Types 9 / > 0x12: sequence bookkeeping `FUN_0045bfa0`. Then `FUN_0045bb50` (queue; types 7 and 8 are ignored there) |
| 0x3EA | pulse → misc type 0x0C | §1.6 |
| 0x3EB | rate report → misc type 0x0E | §1.5 (host only uses it) |
| 0x3EC | → misc type 0x0F | **never sent in 1.06**: no instruction stores 0x3EC anywhere (immediate scan of `.text`: only the compares at `0x45ae24` and `0x45c37c`), and nothing handles misc 0x0F. Unreachable |
| 0x3ED | pong → misc type 0x0D | §1.5 |
| 0x3EE | → misc type 0x10 | **never sent in 1.06**: only `FUN_0045be00` writes 0x3EE (`0x45bea3`), for a record of type 0x10, and its five callers pass types 1 and 6 (handshake `0x4473d4` / `0x447c5a`), 0x0D (pong `0x45c853`), 0x0A (leave ack `0x45c900`) and 0x12 (chat `0x47bcd2`). Unreachable |
| 0x3EF | latency / rate change → misc type 0x11 | only if the sender is active; `tick = max(tick, 0x6b9810)`; sequence bookkeeping, queue |
| 10, 0x14, 0x1E, 0x3C, 0x46 | wrapper notifications | refresh the menu (`FUN_00474b60(1)`); 0x3C logs "I Have Become the Host" |

`FUN_0045bfa0` keeps, per remote player, the highest **contiguous**
sequence received (`0x6b9210[p]`) plus a list of sequences that arrived
early. This is what makes a pulse "confirm" a tick (§1.6).

### 1.4 Running the orders (verified, `FUN_0045d230`, `FUN_0045d120`)

Each tick, first thing in the model update:

1. `FUN_0045a540`: every queued record (game lists, and misc types 9 /
   0x11) with `tick < 0x6b9810` is raised to `0x6b9810`.
2. `FUN_0045cda0`: system orders 9 / 0x11 due now (§1.5, §3.8).
3. For side = 0 .. humans + AI players − 1, when the side is an active
   human or has AI: `FUN_0045d120(side)`. Human list: take the node whose
   seq is the side's next sequence; if its tick is now, run it
   (ProcessOrder), advance the sequence, then `FUN_0045cda0` again (a
   leave or latency order numbered between two game orders runs between
   them). AI list: by tick only (as SP).

So all machines run, in each tick: side 0's due orders in sequence
order, then side 1's, and so on. **The run order differs from single
player** (one list, post order) and must be copied exactly.

If the next-sequence order of a side has a tick **already passed** (it
arrived late), `FUN_0045d120` formats "ProcessOrderQueue() Model Out Of
Sync %d %d" into gWigNetError (`0x797768`) and opens **WIGNET_ERROR**
(`netError.uil`) with the pause flag (`FUN_0045d2c0`). That side's list is
then blocked for good (its head never matches the tick again).
`netError.uil` offers only OK: `WigNetError`, gGameState 1, MAINMENU
(doc 58 §6, the page table).
This should not happen while the stall rule (§1.7) works.

### 1.5 Latency: start value, measurement, adaptation (verified)

**Start** (`FUN_0045a7a0`, every game start):

| gGameType | latency `0x4e3ae8` | stall passes `0x6b93e8` | drop after `0x6b9460` s | resend ms `0x4e3b1c` | half-ping clamp `0x4e3aec`..`0x4e3af0` | factor `0x4e3af4` |
|---|---|---|---|---|---|---|
| 2 Network, 3 Serial | 40 | 180 | 6 | 300 | 150..1500 | 2.0 |
| 4 Modem | 75 | 300 | 30 | 1000 | 300..1500 | 1.8 |
| 5 Internet | 75 | 300 | 30 | 1000 | 300..1500 | 2.1 |
| 0 / 1 (SP) | 2 (`0x4e3b24`) | 10 | 10 | 1000 | 100..1000 | 2.0 |

**Ping.** Every 25th pulse carries `GetTickCount()` (`0x4dfb0c` counter,
§1.6). A peer receiving it answers at once with a pong (message 0x3ED,
unreliable, to the sender) holding the same stamp. On the pong:
`rtt_sum[p] += now_ms − stamp`, `rtt_n[p] += 1` (`0x6b94d0`, `0x6b94d4`).

**Reports.** The 1-second `WM_TIMER` (`0x4a3796`, doc 02) sums loop
passes; every 9th second in MP it calls `FUN_0045d390(average loop
passes per second over those 9 s)` (the sum divided by ECX = 9,
`0x4a37be`) instead of the SP `FUN_0045c480`:
a client sends message 0x3EB {side, rate, ping} reliably to the host; the
host stores its own values. Then the RTT sums are cleared. The ping sent
is `FUN_0045d350`, which **computes the largest average RTT over the
active players but returns the last one computed** (it returns the loop
temporary, not the maximum). Quirk, game-visible only through the
latency. With **no pong at all** in those 9 s it returns the caller's
ECX, which its first instructions push and load into that temporary
(`0x45d350` `push ecx`, `0x45d351` `mov eax, [esp]`): on a **client**
CL is cleared with the wrapper's host byte and CX loaded with the rate's
low word (`0x45d398` / `0x45d3a2`), so the ping reported is **the loop
rate** (e.g. 60); the report's fields are 16-bit (`0x45d3a7`, `0x45d3bf`).
On the **host** CL is loaded with gSideId (`0x45d3e4`), so its own ping
is **its side number** (0, or the new host's side after a hand-over),
stored as a whole dword (`0x45d417`). Ours: `Lockstep::reported_ping`.
When the host has a report from every active human (`FUN_0045c440`), it
sets `0x6b9874`; the next tick calls `FUN_0045c480`.

**Host computation** `FUN_0045c480` (only the host; returns at once on
a client):

```
minRate = min over active humans of reported loop rate;  minRate = max(minRate, 8)
maxPing = max over active humans of reported ping
if only one active human: halfPingMin := 100, maxPing := 2
newCap = 0 (no change)
if minRate != cap or 0x6b98d0:
    if minRate < cap:            newCap = minRate               // drop at once
    elif minRate - cap >= 4:     newCap = cap + 3               // rise slowly
resendMs = 2 * clamp(maxPing / 2, halfPingMin, halfPingMax)    // also the new 0x4e3b1c
newLat = 0
if no latency change pending (0x6b988c) and I have not left:
    h   = clamp(maxPing / 2, halfPingMin, halfPingMax)          // ms, one way
    x   = min(ticksPerSecond(speed), cap)                       // doc 02 buckets 8..25, else cap
    L   = ftol(((x * h) / 1000 + 1) * factor)                   // integer division, then float32 × factor
    if L < lat: L = max(L, lat * 72 / 100)
    if L > lat: L = min(L, lat * 100 / 72)
    if L != lat: newLat = L; 0x6b988c = 1
post order 0x11 { tick = now + max(newLat, lat), +0x14 newLat, +0x1A resendMs, +0x1C newCap }
```

Example: LAN, speed 75 (20 ticks/s), small ping: h = 150 → L =
(3 + 1)·2 = 8. From 40 the latency steps down by at most 28 % per update:
40 → 28 → 20 → 14 → 10 → 8 (10·72/100 = 7 is below 8, so 8), one step
every 9 s (about 45 s to settle). The factors are the float32 constants
`0x40000000` (2.0, game types 2 and 3), `0x3FE66666` (1.8, modem) and
`0x40066666` (2.1, Internet) written by `FUN_0045a7a0`.

**Applying** (`FUN_0045cda0`, in lockstep, at the order's tick and
sequence): `cap = newCap` if non-zero (rebuild the tick pattern),
`0x4e3b1c = resendMs`; if newLat ≠ 0: floor `0x6b9810 = tick +
max(lat, newLat)`, then after the loop `lat = newLat`, `0x6b988c = 0`,
and `FUN_0045a540` raises already queued orders to the floor. Effect: a
latency **decrease** never lets a new order overtake an older one; orders
posted just before the change wait until the floor. A 0x11 that arrives
after its tick is applied late and sets the overlay flag "Latency is out
of sync" (`0x6b9890`). Settled from the code: that branch
(`FUN_0045cda0`, `now > rec.tick`) counts the record in the sender's
sequence and applies only a latency rise and its floor, not the cap or
resend time, and only when the sender is not marked left (`+0x3A`). For a sender that is active and not left it **cannot happen**:
the 0x11 is numbered in the host's sequence and posted at tick t for
t + max(lat, newLat) ≥ t + lat, while a peer may run tick `now` only up
to the host's confirmed tick + lat − 2 (§1.7), and a pulse confirms a
tick only once every order numbered up to it has arrived (§1.6); the
floor raises any order posted between t and the change to the change's
own floor, so an increase cannot overtake either. A late 0x11 therefore
comes only from a player already marked left, and that one is dropped
without effect: the same case as §1.7.

The leave order 9 of the **host** triggers host migration: the lowest
remaining active human becomes host (`FUN_0049fde0`), and that machine's
stored pings are reset to the latency value. The others learn the new
host from its next 0x11 record: a client whose host id is 0 takes the
sender of the next 0x11 as host (`FUN_0045cda0`, the 0x11 branch). Only
a leave whose sender id (+0x10) is the host's counts; a timeout drop of
the host is posted by a survivor, so in the exe DirectPlay's own host
migration (wrapper message 0x3C) picks the new host then; ours uses the
lowest-side rule for that case too (design doc §6.3). Verified 2026-10-16 in
`FUN_0045cda0`: when the leaving side is the host (`0x797f78`), the host
global becomes 0; a machine whose lower-numbered lobby records all have
+0x2F = 0 calls `FUN_0049fde0(1, humans, 0, 1)`, which is only the
Wignet "Set Mode" of the transport (mode byte, +0x91C max players, two
flags), sets every stored ping (`0x6b9200 + i·0x3C`) to the latency
`0x4e3ae8` and clears `0x6b98d0`. Nothing in the simulation changes; our
`Lockstep::after_tick` does the same when `host_side()` changes.

### 1.6 Pulses and the confirmed tick (verified)

Every `loop passes per second / 8` passes (about 8 per second;
`0x6b9830`), each machine broadcasts a **pulse**, message 0x3EA,
**unreliable**: {tick, side (3 bits) + last sequence posted (29 bits),
sync value of the current tick, optional ping stamp (bit 31 of the tick
word)}, 16 or 20 bytes. It is never queued locally.

On receipt (`FUN_0045c740`, misc type 0x0C), for an active, not-left
player p:

- `0x6b9224[p] = now` (last heard), `0x6b8ad0[p] = pulse tick`.
- If all of p's orders up to the pulse's sequence have arrived
  (`seq ≤ 0x6b9210[p]`), **p's confirmed tick** `0x6b91f8[p] = max(own,
  pulse tick)`. Since p posts every order for at least `tick + latency`,
  p's orders for ticks ≤ confirmed + latency are all here.
- Sync check of the pulse tick and value (§2).
- Pong if it carries a stamp.

### 1.7 Stall, timeout and drop (verified)

End of `FUN_0045c740`, every pass:

```
m = min confirmed tick over the other active, not-left humans (with its player q)
if none: no stall
elif m - 2 + latency < now:            // we may run tick `now` only while now <= m + latency - 2
    stalled; overlay "PULSE STALL" if 0x6b8ad0[q] == m else "ORDER STALL"
    if adjust == 0: adjust = 200, rebuild pattern      // x - 200 < 0: no ticks at all
    else stalledPasses += 1
else if adjust != 0: adjust = 0, rebuild, clear timeout state
if stalledPasses > 0x6b93e8 (180 / 300):
    first time: timeout state on, suspect = q, seconds = 0
    a different player becomes the slowest: reset the state
    if seconds > 0x6b9460 (6 / 30) and not done yet:
        post order 9 "q leaves" (+0x14 = 1: remove its side), our quit kind 0x797df8 = 0
        message "You have lost connection with one of the players" (LostOnePlayer)
```

`seconds` (`0x6b93d8`) is counted by the 1-second timer while the
timeout state is on.

What the player sees:

- During the stall the picture and the UI keep running (scroll, menus,
  chat), but no tick runs: units freeze.
- After 180 passes (about 3 s at 60 passes/s; 300 for modem / Internet)
  plus one second, the "Network Timeout section" opens **MP_TIMEOUT**
  (`mTimeout.uil`): "Connection lost.." and "Retrying .. %d seconds"
  counting `6 − seconds` (30 for modem). For Internet games
  **INT_TIMEOUT** (`iTimeout.uil`, "Attempting to reconnect players...").
  The box closes by itself when the peer comes back. **Correction (doc
  58 §6.3):** the box has no Cancel button. The `BUTTON` header line is
  missing in the 1.06 `mTimeout.uil`, so the button is never created; the
  player can only wait for the reconnect or the drop.
- When the countdown is over, the slow player is dropped (above) and the
  game goes on.

The drop is itself an order of the dropping machine, scheduled at
`now + latency`, so every survivor removes q at the same tick. A machine
that sees a leave record in the misc list (its own post or a received
one; `0x45cae0..0x45cb10`) marks the leaving player as **left at once**
(`+0x3A = 1`, leave tick `+0x30`), acks it (type 10) and, when stalled,
clears the timeout state. The stall rule skips left players, so the
survivors tick again right away, up to the leave's tick (found by the
phase-1 harness, 2026-10-14: without it the drop never ran). But q's
last orders may have reached some survivors and not others.
**Settled from the code: yes, that can desync them.** Until the leave
runs at its tick L, q is still an active human (`+0x37`), so
`FUN_0045d120` runs q's orders that are due before L on every machine
that has them; a survivor that misses one of them (q sent it to one peer
and then died, or one link failed) has a gap in q's sequence and runs
none of q's later orders. Nothing re-sends q's orders from a survivor.
The sync check (§2) then ends the game with "Game is out of sync" as soon
as the difference reaches a `rand()` draw. Orders of q with ticks after L
never run (q is no longer an active human and, removed by kind 1, has no
AI). Ours copies this rule; the design doc keeps it.

### 1.8 Tick pacing and game speed (verified)

- Tick pattern as in doc 02, with `x = min(bucket(speed), cap) −
  adjust`. `adjust` = 200 while stalled (§1.7).
- `FUN_0045b790`: in MP the pattern does **not wrap** after its 100th
  slot until the 1-second timer sets `0x6b986c`: at most one pattern
  (100 passes) per second.
- **cap** (`0x6b9864`) is the shared rate cap. It starts at 60
  (`FUN_0045a7a0`) and only the host's order 0x11 changes it: it falls at
  once to the slowest player's loop rate and rises by 3 per 9-second
  update. So the slowest PC limits everyone's tick rate.
- **Speed** (`0x4e3b18`): the host's value is sent in the start message
  (type 5, +0x22) and set on every client. Any player may change it in
  the options menu: the setter `0x45b590` stores the value at once and
  posts system order **0x0B** {tick = now + latency, +0x14 speed},
  queued locally and broadcast. `FUN_0045c740` applies it (sets speed,
  rebuilds) at the first pass with `now ≥ tick`. Pacing does not affect
  the simulation state, so this is not tick-exact. The changing machine
  already uses the new value at its next pattern rebuild: the setter
  writes `0x4e3b18` before posting (`0x45b5a8`), and the rebuild
  `FUN_0045b610` reads that global (`0x45b631`). Values above 100 become
  100, and an unchanged value posts nothing (`0x45b59b`).
- The tick count per second can therefore differ between machines for a
  moment; the stall rule keeps them within `latency − 2` ticks.

### 1.9 The shared RNG (verified)

`srand` is `FUN_004b1800`; callers: `0x447609` (campaign), `0x447e9c`
(MP client), `0x4480f0` (MP host), `0x479b88` (EnterLobby,
`FUN_00479b30`: srand(clock) before the lobby; irrelevant for the game).

- The **host** takes `seed = clock in ms` (`FUN_004b1e60`; 0 with the
  replay flag `0x6b98fc`), calls `srand(seed)` and sends the seed in
  start message type 5 (+8). Each **client** calls `srand(seed)` when it
  receives it. No `rand()` runs between that and the game set-up on any
  machine. Verified: `rand()` is reached only through the thunk `0x448460`;
  of its 48 callers only the set-up (`FUN_00469790`, the game loop's
  colour rolls), the crater / thick-spice tables (`FUN_00466140`, before
  the seed), JoinSession (`FUN_0047ad60`, before) and the music track pick
  `FUN_00481490` are outside the simulation. The handshake loop
  (`FUN_004470a0`, its pump `FUN_00448490`) reaches the music pick through
  the UI manager (`FUN_004712c0` → `FUN_00481460`), but the "track ended"
  flag `0x795600` is only set when gGameType is 0 (campaign, `0x470030`),
  so in a network game it draws nothing.
- Before the seed, `FUN_00466140` (64 crater jitter bytes, doc 26 §4)
  has already used each machine's own RNG state, so **those tables can
  differ between machines**. The same function also rolls the 64-byte
  thick-spice pattern. Both are visual only:
  - the crater jitter is used only by the crater drawing;
  - the thick-spice pattern only picks the tile picture. The cell flags
    (spice level, the thick-spice attributes) do not depend on it.

  Checked in our code and by a two-machine test (2026-10-15). Verified in
  the exe too: the pattern `0x790510` is read only by the re-smoothing
  `FUN_0044ec50` (`0x44ed5e`), which writes tile words from the cells'
  spice flags and never reads a tile; the crater jitter (`0x502ed8`,
  `0x503f88`) is read only by the map draw (`0x423e9f`..`0x423f2b`). The
  cell tile word is read only by the map draw, the developer overlay
  (`0x424e74`), the terrain tooltip (`0x44ba61`, `0x44ba93`), the
  harvester dust colour (`FUN_0045d730`, a fixed number of `rand()` draws
  whatever the tile) and the set-up scan of the map file's own tiles
  (`0x4697fe`, before any smoothing). Tile attributes become cell flags
  only at that set-up (`0x46980e`).
- After the seed, in this order (all machines identical): MP colours of
  the non-human slots (§3.4), bloom rolls of the map scan, AI houses
  (`rand() % 3`), side 0's start (`rand() % 8`) and the rest as in doc 52
  §2.
- **One extra `rand()` per tick** in MP: the sync value, drawn at the
  start of each tick before DoAI (right after `0x449a15`, debug line 0x19f8). The RNG stream
  in MP therefore differs from a skirmish with the same seed.

### 1.10 Order records on the wire (verified)

The 0xA8 record of doc 49 is sent as its first `0x28 + 2·count` bytes:

| Offset | Meaning |
|---|---|
| +0x00 | transport sequence (reliable messages; 0 = unreliable) |
| +0x04 | u16 message id 1000 |
| +0x08 | execution tick |
| +0x0C | order sequence (per player) |
| +0x10 | sender DirectPlay id (filled by the receiver) |
| +0x14 .. +0x22 | order fields (doc 49 §1.1; system orders: see their sections) |
| +0x18 | u16 id count (for chat: text length / 2 + 1) |
| +0x23 | type |
| +0x24 | side |
| +0x26 | up to 64 u16 ids (chat: text) |

Order types 0x13..0x30 are the doc 49 table. MP-only types:

| Type | Poster | Effect |
|---|---|---|
| 0x07 | `FUN_00448230` | start sync "I am ready" (sent directly, not queued) |
| 0x09 | game end `0x44a1a0`, drop `0x45ccfa`, start-sync drop `0x44841a` | leave: +0x14 kind, +0x1A leaving side (§3.8) |
| 0x0B | speed setter `0x45b590` | game speed change |
| 0x0C | loop `0x449982` | pulse (sent directly, not queued; corrects doc 49 §1.2) |
| 0x11 | `FUN_0045c480` | latency / rate cap / resend timeout |
| 0x12 | `FUN_0047bae0` | chat line (UI only, not lockstepped) |
| 0x1B | `FUN_00459b50` | ally / break alliance (§3.6) |
| 0x31 | `FUN_0046ea50` | I retire (surrender): ProcessOrder case 0x1E |
| 0x32 | none in 1.06 | ProcessOrder case 0x1F: LetAITakeOver. No poster: the queue insert's only caller `FUN_0045bf00` is posted with 0x09, 0x11, 0x13–0x31, 0x33, 0x34, never 0x32 (doc 57a §1). Unreachable; ours keeps the case |
| 0x33 / 0x34 | UI `0x445e81`, Internet only | ladder result flags `0x4f3d24` / `0x4f3d28` |
| 0x01..0x06 | pre-game handshake (§3.2) | settings, players, seed |

## 2. Sync check

### 2.1 What is compared (verified, `FUN_0045aab0`)

Not a checksum of the game state: **the 16-bit value `rand()` returned
at the start of each tick** (`0x5179c0`). Since combat, AI, scatter,
blooms and so on all draw from the one RNG, any divergence that touches
a random draw changes it within a few ticks. A divergence that never
reaches a `rand()` call (e.g. a different unit position with no random
consequence) is not detected.

Pairs (tick, value) go into a 150-entry table `0x6b8af0`:

- locally, every tick: (current tick, its value), before the value for
  the next tick is drawn;
- from every pulse: (pulse tick, pulse value).

`FUN_0045aab0(tick, value)`: if an entry with the same tick exists, an
equal value ends the call; a different value is a **mismatch**. Otherwise
the pair takes the first empty entry, or replaces the entry with the
smallest tick. So a peer's value is compared whether it arrives before
or after the local tick.

How often: every pulse (about 8 per second per peer), for the tick in
the pulse. Ticks between pulses are not compared.

### 2.2 On mismatch (verified)

1. `0x4e3b2c` = the tick. From then on the check does nothing (no more
   records).
2. If no end status yet (`0x4db9e0 == 8`): end status = 3 (OUT OF
   SYNC), and the message line shows **"Game is out of sync"**
   (`ModelOutOfSync`, `FUN_00425720`).
3. Next pass (popup checks, `0x4496b2`): the **OUT_OF_SYNC** box
   (`oosync.uil`) opens over the game: "Connection failure : Game is out
   of sync", button OK = `QuitAndJumpToMenu ABORT_MP_GAME` and
   gGameState Briefing (back to MAINMENU plus the lobby of the game type,
   doc 04). The game keeps running behind it until OK. Nothing is sent
   to the other players; each machine detects it on its own.

The developer overlay (`0x5178c4`) prints "Network Out of Sync at tick
%d". `network.log` (`FUN_0045abb0`) is written only when `0x6b98ac` is
set, and nothing writes that flag: dead debug code. The only log call is
"ERROR: Non system order on misc queue".

### 2.3 End status `0x4db9e0` (for the Internet result report)

0 ended normally, 1 I surrendered, 2 opponent surrendered, 3 out of
sync, 4 connection lost, 5 / 6 wash, 7 draw, 8 none yet (table
`0x40dbf4`, strings in `FUN_0040d2d0`). Only Internet games send it
(`FUN_0040d2d0` "SendGameResultsToServer"); for the others only the
"≠ 8" test of §2.2 matters.

## 3. Players and session

### 3.1 Limits

- 8 sides in the simulation (as skirmish). Every MP map has 8 start
  markers (doc 52 §2.3).
- The lobby asserts **humans + AI players ≤ 6** ("Human+AIplayers
  greater than 6! Error.", `FUN_0048e820` `0x48fbf6`).
- gMaxPlayers `0x4e8bec` (static 4) is read only by the Internet code
  (`FUN_0040e5b0`). The DirectPlay session's maximum is the wrapper field
  +0x91C: 100 for `-Dune Lobby-` and 8 for the game session (doc 58 §1);
  DirectPlay's own limit is not applicable to us. The human maximum on a
  LAN is **6**: Join and JoinSession refuse a game group of 6 members
  (`GameIsFull`, doc 58 §2.3), and humans + AI ≤ 6 (doc 58 §5.3).

### 3.2 Pre-game handshake (`FUN_004470a0`, verified)

Runs after StartGame, in the LOADING phase, with a **FOUND_PLAYERS** box
("Connecting %d players", "Found %d players", "Countdown .. %d seconds").
Records are message 1000 with these types:

- **Client → host, type 1**: name (+0x26), house (+0x24), colour (+0x1C),
  handicap (+0x1E).
- **Host**, per type 1 in arrival order: the next side number (1, 2, ...;
  the host is side 0), stores the player's record, answers **type 6**
  (your side +0x24, start index +0x22 = 0) and broadcasts **type 2**
  (players so far). When all `0x7984c0` humans are in:
  - **type 3** per player 1..n−1: side, house, colour, handicap, name;
  - **type 4**: starting credits (+0x14), AI players (+0x1A), worms
    (+0x1C), crates (+0x1E), unit count (+0x20), tech level (+0x22), map
    name (+0x26);
  - **type 5**: seed (+8), human count (+0x14), host house / colour /
    handicap, **game speed** (+0x22), the DirectPlay ids of all players.
- The client waits for players + 2 messages, sets everything, calls
  `srand(seed)`.
- Timeout 60 s (`0x6b9644`): the client shows NO_FIND_PLAYERS ("You
  failed to connect to the other players in the game. Returning to
  lobby.") and aborts (ABORT_MP_GAME); the host goes on with the players
  it has.

Not sent: difficulty (MP has none; handicap is per player), concrete.

### 3.3 Start sync (`FUN_00448230`, verified)

After the whole set-up (map, units, AI), if more than one human: each
machine sends type 7 and waits ("Starting Wait", 200 ms sleeps) until it
has a type 7 from every human. Timeout 20 s (60000 / 3): post a leave
(type 9, kind 1) for each missing player and show "You have lost
connection with one or more players" (`LostSomePlayers`) in the first
pass. Only one human: NO_FIND_PLAYERS and ABORT_MP_GAME. Then the loop
starts at tick 0.

### 3.4 Set-up compared with skirmish (doc 52 §2)

| Step | Skirmish | Network |
|---|---|---|
| Seed | none (RNG continues) | host's clock, shared (§1.9) |
| Local side | always 0 | host 0, clients 1.. in join order |
| Houses | side 0 = player, others i | each human's own; other slots: kept (> 2 → 0) |
| Colours | side 0 = player; sides 1..5 rolled | each human keeps the lobby colour; each **non-human** slot 0..5 rolled `rand() & 7`, not 4 / 7, unique against earlier slots **and every human** (rand lines 0x15E3..0x15F9) |
| Max crates | VARS +0x41 (1) | the same, × humans when VARS +0x42 is set (it is 0) |
| Credits | player: slider; AI: 7000 / 10500 / 14000 by difficulty | **every side: gNetStartingCredits** (`FUN_0044f8e0` `0x44fff4`) |
| Tech | gNetTechLevel for all | the same |
| Mission / map | `_PRAC.MIS`, `<map>.MAP` | the same (`FUN_004752c0` builds both paths in MP) |
| AI sides | 1..n | humans..humans + AI − 1, houses `rand() % 3` (`FUN_00469790`) |
| Starts | side 0 `rand() % 8`, then farthest-first | the same: side 0 is the host |
| Cost / build-time factors | local side: difficulty; others normal | per side: handicap byte `+0x36` (0 → 75 % cost / 125 % speed, 1 → 100 / 100, 2 → 125 / 75; `FUN_004429b0`, `FUN_00442960`) |
| End counter `0x516540` | per tick | −1 at start (§3.9) |

AI slots' handicap byte stays 0 after `FUN_004481b0`, which is the
"easy" factor (cheaper and faster: 75 % cost, 125 % build speed).
Checked 2026-10-15: every write to `0x6b922e + p·0x3c` is the reset to 0
(`0x4481f7`) or a human's record in the handshake (`0x4474fc`,
`0x4475be`, `0x4479f9`, each with the active-human flag). The lobby's
AlterHandicap (`FUN_00491c60`) cycles 0 → 1 → 2 only for entries of the
lobby player list, which holds the connected humans; AI players are only
a count. So AI sides in a network game always build as "easy".

### 3.5 AI in network games

Every machine runs DoAI for every AI side (same code, same RNG); AI
orders are never sent (§1.2). AI order delay = the network latency. The
AI template is the `_PRAC.MIS` one with EnablePractice, as in skirmish.
ActivateAI (`FUN_0041b250`) reads the game type twice. For gGameType ≠ 1
it asserts that the side's tech level and credits (`0x6b87c8[side]`,
`0x6b87d0[side]`) are not 0 ("AI not initialised correctly"); in a
network game every side gets gNetTechLevel (≥ 1) and gNetStartingCredits
(≥ 2500, `0x44fff4`), so it never fires. GroupNewUnits (`FUN_0041c3e0`)
runs only for gGameType ≠ 1 **and** no network flag, i.e. only in the
campaign (ours: `if (campaign)`). The hard-difficulty build-rate factor
reads gDifficultyLevel, which a network game leaves at each machine's
last single-player value; we use normal on every machine
(`docs/design/multiplayer-local-audit.md` B4 / B5).

### 3.6 Alliances (verified)

Key **A** (`0x7983d1`, network only, call at `0x445ec7` in `FUN_004433a0`) calls
`FUN_00459b50(gSideId)`: the target is the owner of the first selected
object of another side (sides in order; units then buildings). Order 0x1B
{side = me, +0x26 = target}. ProcessOrder case 8:

- I am hostile to the target: allowed when the target is not an AI side
  and `FUN_004553e0` finds that some living sides would still be enemies
  (so allying cannot end the game by itself). Then `alliance[me][target]
  = 0` (one-way), "<me> Ally <target>" message with S_CHATMSG, and my
  units and buildings drop their targets on the target's side
  (`FUN_0046eb00`). Otherwise "Unable to ally."
- I am already allied: "<me> declares war on <target>" (and the reverse
  line if the target was allied to me), both directions set hostile.

The default end check (doc 52 §5.2) ends the game when no two living
sides are hostile, so allied survivors win together.

### 3.7 Chat in game (verified)

- **F1..F5** (`0x798400..0x798404`): message to the 1st..5th other active
  human (in side order); **F6**: to all. Opens GAMEMESSAGE
  (`gamemsg.uil`): "<name> : " label and a 64-character edit box;
  Enter = `SendInGameMsgTo` (`FUN_0047bae0`). Not while a menu is open
  or (F1..F5) after a loss. The third test, `0x7984d0` set, never blocks:
  no instruction writes that byte and no pointer to it exists in the image
  (scan of `.text` and data), so it stays 0 (.bss). An empty Enter still
  sends "<name> : " (doc 58 §6.5).
- The line "<my name> : <text>" is shown locally and sent as record type
  0x12 (reliable) to the player or all. The receiver plays S_CHATMSG and
  shows it during the UI pass (not in lockstep). **Correction (doc 58
  §6.5):** the colour argument is never read (`FUN_00425720`, doc 47), so
  all chat and system lines use the normal message-line colour.
- A text starting with byte 0xAC is a text key: "<name> " + text(key).
  Used for "has retired from the game.", "has left the game.", "has been
  defeated.".
- F7 / F8 (paging) are Internet only.

### 3.8 Leaving, surrender, disconnect (verified)

The in-game menu INGAME_MP (`InGameMP.uil`): Options, Abort
(CONFIRM_ABORT_MP, `abortMP.uil`), Resume. CONFIRM_ABORT_MP:

| Button | Commands | Effect |
|---|---|---|
| Abort | S_MISSIONABORT, `AbortGame`, ABORT_MP_GAME | chat "<name> has left the game.", quit kind 0 |
| Surrender | S_MISSIONFAILED, `LeftGame`, gLose TRUE, QuitMenu | chat "<name> has retired from the game.", order **0x31**, quit kind 1; the game goes on to the loss |
| Cancel | BackMenu | |

Order 0x31 (case 0x1E): `FUN_0046e6b0` **destroys every unit (except
worms) and building of that side** and makes it inactive, then
`FUN_0044c4f0(side)`: the side's finishing place `0x797b70[side]` =
the next place `0x797e38` (then + 1), if it has none yet. Internet games
then set the end status (gGameType 5 only).

On leaving the loop (`0x44a114`), every machine posts leave **order 9**
{tick = now + latency, +0x10 my id, +0x14 quit kind (`0x797df8`; 1 for
Internet; 0xFF if I won), +0x1A my side}, then waits up to 10 s (100 ×
100 ms) for all reliable messages to be acked. Peers mark the player as
left when the message arrives (ack type 10) and run it in lockstep
(`FUN_0045cda0`): the side is no longer an active human, then

- kind 0 (Abort, or after we dropped someone): **LetAITakeOver**
  (`FUN_0046e9c0`): the side becomes an AI side and plays on;
- kind 1 (Surrender, start-sync or timeout drop): `FUN_0046e6b0`
  removes the side;
- kind 0xFF: nothing.

A side whose leave came from a timeout drop is removed. Serial / modem:
after the game, the connection is closed when fewer than 2 members
remain or an AI took over (`0x797dfc`).

### 3.9 End of game and score (verified)

- Win / loss detection: the skirmish default check every tick (doc 52
  §5.2), with alliances.
- **Won** (MP): no S_MISSIONWON voice (voice only for gGameType 0 / 1),
  whole-map reveal (`FUN_0044c480`), banner while the VARS delay counter
  runs, then QuitAndJumpToMenu **SCORE** (TitleMultiScore "MULTIPLAYER
  RESULTS").
- **Lost** (MP): no voice; reveal; chat "<name> has been defeated." to
  all. The end counter is −1 at the start of an MP game and is not
  reloaded, so it reaches 0 only when the game is over or no human is
  alive (`0x6b7050`): **a defeated player watches the rest of the game**
  (map revealed) and goes to SCORE when it ends, or leaves with Abort.
- **Order inside the tick** (verified, `0x449b2d`..`0x449f61`). Both end
  paths run once (guard `0x517400`) in the tick that saw the end, after
  the end check: the won path sets the counter to SinglePlayerDelay ×
  ticks in the last second (`0x449b96`; only for gGameType ≥ 2, the
  single-player reload is at `0x449ac3`), then in a network game
  `FUN_00455290(i, 0)` and `(i, 1)` for every side 0..7 (`0x449c54`..
  `0x449c7d`, the lost path `0x449eaf`..`0x449ed8`), then the whole-map
  reveal `FUN_0044c480` (`0x449c7f` / `0x449eda`), then (lost only) the
  defeat chat. Every later tick enters the same block: a counter of 0 on
  entry ends the game in that tick (`0x449c9f` / `0x449f36`); else it
  goes down by one, the first time already in the tick that set it
  (`0x449c9a`), and on the lost path it becomes 0 when the game is over
  or no human is alive (`0x449f3a`..`0x449f57`), so the end comes one tick
  later.
  Ours (`GameScreen::sim_tick`) does the same.
- SCORE as doc 52 §5.5 (finishing places `0x797b70`, colours, names).

## 4. Front end (brief)

`MULTIPLAYERTYPE` (`MPType.uil`):

| Button | Commands |
|---|---|
| Network | `SetValueS2To gNetNetworkType 4`, gGameType Network, `CreateWigNet`, IPXLOBBY |
| Internet | CD check, gNetNetworkType 2, gGameType Internet, `CreateWigNet`, INETLOGON (Westwood Online) |
| Serial / Modem | CD check, CONNECTION_TYPE (`conType.uil`): Dial modem (gNetNetworkType 1, gGameType Modem, MODEM_JOIN), Answer modem (`StartSession`), Null modem (gNetNetworkType 3, gGameType Serial, SERIALLOBBY), Modem settings (`modem.cpl`) |
| Practice | skirmish (doc 51) |

- **IPXLOBBY** (`IPXLobby.uil`): name, house, colour (0, 1, 2, 3, 5, 6),
  game list (`EnterLobby`, `JoinSessionDbl`), player list
  (`GetLobbyList`, `RefreshLobbyList`), chat (`SendMsgToAll`,
  `RefreshChatList`). Buttons: Join (`SendUIMsgToAll ClientWantsToJoin`),
  New (`StartSession`), Cancel (`BreakFromLobby`). Everybody first joins
  a DirectPlay session named "-Dune Lobby-" (`FUN_00479b30`; corrected,
  doc 58 §1).
- **Correction (doc 58 §3):** on a LAN the host page is
  **HOSTHEADTOHEAD** (`HostHTH.uil`) and the client page
  **CLIENTMULTIPLAYER** (`ClientMP.uil`). HOST_MP2 / JOIN_MP2 below are the
  serial / modem pages; the lines below had the two swapped.
- Host page **HOST_MP2** (`hostMP2.uil`; serial / modem: HOSTHEADTOHEAD
  `HostHTH.uil`): map list, unit count, tech, credits, AI players,
  crates, worms, concrete (dead, doc 51), each change sent with
  `SendUIMsgToAll <setting>`; player list with `AlterHandicap` and
  Reject; Start = `StartGame` (`DisplayNetMessage CannotStartGame`
  otherwise); Cancel = `BreakFromSession`.
- Client page **JOIN_MP2** (`joinMP2.uil`; serial / modem
  CLIENTMULTIPLAYER `ClientMP.uil`): the same values read-only
  (`RefreshValue*`), **Accept** (`SendUIMsgToAll AcceptSettings`, the
  ready state) and Cancel (`ClientWantsToQuit`).
- The settings arrive in `FUN_0048e820` (one case per UI message; only
  from the host); the client's StartGame is called from there
  (`0x48f26f`). LOBBY_SCREEN is not a menu file: BackMenuAndJumpTo
  LOBBY_SCREEN picks the lobby by game type (doc 04).
- Error pages: FOUND_PLAYERS, FAILED_MP_CONNECT, NO_FIND_PLAYERS,
  MP_TIMEOUT, INT_TIMEOUT, OUT_OF_SYNC, WIGNET_ERROR, ABORT_CONNECTION
  (serial / modem form of CONFIRM_ABORT_MP), ABORT_VM.

Lobby UI commands with handlers (switch `0x483a94` / `0x4838c0` in
`FUN_00482850`): StartSession `0x48323a`, JoinSession `0x48324a`,
ReEnterLobby `0x48325f`, BreakFromSession `0x48326f`, BreakFromLobby
`0x48327f`, CreateWigNet `0x483299`, DestroyWigNet `0x4832a3`,
CloseSession `0x4832b2`, SendGameOptions `0x483207`, SendInGameMsgTo
`0x48317e`, SendMsgToAll `0x483194`, SendUIMsgToAll `0x4831a9`, LeftGame
`0x482e85`, AbortGame `0x482e8f`. EnterLobby / GetLobbyList /
RefreshLobbyList / JoinSessionDbl are init or list commands (no click
handler). The lobby message ids and the ready / start rule are settled in
doc 58 §4 and §5.4.

## 5. Transport (brief)

- **"WigNet"** (`0x798490`, CreateWigNet / DestroyWigNet) wraps
  **DirectPlay 4** (`DPLAYX.dll`, `IDirectPlay4A`): service provider by
  gNetNetworkType (4 IPX / TCP for "Network", 3 serial, 1 modem).
  Session: `Open` with `DPSESSIONDESC2` flags KEEPALIVE (+ MIGRATEHOST
  when wrapper byte +2), application GUID
  `{31ADB741-187A-11D1-A0D0-006097AA16C0}`, max players from +0x91C
  (`FUN_004a16a0`).
- **All game traffic is sent unguaranteed** (`IDirectPlay::Send`,
  vtable +0x68, flags 0; to = 0 broadcasts). The game adds its own
  reliability (`FUN_0045b0a0`):
  - a reliable message gets a per-destination sequence in its first
    dword and is kept in a resend list (`FUN_0045b020`); a broadcast is
    sent once per member (EnumPlayers), each with its own sequence;
  - the receiver acks every reliable message (0x3E9 {seq},
    `FUN_0045ac30`) and drops duplicates (`FUN_0045ad80`); it does **not
    reorder**: ordering is the game layer's job (order sequence numbers,
    §1.3);
  - un-acked messages are resent after `0x4e3b1c` ms (300 / 1000, then
    2 × the clamped half ping), checked every pass, at most 16 entries
    per pass.
- Reliable: orders, leave, latency (0x3EF), rate reports (0x3EB), chat,
  handshake. Unreliable: pulse (broadcast), pong (to one), 0x3ED / 0x3EE.
- **Internet** (gGameType 5): Westwood Online. `irc.westwood.com`,
  Westwood's WOLAPI chat COM object (`gpChat`, "ConnectToWestwoodChat"),
  channels, paging, ladder, game results upload, and its own transport
  object (wrapper +0x15DC, mode 5) instead of DirectPlay; `WSOCK32.dll`
  belongs to this path. The service shut down long ago; this path cannot
  work as shipped. Not applicable: our build removes the Internet button
  (doc 58 §7.3), so the mode-5 transport is never needed.

## 6. Corrections to earlier docs

- **doc 49 §1.2:** type 0x0C (pulse) and 7 (start sync) are **sent
  directly** (`FUN_0045bcc0`), never queued. The 0x0C send at `0x449982`
  is inside the `0x7984c4` block, which settles that open point.
- **doc 49 §1.4:** AI orders in MP are queued per side with
  `tick = now + latency` (the network latency, 40 / 75 at start).
- **doc 49 §1.3:** the 8-second `FUN_0045c480` call is SP only; in MP the
  timer calls `FUN_0045d390` every 9 s and the host calls `FUN_0045c480`
  from the tick block.
- **doc 52 §6:** in MP every side gets gNetStartingCredits, and the cost
  / build-time factors come from each side's handicap.

## 7. Implications for our implementation

**Must reproduce exactly (game-visible or needed for identical
simulations):**

1. The order record contents and the post-time rules of doc 49, plus a
   per-player sequence counter shared by game orders and the leave /
   latency orders.
2. Execution tick = post tick + current latency for **every** order,
   human and AI, in MP. Per-side lists; run order each tick: misc
   system orders due, then sides 0..n−1, each in sequence order, with
   leave / latency orders run between the game orders they were numbered
   between (§1.4).
3. The latency floor rule on latency changes (`FUN_0045a540`): queued
   orders never run earlier than the floor.
4. Latency values: start 40 / 75; adaptation by an order applied in
   lockstep. If we keep adaptation, keep the formula and the 72 % / 139 %
   step limits (§1.5), because the delay is felt by players and changes
   the AI's reaction time. The inputs (pings, loop rates) may come from
   our own measurements.
5. Shared seed from the host at start; `srand` before the set-up; the
   MP colour roll for non-human slots; AI houses, starts as skirmish; one
   extra `rand()` per tick (the sync value) at the start of the tick.
6. MP set-up differences of §3.4 (credits for all, handicap factors,
   local side = join order).
7. Stall behaviour seen by players: no ticks while a peer's confirmed
   tick + latency − 2 < now; UI keeps running; the timeout box with the
   countdown (6 s LAN / 30 s modem after about 180 / 300 passes) and the
   drop with "You have lost connection with one of the players".
8. Leave kinds: Abort → AI takes over the side; Surrender → order 0x31
   destroys the side; timeout drop → side destroyed; the chat lines for
   each.
9. Alliance order and its rules, chat keys F1..F6, chat display.
10. No pause under menus in MP; defeated players keep watching.
11. Game speed: host's speed at start, any player can change it later
    (order 0x0B); shared rate cap = slowest player's loop rate.
12. Out-of-sync: compare per-tick sync values; on mismatch the message
    line text and the OUT_OF_SYNC box back to the lobby.

**May be replaced (not game-visible):**

- DirectPlay / IPX / serial / modem and the custom ack layer: any
  reliable transport (e.g. UDP with our own reliability, or TCP) is fine,
  as long as the game layer still receives orders with (tick, seq, side)
  and pulses with (tick, last seq, sync value).
- Pulse rate, ping measurement and resend timing (keep the observable
  results: latency steps, stall rule).
- The sync value could be extended to a real state hash for our own
  diagnostics, but the extra `rand()` draw per tick must stay (it moves
  the RNG). The sync table size (150) is not visible.
- Lobby protocol, session discovery, Westwood Online (defunct): replace
  by our own lobby (direct IP / LAN broadcast), keeping the lobby pages'
  look and the settings that reach the game (§3.2 type 4 fields, speed,
  per-player house / colour / handicap / name, join order = side).
- The 60 s handshake and 20 s start-sync timeouts can stay as they are.

## 8. Open questions

None left (closed 2026-10-16). How each former point was settled:

- Timing numbers (order delay 40 / 75 and its descent, stall and timeout
  box): all read from the code (§1.5, §1.7); no running game is needed
  for them.
- Message types 0x3EC / 0x3EE: never sent in 1.06 (§1.3). Order 0x32: no
  poster (§1.10, doc 57a §1).
- Human maximum on a LAN: 6 (§3.1); DirectPlay's +0x91C is not
  applicable. The ready / start rule: doc 58 §5.4.
- A timeout drop can desync the survivors (§1.7); a late 0x11 can only
  come from a player already marked left and is then dropped (§1.5).
- The crater jitter and thick-spice tables are visual only (§1.9).
- WIGNET_ERROR after "Model Out Of Sync": doc 58 §3.8.
- `FUN_0044c4f0` after a surrender: the finishing place (§3.8).
  `FUN_0049fde0` / host migration: §1.5.
- Handicap of AI slots: always 0, "easy" (§3.4).
