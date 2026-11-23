#include "app/user_dirs.hpp"

#include <SDL3/SDL.h>

namespace d2k::app {

std::filesystem::path game_save_dir() {
    std::filesystem::path dir;
    if (char* pref = SDL_GetPrefPath("d2k-faithful", "d2k")) {
        dir = std::filesystem::path(pref) / "GameSave";
        SDL_free(pref);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }
    return dir;
}

} // namespace d2k::app
