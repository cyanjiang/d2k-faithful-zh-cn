#pragma once
#include <array>
#include <map>
#include <filesystem>
// The in-game screen (M3 work in progress): the mission's map, units and
// buildings in the original layout (UIBB.R16 backdrop, 480x380 playfield),
// driven by the tick scheduler, with selection, move orders, edge scrolling
// and the original cursors. RE: docs/re/07, 09, 10.

#include <memory>
#include <string>
#include <vector>

#include "app/menu_runner.hpp"
#include "app/net_game.hpp"
#include "platform/settings.hpp"
#include "app/sidebar.hpp"
#include "audio/channels.hpp"
#include "audio/music_player.hpp"
#include "audio/sample_player.hpp"
#include "formats/r8.hpp"
#include "formats/templates.hpp"
#include "platform/game_data.hpp"
#include "render/display.hpp"
#include "render/sprites.hpp"
#include "sim/game_map.hpp"
#include "sim/game_setup.hpp"
#include "sim/rng.hpp"
#include "sim/tick_scheduler.hpp"
#include "sim/world.hpp"
#include "ui/assets.hpp"
#include "ui/surface.hpp"

struct SDL_Texture;

namespace d2k::app {

// The click puff (docs/re/46 section 2): UI state, never saved.
struct ClickPuff {
    int x = 0, y = 0;          // world pixels of the release point
    unsigned frame = 0xffff;   // >= EX_PUFF's frame count: free
    unsigned counter = 0;      // draws of the current frame
};

// State that outlives one game (exe globals): the shared rand() state, which
// a skirmish never reseeds (docs/re/52 section 1.3), and the tables
// FUN_00466140 fills when a game starts but not on a restart (0x797e1c).
struct GameSession {
    sim::Rng rng{1};                 // MSVC rand() starts at seed 1: srand (0x4b1800) is only called
                                     // by the game start FUN_004470a0 and the lobby FUN_00479b30
    bool restart = false;            // the next game is a RestartGame
    std::array<uint8_t, 64> thick{}; // 0x790510
    std::array<int, 16> jx{}, jy{};  // 0x503f88 / 0x502ed8
    uint32_t reveal_counter = 0;     // 0x54804c, never reset (docs/re/16 section 3.4)
    std::unique_ptr<NetGame> net;    // a network game: the lobby, then the running game's link
    // Globals no game start resets in these two cases (docs/re/46 section
    // 2.5): RestartGame skips FUN_00448470 (0x797e1c), so the puffs go on;
    // an in-mission load (FUN_004418b0 at once, no FUN_00448550) keeps the
    // puffs and the red health colour 0x51653c as well.
    enum class Carry { None, Restart, Load } carry = Carry::None;
    std::array<ClickPuff, 10> puffs{};
    bool red_bar_black = false;
};

class GameScreen {
public:
    // Load: a full save was chosen in the in-game LOAD menu (MenuRunner::
    // take_pending_load); LoadMission: a between-missions save (the front
    // end continues at LOADMISSION).
    enum class Result { Quit, Back, Won, Lost, Restart, Load, LoadMission };

    // `game_speed` = the game speed slider 0..100 (settings.ini game_speed).
    // `menus` hosts the in-game menus (docs/re/20); `settings` holds the
    // OPTIONS values, saved to `settings_file` when a slider changes.
    GameScreen(render::Display& display, const platform::GameData& gd, const std::string& mission,
               MenuRunner& menus, platform::Settings& settings, std::filesystem::path settings_file,
               GameSession& session);
    ~GameScreen();

