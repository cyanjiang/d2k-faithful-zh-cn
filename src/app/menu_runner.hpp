#pragma once
// Runs the original menu flow: a stack of menus loaded via menus.uib, drawn
// bottom to top, with input to the top menu, deferred navigation commands,
// FADE/TWEEN transitions and menu music. docs/re/04-menu-flow.md

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <functional>

#include "app/net_game.hpp"
#include "app/save_games.hpp"
#include "audio/music_player.hpp"
#include "audio/sample_player.hpp"
#include "campaign/campaign_table.hpp"
#include "campaign/regions.hpp"
#include "campaign/score.hpp"
#include "platform/game_data.hpp"
#include "platform/settings.hpp"
#include "render/display.hpp"
#include "render/sprites.hpp"
#include "ui/animation.hpp"
#include "ui/assets.hpp"
#include "ui/credits.hpp"
#include "ui/render.hpp"
#include "ui/screen.hpp"
#include "ui/surface.hpp"
#include "ui/globe.hpp"
#include "ui/transition.hpp"
#include "video/movie_player.hpp"

struct SDL_Texture;

namespace d2k::app {

class MenuRunner {
public:
    MenuRunner(render::Display& display, const platform::GameData& gd);
    ~MenuRunner();

    enum class Result { Quit, StartGame };

    // Opens `menu` and runs the front end (gGameState 1) until the window
    // closes or GameStateQuit (Quit), or the menus close with gGameState set
    // to GameStateMainLoop (StartGame, e.g. "Continue" in the briefing book).
    // `jump` = run QuitAndJumpToMenu `menu` first (after a mission) instead
    // of opening it.
    Result run(const std::string& menu, bool jump = false);
    // run() in steps, for tests (no window, no event loop): front_begin is
    // run()'s set-up, front_step one loop pass (FUN_00492a70) with the
    // mouse at (x, y) and a left press / release seen by this pass's message
    // pump. front_step returns false where run() would stop.
    void front_begin(const std::string& menu, bool jump = false);
    bool front_step(float mouse_x, float mouse_y, bool down, bool up);
    std::string front_top() const; // the top menu's name, or ""
    // Replaces the movie player (tests): gets the .VQA path.
    void set_movie_hook(std::function<video::MovieResult(const std::filesystem::path&)> f) { movie_hook_ = std::move(f); }
    // After a mission (doc 19 section 4.4): the next menu to jump to, with
    // the campaign globals updated (mission + 1 on a win).
    std::string after_mission(bool won);

    // The mission chosen on the campaign map, e.g. "a1v1".
    std::string mission() const;
    // gMissionNumber / gMissionMap / gSideId from a campaign mission name (FUN_004752c0).
    bool set_campaign_mission(const std::string& mission);

    // ---- In-game menus (docs/re/20-ingame-menu.md) ----
    // The game loop hosts the menu stack: menus draw over the frozen game
    // surface and take the game's mouse; nothing fades (all in-game pages
    // are SKIP). Abort / Restart are reported back to the game.
    // Load: a full save was picked during a mission (take_pending_load);
    // LoadMission: a between-missions save, the mission ends and the front
    // end continues at LOADMISSION with the saved side / mission.
    // Lost: REPLAY_MISSION's No in a skirmish (the game ends, LOADMISSION follows).
    enum class IngameEvent { None, Abort, Restart, Load, LoadMission, Lost };
    void ingame_open(const std::string& menu);  // FUN_00470e60
    bool ingame_active() const { return ingame_ && !stack_.empty(); }
    bool ingame_has(const std::string& name) const {
        if (!ingame_) return false;
        for (const auto& m : stack_) {
            if (m->name == name) return true;
        }
        return false;
    }
    // One loop pass: input to the top menu (screen coordinates), navigation,
    // then the dirty elements drawn into `out`.
    IngameEvent ingame_frame(ui::Surface& out, float mouse_x, float mouse_y, bool down, bool up, bool held,
                             bool esc, bool right_up = false);
    // Keyboard events for the menus (edit boxes, Enter) while in a game.
    void ingame_key_event(const SDL_Event& ev);
    void ingame_redraw_all();                   // after the game view was redrawn under the menus
    // A transition owns the screen; never in a mission (FUN_00472730 /
    // FUN_00472870 skip with gGameState 2).
    bool ingame_fading() const { return transition_.owns_screen(); }
    void ingame_draw(ui::Surface& out) { draw_all_dirty(out); }
    // True once after a menu closed (FUN_00473130 sets 0x5178ac / 0x5178b0:
    // the game view is redrawn for the next two passes).
    bool take_closed() {
        const bool c = closed_;
        closed_ = false;
        return c;
    }
    void ingame_close();                        // QuitMenu (e.g. the mission ended)
    // The OPTIONS button's press and its menu open clear 0x797590 (0x444832,
    // 0x444c97); the click-outside flag survives every other way.
    void ingame_clear_outside() { outside_down_ = false; }    bool ingame_outside_flag() const { return outside_down_; } // for tests
    // JumpToMenu over the open in-game menus (MP_TIMEOUT), or opens it.
    void ingame_jump(const std::string& menu);
    // A text box of an open in-game menu shows `text` (MP_TIMEOUT's counter).
    void ingame_set_text(const std::string& menu, const std::string& element, const std::string& text);
    // The network commands of the in-game pages (AbortGame, LeftGame,
    // SendInGameMsgTo; docs/re/58 sections 6.2 and 6.5), for the game.
    void set_ingame_net(std::function<void(const std::string&)> f) { ingame_net_ = std::move(f); }
    // A house voice requested by a menu (PlaySample S_MISSIONABORT), or "".
    std::string take_ingame_voice();
    long global(const std::string& name) const { return global_value(name); }
    std::string global_string(const std::string& name) const {
        auto it = globals_.find(name);
        return it == globals_.end() ? std::string() : it->second;
    }

