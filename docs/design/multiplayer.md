# Multiplayer design

How we build network play. The game behaviour comes from
`docs/re/57-multiplayer.md` (doc 57) and must match the original. The
transport is new, as decided in CLAUDE.md §10 (2026-10-14):
- one player hosts, the others join by IP address or from a LAN list;
- no server;
- d2k-faithful only, with no wire compatibility with the original.

## 1. Goals and limits

- **Same game as the original's network game.** That covers:
  - the order delay (latency) and its adaptation;
  - the per-side run order;
  - the shared seed and the extra `rand()` per tick;
  - the sync check and the stall and drop rules;
  - the three ways to leave (Abort, Surrender, timeout drop);
  - alliances and chat;
  - no pause under menus;
  - the MP set-up differences of doc 57 §3.4.
- **Our transport:** UDP. As in the original, we add our own
  acknowledgements and resends on top of an unreliable datagram service
  (doc 57 §5).
- **Not supported:**
  - the original's DirectPlay, IPX, modem and serial;
  - Westwood Online;
  - saving network games (the original has no MP save).
- **Host migration is supported**, as in the original (decided
  2026-10-15): when the host leaves, the lowest remaining human takes the
  host role (the latency computation, doc 57 §1.5) and the hub role
  (§6.3). Built in `Lockstep::host_side` / `after_tick` and
  `net/transport`.
- **Players:** up to 6 humans and AI together (the lobby rule,
  doc 57 §3.1).

## 2. Layers

| Layer | Library | Contents | Depends on |
|---|---|---|---|
| sim | `d2k_sim` | order records, per-side queues, latency and floor, sync draw, system orders, MP set-up, the shared `GameSetup` builder | formats |
| lockstep | `d2k_net` (`net/lockstep.*`) | the per-peer sequence tracking, confirmed ticks, stall rule, sync table, host latency computation, timeout state machine. **No sockets**: it takes and returns messages as bytes, so tests can drive it | sim |
| transport | `d2k_net` (`net/udp.*`, `net/reliable.*`, `net/lobby.*`) | UDP sockets (Winsock / BSD), reliable messages with acks and resends, LAN discovery, host / join handshake | lockstep, OS sockets |
| app | `d2k` | lobby pages and in-game UI (chat, A key, timeout box, OUT_OF_SYNC), driving the loop | all |

`sim/` stays free of networking. It only learns that a game is a network
game, which sides are humans, and the orders, latency changes and leaves
handed to it.

## 3. Orders as data

Today a queued order is a C++ closure (`World::post_order(side,
std::function)`). A closure cannot travel over the network. New design:

```
struct Order {                 // one exe record (doc 49 §1.1, doc 57 §1.10)
    uint8_t type;              // exe type: 0x13..0x34 game, 9 / 0x11 system
    uint8_t side;
    uint32_t tick;             // execution tick
    uint32_t seq;              // per-player sequence (MP humans only)
    int32_t a = 0, b = 0, c = 0; // the order's arguments (cell, target, record, kind)
    std::vector<uint16_t> ids; // the side's selected unit ids at post time (<= 64)
};
```

- Every `order_*` / `start_*` / `starport_*` function takes the side and
  at most three integers (checked in `world.hpp`), so `a / b / c` cover
  them. `World::execute(const Order&)` switches on the type and calls the
  existing handler, with the side's selection set from `ids` as
  `run_orders` does now.
- **The UI posts records** (`World::post(Order)`), never closures. The
  network layer serialises the same record.
- **AI orders** come from inside DoAI on every machine, so they never
  travel and may stay closures. In MP they are queued per side by tick
  only (doc 57 §1.4).
- **Single player is unchanged.** There is one list in post order with
  latency 2. The existing golden tests and the determinism fingerprint
  must give the same results after the change; that is the proof.

Unit ids: our unit ids (`next_unit_id_`) are deterministic, so the same id
names the same unit on every machine. The exe sends slot indices (doc 49
§4.1); this is the accepted difference already noted there.

