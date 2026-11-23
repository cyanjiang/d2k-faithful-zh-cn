# 59. In-game sound channels, positional effects and the voice queue (1.06)

Target: Dune 2000 1.06 US (`DUNE2000.DAT`). Read from the disassembly and
decompilation of the functions named. "Verified" means checked against the
instructions. No open points remain.

Builds on doc 12 §9 (a first summary of `FUN_0046f540` and
`FUN_0046f820`), doc 15 §5 (speech and music) and doc 20 §3 (the SFX
volume setter). This doc gives the full rules and corrects two points
(§6).

## 0. Functions and globals

| Address | Role |
|---|---|
| `FUN_0046f540(id, cx, cy)` | PlayPositional: pan / volume from the cell, then `FUN_0046f210(…, 1, 5, 2)` |
| `FUN_0046f210(id, pan, vol, first, last, mode)` | pick a channel and play (`__thiscall` on the channel table) |
| `FUN_0046f170` | stop / re-init the channel's Miles sample, load the file, set pan and volume, loop count 1, start |
| `FUN_0046f330` / `FUN_0046f390` | store pan (+8), volume (+0x18) etc. in the channel record, then `AIL_set_sample_pan` / `_volume` |
| `FUN_0046f4f0(id)` | true if any of the 6 channels plays sample `id` |
| `FUN_0046f820(id, mode, limit, prio)` | the voice queue (§3) |
| `FUN_0046f900(S, O, H, mode, limit, prio)` | house voice: picks S_ / H_ / O_ by `0x8cd4f0[0x798544]` (doc 12 §9), then `FUN_0046f820` |
| `FUN_0046f6c0(prio)` / `FUN_0046f6e0` | ring `prio` empty (head = tail) / both rings empty |
| `FUN_0046f700(id, prio)` | `id` already in ring `prio` |
| `FUN_0046f770(id, prio)` | append to ring `prio`; false (dropped) when full |
| `FUN_0046f7d0(&id)` | take the next voice: ring 1 first, then ring 0 |
| `FUN_0046faa0` | per-loop speech service: start the next queued voice on channel 0 |
| `FUN_004762f0(name)` | mission end / UI sample: `S_MISSION*` go to `FUN_0046f900`, any other name plays on channels 0–5 in mode 0 |

| Global | Meaning |
|---|---|
| `0x795608` | pointer to the Miles state; `+4 + ch·0x24` is channel `ch`'s record (6 records, 0..5) |
| record `+0` | the Miles sample handle (`AIL_sample_status` & 4 = playing) |
| record `+8` / `+0x18` / `+0x1c` | pan / volume / sample id |
| `0x795588` | the two rings, `[slot·2 + prio]`, 5 slots each |
| `0x795580` / `0x7955b0` | ring tails / heads (u32 × 2) |
| `0x79562c` | speech active (set when `FUN_0046faa0` starts one, cleared when channel 0 stops) |
| `0x4e5fc4` | `gSFXVolume`, 0..100 |
| `0x4dfb08` | `gGameState` (2 = in a mission) |
| `0x4eb00c` | sound on (every function above returns at once when 0) |
| `0x786d48` / `0x6dc538` | view width 480 / height 380 |
| `0x798538` / `0x79853c` | view scroll x / y in pixels |
| `0x517de8` | map width in cells (i32) |

## 1. The channels (verified)

The Miles state holds **6 channels**. `FUN_0046f210` only accepts channel
numbers below 6, and `FUN_0046f4f0` walks the records `0x00 .. 0xb4` in
steps of 0x24.

- **Channel 0 is the speech channel.** `FUN_0046faa0` plays every voice on
  it, and the voice rules (§3) test its status and sample id.
- **Channels 1–5 are the effect channels.** Every positional effect asks
  for channels 1..5 (`FUN_0046f540` pushes `first = 1, last = 5`).
