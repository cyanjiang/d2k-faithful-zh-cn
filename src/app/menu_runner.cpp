#include "app/display_mode.hpp"
#include "app/menu_runner.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "audio/channels.hpp"
#include "formats/file_io.hpp"
#include "formats/pe_image.hpp"
#include "formats/r8.hpp"
#include "formats/string_table.hpp"
#include "app/user_dirs.hpp"
#include <filesystem>
#include "ui/menu_hooks.hpp"
#include "ui/render.hpp"
#include "video/movie_player.hpp"

namespace d2k::app {

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 400; // screen size globals 0x4eb028 / 0x4eb02c
constexpr size_t kMenuSlots = 20;               // CUIManager menu slots
constexpr ui::Rect kFullRect{0, 0, 640, 400};   // transition rect for FADE / opening
constexpr int kMenuMusicVolume = 100;           // FUN_004700e0, outside the main loop
// Loop rate: 60 Hz by decision (CLAUDE.md section 10); the original was flip-bound. It sets the TWEEN
// speed; FADE steps are additionally gated at 12 ms.
constexpr uint64_t kFrameNs = 1'000'000'000 / 60;

// Index of the first action line in an element body (layout per type).
size_t first_action(const ui::Element& e) {
    switch (e.type) {
    case ui::ElementType::Button: return 11;
    case ui::ElementType::Movie: return 2;
    case ui::ElementType::CheckBox: return 7;
    case ui::ElementType::ValueBar: return 11; // after "min max step default"
    case ui::ElementType::ListBox: return 16;  // after the 16 layout lines (docs/re/27 section 3.1)
    case ui::ElementType::EditBox: return 5;   // pos, size, back, outline, text colour
    case ui::ElementType::TextBox: return 7;   // pos, size, back, hi, lo, font, text key
    case ui::ElementType::MultiStateBox: return 6; // pos, size, border, count, colours, gfx (docs/re/55 1.1)
    default: return e.body.size();
    }
}

std::vector<std::string> actions_of(const ui::Element& e) {
    return {e.body.begin() + std::ptrdiff_t(std::min(first_action(e), e.body.size())), e.body.end()};
}

bool is_navigation(const std::string& cmd) {
    static const char* const kNav[] = {"QuitMenu",   "BackMenuAndJumpTo", "BackMenu",
                                       "GoBackMenu", "GoBackMenuAndJumpTo", "JumpToMenu",
                                       "QuitAndJumpToMenu", "RemoveMenu", "BackTo",
                                       "BackToAndJumpTo"};
    return std::find(std::begin(kNav), std::end(kNav), cmd) != std::end(kNav);
}

// Commands flagged "init" in the exe's command table (0x4beb88): run once
// when the element is created (FUN_00484000), never on click.
bool is_init_command(const std::string& cmd) {
    static const char* const kInit[] = {
        "RedrawEachTime", "GrabBgdEachTime", "EnterText", "SetOnIf", "SetOffIf", "GetComPorts",
        "LoadPhoneInfo", "LoadUserNames", "GetMapList", "StaySelected", "EnterLobby",
        "GetLobbyList", "Check", "NoSpaces", "NoLetters", "NoNumbers", "NoOtherChars",
        "MaxCharacters", "NoHighlight", "EnterActions", "GetGameSaves", "GetCDTracks",
        "GetToggleStatus", "LoadGfxIfValueU1Is"};
    if (std::find(std::begin(kInit), std::end(kInit), cmd) != std::end(kInit)) return true;
    // GetValue*Init, GetValue*MaxInit, GetStringInit, CheckIfValue*Init,
    // GetStringIfValue*Init.
    return cmd.size() > 4 && cmd.compare(cmd.size() - 4, 4, "Init") == 0;
}

// Values as the exe compares them: enum constants (table at 0x4ca820),
// TRUE/FALSE, or numbers.
long value_of(const std::string& v) {
    static const std::map<std::string, long> kEnums = {
        {"GameStateIntro", 0},   {"GameStateBriefing", 1}, {"GameStateMainLoop", 2},
        {"GameStateLoad", 3},    {"GameStateSave", 4},     {"GameStateQuit", 5},
        {"GameTypeCampaign", 0}, {"GameTypeSkirmish", 1},  {"GameTypeNetwork", 2},
        {"GameTypeSerial", 3},   {"GameTypeModem", 4},     {"GameTypeInternet", 5},
        {"TRUE", 1},             {"FALSE", 0},
    };
    auto it = kEnums.find(v);
    return it != kEnums.end() ? it->second : std::atol(v.c_str());
}

// SCORE and SCORE_SINGLE: the score menus (menu +0x30, FUN_004359a0 0x436169).
bool is_score_menu(const std::string& n) { return n == "SCORE" || n == "SCORE_SINGLE"; }

} // namespace

std::vector<std::string> MenuRunner::element_actions(const ui::Element& e) { return actions_of(e); }

MenuRunner::MenuRunner(render::Display& display, const platform::GameData& gd)
    : display_(display), gd_(gd), assets_(gd), music_(gd), speech_(gd), sfx_(gd), screen_(kWidth, kHeight) {
    campaign_ = campaign::CampaignTable::parse(
        formats::read_file(gd_.find("Data/UI_DATA/campaign.uib").value()));
    regions_ = campaign::load_regions(formats::PeImage(formats::read_file(gd_.find("DUNE2000.DAT").value())));
    texture_ = SDL_CreateTexture(display_.renderer(), SDL_PIXELFORMAT_XRGB1555, SDL_TEXTUREACCESS_STREAMING,
                                 kWidth, kHeight);
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
    if (auto m = gd_.find("Data/MOUSE.R16")) {
        cursors_ = std::make_unique<render::SpriteBank>(formats::parse_r8(formats::read_file(*m)),
                                                        std::span<const uint8_t>{});
    }
    // Initial values from the exe's data section (1.06 addresses).
    globals_ = {
        // 0x4dfb08: 0 in the data section, but the menus run inside the boot
        // loop "while (gGameState == 1)" (FUN_00442580); confirmed by the
        // reference game stopping the music when a pop-up opens.
        {"gGameState", "1"},
        {"gGameType", "0"},           // 0x797e34
        {"gDifficultyLevel", "1"},    // 0x4e8bf0
        {"gBitsPerPixel", "16"},      // 0x4eb018
        {"gGameBitsPerPixel", "16"},  // 0x4eb01c
        {"gMissionNumber", "1"},      // 0x4de610
        {"gUseCampaignSelection", "1"}, // 0x4e7ad0
        {"gSideId", "0"},             // 0x798544
    };
    // Of these, dune2000.cfg (FUN_00478c20) holds only gGameBitsPerPixel
    // (0x4eb01c, 8 or 16); we render 16-bit only (CLAUDE.md section 10). The
    // gNet* settings it also holds come from our settings file
    // (load_net_settings).
}

MenuRunner::~MenuRunner() {
    if (texture_) SDL_DestroyTexture(texture_);
}

void MenuRunner::set_input(bool on) {
    if (on == input_on_) return;
    input_on_ = on;
    input_delay_ = on ? 10 : 0;
}

bool MenuRunner::input_allowed() {
    if (input_delay_ > 0) {
        --input_delay_;
        return false;
    }
    return input_on_;
}

void MenuRunner::load_net_settings(const platform::Settings& s) {
    globals_["gNetUnitCount"] = std::to_string(s.net_unit_count);
    globals_["gNetTechLevel"] = std::to_string(s.net_tech_level);
    globals_["gNetStartingCredits"] = std::to_string(s.net_credits);
    globals_["gNetAIPlayers"] = std::to_string(s.net_ai_players);
    globals_["gNetCrates"] = std::to_string(s.net_crates);
    globals_["gNetWorms"] = std::to_string(s.net_worms);
    globals_["gNetPlayerSide"] = std::to_string(s.net_side);
    globals_["gNetPlayerColour"] = std::to_string(s.net_colour);
    // FUN_00470710: the name starts as text NONAME ("NONAME") until one is saved.
    globals_["gNetPlayerName"] = s.net_name.empty() ? assets_.lookup_text("NONAME") : s.net_name;
    globals_["gNetPlayerHandicap"] = "1"; // FUN_00470710 0x47082e (not saved)
    net_port_ = s.net_port;
    game_speed_ = s.game_speed;
    net_addresses_.clear();
    std::istringstream list(s.net_addresses);
    for (std::string a; std::getline(list, a, ',');) {
        if (!a.empty()) net_addresses_.push_back(a);
    }
}

void MenuRunner::store_net_settings(platform::Settings& s) const {
    s.net_unit_count = int(global_value("gNetUnitCount"));
    s.net_tech_level = int(global_value("gNetTechLevel"));
    s.net_credits = int(global_value("gNetStartingCredits"));
    s.net_ai_players = int(global_value("gNetAIPlayers"));
    s.net_crates = int(global_value("gNetCrates"));
    s.net_worms = int(global_value("gNetWorms"));
    s.net_side = int(global_value("gNetPlayerSide"));
    s.net_colour = int(global_value("gNetPlayerColour"));
    auto it = globals_.find("gNetPlayerName");
    s.net_name = it == globals_.end() ? std::string() : it->second;
    s.net_addresses.clear();
    for (const auto& a : net_addresses_) s.net_addresses += (s.net_addresses.empty() ? "" : ",") + a;
}

long MenuRunner::global_value(const std::string& name) const {
    auto it = globals_.find(name);
    return it == globals_.end() ? 0 : value_of(it->second);
}

void MenuRunner::open_menu(const std::string& name) {
    if (stack_.size() >= kMenuSlots) return; // no free slot: the exe opens nothing
    auto info = assets_.menu_info(name);
    auto script = assets_.menu_script(name);
    if (name == "PRECAP" || name == "RECAP") {
        // FUN_004359a0: per-mission scripts L<house><n>MIS (briefing book)
        // and IG<house><n>MIS (in-game recap); menu index -1, so they open
        // with FADE (0) and close with FADE (1).
        static const char kHouse[] = {'A', 'H', 'O'};
        long side = global_value("gSideId");
        std::string file = std::string(name == "PRECAP" ? "L" : "IG") + kHouse[side >= 0 && side < 3 ? side : 0] +
                           std::to_string(global_value("gMissionNumber")) + "MIS";
        script = gd_.find("Data/UI_DATA/" + file + ".uil");
        info = ui::MenuInfo{file, 0, 1};
    }
    if (!info || !script) {
        SDL_Log("menu %s has no script", name.c_str());
        return;
    }
    SDL_Log("open %s", name.c_str());
    cursor_type_ = 0; // FUN_004359a0
    auto m = std::make_unique<Menu>();
    m->name = name;
    m->info = *info;
    // FUN_00491330 picks .tga or .bmp by gGameState when the element loads
    // its image, i.e. now.
    m->main_loop = global_value("gGameState") == 2;
    assets_.set_main_loop(m->main_loop);
    m->screen = ui::parse_screen(formats::parse_uil_lines(formats::read_file(*script)));
    ui::apply_menu_hooks(name, m->screen, ui::HookContext{});
    ui::apply_image_sizes(m->screen, assets_);
    ui::split_textboxes(m->screen, assets_);

    // Bounding box: starts as (640,400)-(0,0) and grows with each element.
    int l = 640, t = 400, r = 0, b = 0;
    const uint64_t now = SDL_GetTicks();
    m->anims.assign(m->screen.elements.size(), {});
    m->frames.assign(m->screen.elements.size(), 0);
    m->states.assign(m->screen.elements.size(), {});
    // Before the init commands, which may select list rows and set box texts.
    m->dirty.assign(m->screen.elements.size(), char(1));
    m->drawn_key.assign(m->screen.elements.size(), -1);
    m->saved_under.resize(m->screen.elements.size());
    for (size_t i = 0; i < m->screen.elements.size(); ++i) {
        const auto& e = m->screen.elements[i];
        if (e.type == ui::ElementType::ValueBar && e.body.size() > 10) {
            // "min max step default" (FUN_00438ad0); the default is clamped.
            auto& vs = m->states[i];
            std::istringstream vl(e.body[10]);
            vl >> vs.vmin >> vs.vmax >> vs.step >> vs.value;
            if (vs.step <= 0) vs.step = 1;
            vs.value = std::clamp(vs.value, vs.vmin, vs.vmax);
        }
        if (e.type == ui::ElementType::ListBox) {
            // FUN_00486440: an empty-slot list starts with the NULL item 0.
            auto& ls = m->states[i];
            ls.empty_slot = e.header.rfind("LISTBOXEMPTYSLOT", 0) == 0;
            if (ls.empty_slot) {
                ls.items = {""};
                ls.item_white = {false};
                ls.item_data = {-1};
            }
        }
        if (e.type == ui::ElementType::EditBox) m->states[i].edit_focus = e.header.rfind("EDITBOXFOCUS", 0) == 0;
        run_init_commands(*m, i, now);
        if (e.type == ui::ElementType::Background) {
            // AddBackground (FUN_00437b60): an image (not "NONE") grows the
            // box to (0, 0) .. (w - 1, h - 1), one less than the other
            // elements' x + w / y + h.
            const auto* img = e.name.compare(0, 4, "NONE") == 0 ? nullptr : assets_.image(e.name);
            if (img) {
                l = std::min(l, 0);
                t = std::min(t, 0);
                r = std::max(r, int(img->width) - 1);
                b = std::max(b, int(img->height) - 1);
            }
            continue;
        }
        auto rect = ui::element_rect(e);
        if (!rect) continue;
        // Every CInterface::Add* (button, toggle button, group box, list,
        // checkbox, multistate box, edit box, text box, value bar,
        // animation, movie: FUN_00437ce0 .. FUN_00438d10) grows the box by
        // the element's (x, y) .. (x + w, y + h).
        l = std::min(l, rect->x);
        t = std::min(t, rect->y);
        r = std::max(r, rect->x + rect->w);
        b = std::max(b, rect->y + rect->h);

        // CAnimation (FUN_0048cec0): "gfx fps"; starts stopped unless a
        // "GetValueECInit Anim..." line sets a state.
        if (e.type != ui::ElementType::Animation || e.body.size() < 3 || rect->w <= 0) continue;
        std::istringstream ss(e.body[2]);
        std::string gfx;
        int fps = 0;
        ss >> gfx >> fps;
        auto& p = m->anims[i];
        if (const auto* img = assets_.image(gfx)) p.frames = std::max(1, int(img->width) / rect->w);
        p.period_ms = fps > 0 ? uint32_t(1000 / fps) : 1000;
        p.last_ms = now;
        for (size_t j = 3; j < e.body.size(); ++j) {
            std::istringstream ls(e.body[j]);
            std::string cmd, state;
            ls >> cmd >> state;
            if (cmd == "GetValueECInit") p.set_mode(ui::anim_mode_from_name(state), now);
        }
    }
    m->bounds = {l, t, r - l, b - t};
    if (name == "IPXLOBBY" || name == "HOSTHEADTOHEAD" || name == "CLIENTMULTIPLAYER") lobby_page_opened(*m);
    if (name == "IPXLOBBY" && globals_["gNetPlayerName"] != assets_.lookup_text("NoName")) {
        // FUN_004359a0 0x436fa9: a name is set, so the focus starts in `chat`.
        const int ne = find_element(*m, "nameEdit"), ce = find_element(*m, "chat");
        if (ne >= 0 && ce >= 0) {
            m->states[size_t(ne)].edit_focus = false;
            m->states[size_t(ce)].edit_focus = true;
        }
    }

    if (name == "GLOBE") {
        // FUN_004359a0 -> CreateGlobe (FUN_0042bc60). The globe edits the
        // menu's background image, so keep a copy of it.
        for (const auto& e : m->screen.elements) {
            if (e.type != ui::ElementType::Background) continue;
            if (const auto* img = assets_.image(e.name)) {
                ui::Surface bg(kWidth, kHeight);
                bg.blit(*img, {0, 0, int(img->width), int(img->height)}, 0, 0, ui::BlitMode::Opaque);
                m->background = std::move(bg);
            }
            break;
        }
        if (!m->background) m->background = ui::Surface(kWidth, kHeight);
        m->globe = std::make_unique<ui::Globe>(assets_, campaign_, regions_, int(global_value("gSideId")),
                                               int(global_value("gMissionNumber")));
    }

    // FUN_004359a0 0x436169: SCORE and SCORE_SINGLE are score menus (+0x30);
    // the game type, not the name, picks the campaign or the multiplayer score.
    if (is_score_menu(name)) {
        if (global_value("gGameType") == 0) score_setup(*m);
        else score_mp_setup(*m);
    }
    if (name == "CREDITS") { // FUN_004359a0: +0x34 = 1, CreateCredits (FUN_0043d2a0)
        set_input(false);
        m->credits = std::make_unique<ui::Credits>(assets_.text(), SDL_GetTicks());
    }

    // End of FUN_004359a0: an opening menu selects its entering transition
    // over the whole 640x400 area, marks it pending (+0x11d = 1) and
    // switches input off (FUN_004706c0(0) at 0x43644a); the next transition
    // step switches it on again (doc 04, "Mouse input switch").
    transition_.select(m->info.enter, kFullRect, true);
    set_input(false);
    stack_.push_back(std::move(m));
    hovered_ = pressed_ = -1;
    top_changed();
}

