// d2k: game entry point. Validates the data folder, opens an integer-scaled
// 640x400 window and runs the original boot sequence and menu flow.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "i18n/i18n.hpp"

#include "app/display_mode.hpp"
#include "app/display_setup_screen.hpp"
#include "app/game_screen.hpp"
#include "app/menu_runner.hpp"
#include "app/setup_screen.hpp"
#include "platform/game_data.hpp"
#include "platform/manifest.hpp"
#include "platform/settings.hpp"
#include "render/display.hpp"
#include "video/movie_player.hpp"

namespace {

using namespace d2k;

// The exe's screen size globals (0x4eb028 / 0x4eb02c) are 640 x 400 and are
// never written: menus, movies and the game all use the whole 640x400
// screen; the game's playfield is the 480x380 under the top bar, left of
// the sidebar (docs/re/07; no resolution setting in 1.06).
constexpr int kLogicalWidth = 640;
constexpr int kLogicalHeight = 400;

// d2k.log next to settings.ini: every SDL_Log line of this run, flushed at
// once, so a playtest log can be read on any machine (also Windows Sandbox).
struct LogFile {
    std::FILE* file = nullptr;
    SDL_LogOutputFunction next = nullptr;
    void* next_data = nullptr;
};
LogFile g_log;

void log_to_file(void*, int category, SDL_LogPriority priority, const char* message) {
    if (g_log.file) {
        std::fprintf(g_log.file, "%llu %s\n", static_cast<unsigned long long>(SDL_GetTicks()), message);
        std::fflush(g_log.file);
    }
    if (g_log.next) g_log.next(g_log.next_data, category, priority, message);
}

void open_log_file() {
    char* pref = SDL_GetPrefPath("d2k-faithful", "d2k");
    if (!pref) return;
    const auto path = std::filesystem::path(pref) / "d2k.log";
    SDL_free(pref);
#ifdef _WIN32
    g_log.file = _wfopen(path.c_str(), L"w");
#else
    g_log.file = std::fopen(path.c_str(), "w");
#endif
    if (!g_log.file) return;
    SDL_GetLogOutputFunction(&g_log.next, &g_log.next_data);
    SDL_SetLogOutputFunction(log_to_file, nullptr);
}

} // namespace

