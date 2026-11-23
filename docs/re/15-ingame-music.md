# 15: In-game music, the jukebox and music volume (1.06)

Covers which track plays in a mission, what happens when it ends, the
SOUNDOPTIONS jukebox, the volumes, and how music meets speech and movies.
Menu music (OPTIONS.AUD / SCORE.AUD, the stream itself) is in
`docs/re/04-menu-flow.md#menu-music`.

Main functions:

| Address | Role |
|---|---|
| `FUN_00448550` | game loop; mission set-up starts the first track |
| `FUN_0046fd90` | open `MUSIC\<file>` and prime 3 buffers (doc 04) |
| `FUN_0046ff30` | refill one buffer; at end of file: loop, and flag "next track" |
| `FUN_004700e0` | play a track and set its volume |
| `FUN_004700b0` | set the music volume (Miles 0..127) |
| `FUN_00470070` | stop the music |
| `FUN_00481460` | if "next track" is flagged, pick and play one |
| `FUN_00481490` | pick a random track for the house, not the current one |
| `FUN_00481700` | is this file in the house's random pool? |
| `FUN_00476590` | `GetCDTracks`: fill the jukebox list |
| `FUN_004764b0` | `PlayCDTrack` (UI command 0xd4) |
| `FUN_00476470` | `StopCDTrack` (UI command 0xd5) |
| `FUN_00476770` | n-th file in `MUSIC\*.aud` |
| `FUN_00477790` | `Campaign.txt` loader; names the music column |

Globals:

| Address | Name (exe global table) | Meaning |
|---|---|---|
| `0x797a98` | – | current track name, no extension, 10 bytes in saves |
| `0x795600` | – | "track ended, pick the next one" |
| `0x5179d0` | – | 1 only while the game loop runs |
| `0x4e5fc8` | `gCDVolume` | music volume 0..100 |
| `0x4e5fc4` | `gSFXVolume` | SFX and speech volume 0..100 |
| `0x4e5fbc` | `gCurrentCDTrack` | jukebox list selection (UI only) |
| `0x795604` | `gRepeatTrack` | no effect (see §4.4) |
| `0x5135b8` | – | music folder path (`gMUSIC_RES_PATH`) |

The global names come from the exe's table of `{name[60], address}`
records (e.g. `gCDVolume` at `0x4c9640`, its address `0x4e5fc8` at
`0x4c967c`).

"Verified" means read from the code or the data. "Inferred" is marked.

## Corrections to earlier docs

- **doc 02, settings table:** `0x4e5fc4` is `gSFXVolume` and `0x4e5fc8` is
  `gCDVolume` (the music volume). The OPTIONS sliders `fxVol` and `cdVol`
  bind to them by name.
- **formats/campaign.md, offset 0xF4 (column 25):** this is the music track
  that starts the mission (§1). Verified.

## 1. The first track of a mission (verified)

In the mission set-up of `FUN_00448550`, unless a save game is being
loaded (`0x5178bc` = 0):

- **Campaign** (`gGameType` `0x797e34` = 0): the track is
  `campaign[gSideId][gMissionNumber].music + ".aud"`. The table is
  `campaign.uib` at `0x795708`; the music name is at `+0xF4` of each
  0x104-byte record (`0x7957fc + (mission + side·10)·0x104`, read at
  `0x448c98`). `gSideId` is `0x798544`, `gMissionNumber` is `0x4de610`
  (1..9).
- **Every other game type** (skirmish, network, …): always
  `arakatak.aud` (`0x448d02`).

The name (without `.aud`) is copied to `0x797a98`. Then the volume is set to
`gCDVolume` (`FUN_004700b0`, `0x448d3a`).

Campaign tracks in 1.06 `campaign.uib` (record 0 is the campaign start and
is not a mission):

| Mission | Atreides | Harkonnen | Ordos |
|---:|---|---|---|
| 1 | atregain | risehark | entordos |
| 2 | spicesct | landsand | fightpwr |
| 3 | fremen | hark_bat | Arakatak |
| 4 | undercon | robotix | landsand |
| 5 | waitgame | undercon | robotix |
| 6 | fremen | risehark | entordos |
| 7 | robotix | spicesct | spicesct |
| 8 | fightpwr | hark_bat | undercon |
| 9 | atregain | fightpwr | waitgame |

