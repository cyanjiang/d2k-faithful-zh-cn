// The front end's mouse input switch (docs/re/04, "Mouse input switch"),
// driven through MenuRunner without a window (a software renderer on a
// surface, no movie playback). Golden: skips without the game data.
#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <filesystem>

#include "app/menu_runner.hpp"
#include "platform/game_data.hpp"
#include "render/display.hpp"

using namespace d2k;
namespace fs = std::filesystem;

namespace {

bool have_data() { return fs::exists(fs::path(D2K_GAMEDATA_DIR) / "Data/UI_DATA/playAT.uil"); }

struct Headless {
    SDL_Surface* target = SDL_CreateSurface(640, 400, SDL_PIXELFORMAT_XRGB8888);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(target);
    ~Headless() {
        if (renderer) SDL_DestroyRenderer(renderer);
        if (target) SDL_DestroySurface(target);
    }
};

// HOUSESELECTION, Atreides, the house movie (HouseAt.uil: MOVIE ...
// BackMenuAndJumpTo PLAY_ATREIDES), then `idle` passes without a click
// after the movie pass, then a click (press and release in one pass) on
// PLAY_ATREIDES's No (359,340 45x20, BackMenu). True if that click closed
// PLAY_ATREIDES.
bool click_after_house_movie(int idle) {
    Headless h;
    REQUIRE(h.renderer);
    platform::GameData gd{fs::path(D2K_GAMEDATA_DIR)};
    render::Display display(h.renderer, render::Aspect::Crt);
    app::MenuRunner menus(display, gd);
    int movies = 0;
    menus.set_movie_hook([&](const fs::path&) {
        ++movies;
        return video::MovieResult::Finished;
    });
    menus.front_begin("HOUSESELECTION");
    // HOUSESELECTION opens with FADE (12 ms per step); then let the
    // 10-query delay run out.
    for (int i = 0; i < 200 && menus.ingame_fading(); ++i) {
        menus.front_step(0, 0, false, false);
        SDL_Delay(13);
    }
    REQUIRE(!menus.ingame_fading());
    for (int i = 0; i < 12; ++i) menus.front_step(0, 0, false, false);

    // Atreides (61,287 84x60): JumpToMenu MVE_ATREIDES on the release.
    menus.front_step(100, 310, true, true);
    REQUIRE(menus.front_top() == "MVE_ATREIDES");
    REQUIRE(movies == 0);
    menus.front_step(100, 310, false, false); // the movie pass
    REQUIRE(movies == 1);
    for (int i = 0; i < idle; ++i) menus.front_step(380, 350, false, false);
    REQUIRE(menus.front_top() == "PLAY_ATREIDES");
    menus.front_step(380, 350, true, true);
    return menus.front_top() == "HOUSESELECTION";
}

} // namespace

TEST_CASE("golden: after the house movie PLAY_ATREIDES takes a click 12 passes after the movie pass, as the exe "
          "(skips without data)") {
    // The exe, one loop pass = FUN_00492a70 (message pump, UI update
    // FUN_004712c0, cursor query FUN_004706f0):
    //  movie pass M: FUN_00472870 switches input on (the MVE page's open
    //    switched it off, 0x43644a); FUN_0048d8f0 plays the movie, queues
    //    BackMenuAndJumpTo PLAY_ATREIDES, and 0x797e28 skips the cursor
    //    query;
    //  M+1: the navigation (0x47259f..0x4726a4) closes the movie page and
    //    opens PLAY_ATREIDES: +0x11d = 1 and input off (FUN_00437040,
    //    0x43644a); the cursor query answers off;
    //  M+2: the handlers return (+0x11d); FUN_00472870 (SKIP) switches input
    //    on: a change, so the delay is 10; cursor query 10 -> 9;
    //  M+3..M+11: cursor queries 9 -> 0 (all "off");
    //  M+12: the pump's press handler gets "on".
    if (!have_data()) return;
    CHECK(!click_after_house_movie(10)); // pass M+11
    CHECK(click_after_house_movie(11));  // pass M+12
}