void MenuRunner::close_top() {
    if (stack_.empty()) return;
    // FUN_00437040: the leaving transition, over the menu's own box unless FADE.
    const Menu& m = *stack_.back();
    if (m.credits) set_input(true); // FUN_0043d4c0, from the menu destroy
    int mode = m.info.leave;
    bool fade = mode == ui::Transition::FadeIn || mode == ui::Transition::FadeOut;
    transition_.select(mode, fade ? kFullRect : m.bounds, false);
    set_input(false); // FUN_00437040: +0x11d = 1, FUN_004706c0(0), after the destroy above
    stack_.pop_back();
    hovered_ = pressed_ = -1;
    closed_ = true;
    // FUN_00473130 flags every remaining menu for a full redraw.
    for (auto& rest : stack_) invalidate(*rest);
    if (!stack_.empty()) top_changed();
}

// FUN_004734e0 / FUN_00473130: when a menu becomes the top.
void MenuRunner::top_changed() {
    const std::string& name = stack_.back()->name;
    if (name == "MAINMENU") {
        music_.play("options.aud");
        music_.set_volume(kMenuMusicVolume);
        eye_timer_ = logo_timer_ = SDL_GetTicks();
    } else if (name.rfind("SCORE", 0) == 0) {
        music_.play("score.aud");
        music_.set_volume(kMenuMusicVolume);
    } else if (global_value("gGameState") == 1) {
        music_.stop();
    }
}

void MenuRunner::process_navigation() {
    if (!nav_) return;
    const NavCommand c = *nav_;
    nav_.reset();
    const std::string& a = c.arg1;
    const long game_type = global_value("gGameType");
    const long mission = global_value("gMissionNumber");
    auto close_all = [&] {
        while (!stack_.empty()) close_top();
    };
    auto in_stack = [&](const std::string& n) {
        return std::any_of(stack_.begin(), stack_.end(), [&](const auto& m) { return m->name == n; });
    };
    auto close_until = [&](const std::string& n) {
        while (!stack_.empty() && stack_.back()->name != n) close_top();
    };
    auto campaign_screen = [&] { return global_value("gUseCampaignSelection") != 0 ? "GLOBE" : "LOADMISSION"; };

    if (c.cmd == "QuitMenu") {
        close_all();
    } else if (c.cmd == "BackMenuAndJumpTo") {
        close_top();
        if (a == "LOBBY_SCREEN" && game_type == 1) {
            open_menu("SKIRMISH"); // FUN_004738b0 case 0xDC: back to the skirmish setup
        } else if (a == "LOBBY_SCREEN" && game_type == 2) {
            // Case 0xDC, LAN: CreateWigNet, gNetNetworkType 4, IPXLOBBY.
            open_network();
            globals_["gNetNetworkType"] = "4";
            open_menu("IPXLOBBY");
        } else if (a == "LOBBY_SCREEN") {
            SDL_Log("LOBBY_SCREEN: game type %ld has no lobby here", game_type);
        } else {
            open_menu(a);
        }
    } else if (c.cmd == "BackMenu") {
        close_top();
    } else if (c.cmd == "GoBackMenu" || c.cmd == "GoBackMenuAndJumpTo") {
        for (int i = 0; i < std::atoi(a.c_str()); ++i) close_top();
        if (c.cmd == "GoBackMenuAndJumpTo") open_menu(c.arg2);
    } else if (c.cmd == "JumpToMenu") {
        if (a.empty()) return;
        std::string target = a;
        if (a == "CONFIRM_ABORT_MP" && (game_type == 3 || game_type == 4)) {
            target = "ABORT_CONNECTION";
        } else if (a == "LOAD") {
            // FUN_00492750: slot 0 must open, else NO_SAVED_GAMES.
            if (!read_save_header(save_path(int(game_type), 0))) target = "NO_SAVED_GAMES";
        }
        if (target == "ERROR_MESSAGE" && in_stack("ERROR_MESSAGE")) return;
        open_menu(target);
    } else if (c.cmd == "QuitAndJumpToMenu") {
        close_all();
        if (a == "ABORT_MP_GAME" && ingame_) {
            // docs/re/20 section 2.3: gGameState 1 ends the game loop; the
            // front end then opens MAINMENU (campaign; mission unchanged).
            ingame_event_ = IngameEvent::Abort;
        } else if (a == "ABORT_MP_GAME" && game_type == 1) {
            // A skirmish abort (docs/re/51 section 6, 0x473e0d / 0x473e19).
            open_menu("MAINMENU");
            open_menu("MULTIPLAYERTYPE");
            open_menu("SCORE");
        } else if (a == "ABORT_MP_GAME" && game_type == 2) {
            // Case 0xE1, LAN (docs/re/58 section 3.7): no SCORE; MAINMENU,
            // MULTIPLAYERTYPE, CreateWigNet, gNetNetworkType 4, IPXLOBBY.
            open_menu("MAINMENU");
            open_menu("MULTIPLAYERTYPE");
            open_network();
            globals_["gNetNetworkType"] = "4";
            open_menu("IPXLOBBY");
        } else if (a == "ABORT_MP_GAME") {
            open_menu("MAINMENU");
            SDL_Log("ABORT_MP_GAME: game type %ld has no lobby here", game_type);
        } else if (a == "LOADMISSION" && ingame_ && game_type == 1) {
            // REPLAY_MISSION's No: the game ends; the front end then runs
            // this jump (MAINMENU, SCORE).
            ingame_event_ = IngameEvent::Lost;
        } else if (a == "SCORE") {
            open_menu("MAINMENU");
            if (game_type == 0) {
                open_menu(mission < 10 ? campaign_screen() : "CREDITS");
                open_menu("SCORE_SINGLE");
            } else {
                if (game_type == 5) open_menu("MULTIPLAYERTYPE");
                open_menu(a);
            }
        } else if (a == "LOADMISSION") {
            open_menu("MAINMENU");
            if (game_type == 0) {
                if (mission < 10) open_menu(campaign_screen());
            } else if (game_type == 1) {
                open_menu("SCORE");
            }
        } else if (a == "HOUSESELECTION") {
            open_menu("MAINMENU");
            open_menu(a);
        } else {
            open_menu(a);
        }
    } else if (c.cmd == "RemoveMenu") {
        if (global_value("gGameState") == 1) close_top();
    } else if (c.cmd == "BackTo") {
        if (!in_stack(a)) {
            SDL_Log("BackTo error: %s", a.c_str());
            return;
        }
        close_until(a);
    } else if (c.cmd == "BackToAndJumpTo") {
        if (in_stack(a)) close_until(a);
        open_menu(c.arg2);
    }
    // FUN_004738b0: a movie screen as the target switches input off.
    const std::string& target =
        (c.cmd == "GoBackMenuAndJumpTo" || c.cmd == "BackToAndJumpTo") ? c.arg2 : c.arg1;
    if (target.rfind("MVE_", 0) == 0) set_input(false);
    start_transition();
}

void MenuRunner::start_transition() {
    // FUN_00472730: unless SKIP, "old" (+0x114) = a copy of the screen
    // passed (always DAT_005178b4, the menu back buffer: callers 0x4426fc,
    // 0x470e84, 0x47420b) and "new" (+0x118) = another copy of DAT_005178b4
    // with every menu of the stack drawn over it (FUN_0043ca50 +
    // FUN_0043c4b0 per menu); with SKIP the flagged menus draw on the next
    // update. So the new image starts from the current screen, not black;
    // it shows only where no menu of the stack draws (the bottom menus
    // have full-screen backgrounds).
    //
    // FUN_00472730 0x47273e: with gGameState (0x4dfb08) == 2, an 8-bit
    // screen (0x4eb018 == 8) or a missing snapshot surface, nothing is
    // started: +0x11d = 0, input on (FUN_004706c0(1)), +0x134 / +0x135 = 0.
    // So in a mission no menu fades or tweens (RECAP's FADE / FADE, LOAD /
    // SAVE's TWEEN): the menus are simply drawn on the next update, as the
    // capture of the original shows (docs/re/20 section 2.5).
    if (global_value("gGameState") == 2) {
        transition_ = ui::Transition{};
        set_input(true);
        return;
    }
    if (!transition_.animates()) return;
    set_input(false);
    ui::Surface fresh = screen_; // FUN_00472730: a copy of the current screen 0x5178b4, not black
    for (auto& m : stack_) invalidate(*m);
    draw_all_dirty(fresh);
    transition_.start(screen_, std::move(fresh));
}

bool MenuRunner::play_movie_if_any(Menu& m) {
    if (m.movie_played) return false;
    m.movie_played = true;
    for (const auto& e : m.screen.elements) {
        if (e.type != ui::ElementType::Movie) continue;
        auto file = assets_.text().get(e.name);
        auto path = file ? gd_.find("MOVIES/" + *file + ".VQA") : std::nullopt;
        // FUN_0048d730: the movie stops the music and shuts Miles down;
        // FUN_0048d880 starts it again with no track (name cleared) and a
        // new sample manager (empty sample cache). Nothing resumes the old
        // track; only a later top-menu change starts menu music (doc 15 §3).
        music_.stop();
        speech_.stop(); // AIL_shutdown (FUN_0046f490) ends every sample
        sfx_.stop();
        SDL_Log("movie %s start", e.name.c_str());
        auto result = !path        ? video::MovieResult::Finished
                      : movie_hook_ ? movie_hook_(*path)
                                    : video::play_movie(display_, *path);
        SDL_Log("movie %s end (%d)", e.name.c_str(), int(result));
        if (result == video::MovieResult::Quit) {
            quit_ = window_closed_ = true;
            return true;
        }
        audio::sample_cache().reset(); // FUN_0048d880 -> FUN_0046f450 -> FUN_0046eee0
        // FUN_0048d8f0: after FUN_00403460 returns, finished or skipped,
        // input goes on (FUN_004706c0(1)) and FUN_00482850 runs the
        // movie's commands (navigation is only queued: the next update
        // runs it).
        set_input(true);
        execute(actions_of(e));
        return true;
    }
    return false;
}

void MenuRunner::run_init_commands(Menu& m, size_t index, uint64_t /*now*/) {
    const auto& e = m.screen.elements[index];
    auto& es = m.states[index];
    for (const auto& line : actions_of(e)) {
        std::istringstream ss(line);
        std::string cmd, a, b;
        ss >> cmd >> a >> b;
        if (!is_init_command(cmd)) continue;
        if (cmd == "EnterActions") {
            es.enter_default = true; // Enter runs this element (doc 20 section 1.2)
        } else if (cmd == "MaxCharacters") {
            es.max_chars = std::atoi(a.c_str()); // GAMEMESSAGE: 64
        } else if (cmd == "NoSpaces" || cmd == "NoNumbers" || cmd == "NoLetters" || cmd == "NoOtherChars") {
            es.refused_chars |= cmd == "NoSpaces" ? 1 : cmd == "NoNumbers" ? 2 : cmd == "NoLetters" ? 4 : 8;
        } else if (cmd == "EnterLobby" || cmd == "GetLobbyList" || cmd == "NoHighlight") {
            lobby_init(m, index, cmd, a); // docs/re/58 (menu_lobby.cpp)
        } else if (cmd == "LoadGfxIfValueU1Is") {
            // FUN_00484000: the group box's image = arg 1 when global(arg 2) == arg 3.
            std::string value;
            ss >> value;
            if (e.type == ui::ElementType::GroupBox && global_value(b) == value_of(value) &&
                m.screen.elements[index].body.size() > 4) {
                m.screen.elements[index].body[4] = a;
            }
        } else if (cmd == "EnterText") {
            if (e.type == ui::ElementType::ListBox) {
                // FUN_00480e00: a list box adds the text as wrapped rows
                // (FUN_00486f10), e.g. the in-game briefing (RECAP).
                if (auto rect = ui::element_rect(e)) {
                    for (auto& row : ui::wrap_list_text(assets_.menu_font(), assets_.lookup_text(a), rect->w - 20)) {
                        es.items.push_back(std::move(row));
                        es.item_white.push_back(false);
                        es.item_data.push_back(0);
                    }
                }
            } else if (a.size() > 1 && a[0] == 'g' && std::isupper(static_cast<unsigned char>(a[1]))) {
                // FUN_00480e00 type 7: a name of a char global (FUN_0048e5b0)
                // shows the global's string (chosenMap: gNetMapDesc).
                auto it = globals_.find(a);
                es.literal = it == globals_.end() ? std::string() : it->second;
            } else {
                es.text = a;
            }
        } else if (cmd == "GetGameSaves") {
            get_game_saves(m, int(index));
        } else if (cmd == "GetCDTracks") {
            get_cd_tracks(m, int(index));
        } else if (cmd == "GetMapList") {
            get_map_list(m, int(index));
        } else if (cmd == "StaySelected") {
            // FUN_004910d0: a list with rows and no selection selects row 1.
            if (e.type == ui::ElementType::ListBox && es.selection == 0 && !es.items.empty()) {
                list_select(m, int(index), 1);
                m.states[index].first = 0;
            }
        } else if (cmd.rfind("GetStringIfValue", 0) == 0) {
            // 0x8F / 0x92: the box's text key = arg 3 when global(arg 1) == arg 2.
            std::string key;
            ss >> key;
            if (global_value(a) == value_of(b)) es.text = key;
        } else if (cmd == "GetStringInit") {
            // 0x79: an edit box starts with the string global.
            if (e.type == ui::ElementType::EditBox) {
                auto it = globals_.find(a);
                es.edit_text = it == globals_.end() ? std::string() : it->second;
                es.edit_cursor = int(es.edit_text.size());
            }
        } else if (cmd == "Check") {
            if (e.type == ui::ElementType::CheckBox) es.checked = true;
        } else if (cmd.rfind("CheckIfValue", 0) == 0) {
            // CheckIfValue*Init (FUN_0047f850 & co.): checked = (global == value).
            if (e.type == ui::ElementType::CheckBox) es.checked = global_value(a) == value_of(b);
            // A multistate box without a value takes the global as its state.
            // (FUN_0047faf0: the value argument is ignored there.)
            if (e.type == ui::ElementType::MultiStateBox) es.value = int(global_value(a));
        } else if (e.type == ui::ElementType::ValueBar && cmd.rfind("GetValue", 0) == 0) {
            // GetValueU1Init / U2Init (FUN_00484000 0x74 / 0x75): the bar's
            // value = the global, clamped to [min, max] and written back.
            es.value = std::clamp(int(global_value(a)), es.vmin, es.vmax);
            globals_[a] = std::to_string(es.value);
        }
        // GetValueECInit for animations is handled in open_menu; others are
        // not needed by the screens implemented so far.
    }
}

