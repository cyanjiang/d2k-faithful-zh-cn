#pragma once
// Graphical display setup wizard (our own screen, like SetupScreen). Lets the
// player pick window/fullscreen, a resolution and the aspect ratio with the
// mouse, so no command-line arguments are needed. Drawn with the CKF sidecar
// font so the labels are localised when a language pack is active.

#include <SDL3/SDL.h>

#include <filesystem>

#include "platform/settings.hpp"
#include "render/display.hpp"

namespace d2k::app {

class DisplaySetupScreen {
public:
    DisplaySetupScreen(render::Display& display, SDL_Window* window, platform::Settings& settings,
                       std::filesystem::path settings_file);

    enum class Result { Start, Quit, Cancel };

    // Runs until the player picks an action. Settings are updated (and applied
    // to the window) live for preview; on Cancel the originals are restored.
    // Start = save and continue into the game; Quit = save and exit; Cancel =
    // discard changes and exit.
    Result run();

private:
    render::Display& display_;
    SDL_Window* window_;
    platform::Settings& settings_;
    std::filesystem::path settings_file_;
};

} // namespace d2k::app