    // The end-of-mission numbers for SCORE_SINGLE (docs/re/24 section 2.3).
    struct SideStats {
        bool active = false;
        int house = 0;
        int32_t units_lost = 0, buildings_lost = 0, spice = 0;
        bool row = false, ai = false;     // SCORE (skirmish): a row; an AI side ("Computer")
        int colour = 0;                   // the slot colour (NetColour<n>)
        int32_t units_killed = 0, buildings_killed = 0;
        uint8_t place = 0xff;             // finishing place 0x797b70
        std::string name;                 // a network player's name (0x798630 + s * 0x3c)
    };
    void set_score_stats(const std::array<SideStats, 8>& s) { score_stats_ = s; }
    void set_global(const std::string& name, long value) { globals_[name] = std::to_string(value); }
    void set_global_string(const std::string& name, const std::string& value) { globals_[name] = value; }
    // The skirmish settings (gNet*, docs/re/51 section 4.1) from / into our
    // settings file (the exe keeps them in dune2000.cfg).
    void load_net_settings(const platform::Settings& s);
    void store_net_settings(platform::Settings& s) const;

    // Saved games (docs/re/27). The game screen supplies the full state while
    // a mission runs; a save picked for loading is handed back to main.
    void set_game_saver(std::function<SaveGame()> f) { game_saver_ = std::move(f); }
    // The music page (SOUNDOPTIONS, docs/re/48): the current track, Play and Stop.
    void set_music_hooks(std::function<std::string()> current, std::function<void(const std::string&)> play,
                         std::function<void()> stop) {
        music_current_ = std::move(current);
        music_play_ = std::move(play);
        music_stop_ = std::move(stop);
    }
    std::optional<SaveGame> take_pending_load() {
        auto g = std::move(pending_load_);
        pending_load_.reset();
        return g;
    }
    // A loaded save's common block into the globals (FUN_004418b0).
    void apply_save_common(const SaveCommon& c, const std::string& mission);

    // ---- Network front end (docs/re/58) ----
    // The session's network game slot: the lobby pages create and use it;
    // StartGame leaves it with the start for the game screen.
    void set_network(std::unique_ptr<NetGame>* slot) { net_slot_ = slot; }
    // WIGNET_ERROR's OK during a game (WigNetError): the front end opens
    // MAINMENU instead of the lobby.
    bool take_wignet_exit() {
        const bool b = wignet_exit_;
        wignet_exit_ = false;
        return b;
    }
    // After a network game: the game's link is dropped; the socket and the
    // lobby client stay for IPXLOBBY.
    void network_game_over();

private:
    struct Menu {
        std::string name;
        ui::MenuInfo info;
        ui::Screen screen;
        ui::Rect bounds;  // union of element rects (CInterface +0x40..0x4c)
        std::vector<ui::AnimationPlayer> anims; // indexed like screen.elements
        std::vector<int> frames;
        std::vector<ui::ElementState> states;   // indexed like screen.elements
        bool movie_played = false;
        // Opened with gGameState 2: its images are the .bmp ones (FUN_00491330).
        bool main_loop = false;

