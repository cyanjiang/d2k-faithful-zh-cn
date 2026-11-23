#pragma once
// Fullscreen / window mode: a settings.ini choice (--fullscreen,
// --windowed, --resolution) plus an F11 hotkey toggle at runtime.
// The 640x400 picture itself is letterboxed by render::Display, so any
// output size works without touching the game's rendering.

#include <filesystem>
#include <SDL3/SDL.h>

#include "platform/settings.hpp"

namespace d2k::app::display_mode {

// Remember the window/settings pair and apply the saved mode.
void install(SDL_Window* window, platform::Settings* settings, const std::filesystem::path& settings_file);
// Re-apply the current settings to the window (after a mode change...).
void apply();
// F11 toggles fullscreen and saves. Returns true if the event was consumed.
bool handle_event(const SDL_Event& ev);

} // namespace d2k::app::display_mode