So the first track is fixed per house and mission. It is not random.

**Loading a save** (`FUN_004418b0`): the save holds the track name (10
bytes at `0x797a98`) and both volumes. After loading, the exe empties the
voice queue (`FUN_0046f980`, `0x441d24`), then plays `<name>.aud` from its
start (`0x441d5b`), even when that track is already playing. An empty
name stops the music (`FUN_00470170`). `FUN_004700e0` sets the volume to
`gCDVolume` (the loaded one) because `gGameState` is 2 in both cases: an
in-game load runs in the game loop, and a main-menu load sets
`gGameState` = 2 before the game loop calls the loader (`0x448bc4`, doc
27 §5). The set-up above then skips its own track start and volume
(`0x448c63`), but takes `gCDVolume` as the last applied value
(`0x448d44`), so nothing re-applies it.

A **globe save** loaded from the front end (`FUN_004418b0(path, 0)`, doc
27) runs the same restart, at volume 100 (`gGameState` 1). The
`QuitAndJumpToMenu LOADMISSION` that follows makes another menu the top
while `gGameState` = 1, which stops the music again (doc 04, menu music),
and the mission set-up replaces the name. So that track is not heard
beyond the pass in between; we do not start it.

## 2. When a track ends (verified, one part inferred)

The refill `FUN_0046ff30` runs at the top of every game loop iteration
(`0x448f11`, trace label "Update Music buffers"). When a refill reads
nothing, it seeks back to offset 12 and refills from the start (doc 04).
In addition, at `0x470030`–`0x47004c`, if

- `gGameState` (`0x4dfb08`) = 2 (in game), **and**
- `gGameType` = 0 (campaign), **and**
- `0x5179d0` is set (the game loop is running),

it sets `0x795600` = 1.

The UI manager `FUN_004712c0` calls `FUN_00481460` first thing, every loop.
If `0x795600` is set (and Miles is up), it clears it and plays
`FUN_00481490(current name)` through `FUN_004700e0`.

**`FUN_00481490`: the random pick.**
1. House = `0x798544` (0, 1, 2; anything else = 3, which logs
   "bf TrackForHouse(house)" and uses only the common pool).
2. List `<music path>*.aud` with `FindFirstFileA` / `FindNextFileA`. Keep
   the files that `FUN_00481700` accepts, in listing order.
3. `FUN_00481700` accepts (case-insensitive file names):
   - house 0: `atregain.aud`; house 1: `hark_bat.aud`, `risehark.aud`;
     house 2: `entordos.aud`;
   - all houses: `arakatak`, `fightpwr`, `fremen`, `landsand`, `robotix`,
     `spicesct`, `undercon`, `waitgame`, `soldappr`, `plotting`, `ambush`.
   - Never: `options.aud`, `score.aud`, or another house's title tracks.
   So the pool has 12 tracks (13 for Harkonnen).
4. Pick `pool[rand() % count]`. `rand()` is the shared MSVC RNG
   (`0x448460` is a thunk to `FUN_004b1810`, state `0x4ed870`, doc 06).
5. While `strnicmp(pick, current, strlen(current)) == 0`, pick again. So the
   same track never plays twice in a row.
6. Store the pick without its last 4 characters (`.AUD`) in `0x797a98`.

Consequences:
- **Campaign:** after the fixed first track, tracks are random from the
  house pool, never the same one twice in a row. `soldappr`, `plotting`
  and `ambush` are never a first track but can come up here.
- **Skirmish and multiplayer:** `arakatak.aud` loops forever (only the
  jukebox changes it).
- **The pick draws from the shared `rand()`.** In the campaign it changes
  the RNG state that sim code (e.g. spice blooms, doc 13) also uses. To
  match the original, our pick must call the same `rand()` at the same
  moment (in the UI step of the loop where the end was found).
  The pass order (`FUN_00448550`): the refill at the top (`0x448f47`)
  sets the flag; the UI manager (`0x449204`) picks in the same pass; both
  come before the orders (`FUN_004433a0`, `0x449852`) and the tick
  (`gGameTicks` at `0x449a5e`, the sim after it). With a menu open the
  pass skips to `0x4491e8`, so the refill and the pick still run. Ours
  does the same (refill and pick before the input handler and the tick).