        // Drawing as the exe does it: the screen is kept between frames and
        // a menu redraws its background / elements only when flagged.
        bool background_dirty = true;            // +0x50
        std::vector<char> dirty;                 // element +0x18
        std::vector<long> drawn_key;             // visual state last drawn
        std::vector<std::optional<ui::Surface>> saved_under; // CAnimation back image (+0x79c)
        std::optional<ui::Surface> background;   // modified copy (GLOBE only)
        std::unique_ptr<ui::Globe> globe;        // GLOBE only
        std::unique_ptr<ui::Credits> credits;    // CREDITS only (menu +0x34, docs/re/42)
    };
    struct NavCommand {
        std::string cmd, arg1, arg2;
    };

    void open_menu(const std::string& name);   // FUN_004734e0
    void close_top();                          // FUN_00473130
    void top_changed();                        // music + main-menu timers
    void process_navigation();                 // FUN_004738b0
    void start_transition();                   // FUN_00472730
    bool play_movie_if_any(Menu& m);
    // Runs an element's click commands (FUN_00482850); `m`/`self` give the
    // element for CheckIfValue / UnCheck. Navigation is deferred.
    void execute(const std::vector<std::string>& actions, Menu* m = nullptr, int self = -1);
    void run_init_commands(Menu& m, size_t index, uint64_t now); // FUN_00484000
    void handle_input();
    // Set-up page tooltips (FUN_004712c0, docs/re/55 section 4).
    void update_tooltip(uint64_t now);
    struct Tooltip {
        bool shown = false;
        ui::Rect rect{};
        std::string text;
    } tip_;
    std::string tip_remembered_; // 0x7956a0 (kept across menus)
    uint64_t tip_timer_ = 0;     // 0x7975c8
    void update_animations(uint64_t now);
    void invalidate(Menu& m);                         // FUN_0043ca50: redraw all
    bool draw_dirty(Menu& m, ui::Surface& out, bool top); // FUN_0043c4b0; true if it drew
    void draw_all_dirty(ui::Surface& out);
    void add_animation(Menu& m, const std::string& name, int x, int y, int w, int h,
                       const std::string& gfx, int fps, ui::AnimMode mode);
    void update_globe(Menu& m);
    void launch_mission(const std::string& mission);  // FUN_004752c0 (campaign part)
    void present();
    bool front_pass(); // one pass of run()'s loop after the events; false = stop
    // The mouse handlers' test (FUN_00437840 / FUN_00437600 / FUN_00437af0),
    // done in the message pump before the UI update: drops a refused press
    // or release, and sets right_list_.
    void gate_mouse();
    bool right_list_ = false;
    std::function<video::MovieResult(const std::filesystem::path&)> movie_hook_;

    long global_value(const std::string& name) const; // numbers, enum names, TRUE/FALSE

    // Mouse input switch (FUN_004706c0 / FUN_004706f0): switched off by a
    // menu open / close, a non-SKIP transition start, an MVE_ target, the
    // credits and the globe reveal; switched on by every UI update whose
    // transition step is not running (FUN_00472870). After switching on,
    // the next 10 queries still say "off".
    void set_input(bool on);
    bool input_allowed();

    render::Display& display_;
    const platform::GameData& gd_;
    ui::Assets assets_;
    audio::MusicPlayer music_;
    audio::SamplePlayer speech_;
    audio::SamplePlayer sfx_;      // PlaySample (UI clicks)
    campaign::CampaignTable campaign_;
    std::array<campaign::Region, 27> regions_;
    SDL_Texture* texture_ = nullptr;
    ui::Surface screen_;

    std::vector<std::unique_ptr<Menu>> stack_; // top = back(); max 20 slots
    std::optional<NavCommand> nav_;            // CUIManager +0x54/0x58/0x94
    ui::Transition transition_;
    std::map<std::string, std::string> globals_; // SetValue* targets
    bool quit_ = false;
    bool window_closed_ = false;

