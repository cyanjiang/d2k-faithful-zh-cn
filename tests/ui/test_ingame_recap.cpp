// The in-game menus driven through MenuRunner (no window: a software
// renderer on a surface). Golden: skips without the game data.
#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <filesystem>

#include "app/menu_runner.hpp"
#include "platform/game_data.hpp"
#include "render/display.hpp"
#include "ui/surface.hpp"

using namespace d2k;
namespace fs = std::filesystem;

namespace {

bool have_data() { return fs::exists(fs::path(D2K_GAMEDATA_DIR) / "Data/UI_DATA/IGA1MIS.uil"); }

struct Headless {
    SDL_Surface* target = SDL_CreateSurface(640, 400, SDL_PIXELFORMAT_XRGB8888);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(target);
    ~Headless() {
        if (renderer) SDL_DestroyRenderer(renderer);
        if (target) SDL_DestroySurface(target);
    }
};

// One paused-loop pass with the mouse at (x, y).
app::MenuRunner::IngameEvent pass(app::MenuRunner& m, ui::Surface& s, int x, int y, bool down, bool up) {
    return m.ingame_frame(s, float(x), float(y), down, up, down, false);
}

uint16_t at(const ui::Surface& s, int x, int y) { return s.pixels()[size_t(y) * size_t(s.width()) + size_t(x)]; }

} // namespace

TEST_CASE("golden: in a mission RECAP opens and closes without a fade; Done shows INGAME over RECAP's remains "
          "(skips without data)") {
    // FUN_00472730 (0x47273e) starts no transition and FUN_00472870 steps
    // none while gGameState (0x4dfb08) is 2, so RECAP's FADE / FADE (and
    // LOAD / SAVE's TWEEN) never run in a mission. Capture of the original
    // (o_recapfade, ~12 ms apart): Done pressed, then one frame with INGAME
    // drawn over RECAP's remains with no darkening, then the game view.
    // FUN_00437840 / FUN_00437600: `FUN_004706f0() != 0 || gGameState == 2`,
    // so the buttons work at once.
    if (!have_data()) return;
    Headless h;
    REQUIRE(h.renderer);
    platform::GameData gd{fs::path(D2K_GAMEDATA_DIR)};
    render::Display display(h.renderer, render::Aspect::Crt);
    app::MenuRunner menus(display, gd);
    menus.set_global("gSideId", 0);
    menus.set_global("gMissionNumber", 1);
    constexpr uint16_t kView = 0x1234; // stands for the frozen game view
    ui::Surface screen(640, 400);
    screen.clear(kView);

    menus.ingame_open("INGAME");
    REQUIRE(menus.ingame_has("INGAME"));
    pass(menus, screen, 260, 245, false, false);
    const ui::Surface ingame_only = screen;
    // RECAP's box (160,40 300x330) outside INGAME's (200,65 205x250) shows
    // the game view under INGAME alone.
    REQUIRE(at(ingame_only, 170, 50) == kView);

    // Briefing (233,235 70x20): JumpToMenu RECAP on the release. RECAP is
    // drawn in that same pass, with no transition.
    pass(menus, screen, 260, 245, true, false);
    pass(menus, screen, 260, 245, false, true);
    REQUIRE(menus.ingame_has("RECAP"));
    CHECK(!menus.ingame_fading());
    CHECK(at(screen, 170, 50) != kView);
    const ui::Surface recap = screen;

    // Done (cancel, 274,342 50x15, BackMenu): one click.
    pass(menus, screen, 305, 352, false, false);
    CHECK(!menus.ingame_fading());
    pass(menus, screen, 305, 352, true, false);
    CHECK(menus.ingame_has("RECAP"));
    pass(menus, screen, 305, 352, false, true);
    CHECK(!menus.ingame_has("RECAP"));
    CHECK(menus.ingame_has("INGAME"));
    CHECK(!menus.ingame_fading());
    // The closing pass: INGAME is redrawn as it was, RECAP's pixels around
    // it stay as they were (no darkening); the game screen restores the
    // game view on the next pass (0x5178ac / 0x5178b0).
    CHECK(at(screen, 170, 50) == at(recap, 170, 50));
    CHECK(at(screen, 450, 360) == at(recap, 450, 360));
    int differ = 0;
    for (int y = 65; y < 65 + 250; ++y) {
        for (int x = 200; x < 200 + 205; ++x) differ += at(screen, x, y) != at(ingame_only, x, y) && at(ingame_only, x, y) != kView;
    }
    CHECK(differ == 0);
}