- **The file listing order matters** for which track a given `rand()` value
  picks. The exe does not sort: `FUN_00481490` keeps the
  `FindFirstFileA` / `FindNextFileA` order, `FUN_00476590` the
  `_findfirst` / `_findnext` order. NTFS returns names in upper-case order
  (checked on the 1.06 install folder: AMBUSH, ARAKATAK, ATREGAIN, ...,
  WAITGAME) and ISO 9660 directories are sorted by name, so we sort by
  upper-case name. (A FAT-formatted install would list in creation
  order; we do not reproduce that.)

**The end of each track is cut** (verified). The refill (`FUN_0046ff30`)
hands app buffer `+0x1a8` to Miles when one of Miles' two buffers is free,
then reads the next chunk into that app buffer; so the three app buffers
run three chunks ahead of Miles. The empty read therefore happens while
the old track still has unplayed audio: the Miles buffer that just
started, the one just handed over, and the two app buffers with the last
chunks. The switch runs in the same pass. `FUN_0046fd90` calls
`AIL_init_sample` on the music sample, and in `MSS32.DLL` (DirectSound
path) that is `0x2000e340`, which releases the sample's DirectSound
buffer (`0x2000dbc0`, `IDirectSoundBuffer::Release`) and resets the
buffer state: everything queued is dropped at once. So about three to
four chunks (0x8000 bytes = 0.74 s each, the last one partial) of each
track's end are never heard in the campaign. In skirmish the loop instead
plays those buffers, then the start again. `audio::MusicPlayer` keeps the
same three app buffers and two Miles buffers.

There is **no crossfade, no fade-out and no pause** between tracks. The new
track starts in the same loop.

## 3. Music around a mission (verified)

- **Leaving the menus:** when the front-end loop ends (`FUN_00442580`), the
  menu music stops (`0x442791`) and the name is cleared. Loading is silent.
- **Briefing movies:** a MOVIE element shuts Miles down while it plays
  (`FUN_0048d730`: stop music, `AIL_shutdown`) and starts it again when the
  element is destroyed (`FUN_0048d880`, which also clears the track name).
  No music plays under movies.
- **Menus in the front end:** doc 04. Any menu other than MAINMENU / SCORE*
  on top stops the music while `gGameState` = 1.
- **Start of the mission:** §1. The track is opened during set-up. The first
  buffer goes to Miles at the top of the first loop iteration. There is no
  fade-in of the music.
- **In-game menus** (the in-game menu, OPTIONS, SOUNDOPTIONS, objectives):
  the stop rule in `FUN_00473130` / `FUN_00473300` / `FUN_004734e0` only
  applies while `gGameState` = 1. In game (state 2) the music keeps playing,
  and the refill at the top of the loop keeps running.
- **No combat / attack music.** No code switches tracks on attacks.
  Nothing else calls `FUN_004700e0` in game (callers: set-up, save load,
  menu stack, `FUN_00481460`).
- **Victory / defeat:** the game queues `S_MISSIONWON` / `S_MISSIONFAILED`
  (`0x449bb9`, `0x449dde`). Then it runs a busy loop that only services the
  speech and the music until the speech ends. The music keeps playing under
  it. When the game loop ends, `0x44a0ea` stops the music and the speech,
  and clears `0x5179d0` and `0x795600`. The SCORE menu then starts
  `score.aud` (doc 04).
- **Restart mission** (`0x797e1c` jumps back to the set-up): the first track
  starts again.

## 4. Options and the jukebox

### 4.1 Where (verified, UI data)

The in-game menus `InGame`, `InGameSK` and `InGameMP` all open `OPTIONS`
(`options.uil`; `MP_OPTIONS` is no longer used). The front end has no
OPTIONS menu. OPTIONS has:
- `cdVol` slider: "Music Volume", 0..100, step 5, bound to `gCDVolume`.
- `fxVol` slider: "SFX Volume", bound to `gSFXVolume`.
- `music` button: `JumpToMenu SOUNDOPTIONS` (`sound.uil`).

There is no music on/off switch. Volume 0 is silence. (If no `MUSIC` folder
exists under the install path, the exe reads music from the CD;
`0x797df4`, `FUN_004700e0`. Not relevant to us.)

### 4.2 Volume (verified)

- Miles volume is 0..127. Menus use 100. In game the music volume is
  `gCDVolume` itself (0..100), so 100 % = 100/127, the same as the menus.