    // Mouse state for the top menu.
    float mouse_x_ = -1, mouse_y_ = -1;
    int hovered_ = -1, pressed_ = -1;
    bool mouse_down_ = false, mouse_up_ = false; // this frame
    bool mouse_held_ = false;
    bool mouse_double_ = false;                  // this press is a double-click's second (WM_LBUTTONDBLCLK)
    // Value bars (FUN_0048c640 / FUN_0048c9c0 / FUN_0048c450).
    int bar_drag_ = -1, bar_repeat_ = -1, bar_grab_ = 0;
    uint64_t bar_repeat_ms_ = 0;
    void bar_dirty(Menu& m, int i);
    void valuebar_down(Menu& m, int i, uint64_t now);
    void valuebar_held(Menu& m, uint64_t now);
    // In-game mode.
    // The cursor (FUN_00423a20): MOUSE.R16 shape 0x798540 (0 arrow, 2 over
    // a campaign-map choice), frame +1 every 3 loops, drawn while input is on.
    std::unique_ptr<render::SpriteBank> cursors_;
    ui::Surface frame_{640, 400};
    int cursor_type_ = 0, cursor_frame_ = 0, cursor_counter_ = 0;
    bool ingame_ = false;
    // SCORE_SINGLE (FUN_0043d820 / FUN_0043ec70 / FUN_0043f030); the numbers
    // live in campaign::ScoreCount (src/campaign/score.hpp).
    // SCORE_SINGLE (FUN_0043d820 / FUN_0043ec70 / FUN_0043f030).
    struct Score {
        bool on = false;
        campaign::ScoreCount count;
        std::array<std::string, 3> our_bar, their_bar;
    } score_;
    // 0x516270 / 0x516274: set by Esc / Tab on any score menu (+0x30, also
    // the skirmish SCORE) while not finished; reset only by the campaign
    // score set-up, so they outlive a menu.
    bool score_finished_ = false, score_skip_ = false;
    std::array<SideStats, 8> score_stats_{};
    void score_setup(Menu& m);
    void score_mp_setup(Menu& m);           // FUN_0043d7e0 / FUN_0043dcb0 (docs/re/56)
    bool score_step(Menu& m, uint32_t now);  // FUN_0043f030: false when done
    void score_update(Menu& m, uint64_t now); // FUN_0043ee80
    void score_draw_bars(ui::Surface& out);
    void draw_credits(ui::Credits& c, uint64_t now); // FUN_0043d520: draw, then step
    void set_box_text(Menu& m, const std::string& name, const std::string& text);
    void handle_keys(bool esc, bool tab, bool enter);
    // High-score table (docs/re/24 section 2.6): 6 slots of 0xDC bytes at
    // 0x515bb8, the first 5 kept in GameSave\hs.dat.
    campaign::HighScores hs_{};
    bool hs_table_ = false;         // 0x51626c
    void hs_load();                 // FUN_0043eee0
    void hs_save();                 // FUN_0043efd0
    void hs_build(Menu& m);         // FUN_0043fc90
    void add_element(Menu& m, ui::Element e, ui::ElementState s = {});
    int focused_edit(Menu& m) const; // the edit box with focus (+0xaac), or -1
    void edit_char(Menu& m, int i, char c);   // FUN_0048b4a0
    void edit_key(Menu& m, int i, SDL_Keycode key); // FUN_0043cdf0: left, right, backspace, delete
    void edit_changed(Menu& m, int i);
    // Saved games.
    std::function<SaveGame()> game_saver_;
    std::function<std::string()> music_current_;
    std::function<void(const std::string&)> music_play_;
    std::function<void()> music_stop_;
    std::optional<SaveGame> pending_load_;
    int forced_button_ = -1;                 // ForceButtonDown (500 ms)
    uint64_t forced_until_ = 0;
    bool right_up_ = false;
    int find_element(const Menu& m, const std::string& name) const;
    Menu* find_menu(const std::string& name);
    void key_event(const SDL_Event& ev, bool enter_too);
    void sync_text_input();
    void get_game_saves(Menu& m, int list);                 // FUN_00475a00
    void get_cd_tracks(Menu& m, int list);                  // FUN_00476590 (docs/re/48)
    void get_map_list(Menu& m, int list);                  // FUN_0047a450 (docs/re/51)
    void get_map_from_list(Menu& m, int list);             // FUN_00490bb0
    void set_text_key(Menu& m, const std::string& name, const std::string& key); // a text box's key, redrawn
    std::vector<std::string> cd_files_;                    // MUSIC*.aud bases, whole listing
    void list_select(Menu& m, int list, int selection);     // FUN_00487a00
    void list_mouse(Menu& m, int list, bool down, bool up, bool held); // FUN_00487af0 & co.
    void list_remove_selected(Menu& m, int list);           // FUN_00487420
    void check_save_game(const std::string& list, const std::string& edit); // FUN_00474cf0
    void save_game(const std::string& list, const std::string& edit, bool from_confirm); // FUN_00474e20
    void load_game(const std::string& list);                // FUN_00475740
    void delete_game(const std::string& list);              // FUN_004760c0
    void house_voice(const std::string& name);              // FUN_0046f900 (S_ / H_ / O_)
    bool closed_ = false;
    bool outside_down_ = false;               // 0x797590
    IngameEvent ingame_event_ = IngameEvent::None;
    std::string ingame_voice_;
    bool input_on_ = true;   // 0x4e7ad4
    int input_delay_ = 10;   // 0x797594 (the constructor switches input on)

