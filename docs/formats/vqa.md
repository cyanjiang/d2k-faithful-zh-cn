# VQA movies (HiColor variant)

Code: `src/formats/vqa.cpp`, `lcw.cpp`, `ima_adpcm.cpp`. Player: `src/video/movie_player.cpp`.
Tools: `d2k-tool vqa-info | vqa-frame | vqa-wav`.

## Verification

All 43 movies in 1.06 (`MOVIES\*.VQA`, 518 MB) decode with these results:
- frame count equals the header's frame count;
- every `SN2J` audio checkpoint matches our ADPCM decoder state exactly
  (30,864 checkpoints; predictor and step index);
- every frame's vector-pointer stream is consumed to the last byte;
- sample frames render correctly (title screen, intro, Arrakis globe).

## Container

Westwood IFF: `FORM` <size BE> `WVQA`, then chunks with a 4-char id and a
big-endian u32 size. Bodies are padded to an even length.

Top-level chunks, in file order: `VQHD`, `LINF`, `CINF`, `FINF`, then per
frame `SN2J`, `SND2`, `VQFR`. `VQFL` appears 172 times and carries a
codebook only.

### VQHD (42 bytes, little-endian)

| Off | Type | Field | Dune 2000 values |
|---:|---|---|---|
| 0 | u16 | version | 2 |
| 2 | u16 | flags | 0x1D (0x14 for the one silent movie) |
| 4 | u16 | frame count | |
| 6 | u16 | width | 640 |
| 8 | u16 | height | 400 |
| 10 | u8 | block width | 4 |
| 11 | u8 | block height | 4 (3 movies use 2) |
| 12 | u8 | frame rate | 15 |
| 13 | u8 | codebook parts | 0 |
| 14 | u16 | colours | **0 = HiColor**, no palette |
| 16 | u16 | max blocks | |
| 18 | u16 | x offset | 0 (the draw offset when the player's option flag 8 is set, 0x4acefc) |
| 20 | u16 | y offset | 0 |
| 22 | u16 | largest compressed codebook (CBFZ buffer size, 0x4ac498) | |
| 24 | u16 | audio rate | 22050 (0 = no audio) |
| 26 | u8 | channels | 1 |
| 27 | u8 | bits | 16 |
| 28 | 14 bytes | ? | |

`LINF`/`CINF`/`FINF` hold loop, codebook, and frame-offset tables. We don't
need them for linear playback.

### Frames

`VQFR` and `VQFL` contain sub-chunks:
- `CBFZ`: a full codebook, LCW-compressed. It replaces the current codebook
  immediately. Each entry is a block of RGB555 pixels
  (block width × block height × u16 LE). Ten movies have one codebook
  that decompresses to 1–2 bytes beyond a whole number of entries. No
  frame indexes that partial entry (our decoder throws on an index past the
  whole entries, and every frame decodes), so the bytes never show.
- `VPRZ` (LCW-compressed) / `VPTR` (raw): the vector-pointer stream.

### Vector-pointer stream

The stream is u16 LE commands, decoded one row of blocks at a time, left to
right. `op = code >> 13`, `arg = code & 0x1FFF`.

| op | meaning |
|---|---|
| 0 | skip `arg & 0xFF` blocks; they keep the previous frame's pixels |
| 1 | `index = arg & 0xFF`, `count = ((arg >> 8) & 0x1F) * 2 + 2`: repeat one block |
| 2 | same index/count formula with `+3`: first block `index`, then each next block's index is the next byte |
| 3 | one block, `index = arg` |
| 4 | as 3, but pixels equal to `0x8000` are not written (transparent) |
| 5 | `index = arg`, `count = next byte`: repeat |
| 6 | as 5, transparent like 4 |
| 7 | ignored (nothing drawn, no advance) |

Source: the HiColor 4×4 decoder of the VQA library in the exe,
`0x4aedd0` (ops at 0x4aee26: 0x4aef5d skip, 0x4aef2f run, 0x4aee58 list,
0x4aef6a block, 0x4af0f4 transparent block, 0x4aef4a counted run,
0x4aefe6 transparent counted run). The skip takes only the low byte; a row
of 4-wide blocks is 160, so every 1.06 skip fits (our decoder rejects a skip
past the row end, and all 43 movies decode). D2K uses no op 4, 6 or 7.

### Audio: SND2 and SN2J

`SND2` is Westwood IMA ADPCM, 4 bits per sample with the low nibble first,
and standard IMA step and index tables. Each chunk holds 735 bytes, which
is 1,470 samples, or exactly one frame at 22050 Hz / 15 fps. Decoder state
carries over from one chunk to the next.

`SN2J` (6 bytes) is the decoder state at the start of the next `SND2`:
u16 `step_index << 5`, then i32 predictor. The library copies them into its
decoder state (0x4ab054: the i32 to +0x7d4, the u16 to +0x7d8); the ADPCM
routine (0x4af6c9) reads that u16 as its table index, step index × 32 with
the nibble ORed into the low bits. Verified against our decoder on every
chunk of every movie.

## LCW ("format80")

The command stream:
- `0cccpppp pppppppp`: copy c+3 bytes from p bytes back.
- `10cccccc`: c literal bytes; c=0 ends the stream.
- `11cccccc` + u16 p: copy c+3 bytes from p.
- `0xFE` + u16 n + byte v: fill n bytes with v.
- `0xFF` + u16 n + u16 p: copy n bytes from p.

When the stream starts with byte 0, p in the last three forms is relative
(distance back from the current output position). Most HiColor codebooks
use this mode. Copies run byte by byte, so overlapping runs are intended.

## Presentation

- The screen is 640×400 (docs/re/04-menu-flow.md), the movie size, and the
  header offsets are 0: a movie fills the screen. The player clears its
  buffer to black first (`FUN_00403460`).
- **Only Esc skips.** The play loop (`FUN_00406d10`) checks the key-down
  array entry `0x7983ab` = `0x798390 + VK_ESCAPE`. The window procedure sets
  an entry on a new key press only, not on auto-repeat (0x4a33aa), and the
  movie element clears Esc before playing (`FUN_0048d8f0`). Mouse buttons
  and other keys do nothing.
- A finished or skipped movie then runs its element's commands
  (`FUN_0048d8f0` calls `FUN_00482850` after the player returns), e.g.
  `QuitAndJumpToMenu MVE_INTRO2`.
- The player does not touch the menu music: it already stopped when the
  `MVE_` menu became the top (docs/re/04).
- Boot sequence: docs/re/01-boot-sequence.md.