void MenuRunner::execute(const std::vector<std::string>& actions, Menu* m, int self) {
    for (const auto& line : actions) {
        std::istringstream ss(line);
        std::string cmd, a, b;
        ss >> cmd >> a >> b;
        if (cmd.empty() || is_init_command(cmd)) continue; // init commands are not stored
        if (is_navigation(cmd)) {
            nav_ = NavCommand{cmd, a, b}; // FUN_00473840: stored, run later
        } else if (lobby_command(cmd, a, b, m, self)) {
            // the network pages' commands (menu_lobby.cpp)
        } else if (cmd.rfind("SetValue", 0) == 0 && cmd.size() > 7 && cmd.compare(cmd.size() - 7, 7, "Special") == 0) {
            // SetValueU2Special / S2Special / S4Special (FUN_00483b80): the
            // element's current value into the global (its callback, if any,
            // is the game's business: the game screen reads the globals). A
            // list box gives selection - 1, and only when it has one.
            if (m && self >= 0) {
                const auto& st = m->states[size_t(self)];
                if (m->screen.elements[size_t(self)].type == ui::ElementType::ListBox) {
                    if (st.selection != 0) globals_[a] = std::to_string(st.selection - 1);
                } else {
                    globals_[a] = std::to_string(st.value);
                }
            }
        } else if (cmd == "ParseEndCommands") {
            // FUN_004749c0 runs the other elements' ...AtEnd setters; the
            // in-game pages have none.
        } else if (cmd == "RestartGame") {
            // FUN_0044c460: gRestartGame and QuitMenu; the game loop restarts
            // the mission in place (docs/re/20 section 2.4).
            ingame_event_ = IngameEvent::Restart;
            nav_ = NavCommand{"QuitMenu", "", ""};
        } else if (cmd == "PlaySample" && ingame_ && a.rfind("S_MISSION", 0) == 0) {
            // The mouse-up handler routes S_MISSION* through FUN_004762f0 (a
            // house voice in the game's speech queue).
            ingame_voice_ = a;
        } else if (cmd.rfind("SetValue", 0) == 0 && b == "TOGGLE") {
            // SetValueBFTo ... TOGGLE (0xB5, FUN_0047df90): flips the flag.
            globals_[a] = global_value(a) ? "0" : "1";
            // gNetCrates' setter callback (0x490240 -> FUN_004931c0) relabels
            // the `crates` box (docs/re/55 section 3).
            if (m && a == "gNetCrates") set_text_key(*m, "crates", global_value(a) ? "CratesOn" : "CratesOff");
        } else if (cmd == "SetFromUIValueU1To") {
            // 0xBB (FUN_0047e470): the global = the multistate box's state.
            if (m && self >= 0) {
                const long before = global_value(a);
                globals_[a] = std::to_string(m->states[size_t(self)].value);
                // gNetWorms' callback (0x490190 -> FUN_00493350) on a change.
                static const char* const kWorms[] = {"WormsOff", "WormsLow", "WormsMedium", "WormsHigh"};
                const long v = global_value(a);
                if (a == "gNetWorms" && v != before && v >= 0 && v < 4) set_text_key(*m, "worms", kWorms[v]);
            }
        } else if (cmd == "GetMapFromList") {
            if (m && self >= 0) get_map_from_list(*m, self);
        } else if (cmd == "SetStringInTextBox") {
            // 0x12 (FUN_00474540): the named text box in the top menu shows key `a`.
            if (!m) continue;
            set_text_key(*m, b, a);
        } else if (cmd == "StartGame") {
            // FUN_00479e80 -> FUN_004752c0 for gGameType 1 (docs/re/51 section
            // 5): gGameState 2, the map <gNetMap>.map with Data\maps\_prac.mis,
            // no check that a map is chosen; the front end then ends (the exe
            // opens LOADING, which the boot loop leaves at once on gGameState 2).
            // The argument (the list name) is ignored.
            globals_["gGameState"] = "2";
            globals_["gMissionMap"] = globals_["gNetMap"];
            globals_["gSideId"] = "0";
            nav_ = NavCommand{"QuitMenu", "", ""};
        } else if (cmd.rfind("SetValue", 0) == 0) {
            globals_[a] = b;
            if (a == "gGameState" && b == "GameStateQuit") quit_ = true;
        } else if (cmd.rfind("CheckIfValue", 0) == 0) {
            // FUN_00482850 cases 0x82..0x89: sets the checkbox from the value;
            // a mismatch stops the remaining commands.
            if (!m || self < 0 || m->screen.elements[size_t(self)].type != ui::ElementType::CheckBox) return;
            bool match = global_value(a) == value_of(b);
            m->states[size_t(self)].checked = match;
            if (!match) return;
        } else if (cmd == "UnCheck") {
            // FUN_00474250: by name in the top menu.
            if (!m) continue;
            for (size_t i = 0; i < m->screen.elements.size(); ++i) {
                if (m->screen.elements[i].name == a) m->states[i].checked = false;
            }
        } else if (cmd == "PlaySample") {
            // samples.uib name -> GAMESFX file (e.g. BUTTON -> button1).
            // FUN_004762f0 -> FUN_0046f210(id, 0x40, 100, channels 0-5, mode 0)
            // -> the sample cache (docs/re/59 section 1). Our front end has
            // the speech stream as channel 0 and one sample player as the
            // first free channel after it.
            if (auto file = assets_.sample_file(a)) {
                int id = -1;
                for (size_t i = 0;; ++i) {
                    const auto n = assets_.sample_name(i);
                    if (!n) break;
                    if (*n == a) {
                        id = int(i);
                        break;
                    }
                }
                const bool speaking = speech_.playing();
                const int channel = speaking ? 1 : 0;
                sfx_.stop();
                std::array<bool, audio::kChannels> playing{};
                playing[0] = speaking;
                auto& cache = audio::sample_cache();
                const int s = id < 0 ? -1 : cache.load(id, sfx_.in_sound_rs(*file), playing);
                if (s >= 0) {
                    cache.channel_slot[size_t(channel)] = s;
                    sfx_.play_file(*file);
                }
            }
        } else if (cmd == "Dune2000CdCheck") {
            // The original checks for the CD; our data folder is validated at start.
        } else if (cmd == "SetFromUIStringSpecial") {
            // 0xAE: the element's text into the named global.
            if (!m || self < 0) continue;
            const std::string& text = m->states[size_t(self)].edit_text;
            if (a.rfind("gHighScore", 0) == 0) {
                const int slot = std::atoi(a.c_str() + 10);
                if (slot >= 0 && slot < 6) hs_[size_t(slot)].name = text;
            } else {
                globals_[a] = text;
            }
        } else if (cmd == "CopyToEditBox") {
            // 0xCF (FUN_00475c10): the selected item's text into the edit box,
            // cursor at the end; selection 0 on an empty-slot list clears it.
            if (!m || self < 0) continue;
            const int ei = find_element(*m, a);
            if (ei < 0) continue;
            const auto& ls = m->states[size_t(self)];
            auto& es = m->states[size_t(ei)];
            es.edit_text = ls.selection > 0 && ls.selection - 1 < int(ls.items.size()) ? ls.items[size_t(ls.selection - 1)]
                                                                                    : std::string();
            es.edit_cursor = int(es.edit_text.size());
            edit_changed(*m, ei);
        } else if (cmd == "UICheckSaveGame") {
            check_save_game(a, b);
        } else if (cmd == "UISaveGame") {
            save_game(a, b, true);
        } else if (cmd == "ForceButtonDown") {
            // 0x1E: only from an edit box; the button shows pressed for 500 ms.
            if (!m || self < 0 || m->screen.elements[size_t(self)].type != ui::ElementType::EditBox) continue;
            forced_button_ = find_element(*m, a);
            forced_until_ = SDL_GetTicks() + 500;
            if (forced_button_ >= 0) m->dirty[size_t(forced_button_)] = 1;
        } else if (cmd == "ParseEditBox") {
            // 0xCE (FUN_00475b60): the edit box's commands, if it has text or
            // the top menu is SAVE.
            if (!m) continue;
            const int ei = find_element(*m, a);
            if (ei < 0) continue;
            if (!m->states[size_t(ei)].edit_text.empty() || m->name == "SAVE") {
                execute(actions_of(m->screen.elements[size_t(ei)]), m, ei);
            }
        } else if (cmd == "UILoadGame") {
            load_game(a);
        } else if (cmd == "JumpIfSelected") {
            // 0x19 (FUN_00480dc0): the menu only when the list has a selection.
            if (!m) continue;
            const int li = find_element(*m, a);
            if (li >= 0 && m->states[size_t(li)].selection != 0) nav_ = NavCommand{"JumpToMenu", b, ""};
        } else if (cmd == "DeleteGame") {
            delete_game(a);
        } else if (cmd == "SetValueU4SpecialFrom") {
            // Only the 4 names of the exe's U4 table (0x4c9ad0) are sources;
            // sound.uil's gLastCDTrack is not one, so nothing (docs/re/48 A.4).
        } else if (cmd == "StaySelectedSpecial") {
            // FUN_00491200: a list left without a selection selects item 1
            // and scrolls to the top.
            if (!m || self < 0) continue;
            auto& ls = m->states[size_t(self)];
            if (ls.selection == 0 && !ls.items.empty()) {
                list_select(*m, self, 1);
                m->states[size_t(self)].first = 0;
            }
        } else if (cmd == "PlayCDTrack") {
            // FUN_004764b0: file (selection - 1) of the WHOLE MUSIC listing,
            // though the list skips the files without a music_ key (OPTIONS,
            // SCORE): rows 9+ play the file two places earlier. Original, kept.
            if (!m) continue;
            const int li = find_element(*m, a);
            const int s = li >= 0 ? m->states[size_t(li)].selection : 0;
            if (s > 0 && size_t(s - 1) < cd_files_.size() && music_play_) music_play_(cd_files_[size_t(s - 1)]);
        } else if (cmd == "StopCDTrack") {
            if (music_stop_) music_stop_(); // FUN_00476470: silent until Play, a load or the next mission
        } else if (cmd == "ResetToggleButton") {
            // FUN_00474..: only a toggle of that name; sound.uil's repeat toggle
            // is never created (its header line is missing), so nothing.
        } else {
            SDL_Log("UI action not implemented: %s", line.c_str());
        }
    }
}

void MenuRunner::handle_input() {
    Menu& top = *stack_.back();
    hovered_ = -1;
    for (int i = int(top.screen.elements.size()) - 1; i >= 0; --i) {
        const auto& e = top.screen.elements[size_t(i)];
        auto r = ui::element_rect(e);
        bool clickable = e.type == ui::ElementType::Button || e.type == ui::ElementType::CheckBox ||
                         e.type == ui::ElementType::MultiStateBox ||
                         e.type == ui::ElementType::ValueBar || e.type == ui::ElementType::ListBox;
        if (clickable && r && r->contains(int(std::floor(mouse_x_)), int(std::floor(mouse_y_)))) {
            hovered_ = i;
            break;
        }
    }
    // The mouse handlers' input-switch test ran before the UI update
    // (gate_mouse): a refused press or release is already gone here.
    const bool right_list = right_list_;
    right_list_ = false;
    if (mouse_down_) pressed_ = hovered_;
    if (mouse_down_ && pressed_ >= 0 && !mouse_double_ &&
        (top.screen.elements[size_t(pressed_)].type == ui::ElementType::CheckBox ||
         top.screen.elements[size_t(pressed_)].type == ui::ElementType::MultiStateBox ||
         top.screen.elements[size_t(pressed_)].type == ui::ElementType::ValueBar) &&
        no_change_options(top, pressed_)) {
        pressed_ = -1; // a client's click on the host's options (docs/re/58 section 5.1)
    }
    if (mouse_down_ && mouse_double_ && pressed_ >= 0 &&
        top.screen.elements[size_t(pressed_)].type == ui::ElementType::ListBox) {
        // WM_LBUTTONDBLCLK on a list: its double-click commands (FUN_00484e50),
        // JoinSessionDbl = ClientWantsToJoin.
        for (const auto& l : actions_of(top.screen.elements[size_t(pressed_)])) {
            if (l.rfind("JoinSessionDbl", 0) == 0) join_game(top);
        }
        mouse_down_ = false;
        pressed_ = -1;
    }
    if (mouse_down_) {
        // FUN_00437840: a press in an edit box gives it the focus (cursor at
        // the end); every other edit box loses it.
        const int mx = int(std::floor(mouse_x_)), my = int(std::floor(mouse_y_));
        int hit = -1;
        for (size_t i = 0; i < top.screen.elements.size(); ++i) {
            const auto& e = top.screen.elements[i];
            const auto r = ui::element_rect(e);
            if (e.type == ui::ElementType::EditBox && r && r->contains(mx, my)) hit = int(i);
        }
        if (hit >= 0) {
            for (size_t i = 0; i < top.screen.elements.size(); ++i) {
                if (top.screen.elements[i].type != ui::ElementType::EditBox) continue;
                auto& st = top.states[i];
                const bool focus = int(i) == hit;
                if (focus && !st.edit_focus) st.edit_cursor = int(st.edit_text.size());
                if (st.edit_focus != focus) {
                    st.edit_focus = focus;
                    edit_changed(top, int(i));
                }
            }
        }
    }
    if (mouse_down_ && pressed_ >= 0 && !mouse_double_ &&
        (top.screen.elements[size_t(pressed_)].type == ui::ElementType::CheckBox ||
         top.screen.elements[size_t(pressed_)].type == ui::ElementType::MultiStateBox)) {
        // FUN_00437840 -> FUN_0048b0d0: checkboxes and multistate boxes act
        // on the left press (docs/re/55 section 1.4); a double-click's second
        // press (WM_LBUTTONDBLCLK) skips the UI handler.
        const int i = pressed_;
        const auto& e = top.screen.elements[size_t(i)];
        auto actions = actions_of(e);
        auto& es = top.states[size_t(i)];
        if (e.type == ui::ElementType::CheckBox) {
            // A checkbox with an UnCheck command (radio group) becomes
            // checked; otherwise a click toggles it.
            bool radio = std::any_of(actions.begin(), actions.end(),
                                     [](const std::string& l) { return l.rfind("UnCheck", 0) == 0; });
            es.checked = radio ? true : !es.checked;
        } else {
            const int count = e.body.size() > 3 ? std::atoi(e.body[3].c_str()) : 0;
            es.value = es.value + 1 < count ? es.value + 1 : 0; // 0 -> 1 -> ... -> count - 1 -> 0
        }
        top.dirty[size_t(i)] = 1;
        // Click sounds come only from PlaySample commands in the scripts.
        execute(actions, &top, i);
        pressed_ = -1;
    }
    mouse_double_ = false;
    // List boxes take their own presses, drags and releases.
    for (size_t i = 0; i < top.screen.elements.size(); ++i) {
        if (top.screen.elements[i].type != ui::ElementType::ListBox) continue;
        list_mouse(top, int(i), mouse_down_ && pressed_ == int(i), mouse_up_, mouse_held_);
        if (right_list && hovered_ == int(i)) list_select(top, int(i), 0); // right release clears it
    }
    // FUN_00471250 (window procedure 0x4a373a, right-button release): clears
    // 0x797590 whatever menu is on top.
    if (right_up_) outside_down_ = false;
    right_up_ = false;
    if (mouse_up_ && pressed_ >= 0 && top.screen.elements[size_t(pressed_)].type == ui::ElementType::ListBox) {
        pressed_ = -1;
        return;
    }
    if (mouse_down_ && pressed_ >= 0 && top.screen.elements[size_t(pressed_)].type == ui::ElementType::ValueBar) {
        valuebar_down(top, pressed_, SDL_GetTicks());
    } else if (mouse_held_ && !mouse_down_) {
        valuebar_held(top, SDL_GetTicks());
    }
    if (ingame_ && (top.name == "INGAME" || top.name == "INGAME_SKIRMISH" || top.name == "INGAME_MP" ||
                    top.name == "ABORT_VM")) {
        // FUN_004711a0: a left press outside the box (PtInRect, right and
        // bottom exclusive) sets 0x797590; a press inside leaves it as it is.
        // FUN_004710d0: a left release outside with the flag set queues
        // QuitMenu; the release never clears it. Only a right release
        // (above) and the OPTIONS button (0x444832 / 0x444c97,
        // ingame_clear_outside) clear it, so a stale flag survives a
        // press-outside / release-inside and even the menu's close.
        const bool outside = !top.bounds.contains(int(std::floor(mouse_x_)), int(std::floor(mouse_y_)));
        if (mouse_down_ && outside) outside_down_ = true;
        if (mouse_up_ && outside_down_ && outside) nav_ = NavCommand{"QuitMenu", "", ""};
    }
    if (mouse_down_ && top.globe) {
        // The button-down handler calls the globe click (FUN_0042cf10).
        if (auto mission = top.globe->click(int(std::floor(mouse_x_)), int(std::floor(mouse_y_)))) {
            pressed_ = -1;
            launch_mission(*mission);
            return;
        }
    }
    if (mouse_up_ && pressed_ >= 0 && top.screen.elements[size_t(pressed_)].type == ui::ElementType::ValueBar) {
        // FUN_00437600: the bar's commands run on release (the setters); the
        // thumb goes back to the value's place.
        bar_drag_ = bar_repeat_ = -1;
        top.states[size_t(pressed_)].drag_thumb_x = -1;
        bar_dirty(top, pressed_);
        execute(actions_of(top.screen.elements[size_t(pressed_)]), &top, pressed_);
        pressed_ = -1;
        return;
    }
    if (mouse_up_) {
        if (pressed_ >= 0 && pressed_ == hovered_) {
            const auto& e = top.screen.elements[size_t(pressed_)];
            // Click sounds come only from PlaySample commands in the scripts.
            // (Checkboxes and multistate boxes acted on the press.)
            if (e.type != ui::ElementType::CheckBox && e.type != ui::ElementType::MultiStateBox) {
                execute(actions_of(e), &top, pressed_);
            }
        }
        pressed_ = -1;
    }
}