    uint64_t eye_timer_ = 0, logo_timer_ = 0; // 0x797580 / 0x797584

    // ---- Network front end (menu_lobby.cpp, docs/re/58) ----
    std::unique_ptr<NetGame>* net_slot_ = nullptr;
    std::function<void(const std::string&)> ingame_net_;
    NetGame* net() const { return net_slot_ ? net_slot_->get() : nullptr; }
    static std::vector<std::string> element_actions(const ui::Element& e); // its command lines
    std::string chat_page_;                        // the page whose chat lines chat_shown_ holds
    bool open_network();                           // CreateWigNet: the socket and the lobby client
    // The lobby commands of execute(); false when `cmd` is not one of them.
    bool lobby_command(const std::string& cmd, const std::string& a, const std::string& b, Menu* m, int self);
    void lobby_init(Menu& m, size_t index, const std::string& cmd, const std::string& a); // EnterLobby, GetLobbyList, NoHighlight
    void lobby_page_opened(Menu& m);               // chat lines already there are not shown again
    void lobby_pump(uint64_t now);                 // FUN_0048e660, every menu pass
    void lobby_refresh(Menu& m);                   // FUN_00474b60(1): the refresh commands of every element
    void lobby_lists(Menu& m, int list, const std::string& game_list); // GetLobbyList / RefreshLobbyList
    void chat_list(Menu& m, int list);             // RefreshChatList
    void net_message(Menu& m, const std::string& key); // DisplayNetMessage (FUN_00492880)
    void add_response_line(Menu& m, const std::string& text, const std::string& colour);
    void alter_handicap(Menu& m, int list, int row); // FUN_00491c60
    bool no_change_options(Menu& m, int i);        // FUN_00482850's client rule
    void host_settings_changed();                  // a host setter + SendUIMsgToAll: the settings go out
    void client_settings_to_globals(Menu& m);      // messages 0xE..0x13, 6 on the client page
    void set_button(Menu& m, const std::string& name, bool enabled, const std::string& enabled_action,
                    const std::string& disabled_action); // Start / Accept (FUN_00492400 / FUN_00492110 ...)
    void join_game(Menu& m);                       // ClientWantsToJoin / JoinSession
    void probe_address(Menu& m, int self);         // ours: the address box (docs/re/58 section 7.2)
    struct ShownGame {
        net::Address host;
        std::vector<net::LobbyMember> members;
    };
    std::vector<ShownGame> shown_games_;           // gameList rows 2.. as last drawn
    std::vector<std::pair<uint32_t, uint32_t>> chat_shown_; // (sender, line) already in responseList
    std::string lobby_view_;                        // what the lists show; a change refreshes the page
    uint32_t client_rev_ = 0;                       // the client's state revision taken into the globals
    net::LobbyClient::Status client_status_ = net::LobbyClient::Status::Idle;
    bool wignet_pending_ = false; // no socket: WIGNET_ERROR on the next pass
    bool wignet_exit_ = false;
    uint64_t probe_due_ = 0;
    std::vector<std::unique_ptr<net::LobbyHost>> closing_hosts_; // cancelled games still telling their clients
    int net_port_ = net::kDefaultPort;
    std::vector<std::string> net_addresses_;       // remembered addresses, newest first
    int game_speed_ = 75;                          // the host's speed for the start
};

} // namespace d2k::app