- `FUN_004762f0` asks for **0..5** in mode 0 (§2). In the game it is used
  only for the mission end names (which divert to the voice queue) and by
  the menu button handler `FUN_00437600` (`BUTTON`), so effects in play
  never use channel 0.

### 1.1 Picking a channel: `FUN_0046f210` (verified)

```
if last < first: last = first
if first >= 6 or last >= 6: return none
best = -1, best_vol = 0xff
for c in first .. last:
    if channel c is not playing: play here; return c
    if volume(c) < best_vol: best_vol = volume(c), best = c   // strict: the first quietest wins
if mode != 1 and best >= 0 and (best_vol < vol or mode == 0):
    play on best (it is cut off); return best
return none
```

- **mode 0**: always takes the quietest channel when all are busy;
- **mode 1**: never cuts a sound off;
- **mode 2** (positional effects): cuts off the quietest channel only when it
  is **strictly quieter** than the new sound. Otherwise the new sound is
  dropped.

`volume(c)` is `AIL_sample_volume`, i.e. the value `FUN_0046f330` set at
the start of that sound (§2), so it already includes `gSFXVolume`.

Playing (`FUN_0046f170`): stop, end and re-init the Miles sample *first*,
then load the sample into the 16-slot cache (`FUN_0046f080(id, 0)`). Only
if that succeeds is the file set, pan and volume applied, the loop count
set to 1 and the sample started. So a missing file still silences the
chosen channel.

### 1.1 The sample cache: `FUN_0046f080(id, 0)` (verified)

The sample manager (`0x795608`, 0x1fc bytes) holds 16 cache slots of 12
bytes at `+0xdc`: sample id (`-1` = free, set by `FUN_0046eee0`), a
0x10000-byte buffer, and a stamp. Each channel record (0x24 bytes from
`+4`) keeps the id it last played (`+0x1c`) and the slot it used (`+0x20`);
both start at `-1` (`FUN_0046eda0`).

1. **Hit:** a slot with this id is returned at once. The stamp is **not**
   refreshed.
2. **Free slot:** the first slot with id < 0 is loaded (`FUN_0046ef90`):
   the id's file name (samples.uib, `FUN_0048de10`) is looked up in the
   `SOUND.RS` directory (`FUN_0046ecc0`, case-insensitive, ".WAV" cut);
   not found, or larger than 0xffff bytes, fails (-1, the slot stays free).
   On success the slot gets the id and the stamp `gGameTicks`
   (`0x5173f4`).
3. **Full:** for each of the 6 channels whose Miles status is "playing"
   (`AIL_sample_status & 4`), its slot (`+0x20`) gets the stamp
   `gGameTicks`; a playing channel with a slot outside 0..15 makes the
   whole load fail (-1). Then the **first** slot with the lowest stamp
   **below** `gGameTicks` is reloaded; if no stamp is below it, -1.

`FUN_0046f170` writes the channel's id and slot only after a successful
load. A failed load leaves the channel stopped and silent.

Consequences, all from the code:

- Every 1.06 `SOUND.RS` entry is at most 63 722 bytes, so the size test
  never fails. All `S_*` effect names in samples.uib point to `SOUND.RS`
  files (none also exists as `GAMESFX\*.AUD`); `S_LASERTURRET`
  (`lasturt1`) and the harvester voices `*_HARVLOST` / `*_HARVSTUCK` name
  files that exist nowhere and stay silent.
- Only the channel plays (`FUN_0046f210`) use the cache: effects
  (`FUN_0046f540`), speech whose AUD is missing (`FUN_0046faa0`: BUTTON,
  `S_MONEYTICK`, `S_ICONSCROLL`, ...), and `FUN_004762f0` (UI
  `PlaySample`, channels 0..5, mode 0).
- A speech stream (AUD) on channel 0 does not touch its slot index. Until
  channel 0 has played one cached sample, that index is -1, so with a
  full cache every sample that is not cached fails while a voice streams.