void MenuRunner::update_animations(uint64_t now) {
    // UI manager FUN_004712c0: on MAINMENU the eyes play once every 300 s and
    // the logo once every 20 s.
    Menu& top = *stack_.back();
    if (top.name == "MAINMENU") {
        auto play_once = [&](const char* name) {
            for (size_t i = 0; i < top.screen.elements.size(); ++i) {
                if (top.screen.elements[i].name == name) top.anims[i].set_mode(ui::AnimMode::ForwardOnce, now);
            }
        };
        if (now - eye_timer_ > 300000) {
            play_once("l_eye");
            play_once("r_eye");
            eye_timer_ = now;
        }
        if (now - logo_timer_ > 20000) {
            play_once("duneLogo");
            logo_timer_ = now;
        }
    }
    // FUN_0043c4b0 (0x43c796..0x43c7cc): the top menu's animations always
    // step; a lower menu's animation steps only when FUN_0043d070 finds
    // that its rect meets no box (+0x40..+0x4c) of a menu above it
    // (IntersectRect), so an uncovered animation keeps playing under a
    // pop-up. (No transition runs here: update_animations is skipped then.)
    auto meets = [](const ui::Rect& a, const ui::Rect& b) {
        return std::max(a.x, b.x) < std::min(a.x + a.w, b.x + b.w) && std::max(a.y, b.y) < std::min(a.y + a.h, b.y + b.h);
    };
    for (size_t k = 0; k < stack_.size(); ++k) {
        Menu& m = *stack_[k];
        for (size_t i = 0; i < m.anims.size(); ++i) {
            if (k + 1 < stack_.size()) {
                const auto r = ui::element_rect(m.screen.elements[i]);
                if (!r) continue;
                bool covered = false;
                for (size_t j = k + 1; j < stack_.size() && !covered; ++j) covered = meets(*r, stack_[j]->bounds);
                if (covered) continue;
            }
            m.anims[i].update(now);
            m.frames[i] = m.anims[i].frame;
        }
    }
}

void MenuRunner::invalidate(Menu& m) {
    m.background_dirty = true;
    std::fill(m.dirty.begin(), m.dirty.end(), char(1));
}

bool MenuRunner::draw_dirty(Menu& m, ui::Surface& out, bool top) {
    bool drew = false;
    assets_.set_main_loop(m.main_loop); // the images this menu loaded when it opened
    if (m.background_dirty) {
        drew = true;
        ui::render_background(m.screen, assets_, out, m.background ? &*m.background : nullptr);
        m.background_dirty = false;
    }
    ui::RenderState st;
    st.anim_frames = &m.frames;
    st.states = &m.states;
    if (top) {
        st.hovered = hovered_;
        st.pressed = pressed_;
        if (forced_button_ >= 0 && SDL_GetTicks() < forced_until_) st.hovered = st.pressed = forced_button_;
    }
    for (size_t i : ui::draw_order(m.screen)) {
        const auto& e = m.screen.elements[i];
        // What the element looks like now; a change stands for the exe
        // setting the element's dirty flag on that state change.
        bool down = top && ((pressed_ == int(i) && hovered_ == int(i)) ||
                            (forced_button_ == int(i) && SDL_GetTicks() < forced_until_));
        long key = (long(m.frames[i]) << 2) | (m.states[i].checked ? 2 : 0) | (down ? 1 : 0);
        if (!m.dirty[i] && key == m.drawn_key[i]) continue;
        if (e.type == ui::ElementType::Animation) {
            // FUN_0048d650: grab what is under the animation once, restore it
            // before each frame.
            if (auto r = ui::element_rect(e)) {
                auto& under = m.saved_under[i];
                bool grab = !under;
                if (grab) under = ui::Surface(r->w, r->h);
                for (int y = 0; y < r->h; ++y) {
                    for (int x = 0; x < r->w; ++x) {
                        int sx = r->x + x, sy = r->y + y;
                        if (sx < 0 || sy < 0 || sx >= out.width() || sy >= out.height()) continue;
                        uint16_t& screen_px = out.pixels()[size_t(sy) * out.width() + sx];
                        uint16_t& saved_px = under->pixels()[size_t(y) * r->w + x];
                        if (grab) {
                            saved_px = screen_px;
                        } else {
                            screen_px = saved_px;
                        }
                    }
                }
            }
        }
        ui::render_element(m.screen, i, assets_, out, st);
        m.dirty[i] = 0;
        m.drawn_key[i] = key;
        drew = true;
    }
    if (top && drew && tip_.shown) {
        // FUN_0048bb10 for the `tooltip` box, drawn last (docs/re/55 4.6): a
        // Black fill, a 1 px Yellow frame, the text centred in font 0 with
        // the box's font colour at (h - glyph height) / 2.
        const auto black = assets_.colour("Black").value_or(formats::Rgb{0, 0, 0});
        const auto yellow = assets_.colour("Yellow").value_or(formats::Rgb{222, 99, 0});
        out.fill(tip_.rect, ui::to_rgb555(black));
        out.frame(tip_.rect, ui::to_rgb555(yellow), ui::to_rgb555(yellow));
        std::string colour = "JHighLightText";
        const int ti = find_element(m, "tooltip");
        if (ti >= 0 && m.screen.elements[size_t(ti)].body.size() > 5) {
            colour = ui::strip_comment(m.screen.elements[size_t(ti)].body[5]);
        }
        const auto& font = assets_.menu_font();
        const auto c = assets_.colour(colour).value_or(formats::Rgb{255, 255, 255});
        const int tw = ui::text_width(font, tip_.text);
        out.draw_text_solid(font, tip_.rect.x + (tip_.rect.w - tw) / 2,
                            tip_.rect.y + (tip_.rect.h - int(font.height)) / 2, tip_.text, ui::to_rgb555(c));
    }
    return drew;
}

void MenuRunner::update_tooltip(uint64_t now) {
    // FUN_004712c0's tooltip part (docs/re/55 section 4): the front end, a
    // set-up page on top, and the page has a `tooltip` text box.
    static const char* const kPages[] = {"HOSTINETGAME", "CLIENTINETGAME", "INETLOBBY", "IPXLOBBY", "HOSTHEADTOHEAD",
                                         "CLIENTMULTIPLAYER", "HOST_MP2", "JOIN_MP2", "SKIRMISH"};
    if (stack_.empty() || ingame_ || global_value("gGameState") != 1) return;
    Menu& m = *stack_.back();
    if (std::find(std::begin(kPages), std::end(kPages), m.name) == std::end(kPages)) return;
    if (find_element(m, "tooltip") < 0) return;
    // The hovered element: buttons, checkboxes, multistate boxes, value
    // bars, list boxes, edit boxes; a later hit overrides an earlier one, a
    // hit without a text.uib key clears it.
    const int mx = int(std::floor(mouse_x_)), my = int(std::floor(mouse_y_));
    std::string key;
    ui::Rect er{};
    static const ui::ElementType kClasses[] = {ui::ElementType::Button, ui::ElementType::CheckBox,
                                               ui::ElementType::MultiStateBox, ui::ElementType::ValueBar,
                                               ui::ElementType::ListBox, ui::ElementType::EditBox};
    for (auto t : kClasses) {
        for (const auto& e : m.screen.elements) {
            if (e.type != t) continue;
            auto r = ui::element_rect(e);
            if (!r || !r->contains(mx, my)) continue;
            const std::string k = "tooltip_" + e.name;
            if (assets_.text().get(k)) {
                key = k;
                er = *r;
            } else {
                key.clear();
            }
        }
    }
    auto same = [](const std::string& x, const std::string& y) { return SDL_strcasecmp(x.c_str(), y.c_str()) == 0; };
    if (!key.empty() && (tip_remembered_.empty() || !same(tip_remembered_, key))) tip_timer_ = now;
    const uint64_t dt = now > tip_timer_ ? now - tip_timer_ : tip_timer_ - now;
    if (!key.empty() && same(tip_remembered_, key) && dt > 600) {
        tip_.text = assets_.lookup_text(tip_remembered_);
        const int w = ui::text_width(assets_.menu_font(), tip_.text) + 10;
        int y = (er.y + er.y + er.h) / 2 < 200 ? my + 20 : my - 25;
        int x = std::max(mx, 0);
        y = std::max(y, 0);
        if (x + w > 640) x = 639 - w;
        if (y + 20 > 400) y = 379;
        tip_.rect = {x, y, w, 20};
        tip_.shown = true;
        invalidate(m); // FUN_0043ca50: the whole page, every loop while shown
        return;
    }
    if (!key.empty()) tip_remembered_ = key;
    if (tip_.shown) {
        tip_.shown = false;
        invalidate(m);
    }
}

void MenuRunner::draw_all_dirty(ui::Surface& out) {
    // FUN_004712c0: every menu, bottom to top; once a menu below the top has
    // drawn something, the menus above it redraw completely.
    bool redraw_above = false;
    for (size_t k = 0; k < stack_.size(); ++k) {
        bool top = k + 1 == stack_.size();
        if (redraw_above) invalidate(*stack_[k]);
        if (draw_dirty(*stack_[k], out, top) && !top) redraw_above = true;
    }
}

void MenuRunner::add_animation(Menu& m, const std::string& name, int x, int y, int w, int h,
                               const std::string& gfx, int fps, ui::AnimMode mode) {
    // FUN_00438c10: an ANIMATION element created from code.
    ui::Element e;
    e.type = ui::ElementType::Animation;
    e.header = "ANIMATION " + name;
    e.name = name;
    e.body = {std::to_string(x) + ", " + std::to_string(y), std::to_string(w) + ", " + std::to_string(h),
              gfx + " " + std::to_string(fps)};
    ui::AnimationPlayer p;
    if (const auto* img = assets_.image(gfx)) p.frames = std::max(1, int(img->width) / w);
    p.period_ms = uint32_t(1000 / fps);
    p.last_ms = SDL_GetTicks();
    p.set_mode(mode, p.last_ms);
    m.screen.elements.push_back(std::move(e));
    m.anims.push_back(p);
    m.frames.push_back(p.frame);
    m.states.emplace_back();
    m.dirty.push_back(1);
    m.drawn_key.push_back(-1);
    m.saved_under.emplace_back();
}

void MenuRunner::update_globe(Menu& m) {
    // Called from the menu draw (FUN_0043c4b0 -> FUN_0042c400) for the top
    // GLOBE menu when no transition runs, after its elements.
    auto f = m.globe->update(screen_, *m.background);
    if (m.globe->revealing()) set_input(false); // FUN_0042c9b0 while revealing
    if (f.finished) set_input(true);
    if (f.wait_for_speech) {
        // The exe busy-waits for the previous speech to end (no redraw).
        if (speech_.playing()) SDL_Log("globe: waiting for speech");
        while (speech_.playing() && !quit_) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (display_mode::handle_event(ev)) continue;
                if (ev.type == SDL_EVENT_QUIT) quit_ = true;
            }
            music_.service();
            SDL_Delay(5);
        }
    }
    if (f.speech) {
        SDL_Log("globe: speech %s", f.speech->c_str());
        if (auto file = assets_.sample_file(*f.speech)) speech_.play_file(*file);
    }
    // FUN_0042c400: when the reveal is over the menu redraws completely (its
    // background now holds the finished map), then the arrows appear.
    if (f.finished) invalidate(m);
    for (const auto& a : f.arrows) {
        add_animation(m, a.name, a.x, a.y, 72, 70, a.gfx, 14, ui::AnimMode::LoopForward);
    }
}

bool MenuRunner::set_campaign_mission(const std::string& mission) {
    // FUN_004752c0, campaign: gMissionNumber from the name ("a1v1" -> 1),
    // gSideId from its letter.
    if (mission.size() < 2) return false;
    const int number = std::atoi(mission.c_str() + 1);
    const char* side = nullptr;
    switch (mission[0]) {
    case 'a': case 'A': side = "0"; break;
    case 'h': case 'H': side = "1"; break;
    case 'o': case 'O': side = "2"; break;
    default: return false;
    }
    globals_["gMissionNumber"] = std::to_string(number);
    globals_["gMissionMap"] = mission;
    globals_["gSideId"] = side;
    return true;
}