- The loop compares `gCDVolume` with the last value it applied, every
  iteration, and re-applies it when it changed (`0x448f1b`). The slider
  takes effect at once.
- `gCDVolume` is saved in `dune2000.cfg` and in save games.

### 4.3 SOUNDOPTIONS: the jukebox (verified)

Title "MUSIC SELECTION". Elements: list `musicList`, buttons Play, Stop,
Done.

- **The list** (`GetCDTracks` → `FUN_00476590`): lists `MUSIC\*.aud` with
  `_findfirst` / `_findnext`. For each file it looks up the string
  `music_<name>` in `text.uib`. Files with a string are added, with the
  string as the label. So OPTIONS.AUD and SCORE.AUD are not listed (15
  items). The item whose name equals the current track is selected.
- **Play** (`PlayCDTrack musicList` → `FUN_004764b0`): takes the selected
  item number (1-based, list `+0x7e8`), and plays file number
  `item − 1` of the **unfiltered** listing (`FUN_00476770`). It calls
  `FUN_0046fd90` directly, not `FUN_004700e0`, and stores the name in
  `0x797a98`.
- **Stop** (`StopCDTrack` → `FUN_00476470`): stops the music and clears the
  name.

Three original quirks follow from this (verified from code and data; the
code paths leave no choice, so no listening test is needed):

1. **Wrong file after item 8.** The list skips OPTIONS and SCORE, but Play
   counts them. With a sorted listing, items 1–8 (Ambush … Land of Sand)
   are right, then: Plotting → OPTIONS.AUD, Rise of Harkonnen → PLOTTING,
   Robotix → RISEHARK, The Soldiers Approach → ROBOTIX, Spice Scouting →
   SCORE.AUD, Under Construction → SOLDAPPR, The Waiting Game → SPICESCT.
2. **Swapped labels in `text.uib`:** `music_risehark` = "Robotix" and
   `music_robotix` = "Rise of Harkonnen".
3. **Volume after Play:** `FUN_0046fd90` sets the Miles volume to 0x40
   (64/127) and nothing re-applies `gCDVolume` until the slider value
   changes. So a jukebox track plays at about half the normal level.

A track picked with Play follows §2 at its end: random in the campaign,
loops otherwise.

### 4.4 Repeat (verified: no effect)

`sound.uil` has a toggle "Loop" / "Single" for `gRepeatTrack`, but its
`TOGGLEBUTTON repeat` header line is missing. The loader skips the block
(doc 03), so the toggle does not exist. Nothing in the music code reads
`gRepeatTrack` (`0x795604`) either; its only users are the generic UI value
commands.

## 5. Music and speech (verified)

- Speech (`FUN_0046faa0`, `GameSFX\<name>.AUD`) uses its own Miles sample
  (`*(0x795608) + 4`). It never touches the music sample.
- **No ducking.** The music volume is only set by `FUN_004700b0`, whose
  callers are the set-up, the volume check in the loop, and
  `FUN_004700e0`.
- Speech volume in game is `gSFXVolume`, else 100.

## 6. For the reimplementation

`audio::MusicPlayer` can already play, stop, set the volume and refill. It
also needs:
- an "end of file reached" signal from the refill (the empty read), so the
  loop can set the next-track flag in campaign games;
- the current track name, kept in saves (10 bytes);
- the random pick with the shared `rand()`, in the UI step;
- the restart-from-the-switch behaviour (drop queued audio, §2);
- Play setting volume 0x40, as the original does (it is not a gameplay
  change, but a known quirk; keep it, or put a fix behind `fixes.ini`).

## 7. Closed points

- The cut track ends (§2): verified from the exe and `MSS32.DLL`
  (`AIL_init_sample` releases the DirectSound buffer).
- The jukebox quirks (§4.3): verified from the code and the data.
- The listing order (§2): the exe does not sort; NTFS / ISO 9660 give the
  sorted order we use.
- `gLastCDTrack`: `SetValueU4SpecialFrom` looks the target up in the U4
  table only, so an unknown name does nothing (doc 48 §A.4).
- The Miles volume curve: the DirectSound law of doc 59 §7 (not linear).
- Movies (§3): `FUN_0048d730` stops the music and shuts Miles down;
  `FUN_0048d880` clears the track name and builds a new sample manager.
  Nothing resumes the old track; menu music only starts on a later
  top-menu change.
