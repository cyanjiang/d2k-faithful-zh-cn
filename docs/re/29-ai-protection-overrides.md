# 29: Computer player: DoProtection, DoOverrides, DoBuildCarryalls (1.06)

Three per-tick routines of DoAI (`FUN_0041b610`, doc 22 §2.2 / doc 23
§1.1) written for a line-by-line port. Offsets without a prefix are
relative to the side's AI block (`0x4f4118 + slot × 0x1DB8`, `ECX` of
every function here); `player+…` is the player struct returned by
`FUN_004a4660(side)`; `unit+…` / `bld+…` are object fields (objects of
the player's array at `player+0x30`, stride 0x94, lists linked by
`+0x7c`); group fields are `G(g)+off` = `block + g × 0x2C + off` with
doc 23 §3.1's offsets (0x2E target, 0x34 timer, 0x38 strength, 0x3C start
strength, 0x44 age, 0x48 job, 0x49 substate, 0x50 / 0x51 cell, 0x52 in
use).

"Verified" = read from the decompile **and** the disassembly
(`re/disasm.py`); every former guess was settled from the exe in the
closing pass of 2026-10-16 (evidence next to each point). `rand()` is the shared
MSVC LCG (`FUN_00448460` / `thunk_FUN_004b1810`, doc 23 §1); the
"line" numbers are the debug line arguments pushed with each call and
identify the call site.

Mission values quoted below come from the 1.06 `MISSIONS\_*.MIS` AI blocks
(file +0x58, 113 active AI slots including the three `_TEST*.MIS`).

## 1. DoProtection (`FUN_0041c070`, label "DoProtection AI")

Called every tick from DoAI, after DoAttackBuildings and before
DoSpecialWeapons. No own stagger: the per-unit test below staggers it.

### 1.1 Settings used

| Off | Name | 1.06 missions |
|---|---|---|
| +0x1CC8 | ProtectUnit u8[60], by unit record | {8 HARVESTER} in every active slot except `_A6V1` slot 5 (none) |
| +0xF3E | ProtectStrength u8 (percent) | 10…80 (A1: 15, A3 Ordos: 30) |
| +0xF43 | MaxProtectGroups u8 | 2 everywhere |
| +0x1D34 | ProtectTicks u32 | 4 everywhere (0 would divide by zero) |
| +0x1D1F | AirliftPercentage u8 | 50 (25 in a few) |
| +0x1D44 | AirliftAndAttack u8 | 0, except the three `_TEST*.MIS` (1) |
| +0x1D3C | the **Base** group index (job 1, where new units go) | set at activation |
| +0x1D20 / +0xF4C | UseIdealStrength / MaxBaseStrength (GetPercentage) | 0 everywhere |
| +0xF6F | GroupSizeRandomPercentage (CreateSplinterGroup) | 20 everywhere |

Note on the group indices (verified at `0x41b427` / `0x41b49f`):
ActivateAI stores the job-2 (Guarding) group in +0x1D3D and the job-1
(Base) group in +0x1D3C. Doc 23 names them correctly; doc 22 §1.3 / §2.1
swaps the names.

### 1.2 Pseudo-code (verified)