void MenuRunner::launch_mission(const std::string& mission) {
    // FUN_004752c0, then the briefing movie MVE_<HOUSE><n>.
    if (!set_campaign_mission(mission)) return;
    static const char* const movies[] = {"MVE_ATREIDES", "MVE_HARKONNEN", "MVE_ORDOS"};
    nav_ = NavCommand{"JumpToMenu",
                      movies[global_value("gSideId")] + std::to_string(global_value("gMissionNumber")), ""};
    process_navigation();
}

void MenuRunner::present() {
    frame_.pixels() = screen_.pixels();
    // FUN_00492a70: the per-loop cursor draw asks the input switch, which
    // also counts the 10-pass delay down (FUN_004706f0).
    if (cursors_ && input_allowed() && mouse_x_ >= 0) {
        const size_t entry = size_t(cursor_type_ * 8 + cursor_frame_);
        if (entry < cursors_->size()) {
            const auto& img = cursors_->entry(entry);
            cursors_->draw_at(frame_, entry, int(std::floor(mouse_x_)) - 24 - img.offset_x,
                              int(std::floor(mouse_y_)) - 24 - img.offset_y, kFullRect);
        }
    }
    if (++cursor_counter_ % 3 == 0) cursor_frame_ = (cursor_frame_ + 1) & 7;
    SDL_UpdateTexture(texture_, nullptr, frame_.pixels().data(), kWidth * 2);
    display_.begin_frame();
    SDL_RenderTexture(display_.renderer(), texture_, nullptr, nullptr);
    display_.end_frame();
}

std::string MenuRunner::mission() const {
    auto it = globals_.find("gMissionMap");
    return it == globals_.end() ? std::string() : it->second;
}

std::string MenuRunner::after_mission(bool won) {
    const long mission = global_value("gMissionNumber");
    const long game_type = global_value("gGameType");
    // A skirmish loss arrives here after REPLAY_MISSION's No: QuitAndJumpToMenu LOADMISSION.
    if (!won) return game_type == 0 ? "MVE_LOSE" : "LOADMISSION";
    if (game_type != 0) return "SCORE";
    if (mission >= 9) {
        globals_["gMissionNumber"] = "10";
        const long side = global_value("gSideId");
        return side == 1 ? "MVE_HARKONNEN_END" : side == 2 ? "MVE_ORDOS_END" : "MVE_ATREIDES_END";
    }
    globals_["gMissionNumber"] = std::to_string(mission + 1);
    return "SCORE";
}

void MenuRunner::front_begin(const std::string& menu, bool jump) {
    // Back in the front end: the in-game mode is over, and a transition the
    // in-game menus selected must not replay its old snapshots over these
    // menus.
    ingame_ = false;
    transition_ = ui::Transition{};
    quit_ = false;
    stack_.clear();
    globals_["gGameState"] = "1"; // the boot loop runs the menus with gGameState 1
    if (jump) {
        nav_ = NavCommand{"QuitAndJumpToMenu", menu, ""};
        process_navigation();
    } else {
        open_menu(menu);
    }
    start_transition(); // the boot code (FUN_00442580) starts one too
}

bool MenuRunner::front_step(float mouse_x, float mouse_y, bool down, bool up) {
    mouse_x_ = mouse_x;
    mouse_y_ = mouse_y;
    mouse_down_ = down;
    mouse_up_ = up;
    if (down) mouse_held_ = true;
    if (up) mouse_held_ = false;
    return front_pass();
}

std::string MenuRunner::front_top() const { return stack_.empty() ? std::string() : stack_.back()->name; }

bool MenuRunner::front_pass() {
    sync_text_input();
    const uint64_t now = SDL_GetTicks();
    music_.service();
    lobby_pump(now); // FUN_0048e660 from the menu draw: the network pages' messages

    // The window procedure's mouse handlers ran in the message pump, before
    // this UI update: they ask the input switch now (FUN_004a3980 at the
    // start of FUN_00492a70, then FUN_004712c0).
    gate_mouse();

    // FUN_004712c0 starts with the transition step and returns while it
    // runs: no input, no animation, no redraw. Whenever it is not running
    // (nothing pending, a SKIP, or a finish path) FUN_00472870 switches
    // input on (LAB_00472b26 and the finish paths); FUN_004706c0 acts only
    // on a change, which restarts the 10-query delay.
    const bool transition_running = transition_.step(screen_, now);
    if (!transition_running) {
        set_input(true);
        if (stack_.empty()) return false;
        // FUN_0043c4b0 -> FUN_0048d8f0: a movie element plays from the menu
        // draw, after this update's navigation (0x47259f..0x4726a4 come
        // before the draw at 0x4726ee). Its commands only queue the
        // navigation, which runs in the next update; 0x797e28 skips this
        // pass's cursor query and blit (FUN_00492a70).
        if (play_movie_if_any(*stack_.back())) return !quit_;
        handle_input();
        update_tooltip(SDL_GetTicks());
        update_animations(now);
        process_navigation();
        if (quit_ || stack_.empty()) return false;
        if (!transition_.owns_screen()) {
            // FUN_0043c4b0 -> FUN_0043ee80: the score counts one step per
            // update while it is the top menu.
            if (score_.on && is_score_menu(stack_.back()->name)) score_update(*stack_.back(), now);
            if (score_.on && std::none_of(stack_.begin(), stack_.end(), [](const auto& mm) {
                    return is_score_menu(mm->name);
                })) {
                hs_save(); // FUN_0043dc60, the score menu's destroy
                score_.on = false;
            }
            // FUN_0043c4b0 for the top menu with +0x34: input off and a full
            // redraw every update, then CreditsDraw (FUN_0043d520) on top.
            Menu& top = *stack_.back();
            if (top.credits) {
                set_input(false);
                invalidate(top);
            }
            draw_all_dirty(screen_);
            if (top.credits) draw_credits(*top.credits, now);
            if (score_.on && is_score_menu(stack_.back()->name)) score_draw_bars(screen_);
            if (stack_.back()->globe) update_globe(*stack_.back());
        }
    }
    // FUN_00492a70 -> FUN_00423a20: the game's own cursor, hidden while
    // input is off. On the campaign map it is shape 2 over a choice
    // (FUN_0042c400), else the arrow.
    SDL_HideCursor();
    if (!stack_.empty() && stack_.back()->globe) {
        cursor_type_ = stack_.back()->globe->hover(int(std::floor(mouse_x_)), int(std::floor(mouse_y_))) ? 2 : 0;
    }
    present();
    return true;
}

void MenuRunner::gate_mouse() {
    // FUN_00437840 (left down), FUN_00437600 (left up) and FUN_00437af0
    // (right up, via FUN_00471250) each return at once while a transition
    // is pending (manager +0x11d, no query), else run only if
    // `FUN_004706f0() != 0 || gGameState (0x4dfb08) == 2`. The query still
    // counts the 10 "off" answers down.
    auto handler_runs = [this] {
        if (transition_.pending()) return false;
        const bool on = input_allowed();
        return on || global_value("gGameState") == 2;
    };
    if (mouse_down_ && !handler_runs()) mouse_down_ = false;
    if (mouse_up_ && !handler_runs()) mouse_up_ = false;
    right_list_ = right_up_ && handler_runs();
}

MenuRunner::Result MenuRunner::run(const std::string& menu, bool jump) {
    front_begin(menu, jump);
    uint64_t next_frame = SDL_GetTicksNS();
    while (!quit_) {
        mouse_down_ = mouse_up_ = false;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (display_mode::handle_event(ev)) continue;
            if (ev.type == SDL_EVENT_QUIT) quit_ = window_closed_ = true;
            // The key handlers (FUN_0043cb70 / FUN_0043cdf0) do not ask the
            // input switch; only the mouse handlers and the cursor do (doc 04).
            if (!stack_.empty() && !transition_.pending()) key_event(ev, false);
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP && ev.button.button == SDL_BUTTON_RIGHT) right_up_ = true;
            // Tab repeats while held (FUN_0043cdf0 sees every WM_KEYDOWN).
            if (ev.type == SDL_EVENT_KEY_DOWN && (!ev.key.repeat || ev.key.key == SDLK_TAB) && !stack_.empty() &&
                !transition_.pending()) {
                handle_keys(ev.key.key == SDLK_ESCAPE, ev.key.key == SDLK_TAB,
                            ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER);
            }
            if (ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                bool motion = ev.type == SDL_EVENT_MOUSE_MOTION;
                SDL_FPoint p = display_.window_to_screen(motion ? ev.motion.x : ev.button.x,
                                                         motion ? ev.motion.y : ev.button.y);
                mouse_x_ = p.x;
                mouse_y_ = p.y;
                if (!motion && ev.button.button == SDL_BUTTON_LEFT) {
                    (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? mouse_down_ : mouse_up_) = true;
                    mouse_held_ = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN; // drags and repeats (value bars, lists)
                    // Windows sends every second press of a fast pair as WM_LBUTTONDBLCLK.
                    if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.clicks >= 2 && ev.button.clicks % 2 == 0) {
                        mouse_double_ = true;
                    }
                }
            }
        }
        if (quit_) break;
        if (!front_pass()) break;

        static uint64_t last_present = 0;
        uint64_t t = SDL_GetTicks();
        if (last_present && t - last_present > 50) SDL_Log("slow frame: %llu ms", (unsigned long long)(t - last_present));
        last_present = t;
        next_frame += kFrameNs;
        uint64_t now_ns = SDL_GetTicksNS();
        if (next_frame > now_ns) {
            SDL_DelayNS(next_frame - now_ns);
        } else {
            next_frame = now_ns;
        }
    }
    if (!window_closed_ && global_value("gGameState") == 2) {
        music_.stop(); // leaving the front end stops its music (FUN_00442580, doc 15)
        return Result::StartGame;
    }
    return Result::Quit;
}

void MenuRunner::bar_dirty(Menu& m, int i) {
    m.dirty[size_t(i)] = 1;
    // A VALUEBAR prints its number beside the bar over saved background
    // (docs/re/55 section 2.2): the old digits need the background again.
    if (m.screen.elements[size_t(i)].header.rfind("VALUEBAR ", 0) == 0) {
        m.background_dirty = true;
        std::fill(m.dirty.begin(), m.dirty.end(), char(1));
    }
}

void MenuRunner::valuebar_down(Menu& m, int i, uint64_t now) {
    // FUN_0048c640: on the thumb, drag; beside it, one step towards the
    // mouse, repeated every 200 ms while held.
    auto rect = ui::element_rect(m.screen.elements[size_t(i)]);
    if (!rect) return;
    auto& s = m.states[size_t(i)];
    const ui::Rect thumb = ui::valuebar_thumb(*rect, s);
    const int mx = int(std::floor(mouse_x_)), my = int(std::floor(mouse_y_));
    if (!rect->contains(mx, my)) return; // the region rect +0x7cc..+0x7d8
    if (thumb.contains(mx, my)) {
        bar_drag_ = i;
        bar_grab_ = mx - thumb.x; // +0x7e8
        return;
    }
    // Left of the thumb: one step down; right of its right edge (+0x7c4):
    // one step up; on that edge pixel itself nothing.
    if (mx >= thumb.x && mx <= thumb.x + thumb.w) return;
    if (ui::valuebar_step(s, mx < thumb.x ? -1 : 1)) bar_dirty(m, i);
    bar_repeat_ = i;
    bar_repeat_ms_ = now;
}

void MenuRunner::valuebar_held(Menu& m, uint64_t now) {
    if (bar_drag_ >= 0 && size_t(bar_drag_) < m.states.size()) {
        // FUN_0048c9c0: the thumb follows the mouse; the value from the
        // exe's float expression (ui::valuebar_drag, docs/re/55 section 2.4).
        auto rect = ui::element_rect(m.screen.elements[size_t(bar_drag_)]);
        if (!rect) return;
        auto& s = m.states[size_t(bar_drag_)];
        if (ui::valuebar_drag(*rect, s, int(std::floor(mouse_x_)) - bar_grab_)) bar_dirty(m, bar_drag_);
        return;
    }
    if (bar_repeat_ >= 0 && size_t(bar_repeat_) < m.states.size() && now - bar_repeat_ms_ > 199) {
        // FUN_0048c450: repeats while the button stays down inside the bar.
        auto rect = ui::element_rect(m.screen.elements[size_t(bar_repeat_)]);
        const int mx = int(std::floor(mouse_x_)), my = int(std::floor(mouse_y_));
        bar_repeat_ms_ = now; // +0x7e4 is reset before the tests (also outside the bar)
        if (!rect || !rect->contains(mx, my)) return;
        auto& s = m.states[size_t(bar_repeat_)];
        const ui::Rect thumb = ui::valuebar_thumb(*rect, s);
        if (mx >= thumb.x && mx <= thumb.x + thumb.w) return; // on the thumb or its right edge
        if (ui::valuebar_step(s, mx < thumb.x ? -1 : 1)) bar_dirty(m, bar_repeat_);
    }
}

void MenuRunner::ingame_open(const std::string& menu) {
    // FUN_00470e60: close all, open the page; nothing fades (SKIP / SKIP).
    ingame_ = true;
    transition_ = ui::Transition{};
    globals_["gGameState"] = "2";
    stack_.clear();
    nav_.reset();
    ingame_event_ = IngameEvent::None;
    // 0x797590 is not cleared by FUN_00470e60 (only the OPTIONS path does,
    // ingame_clear_outside).
    closed_ = false;
    bar_drag_ = bar_repeat_ = -1;
    input_on_ = true;
    input_delay_ = 0;
    open_menu(menu);
    start_transition(); // 0x470e84: FUN_00472730 starts none with gGameState 2 (+0x11d = 0, input on)
}

void MenuRunner::ingame_redraw_all() {
    for (auto& m : stack_) invalidate(*m);
}

void MenuRunner::ingame_close() {
    stack_.clear();
    nav_.reset();
}

void MenuRunner::ingame_jump(const std::string& menu) {
    if (!ingame_active()) {
        ingame_open(menu);
        return;
    }
    open_menu(menu);
}

void MenuRunner::ingame_set_text(const std::string& menu, const std::string& element, const std::string& text) {
    if (Menu* m = find_menu(menu)) set_box_text(*m, element, text);
}

std::string MenuRunner::take_ingame_voice() {
    std::string v;
    v.swap(ingame_voice_);
    return v;
}

MenuRunner::IngameEvent MenuRunner::ingame_frame(ui::Surface& out, float mouse_x, float mouse_y, bool down,
                                                 bool up, bool held, bool esc, bool right_up) {
    if (stack_.empty()) return IngameEvent::None;
    right_up_ = right_up;
    mouse_x_ = mouse_x;
    mouse_y_ = mouse_y;
    mouse_down_ = down;
    mouse_up_ = up;
    mouse_held_ = held;
    // FUN_004712c0 starts with the transition step FUN_00472870, which at
    // 0x472887 skips everything while gGameState (0x4dfb08) == 2: +0x11d = 0
    // and input on (LAB_00472b26). So in a mission a selected transition is
    // dropped and the menus draw at once (no fade, docs/re/20 section 2.5).
    // The mouse handlers ask the input switch before that (gate_mouse); in
    // a game they run whatever it says.
    if (global_value("gGameState") == 2) {
        if (transition_.pending()) transition_ = ui::Transition{};
        gate_mouse();
        set_input(true);
    } else {
        gate_mouse();
        if (transition_.step(out, SDL_GetTicks())) return IngameEvent::None;
        set_input(true); // LAB_00472b26 / the finish paths
    }
    if (esc) {
        // FUN_0043cb70: Esc runs the first button (script order) that has a
        // BackMenu command, with all its commands.
        Menu& top = *stack_.back();
        for (size_t i = 0; i < top.screen.elements.size(); ++i) {
            const auto& e = top.screen.elements[i];
            if (e.type != ui::ElementType::Button) continue;
            auto actions = actions_of(e);
            if (std::none_of(actions.begin(), actions.end(),
                             [](const std::string& l) { return l.rfind("BackMenu", 0) == 0 && l.size() >= 8 &&
                                                               (l.size() == 8 || l[8] == ' ' || l[8] == '\t'); })) {
                continue;
            }
            execute(actions, &top, int(i));
            break;
        }
    } else {
        handle_input();
    }
    process_navigation();
    sync_text_input();
    if (!stack_.empty() && !transition_.owns_screen()) draw_all_dirty(out);
    const IngameEvent e = ingame_event_;
    ingame_event_ = IngameEvent::None;
    if (e != IngameEvent::None) stack_.clear();
    if (stack_.empty()) sync_text_input();
    return e;
}