- `gGameTicks` is set to 0 at each mission start (`0x44861e`, EBP = 0 from
  `0x4485b5`), counts up per tick (`0x449a5e`) and is restored by a load.
  The manager, and with it the cache, is only rebuilt at start-up
  (`FUN_0044a870` → `FUN_0046f450`) and after each movie
  (`FUN_0048d880`). So after a long game, a second game without a movie
  in between starts with slots stamped higher than its tick count: those
  cannot be reloaded until the count passes their stamps, and a sample
  that is not cached may stay silent for a long time.

Ours: `audio::SampleCache` (`src/audio/channels.hpp`), one per process
(`audio::sample_cache()`), reset after each movie; the game stamps with
the world's tick count. The front end's `PlaySample` uses it with our
one-player model (the speech stream as channel 0, the sample player as
the next channel).

## 2. Positional effects: `FUN_0046f540(id, cx, cy)` (verified)

Nothing when the id is negative. The cell bytes are `cx & 0xff`,
`cy & 0xff`. All divisions below truncate (the exe uses `cdq; and edx, 31`
for the signed `/ 32` and unsigned `div` for the rest).

```
hw = (480 / 2) / 32 = 7           // from 0x786d48
hh = (380 / 2) / 32 = 5           // from 0x6dc538
centre_x = scroll_x / 32 + hw     // 0x798538
centre_y = scroll_y / 32 + hh     // 0x79853c
dx = |centre_x - cx|, dy = |centre_y - cy|
W = map width (0x517de8)

pan = 0x40
if dx > hw:
    pan = 0x40 - dx·0x40 / W      (unsigned)
    if pan < 0x10: pan = 0x10
    if cx > centre_x: pan = 0x80 - pan   // to the right

vol = 50
d = max(dx, dy)
if dx > hw or dy > hh:              // outside the view's cell box
    if d >= W: vol = 5
    else:
        vol = 35 - d·35 / W         (unsigned)
        if vol < 5: vol = 5

vol = vol · gSFXVolume / 100        (unsigned; 0x51eb851f · >> 5)
FUN_0046f210(id, pan, vol, 1, 5, 2)
```

Consequences:
- **On screen** (|dx| ≤ 7 and |dy| ≤ 5 cells from the centre cell): pan
  centre, volume 50 (× SFX / 100). Note the box is 15 × 11 cells, a little
  larger than the 480 × 380 view.
- **Off screen**: a step down to 35, then linear fall-off with the
  Chebyshev distance, at least 5. **There is no distance cut-off**: a sound
  anywhere on the map plays, at volume 5 at worst.
- **The distance scale is the map width**, also for `dy`.
- **Pan** only changes when the sound is off screen horizontally; it runs
  from 0x10 (far left) to 0x70 (far right), never fully to one side. Miles
  pan is 0 = left, 64 = centre, 127 = right.
- The maximum effect volume is **50 of 127**, speech plays at
  `gSFXVolume` (up to 100 of 127, §3.3): effects are about half as loud as
  speech.
- **Shroud is not tested here.** Callers that care test it themselves
  (worm sign, thumper: doc 17, doc 54).

### 2.1 "Not playing already" (verified)

`FUN_0046f4f0(id)` is true when any of the 6 channels is playing with
that sample id. Callers: the worm sign (`0x49af0b`) and the thumper
(`0x49b5cd`) in `FUN_00497b90`; both skip `FUN_0046f540` when it is true.

## 3. The voice queue: `FUN_0046f820(id, mode, limit, prio)` (verified)

1. **Repeat limit** (limit ≠ 0): per sample id, the tick it last passed
   (`table[id] + 8`). If set and `tick < last + limit`, return; else store
   the tick. This happens before the mode test, so the tick is stored even
   when the voice is then not queued.