```
DoProtection(ai):
    P = FUN_004a4660(ai[+0])                       // player struct
    for u in P.units (list +8, next +0x7c):
        T = ai.u32[+0x1D34]                        // ProtectTicks
        if (u.u16[+0x8c] % T) != (tick % T): continue      // unsigned; tick = 0x5173f4
        if ai.u8[+0x1CC8 + u.u8[+0x18]] == 0: continue     // record not protected
        if NumberOfGroupsDoingTask(3) >= ai.u8[+0xF43]:    // u8 compare
            return                                  // leaves the whole function
        if u.u8[+0x2A] >= T: continue               // not hit in the last T "×8 ticks"
        if u.flags[+0x08] & 8: continue             // already handled
        if u.u32[+0x10] == 0x12: continue           // waiting for a carryall
        u.flags |= 8
        r = rand()                                  // line 0x40B, always drawn here
        lifted = false
        if (r % 100) <= ai.u8[+0x1D1F]:             // unsigned; <= (so 0 % still lifts 1 %)
            slot = FindCarryallSlot(P, u)           // FUN_0046cca0, §1.4
            if slot != 0xFF:
                u.u8[+0x48] = W / 2                 // W = i32 0x517de8, signed / 2
                u.u8[+0x49] = H / 2                 // H = i32 0x517dec
                GetBestBasePosition(ai, &u[+0x48], &u[+0x49])   // §1.5, overwrites both
                if AddToQueue(P, u, u.u16[+0x8c], slot, 1, u.u32[+0x10]) != 0:   // §1.4
                    lifted = true
        if lifted and ai.u8[+0x1D44] == 0: continue // AirliftAndAttack off
        // escort (always when not lifted; also when lifted with AirliftAndAttack)
        from = ai.u8[+0x1D3C]                       // Base group
        s    = GetPercentage(from, ai.u8[+0xF3E])   // FUN_0041e5d0, §1.6
        g    = FindFirstFreeGroup()                 // FUN_0041c3a0, §1.6
        if g == 0: continue
        CreateSplinterGroup(from, g, s)             // FUN_0041ca60, rand() line 0x645, §1.6
        G(g).u8[+0x48]  = 0x0E                      // Rampaging
        G(g).i32[+0x34] = 10000                     // FUN_00423820 returns 10000
        G(g).u8[+0x49]  = 4                         // substate 4 = "find a target"
        G(g).u16[+0x2E] = 0xFFFF                    // no target
        G(g).u8[+0x50]  = (u.i32[+0x34] / 32) / 65536   // C signed division, twice, low byte
        G(g).u8[+0x51]  = (u.i32[+0x38] / 32) / 65536
```

`(x / 32) / 65536` with truncating signed division equals `trunc(x /
2^21)`: the cell of the unit's 16.16 pixel position (doc 09).

Order of evaluation per unit (verified from the disassembly): stagger,
ProtectUnit, group count (may return), +0x2A, flag 8, state 0x12, set
flag 8, rand, airlift try, then GetPercentage, FindFirstFreeGroup,
CreateSplinterGroup, group fields. **rand() order:** one draw (line
0x40B) per unit that passes the five tests, then one more (line 0x645)
only if a free group was found. Several units can pass in one tick (all
protected units with the same index residue).

The stagger index `unit+0x8c` is the unit's **slot in its side's object
pool** (doc 50 §1: 1000 slots per side, shared with buildings, bullets
and effects, handed out from a free list), not a global creation
counter. Our `Unit::index` is a global sequential id, so the tick on
which a protected unit is checked (and with it the order of the rand()
draws) can differ from the exe's. Matching it needs the per-side pool
slot numbering of doc 50; the same index staggers the unit update
(flag 8 clear above), GetNextSquare and the shroud reveal (doc 16).

### 1.3 What it means in practice (verified unless marked)

- **The MaxProtectGroups limit never bites in retail.**
  NumberOfGroupsDoingTask(3) (`FUN_004206c0`: count of the 32 groups with
  `G+0x52 ≠ 0` and `G+0x48 == 3`) counts *Protect* groups, but DoProtection
  creates **Rampaging** (0xE) groups, and no code in 1.06 writes job 3:
  an operand scan finds no `mov [..+0x48], 3` and no register store to a
  group's +0x48 in the AI code (0x41a000–0x424000); the only copies are
  the MonitorGroups split (copies an existing record) and the `.MIS` load,
  whose groups ActivateAI clears. So the count is 0 and the test only
  stops DoProtection when MaxProtectGroups = 0 (no 1.06 mission). Port it
  as written anyway. Each hit on a protected unit can therefore create
  another escort group (bounded by flag 8 and the 31 groups).
- **Flag 8 (unit +0x08 bit 3) = "protection already requested".** Nothing
  in DoProtection clears it. The unit update clears it at `0x498034`:
  on the unit's 8-tick slot (`tick & 7 == param_3 & 7` in the unit
  update `FUN_00497b90`, the same slot as the +0x2A counter of doc 18;
  param_3 is the unit's own index: the only caller, the unit loop
  `FUN_00458e90`, pushes `u16 [unit+0x8c]` at `0x458f07..0x458f12`, and
  `0x497f50..0x497f65` masks it with 0xFFFF and 7), when the unit was not hit this tick (flag 1
  clear) and is not carried (flag 0x100 clear), +0x2A += 1 (max 255) and,
  if the new value is > 100, flag 8 is cleared. So a protected unit
  triggers again only after about 101 × 8 = 808 ticks without being hit.
