#pragma once
// The first-run data setup (docs/first-run-setup.md): our own screen, shown
// when no usable 1.06 data folder exists. Drawn with SDL's debug font and
// rectangles, since no game data is available yet.

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

#include "render/display.hpp"

namespace d2k::app {

class SetupScreen {
public:
    // `dest`: where the data goes (the game's own data folder).
    SetupScreen(render::Display& display, SDL_Window* window, std::filesystem::path dest);

    // Runs until the data is installed (true) or the player quits (false).
    // `reason`: why setup is shown (start-up check problems), may be empty.
    bool run(const std::vector<std::string>& reason);

private:
    render::Display& display_;
    SDL_Window* window_;
    std::filesystem::path dest_;
};

} // namespace d2k::app
