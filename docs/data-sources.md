# Data sources and target version

## Inputs (user-supplied, never committed)

| File | SHA-256 | Notes |
|------|---------|-------|
| `DUNE2000.iso` | `01c9cca4aa9400c44db922fb3e1181feabd40a6172184f0d43fbe5a001699d89` | US CD, volume `DUNE2000`, publisher WESTWOOD STUDIOS, files dated 1998-08-20 |
| `d2k106us.exe` | `ce07a0efe1118d46411771e3aaf06b4cc70f958dbb584cc2296b9f88524a5326` | Official 1.06 US patch, self-extracting archive (7-Zip opens it), readme dated 1998-12-17 |

## Building the 1.06 game folder

Players: the game's first-run setup screen does this itself from a CD
image, CD drive, CD folder or installed game plus the patch
(`docs/first-run-setup.md`). The script below is the developer route and
still produces our repo's `gamedata/1.06`:

```
python tools/import_game.py DUNE2000.iso d2k106us.exe gamedata/1.06
python tools/inventory.py gamedata/1.06 --csv docs/inventory-1.06.csv
```

No installer is run. See `docs/formats/installshield3.md`.

## Target executable (pinned)

The game exe is `DUNE2000.DAT`. `DUNE2000.EXE` is a small launcher.

| Version | File | Size | SHA-256 |
|---------|------|-----:|---------|
| **1.06 (target)** | `DUNE2000.DAT` | 999,936 | `51082bd7306b5e76b93fc694fc6455ce1704140918f9abe2d42394682b85e8f2` |
| 1.00 (CD) | `DUNE2000.DAT` | 975,872 | `70bc0c1c849d34bf3d6e702e0d48fb4e95e380e664bf96a5be0657571862de70` |
| launcher | `DUNE2000.EXE` | 91,136 | `81e7b1c7f811afbc70eb9986873f10a4d1b9c84d3477142d7a0c85e2b43d8eba` |

## What the 1.06 patch changes (from its README)

- It includes the 1.03 changes ("Adjusted multi-player unit balancing").
- 1.06 adds "Updated unit data (balancing tweaks)", 10 multiplayer maps,
  2 tilesets, and 3 units (Grenadier, Sardaukar, Stealth Raider) for
  multiplayer/practice only.
- It replaces `DUNE2000.DAT`, `DATA.R8`/`DATA.R16`, all `bin\*.BIN`, the UI
  `.UIL`/`.UIB` files, and adds `BLOXXMAS.R8`.

**Answered (2026-10-16): no question for us.** The target is 1.06
(CLAUDE.md section 10). The 1.06 exe reads its unit, building, weapon and
AI numbers from the patched `bin\*.BIN` tables and the patched
`DUNE2000.DAT` for every game type, campaign included (there is no
separate campaign rules set), and we load exactly those files. Whatever
the patch changed therefore applies to our campaign as it does to the
original 1.06 campaign. A 1.00 vs 1.06 diff would only be of historical
interest.