- **Trigger:** +0x2A < ProtectTicks, i.e. hit within the last ~4 × 8
  ticks (+0x2A is reset to 0 by a hit, doc 18), checked on the unit's
  ProtectTicks slot.
- **Escort group:** a Rampaging group made from ProtectStrength % of the
  Base group's **stored** strength (`G(base)+0x38`, last recomputed by
  MonitorGroups / GroupNewUnits), ± GroupSizeRandomPercentage with the
  unsigned quirk of doc 23 §4.1, placed at the protected unit's cell with
  substate 4. It gets **no order here**: MonitorGroups (doc 23 §4.6, job
  0xE, substate 4) runs GetNearestHostileThing from that cell on its
  group slot (`(tick + off) & 0x1F`, so within 32 ticks) and attacks the
  nearest hostile unit (order 0x14) or building (0x15); the 10000 timer
  makes it practically permanent; when it reaches 0 the group returns to
  base. With an empty Base group the new group has strength 0, stays in
  use and is freed by the next MonitorGroups pass of that group.
- Fields not written by DoProtection or CreateSplinterGroup keep the free
  group's old values: target side (`G+0x2C`, the byte at the next record),
  flags `G+0x4C`, idle `G+0x40`, mode `G+0x53`, defend index `G+0x54`.
  The strength sum in CreateSplinterGroup uses the **old** job byte of
  group g (DoProtection writes 0xE afterwards): `GetUnitStrength(unit,
  G(g)+0x48)` reads HPWeight / SpeedWeight of that stale job. In 1.06 all
  weights are 1.0: SpeedWeight (+0xF70), HPWeight (+0xFBC) and
  FirePowerWeight (+0x1008), all 19 entries, in all 8 AI blocks of every
  1.06 mission file (the 45 `MISSIONS\_*.MIS` and `Data\maps\_PRAC.MIS`,
  file offset 0x58 + slot × 0x1DB8; checked 2026-10-16). So the stale job
  makes no difference with 1.06 data. Port it in this order.

### 1.4 The airlift request (carryall ferry table, doc 13 §6)

**FindCarryallSlot** (`FUN_0046cca0`, thiscall ECX = player, arg unit;
verified):
```
if u.u32[+0x10] == 0x12: return 0       // low byte 0 (not reachable from DoProtection)
if P.u8[+0x245c0] == 0: return 0xFF     // no carryall owned
for i in 0 .. P.u8[+0x2688c] − 1:       // slot count
    if P.u8[+0x26611 + i × 0x14] == 0: return i     // free slot
return 0xFF
```

**AddToQueue** (`FUN_0046cd00(P, unit, id, slot, kind, saved_state)`,
ECX = player; DoProtection passes kind = 1, saved_state = `u+0x10`;
verified). Slot `s` = `P + 0x2660c + s × 0x14`:
```
(debug only: error if saved_state == 0x12 or slot > 31)
for i in 0 .. count − 1:
    e = slot i
    if e.u16[+6] == id and e.u8[+5] != 0:
        if e.u8[+5] != 1: return 0       // already taken / done / picked up: fail
        e.u8[+5] = 0                     // drop the older waiting request
e = slot `slot`
e.u8[+5] = 1                             // waiting
e.u32[+0x10] = saved_state
e.u32[+0x0C] = 0                         // age
e.u8[+0] = u.u8[+0x4a]; e.u8[+1] = u.u8[+0x4b]   // from = current cell
e.u8[+2] = u.u8[+0x48]; e.u8[+3] = u.u8[+0x49]   // to = the base position just written
e.u16[+6] = id
e.u8[+4] = kind (1)
return 1
```