## 4. The simulation in network mode

`World::set_network(NetGame)` before the first tick, with:
- the human sides;
- the starting latency (40);
- the factor table of doc 57 §1.5.

Then:

1. **Queues** (doc 57 §1.2, §1.4):
   - one list per side, human lists sorted by (tick, seq), AI lists by
     tick;
   - a misc list for system orders 9 and 0x11;
   - each tick, `FUN_0045a540` raises queued orders to the floor, then
     `FUN_0045cda0` runs due system orders, then sides 0 .. n−1 run in
     order, with system orders numbered between two game orders running
     between them.
2. **Latency** is a member, no longer a constant (`kOrderLatency` stays
   for SP):
   - every post uses `tick = now + latency`;
   - order 0x11 applies the new latency, the floor and the resend value
     at its tick (doc 57 §1.5 "Applying").
3. **Late order:** a human order whose tick has passed reports "Model Out
   Of Sync" (doc 57 §1.4) through a flag the app shows as WIGNET_ERROR.
4. **Sync value:** at the start of each tick, before the counter goes
   up, record (tick, sync) and draw `sync = rand()`. Exposed as
   `World::sync_value()` and a small history the lockstep layer reads
   (doc 57 §1.1, §2).
5. **System and MP orders:**
   - 9 leave: kind 0 → LetAITakeOver, kind 1 → remove the side
     (`FUN_0046e6b0`), 0xFF → nothing;
   - 0x1B ally / war (doc 57 §3.6);
   - 0x31 retire;
   - 0x32 AI takeover: no poster exists in 1.06 (doc 57a §1), so it is
     never sent; the case is kept in `World::execute` as the exe keeps
     it.
6. **MP set-up** (doc 57 §3.4):
   - every side gets gNetStartingCredits;
   - per-side handicap cost and speed factors (`FUN_004429b0`,
     `FUN_00442960`);
   - the colour roll for non-human slots;
   - the end counter −1 (defeated players watch);
   - no pause and no S_MISSIONWON voice in MP.
7. **Local-only state:** the World keeps a local side for sounds,
   messages and the shroud display. An audit
   (`docs/design/multiplayer-local-audit.md`, from the research pass)
   lists every simulation use of the local side and the selection. Each
   one must be presentation only, or the same as the exe's gSideId use.

### 4.1 One set-up for everything

The skirmish set-up lives in the `GameScreen` constructor today, and the
test helper `Skirmish` (test_world.cpp) copies it by hand and already
leaves out steps (colour roll, thick-spice pattern). Move it to
`sim::GameSetup` plus `build_game(setup, read_file)` in `d2k_sim`. The
caller passes a file reader, so sim still does not depend on platform.
`GameSetup` holds:
- the game type and the map / mission;
- per side: house, colour, handicap, human or AI, name;
- the AI count, credits, tech, units, worms, crates and difficulty;
- the seed and the local side.

GameScreen, the tests and the network game all build through it.

## 5. Lockstep layer (`net/lockstep`)

One `Lockstep` object per machine and game. It holds the doc 57 §0
per-player record:
- the confirmed tick and last-heard tick;
- the contiguous sequence and the early sequences;
- the loop rate and ping reports;
- the leave tick and the active flag.

Its interface:

```
void on_message(int from, span<const uint8_t>);   // order, pulse, pong, report, leave, chat
std::vector<Outgoing> poll(uint64_t now_ms, uint32_t tick); // pulses, reports, pongs, resends
bool may_tick(uint32_t tick) const;                // the stall rule, doc 57 §1.7
TimeoutState timeout() const;                      // for the MP_TIMEOUT box
bool out_of_sync() const;                          // first mismatch tick
```

- **Posting:** the app posts an order to the World. The World gives the
  record its seq and tick, and the Lockstep broadcasts it.
- **Received orders** go to `World::receive(Order)` after the sequence
  bookkeeping.