2. **mode 0**: queue only if **both rings are empty** (`FUN_0046f6e0`)
   and (channel 0 is not playing, **or** neither the new id nor channel
   0's id is `S_MONEYTICK`). So a mode-0 sound waits behind the voice that
   is playing, but never behind or in front of the money tick, and never
   when anything is queued.
3. **mode ≠ 0**: queue unless channel 0 is playing this same id, or the
   id is already in ring `prio` (`FUN_0046f700`; the other ring is not
   searched).
4. **Append** (`FUN_0046f770`): `next = (tail + 1) % 5`; if `next == head`
   the ring is full and the id is **dropped**. So each ring holds **4**
   voices, not 5. A prio above 1 logs "Error with SFX priority" (none is
   used).

### 3.1 Starting a voice: `FUN_0046faa0` (verified)

Once per loop pass: if either ring has an entry and no speech is active
(`0x79562c` = 0), take the next id (`FUN_0046f7d0`: **ring 1 first**),
re-init channel 0, and play `GameSFX\<file>.AUD` streamed on channel 0 at
pan 0x40 and volume `gGameState == 2 ? gSFXVolume : 100`. If the AUD is
missing it plays the sample (SOUND.RS) through `FUN_0046f210(id, 0x40,
vol, 0, 0, 0)`, i.e. channel 0 in mode 0. `0x79562c` is cleared when a
later pass finds channel 0 stopped.

### 3.2 Who uses which mode and priority (verified per call site)

`FUN_0046f900` pushes `(S, O, H, mode, limit, prio)`; the direct calls
push `(id, mode, limit, prio)`. Every call site:

| Mode 0 (prio 0, limit 0) | Site |
|---|---|
| `*_PRIMARY` | `0x445b36` |
| `*_RETREAT`, `*_GUARDING` | `0x444819`, `0x445f61`, `0x445fdd` (EDI / ESI = 0) |
| `*_PORTORDER` | `0x444a61` → `0x444b62` (EDI = 0) |
| `*_CANCELLED` | `0x444b62`, `0x4561fa`, `0x45585c` |
| `*_BUILDING` / `*_TRAINING` (start / resume) | `0x4556c2` (EDX = 0), `0x4560dd` (EAX = 0) |
| `*_ONHOLD` | `0x457573` |
| `*_SELL` | `0x456fa3` |
| `*_BUILDUPGRADE` | `0x457481` (EBP = 0) |
| `*_REPAIRUNIT`, `*_UNITREPAIRED` | `0x49a5b9`, `0x49a6a6`, `0x49a848` |
| unit select / ack voices | `0x445329`, `0x49f2dc`, `0x49f2fa` |
| `S_POWERDOWN`, `S_POWERUP`, `S_RADARONLINE` | `0x44ab3c`, `0x44ab85`, `0x44abbf` |
| `S_CHATMSG` | `0x454415` (SHOWMESSAGE), `0x45ca04`, `0x458148`, `0x458397` |
| `S_ICONSCROLL` / `S_ENDSCROLL` (sidebar scroll) | `0x4445d5`, `0x44492c` |
| `BUTTON` (sidebar repair / sell toggles) | `0x444712` |
| `S_MONEYTICK` (limit 15) | `0x46c914`, `0x46c99d` |
| `S_SCORETICK` (score screen) | `FUN_0043f030` |

| Mode 1 | Limit | Prio | Site |
|---|---|---|---|
| `*_REINFORCE` | 0 | **1** | `0x46e1ef` |
| `*_DEATHLAUNCHED` | 0 | **1** | `0x49e9bc` |
| `*_FRIGATEIN*` | from `0x6b8834` | 0 | `0x46e050` |
| `*_LOWPOWER`, `*_SILOSNEEDED`, `*_LOWMONEY` | 2000 (some LOWMONEY 0) | 0 | `0x446dcd`, `0x446e2c`, `0x446e89`, `0x4570b9`, `0x49a6fb`, `0x49a995`, `0x4560dd` (the branch from `0x456057`) |
| `*_UNDERATTACK`, `*_HARVATTACK` | 300 | 0 | `0x49d5ce`, `0x49db4d` |
| `*_WORMSIGN` | 2000 | 0 | `0x49aefb` |
| `*_BUILDREADY`, `*_UNITREADY`, `*_DEATHREADY`, `*_NEWOPTIONS`, `*_DEATHPREPPING`, `*_BUILDINGLOST`, `*_UNITLOST`, `*_WORMATTACK`, `*_CAPTURE`, `*_CANTBUILD`, `*_CANTDEPLOY`, `*_GAMELOADED`, `*_GAMESAVED`, `*_MISSION*` | 0 | 0 | the other `FUN_0046f900` sites |

The register values (EDI, ESI, EDX, EAX, EBP) were followed back to their
last write in the function's listing (`xor reg, reg`). The three EDI / ESI
sites in `FUN_004433a0` (RETREAT / GUARDING / PORTORDER: `0x444819`,
`0x444b62` with `0x444a61`, `0x445f61`, `0x445fdd`) were checked along
every path: a constant propagation over the whole function's control
flow (0x4433a0-0x44667d, jump tables followed, calls keeping
EBX / ESI / EDI / EBP) reaches all its instructions and gives EDI = 0 at
the first two sites and ESI = 0 at the last two on every path. So mode,
limit and prio are 0 there.

### 3.4 Sounds and the loop pass (verified)

- **Effects** start inside the tick (`FUN_0046f540` → `FUN_0046f210`),
  with the channels' status as it is then. **Voices** are only queued in
  the tick; `FUN_0046faa0` starts one at the **top of the next pass**
  (`0x448f78`), before the UI manager (`0x449204`), the orders
  (`FUN_004433a0`, `0x449852`) and the tick.
- Ours collects the tick's sounds and plays them right after the tick in
  the same order, then starts the next voice. Within one tick nothing
  starts a voice and the channels do not change between the exe's calls
  except by finishing in real time, so the picks and the queue are the
  same; a voice starts less than one pass earlier.
- The sidebar's BUTTON / scroll sounds are queued by `FUN_004433a0`
  before the tick, so we queue them before the tick's voices (mode 0
  needs empty rings, so the order matters). `S_MONEYTICK` is queued by the
  cash drip (`FUN_0046c7d0`, local side only, `0x46c8fe`) inside the model
  update (`FUN_0044aca0` → `FUN_0044abe0`, after the units); ours queues
  it as a sound event at that point of the tick.
