# Game timing and simulation arithmetic (1.06)

## Main loop

`FUN_00448550` is the in-game main loop. It is full of trace labels
(`Loop Start`, `Blitting Game View`, `Game UI Polling`, `Blit to Screen`,
`Game AI section`, `End of game loop`, `Leaving Main Loop`). Each iteration:
updates music, does UI and input (`FUN_004433a0` handles scrolling and
keys), blits the frame, pumps window messages (`FUN_004a3980`), then runs
**at most one simulation tick** ("Game AI processing"):

```
if (force_tick || (tick_due() && ticks < tick_limit)) { ticks++; simulate(); }
```

`ticks` = `DAT_006b985c`; `tick_due()` = `FUN_0045b790`.

## Tick pattern ("pulse")

`tick_due()` walks a 100-entry byte table `DAT_006b95e0`, one slot per loop
iteration, and returns the slot's value. In multiplayer, the walk waits for a
network pulse before wrapping (`ProcessPulseSignal`, `FUN_0045c740`). In
single player it simply wraps.

`FUN_0045b610` builds the table (from the exact x87 sequence):

```
speed  = DAT_004e3b18          // options "game speed" slider, 0..100, saved in dune2000.cfg
cap    = DAT_006b9864          // single player: = DAT_004e3b14 = 100 (FUN_0045c480)
adjust = DAT_006b9888          // single player: 0 (multiplayer lag adjustment)
if cap == 0: no ticks
x = speed <= 7  ? 8  : speed <= 23 ? 10 : speed <= 39 ? 12 :
    speed <= 59 ? 15 : speed <= 78 ? 20 : speed <= 94 ? 25 : cap     (float32 consts @ 0x004c942c..)
x = min(x, (float)cap) - adjust
tick_limit = ftol(x), clamped >= 0
step = (x > 0) ? 100 / x : (force ? 1 : no ticks);  step = max(step, 1.0)
                               // max: fcom 1.0f (0x4c9444) at 0x45b739;
                               // force = 0x5179cc, never written: unreachable
acc = 0; i = 0
do { table[i] = 1; acc += step; i = ftol(acc) } while (i < 100)
```

So in single player there are **8, 10, 12, 15, 20, 25 or 100 sim ticks per
100 main-loop iterations**, spread evenly (e.g. speed 8 → slots 0, 12, 25,
37, 50, 62, 75, 87). `ftol` truncates (MSVC `__ftol`, `0x004b1840`: it
sets rounding control to "chop" for its one `FISTP`, then restores the
control word). The rounding mode otherwise in effect is the x87 default,
round to nearest (see "The simulation uses floating point" below).

## What paces the main loop?

No real-time clock was found in the single-player loop:
- `timeSetEvent(16 ms)` (`FUN_00401280`) and both `timeGetTime` wrappers
  (`FUN_00402860/70`) belong to the MovieCode (VQA player).
- `GetTickCount` calls in the loop are for multiplayer and stats only.
- Screen update: when `DAT_004eb008 && DAT_004eb010` (page-flipping mode),
  `IDirectDrawSurface::Flip(NULL, DDFLIP_WAIT)` (vtable +0x2C). Otherwise
  `FUN_0042f800` does Lock / copy / Unlock, retrying only while
  `DDERR_WASSTILLDRAWING`. There is no other wait. `FUN_004a3980` is a plain
  PeekMessage pump.

The loop rate itself is not fixed: it follows the display (flip mode) or
runs free. **But the tick rate does not depend on it** (verified
2026-10-06, see below).

## The 1-second timer makes the buckets ticks per real second (verified)

`base` in the pattern is not a constant 100. `0x4e3b14` starts at 100
(`FUN_0045a7a0`, which also sets the cap `0x6b9864` = 0x3c = 60), and a
1-second `WM_TIMER` handler (exe `0x4a3796..0x4a383f`, inside the window
procedure, no Ghidra function) does, in single player:

```
if (seconds_0x6b987c >= 8) { FUN_0045c480(); seconds = 0 }   // SP: cap = base; rebuild
else seconds++
ticks_last_second_0x6b9858 = ticks_0x6b985c; ticks = 0
FUN_0045b570(frames_0x6b9860)   // if frames != 0 && frames != base: base = frames; rebuild
frames = 0
```