void MenuRunner::set_box_text(Menu& m, const std::string& name, const std::string& text) {
    for (size_t i = 0; i < m.screen.elements.size(); ++i) {
        if (m.screen.elements[i].name != name) continue;
        if (m.states[i].literal && *m.states[i].literal == text) return;
        m.states[i].literal = text;
        m.dirty[i] = 1;
        // The box is transparent: its old digits need the background again.
        m.background_dirty = true;
        std::fill(m.dirty.begin(), m.dirty.end(), char(1));
    }
}

void MenuRunner::score_mp_setup(Menu& m) {
    // FUN_0043d7e0 / FUN_0043dcb0 (docs/re/56 section 1): the background
    // becomes scoreMP, the MoonSpin animation `dune` goes, and a table is
    // built: one row per side with an active AI, or the local side in a
    // skirmish; no count-up, no high scores.
    if (const auto* img = assets_.image("scoreMP")) {
        ui::Surface bg(kWidth, kHeight);
        bg.blit(*img, {0, 0, int(img->width), int(img->height)}, 0, 0, ui::BlitMode::Opaque);
        m.background = std::move(bg);
    }
    if (const int di = find_element(m, "dune"); di >= 0) {
        m.screen.elements.erase(m.screen.elements.begin() + di);
        m.states.erase(m.states.begin() + di);
        m.anims.erase(m.anims.begin() + di);
        m.frames.erase(m.frames.begin() + di);
        m.dirty.erase(m.dirty.begin() + di);
        m.drawn_key.erase(m.drawn_key.begin() + di);
        m.saved_under.erase(m.saved_under.begin() + di);
    }
    // A text box (FUN_004384a0 -> FUN_0048b9c0, type 7): Transparent
    // background, font 0, alignment +0x85c. The box keeps the x / width
    // passed (0x71 = 113 for the name, 0xeb = 235 ... for the numbers);
    // the 5 px margin is added once, when drawn (FUN_0048bb10: left text at
    // x + 5, right text ending at right - 5), so the name is drawn at 118
    // and the first number column at 240, as on the original's screen.
    auto box = [&](const std::string& name, int x, int y, int w, int h, const std::string& key,
                   std::optional<std::string> literal, ui::TextAlign align, const std::string& colour,
                   bool underline = false) {
        ui::Element e;
        e.type = ui::ElementType::TextBox;
        e.name = name;
        e.header = "TEXTBOX " + name;
        e.align = align;
        e.underline = underline;
        e.body = {std::to_string(x) + ", " + std::to_string(y), std::to_string(w) + ", " + std::to_string(h),
                  "Transparent", underline ? "White" : "Transparent", "Transparent", colour, key};
        e.literal_text = std::move(literal);
        add_element(m, std::move(e));
    };
    const bool skirmish = global_value("gGameType") == 1;
    box("title", 20, 20, 600, 25, skirmish ? "TitlePracticeScore" : "TitleMultiScore", std::nullopt,
        ui::TextAlign::Centre, "White", true);
    box("names", 87, 53, 150, 24, "PlayerNames", std::nullopt, ui::TextAlign::Centre, "White");
    box("casualties", 235, 53, 133, 24, "Casualties", std::nullopt, ui::TextAlign::Centre, "White");
    box("buildings", 368, 53, 124, 24, "Buildings", std::nullopt, ui::TextAlign::Centre, "White");
    box("spice", 492, 53, 59, 24, "Spice", std::nullopt, ui::TextAlign::Centre, "White");
    box("unit_dest", 235, 77, 69, 24, "Inflicted", std::nullopt, ui::TextAlign::Centre, "White");
    box("unit_cas", 304, 77, 64, 24, "Received", std::nullopt, ui::TextAlign::Centre, "White");
    box("build_cas", 368, 77, 60, 24, "Lost", std::nullopt, ui::TextAlign::Centre, "White");
    box("build_dest", 428, 77, 64, 24, "Destroyed", std::nullopt, ui::TextAlign::Centre, "White");
    box("harvested", 492, 77, 58, 24, "Harvested", std::nullopt, ui::TextAlign::Centre, "White");
    // The rows and their order: descending place, ties in side order. The
    // exe sorts row indices as side numbers (right when the rows are sides
    // 0..n-1, as in a skirmish).
    std::vector<int> rows;
    for (int s = 0; s < 8; ++s) {
        if (score_stats_[size_t(s)].row) rows.push_back(s);
    }
    const int n = int(rows.size());
    std::vector<int> idx(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k) idx[size_t(k)] = k;
    for (int i = 0; i < n; ++i) {
        for (int j = i; j < n; ++j) {
            if (score_stats_[size_t(idx[size_t(i)])].place < score_stats_[size_t(idx[size_t(j)])].place) {
                std::swap(idx[size_t(i)], idx[size_t(j)]);
            }
        }
    }
    static const char* const kIcons[] = {"mp_at", "mp_hk", "mp_or"};
    for (int k = 0; k < n; ++k) {
        const int s = idx[size_t(k)];
        const auto& st = score_stats_[size_t(s)];
        const int y = 101 + 24 * k;
        const std::string ks = std::to_string(k);
        if (st.house >= 0 && st.house <= 2) {
            ui::Element icon;
            icon.type = ui::ElementType::GroupBox;
            icon.name = "icon" + ks;
            icon.header = "GROUPBOX " + icon.name;
            icon.body = {"87, " + std::to_string(y + 1), "24, 24", "Transparent", "Transparent", kIcons[st.house]};
            add_element(m, std::move(icon));
        }
        // AI active: "Computer"; else the network name, or in a skirmish
        // gNetPlayerName (docs/re/56 section 1.5). The exe passes the network
        // name to sprintf as the format; names hold only letters, digits and
        // spaces (NoOtherChars, docs/re/58 section 2.3), so no '%' can change it.
        auto it = globals_.find("gNetPlayerName");
        const std::string name = st.ai              ? assets_.lookup_text("UI_Computer")
                                 : !st.name.empty() ? st.name
                                                    : (it != globals_.end() ? it->second : std::string());
        box("name" + ks, 113, y, 124, 24, "", name, ui::TextAlign::Left, "NetColour" + std::to_string(st.colour));
        box("unitdest" + ks, 235, y, 69, 24, "", std::to_string(st.units_killed), ui::TextAlign::Left, "LGrey");
        box("unitcas" + ks, 304, y, 64, 24, "", std::to_string(st.units_lost), ui::TextAlign::Left, "LGrey");
        box("buildcas" + ks, 368, y, 60, 24, "", std::to_string(st.buildings_lost), ui::TextAlign::Left, "LGrey");
        box("builddest" + ks, 428, y, 64, 24, "", std::to_string(st.buildings_killed), ui::TextAlign::Left, "LGrey");
        box("spice" + ks, 492, y, 58, 24, "", std::to_string(st.spice), ui::TextAlign::Left, "LGrey");
    }
}

void MenuRunner::score_setup(Menu& m) {
    // FUN_0043d820 "CreateScore()" + FUN_0043ec70 (docs/re/24 section 2.2 / 2.3).
    score_ = Score{};
    score_.on = true;
    score_finished_ = score_skip_ = false; // 0x43d856 / 0x43d861
    hs_load();
    hs_table_ = false;
    const long side = global_value("gSideId");
    const long mission = global_value("gMissionNumber"); // already the next one
    const int house = side >= 0 && side < 3 ? int(side) : 0;
    const int rec = std::clamp(int(mission) - 1, 0, campaign::kRecords - 1);
    auto& c = score_.count;
    c.m = campaign_.at(house, rec).value; // the record of the mission just won
    // d = 2 only when the local player lost (0x6b8278). A campaign score
    // follows a win (a loss goes to MVE_LOSE), and won / lost exclude each
    // other (doc 19 section 3), so d = 1 here.
    c.d = 1;
    // Totals over every active side; "ours" = the local player's share of
    // the others' losses and of all the spice; the local player is slot
    // gSideId (0x798544).
    std::array<campaign::ScoreSide, 8> sides{};
    for (size_t s = 0; s < 8; ++s) {
        const auto& st = score_stats_[s];
        sides[s] = {st.active, uint32_t(st.units_lost), uint32_t(st.buildings_lost), uint32_t(st.spice)};
    }
    campaign::score_targets(c, sides, house);
    // Bar images by house: ours by the player's house, theirs by the
    // mission's enemy house (campaign column 24).
    static const char kSuffix[] = {'b', 'r', 'g'};
    const int enemy = std::clamp(campaign_.at(house, rec).track_house, 0, 2);
    static const char* const kKind[] = {"u", "b", "s"};
    for (int k = 0; k < 3; ++k) {
        score_.our_bar[size_t(k)] = std::string(kKind[k]) + "1_" + kSuffix[house];
        score_.their_bar[size_t(k)] = std::string(kKind[k]) + "2_" + kSuffix[enemy];
    }
    // The house picture at (16, 12) on the menu's background.
    static const char* const kPicture[] = {"at_score", "hk_score", "od_score"};
    ui::Surface bg(kWidth, kHeight);
    for (const auto& e : m.screen.elements) {
        if (e.type != ui::ElementType::Background) continue;
        if (const auto* img = assets_.image(e.name)) {
            bg.blit(*img, {0, 0, int(img->width), int(img->height)}, 0, 0, ui::BlitMode::Opaque);
        }
        break;
    }
    if (const auto* pic = assets_.image(kPicture[house])) {
        bg.blit(*pic, {0, 0, int(pic->width), int(pic->height)}, 16, 12, ui::BlitMode::KeyedShadow);
    }
    m.background = std::move(bg);
    SDL_Log("score: targets ours %d/%d/%d theirs %d/%d/%d x %.2f", c.our_t[0], c.our_t[1], c.our_t[2], c.their_t[0],
            c.their_t[1], c.their_t[2], double(c.m));
}

bool MenuRunner::score_step(Menu& m, uint32_t now) {
    // FUN_0043f030 (docs/re/24 section 2.4): the numbers come from
    // campaign::score_step; here the boxes and the sound.
    static const char* const kOur[] = {"ourUnitCasualty", "ourBuildingCasualty", "ourSpiceHarvested"};
    static const char* const kTheir[] = {"theirUnitCasualty", "theirBuildingCasualty", "theirSpiceHarvested"};
    auto& c = score_.count;
    const campaign::ScoreStep r = campaign::score_step(c, now);
    if (r.tick && !speech_.playing()) {
        // S_SCORETICK through the voice queue: plays whenever speech is free.
        if (auto f = assets_.sample_file("S_SCORETICK")) speech_.play_file(*f);
    }
    for (int k = 0; k < 3; ++k) {
        if (r.our_moved[size_t(k)]) set_box_text(m, kOur[k], campaign::score_our_text(c, k));
        if (r.their_moved[size_t(k)]) set_box_text(m, kTheir[k], campaign::score_their_text(c, k));
    }
    if (r.totals) {
        set_box_text(m, "ourScore", std::to_string(c.our_total));
        set_box_text(m, "theirScore", std::to_string(c.their_total));
    }
    if (!r.busy) score_finished_ = true; // 0x43f181
    return r.busy;
}

void MenuRunner::score_update(Menu& m, uint64_t now) {
    // FUN_0043ee80, once per UI update while the score menu is on top
    // (campaign only).
    if (score_finished_ && score_skip_) {
        // FUN_0043fb60: every counter to its target - 1, then count until
        // done. The 2 s pauses are inside, so the screen freezes (seen in
        // the original). The table follows on the next update.
        campaign::score_skip_counters(score_.count);
        while (score_step(m, uint32_t(SDL_GetTicks()))) SDL_Delay(1);
        score_skip_ = false;
        return;
    }
    if (!score_step(m, uint32_t(now)) && !hs_table_) {
        hs_build(m); // FUN_0043fc90, once, when the count first reports done
        hs_table_ = true;
    }
}

void MenuRunner::hs_load() {
    // FUN_0043eee0: 5 slots from GameSave\hs.dat (0x44c bytes); slot 6 and,
    // without the file, every slot: unused, empty name, house 3.
    hs_ = {};
    const auto dir = game_save_dir();
    if (dir.empty()) return;
    std::FILE* f = std::fopen((dir / "hs.dat").string().c_str(), "rb");
    if (!f) return;
    std::vector<uint8_t> d(campaign::kHighScoreFileSize);
    const size_t n = std::fread(d.data(), 1, d.size(), f);
    std::fclose(f);
    d.resize(n);
    hs_ = campaign::high_scores_from_file(d);
}

void MenuRunner::hs_save() {
    // FUN_0043efd0: the first 5 slots, 0xDC bytes each.
    const auto dir = game_save_dir();
    if (dir.empty()) return;
    const std::vector<uint8_t> d = campaign::high_scores_to_file(hs_);
    if (std::FILE* f = std::fopen((dir / "hs.dat").string().c_str(), "wb")) {
        std::fwrite(d.data(), 1, d.size(), f);
        std::fclose(f);
    } else {
        SDL_Log("SaveHighScores(): Could not write to file GameSave\\hs.dat");
    }
}

void MenuRunner::add_element(Menu& m, ui::Element e, ui::ElementState s) {
    m.screen.elements.push_back(std::move(e));
    m.states.push_back(std::move(s));
    m.anims.emplace_back();
    m.frames.push_back(0);
    m.dirty.push_back(1);
    m.drawn_key.push_back(-1);
    m.saved_under.emplace_back();
    // The run-time Add* calls grow the menu's box like the parsed elements.
    if (const auto r = ui::element_rect(m.screen.elements.back())) {
        const int l = std::min(m.bounds.x, r->x), t = std::min(m.bounds.y, r->y);
        const int rr = std::max(m.bounds.x + m.bounds.w, r->x + r->w);
        const int bb = std::max(m.bounds.y + m.bounds.h, r->y + r->h);
        m.bounds = {l, t, rr - l, bb - t};
    }
}

