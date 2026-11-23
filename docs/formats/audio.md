# Audio: AUD and SOUND.RS

Code: `src/formats/aud.cpp`, `sound_rs.cpp`, `ima_adpcm.cpp`.
Tools: `d2k-tool aud-wav <file.AUD> <out.wav>`, `d2k-tool rs-list <SOUND.RS>`.

## AUD (`MUSIC\*.AUD` ×17, `Data\GAMESFX\*.AUD` ×251)

All 268 files in 1.06 share one format: 22050 Hz, flags 2 (16-bit mono),
compression 99. Verified on every file.

Header (12 bytes, LE): u16 sample rate, u32 size of data after the header
(equals file size − 12), u32 uncompressed bytes, u8 flags (bit 0 stereo,
bit 1 16-bit), u8 compression (99 = IMA ADPCM).

The body is a series of chunks: u16 compressed bytes, u16 output bytes,
u32 `0x0000DEAF`, then the data. The data uses the same IMA ADPCM as VQA
`SND2` (low nibble first).

- Decoder state **carries over** from one chunk to the next. Evidence: with
  carry-over, the sample jump at chunk boundaries (mean 249) matches the
  jump inside chunks (253). Resetting at each chunk collapses the signal.
- In 173 files, the last chunk declares exactly **one sample more** than its
  data holds (`out = 4·in + 2`). The exe's chunk decoder (`FUN_004048c0`)
  decodes `out / 2` samples, so the extra sample comes from the low nibble
  of the byte after the chunk's data:
  - the music streamer (`FUN_0046fcc0`, `FUN_0046ff30`) reads each chunk
    into one reused buffer, so it is that byte of the previous chunk; `AudStream`
    reproduces this;
  - speech and other AUD sounds play from a 64 KB buffer (`FUN_0046f9a0`,
    allocated in `FUN_0046eee0`), so it is whatever an earlier, longer file
    left there. That depends on the play history; for this one sample
    (45 µs) `decode_aud` repeats the last sample.

## SOUND.RS (`Data\GAMESFX\SOUND.RS`)

A plain archive. It starts with a u32 directory size (not counting that
field), then entries of a NUL-terminated name, u32 offset, and u32 size.
The entries are packed back to back and cover the file exactly.

It holds 57 entries, all standard RIFF PCM WAV, mono: 29 are 16-bit
22050 Hz, 26 are 8-bit 22050 Hz, and 2 are 16-bit 16007 Hz (e.g.
`BUILD1.WAV`). 8-bit samples are unsigned. We widen them as `(v − 128) << 8`.

## Answered (2026-10-16)

- Mixing, volume curves, channel limits and overlap: docs/re/59 (5 effect
  channels plus speech, the channel pick, the exe's pan / volume, and the
  Miles DirectSound path read in the game's own `MSS32.DLL`, section 7).
- Resampling of the 16007 Hz effects: not done by the game. Miles gives
  each sample its own DirectSound secondary buffer created with
  CTRLFREQUENCY at the sample's own rate (docs/re/59 section 7), so
  DirectSound converts it to the 22050 Hz primary buffer. The exact
  converter was Windows' or the sound card's, never part of Dune 2000;
  ours lets SDL convert the same way.
