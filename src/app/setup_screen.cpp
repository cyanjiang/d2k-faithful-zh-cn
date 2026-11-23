#include "app/setup_screen.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include "i18n/cjk_font.hpp"
#include "i18n/i18n.hpp"
#include "i18n/utf8.hpp"
#include "platform/manifest.hpp"
#include "platform/setup_sources.hpp"

namespace d2k::app {

namespace fs = std::filesystem;
namespace setup = platform::setup;

namespace {

constexpr int kW = render::Display::kWidth;

struct Rgb {
    uint8_t r, g, b;
};
constexpr Rgb kBack{18, 14, 10}, kPanel{44, 34, 24}, kText{230, 214, 180}, kDim{140, 124, 100};
constexpr Rgb kAccent{226, 160, 60}, kGood{140, 200, 110}, kBad{230, 110, 90}, kButton{78, 58, 36},
    kButtonHot{120, 88, 50};

struct Button {
    SDL_FRect r;
    std::string label;
    bool enabled = true;
};

// Truncate on a UTF-8 boundary so a Chinese status line never ends in a half
// character (which the surface renderer would then draw as garbage).
std::string clip(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    return i18n::utf8::truncate(s, n - 3) + "...";
}

// The first-run setup runs before any game data (and therefore before
// FONT.FNT) is available, so it uses SDL's built-in 8x8 debug font. That font
// is ASCII-only. When a localisation pack is active, draw CJK codepoints from
// the CKF sidecar instead, caching one SDL texture per glyph. With no pack
// loaded every string stays ASCII and the screen is pixel-identical to before.
class LocalisedText {
public:
    explicit LocalisedText(SDL_Renderer* ren) : ren_(ren), cjk_(i18n::cjk_font()) {}
    ~LocalisedText() {
        for (auto& [cp, tex] : cache_) {
            if (tex) SDL_DestroyTexture(tex);
        }
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
        std::string run; // consecutive ASCII, drawn in one call like before
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
                        // 16px CJK glyph vertically centred on the 8px ASCII line
                        const SDL_FRect dst{pen, y - 4, float(cjk_->cell_w()), float(cjk_->cell_h())};
                        SDL_RenderTexture(ren_, tex, nullptr, &dst);
                    }
                    pen += g->advance ? float(g->advance) : float(cjk_->cell_w());
                    continue;
                }
            }
            const unsigned char ch = static_cast<unsigned char>(d.cp & 0xff);
            run.push_back((ch >= 32 && ch < 127) ? char(ch) : '?'); // same filtering as ascii()
        }
        flush();
    }