void MenuRunner::hs_build(Menu& m) {
    // FUN_0043fc90 (docs/re/24 section 2.6). All texts White on Transparent,
    // 15 px high, font 0.
    auto box = [&](const std::string& name, int x, int y, int w, const std::string& key,
                   std::optional<std::string> literal, ui::TextAlign align) {
        ui::Element e;
        e.type = ui::ElementType::TextBox;
        e.name = name;
        e.header = "TEXTBOX " + name;
        e.align = align;
        e.body = {std::to_string(x) + ", " + std::to_string(y), std::to_string(w) + ", 15", "Transparent",
                  "Transparent", "Transparent", "White", key};
        e.literal_text = std::move(literal);
        add_element(m, std::move(e));
    };
    {
        ui::Element frame; // the frame hs_border (90, 233) with the image hs_table
        frame.type = ui::ElementType::GroupBox;
        frame.name = "hs_border";
        frame.header = "GROUPBOX hs_border";
        int w = 10, h = 10;
        if (const auto* img = assets_.image("hs_table")) {
            w = int(img->width);
            h = int(img->height);
        }
        frame.body = {"90, 233", std::to_string(w) + ", " + std::to_string(h), "Transparent", "Transparent", "hs_table"};
        add_element(m, std::move(frame));
    }
    box("high_scores", 90, 220, 410, "SS_HighScore", std::nullopt, ui::TextAlign::Centre);
    box("name", 110, 235, 100, "SS_Name", std::nullopt, ui::TextAlign::Left);
    box("rating", 210, 235, 160, "SS_Rating", std::nullopt, ui::TextAlign::Left);
    box("house", 370, 235, 80, "SS_House", std::nullopt, ui::TextAlign::Left);
    box("mission", 450, 235, 40, "SS_Mission", std::nullopt, ui::TextAlign::Left);
    box("score", 500, 235, 60, "SS_Score", std::nullopt, ui::TextAlign::Left);
    for (int k = 0; k < 6; ++k) {
        box("slot" + std::to_string(k + 1), 90, 250 + 15 * k, 20, "", std::to_string(k + 1), ui::TextAlign::Left);
    }

    // The new score goes into the first slot that is unused or lower; the
    // slots below move down (slot 6 drops out).
    const int fresh = campaign::high_score_insert(hs_, uint32_t(score_.count.our_total), global_value("gSideId"),
                                                  global_value("gMissionNumber"));

    for (int k = 0; k < 6; ++k) {
        const auto& h = hs_[size_t(k)];
        if (!h.used) continue;
        const int y = 250 + 15 * k;
        const std::string n = std::to_string(k);
        if (k == fresh) {
            // The new entry's name: an edit box with focus (FUN_00438390).
            ui::Element e;
            e.type = ui::ElementType::EditBox;
            e.name = "name" + n;
            e.header = "EDITBOX " + e.name;
            e.body = {"110, " + std::to_string(y), "100, 15", "Transparent", "Transparent", "White",
                      "SetFromUIStringSpecial gHighScore" + n, "BackMenu"}; // commands 0xAE, 0xDD
            ui::ElementState s;
            s.edit_focus = true;
            add_element(m, std::move(e), std::move(s));
        } else {
            box("name" + n, 110, y, 100, "", h.name, ui::TextAlign::Left);
        }
        box("rank" + n, 210, y, 130, "Rank" + std::to_string(h.rank + 1), std::nullopt, ui::TextAlign::Left);
        static const char* const kHouse[] = {"Atreides", "Harkonnen", "Ordos"};
        if (h.house < 3) box("house" + n, 370, y, 80, kHouse[h.house], std::nullopt, ui::TextAlign::Left);
        else box("house" + n, 370, y, 80, "", "Error", ui::TextAlign::Left);
        box("mission" + n, 450, y, 40, "", std::to_string(h.mission), ui::TextAlign::Left);
        box("score" + n, 500, y, 60, "", std::to_string(h.score), ui::TextAlign::Left);
    }
    m.background_dirty = true;
    std::fill(m.dirty.begin(), m.dirty.end(), char(1));
}

int MenuRunner::focused_edit(Menu& m) const {
    for (size_t i = 0; i < m.screen.elements.size() && i < m.states.size(); ++i) {
        if (m.screen.elements[i].type == ui::ElementType::EditBox && m.states[i].edit_focus) return int(i);
    }
    return -1;
}

void MenuRunner::edit_changed(Menu& m, int i) {
    // The box is transparent: the old text needs the background again.
    m.dirty[size_t(i)] = 1;
    m.background_dirty = true;
    std::fill(m.dirty.begin(), m.dirty.end(), char(1));
}

void MenuRunner::edit_char(Menu& m, int i, char c) {
    // FUN_0048b4a0: not a leading space; insert at the cursor; a text wider
    // than the box - 10 px loses the character again (FUN_0048b3c0).
    auto& es = m.states[size_t(i)];
    if (es.edit_cursor == 0 && c == ' ') return;
    // FUN_0048b4a0: refused when MaxCharacters (u16 +0xab2) != 0 and <= the
    // current length (0x48b4b5..0x48b4d6; GAMEMESSAGE's msgEdit: 64). The
    // width test below measures in font slot 0 (FUN_0042dfb0(text, 0) at
    // 0x48b535, FONT.FNT), which is menu_font().
    if (es.max_chars > 0 && int(es.edit_text.size()) >= es.max_chars) return;
    es.edit_text.insert(es.edit_text.begin() + es.edit_cursor, c);
    ++es.edit_cursor;
    const auto rect = ui::element_rect(m.screen.elements[size_t(i)]);
    if (rect && ui::text_width(assets_.menu_font(), es.edit_text) > rect->w - 10) edit_key(m, i, SDLK_BACKSPACE);
    edit_changed(m, i);
}

void MenuRunner::edit_key(Menu& m, int i, SDL_Keycode key) {
    auto& es = m.states[size_t(i)];
    auto& t = es.edit_text;
    auto strip = [&]() { // leading spaces go; the cursor stays inside the text
        while (!t.empty() && t[0] == ' ') t.erase(t.begin());
        es.edit_cursor = std::min(es.edit_cursor, int(t.size()));
    };
    switch (key) {
    case SDLK_LEFT: // FUN_0048b2a0
        if (es.edit_cursor > 0) --es.edit_cursor;
        break;
    case SDLK_RIGHT: // FUN_0048b2d0
        if (es.edit_cursor < int(t.size())) ++es.edit_cursor;
        break;
    case SDLK_BACKSPACE: // FUN_0048b3c0
        if (es.edit_cursor == 0) return;
        --es.edit_cursor;
        t.erase(t.begin() + es.edit_cursor);
        strip();
        break;
    case SDLK_DELETE: // FUN_0048b310
        if (es.edit_cursor < int(t.size())) t.erase(t.begin() + es.edit_cursor);
        strip();
        break;
    default:
        return;
    }
    edit_changed(m, i);
}

int MenuRunner::find_element(const Menu& m, const std::string& name) const {
    for (size_t i = 0; i < m.screen.elements.size(); ++i) {
        if (m.screen.elements[i].name == name) return int(i);
    }
    return -1;
}

MenuRunner::Menu* MenuRunner::find_menu(const std::string& name) {
    for (auto& mm : stack_) {
        if (mm->name == name) return mm.get(); // FUN_00470f10
    }
    return nullptr;
}

void MenuRunner::house_voice(const std::string& name) {
    // FUN_0046f900: the house prefix by gSideId (S_ Atreides, H_, O_).
    const long side = global_value("gSideId");
    const std::string prefix = side == 1 ? "H_" : side == 2 ? "O_" : "S_";
    if (auto f = assets_.sample_file(prefix + name)) speech_.play_file(*f);
}

void MenuRunner::get_game_saves(Menu& m, int list) {
    // 0xCD (FUN_00475a00): S0, S1, ... until a file is missing; the row is
    // the description, White for in-mission saves.
    auto& ls = m.states[size_t(list)];
    const int type = int(global_value("gGameType"));
    for (int n = 0;; ++n) {
        const auto h = read_save_header(save_path(type, n));
        if (!h) break;
        ls.items.push_back(h->description);
        ls.item_white.push_back(h->full);
        ls.item_data.push_back(n);
    }
}

void MenuRunner::get_cd_tracks(Menu& m, int list) {
    // GetCDTracks (FUN_00476590, docs/re/48 A.2): MUSIC\*.aud in listing
    // order (the exe does not sort; NTFS and the CD's ISO 9660 list by
    // upper-case name, as we sort; docs/re/15 section 2);
    // a row for each file with a text.uib key music_<base> (any case). The
    // current track's row is selected, without scrolling.
    cd_files_.clear();
    if (auto dir = gd_.find("MUSIC")) {
        for (const auto& f : std::filesystem::directory_iterator(*dir)) {
            std::string file = f.path().filename().string();
            for (char& c : file) c = char(std::toupper(static_cast<unsigned char>(c)));
            if (file.size() > 4 && file.compare(file.size() - 4, 4, ".AUD") == 0) {
                cd_files_.push_back(file.substr(0, file.size() - 4));
            }
        }
    }
    std::sort(cd_files_.begin(), cd_files_.end());
    auto& ls = m.states[size_t(list)];
    const std::string current = music_current_ ? music_current_() : std::string();
    int select = 0;
    for (const auto& base : cd_files_) {
        auto label = assets_.text().get("music_" + base);
        if (!label) continue;
        ls.items.push_back(*label);
        ls.item_white.push_back(false);
        ls.item_data.push_back(0);
        std::string up = current;
        for (char& c : up) c = char(std::toupper(static_cast<unsigned char>(c)));
        if (up == base) select = int(ls.items.size());
    }
    if (select != 0) list_select(m, list, select);
}

void MenuRunner::set_text_key(Menu& m, const std::string& name, const std::string& key) {
    const int ti = find_element(m, name);
    if (ti < 0) return;
    m.states[size_t(ti)].text = key;
    m.states[size_t(ti)].literal.reset();
    m.background_dirty = true; // a transparent box: the old text needs the background again
    std::fill(m.dirty.begin(), m.dirty.end(), char(1));
}

void MenuRunner::get_map_list(Menu& m, int list) {
    // GetMapList (FUN_0047a450, docs/re/51 section 3.1): every text.uib entry
    // whose key starts with "map_" (case-sensitive), in table order, when
    // Data\maps\<key + 4>.map exists; the row is the entry's value, white.
    if (m.screen.elements[size_t(list)].type != ui::ElementType::ListBox) return;
    auto& ls = m.states[size_t(list)];
    const std::string current = globals_["gNetMap"];
    int match = 0;
    for (const auto& [key, value] : assets_.text().entries()) {
        if (key.compare(0, 4, "map_") != 0 || key.size() <= 4) continue;
        const std::string name = key.substr(4);
        if (!gd_.find("Data/maps/" + name + ".map")) continue;
        ls.items.push_back(value);
        ls.item_white.push_back(true);
        ls.item_data.push_back(0);
        if (match == 0 && SDL_strcasecmp(name.c_str(), current.c_str()) == 0) match = int(ls.items.size());
    }
    if (ls.items.empty()) return;
    if (match == 0) {
        // FUN_00493050: the first listed map becomes gNetMap.
        match = 1;
    }
    list_select(m, list, match);
    // Scrolled into view.
    if (auto rect = ui::element_rect(m.screen.elements[size_t(list)])) {
        const auto g = ui::list_geometry(*rect, ls, assets_.menu_font());
        if (match - 1 < ls.first || match - 1 >= ls.first + g.visible) {
            ls.first = std::clamp(match - 1, 0, std::max(0, int(ls.items.size()) - g.visible));
        }
    }
}

void MenuRunner::get_map_from_list(Menu& m, int list) {
    // GetMapFromList (FUN_00490bb0, docs/re/51 section 3.2).
    auto& ls = m.states[size_t(list)];
    if (ls.selection == 0) {
        if (ls.items.empty()) {
            globals_["gNetMap"].clear();
            globals_["gNetMapDesc"] = assets_.lookup_text("NoMapSelected");
        } else {
            list_select(m, list, 1); // runs this command again with the row
            ls.first = 0;
            return;
        }
    } else {
        // The first text.uib entry whose value is the row's text.
        const std::string& row = ls.items[size_t(ls.selection - 1)];
        for (const auto& [key, value] : assets_.text().entries()) {
            if (value != row) continue;
            globals_["gNetMap"] = key.size() > 4 ? key.substr(4) : std::string();
            globals_["gNetMapDesc"] = value;
            break;
        }
    }
    set_box_text(m, "chosenMap", globals_["gNetMapDesc"]);
}

void MenuRunner::list_select(Menu& m, int list, int selection) {
    // FUN_00487a00: only a change stores it and runs the list's commands.
    auto& ls = m.states[size_t(list)];
    if (ls.selection == selection) return;
    ls.selection = selection;
    invalidate(m);
    execute(actions_of(m.screen.elements[size_t(list)]), &m, list);
    // On the network pages a selection change refreshes the lists
    // (FUN_00474b60(0)): IPXLOBBY's player list follows the game row.
    if (m.name == "IPXLOBBY" && m.screen.elements[size_t(list)].name == "gameList") lobby_refresh(m);
}

void MenuRunner::list_mouse(Menu& m, int list, bool down, bool up, bool held) {
    // FUN_00487af0 (rows), FUN_004883b0 (arrows, one row on release),
    // track paging and the thumb drag (FUN_004880f0); docs/re/27 section 3.4.
    auto rect = ui::element_rect(m.screen.elements[size_t(list)]);
    if (!rect) return;
    auto& ls = m.states[size_t(list)];
    const auto g = ui::list_geometry(*rect, ls, assets_.menu_font());
    const int mx = int(std::floor(mouse_x_)), my = int(std::floor(mouse_y_));
    const int count = int(ls.items.size());
    auto clamp_first = [&] { ls.first = std::clamp(ls.first, 0, std::max(0, count - g.visible)); };
    if (down) {
        if (g.bar && g.up.contains(mx, my)) {
            ls.arrow_pressed = 1;
        } else if (g.bar && g.down.contains(mx, my)) {
            ls.arrow_pressed = 2;
        } else if (g.active && g.track.contains(mx, my)) {
            if (my < g.thumb.y) {
                ls.first -= g.visible;
            } else if (my >= g.thumb.y + g.thumb.h) {
                ls.first += g.visible;
            } else {
                ls.dragging = true;
                ls.grab = my - g.thumb.y;
            }
            clamp_first();
        } else if (g.rows.contains(mx, my)) {
            const int row = (my - g.rows.y) / g.row_h;
            if (ls.empty_slot && row == 0) {
                list_select(m, list, 0); // the "Empty slot" row deselects
            } else if (row < g.visible && ls.first + row < count) {
                // FUN_00487a00: the row-click commands first (AlterHandicap).
                alter_handicap(m, list, ls.first + row);
                list_select(m, list, ls.first + row + 1);
            }
        }
        invalidate(m);
        return;
    }
    if (ls.dragging && held) {
        const int span = g.track.h - g.thumb.h;
        const int y = my - ls.grab - g.track.y;
        ls.first = span > 0 ? y * (count - g.visible) / span : 0;
        clamp_first();
        invalidate(m);
    }
    if (up) {
        if (ls.arrow_pressed == 1 && g.up.contains(mx, my)) --ls.first;
        if (ls.arrow_pressed == 2 && g.down.contains(mx, my)) ++ls.first;
        clamp_first();
        if (ls.arrow_pressed || ls.dragging) invalidate(m);
        ls.arrow_pressed = 0;
        ls.dragging = false;
    }
}

void MenuRunner::list_remove_selected(Menu& m, int list) {
    // FUN_00487420: the rest shifts up; past the end, the last item is selected.
    auto& ls = m.states[size_t(list)];
    const int k = ls.selection - 1;
    if (k < 0 || k >= int(ls.items.size())) return;
    ls.items.erase(ls.items.begin() + k);
    ls.item_white.erase(ls.item_white.begin() + k);
    ls.item_data.erase(ls.item_data.begin() + k);
    if (k < int(ls.item_extra.size())) ls.item_extra.erase(ls.item_extra.begin() + k);
    if (ls.selection > int(ls.items.size())) ls.selection = int(ls.items.size());
    invalidate(m);
}

