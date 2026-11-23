#include "app/display_mode.hpp"

#include "render/display.hpp"

namespace d2k::app::display_mode {

namespace {
SDL_Window* window_ = nullptr;
platform::Settings* settings_ = nullptr;
std::filesystem::path file_;
} // namespace

void install(SDL_Window* window, platform::Settings* settings, const std::filesystem::path& settings_file) {
    window_ = window;
    settings_ = settings;
    file_ = settings_file;
    apply();
}

void apply() {
    if (!window_ || !settings_) return;
    if (!settings_->fullscreen) {
        SDL_SetWindowFullscreen(window_, 0);
        SDL_SetWindowBordered(window_, true);
        // A resolution chosen in the setup wizard becomes the window size;
        // otherwise fall back to the original 2x / 4:3 frame (main.cpp).
        int w = settings_->display_width, h = settings_->display_height;
        if (w < 320 || h < 200) {
            w = render::Display::kWidth * 2;
            h = settings_->aspect == "square" ? render::Display::kHeight * 2 : w * 3 / 4;
        }
        SDL_SetWindowSize(window_, w, h);
        SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        return;
    }
    SDL_DisplayMode want{};
    if (settings_->display_width >= 320 && settings_->display_height >= 200) {
        want.w = settings_->display_width;
        want.h = settings_->display_height;
        // The closest mode the monitor supports: the smallest one at least
        // as large as the request, else the largest available; none found
        // (nullptr) falls back to the desktop's own resolution.
        const SDL_DisplayID id = SDL_GetDisplayForWindow(window_);
        int count = 0;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(id, &count);
        const SDL_DisplayMode* best = nullptr;
        if (modes) {
            for (int i = 0; i < count; ++i) {
                const SDL_DisplayMode* m = modes[i];
                if (m->w < want.w || m->h < want.h) continue;
                if (!best || m->w * m->h < best->w * best->h) best = m;
            }
            if (!best) {
                for (int i = 0; i < count; ++i) {
                    const SDL_DisplayMode* m = modes[i];
                    if (!best || m->w * m->h > best->w * best->h) best = m;
                }
            }
        }
        SDL_SetWindowFullscreenMode(window_, best);
        if (modes) SDL_free(modes);
    } else {
        SDL_SetWindowFullscreenMode(window_, nullptr); // the desktop's own mode
    }
    SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN);
}

bool handle_event(const SDL_Event& ev) {
    if (!window_ || ev.type != SDL_EVENT_KEY_DOWN || ev.key.repeat) return false;
    if (ev.key.key != SDLK_F11) return false;
    settings_->fullscreen = !settings_->fullscreen;
    apply();
    if (!file_.empty()) settings_->save(file_);
    return true;
}

} // namespace d2k::app::display_mode