private:
    SDL_Texture* texture(uint32_t cp, const i18n::CjkGlyph* g) {
        if (auto it = cache_.find(cp); it != cache_.end()) return it->second;
        const int w = cjk_->cell_w(), h = cjk_->cell_h();
        SDL_Texture* tex = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_STATIC, w, h);
        if (!tex) {
            cache_[cp] = nullptr;
            return nullptr;
        }
        std::vector<uint32_t> px(size_t(w) * h);
        for (std::size_t i = 0; i < px.size(); ++i) {
            const uint32_t a = i < g->alpha.size() ? g->alpha[i] : 0;
            px[i] = (a << 24) | 0x00ffffffu; // white RGB, alpha from the glyph
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

std::string mb(uint64_t bytes) { return std::to_string((bytes + (1u << 19)) >> 20) + " MB"; }

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A file or folder picked in a native dialog (the callback may come on
// another thread).
struct Pick {
    std::mutex m;
    std::optional<std::string> path;
    bool open = false;
    enum Kind { Image, Folder, Patch } kind = Image;
};

void SDLCALL on_pick(void* user, const char* const* list, int) {
    auto* p = static_cast<Pick*>(user);
    std::lock_guard lock(p->m);
    p->open = false;
    if (list && list[0]) p->path = list[0];
}

// One background job (scan or install) at a time.
struct Job {
    std::thread thread;
    std::atomic<bool> busy{false}, cancel{false};
    std::atomic<uint64_t> done{0}, total{0};
    std::mutex m;
    std::string what, error;
    bool installed = false;

    void start(std::string label, std::function<void()> fn) {
        if (thread.joinable()) thread.join();
        {
            std::lock_guard lock(m);
            what = std::move(label);
            error.clear();
        }
        done = total = 0;
        cancel = false;
        busy = true;
        thread = std::thread([this, fn = std::move(fn)] {
            try {
                fn();
            } catch (const std::exception& e) {
                std::lock_guard lock(m);
                error = e.what();
            }
            busy = false;
        });
    }
    setup::Progress progress() {
        return [this](uint64_t d, uint64_t t) {
            done = d;
            total = t;
            return !cancel.load();
        };
    }
    ~Job() {
        cancel = true;
        if (thread.joinable()) thread.join();
    }
};

} // namespace

SetupScreen::SetupScreen(render::Display& display, SDL_Window* window, fs::path dest)
    : display_(display), window_(window), dest_(std::move(dest)) {}

bool SetupScreen::run(const std::vector<std::string>& reason) {
    SDL_Renderer* ren = display_.renderer();
    setup::Collector collector;
    Job job;
    static Pick pick; // a dialog's callback can arrive after this screen is gone
    {
        std::lock_guard lock(pick.m);
        pick.path.reset();
    }
    std::vector<std::string> notes; // results of the last action
    std::string status = reason.empty() ? ""
        : i18n::localize("The game data folder is not usable: ") + reason.front();
    bool status_bad = !reason.empty();

    auto add_source = [&](fs::path path, Pick::Kind kind) {
        for (const auto& s : collector.sources()) {
            std::error_code ec;
            if (fs::equivalent(s.location, path, ec)) {
                status = path.filename().string() + i18n::localize(" is already added.");
                status_bad = false;
                return;
            }
        }
        job.start(i18n::localize("Reading ") + path.filename().string(), [&, path, kind] {
            setup::Source src = kind == Pick::Image   ? setup::open_cd_image(path)
                                : kind == Pick::Patch ? setup::open_patch(path)
                                                      : setup::open_folder(path);
            const std::string label = src.label;
            const size_t n = collector.add(std::move(src), job.progress());
            std::lock_guard lock(job.m);
            if (job.cancel) job.error = i18n::localize("Stopped.");
            else if (n == 0) job.error = label + i18n::localize(" has none of the files still needed.");
            else job.what = label + ": " + std::to_string(n) + i18n::localize(" files found.");
        });
    };

    // Files kept next to the game (or in the folder it was started from).
    {
        std::vector<fs::path> dirs;
        if (const char* base = SDL_GetBasePath()) dirs.emplace_back(base);
        std::error_code ec;
        dirs.push_back(fs::current_path(ec));
        std::vector<std::pair<fs::path, Pick::Kind>> found;
        for (const auto& d : dirs) {
            for (const auto& e : fs::directory_iterator(d, ec)) {
                const std::string n = lower(e.path().filename().string());
                const Pick::Kind k = n.ends_with(".iso") ? Pick::Image : Pick::Patch;
                if (!(n.ends_with(".iso") || n == "d2k106us.exe")) continue;
                if (std::none_of(found.begin(), found.end(), [&](const auto& f) { return fs::equivalent(f.first, e.path(), ec); })) {
                    found.emplace_back(e.path(), k);
                }
            }
        }
        if (!found.empty()) {
            job.start(i18n::localize("Reading the files next to the game"), [&, found] {
                for (const auto& [p, k] : found) {
                    try {
                        collector.add(k == Pick::Image ? setup::open_cd_image(p) : setup::open_patch(p), job.progress());
                    } catch (const std::exception&) {
                        // not a Dune 2000 file: ignore it here
                    }
                    if (job.cancel) return;
                }
                std::lock_guard lock(job.m);
                job.what = i18n::localize("Read the files next to the game.");
            });
        }
    }

    constexpr float kBw = 190, kBh = 22; // three across 592 px with 10 px gaps
    Button add_image{{24, 118, kBw, kBh}, "Add CD image (.iso)"};
    Button add_folder{{24 + kBw + 10, 118, kBw, kBh}, "Add CD drive or folder"};
    Button add_patch{{24 + 2 * (kBw + 10), 118, kBw, kBh}, "Add patch d2k106us.exe"};
    Button install{{24, 360, 140, 24}, "Install"};
    Button quit{{kW - 24 - 100, 360, 100, 24}, "Quit"};
    float mx = -1, my = -1;
    bool quitting = false, was_busy = job.busy;

    LocalisedText painter(ren);
    auto text = [&](float x, float y, const std::string& s, Rgb c) {
        painter.draw(x, y, s, c);
    };
    auto fill = [&](SDL_FRect r, Rgb c) {
        SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
        SDL_RenderFillRect(ren, &r);
    };
    auto hit = [&](const Button& b) { return b.enabled && mx >= b.r.x && mx < b.r.x + b.r.w && my >= b.r.y && my < b.r.y + b.r.h; };
    auto draw_button = [&](const Button& b) {
        fill(b.r, b.enabled ? (hit(b) ? kButtonHot : kButton) : kPanel);
        SDL_SetRenderDrawColor(ren, b.enabled ? kAccent.r : kDim.r, b.enabled ? kAccent.g : kDim.g, b.enabled ? kAccent.b : kDim.b, 255);
        SDL_RenderRect(ren, &b.r);
        const float tw = painter.width(i18n::localize(b.label));
        text(b.r.x + (b.r.w - tw) / 2, b.r.y + (b.r.h - 8) / 2, b.label, b.enabled ? kText : kDim);
    };
    auto bar = [&](float y, uint64_t done, uint64_t total, Rgb c) {
        const SDL_FRect frame{24, y, float(kW - 48), 10};
        fill(frame, kPanel);
        if (total) fill({frame.x, y, frame.w * float(double(done) / double(total)), 10}, c);
    };

    auto click = [&] {
        if (hit(quit)) {
            if (job.busy) job.cancel = true;
            else quitting = true;
        } else if (hit(add_image) || hit(add_patch) || hit(add_folder)) {
            {
                std::lock_guard lock(pick.m);
                pick.open = true;
                pick.kind = hit(add_image) ? Pick::Image : hit(add_patch) ? Pick::Patch : Pick::Folder;
            }
            if (hit(add_folder)) {
                SDL_ShowOpenFolderDialog(on_pick, &pick, window_, nullptr, false);
            } else if (hit(add_image)) {
                static const SDL_DialogFileFilter f[] = {{"CD image", "iso;bin"}, {"All files", "*"}};
                SDL_ShowOpenFileDialog(on_pick, &pick, window_, f, 2, nullptr, false);
            } else {
                static const SDL_DialogFileFilter f[] = {{"Dune 2000 patch", "exe"}, {"All files", "*"}};
                SDL_ShowOpenFileDialog(on_pick, &pick, window_, f, 2, nullptr, false);
            }
        } else if (hit(install)) {
            job.start(i18n::localize("Installing"), [&] {
                std::error_code ec;
                fs::create_directories(dest_.parent_path(), ec);
                const bool ok = collector.install(dest_, job.progress());
                std::lock_guard lock(job.m);
                if (ok) {
                    job.installed = true;
                    job.what = i18n::localize("Installed.");
                } else {
                    job.error = i18n::localize("Install stopped. Nothing was changed.");
                }
            });
            was_busy = true;
        }
    };

    while (!quitting) {
        // Results of a finished job.
        if (was_busy && !job.busy) {
            if (job.thread.joinable()) job.thread.join();
            std::lock_guard lock(job.m);
            status_bad = !job.error.empty();
            status = status_bad ? job.error : job.what;
            if (job.installed) return true;
        }
        was_busy = job.busy;
        // A file picked in a dialog.
        {
            std::optional<std::string> p;
            Pick::Kind kind;
            {
                std::lock_guard lock(pick.m);
                p.swap(pick.path);
                kind = pick.kind;
            }
            if (p && !job.busy) add_source(fs::path(reinterpret_cast<const char8_t*>(p->c_str())), kind);
        }
        bool pick_open;
        {
            std::lock_guard lock(pick.m);
            pick_open = pick.open;
        }
        const bool idle = !job.busy && !pick_open;
        add_image.enabled = add_folder.enabled = add_patch.enabled = idle && !collector.complete();
        install.enabled = idle && collector.complete();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) quitting = true;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
                if (job.busy) job.cancel = true;
                else quitting = true;
            }
            if (ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                const SDL_FPoint p = display_.window_to_screen(ev.type == SDL_EVENT_MOUSE_MOTION ? ev.motion.x : ev.button.x,
                                                               ev.type == SDL_EVENT_MOUSE_MOTION ? ev.motion.y : ev.button.y);
                mx = p.x;
                my = p.y;
            }
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP && ev.button.button == SDL_BUTTON_LEFT) click();
        }

        display_.begin_frame();
        fill({0, 0, float(kW), 400}, kBack);
        fill({0, 0, float(kW), 30}, kPanel);
        text(24, 11, "DUNE 2000  -  GAME DATA SETUP", kAccent);
        text(24, 46, "This game plays from the files of your own Dune 2000 (US) CD.", kText);
        text(24, 60, "Add your CD image, your CD drive (or a folder with its files)", kText);
        text(24, 72, "or your installed Dune 2000 folder, and the official 1.06 patch.", kText);
        text(24, 90, "Everything is checked and copied once into the game's own folder.", kDim);
        draw_button(add_image);
        draw_button(add_folder);
        draw_button(add_patch);

        float y = 160;
        text(24, y, "Sources", kAccent);
        y += 14;
        if (!job.busy && collector.sources().empty()) {
            text(36, y, "none yet", kDim);
            y += 12;
        }
        if (!job.busy) {
            for (size_t i = 0; i < collector.sources().size() && y < 230; ++i) {
                const auto& s = collector.sources()[i];
                text(36, y, clip(s.label, 52), kText);
                text(470, y, std::to_string(collector.supplied_by(i)) + i18n::localize(" files"), kDim);
                y += 12;
            }
        }
        y = 238;
        const size_t found = job.busy ? 0 : collector.found(), needed = collector.needed();
        if (!job.busy) {
            text(24, y, i18n::localize("Game files  ") + std::to_string(found) + " / " + std::to_string(needed), kAccent);
            bar(y + 12, found, needed, collector.complete() ? kGood : kAccent);
            y += 30;
            if (collector.complete()) {
                text(24, y, "Everything is here. Press Install.", kGood);
            } else if (!collector.sources().empty()) {
                text(24, y, "Still needed:", kText);
                y += 12;
                for (const auto& line : collector.missing_summary()) {
                    if (y > 330) break;
                    text(36, y, clip(line, 74), kDim);
                    y += 12;
                }
            }
        } else {
            std::string what;
            {
                std::lock_guard lock(job.m);
                what = job.what;
            }
            const uint64_t d = job.done, t = job.total;
            text(24, y, clip(what, 60) + (t ? "   " + mb(d) + " / " + mb(t) : ""), kAccent);
            bar(y + 12, d, t, kAccent);
            text(24, y + 30, "Esc stops.", kDim);
        }
        if (!status.empty() && !job.busy) text(24, 342, clip(status, 76), status_bad ? kBad : kGood);
        install.label = "Install";
        quit.label = job.busy ? "Stop" : "Quit";
        quit.enabled = true;
        draw_button(install);
        draw_button(quit);
        display_.end_frame();
        SDL_Delay(16);
    }
    job.cancel = true;
    return false;
}

} // namespace d2k::app