void MenuRunner::check_save_game(const std::string& list, const std::string& edit) {
    // 0xCA (FUN_00474cf0): no text -> SAVE_DESC; a description that exists
    // (case-insensitive) or a selected row -> CONFIRM_OVERWRITE; else save.
    Menu* m = stack_.empty() ? nullptr : stack_.back().get();
    if (!m) return;
    const int li = find_element(*m, list), ei = find_element(*m, edit);
    if (li < 0 || ei < 0) return;
    const auto& ls = m->states[size_t(li)];
    const std::string& text = m->states[size_t(ei)].edit_text;
    if (text.empty()) {
        nav_ = NavCommand{"JumpToMenu", "SAVE_DESC", ""};
        return;
    }
    auto same = [&](const std::string& x) {
        return x.size() == text.size() && std::equal(x.begin(), x.end(), text.begin(), [](char p, char q) {
                   return std::tolower(static_cast<unsigned char>(p)) == std::tolower(static_cast<unsigned char>(q));
               });
    };
    bool match = ls.selection != 0;
    for (size_t k = 1; k < ls.items.size(); ++k) match = match || same(ls.items[k]);
    if (!ls.empty_slot && !ls.items.empty()) match = match || same(ls.items[0]);
    if (match) {
        nav_ = NavCommand{"JumpToMenu", "CONFIRM_OVERWRITE", ""};
        return;
    }
    save_game(list, edit, false);
}

void MenuRunner::save_game(const std::string& list, const std::string& edit, bool from_confirm) {
    // 0xC9 (FUN_00474e20); the list and edit box of the SAVE menu.
    Menu* m = find_menu("SAVE");
    if (!m) return;
    const int li = find_element(*m, list), ei = find_element(*m, edit);
    if (li < 0 || ei < 0) return;
    auto& ls = m->states[size_t(li)];
    const std::string text = m->states[size_t(ei)].edit_text;
    if (text.empty()) return;
    const int count = int(ls.items.size());
    int t = ls.selection != 0 ? ls.selection - 1 : count;
    for (int k = 1; k < count; ++k) {
        const auto& x = ls.items[size_t(k)];
        if (x.size() == text.size() && std::equal(x.begin(), x.end(), text.begin(), [](char p, char q) {
                return std::tolower(static_cast<unsigned char>(p)) == std::tolower(static_cast<unsigned char>(q));
            })) {
            t = k; // the last match wins
        }
    }
    if (ls.empty_slot) --t;
    const int files = ls.empty_slot ? count - 1 : count;
    const int type = int(global_value("gGameType"));
    const bool full = global_value("gGameState") == 2 && game_saver_;
    // Disk space: more than 0x190000 (in a mission) / 0x200 bytes free, or
    // the replaced file is already that big.
    const uintmax_t need = full ? 0x190000 : 0x200;
    std::error_code ec;
    const auto dir = game_save_dir();
    const auto space = std::filesystem::space(dir.empty() ? std::filesystem::path(".") : dir, ec);
    const auto replaced = t < files ? std::filesystem::file_size(save_path(type, t), ec) : uintmax_t(0);
    if (!ec && space.available <= need && replaced < need) {
        nav_ = NavCommand{"JumpToMenu", "NO_FREE_SPACE", ""};
        return;
    }
    // Renumber so the new save is slot 0.
    auto rename = [&](int from, int to) {
        std::error_code e2;
        std::filesystem::rename(save_path(type, from), save_path(type, to), e2);
        if (e2) SDL_Log("Save file %s missing", save_path(type, from).string().c_str());
    };
    if (t >= files) {
        for (int k = files - 1; k >= 0; --k) rename(k, k + 1);
    } else {
        std::filesystem::remove(save_path(type, t), ec);
        for (int k = t - 1; k >= 0; --k) rename(k, k + 1);
    }
    SaveGame g = full ? game_saver_() : SaveGame{};
    g.full = full;
    g.description = text;
    g.common.mission = uint8_t(global_value("gMissionNumber"));
    g.common.side = uint8_t(global_value("gSideId"));
    g.common.game_type = uint32_t(type);
    g.common.difficulty = uint16_t(global_value("gDifficultyLevel"));
    if (!write_save(save_path(type, 0), g)) SDL_Log("could not write %s", save_path(type, 0).string().c_str());
    ls.items.push_back(text);
    house_voice("GAMESAVED");
    nav_ = from_confirm ? NavCommand{"GoBackMenu", "2", ""} : NavCommand{"BackMenu", "", ""};
}

void MenuRunner::apply_save_common(const SaveCommon& c, const std::string& mission) {
    globals_["gMissionNumber"] = std::to_string(c.mission);
    globals_["gSideId"] = std::to_string(c.side);
    globals_["gGameType"] = std::to_string(c.game_type);
    globals_["gDifficultyLevel"] = std::to_string(c.difficulty);
    if (!mission.empty()) globals_["gMissionMap"] = mission;
}

void MenuRunner::load_game(const std::string& list) {
    // 0xCC (FUN_00475740): nothing selected -> nothing. The row colour tells
    // a full save (White) from a between-missions save.
    if (stack_.empty()) return;
    Menu& m = *stack_.back();
    const int li = find_element(m, list);
    if (li < 0) return;
    const auto& ls = m.states[size_t(li)];
    if (ls.selection == 0) return;
    const int item = ls.selection - 1;
    const int file = ls.empty_slot ? item - 1 : item;
    const bool white = item < int(ls.item_white.size()) && ls.item_white[size_t(item)];
    const auto path = save_path(int(global_value("gGameType")), file);
    const bool in_mission = global_value("gGameState") == 2 && ingame_;
    if (white) {
        auto g = read_save(path, true);
        if (!g) {
            SDL_Log("not a saved game of ours: %s", path.string().c_str()); // e.g. an original exe save
            return;
        }
        apply_save_common(g->common, g->mission);
        pending_load_ = std::move(*g);
        globals_["gGameState"] = "2";
        if (in_mission) ingame_event_ = IngameEvent::Load;
        nav_ = NavCommand{"QuitMenu", "", ""};
        return;
    }
    auto g = read_save(path, false);
    if (!g) return;
    apply_save_common(g->common, "");
    globals_["gUseCampaignSelection"] = "1";
    if (in_mission) {
        ingame_event_ = IngameEvent::LoadMission; // gGameState 1: the mission ends
        nav_ = NavCommand{"QuitMenu", "", ""};
        return;
    }
    // FUN_004418b0(path, 0) restarts the saved track here (0x441d5b), but
    // the QuitAndJumpToMenu LOADMISSION that follows makes a non-MAINMENU
    // menu the top while gGameState is 1, which stops it again at the next
    // UI pass (doc 04, menu music); the name is replaced at the mission
    // set-up. So it is not started here (docs/re/15 section 1).
    nav_ = NavCommand{"QuitAndJumpToMenu", "LOADMISSION", ""};
}

void MenuRunner::delete_game(const std::string& list) {
    // 0xD1 (FUN_004760c0): the list in the menu below the pop-up; delete
    // the file, move the later ones down, drop the row; then BackMenu.
    if (stack_.size() >= 2) {
        Menu& below = *stack_[stack_.size() - 2];
        const int li = find_element(below, list);
        if (li >= 0 && below.states[size_t(li)].selection != 0) {
            const auto& ls = below.states[size_t(li)];
            const int file = ls.empty_slot ? ls.selection - 2 : ls.selection - 1;
            const int type = int(global_value("gGameType"));
            std::error_code ec;
            std::filesystem::remove(save_path(type, file), ec);
            for (int k = file + 1; std::filesystem::exists(save_path(type, k)); ++k) {
                std::filesystem::rename(save_path(type, k), save_path(type, k - 1), ec);
            }
            list_remove_selected(below, li);
        }
    }
    nav_ = NavCommand{"BackMenu", "", ""};
}

void MenuRunner::key_event(const SDL_Event& ev, bool enter_too) {
    // An edit box with focus: characters (FUN_0043cb70) and keys
    // (FUN_0043cdf0), both with the keyboard's auto-repeat.
    Menu& tm = *stack_.back();
    const int fe = focused_edit(tm);
    if (fe >= 0 && ev.type == SDL_EVENT_TEXT_INPUT) {
        // UTF-8 to bytes; the exe compares the character as a signed byte
        // (0x43ccf0), so only 0x20..0x7F get in.
        const auto* t = reinterpret_cast<const unsigned char*>(ev.text.text);
        for (size_t k = 0; t[k];) {
            size_t n = 1;
            while (t[k] >= 0x80 && t[k + n] && (t[k + n] & 0xC0) == 0x80) ++n;
            if (ui::ElementState::char_allowed(t[k], tm.states[size_t(fe)].refused_chars)) edit_char(tm, fe, char(t[k]));
            k += n;
        }
    }
    if (fe >= 0 && ev.type == SDL_EVENT_KEY_DOWN) edit_key(tm, fe, ev.key.key);
    if (enter_too && ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat &&
        (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER)) {
        handle_keys(false, false, true);
    }
}

void MenuRunner::ingame_key_event(const SDL_Event& ev) {
    if (stack_.empty()) return;
    key_event(ev, true);
    // Tab in the in-game menus too (the SAVE edit box), repeating while held.
    if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_TAB) handle_keys(false, true, false);
}

void MenuRunner::sync_text_input() {
    // Text events only while an edit box has focus (SDL shows an IME otherwise).
    SDL_Window* w = SDL_GetRenderWindow(display_.renderer());
    const bool want = !stack_.empty() && focused_edit(*stack_.back()) >= 0;
    if (want != SDL_TextInputActive(w)) {
        if (want) SDL_StartTextInput(w);
        else SDL_StopTextInput(w);
    }
}

void MenuRunner::draw_credits(ui::Credits& c, uint64_t now) {
    // FUN_0043d520: font 0, "White", centred in the 0..640 box; rows in the
    // 30 px edge bands blend in (FUN_0042e110), the rest are solid (FUN_0042e050).
    const auto& font = assets_.small_font();
    const uint16_t white = ui::to_rgb555(assets_.colour("White").value_or(formats::Rgb{255, 255, 255}));
    for (const auto& line : c.visible()) {
        const int x = ui::Credits::kLeft + ((ui::Credits::kRight - ui::Credits::kLeft - ui::text_width(font, *line.text)) >> 1);
        if (line.alpha < 32) screen_.draw_text_alpha(font, x, line.y, *line.text, white, line.alpha);
        else screen_.draw_text_solid(font, x, line.y, *line.text, white);
    }
    c.step(now);
}

void MenuRunner::score_draw_bars(ui::Surface& out) {
    // FUN_0043f030's bars: the image blitted keyed with shadow (FUN_00430770
    // flags 1, 1) at (265, y), clipped to the rect (265, y) .. (265 + width,
    // y + 16); width = counter x 100 / max. The rect is a clip on the
    // screen, not a source rect: the image shows from its own (0, 0).
    static const int kY[3][2] = {{40, 60}, {100, 120}, {160, 180}};
    const auto& c = score_.count;
    for (size_t k = 0; k < 3; ++k) {
        const int wo = campaign::score_bar_width(c.our_c[k], int(k));
        const int wt = campaign::score_bar_width(c.their_c[k], int(k));
        if (const auto* img = assets_.image(score_.our_bar[k]); img && wo > 0) {
            out.blit(*img, {0, 0, std::min(wo, int(img->width)), std::min(16, int(img->height))}, 265, kY[k][0],
                     ui::BlitMode::KeyedShadow);
        }
        if (const auto* img = assets_.image(score_.their_bar[k]); img && wt > 0) {
            out.blit(*img, {0, 0, std::min(wt, int(img->width)), std::min(16, int(img->height))}, 265, kY[k][1],
                     ui::BlitMode::KeyedShadow);
        }
    }
}

void MenuRunner::handle_keys(bool esc, bool tab, bool enter) {
    if (!esc && !tab && !enter) return; // other keys only reach edit boxes
    Menu& top = *stack_.back();
    // The GLOBE (menu +0x2C) ignores Esc and Tab until the territory reveal
    // is done (0x5139f0, docs/re/48 B).
    if ((esc || tab) && top.globe && top.globe->revealing()) return;
    if (is_score_menu(top.name) && !score_finished_ && (esc || tab)) {
        // FUN_0043cb70 (0x43cbd0) / FUN_0043cdf0 (0x43cee3) on a score menu
        // (+0x30): finished and skip, and nothing else. On the skirmish SCORE
        // nothing reads them, so its first Esc / Tab does nothing visible
        // (unless an earlier campaign score left "finished" set).
        score_finished_ = score_skip_ = true;
        return;
    }
    if (top.credits && (esc || tab)) {
        // FUN_0043cb70 / FUN_0043cdf0 on a +0x34 menu: queue RemoveMenu.
        // Esc is ignored while 0x5178b8 is set, but that flag is only set
        // around a menu's own movie (FUN_0048d8f0 -> FUN_00403460), and
        // CREDITS has none (docs/re/42 section 5).
        if (!nav_) nav_ = NavCommand{"RemoveMenu", "", ""};
        return;
    }
    if (tab) {
        // FUN_0043cdf0: the first focused edit box loses the focus; the next
        // unfocused one after it (else the first unfocused from the start)
        // gets it, cursor at the end. With one box only the cursor moves.
        std::vector<int> boxes;
        for (size_t i = 0; i < top.screen.elements.size() && i < top.states.size(); ++i) {
            if (top.screen.elements[i].type == ui::ElementType::EditBox) boxes.push_back(int(i));
        }
        if (boxes.empty()) return;
        size_t from = 0;
        for (size_t k = 0; k < boxes.size(); ++k) {
            if (top.states[size_t(boxes[k])].edit_focus) {
                top.states[size_t(boxes[k])].edit_focus = false;
                from = k + 1;
                break;
            }
        }
        for (size_t n = 0; n < boxes.size(); ++n) {
            auto& st = top.states[size_t(boxes[(from + n) % boxes.size()])];
            if (st.edit_focus) continue;
            st.edit_focus = true;
            st.edit_cursor = int(st.edit_text.size());
            break;
        }
        invalidate(top);
        return;
    }
    if (enter) {
        // FUN_0043cb70 '\r': every edit box with focus runs its commands
        // (e.g. SetFromUIStringSpecial gHighScore<i> + BackMenu, or
        // UICheckSaveGame + ForceButtonDown), then the EnterActions buttons.
        for (size_t i = 0; i < top.screen.elements.size() && i < top.states.size(); ++i) {
            if (top.screen.elements[i].type != ui::ElementType::EditBox || !top.states[i].edit_focus) continue;
            execute(actions_of(top.screen.elements[i]), &top, int(i));
        }
    }
    for (size_t i = 0; i < top.screen.elements.size(); ++i) {
        const auto& e = top.screen.elements[i];
        if (e.type != ui::ElementType::Button) continue;
        auto actions = actions_of(e);
        bool match = false;
        if (enter) {
            match = top.states[i].enter_default;
        } else {
            match = std::any_of(actions.begin(), actions.end(), [](const std::string& l) {
                return l.rfind("BackMenu", 0) == 0 && (l.size() == 8 || l[8] == ' ' || l[8] == '\t');
            });
        }
        if (!match) continue;
        execute(actions, &top, int(i)); // FUN_00482850(0)
        process_navigation();
        return;
    }
}

} // namespace d2k::app
