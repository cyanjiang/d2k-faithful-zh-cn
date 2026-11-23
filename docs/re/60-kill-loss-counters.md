# 60. Kill and loss counters (per side, per record)

Written 2026-10-16. Sources: a scan of every instruction in the 1.06 exe
whose displacement falls in the counter block of the player struct
(`0x798870 + side·0x26990`, also as absolute addresses), and the
functions listed below. Related: doc 24 §5 (score), doc 56 §5 (skirmish
score), doc 19 (CASUALTIES), doc 30 §2.4–2.5 (capture), doc 40 §4.1
(deviated units), doc 17 §3.2 (worm).

## 1. Layout

The reset at mission start (`0x46bb81..0x46bc57`, in the player reset
that also clears the Starport and power fields) fixes the sizes:

| Offset | Size | Meaning |
|---|---|---|
| +0x24780 | i32 | units built |
| +0x24784 | i32 | buildings built |
| +0x24788 | i32[60] | units built per unit record |
| +0x24878 | i32[100] | buildings built per building record |
| +0x24a08 | i32 | **units lost** |
| +0x24a0c | i32 | **buildings lost** |
| +0x24a10 | i32[60] | **units lost per unit record** |
| +0x24b00 | i32[100] | buildings lost per building record (**never written**) |
| +0x24c90 | i32 | **units killed** |
| +0x24c94 | i32 | **buildings killed** |
| +0x24c98 | i32[60·8] | **units killed**, index `record·8 + side` |
| +0x25418 | i32[100·8] | **buildings killed**, index `record·8 + side` |

The next field, +0x26098, is the first primary-building slot (doc 12 §6).
The reset clears the two killed arrays as 8 interleaved passes (stride
0x20 per record), which confirms the `record·8 + side` index.

## 2. Writers (all verified)

`KillUnit` (`FUN_0049cde0`), `BuildingDestroyed` (`FUN_0049ca10`) and
`RemoveSide` (`FUN_0046e6b0`) count **nothing**; every count is made by
the caller. So a unit killed inside a destroyed carryall, the units and
buildings of a side removed by `RemoveSide` (defeat, surrender, a network
player leaving) and a deployed MCV are not counted anywhere.

| Where | Event | Counts |
|---|---|---|
| `FUN_0049d2e0` 0x49d647..0x49d6d7 | a building destroyed by damage | unless the bullet is SPICEEXPL (`0x6d610a`): building +0x50 / +0x52 = attacker side / firer (0xFFFF too), then attacker `+0x25418[rec·8 + owner]` += 1 and `+0x24c94` += 1. Always: owner `+0x24a0c` += 1. No owner/attacker test: a side's own fire counts as its kill. |
| `FUN_0049d2e0` 0x49d927..0x49d99e | a unit killed by damage (worms too, which then keep 1 hp) | unless SPICEEXPL: unit +0x30 / +0x32 = attacker / firer, attacker `+0x24c98[rec·8 + owner]` += 1 and `+0x24c90` += 1 (also for an attacker of the same side). |
| `FUN_0049d2e0` 0x49da35..0x49da91 | the same death, not a worm | `s` = original side +0x2D if flag 0x1000 (deviated), else the owner; `s`: `+0x24a08` += 1, `+0x24a10[rec]` += 1. |
| `FUN_0049f8f0` (crush) | infantry crushed by a vehicle of side `c` | victim's side `+0x24a08`, `+0x24a10[rec]` += 1; crusher side `c`: `+0x24c98[rec·8 + c]` += 1 (**quirk:** the crusher's own side as the second index, not the victim's), `+0x24c90` += 1. |
| `FUN_00497b90` 0x49ab6c.. (worm eats) | a vehicle eaten | the cell owner (the victim's side): `+0x24a08`, `+0x24a10[rec]` += 1. No kill. |
| `FUN_00497b90` 0x49a218.. (engineer, capture done) | building captured | victim `+0x24a0c` += 1; captor `+0x25418[rec·8 + victim]`, `+0x24c94` += 1, and captor `+0x24784` += 1 (on top of CreateBuilding's). |
| `FUN_00497b90` 0x49a365.. (saboteur) | building blown up | victim `+0x24a0c`; saboteur side `+0x25418[rec·8 + victim]`, `+0x24c94`. |
| `FUN_00497b90` 0x49a45f.. (state 0x17 end) | the entrant (engineer / saboteur) | its side `+0x24a08`, `+0x24a10[rec]` += 1 (also when the building was gone). |
| `FUN_00497b90` 0x49b3f7.. (Devastator self-destruct) | detonation | its side `+0x24a08`, `+0x24a10[rec]` (doc 37). |
| `FUN_0046d6d0` 0x46d925.. (capture) | a docked harvester of the victim | victim `+0x24a08`, `+0x24a10[rec]`, then ConvertUnit. |
| `FUN_0044dbd0` 0x44dd18.. (CreateBuilding) | a unit of another side on the new footprint | that side `+0x24a08`, `+0x24a10[rec]`, then ConvertUnit (doc 12 §6 step 12). |

The deviator's conversion (doc 40) and its way back count nothing.

## 3. Readers

| Field | Read by |
|---|---|
| +0x24a08 / +0x24c90 | CASUALTIES condition (`0x453d45`, doc 19), SCORE_SINGLE (`0x43dce8` loop, doc 24), the skirmish score and the in-game score box (`0x425f02`, doc 56), the dead debug box (`0x425db0`) |
| +0x24a0c / +0x24c94 | SCORE_SINGLE, skirmish score, score box, debug box |
| +0x24780 / +0x24784 | the dead debug box and the Westwood Online upload (doc 56 §3) |
| the four per-record arrays | **nothing.** No instruction reads them (the scan finds only the increments above and the reset). They are kept in the saved game, because the save writes the player blocks raw (doc 27 §2.3). |

So the per-record counts change no game behaviour, score, AI decision or
screen in 1.06. We keep them (and save them) so the state matches the
original's player block.

## 4. Port notes

- Count in the callers, as the exe does, not in `kill_unit`; this fixes
  three differences of the first port: SPICEEXPL deaths no longer count
  a kill (or write the attacker), a carryall's cargo and the units of a
  removed side no longer count as lost / killed, and buildings destroyed
  by a hit with no firer object (crate, SARDDEATH, debris) now count as a
  kill for the attacking side.
- `+0x24b00` stays zero; we do not keep it.
- The built counters (`+0x24780..+0x24878`) are not ported (no reader
  besides the dead debug box and the Internet upload).
