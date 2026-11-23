# Enhanced movies (optional, personal use)

The movies of Dune 2000 are 640×400 VQA files at 15 frames per second.
The display option `enhanced_movies` lets the game show **upscaled
pictures** that you make yourself from your own movies, for example with
Topaz Video. It is off by default and is not part of the original game.

What changes and what does not:

- Only the **picture** is replaced. The sound, the timing, the length,
  skipping with Esc and what happens after the movie all still come from
  the original VQA, which plays alongside and drives the clock.
- A movie without an upscaled file plays as the original.
- The upscaled files stay on your PC. Never put them in this repository or
  share them: they are made from the original game's movies.

## Turning it on

`--enhanced-movies 1` once (saved), or `enhanced_movies=1` in
`settings.ini`. The game then looks for

```
%APPDATA%\d2k-faithful\d2k\movies-enhanced\<NAME>.mp4
```

where `<NAME>` is the VQA's name, e.g. `A_BR01_E.mp4` for
`MOVIES\A_BR01_E.VQA`. The log (`d2k.log`) says `enhanced movie WxH` when
one is used.

Format the game expects: **H.264 in .mp4**, 4:2:0, BT.709 limited range
(what `tools/enhanced-movies.ps1 encode` writes). Any size works; it is
scaled into the same place as the 640×400 picture, in either `aspect`
mode. Any frame rate works (each picture is shown at its own time stamp),
so frame-interpolated 30 / 60 fps versions keep the original length.

Windows only: the decoder is Windows Media Foundation's built-in H.264
decoder. On Linux the option is ignored and the original movies play.

## Making the files: `tools/enhanced-movies.ps1`

Three steps, each skipping movies already done (`-Only NAME` for one
movie):

1. **`export`**: every VQA → a lossless ProRes 4444 source
   (`d2k-tool vqa-rgb` piped into ffmpeg). 43 movies, about 8 GB, in
   `%LOCALAPPDATA%\d2k-faithful\movies-work\source`.
2. **`upscale`**: Topaz Video's own ffmpeg (`C:\Program Files\Topaz Labs
   LLC\Topaz Video\ffmpeg.exe`, models in `%ProgramData%\Topaz Labs
   LLC\Topaz Video\models`) upscales 4× to 2560×1600 with `tvai_up` and
   interpolates to 60 fps with `tvai_fi`, into a ProRes master; the master is
   then encoded to the .mp4 the game plays (libx264 CRF 14) and deleted
   (60 fps masters take about 5 GB a minute; `-KeepMaster` keeps them).
   Topaz must be installed and signed in. About 2 hours for all 43 movies
   on an RTX 5090.
3. **`encode`**: a kept master (or a `lanczos` test) → the .mp4.

Chosen 2026-10-23 by comparing captures (`-Model`, `-Fps`, `-FpsModel` to
change):

- **Model `iris-3` (Iris).** The VQA's 4×2 / 4×4 block pattern is the main
  flaw of the source; Iris removes it on faces and on the CGI. Proteus
  (`prob-4`), Artemis (`ahq-12`) and Gaia CG (`gcg-5`) keep the blocks and
  sharpen them; Rhea (`rhea-1`) is grainier. Starlight Mini (`slm-1`)
  crashed after 5 minutes on 86 frames.
- **60 fps with Apollo (`apo-8`)**, judged worth it on the Ordos ending
  (`O_FINL_E`). `-Fps 0` keeps the original 15 fps.

**Warning:** Topaz's ffmpeg prints the signed-in account's tokens on
stderr at every start, whatever `-loglevel` says. The script throws its
output away; never log it or paste it anywhere.

`lanczos` instead of `upscale` makes a plain 4× resize, to test the game
side without Topaz.

Needs ffmpeg on PATH (e.g. `winget install Gyan.FFmpeg`) and a Release
build of `d2k-tool`.