- **Pulses** are sent about 8 times a second and carry {tick, last seq,
  sync value, optional ping stamp}.
- **The host's 9-second latency computation** follows doc 57 §1.5 to the
  letter, including the `FUN_0045d350` ping quirk and the 72 % / 139 %
  steps. It posts order 0x11.
- **Game speed** change: message 0x0B, applied at the first pass with
  `now >= tick` (not tick-exact, as in the exe).

Our addition, diagnostics only: each pulse also carries a 64-bit hash of
the game state (§7), compared like the sync value. A mismatch is written
to `d2k-net.log` in the user folder. It does not end the game: only the
original's `rand()` check does that. This tells us early when our code
is at fault.

## 6. Transport (`net/udp`, `net/reliable`, `net/lobby`)

### 6.1 Packets

UDP, little-endian, one message per datagram. All messages are small: an
order is about 40 bytes plus 4 per id. Header:

| Field | Size | Meaning |
|---|---|---|
| magic | 4 | `D2KF` |
| protocol | 2 | protocol version |
| kind | 1 | transport kind: data, ack, hello, hub (below) |
| flags | 1 | bit 0 reliable |
| from | 1 | the original sender's side (0xFF before a side is given) |
| to | 1 | the final receiver's side |
| seq | 4 | reliable: sequence of this sender → receiver pair; 0 = unreliable |

The payload of a data message is one lockstep message (`net::Lockstep`
encoding: order, pulse, pong, report) or one session message (§6.4).

### 6.2 Reliability, end to end (as the original)

The original numbers reliable messages per destination, acks each one,
drops duplicates and does not reorder (doc 57 §5). We keep exactly that,
but **between the original sender and the final receiver**, not per hop:

- **Broadcasts** are sent as one copy per receiver, each with that
  pair's sequence, as the original does (EnumPlayers).
- **The receiver acks** each reliable message to its sender (an ack
  message routed back like any other) and drops duplicates by (from,
  seq). It does **not** reorder; the lockstep's order sequence numbers
  do that.
- **The sender** keeps every reliable message until its receiver acks
  it. It resends after the resend time: 300 ms on a LAN, then 2 × the
  clamped half ping from latency record 0x11. This is checked every loop
  pass, at most 16 resends per pass.
- Pulses and pongs are unreliable.

Because acks are end to end, a message the hub never forwarded is not
acked, and is sent again. This is what makes the hand-over of §6.3 safe:
no order can get lost in a host that disappears.

### 6.3 Topology: a star with a hand-over

The original is a DirectPlay mesh: every message goes to every member.
We use a **star through a hub** instead, as the user chose: only the hub
needs a reachable port for internet play.

- The hub is the host at the start. Every player talks only to the hub.
  The hub forwards each message by its `to` field. It does not open or
  change the payload.
- Clients behind NAT need no port forwarding: their outgoing packets
  open the way for the hub's replies.
- The game layer still sees the original's model, where every player
  hears from every other player. A forwarded message arrives a little
  later than a direct one. That only changes the measured ping, which
  the host's latency computation (doc 57 §1.5) uses; it never changes
  the game result.

**Hand-over (decided 2026-10-15, as the original).** In the original,
when the host's leave record runs, the lowest remaining active human
becomes the host (`FUN_0045cda0`, `FUN_0049fde0`). The others learn the
new host from the next latency record (0x11). We split this into two
parts:

1. **The game role** (the latency computation, doc 57 §1.5) is always
   held by the lowest-numbered human who is active and has not left.
   - On a voluntary leave this is exactly the original's rule
     (`FUN_0045cda0`). Every player's stored ping is reset to the latency
     value, as there.
   - On a crash the original let DirectPlay choose the new host (wrapper
     notification 0x3C, "I Have Become the Host"); we use the same
     lowest-side rule.

   Only the latency adaptation depends on who holds this role, never
   the game result. If two machines briefly both act as host, both post
   latency records; every machine runs them in the same order, so the
   games stay identical.