`frames` (`0x6b9860`) is +1 per game-view blit in `FUN_00448550`
("Blitting Game View"), reset while a menu is open over the game. The
rebuild (`FUN_0045b610`) uses `step = base / x`, so x slots out of every
`base` loop passes carry a tick, and `x` is a number of **ticks per real
second**: speed 0..7 = 8, ..23 = 10, ..39 = 12, ..59 = 15, ..78 = 20,
..94 = 25, else the cap (= the frame rate after the first 9 s: a tick
every frame). The rebuild resets the slot to 0.

**Measurement (2026-10-06, reference install, cnc-ddraw, A1V1, speed 75,
`tools/measure-reference.ps1`):** 19.98 ticks/s at about 61-70 loop
passes/s. Live values: pattern with 34 slots set (step 61/20), cap 61,
tick limit 20.

Our loop is fixed at 60 Hz, so the base settles at 60 and the cap at 60:
default speed = 20 ticks/s, as in the original.

## Settings found in dune2000.cfg (load `FUN_00478c20`, save `FUN_00478fa0`)

| Var | Size | Range / default | Meaning |
|---|---|---|---|
| 0x004e7acc | 1 | 0/1, 1 | play full intro (boot) |
| 0x004e3b04 | 1 | 1..7, 1 | skirmish tech level |
| 0x004e3b08 | 2 | 2500..7000, 2500 | skirmish starting credits |
| 0x004e3b0c | 1 | 0..5, 1 | skirmish AI players (TODO verify) |
| 0x004e3b18 | 2 | 0..100 | **game speed** slider |
| 0x004eb030 | 4 | 4..40, 20 | **scroll speed**, in pixels per loop iteration (`FUN_004433a0`) |
| 0x004eb01c | 4 | 8 or 16 | colour depth |
| 0x004e5fc4 / 0x004e5fc8 | 4 | 0..100, 100 | `gSFXVolume` / `gCDVolume` (music); doc 15 |

`FUN_004433a0` clamps the camera to `x <= map_w*32 - DAT_00786d48` (480)
and `y <= map_h*32 - DAT_006dc538` (380), so width pairs with width
(corrected per docs/re/10-ingame-input.md). These two globals are the
**viewport size in pixels**, which matters for the "never show more map"
rule.

**Real-time cap.** WinMain calls `SetTimer(hwnd, 1, 1000)`. Each WM_TIMER
(window procedure, `0x4a37f6`) copies the tick counter `0x6b985c` to
`0x6b9858` and resets it to 0. The main loop runs a tick only while the
counter is below `tick_limit`, so the simulation never exceeds the bucket's
ticks **per second** (8, 10, 12, 15, 20, 25 or 100).

The flip flags `0x4eb008` / `0x4eb010` default to 1 (page flipping with
`DDFLIP_WAIT`), so the loop ran at the monitor refresh. At 60 Hz the pattern
gives 0.6 × N ticks per second. The cap is reached only at ≥ 100 Hz.

## Decision (user, 2026-10-03)

Float handling is a modern-hardware compatibility choice, on by default: exact
formulas, constants, operation order and `__ftol` truncation, but plain IEEE
float32 (SSE2, `/fp:strict`, no FMA contraction). No x87 precision emulation.
The loop rate becomes a fixed real-time constant (to be measured). See CLAUDE.md §10.

## The simulation uses floating point

`Data\bin\SPEED.BIN` is 96 × float32 movement multipliers. The tick scheduler
above is also x87 float code. For exact parity, `sim/` must reproduce the
float32 operations, their order, and the truncation (`__ftol`).

**The FPU control word (verified 2026-10-16).** A scan of every executable
byte for `FLDCW` / `FSTCW`, each candidate confirmed by Ghidra's
disassembly, finds only two writers:
- the CRT start-up `__fpmath` (0x4b0f3f) → `__setdefaultprecision`
  (0x4b5af0) → `_controlfp(_PC_53, _MCW_PC)` (0x4b8c80 / 0x4b8c40): the
  precision becomes **53 bits (double)**; the rounding mode is not touched
  and stays **round to nearest**;
- `__ftol` (0x4b1840): chop for its one store, then the old word back.

The game uses DirectDraw only (no Direct3D device, which could switch the
FPU to 24 bits), so the simulation runs with x87 at 53-bit precision and
round to nearest. Its float32 expressions are therefore computed in double
precision and rounded to float32 when stored. Our decision above (plain
float32, no x87 emulation) accepts the rare last-bit differences this
gives; `x87.hpp` reproduces the few places where they change a result.
