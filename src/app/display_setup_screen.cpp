#include "app/display_setup_screen.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "app/display_mode.hpp"
#include "i18n/cjk_font.hpp"
#include "i18n/i18n.hpp"
#include "i18n/utf8.hpp"

namespace d2k::app {

namespace {

constexpr int kW = render::Display::kWidth;

struct Rgb {
    uint8_t r, g, b;
};
constexpr Rgb kBack{18, 14, 10}, kPanel{44, 34, 24}, kText{230, 214, 180}, kDim{140, 124, 100};
constexpr Rgb kAccent{226, 160, 60}, kGood{140, 200, 110}, kButton{78, 58, 36},
    kButtonHot{120, 88, 50}, kSelected{90, 130, 70};

struct Button {
    SDL_FRect r;
    std::string label;
    bool selected = false;
    bool enabled = true;
};

// Same CJK-aware painter as the first-run setup screen: ASCII via SDL's debug
// font, Unicode codepoints from the CKF sidecar. No pack loaded => pure ASCII.
class LocalisedText {
public:
    explicit LocalisedText(SDL_Renderer* ren) : ren_(ren), cjk_(i18n::cjk_font()) {}
    ~LocalisedText() {
        for (auto& [cp, tex] : cache_)
            if (tex) SDL_DestroyTexture(tex);
    }

    float width(const std::string& s) const {
        float w = 0;
        std::size_t p = 0;
        while (p < s.size()) {
            const auto d = i18n::utf8::decode(s, p);
            if (d.multibyte && cjk_) {
                if (const auto* g = cjk_->find(d.cp)) {
                    w += g->advance ? float(g->advance) : float(cjk_->cell_w());
                    continue;
                }
            }
            w += 8;
        }
        return w;
    }

    void draw(float x, float y, const std::string& s, Rgb c) {
        const std::string t = i18n::localize(s);
        float pen = x;
        std::string run;
        auto flush = [&] {
            if (run.empty()) return;
            SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
            SDL_RenderDebugText(ren_, pen, y, run.c_str());
            pen += float(run.size() * 8);
            run.clear();
        };
        std::size_t p = 0;
        while (p < t.size()) {
            const auto d = i18n::utf8::decode(t, p);
            if (d.multibyte && cjk_) {
                if (const auto* g = cjk_->find(d.cp)) {
                    flush();
                    if (SDL_Texture* tex = texture(d.cp, g)) {
                        SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
                        const SDL_FRect dst{pen, y - 4, float(cjk_->cell_w()), float(cjk_->cell_h())};
                        SDL_RenderTexture(ren_, tex, nullptr, &dst);
                    }
                    pen += g->advance ? float(g->advance) : float(cjk_->cell_w());
                    continue;
                }
            }
            const unsigned char ch = static_cast<unsigned char>(d.cp & 0xff);
            run.push_back((ch >= 32 && ch < 127) ? char(ch) : '?');
        }
        flush();
    }

private:
    SDL_Texture* texture(uint32_t cp, const i18n::CjkGlyph* g) {
        if (auto it = cache_.find(cp); it != cache_.end()) return it->second;
        const int w = cjk_->cell_w(), h = cjk_->cell_h();
        SDL_Texture* tex = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!tex) {
            cache_[cp] = nullptr;
            return nullptr;
        }
        std::vector<uint32_t> px(size_t(w) * h);
        for (std::size_t i = 0; i < px.size(); ++i) {
            const uint32_t a = i < g->alpha.size() ? g->alpha[i] : 0;
            px[i] = (a << 24) | 0x00ffffffu;
        }
        SDL_UpdateTexture(tex, nullptr, px.data(), w * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        cache_[cp] = tex;
        return tex;
    }