2. **The hub role** (routing) must move earlier. Once the hub is silent,
   no one hears anyone. The stall rule would then pick a wrong "slowest
   player" and drop a healthy player.

   So the transport watches the hub: after **2 seconds** with no packet
   from it, the next hub is the lowest-numbered human side that has not
   left and is not the silent hub. The game's stall box only opens after
   about 3 seconds. Everyone already knows every player's address and
   port, sent at the start. Every client then sends a **hub hello** to
   the new hub and from then on routes through it, resending everything
   not yet acked. The new hub answers each hello, so a client that gets
   no answer can try the next candidate.

   When the old host is truly gone, the game then drops it as usual: the
   stall box, the countdown and a leave of kind 1. Its side is removed.
   That leave runs on every machine and moves the game role.

   When the old host left on purpose (Abort, or quit after a loss), it
   first posts its leave record and keeps routing while it waits for its
   acks, up to 10 s as the original (doc 57 §3.8). Then the game role
   moves at the leave's tick and the hub role follows at once: the old
   host stops, and the 2-second rule moves the hub.

**A host that only lagged** (a freeze or a short Wi-Fi drop) is not
dropped by the hub move. Only the game's own timeout drops a player:
about 3 s of stall plus the 6 s countdown, about 9 s, as in the
original. While the old host is silent the clients route through the new
hub, which also forwards to the old host. When the old host is heard
again, it sees another machine forwarding traffic, steps down, says
hello to the new hub and resends what is unacked through it. Tested: a
5-second host freeze, nobody dropped, all games identical (2026-10-15,
after a side note asked how a returning host gets back in).

**When nobody can reach the new hub** (internet, the new hub without an
open port): the clients can no longer hear each other. After the normal
timeout each of them drops the others, so every one ends up alone and
sees the game end. This is the "game ends for everyone" case the user
accepted for that situation.

- Default port: 1998 UDP, settable by the user. Every player's game
  listens on its port, so the hand-over can use it. On a LAN this always
  works; over the internet, only when the new hub's port is reachable
  (port forwarding or a VPN such as Tailscale or ZeroTier).

- Default port: 1998 UDP (user-settable).
- **LAN discovery:** the joiner broadcasts a probe to 255.255.255.255 and
  to each interface's broadcast address. Hosts answer with the game name,
  map, player count and protocol version.
- **Join by IP:** the same probe, sent to one address.
- **A player on another port** (a second copy on the same PC, or a PC
  whose 1998 was taken) never gets broadcasts sent to 1998. Whoever hears
  such a player's presence or lobby chat remembers its address and sends
  its own presence, lobby chat and probes there too (found in the first
  playtest, 2026-10-16: one copy listed both players, the other only
  itself). Since the Windows Sandbox test the same day this holds for
  every player heard, whatever its port: broadcasts from the PC into the
  sandbox were often lost, so the PC's name came and went there.
- **Version check (built 2026-10-15):** the program version, the
  protocol and a **build fingerprint** must match or the join is refused
  ("WrongNetworkVersion", doc 57 §4 strings). The fingerprint is a hash of
  every source file under `src/` (line endings normalised) and the
  compiler name and version (`cmake/BuildFingerprint.cmake`, refreshed on
  every build). It is strict on purpose: any source change, even a
  comment, or another compiler update gives another value, so copies
  built on different PCs or by CI may refuse each other. Players copy the
  same build to every PC. Debug and Release of one source share a value
  (the determinism fingerprint test passes in both).
- **Data check:** `build_game` hashes every file it reads (paths and
  contents, `Game::data_hash`); the start sync's "ready" message carries
  it, and another value from any player sets out of sync at tick 0, so
  different data files (a modded rules file, another copy of the map)
  show at once instead of as a drift later.

### 6.4 Session flow (doc 57 §3.2, §3.3)