    Result run();
    // Saved games: the simulation state plus this screen's own (camera,
    // crater jitter, bookmarks, sidebar page and scrolls, music track).
    std::vector<uint8_t> save_blob();
    bool load_blob(std::span<const uint8_t> data);
    // A saved game picked for loading: its state and *_GAMELOADED.
    bool load_saved(const SaveGame& g);
    // The end-of-mission numbers for the score screen (docs/re/24 section 2.3).
    std::array<MenuRunner::SideStats, 8> score_stats() const;

private:
    Result run_loop();
    void scroll_by(int dx, int dy);
    void handle_mouse();
    int cursor_type() const;
    // An in-game menu that stops the game (FUN_00470f90: mgr+0x110). Not
    // GAMEMESSAGE or PAGE_INGAME: the game runs and is drawn under them.
    bool menu_pauses() const;
    bool all_harvesters() const; // all selected units are harvesters (FUN_004a59..)
    bool infantry_only() const;
    bool all_kind(int kind) const; // FUN_004a59c0
    bool any_kind(int kind) const; // FUN_004a57f0
    int unit_at(int wx, int wy) const;     // FUN_004a5270 via World::click_unit (world pixel)
    int building_at(int cx, int cy) const; // FUN_004a5640 (World::building_at_cell)
    void draw();
    void draw_selection(bool building, size_t index); // one draw-list item's bracket (FUN_00428e30)
    void draw_shroud();             // FUN_00423fb0
    void present();                 // the cursor on a copy of the game surface, then the window
    // Tooltips (FUN_0044b5c0, drawn by FUN_00423b40; docs/re/11 section 11).
    void update_tooltip();
    void draw_tooltip(ui::Surface& out) const;
    int text_index(const std::string& key) const; // FUN_0048dc10: first match, 0 if none
    struct Tooltip {
        bool shown = false;
        std::string text, cost; // cost line "$%d" (empty = none)
        uint16_t colour = 0;
        formats::Rgb rgb{};
        ui::Rect box{}, cost_box{};
    } tip_;
    int tip_counter_ = 0;              // 0x51756c: loops with the mouse still
    int tip_last_x_ = -1, tip_last_y_ = -1; // 0x517560 / 0x517564
    std::array<uint32_t, 800> tile_text_{}; // 0x516770
    std::array<int, 16> crater_jx_{}, crater_jy_{}; // 0x503f88 / 0x502ed8
    // The sonic lens images (FUN_0042ada0 writes the bullet's shared image):
    // one 16 x 16 buffer per bullet image, from DATA.R16 at first use.
    std::map<int, std::array<uint16_t, 256>> lens_;
    void draw_craters();                         // FUN_00423d20 (the crater part)
    void compose();                 // the game view, sidebar and texts into screen_
    // FUN_00473130 / FUN_00473300 set both to 1; the paused loop draws the
    // game view while 0x5178ac is set (0x448fb7) and counts 0x5178b0 down
    // after the UI manager (0x449429), clearing 0x5178ac at 0.
    bool redraw_view_ = false;      // 0x5178ac
    int redraw_count_ = 0;          // 0x5178b0
    void menu_closed_redraw() {
        redraw_view_ = true;
        redraw_count_ = 1;
    }
    void open_ingame_menu();        // docs/re/20 section 1.1
    void apply_options();           // the OPTIONS globals -> settings, scheduler, audio
    void draw_top_left_text();      // FUN_004255e0: mission timer, messages
    void service_speech();          // FUN_0046faa0 (start the next queued voice)
    void draw_particles();          // FUN_0045d550
    void draw_bullets();            // FUN_00428070
    std::array<ClickPuff, 10> puffs_{}; // 0x516490 (docs/re/46 section 2)
    GameSession& session_;
    GameSession::Carry carry_ = GameSession::Carry::None; // what the previous game handed over
    bool red_bar_black_ = false;        // the full-table bug wrote 0 into 0x51653c
    int puff_type() const;
    void add_click_puff(int wx, int wy); // FUN_00456d50
    void draw_click_puffs();             // FUN_00428220
    void draw_lens(int image, size_t entry, int x, int y); // the sonic lens (FUN_0042ada0)
    bool turret_click_building(int bi); // docs/re/38 section 3.3
    void draw_effects(bool high);   // FUN_004282f0
    void draw_aircraft(int pass);   // FUN_0042a5d0
    void play_sounds();
    void play_effect(const std::string& name, int cx, int cy); // FUN_0046f540
    bool cache_sample(int channel, const std::string& name, bool in_rs); // FUN_0046f080
    void sync_channels();                                      // AIL_sample_status of the 6 channels
    void queue_voice(const std::string& name, bool mode0, int prio); // FUN_0046f820 after its limit
    void next_track();          // FUN_00481490