- `FUN_0046f980` (speech stop) only empties both rings (`FUN_0046f6a0`)
  and clears the "speech active" flag `0x79562c`; it does not stop the
  Miles sample, which plays out the at most two 0x8000-byte buffers it
  already has. Callers: the end of the game loop (`0x44a0ef`) and a load
  (`0x441d24`).
- **`S_MISSIONABORT` is never heard.** The Abort buttons (`confirmA.uil`,
  `abortMP.uil`) run `PlaySample S_MISSIONABORT` (→ `FUN_004762f0` → the
  voice queue), `SetValueECTo gGameState GameStateBriefing` and
  `QuitAndJumpToMenu ABORT_MP_GAME` (`abortMP` adds `AbortGame`,
  `FUN_00492eb0`, which plays nothing). The UI manager runs these at
  `0x449204`, after the pass's `FUN_0046faa0`; the end-of-pass test
  (`0x44a0b6`) leaves the loop and `0x44a0ef` empties the queue before
  any `FUN_0046faa0` runs again. Our in-game Abort drops it the same way.

### 3.3 Volumes

- Speech: `gSFXVolume` in a mission (`gGameState` 2), else 100.
- Effects: §2, at most 50 · `gSFXVolume` / 100.
- `FUN_004762f0` names other than `S_MISSION*`: like speech, pan 0x40.
- The volume is applied **when a sample starts**. Moving the SFX slider
  changes later samples only (doc 20 §3); nothing re-applies it to a
  sample that is playing.

## 4. Summary for the reimplementation

