# InstallShield 3 archive (`SETUP\SETUP.Z`)

Used on the Dune 2000 CD to hold everything the installer copies to disk
(game exe, `Data\` tree). Neither 7-Zip nor the 16-bit installer works on
modern 64-bit Windows, so we read it ourselves: `tools/is3extract.py`.

Verified against the US CD (`SETUP.Z`, 28,109,143 bytes, 1165 files).
All integers little-endian.

## Header (offsets from file start)

| Off | Type | Meaning |
|----:|------|---------|
| 0   | u32  | signature `0x8C655D13` |
| 12  | u16  | total file count |
| 18  | u32  | archive size in bytes (equals file size) |
| 41  | u32  | offset of directory table |
| 49  | u16  | directory count |

The other header bytes are not needed for extraction (meaning unknown).
File data starts at offset 255 on this CD.

## Directory table

`dir_count` entries, starting at the directory-table offset:

| Off | Type | Meaning |
|----:|------|---------|
| 0 | u16 | number of files in this directory |
| 2 | u16 | entry size (step to the next entry) |
| 4 | u16 | name length |
| 6 | char[] | name, e.g. `DUNE\Data\GAMESFX` (padded) |

## File table

This follows straight after the directory table. Files are grouped by
directory, in directory order, with each directory's file count taken from
its entry.

| Off | Type | Meaning |
|----:|------|---------|
| 0  | u8  | ? (0) |
| 1  | u16 | ? (not a reliable dir index; we use the grouping instead) |
| 3  | u32 | uncompressed size |
| 7  | u32 | compressed size |
| 11 | u32 | data offset |
| 15 | u16 | DOS date |
| 17 | u16 | DOS time |
| 19 | u32 | attributes? (`0x80`) |
| 23 | u16 | entry size (step to the next entry) |
| 25 | u32 | ? |
| 29 | u8  | name length |
| 30 | char[] | file name |

## Compression

Each file is an independent PKWARE DCL "implode" stream: binary or ASCII
literals, a 4/5/6-bit dictionary, and an end-of-stream code with length 519.
Checks: every file decompresses to exactly its stated size, and the text
files read correctly.

## Contents

- `API\`: Westwood Online support (WOLAPI, register tool). We skip it.
- `DUNE\`: the install root. It holds `DUNE2000.EXE` (91 KB launcher stub),
  `DUNE2000.DAT` (the real game PE executable), and `Data\` with these
  subfolders: `GAMESFX`, `UI_DATA`, `UI_GFX`, `UI_GFX\Regions`, `bin`, `maps`.