    render::Display& display_;
    const platform::GameData& gd_;
    ui::Assets assets_;
    audio::SamplePlayer speech_;                             // channel 0 (docs/re/59)
    std::array<std::unique_ptr<audio::SamplePlayer>, 5> fx_; // channels 1-5
    audio::Channels channels_;                               // what each channel plays, at which volume
    audio::VoiceQueue voices_;                               // the two speech rings
    audio::MusicPlayer music_;
    std::string track_;                // 0x797a98, without ".aud"
    int side_ = -1;                    // campaign house (gSideId), -1 = not a campaign mission
    sim::Rng& rng_;                    // the session's (shared with the front end)
    uint32_t& session_reveal_;         // GameSession::reveal_counter, written back when the game ends
    std::unique_ptr<sim::Game> game_;  // owns what the World refers to; declared before map_ / world_
    sim::ExeTables tables_;
    std::unique_ptr<sim::GameMap> map_;
    std::vector<formats::R8Image> tiles_;
    formats::Templates templates_;
    std::unique_ptr<render::SpriteBank> sprites_;
    std::unique_ptr<render::SpriteBank> cursors_;
    std::unique_ptr<sim::World> world_;
    std::unique_ptr<Sidebar> sidebar_;
    int mode_ = mode::kNormal; // 0x516438
    bool place_valid_ = false; // 0x51643d
    bool end_voice_ = false;  // MISSIONWON / MISSIONFAILED played (0x517400)
    int end_delay_ = -1;      // 0x516540 once the mission ended
    std::vector<uint64_t> tick_times_; // ticks in the last real second (0x6b9858)
    sim::TickScheduler scheduler_;
    std::vector<uint16_t> backdrop_; // UIBB.R16, 640x400
    ui::Surface screen_; // the game surface (0x5178b4), persistent
    ui::Surface frame_;  // screen_ plus the cursor, shown
    SDL_Texture* texture_ = nullptr;
    int scroll_x_ = 0, scroll_y_ = 0;
    int player_ = 0; // the human player's slot
    bool skirmish_ = false; // gGameType 1 (docs/re/51, 52)
    // A network game (gGameType 2, docs/re/57; docs/design/multiplayer.md):
    // the session's link, polled every loop pass, menus open or not.
    NetGame* net_ = nullptr;
    bool net_started_ = false;  // the start sync is done (tick 0 may run)
    bool net_alone_ = false;    // nobody else came: NO_FIND_PLAYERS
    uint32_t net_passes_ = 0;   // loop passes this second (0x798514)
    uint64_t net_second_ms_ = 0;
    void net_pass(uint64_t now_ms); // GetNetworkOrders, ProcessPulseSignal, pulses, the start sync
    bool sim_tick_due(uint64_t now_ms); // the tick pattern, and in a network game the lockstep's leave
    void sim_tick();                    // one World::tick with the network steps around it
    // The in-game network UI (docs/re/58 section 6).
    void net_ui();                      // chat, lost-player lines, MP_TIMEOUT, OUT_OF_SYNC, speed
    void net_menu_command(const std::string& cmd); // AbortGame, LeftGame, SendInGameMsgTo
    void net_line(const std::string& text);        // a message line as is (FUN_00425720)
    void net_chat_key(int k);           // F1..F6 (k = 0..5): GAMEMESSAGE
    void net_ally_key();                // A: order 0x1B (FUN_00459b50)
    void net_leave();                   // the leave order at the loop's end, acks waited for
    bool wignet_box_ = false;           // WIGNET_ERROR was opened (once)
    bool found_box_ = false;           // FOUND_PLAYERS is up (the start sync waits)
    void net_found_box();
    uint32_t logged_latency_ = 0;       // the last latency written to the log
    int quit_kind_ = 0;                // 0x797df8: 0 Abort / dropped someone, 1 surrender, 0xFF won
    int chat_target_ = -1;              // 0x797e18: the side the chat goes to, -1 = all
    bool oos_line_ = false;             // "Game is out of sync" shown
    bool oos_box_ = false;              // OUT_OF_SYNC opened
    bool defeated_said_ = false;        // "<name> has been defeated." sent
    bool surrendered_ = false;          // gLose set by Surrender
    bool net_left_ = false;             // the leave order went out
    bool net_win_counted_ = false;      // the win's end counter was set
    bool net_end_ = false;              // a tick entered the end block with the counter at 0
    bool sp_end_ = false;               // the same for a single-player game (0x449c9f / 0x449f36)
    bool alone_box_ = false;            // NO_FIND_PLAYERS opened
    std::string mission_;        // the mission file name, for saved games

    // Mouse (screen pixels) and the wndproc-style button events.
    int mouse_x_ = 0, mouse_y_ = 0;
    bool esc_key_ = false, space_key_ = false;
    std::vector<int> pressed_keys_;             // keydown[] events of this loop (0x798390)
    std::array<uint8_t, 10> group_said_{};      // 0x5165b0: the select voice once per press
    std::array<std::array<int, 2>, 4> bookmarks_{}; // F9-F12 (0x51645c)
    void handle_keys();                         // FUN_004433a0's hotkey block (docs/re/25)
    bool radar_cell(int& cx, int& cy) const;    // the map cell under the mouse on a working radar
    void set_camera(int x, int y, bool minus_one); // clamp to the map
    bool left_held_ = false, left_down_ = false, left_up_ = false, right_down_ = false, right_up_ = false;
    bool dragging_ = false;            // 0x516458
    int drag_x_ = 0, drag_y_ = 0;      // 0x516450 / 0x516454, world pixels
    int cursor_frame_ = 0, cursor_counter_ = 0; // 0x512d68 / 0x512d6c
    int edge_cursor_ = -1;             // the edge scroll's cursor this pass (FUN_004433a0), -1 = none
    void edge_scroll();                // FUN_004433a0 0x443d3e..0x443fb0: scroll and its cursor
    MenuRunner& menus_;
    platform::Settings& settings_;
    std::filesystem::path settings_file_;
};

} // namespace d2k::app