**DoProtection does not put the unit in state 0x12.** The harvester's own
ferry request (doc 13 §1.1) does (`SetState 0x12` after AddToQueue). The
request monitor `FUN_0046d180` (called from `FUN_00448550` at `0x449b15`
for player `tick & 7`, so every 8 ticks per player) marks a live request
done (+5 = 3, freed on the next call) when its unit is not in state
0x11, 0x12 or 0x13. So a DoProtection request is normally dropped within
8 ticks; it survives only if a carryall takes it first (`FUN_0046ce00`
takes slots in state 1). Settled from the exe (doc 33 §0 and §5): in the
tick `FUN_00448550` runs DoAI for every slot (`0x449a7a`) **before** the
unit updates (`FUN_0044aca0` at `0x449ad8`), whose idle-carryall branch
(`FUN_00497b90` → `FUN_0046ce00` at `0x4982cf`) takes waiting requests,
and RequestMonitor (`0x449b15`, side `tick & 7`) runs **after** them. A
carryall may therefore take the request and fly off, but it never lifts
the unit: the pick-up needs the unit in state 0x12 (doc 33 §2.5), and
RequestMonitor marks the request done (+5 = 3) on its next pass because
the unit is not in state 0x11 / 0x12 / 0x13.

The lasting side effect is on the unit: its **destination cell +0x48 /
+0x49 is overwritten with the base position** (even when AddToQueue
fails). A harvester in state 0xb (driving to spice) or 0xd then drives
to that cell; its own carryall check (doc 13 §1.1, distance² > 64) may
then queue a real ferry request to the base and enter state 0x12. In
state 0xd the overwrite is lost at once (0xd sets the destination to the
dock cell again every update). The visible result is doc 33 §0 (b): a
harvester driving to spice turns for the base, and only its own far
check can bring a carryall that lifts it.

### 1.5 GetBestBasePosition (`FUN_0041e180`, verified)

```
GetBestBasePosition(ai, &x, &y):
    P = FUN_004a4660(ai[+0])
    BaseCentre(P, &x, &y)                  // FUN_0046e3f0 (ECX = P), doc 21 §6
    if x == 0 and y == 0:
        if ai.i32[+0x24] != -1: x = ai.u8[+0x24]; y = ai.u8[+0x28]
        else: ai.u32[+0x24] = 0; ai.u32[+0x28] = 0        // remembers (0, 0)
    else if ai.i32[+0x24] == -1:
        ai.u32[+0x24] = x; ai.u32[+0x28] = y              // first non-zero result is remembered
    if x >= W − 1: x = W − 1               // signed compare, u8 store
    if y >= H − 1: y = H − 1
    return 1
```

BaseCentre (`FUN_0046e3f0`, already ported as `World::retreat_destination`):
with buildings, the average cell of the buildings within distance² < 100
of the primary construction yard (`P+0x26098`, else the first building of
the list); without buildings, the cell of the first unit without flag
0x40, else the middle of the base box (`P+0x262ea..eb`, `+0x262e8..e9`);
clamped to W − 1 / H − 1.

### 1.6 Group helpers (verified; also in doc 23)

- **NumberOfGroupsDoingTask(job)** (`FUN_004206c0`): groups 0..31 with
  `G+0x52 ≠ 0 and G+0x48 == job`; returns a u8 count.
- **GetPercentage(g, p)** (`FUN_0041e5d0`): if UseIdealStrength (+0x1D20):
  `(u32)(MaxBaseStrength × p) / 100` (unsigned `mul`); else
  `(i32)(G(g).i32[+0x38] × p) / 100` (signed `imul`, truncation toward 0).
- **FindFirstFreeGroup** (`FUN_0041c3a0`): the first g in 1..31 with
  `G(g)+0x52 == 0`; 0 if none (group 0 is never handed out).
- **CreateSplinterGroup(from, to, strength)** (`FUN_0041ca60`):
  ```
  q = ai.u8[+0xF6F]                                   // GroupSizeRandomPercentage
  r = rand()                                          // line 0x645
  off = (u32)(q − r % (2q))                           // unsigned div; q = 0 divides by zero
  limit = ftol((((u64)off) × 0.01f − (−1.0f)) × strength)   // x87: FILD qword (high 0), FMUL f32, FSUB f32, FIMUL i32; low dword
  sum = 0
  for u in P.units while u != 0 and sum < limit (unsigned):
      if u.u8[+0x28] == from and u.u32[+0x10] != 8
         and unitrec[u.u8[+0x18]].u8[+0x2D] not in {1, 2}:   // not harvester / carryall (0x77e27d)
          u.u8[+0x28] = to
          sum += GetUnitStrength(u, G(to).u8[+0x48])        // FUN_0041cb70, stale job (§1.3)
  G(to).i32[+0x38] = G(to).i32[+0x3C] = sum
  G(to).u32[+0x44] = 0; G(to).u8[+0x52] = 1; G(to).u8[+0x49] = 0
  return sum != 0
  ```
  The ±q % size and the off < 0 quirk (a huge limit, so the whole Base
  group moves) are doc 23 §4.1. Constants: `0x4be3bc` = 0.01f,
  `0x4be3d0` = −1.0f (doc 23).