1. **Lobby.** The host page sends settings changes; clients send Accept,
   as the original's LAN pages HOSTHEADTOHEAD / CLIENTMULTIPLAYER (doc 58
   §3; HOST_MP2 / JOIN_MP2 are the serial / modem pages).
2. **Start.** When the host presses Start, it sends start = {seed (clock
   ms), players with side / house / colour / handicap / name / address,
   settings, speed}, as the type 3 / 4 / 5 handshake. Each machine calls
   `srand(seed)` and builds the game with `build_game` and its own local
   side.
3. **Start sync.** Each machine sends "ready" and waits for every human
   (20 s timeout: missing players leave with kind 1, doc 57 §3.3), then
   tick 0.

## 7. State hash (diagnostics)

`World::save_state()` holds presentation fields too: the selection,
messages and voice timers. The audit lists them. The diagnostic hash is
SHA-256 (truncated to 64 bits) of `save_state()` with those fields left
out, through a `save_state(StateScope::Shared)` variant. Taken every
pulse tick, it is cheap enough at about 8 per second. The tests use it
as their main assertion.

## 8. Phases

**Status (2026-10-16): phase 3 played with two copies on one PC; fixed: the chat box froze the picture, a game listed twice, the lobby player list one-sided on a second port. Next: two PCs on a LAN.**
- **Lobby pages** (`app/menu_lobby.cpp`, doc 58 §2-5): IPXLOBBY with the
  game list, the LAN lobby's players and chat, and our address box;
  HOSTHEADTOHEAD and CLIENTMULTIPLAYER with the player rows (handicap
  icon, colour, house, host / Accept marks), the settings to the
  clients, AlterHandicap, Reject, Accept and Start states, NoChangeOptions;
  MULTIPLAYERTYPE without Internet and Modem/Serial.
- **In game** (doc 58 §6): INGAME_MP, CONFIRM_ABORT_MP (Abort and
  Surrender), MP_TIMEOUT, OUT_OF_SYNC, NO_FIND_PLAYERS, F1..F6 chat with
  GAMEMESSAGE, the A key, the lost-player and defeat lines, the speed
  order 0x0B, the leave order at the end with the 10-second ack wait,
  MULTIPLAYER RESULTS with the network names, the routes back to IPXLOBBY.