int main(int argc, char* argv[]) {
    std::string data_dir;     // --data DIR: a ready 1.06 folder (skips the first-run setup)
    bool data_given = false;
    bool reset_intro = false; // --reset-intro: behave like a first launch
    std::string aspect;       // --aspect crt|square
    int speed = -1;           // --speed 0..100 (the game speed slider; saved)
    int grab = -1;            // --grab 0|1: keep the mouse inside the window (saved)
    int enhanced = -1;        // --enhanced-movies 0|1: upscaled movie pictures (saved)
    std::string lang;         // --lang CODE: optional localisation pack (saved)
    int fullscreen = -1;      // --fullscreen | --windowed (saved)
    std::string resolution;   // --resolution WxH|desktop (saved)
    bool display_setup = false; // --display-setup: open the graphical wizard
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) {
            data_dir = argv[++i];
            data_given = true;
        }
        else if (a == "--reset-intro") reset_intro = true;
        else if (a == "--aspect" && i + 1 < argc) aspect = argv[++i]; // crt | square (saved)
        else if (a == "--speed" && i + 1 < argc) speed = std::atoi(argv[++i]); // 0..100 (saved)
        else if (a == "--grab" && i + 1 < argc) grab = std::atoi(argv[++i]);   // 0 | 1 (saved)
        else if (a == "--enhanced-movies" && i + 1 < argc) enhanced = std::atoi(argv[++i]); // 0 | 1 (saved)
        else if (a == "--lang" && i + 1 < argc) lang = argv[++i];                          // i18n/<lang>
        else if (a == "--fullscreen") fullscreen = 1;
        else if (a == "--windowed") fullscreen = 0;
        else if (a == "--resolution" && i + 1 < argc) resolution = argv[++i]; // WxH or "desktop"
        else if (a == "--display-setup") display_setup = true;
    }
    open_log_file();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    if (data_given) {
        auto problems = platform::quick_check(data_dir);
        if (!problems.empty()) {
            std::string msg = "Dune 2000 data folder is not usable:\n" + data_dir + "\n";
            for (const auto& p : problems) msg += "\n- " + p;
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "d2k", msg.c_str(), nullptr);
            SDL_Quit();
            return 1;
        }
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Dune 2000", kLogicalWidth * 2, kLogicalHeight * 2,
                                     SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Log("window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    std::filesystem::path settings_file, own_data;
    if (char* pref = SDL_GetPrefPath("d2k-faithful", "d2k")) {
        settings_file = std::filesystem::path(pref) / "settings.ini";
        own_data = std::filesystem::path(pref) / "gamedata" / "1.06"; // the first-run setup's copy
        SDL_free(pref);
    }
    auto settings = platform::Settings::load(settings_file);
    if (reset_intro) settings.play_full_intro = true;
    if (!lang.empty()) {
        settings.lang = lang;
        settings.save(settings_file);
    }
    // Localisation packs are optional and never part of the hash-checked game
    // data: look next to the executable first, then in the user folder.
    {
        std::vector<std::filesystem::path> dirs;
        // SDL3's SDL_GetBasePath() returns a const, SDL-managed string (unlike
        // SDL_GetPrefPath(), whose char* we free above); do not SDL_free it.
        if (const char* base = SDL_GetBasePath()) dirs.emplace_back(base);
        if (!settings_file.empty()) dirs.push_back(settings_file.parent_path());
        i18n::I18n::instance().init(dirs, settings.lang);
        SDL_Log("i18n: %s", i18n::I18n::instance().status().c_str());
    }
    if (aspect == "crt" || aspect == "square") {
        settings.aspect = aspect;
        settings.save(settings_file);
    }
    if (grab >= 0) {
        settings.mouse_grab = grab != 0;
        settings.save(settings_file);
    }
    if (fullscreen >= 0) {
        settings.fullscreen = fullscreen == 1;
        settings.save(settings_file);
    }
    if (!resolution.empty()) {
        // "WxH" picks the closest mode the monitor supports; "desktop" (or
        // "0x0") stays at the desktop's own resolution.
        int w = 0, h = 0;
        if (resolution == "desktop" || resolution == "0x0" ||
            std::sscanf(resolution.c_str(), "%dx%d", &w, &h) != 2 || w < 320 || h < 200) {
            w = 0;
            h = 0;
        }
        settings.display_width = w;
        settings.display_height = h;
        settings.save(settings_file);
    }
    SDL_SetWindowMouseGrab(window, settings.mouse_grab);
    // Fullscreen and the saved resolution (F11 toggles at runtime; the
    // 640x400 picture is letterboxed by render::Display at any size).
    app::display_mode::install(window, &settings, settings_file);
    if (enhanced >= 0) {
        settings.enhanced_movies = enhanced != 0;
        settings.save(settings_file);
    }
    // Upscaled movie pictures (docs/enhanced-movies.md): <user folder>/movies-enhanced.
    if (settings.enhanced_movies && !settings_file.empty()) {
        video::set_enhanced_movies_dir(settings_file.parent_path() / "movies-enhanced");
    }
    if (speed >= 0) {
        settings.game_speed = speed > 100 ? 100 : speed;
        settings.save(settings_file);
    }
    render::Display display(renderer, settings.aspect == "square" ? render::Aspect::Square
                                                                  : render::Aspect::Crt);
    if (!settings.fullscreen && settings.aspect != "square") {
        SDL_SetWindowSize(window, kLogicalWidth * 2, kLogicalWidth * 2 * 3 / 4); // 1280x960
    }

    // Graphical display setup (double-click "显示设置.bat" / --display-setup):
    // pick window/fullscreen and a resolution with the mouse, no command line.
    // Preview is live; Cancel restores the saved values.
    if (display_setup) {
        const auto r = app::DisplaySetupScreen(display, window, settings, settings_file).run();
        if (r != app::DisplaySetupScreen::Result::Start) {
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 0;
        }
    }

    // The game data (docs/first-run-setup.md): --data, else the game's own
    // copy, else the first-run setup screen fills it.
    if (!data_given) {
        auto problems = platform::quick_check(own_data);
        data_dir = own_data.string();
        if (!problems.empty()) {
            std::error_code ec; // a fresh first run says nothing about a folder that never existed
            if (!std::filesystem::exists(own_data, ec)) problems.clear();
            if (own_data.empty() || !app::SetupScreen(display, window, own_data).run(problems)) {
                SDL_DestroyRenderer(renderer);
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 0;
            }
            data_dir = own_data.string();
        }
    }
    platform::GameData gd(data_dir);

    bool running = true;
    try {
        app::GameSession session; // the rand() state and tables that outlive a game
        {
            // Boot (RE: 1.06 FUN_00442580, docs/re/01-boot-sequence.md): full
            // intro once, then the short title on every later launch. The
            // flag is cleared as the intro starts and saved.
            std::string menu = settings.play_full_intro ? "MVE_INTRO1" : "MVE_INTRO3";
            if (settings.play_full_intro) {
                settings.play_full_intro = false;
                settings.save(settings_file);
            }
            // FUN_00442580 alternates the front end (gGameState 1) and the
            // game (gGameState 2).
            app::MenuRunner menus(display, gd);
            menus.load_net_settings(settings);
            menus.set_network(&session.net); // the lobby pages fill it; the game screen plays it
            // The exe writes dune2000.cfg (FUN_00478fa0) only once, when the
            // program ends (the UI manager's destructor FUN_00470b90, 0x470ba0,
            // from FUN_0044a870 at 0x44a9c6). We also write at each game
            // start, so a crash in a game does not lose the menu settings; the
            // file's content is the same.
            auto save_net = [&] {
                menus.store_net_settings(settings);
                settings.save(settings_file);
            };
            bool jump = false;
            while (menus.run(menu, jump) == app::MenuRunner::Result::StartGame) {
                save_net();
                app::GameScreen::Result result;
                // A full save chosen in a LOAD menu replaces the normal mission
                // start (gLoadSavedGame 0x5178bc, docs/re/27 section 5).
                std::optional<app::SaveGame> load = menus.take_pending_load();
                do { // RestartGame: the same mission again, no briefing (docs/re/20 section 2.4)
                    app::GameScreen game(display, gd, load ? load->mission : menus.mission(), menus, settings,
                                         settings_file, session);
                    if (load) game.load_saved(*load);
                    result = game.run();
                    menus.set_score_stats(game.score_stats());
                    load = result == app::GameScreen::Result::Load ? menus.take_pending_load() : std::nullopt;
                    session.restart = result == app::GameScreen::Result::Restart;
                } while (result == app::GameScreen::Result::Restart ||
                         (result == app::GameScreen::Result::Load && load));
                if (result == app::GameScreen::Result::Quit) break;
                if (menus.global("gGameType") == 2) {
                    // A network game (docs/re/58 section 3.7): its end -> SCORE
                    // ("MULTIPLAYER RESULTS", Continue -> IPXLOBBY); Abort, a
                    // failed start -> IPXLOBBY without SCORE.
                    const bool ended = result == app::GameScreen::Result::Won || result == app::GameScreen::Result::Lost;
                    menus.network_game_over();
                    menu = menus.take_wignet_exit() ? "MAINMENU" : ended ? "SCORE" : "ABORT_MP_GAME";
                    jump = true;
                    continue;
                }
                if (result == app::GameScreen::Result::LoadMission) {
                    menu = "LOADMISSION"; // QuitAndJumpToMenu LOADMISSION: the globe at the saved mission
                    jump = true;
                    continue;
                }
                // Doc 19 section 4.5: SCORE (then the campaign map) after a
                // win, MVE_LOSE (then the map, same mission) after a loss.
                if (result == app::GameScreen::Result::Won || result == app::GameScreen::Result::Lost) {
                    menu = menus.after_mission(result == app::GameScreen::Result::Won);
                    jump = true;
                } else {
                    // Abort (docs/re/20 section 2.3): campaign -> MAINMENU; skirmish ->
                    // MAINMENU, MULTIPLAYERTYPE, SCORE (docs/re/51 section 6).
                    const bool sk = menus.global("gGameType") == 1;
                    menu = sk ? "ABORT_MP_GAME" : "MAINMENU";
                    jump = sk;
                }
            }
            save_net();
            running = false;
        }
    } catch (const std::exception& e) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "d2k", e.what(), window);
    }

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT ||
                (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