## 2. DoOverrides (`FUN_00422c20`, label "DoOverrides AI")

Called every tick from DoAI, after DoBuildBuildings and before
DoAttackApproachers. **No rand() calls.** It does **not** touch the
emergency flag +0x1D1D or EmergencyBuildRate +0xF28 (see §2.3).

### 2.1 Pseudo-code (verified from the disassembly)

```
DoOverrides(ai):
    if ai.u8[+0x07] != 0: return                  // DontSell: the WHOLE routine is off
    if (u8)(tick & 7) != ai.u8[+0]: return        // once per 8 ticks, slot = side
    ai.u8[+0x08] = 0; ai.u8[+0x09] = 0
    P = FUN_004a4660(ai[+0])
    if P.u32[+0x24254] == P.u32[+0x24258]:        // spice == capacity
        ai.u8[+0x09] = 1; ai.u8[+0x08] = 1        // storage full
    if P.u32[+0x260b4] == 0: return               // empty build list (no construction yard)
    refineries = P.u8[+0x245c1 + byte 0x6d60df]   // building group 7 = REFINERY
    harvesters = P.u8[+0x245b8]
    if P.spice(+0x24254) + P.cash(+0x2425c) == 0: // 32-bit sum
        ai.u8[+0x06] = 1
        if refineries != 0: return
        if harvesters == 0: return
        // else (no refinery but a harvester): fall through
    ai.u8[+0x06] = 0
    if P.u32[+0x245a8] >= P.u32[+0x245ac]: return // power produced >= used (unsigned)

    // ---- power shortage ----
    any = false
    for b in P.buildings (list +0x10, next +0x7c):
        R = bldrec[b.u8[+0x34]]                   // 0x6dc540 + rec × 0x10C
        if R.i32[+0x3C] < 0 and b.i32[+0x24] < R.i32[+0x00]:   // power producer, damaged (signed)
            b.flags[+0x28] |= 1                   // repair (no cash test here)
            any = true
    ai.u8[+0x09] = 1
    if any: goto done
    if P.spice + P.cash >= 400: goto done         // unsigned
    // ---- choose ONE building to sell ----
    pick = 0
    (a) for b in list: if bldrec[b.rec].u8[+0x4E] != 0: pick = b; break
    (b) if !pick: for b in list:
            if (float)b.i32[+0x24] < (float)(u32)R.i32[+0x00] × 0.1f   // see §2.2
               and b.u8[+0x41] < 10:
                pick = b; break
    (c) if !pick: best = 0
            for b in list: v = bldrec[b.rec].u8[+0x5D]
                           if v > best: best = v; pick = b          // strict: first of the max wins
        if !pick: goto done
    if (pick.flags[+0x28] & 0x400000E) == 0:      // not building up / selling / removed / AI-sold
        FUN_00456ee0(ai[+0], pick.u16[+0x8c])     // sell handler, called directly (doc 21 §3.2)
        pick.flags[+0x28] |= 0x4000000
done:
    ai.u8[+0x09] = 1
```

### 2.2 Details

- **Power producers** = records with negative power (+0x3C): the
  construction yards (−20) and wind traps (−200). A damaged producer is
  flagged for repair on every pass (OR, not the order 0x22 toggle); while
  any producer is below full hit points, nothing is sold.