- 6 channels: speech (0) + 5 effects (1–5), each remembering its sample and
  volume.
- Effects: `FUN_0046f540` → pan / volume → channel pick in mode 2.
- Voices: two rings of 4 usable slots, ring 1 first; the mode-0 rule;
  channel 0 only.
- The sidebar's own clicks (`BUTTON`, scroll) go through the voice queue
  in mode 0, not through an effect channel.
- Volume and pan follow Miles' DirectSound law (§7): gain about
  `volume / 127`, the near side at full gain and the far side about
  `pan / 63` (or `(127 - pan) / 63`), both rounded down to 1/100 dB.

## 5. Where it is implemented

| Rule | Code |
|---|---|
| channel pick, positional pan / volume, "already playing", the voice rings | `src/audio/channels.hpp` (no SDL; `tests/audio/test_channels.cpp`) |
| Miles volume / pan law (§7) | `miles_volume_db`, `miles_pan_db`, `volume_gain`, `pan_gains` in `src/audio/channels.hpp` |
| per-sample volume and pan on a stereo stream | `src/audio/sample_player.cpp` (music: `src/audio/music_player.cpp`) |
| the 6 channels, effects, speech service | `GameScreen::play_sounds`, `play_effect`, `service_speech` |
| voice mode / prio per name | `World::voice` (`src/sim/placement.cpp`) |

## 6. Corrections to earlier docs

- **doc 12 §9:** the voice rings hold 4 voices each (a full ring is
  `(tail + 1) % 5 == head`), not 5. `FUN_0046f6c0(i)` is "ring i is empty",
  so the mode-0 condition is "both rings empty" (was inferred).
- **doc 12 §9:** the channel pick takes the quietest channel only when it
  is strictly quieter than the new sound, and only in mode 2; mode 0
  always takes it, mode 1 never.
- **§4 (earlier version of this doc):** the far side of a pan below 64
  falls as `pan / 63`, not `pan / 64`, and pan 63 is centre too (§7).

## 7. Miles volume and pan law

Source: the game's own `MSS32.DLL` (1.06 install, 160 768 bytes,
SHA-256 `9686B6401809DEC26D59C7840A7AD48494C0452E0A08AAE4DF9342676D5819EB`),
imported read-only into a separate headless Ghidra project (image base
0x20000000). Addresses below are in that DLL. Verified from the
disassembly unless marked.

### 7.1 What the game calls (verified)

The exe imports from `MSS32.DLL` only `AIL_set_sample_volume`,
`AIL_set_sample_pan` and `AIL_sample_volume` for levels; there is no
`AIL_set_digital_master_volume`, `AIL_set_sample_volume_pan` or stream
call. Set-up (`FUN_0046f450`, `FUN_0046eda0`):

