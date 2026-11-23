# Campaign map ("GLOBE" menu) (1.06)

Code: `src/ui/globe.cpp`. Data: `docs/formats/campaign.md`.

## Flow

The first-campaign path runs as follows:
1. `HOUSESELECTION`
2. `MVE_<HOUSE>` (house movie)
3. `PLAY_<HOUSE>` ("Do you want to be …?")
4. `MVE_UNFOLD`
5. `GLOBE`
6. A click on a territory calls `FUN_004752c0`. It sets
   `gMissionNumber = atoi(name + 1)` and `gSideId` from the name's letter,
   sets up the `.map` / `.mis` names, then runs `JumpToMenu MVE_<HOUSE><n>`
   (the briefing movie).
7. `BackMenuAndJumpTo PRECAP`. PRECAP has no script; the game starts here
   (M3).

Other details:
- The menu hook removes GLOBE's `prev` / `next` buttons unless the debug
  flag `0x7984d0` is set, which never happens in 1.06. Those buttons are
  `PreviousMission` / `NextMission`, `FUN_0042d0e0` / `FUN_0042d110`.
- When `gUseCampaignSelection` is 0, LOADMISSION is used instead of GLOBE.

## CreateGlobe (`FUN_0042bc60`, called when GLOBE opens)

**Ownership.** Every territory starts as "nobody" (3). The records for the
player's house are applied in order:
- Records `0 .. mission-2` are applied directly.
- Record `mission-1` is applied with each changed territory marked
  *pending*.
- Applying a record does two things:
  - each house takes the territories in its "gained" list;
  - **both** of the record's selectable territories go to the player's
    house.

**Images.** An owned territory uses `UI_gfx\Regions\<a|h|o><n>`, which
exists only as an 8-bit BMP. `dunemap` (the map with borders) is loaded, and
every owned territory that is not pending is drawn onto it with flags (1,1).

**Current record.** The record for `mission` supplies the reveal order
(houses and speech) and the two choices.

## Update (`FUN_0042c400` → `FUN_0042c9b0`)

**When it runs.** It is called from the menu draw (`FUN_0043c4b0`) after the
GLOBE's elements, while GLOBE is the top menu and no transition runs. It
works on two images:
- the menu's background image (`+0x54`, from `DuneMap2`), which it edits
  in place;
- the `dunemap` copy.

**State.**
- The counter `c` starts at 0.
- The threshold is 32 (`0x4de608`) and the step is 4 (`0x4de60c`).
- Phases:
  - *initial*: the whole map fades in;
  - *revealing*: one territory at a time;
  - *done*.

**Each update:**
1. If `c > 32`, reset `c` to 0, then:
   - *Initial:* switch to *revealing* and pick the next pending territory.
     If there is one, copy dunemap over the background, unmark it, draw it
     onto dunemap, and set the dirty rect to its image rect.
   - *Revealing:* draw the previously revealed territory onto the
     background, then pick the next one as above. If none is left, switch
     to *done*.
2. Cross-fade `screen = (background·(32−c) + dunemap·c) >> 5` (per 5-6-5 or
   5-5-5 field). In *initial* it covers the whole 640×400 and `c += 4`. In
   *revealing* it covers the dirty rect and `c += 2`.

**Timing.** That is 9 updates for the map, then 17 per territory. The speed
depends on the loop rate, since there is no timer: in the original the
monitor refresh (DDFLIP_WAIT); ours runs one update per loop at the fixed
60 Hz (decided 2026-10-03, docs/re/02-timing-and-arithmetic.md).

**Pick order** (`FUN_0042c6b0`).
- Houses go in the record's reveal order. Within a house, the lowest
  territory index comes first.
- When a house's first territory is picked, the exe **busy-waits** until
  the previous speech sample finishes. It then plays the house's speech
  (`samples.uib` → `GAMESFX\<file>.AUD`) unless that is `NONE`.
- When nothing is left it waits for the speech once more.

**Verified in the reference game (2026-10-03):**
- the reveal pauses until each narrator line ends;
- the button panel disappears during the reveal and comes back at the end;
- button clicks act on release.

**End of the reveal.**
- **The whole GLOBE menu is redrawn** (`FUN_0043ca50`), so its background,
  now the finished map, and the button panel come back. The full-screen
  fade painted over the panel while the reveal ran.
- Two looping animations are created:
  - names `region1anim` / `region2anim`;
  - 72×70 at the record's arrow position, or at the territory centre
    − (16,32) when that is (0,0);
  - gfx `ATpoint` / `HKpoint` / `ORpoint`, 14 fps.

**Mouse.** Input is switched off while the reveal runs, so clicks are ignored until
10 loops after it ends (see the input switch in `04-menu-flow.md`).
- Hovering over a choice sets `0x798540 = 2`, probably the cursor shape.
- A click in a choice's polygon (`PtInRegion`) launches that mission. It
  fires on **mouse down**: `FUN_0042cf10` is called only from
  `FUN_00437840`, which only the button-down dispatcher `FUN_004711a0`
  calls (the up dispatcher `FUN_004710d0` calls `FUN_00437600`). Buttons
  act on release, which is what the reference check above saw.
- `PtInRegion` on a `CreatePolygonRgn` (ALTERNATE) region equals an
  even-odd test at the integer point: left and top edges inside, right and
  bottom outside. Checked against gdi32 for every pixel around all 27
  territories (`campaign::Region::contains`). The mouse point is first
  offset by `0x513a10` / `0x513a14`.

## Menus are drawn incrementally (applies to all menus)

The exe keeps the screen between frames; `FUN_004712c0` draws the menus as
follows.
- Every update calls `FUN_0043c4b0` for each menu in the stack, bottom to
  top.
- A menu draws its background only when `+0x50` is set, and an element
  only when its `+0x18` is set (or "RedrawEachTime").
- Elements are drawn by class, not in script order: group boxes, buttons,
  toggle buttons, list boxes, checkboxes, multi-state boxes, edit boxes,
  text boxes, value bars, animations, movies.
- If a menu below the top drew anything, every menu above it is flagged
  for a full redraw.
- **Animations** save what is under them at their first draw and restore
  it before each new frame (`FUN_0048d650`).
- **Who flags redraws:**
  - opening a menu flags all of it;
  - closing one flags every remaining menu (`FUN_00473130`);
  - a non-SKIP transition redraws everything into its "new" image;
  - element state changes (press, check) flag that element.