- **Sell rules** (record values from `Templates.bin`, verified):
  - (a) rec +0x4E ≠ 0 = "needs power to fire": only the **large gun
    turrets** (records 29–31). The first one in the building list is the
    pick.
  - (b) the first building below 10 % hit points that was hit recently
    (`bld+0x41` < 10; +0x41 is the building's ticks/8-since-hit counter,
    doc 23 §5.4). x87: `FILD i32 hp`, `FILD qword (max as u32)`, `FMUL
    f32 0.1f` (`0x4be3b8` = 0x3DCCCCCD), `FCOMPP`; the building qualifies
    when `max × 0.1f > hp` (neither less nor equal; disassembly
    `0x422daf..0x422dd3`, `test ah, 0x41`). **Precision (settled):** the
    CRT start-up `__fpmath` (`0x4b0f30`, call at `0x4b0f3f`) calls
    `__setdefaultprecision` (`0x4b5af0`) = `_controlfp(0x10000 _PC_53,
    0x30000 _MCW_PC)`; the exe has no Direct3D, and its only other FLDCW
    are `__ftol`'s own save / restore (`0x4b1856`, `0x4b185c`). So the
    x87 runs at 53 bits. The FILD loads are exact and the product of
    max (< 2^24) and the 24-bit mantissa of 0.1f needs at most 48 bits,
    so the exe compares the **exact** product: max × 0.1f =
    max × 0.100000001490116… is slightly above max / 10, and **hp =
    max / 10 qualifies** whenever max is divisible by 10. A float32
    product rounds back to exactly max / 10 (the excess, about
    1.5·10⁻⁸ relative, is below half an ulp) and would not qualify. Our
    port therefore computes this compare in double, which is exact and
    gives the exe's result on every machine.
  - (c) the largest rec +0x5D: OUTPOST 13, LARGE GUN TURRET 12, RESEARCH
    CENTRE 11, REPAIR PAD 10, HIGH TECH 9, STAR PORT 8, SILO 7, REFINERY
    6, PALACE 5, HEAVY FACTORY 4, LIGHT FACTORY 3, BARRACKS / SIETCH 2,
    WALL / MEDIUM GUN TURRET 1, CONSTRUCTION YARD / WIND TRAP / CONCRETE /
    SP_OUTPOST 0 (never sold by (c)). So +0x5D is a sell-priority.
  - The pick is final: if it is already being built up, sold or removed
    (flags 2 / 4 / 8) or already AI-sold (0x4000000), **nothing** is sold
    this pass; the next candidate is not tried. In particular a large gun
    turret that cannot be sold (e.g. building up) blocks rules (b) / (c).
  - One building at most per pass (every 8 ticks). The sell starts at
    once (handler, not the order queue); doc 21 §3.3 counts it down.
- **+0x06** ("broke"): written only here (and at activation); no reader
  in the exe at all: an operand scan of the whole code section finds no
  absolute access to `0x4f411e + k × 0x1DB8`, and the register-relative
  `[reg+6]` byte accesses outside the AI code (`0x45c1b7`, `0x45c28c`,
  `0x45c360`, `0x45d505`, `0x491705`) are network-message and particle
  records, not AI blocks; inside the AI code the only accesses are the
  three writes `0x41b20a` (ActivateAI), `0x422ccf`, `0x422ce1`. So +0x06
  is write-only (it is still saved with the block). Note the quirk: when broke and no refinery but some
  harvester, +0x06 is set and immediately cleared again.
- **+0x08** (storage full) is read by ShouldIBuildUnit (`FUN_00420350`):
  a unit may be bought without the CashStash[BuildUnits] reserve when
  +0x08 or +0x1D1D is set. **+0x09** ("may spend on buildings") is read
  by GetBestBuildingToBuild (`0x420dfc`) and cleared by RebuildBuildings
  (`0x41ec56`) when cash + spice > CashStash[BuildBuildings]. After a
  DoOverrides pass: +0x09 = 1 if storage is full or power is short
  (whatever the selling outcome), else 0.
- **DontSell = 1** (only `_A5V1` slot 1 in 1.06) also disables the
  storage-full and power logic: +0x08 / +0x09 then keep their file values
  (RebuildBuildings can still clear +0x09).
- The building flag 0x4000000 is the AI "already sold" marker, shared
  with MonitorBuildings and BESERK (doc 22 §3.4, §3.9).

### 2.3 Emergency flag and EmergencyBuildRate (for reference, not DoOverrides)