- `AIL_startup`, then `AIL_set_preference(8, 1)`, `(0, 8)`, `(1, 0x7f)`.
  Preference 15 (stored at `0x20024ecc` = `0x20024e90 + 15·4`, default 0
  from the defaults list in the DLL's start-up) is **not** changed.
- `AIL_waveOutOpen(&drv, 0, 0, fmt)` with `fmt` = PCM, **2 channels,
  22050 Hz, 16-bit** (`0x20001`, `0x5622`, `0x15888`, `0x100004`).

### 7.2 Which mixer (verified)

Every level export branches on `0x20024ecc` (preference 15):

| `0x20024ecc` | `AIL_waveOutOpen` | `AIL_set_sample_volume` / `_pan` |
|---|---|---|
| 0 (the game) | `FUN_2000d4f0`: DirectSound (`DSOUND.DLL`, `DirectSoundCreate`) | `FUN_2000e5a0` → `FUN_2000de00` / `FUN_2000e5f0` → `FUN_2000def0` |
| ≠ 0 | `FUN_2000b680`: Miles' own waveOut mixer | `FUN_2000c760` / `FUN_2000c790` → `FUN_2000c490` (tables) |

So the game uses the **DirectSound path**. Each Miles sample gets its own
secondary buffer (`FUN_2000df50`: `CreateSoundBuffer` with flags `0xe0` =
CTRLFREQUENCY | CTRLPAN | CTRLVOLUME, in the sample's own format), and
DirectSound mixes them. When a sample starts (`FUN_2000dd50`) Miles calls
`FUN_2000de00` (volume) and `FUN_2000def0` (pan) and then `Play`; a later
`AIL_set_sample_volume` / `_pan` on a sample with a buffer calls them
again (only if the value changed).

### 7.3 Volume: `FUN_2000de00` (verified)

```
vol = clamp(sample.volume (+0x50), 0, 127)          // stored back
v   = clamp(driver.master (+0x90) * vol / 127, 0, 127)  // master = 0x7f at open
IDirectSoundBuffer::SetVolume(FUN_2000de70(0, 127, v))  // vtable +0x3c
```

`FUN_2000de70(lo, hi, x)` (ECX, EDX, stack):

```
if x == lo: return -10000          // 0xffffd8f0, DSBVOLUME_MIN
if x == hi: return 0
q = (hi + lo) as double; if x != 0.0: q /= x
return __ftol(log10(q) * -2000.0)  // FLDLG2; FYL2X; FMUL [0x2001e008] = -2000.0
```

`__ftol` truncates toward zero. So `SetVolume = trunc(-2000 · log10(127 /
v))` hundredths of a dB, i.e. **20·log10(v / 127) dB rounded towards 0
dB**: the amplitude is `v / 127` within +0.115 % (1/100 dB). With the
master at 127 (`puVar4[0x24] = 0x7f` in `FUN_2000d4f0`, and the game never
changes it) `v = vol`. Examples: 100 → -207, 50 → -809, 35 → -1119,
5 → -2809, 0 → -10000 (silent).

### 7.4 Pan: `FUN_2000def0` (verified)

Only when the driver output is stereo (`driver+0x70` not 0 / 1; the game
opens 2 channels, so 3):

```
pan = sample.pan (+0x54)                 // not clamped
if pan > 64:  SetPan( -FUN_2000de70(0, 63, 127 - pan))   // > 0: left quieter
if pan < 64:  SetPan(  FUN_2000de70(0, 63, pan))         // < 0: right quieter
if pan == 64: SetPan(0)                                  // vtable +0x40
```

DSBPAN attenuates only the far channel; the near one stays at full gain.
So the far side's amplitude is `pan / 63` for pan < 64 and `(127 - pan) /
63` for pan > 64 (again within 1/100 dB, rounded towards louder). Pan 63
and 64 are both centre; 0 and 127 silence the far side. The game's pans
(§2): 0x10 → right side -1190 (×0.2541), 0x70 → left side -1246 (×0.2382).

The meaning of DSBVOLUME / DSBPAN (hundredths of a dB of attenuation,
amplitude `10^(value / 2000)`, -10000 = silence) is DirectSound's own,
outside `MSS32.DLL`.

Truncation check: for every `x` in 1..126 (hi 127) and 1..62 (hi 63) the
exact value is at least 0.0024 from an integer, so computing in IEEE
double (instead of the x87's extended `FYL2X`) gives the same integers.

### 7.5 The waveOut mixer (not used by the game)

For completeness: with preference 15 set, `FUN_2000c490` clamps volume
and pan to 0..127, takes `v = master·vol / 127`, and for a 16-bit sample
stores `left = T[127 - pan]·v / 127`, `right = T[pan]·v / 127`, where the
table at `0x20022618` is `T[i] = 2i` for i = 0..62 and `T[i] = 128` for
i = 63..127 (8-bit samples get 256-entry lookup tables built from the same
values, with `v + 1`). That is a balance law with the far side `pan / 64`.
Its final shift is not read: preference 15 stays 0 in the game (§7.1), so
this mixer is never used and has no effect on what the game plays.