- **Version check:** the build fingerprint and the data hash (§6.3).
- Added 2026-10-16 (not yet seen in a played game):
  - **FOUND_PLAYERS** over the start sync. The original shows it during
    its pre-game handshake (doc 57 §3.2), which ours does in the lobby;
    the start sync is the wait where players come in. "Found" counts the
    humans ready so far (me included; the original's client shows 1);
    the countdown runs to our 20 s start-sync timeout, not the
    handshake's 60 s.
  - **WIGNET_ERROR**: opened when an order arrives after its tick
    ("Model Out Of Sync", doc 57 §1.4) and when no UDP socket can be
    opened at all (the wrapper error callback's place). OK → MAINMENU.
  - **The sidebar score box** with the exe's network rule (doc 56 §4.2):
    network names, "Computer", a side with a place goes (own row too).
  - **NoSpaces / NoNumbers / NoLetters / NoOtherChars** on edit boxes
    (doc 58 §3.1): the name boxes take only letters, digits and spaces.
- Next (phase 4): two PCs on a LAN (one PC and Windows Sandbox done
  2026-10-16).
- **Tailscale / ZeroTier:** nothing special in the game. Join by the
  peer's VPN address or name in the address box (names are looked up).
  These networks carry no broadcasts, so the LAN game list, the IPXLOBBY
  player list and lobby chat do not reach across them; joining and the
  game itself work. Our largest message (the lobby state, about 1 KB)
  fits Tailscale's 1280-byte MTU.

**Status (2026-10-15): phase 2 done.**
- **`net/udp`:** the socket layer.
- **`net/transport`:** the star through a hub, end-to-end reliability and
  the hub hand-over.
- **`net/lobby`:** finding games (LAN broadcast, this PC, or an
  address), joining, the settings, Accept, lobby chat, and the start with
  the seed and the final player list.
- **`Lockstep::start_sync`:** the 20-second start sync.

Tested on localhost over real UDP:
- loss and the hand-over;
- three machines in step with 15 % loss;
- the host dies, and the game goes on through side 1;
- the whole lobby, through start sync and play.

A real two-PC game needs the front end (phase 3).

**Status (2026-10-14): phase 1 done.**
- `sim::Order` and `World::post`.
- `build_game`.
- The network queue in `sim/network.cpp`, with TakeOver_AI (doc 57a).
- `net/lockstep`.
- The in-process harness in `tests/sim/test_network.cpp`. Three
  machines over a lossy, reordering link stay identical for 75 s of game,
  and the host adapts the latency. Also tested:
  - a silent player stalls the others, then is dropped and removed;
  - a diverged machine is caught;
  - leave, surrender, ally and the latency floor.

Not in phase 1:
- The diagnostic state hash (§7). The tests compare a shared-state list
  instead.
- The thick-spice tile check (§9).

1. **Simulation and lockstep, no sockets.**
   - Orders as data: UI call sites converted; SP results unchanged
     (golden tests and fingerprint).
   - The network mode of §4 and the `GameSetup` builder.
   - `net/lockstep`.
   - An in-process harness: N games plus N Lockstep objects joined by a
     fake link with delay, loss and reordering, and scripted human
     orders. Tests:
     - all machines end with the same state hash;
     - latency adaptation steps;
     - stall when a peer is silent;
     - timeout drop;
     - Abort / Surrender / leave kinds;
     - alliances;
     - a forced divergence is caught by the sync check;
     - a late order gives "Model Out Of Sync".
2. **Transport.** UDP, reliability, LAN discovery, join and start
   handshake; the same harness over real sockets on localhost (two or
   three processes started by a test).
3. **Front end.**
   - The original's MULTIPLAYERTYPE → Network flow with our connection
     step: the IPXLOBBY page as the LAN game list plus an IP field;
   - HOSTHEADTOHEAD / CLIENTMULTIPLAYER (doc 58 §3);
   - FOUND_PLAYERS, MP_TIMEOUT, OUT_OF_SYNC, WIGNET_ERROR,
     CONFIRM_ABORT_MP and INGAME_MP;
   - chat F1–F6 with GAMEMESSAGE, the A key, the MP score.

   Before this phase starts: a UI findings note on how each original
   lobby page maps to our controls.
4. **Playtest.** Two copies on one PC, then two PCs on a LAN; compare
   the timing numbers with doc 57 where possible.

## 9. Open points

- Every timing value of doc 57 is read from the exe's code; playing
  against the original is not needed to settle them.
- Host migration is built (§1, §6.3): the host role follows the lowest
  active human (`FUN_0045cda0`; `FUN_0049fde0` is only the transport's
  Set Mode, and the new host's stored pings become the latency), the hub
  role moves after 2 s of silence.
- A timeout drop can desync the survivors (doc 57 §1.7, settled from
  the code): the dropped player's last orders run on the machines that
  got them. The original's behaviour is kept, and the sync check ends
  such a game as the exe does.
- Two tables are rolled before the seed (doc 57 §1.9), so they differ
  per machine:
  - **Crater jitter:** only the game screen draws with it (visual).
  - **Thick-spice pattern** (`session.thick`): it picks which of the
    four thick-spice tiles a cell gets (`game_map.cpp`). **Checked
    2026-10-15: picture only.**
    - A spice cell's game properties are its flags: the spice level
      and the fixed thick-spice flags. The pattern only sets the tile
      index.
    - Harvesting, re-smoothing, worms and building read the flags. The
      tile index is read only by the screen (drawing, radar, terrain
      tooltip).
    - Test "machines with different thick-spice patterns stay
      identical": the tiles differ, but after 6000 ticks the shared
      state and every cell's flags are equal.

    The diagnostic hash (§7) must leave the tile index out.
