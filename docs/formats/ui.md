# UI data (`Data\UI_DATA`)

Code: `src/formats/string_table.cpp`.

## String tables (`text.uib`)

u32 entry count, then that many (key, value) pairs. Each string is a u16
length (including the NUL), followed by the bytes and the NUL.

`text.uib` (1.06) has 1,322 entries but only 957 distinct keys. **The table
is an ordered list**, and repeated keys are deliberate: the credits are 348
consecutive `CreditEntry` rows, and several multiplayer labels share an empty
key. 5 keys have duplicates with different values (`ServerBannedYou`,
`NoNameEntered`, `DialModem`, `CreditEntry`, empty key).

**Lookup (verified, `FUN_0048dc10`):** a linear scan with `_strcmpi`, so
keys are **case-insensitive** and the **first** match wins. It returns the
entry index, and 0 when nothing matches; `FUN_0048dcc0` then gives entry
0's text, `NoText` → "" in 1.06. The scripts rely on both: `lo2mis2.uil`
(the O2 briefing) asks for `B_raider`, the table has `B_Raider`; and some
keys miss and show nothing, e.g. `MultiPlayer` (CGameSes.uil),
`EnterComPort` (NullModm.uil), `AMission4V2` … (loadmis.uil). Ours does the
same (`ui::Assets::lookup_text`).

It contains: UI text, mission objectives (`M1Intro1`, `H7V1Intro` …), credits,
and the movie table (`mve_*` → VQA base name, e.g. `mve_intro1` → `g_int1_e`).

## Menu scripts (`*.uil`, 302 files)

u32 line count, then lines in the same counted-string format. They are
tokenised text, one value per line, e.g.

```
GROUPBOX gb / 0 0 / 640 400 / Black / Black / NONE / END_GROUPBOX
MOVIE mve_intro1 / 0, 0 / 640, 400 / QuitAndJumpToMenu MVE_INTRO2 / END_MOVIE
```

All 302 parse exactly. The element grammar (GROUPBOX, MOVIE, buttons, …)
will be documented as we implement menus (M2).

## Menu registry (`menus.uib`)

u32 count (275 in 1.06), then per entry: counted menu name, counted script
name (stored as `xxx.uid`; the file on disk is `xxx.uil`), u32 a, u32 b.
It parses exactly to the end of the file. Examples: `MAINMENU` → `MainMenu`
(3,1), `HOUSESELECTION` → `HouseSel` (0,1), `MVE_INTRO1` → `Intro1` (0,3).
Values seen: a ∈ {0,2,3}, b ∈ {1,2,3}: the entering and leaving
transitions (`FUN_00477190`; FADE / TWEEN / SKIP, docs/re/04-menu-flow.md).
5 entries name scripts that are not on disk (e.g. `REPLAY_LAST_MISSION`).

## UIL grammar (observed)

Blocks: `BACKGROUND <img>` … `END_BACKGROUND`, `ANIMATION <name>` / pos /
size / `<anim> <rate>`, `BUTTON <name> [n]` / pos / size / text key /
4 border-fill colours / 2 font colours / up-gfx / down-gfx / actions…
`END_BUTTON`, `TEXTBOX[_R] <name>` / pos / size / 3 colours / font colour /
text key, `GROUPBOX`, `MOVIE`. Text after `//` is a comment. Actions include
`JumpToMenu X`, `QuitAndJumpToMenu X`, `BackMenu`, `BackMenuAndJumpTo X`,
`SetValueECTo/SetValueBFTo/SetValueS4To/SetValueU1To <global> <value>`
(the suffix is the value type), `Dune2000CdCheck`, `SetLanguageTo X`.

**Headerless blocks:** some files contain block bodies whose header line
is missing. The bytes really are absent: e.g. HouseSel's group box, and
MainMenu's French/German language buttons and a "ScorePreview" debug
button. These look like commented-out headers that the build tool
stripped. The loader (`FUN_004390e0`) skips every line that starts with no
known keyword, so these bodies are inert (docs/re/03-ui-scripts.md).

## Not yet decoded

- `campaign.uib`, `samples.uib`, `colours.uib`.
- `UI_ENG.R8/R16`, `UIBB.R8/R16`: see `docs/formats/r8.md`.