- +0x1D1D is written only by ActivateAI (`0x41b236`, 0) and MonitorGroups
  (`0x41dd1c` / `0x41dd25`): in the defend-area code, for the group whose
  defend index (`G+0x54`) is the base's area (+0x1D52), it becomes 1 when
  the area scan found a hostile unit (local flag set at `0x41dfc8`) and 0
  otherwise. The scan is the DefendArea pass of doc 53 §5.3 (checked
  again in `FUN_0041d070`'s decompile): the area grown by 4 cells,
  4 passes, cells whose owner bits are an enemy (`0x798830` = 1) with
  unit bits, holding a unit without flag 4; the patrol branch leaves the
  flag at 0. Only DefendOn (+0x1D) sides with the base area's group
  (`G+0x54 == +0x1D52`) write it.
- It is read by ShouldIBuildNow (`0x41beaf`: rate = EmergencyBuildRate
  +0xF28 instead of UnitBuildRate) and ShouldIBuildUnit (`0x4203b1`, the
  reserve bypass). EmergencyBuildRate is 1 in every 1.06 mission except
  the `_TESTH` / `_TESTO` slots (100): during an emergency the AI tries to
  order a unit every tick.

## 3. DoBuildCarryalls (`FUN_004203d0`, verified)

Every tick, no stagger, no rand():
```
P = FUN_004a4660(ai[+0])
if P.cash + P.spice < ai.u32[+0x1064]: return 0          // CashStash[BuildCarryalls], unsigned
if (i32)P.u8[+0x245c0] >= (i32)(P.u8[+0x245c1 + byte 0x6d60df] × ai.u8[+0x1D05]): return 0
                                                          // carryalls >= refineries × CarryallsPerRefinery
rec = MyVersionOfUnit(P, byte 0x6d60d8, 0)               // FUN_0046c570: the house's CARRYALL record
if !CanBuildUnit(ai, rec): return 0                       // FUN_0041b850 (doc 22 §3.2)
if !ShouldIBuildCarryall(ai): return 0                    // FUN_00420320: P.u8[+0x245b8] != 0 (owns a harvester)
FUN_0045a0b0(ai[+0], MyVersionOfUnit(P, byte 0x6d60d8, 0))   // order 0x27 start unit
return 1
```
It does nothing when: CarryallsPerRefinery (+0x1D05) = 0, no refinery,
enough carryalls, cash + spice below CashStash[4] (1800 in every 1.06
mission), no harvester, or CanBuildUnit fails (`FUN_004430d0(side, rec,
1)` refuses, e.g. another item blocks the factory; cost above cash +
spice; carryall not in the unit build list, i.e. no High Tech factory /
tech). 1.06 values: CarryallsPerRefinery 1, except `_A8V1` slots 2 and 4
(2). No UnitBuildRate pacing: while the conditions hold the order is
posted every tick; CanBuildUnit's `FUN_004430d0` check is what stops
duplicates once production started. Before that it **is** posted again:
an order runs 2 ticks after it is posted (doc 49), so DoAI of the next
two ticks finds the factory still free and posts up to 3 orders 0x27 for
the carryall. The duplicates do nothing: the handler (`FUN_00455510`,
ProcessOrder case 0x14) first looks for the record in the 10 production
slots (`P+0x26530`) and, when found, returns unless that slot is on hold
(+0x10 set: the money-out pause, which it then resumes). Our
`World::start_unit` has the same test.

MyVersionOfUnit (`FUN_0046c570(P, name, exclude_buildable)`): over the
unit records (count `0x795538`), the last record with `rec+1 == name`
whose house mask (`rec+0`) has the bit of `P+0x24253`, else the first
record with that name; with the third argument set, 0xFF if a record of
that name is already in the build list. Called with 0 here.

## 4. Porting checklist

1. DoProtection after DoAttackBuildings, every tick; keep the `return`
   on the group count, the evaluation order and the two rand() sites.
2. Unit flag 8 and its clear in the unit update (+0x2A > 100, `0x498034`)
   if not already ported.
3. Airlift: FindCarryallSlot / AddToQueue on the existing ferry table
   (doc 13 §6); write the unit's +0x48 / +0x49 before AddToQueue; no
   state change.
4. Escort: GetPercentage, FindFirstFreeGroup, CreateSplinterGroup (stale
   job for the strength), then job 0xE, timer 10000, substate 4, target
   0xFFFF, cell; no order.
5. DoOverrides after DoBuildBuildings; per 8 ticks on the side's slot;
   DontSell gates everything; sell with the handler directly; no rand().
6. DoBuildCarryalls every tick as §3.