    SDL_Renderer* ren_;
    const i18n::CjkFont* cjk_;
    std::unordered_map<uint32_t, SDL_Texture*> cache_;
};

struct Res {
    int w = 0, h = 0; // 0x0 = the desktop's own mode
    std::string label() const {
        return w ? std::to_string(w) + "x" + std::to_string(h) : i18n::localize("Desktop default");
    }
};

// The fullscreen modes the monitor actually supports, de-duplicated by size
// and sorted small -> large, plus a "Desktop default" entry (0x0).
std::vector<Res> resolutions(SDL_Window* window) {
    std::vector<Res> out{{0, 0}};
    const SDL_DisplayID id = SDL_GetDisplayForWindow(window);
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(id, &count);
    if (modes) {
        std::vector<std::pair<int, int>> sizes;
        for (int i = 0; i < count; ++i) {
            const SDL_DisplayMode* m = modes[i];
            if (m->w >= 640 && m->h >= 400) sizes.emplace_back(m->w, m->h);
        }
        SDL_free(modes);
        std::sort(sizes.begin(), sizes.end());
        sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
        for (const auto& [w, h] : sizes) out.push_back({w, h});
    }
    if (out.size() == 1) {
        out.push_back({1280, 720});
        out.push_back({1024, 768});
    }
    return out;
}

} // namespace

DisplaySetupScreen::DisplaySetupScreen(render::Display& display, SDL_Window* window,
                                       platform::Settings& settings, std::filesystem::path settings_file)
    : display_(display), window_(window), settings_(settings), settings_file_(std::move(settings_file)) {}

DisplaySetupScreen::Result DisplaySetupScreen::run() {
    SDL_Renderer* ren = display_.renderer();
    const platform::Settings original = settings_; // restore on Cancel
    const auto list = resolutions(window_);

    bool fullscreen = settings_.fullscreen;
    bool square = settings_.aspect == "square";
    int selected = 0;
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].w == settings_.display_width && list[i].h == settings_.display_height) {
            selected = int(i);
            break;
        }
    }
    int page = 0;
    const int kCols = 4, kRows = 4, kPerPage = kCols * kRows;
    const int pages = std::max(1, (int(list.size()) + kPerPage - 1) / kPerPage);

    // Apply the choice to the live window and the letterbox aspect.
    auto preview = [&] {
        settings_.fullscreen = fullscreen;
        settings_.aspect = square ? "square" : "crt";
        settings_.display_width = list[size_t(selected)].w;
        settings_.display_height = list[size_t(selected)].h;
        display_.set_aspect(square ? render::Aspect::Square : render::Aspect::Crt);
        display_mode::apply();
    };

    constexpr float kBh = 24;
    Button mode_window{{24, 74, 150, kBh}, "Windowed"};
    Button mode_full{{182, 74, 150, kBh}, "Fullscreen"};
    Button ratio_crt{{24, 250, 160, kBh}, "CRT 4:3 (classic)"};
    Button ratio_square{{192, 250, 190, kBh}, "Square pixels 16:10"};
    Button save{{24, 356, 190, 28}, "Save & Start"};
    Button save_quit{{24 + 190 + 12, 356, 190, 28}, "Save & Quit"};
    Button cancel{{kW - 24 - 140, 356, 140, 28}, "Cancel"};
    Button prev{{kW - 24 - 92, 108, 34, 20}, "<"};
    Button next{{kW - 24 - 52, 108, 34, 20}, ">"};

    float mx = -1, my = -1;
    bool quitting = false;
    Result result = Result::Cancel;
    LocalisedText painter(ren);

    auto fill = [&](SDL_FRect r, Rgb c) {
        SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
        SDL_RenderFillRect(ren, &r);
    };
    auto hit = [&](const Button& b) {
        return b.enabled && mx >= b.r.x && mx < b.r.x + b.r.w && my >= b.r.y && my < b.r.y + b.r.h;
    };
    auto text = [&](float x, float y, const std::string& s, Rgb c) { painter.draw(x, y, s, c); };
    auto draw_button = [&](const Button& b) {
        fill(b.r, !b.enabled ? kPanel : b.selected ? kSelected : hit(b) ? kButtonHot : kButton);
        SDL_SetRenderDrawColor(ren, kAccent.r, kAccent.g, kAccent.b, 255);
        SDL_RenderRect(ren, &b.r);
        const float tw = painter.width(i18n::localize(b.label));
        text(b.r.x + (b.r.w - tw) / 2, b.r.y + (b.r.h - 8) / 2, b.label, b.enabled ? kText : kDim);
    };

    // The resolution buttons are rebuilt per page; kept as plain rects.
    auto res_rect = [&](int i) -> SDL_FRect {
        const int col = i % kCols, row = i / kCols;
        return {float(24 + col * 148), float(132 + row * 23), 140.0f, 21.0f};
    };
    auto hit_res = [&](int i) {
        const SDL_FRect r = res_rect(i);
        return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
    };

    // Initial preview so the window matches the saved choices right away.
    preview();

    while (!quitting) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) quitting = true;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE && !ev.key.repeat) quitting = true;
            if (ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                const SDL_FPoint p = display_.window_to_screen(
                    ev.type == SDL_EVENT_MOUSE_MOTION ? ev.motion.x : ev.button.x,
                    ev.type == SDL_EVENT_MOUSE_MOTION ? ev.motion.y : ev.button.y);
                mx = p.x;
                my = p.y;
            }
            if (ev.type == SDL_EVENT_MOUSE_WHEEL && my >= 120 && my < 250) {
                page = (page + (ev.wheel.y > 0 ? -1 : 1) + pages) % pages;
            }
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP && ev.button.button == SDL_BUTTON_LEFT) {
                if (hit(mode_window)) { fullscreen = false; preview(); }
                else if (hit(mode_full)) { fullscreen = true; preview(); }
                else if (hit(ratio_crt)) { square = false; preview(); }
                else if (hit(ratio_square)) { square = true; preview(); }
                else if (hit(prev)) page = (page - 1 + pages) % pages;
                else if (hit(next)) page = (page + 1) % pages;
                else if (hit(save)) { result = Result::Start; quitting = true; }
                else if (hit(save_quit)) { result = Result::Quit; quitting = true; }
                else if (hit(cancel)) { result = Result::Cancel; quitting = true; }
                else {
                    for (int i = 0; i < kPerPage; ++i) {
                        const int idx = page * kPerPage + i;
                        if (idx < int(list.size()) && hit_res(i)) { selected = idx; preview(); break; }
                    }
                }
            }
        }

        display_.begin_frame();
        fill({0, 0, float(kW), 400}, kBack);
        fill({0, 0, float(kW), 30}, kPanel);
        text(24, 10, "DUNE 2000  -  DISPLAY SETUP", kAccent);
        text(24, 42, "Choose how the game fills the screen. Changes preview immediately.", kText);

        text(24, 60, "Display mode", kAccent);
        mode_window.selected = !fullscreen;
        mode_full.selected = fullscreen;
        draw_button(mode_window);
        draw_button(mode_full);

        text(24, 110, "Resolution", kAccent);
        prev.enabled = next.enabled = pages > 1;
        draw_button(prev);
        draw_button(next);
        const std::string page_label = std::to_string(page + 1) + "/" + std::to_string(pages);
        text(kW - 24 - 92 - painter.width(i18n::localize(page_label)) - 8, 113, page_label, kDim);
        for (int i = 0; i < kPerPage; ++i) {
            const int idx = page * kPerPage + i;
            if (idx >= int(list.size())) break;
            const SDL_FRect r = res_rect(i);
            const bool on = idx == selected;
            fill(r, on ? kSelected : hit_res(i) ? kButtonHot : kButton);
            SDL_SetRenderDrawColor(ren, (on ? kGood : kAccent).r, (on ? kGood : kAccent).g,
                                   (on ? kGood : kAccent).b, 255);
            SDL_RenderRect(ren, &r);
            const std::string label = list[size_t(idx)].label();
            const float tw = painter.width(i18n::localize(label));
            text(r.x + (r.w - tw) / 2, r.y + (r.h - 8) / 2, label, on ? kText : kDim);
        }

        text(24, 235, "Aspect ratio", kAccent);
        ratio_crt.selected = !square;
        ratio_square.selected = square;
        draw_button(ratio_crt);
        draw_button(ratio_square);

        const std::string cur = i18n::localize("Current: ") +
            (fullscreen ? i18n::localize("Fullscreen") : i18n::localize("Windowed")) + "  " +
            list[size_t(selected)].label() + "  " + (square ? "16:10" : "4:3");
        text(24, 296, cur, kGood);
        text(24, 314, i18n::localize("Press F11 in game to toggle fullscreen at any time."), kDim);
        text(24, 330, i18n::localize("Settings are saved to settings.ini; no command line needed."), kDim);

        draw_button(save);
        draw_button(save_quit);
        draw_button(cancel);
        display_.end_frame();
        SDL_Delay(16);
    }

    if (result == Result::Cancel) {
        settings_ = original; // discard the live preview
        display_.set_aspect(original.aspect == "square" ? render::Aspect::Square : render::Aspect::Crt);
        display_mode::apply();
        return result;
    }
    settings_.save(settings_file_);
    return result;
}

} // namespace d2k::app
