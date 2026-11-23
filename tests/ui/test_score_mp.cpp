// The skirmish SCORE page (Practice Game Results) built at run time by
// FUN_0043dcb0 (docs/re/56 section 1). Golden: skips without the game data.
#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <filesystem>

#include "app/menu_runner.hpp"
#include "platform/game_data.hpp"
#include "render/display.hpp"
#include "ui/assets.hpp"
#include "ui/surface.hpp"

using namespace d2k;
namespace fs = std::filesystem;

namespace {

struct Headless {
    SDL_Surface* target = SDL_CreateSurface(640, 400, SDL_PIXELFORMAT_XRGB8888);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(target);
    ~Headless() {
        if (renderer) SDL_DestroyRenderer(renderer);
        if (target) SDL_DestroySurface(target);
    }
};

// The skirmish SCORE drawn offscreen with the given rows.
ui::Surface draw_score(const platform::GameData& gd, render::Display& display,
                       const std::array<app::MenuRunner::SideStats, 8>& stats) {
    app::MenuRunner menus(display, gd);
    menus.set_global("gGameType", 1);
    menus.set_global_string("gNetPlayerName", "Kyubey");
    menus.set_score_stats(stats);
    menus.ingame_open("SCORE");
    REQUIRE(menus.ingame_has("SCORE"));
    ui::Surface s(640, 400);
    menus.ingame_draw(s);
    return s;
}

uint16_t at(const ui::Surface& s, int x, int y) { return s.pixels()[size_t(y) * size_t(s.width()) + size_t(x)]; }

// The leftmost column in [x0, x1) x [y0, y1) where a and b differ, or -1.
int leftmost_diff(const ui::Surface& a, const ui::Surface& b, int x0, int x1, int y0, int y1) {
    for (int x = x0; x < x1; ++x) {
        for (int y = y0; y < y1; ++y) {
            if (at(a, x, y) != at(b, x, y)) return x;
        }
    }
    return -1;
}

} // namespace

TEST_CASE("golden: skirmish SCORE table texts get the 5 px margin once (FUN_0043dcb0 / FUN_0048bb10) "
          "(skips without data)") {
    // FUN_0043dcb0 creates the row boxes with FUN_004384a0 at the exe's x
    // (name 0x71 = 113, inflicted 0xeb = 235, ...), left aligned; the box
    // keeps that x (FUN_0048b9c0) and FUN_0048bb10 draws left text at
    // x + 5. Capture of the original (o_skir_score.png): the first number
    // column's "0" starts at x 240; ours drew it at 245 (margin twice).
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/UI_DATA")) return;
    Headless h;
    REQUIRE(h.renderer);
    platform::GameData gd{root};
    render::Display display(h.renderer, render::Aspect::Crt);

    std::array<app::MenuRunner::SideStats, 8> none{};
    std::array<app::MenuRunner::SideStats, 8> one{};
    one[0].active = true;
    one[0].row = true;
    one[0].house = 0;
    one[0].colour = 0;
    one[0].place = 0;
    // All zero: every number column draws "0".
    const ui::Surface without = draw_score(gd, display, none);
    const ui::Surface with = draw_score(gd, display, one);

    // Row 0 is y 101..124 (0x65, 24 high).
    const int number_x = leftmost_diff(with, without, 235, 304, 101, 125);
    CHECK(number_x == 240); // the original's capture

    // The name: drawn from 113 + 5 = 118 (its ink may start left of the
    // origin, so compare with the same text drawn at 118).
    ui::Assets assets(gd);
    ui::Surface ref(640, 400), blank(640, 400);
    ref.draw_text_solid(assets.menu_font(), 118, 101, "Kyubey", 0x7fff);
    const int expected_name_x = leftmost_diff(ref, blank, 100, 235, 101, 125);
    REQUIRE(expected_name_x >= 0);
    const int name_x = leftmost_diff(with, without, 113, 235, 101, 125);
    CHECK(name_x == expected_name_x);
    CHECK(name_x < 120); // ours drew it 5 px further right (at 123)
}
