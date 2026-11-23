#include "app/display_mode.hpp"
#include "app/game_screen.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "i18n/i18n.hpp"
#include "i18n/utf8.hpp"
#include "campaign/campaign_table.hpp"
#include "formats/file_io.hpp"
#include "formats/map.hpp"
#include "formats/pe_image.hpp"
#include "formats/templates.hpp"
#include "render/objects.hpp"
#include "render/terrain.hpp"

namespace d2k::app {

namespace {
constexpr int kWidth = 640, kHeight = 400;
constexpr uint64_t kFrameNs = 1'000'000'000 / 60; // loop rate: 60 Hz by decision
const ui::Rect kPlayfield{render::kPlayfieldX, render::kPlayfieldY, render::kPlayfieldWidth,
                          render::kPlayfieldHeight};
const ui::Rect kScreen{0, 0, kWidth, kHeight};

// Selection pieces, fixed DATA.R16 entries (docs/re/10-ingame-input.md §8).
constexpr size_t kBracketLeft = 5, kBracketRight = 6, kBracketBottomLeft = 7, kBracketBottomRight = 8;
constexpr size_t kBracketTop = 9; // + size class
constexpr int kBracketHeight[6] = {19, 24, 32, 64, 96, 32}; // 0x4e3e48

// Cursor types; MOUSE.R16 entry = type * 8 + frame (docs/re/10 §5).
constexpr int kCursorNormal = 0, kCursorMove = 1, kCursorAttack = 2, kCursorNoMove = 3, kCursorSelect = 5;
constexpr int kCursorWalkOnly = 0x20;

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Surface::fill clipped to a rect.
void fill_clipped(ui::Surface& s, ui::Rect r, const ui::Rect& clip, uint16_t c) {
    const int x0 = std::max(r.x, clip.x), y0 = std::max(r.y, clip.y);
    const int x1 = std::min(r.x + r.w, clip.x + clip.w), y1 = std::min(r.y + r.h, clip.y + clip.h);
    if (x1 > x0 && y1 > y0) s.fill({x0, y0, x1 - x0, y1 - y0}, c);
}
} // namespace

GameScreen::GameScreen(render::Display& display, const platform::GameData& gd, const std::string& mission,
                       MenuRunner& menus, platform::Settings& settings, std::filesystem::path settings_file,
                       GameSession& session)
    : session_(session), display_(display), gd_(gd), assets_(gd), speech_(gd), music_(gd), rng_(session.rng),
      session_reveal_(session.reveal_counter),
      scheduler_(settings.game_speed), screen_(kWidth, kHeight), frame_(kWidth, kHeight), menus_(menus),
      settings_(settings), settings_file_(std::move(settings_file)) {
    // Single player seeds rand() from the clock (FUN_004470a0). Game speed is
    // gLimitedModelPct (0x4e3b18, default 75), the saved setting (scheduler_).
    const std::string name = upper(mission);
    mission_ = name;
    // gSideId / gMissionNumber follow the mission being played (the exe sets
    // them on launch and from a saved game's header), so RECAP opens
    // IG<side><n>MIS also after --mission or a load.
    if (name.size() >= 2 && (name[0] == 'A' || name[0] == 'H' || name[0] == 'O') && std::isdigit(uint8_t(name[1]))) {
        menus_.set_global("gSideId", name[0] == 'A' ? 0 : name[0] == 'H' ? 1 : 2);
        menus_.set_global("gMissionNumber", std::atol(name.c_str() + 1));
        // The local player is slot gSideId (0x798544): Atreides 0,
        // Harkonnen 1, Ordos 2 (FUN_00442960 and the rest compare to it).
        player_ = name[0] == 'A' ? 0 : name[0] == 'H' ? 1 : 2;
    }
    // The full state for the SAVE menu (docs/re/27 section 4.3).
    // The music page (SOUNDOPTIONS, docs/re/48 A.5 / A.6): Play starts that
    // file now at volume 0x40 (not gCDVolume); Stop silences the music and
    // clears the track name.
    menus_.set_music_hooks([this]() { return track_; },
                           [this](const std::string& base) {
                               std::string lower = base;
                               for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
                               track_ = lower;
                               music_.play(track_ + ".aud");
                               music_.set_volume(0x40);
                           },
                           [this]() {
                               music_.stop();
                               track_.clear();
                           });
    menus_.set_game_saver([this]() {
        SaveGame g;
        g.mission = mission_;
        g.state = save_blob();
        g.common.music = track_;
        g.common.scroll_speed = uint32_t(settings_.scroll_speed);
        g.common.game_speed = uint16_t(settings_.game_speed);
        g.common.sfx_volume = uint32_t(settings_.sfx_volume);
        g.common.cd_volume = uint32_t(settings_.music_volume);
        return g;
    });
    // The game set-up order (FUN_00448550, docs/re/52 section 2.1).
    // 1. FUN_00466140 (not on a restart, 0x797e1c): 64 rand() & 3 bytes, the
    //    thick-spice pattern 0x790510, then the crater jitter tables,
    //    rand() % 12 - 12 (x, then y) x 16 (docs/re/26 section 4).
    if (!session.restart) {
        for (auto& p : session.thick) p = uint8_t(rng_.next() & 3);
        for (int k = 0; k < 16; ++k) {
            session.jx[size_t(k)] = int(rng_.next() % 12) - 12;
            session.jy[size_t(k)] = int(rng_.next() % 12) - 12;
        }
    }
    // RestartGame skips the puff reset FUN_00448470 (0x448b19: 0x797e1c), so
    // live puffs go on; the red colour is rebuilt anyway (FUN_0044acc0 at
    // 0x4485d7). An in-mission load keeps both (load_saved).
    carry_ = session.carry;
    session.carry = GameSession::Carry::None;
    if (carry_ == GameSession::Carry::Restart && session.restart) puffs_ = session.puffs;
    session.restart = false;
    crater_jx_ = session.jx;
    crater_jy_ = session.jy;
    // 2. FUN_004470a0: the campaign seeds rand() from the clock; a skirmish
    //    keeps the running state (docs/re/52 section 1.3); a network game
    //    seeds it with the host's seed (doc 57 section 1.9).
    net_ = session.net && session.net->started ? session.net.get() : nullptr;
    skirmish_ = !net_ && menus_.global("gGameType") == 1;
    if (net_) rng_.seed(net_->start.seed);
    else if (!skirmish_) rng_.seed(uint32_t(SDL_GetTicks()));
    // 3..: the sides, the mission and map files, the World, the start and
    //    the AI (sim::build_game, docs/design/multiplayer.md section 4.1).
    sim::GameSetup setup;
    setup.mission = name;
    setup.thick = session.thick;
    setup.difficulty = int(std::clamp(menus_.global("gDifficultyLevel"), 0L, 2L));
    if (net_) {
        // The host's settings and players; this machine's side is its join
        // order (doc 57 section 3.2).
        setup = net_->start.setup();
        setup.thick = session.thick;
        player_ = net_->start.local;
        menus_.set_global("gSideId", player_);
        menus_.set_global("gGameType", 2);
    } else if (skirmish_) {
        // The local player is side 0 (0x44a54e).
        player_ = 0;
        menus_.set_global("gSideId", 0);
        setup.type = sim::GameSetup::Type::Skirmish;
        setup.colour[0] = uint8_t(menus_.global("gNetPlayerColour") & 7);
        setup.house[0] = uint8_t(std::clamp(menus_.global("gNetPlayerSide"), 0L, 2L));
        setup.credits = int(menus_.global("gNetStartingCredits"));
        setup.ai_players = int(std::clamp(menus_.global("gNetAIPlayers"), 0L, 7L));
        setup.unit_count = int(std::clamp(menus_.global("gNetUnitCount"), 0L, 10L));
        setup.tech = int(std::clamp(menus_.global("gNetTechLevel"), 0L, 9L));
        setup.worms = int(std::clamp(menus_.global("gNetWorms"), 0L, 3L));
        setup.crates = menus_.global("gNetCrates") != 0;
    }
    setup.local = player_;
    setup.reveal_counter = session_reveal_; // 0x54804c is never reset (docs/re/16 section 3.4)
    game_ = sim::build_game(setup, rng_, [this](const std::string& p) -> std::optional<std::vector<uint8_t>> {
        auto f = gd_.find(p);
        if (!f) return std::nullopt;
        return formats::read_file(*f);
    });
    const auto& map = game_->map;
    const auto& mis = game_->mis;
    tables_ = game_->tables;
    templates_ = game_->templates;
    map_ = std::move(game_->game_map);
    world_ = std::move(game_->world);
    tile_text_ = game_->attributes.text;
    if (net_) {
        // The link for this game: the transport on the lobby's socket, the
        // lockstep on this World; every numbered order goes out.
        net_->transport = std::make_unique<net::Transport>(net_->socket, net_->start.transport());
        net::Lockstep::Config lc;
        lc.local = player_;
        lc.humans = int(net_->start.state.players.size());
        lc.data_hash = game_->data_hash;
        net_->lockstep = std::make_unique<net::Lockstep>(*world_, lc);
        world_->on_post = [this](const sim::Order& o) { net_->lockstep->sent(o); };
        scheduler_.set_network(true);
        // The host's speed (start message +0x22); a change in the options
        // is order 0x0B for everyone (doc 57 section 1.8, apply_options).
        scheduler_.set_speed(net_->start.state.settings.speed);
        settings_.game_speed = net_->start.state.settings.speed; // 0x4e3b18 = the host's value
        menus_.set_ingame_net([this](const std::string& cmd) { net_menu_command(cmd); });
    }
    if (SDL_strcasecmp(mis.tileset.c_str(), "BLOXXMAS") == 0) {
        // FUN_00462d10 in 16-bit mode (docs/re/51 section 9): "bloxxmas" has
        // no .r16, so the .r8 tiles are converted at load through
        // bin\Palette.bin (each 6-bit channel << 2, packed to the screen
        // format, 555 here); index 0 is not transparent and the R8 file's own
        // palettes are ignored.
        tiles_ = formats::parse_r8(formats::read_file(gd_.find("Data/" + mis.tileset + ".R8").value()));
        const auto pal = formats::read_file(gd_.find("Data/bin/PALETTE.BIN").value());
        std::array<uint16_t, 256> c16{};
        for (size_t i = 0; i < 256 && 3 * i + 2 < pal.size(); ++i) {
            const int r = (pal[3 * i] << 2) & 0xff, g = (pal[3 * i + 1] << 2) & 0xff, b = (pal[3 * i + 2] << 2) & 0xff;
            c16[i] = uint16_t((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3));
        }
        for (auto& t : tiles_) {
            if (t.bits_per_pixel != 8 || t.pixels.empty()) continue;
            std::vector<uint8_t> out(t.pixels.size() * 2);
            for (size_t i = 0; i < t.pixels.size(); ++i) {
                out[2 * i] = uint8_t(c16[t.pixels[i]]);
                out[2 * i + 1] = uint8_t(c16[t.pixels[i]] >> 8);
            }
            t.pixels = std::move(out);
            t.bits_per_pixel = 16;
            t.palette.reset();
        }
    } else {
        tiles_ = formats::parse_r8(formats::read_file(gd_.find("Data/" + mis.tileset + ".R16").value()));
    }

    auto bb = formats::read_file(gd_.find("Data/UIBB.R16").value());
    backdrop_.resize(size_t(kWidth) * kHeight);
    for (size_t i = 0; i < backdrop_.size() && 2 * i + 1 < bb.size(); ++i) {
        backdrop_[i] = uint16_t(bb[2 * i] | bb[2 * i + 1] << 8);
    }
    screen_.pixels() = backdrop_; // the game surface starts as UIBB and is never cleared (FUN_00460c80)

    sprites_ = std::make_unique<render::SpriteBank>(
        std::move(game_->data_images), formats::read_file(gd_.find("Data/bin/COLOURS.BIN").value()));
    cursors_ = std::make_unique<render::SpriteBank>(
        formats::parse_r8(formats::read_file(gd_.find("Data/MOUSE.R16").value())), std::span<const uint8_t>{});
    sidebar_ = std::make_unique<Sidebar>(gd_, assets_, templates_, *sprites_);
    sidebar_->set_net_name(menus_.global_string("gNetPlayerName"));

    // The start view (FUN_00448550 at 0x448a45, docs/re/25 A.8, docs/re/52
    // section 2.7): HomeView(gSideId, .., 0); the camera stays when the
    // side has nothing. All four F9-F12 bookmarks start at it (0x448a5b).
    {
        // The mouse position 0x4eb048 / 0x4eb04c is written only by the
        // window procedure's WM_MOUSEMOVE (0x4a3532 / 0x4a354a), never reset:
        // the game starts with the mouse where the front end left it. Starting
        // at (0, 0) until the first motion event edge-scrolled the view to
        // the top-left corner.
        float fx = 0, fy = 0;
        SDL_GetMouseState(&fx, &fy);
        const SDL_FPoint p = display_.window_to_screen(fx, fy);
        mouse_x_ = std::clamp(int(std::floor(p.x)), 0, kWidth - 1);
        mouse_y_ = std::clamp(int(std::floor(p.y)), 0, kHeight - 1);
        int vx, vy;
        if (world_->home_view(player_, vx, vy)) set_camera(vx, vy, true);
        for (auto& b : bookmarks_) b = {scroll_x_, scroll_y_};
    }

    // Music (docs/re/15-ingame-music.md): a campaign mission starts with its
    // campaign.uib track (+0xF4); anything else plays arakatak. The volume
    // is gCDVolume (0..100 on Miles' 0..127, 0x448d3a), the OPTIONS
    // slider's saved value (settings.ini, as the exe's dune2000.cfg).
    if (name.size() >= 2 && (name[0] == 'A' || name[0] == 'H' || name[0] == 'O') && name[1] >= '1' &&
        name[1] <= '9') {
        side_ = name[0] == 'A' ? 0 : name[0] == 'H' ? 1 : 2;
        const auto table = campaign::CampaignTable::parse(
            formats::read_file(gd_.find("Data/UI_DATA/campaign.uib").value()));
        track_ = table.at(side_, name[1] - '0').track;
    } else {
        track_ = "arakatak";
    }
    music_.play(track_ + ".aud");
    music_.set_volume(settings_.music_volume); // gCDVolume
    for (auto& f : fx_) f = std::make_unique<audio::SamplePlayer>(gd_);

    texture_ = SDL_CreateTexture(display_.renderer(), SDL_PIXELFORMAT_XRGB1555, SDL_TEXTUREACCESS_STREAMING,
                                 kWidth, kHeight);
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
    SDL_Log("game: %s, %dx%d, tileset %s, %zu units, %zu buildings, %d ticks per second", name.c_str(),
            map.width, map.height, mis.tileset.c_str(), world_->units().size(), world_->buildings().size(),
            scheduler_.tick_limit());
}

GameScreen::~GameScreen() {
    if (world_) session_reveal_ = world_->reveal_counter(); // the next game starts from it (0x54804c)
    menus_.set_ingame_net(nullptr);
    menus_.set_game_saver(nullptr);
    menus_.set_music_hooks(nullptr, nullptr, nullptr);
    if (texture_) SDL_DestroyTexture(texture_);
}

void GameScreen::scroll_by(int dx, int dy) {
    // FUN_004433a0 clamps to map_w*32 - 480 and map_h*32 - 380.
    const int max_x = std::max(0, map_->width() * render::kCellSize - render::kPlayfieldWidth);
    const int max_y = std::max(0, map_->height() * render::kCellSize - render::kPlayfieldHeight);
    scroll_x_ = std::clamp(scroll_x_ + dx, 0, max_x);
    scroll_y_ = std::clamp(scroll_y_ + dy, 0, max_y);
}

void GameScreen::edge_scroll() {
    // FUN_004433a0 0x443d3e..0x443fb0 (no scroll keys in retail, 0x4eb008 =
    // 1): vertical first, then horizontal, gScrollSize px per pass. "At the
    // limit" means this pass's step went past the edge and was clamped. The
    // cursor is set only in mode 0 and not while dragging, and the
    // horizontal part builds on the vertical one (0x443e5f / 0x443f2c):
    // at the x limit a moving up / down cursor stays as it is and a blocked
    // one turns into the blocked diagonal; off the x limit a moving up /
    // down cursor turns into the diagonal and a blocked one into the plain
    // side arrow.
    const int speed = settings_.scroll_speed; // gScrollSize 0x4eb030
    const int max_x = map_->width() * render::kCellSize - render::kPlayfieldWidth;
    const int max_y = map_->height() * render::kCellSize - render::kPlayfieldHeight;
    const bool set = mode_ == mode::kNormal && !dragging_;
    int c = -1;
    if (mouse_y_ >= 399) {
        scroll_y_ += speed;
        const bool lim = max_y < scroll_y_;
        if (lim) scroll_y_ = max_y;
        if (set) c = lim ? 0x1a : 0x12;
    } else if (mouse_y_ < 1) {
        scroll_y_ -= speed;
        const bool lim = scroll_y_ < 0;
        if (lim) scroll_y_ = 0;
        if (set) c = lim ? 0x16 : 0x0e;
    }
    const bool right = mouse_x_ >= 639 || (dragging_ && mouse_x_ > render::kPlayfieldWidth);
    if (right || mouse_x_ < 1) {
        bool lim;
        if (right) {
            scroll_x_ += speed;
            lim = max_x < scroll_x_;
            if (lim) scroll_x_ = max_x;
        } else {
            scroll_x_ -= speed;
            lim = scroll_x_ < 0;
            if (lim) scroll_x_ = 0;
        }
        if (set) {
            if (lim) {
                if (c == 0x1a) c = right ? 0x19 : 0x1b;
                else if (c == 0x16) c = right ? 0x17 : 0x1d;
                else if (c != 0x0e && c != 0x12) c = right ? 0x18 : 0x1c;
            } else {
                if (c == 0x12) c = right ? 0x11 : 0x13;
                else if (c == 0x0e) c = right ? 0x0f : 0x15;
                else c = right ? 0x10 : 0x14;
            }
        }
    }
    edge_cursor_ = c;
}

int GameScreen::unit_at(int wx, int wy) const {
    // The click / cursor lookup FUN_004a5270 (docs/re/10 "Hit test"): cell
    // based, with the cloak rules of its callers.
    return world_->click_unit(player_, wx, wy);
}

int GameScreen::building_at(int cx, int cy) const {
    return world_->building_at_cell(cx, cy); // FUN_004a5640
}

bool GameScreen::radar_cell(int& cx, int& cy) const {
    // docs/re/25 B.3: the map image only, strict bounds; needs a radar
    // building (kind 1) and power > 99. (0x51643c, the multiplayer map
    // toggle, is never on in the campaign.)
    const int ox = (128 - map_->width()) / 2, oy = (128 - map_->height()) / 2;
    if (!(497 + ox < mouse_x_ && mouse_x_ < 497 - ox + 128 && 8 + oy < mouse_y_ && mouse_y_ < 8 - oy + 128)) return false;
    if (sidebar_->map_toggle()) return false; // the score box is shown instead (docs/re/25 B.3)
    if (world_->player(player_).power_percent <= 99) return false;
    bool radar = false;
    for (const auto& b : world_->buildings()) {
        if (b.owner == player_ && !b.removed && templates_.building_records[size_t(b.record)].kind == 1) radar = true;
    }
    if (!radar) return false;
    cx = mouse_x_ - (497 + ox);
    cy = mouse_y_ - (8 + oy);
    return true;
}

void GameScreen::handle_mouse() {
    const int wx = mouse_x_ - render::kPlayfieldX + scroll_x_;
    const int wy = mouse_y_ - render::kPlayfieldY + scroll_y_;
    const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    const bool inside = kPlayfield.contains(mouse_x_, mouse_y_);

    // Right press: playfield mode 0 and the drag ends; then the sidebar
    // (build cells, scroll arrows), else deselect on the playfield or radar.
    if (right_down_) {
        mode_ = mode::kNormal;
        dragging_ = false;
        const bool radar = mouse_x_ >= 497 && mouse_x_ < 497 + 128 && mouse_y_ >= 8 && mouse_y_ < 8 + 128;
        if (!sidebar_->right_down(mouse_x_, mouse_y_, *world_, player_) && (inside || radar)) {
            world_->deselect_all();
        }
    }
    if (right_up_) sidebar_->right_up();
    int rx, ry;
    if (left_held_ && mode_ == mode::kNormal && !dragging_ && !world_->any_unit_selected(player_) && radar_cell(rx, ry)) {
        // Left held on the radar, no unit selected: every loop the cell under
        // the mouse becomes the view centre (0x798520 branch).
        scroll_x_ = std::clamp(rx * 32 - 240, 0, std::max(0, map_->width() * 32 - 480));
        scroll_y_ = std::clamp(ry * 32 - 190, 0, std::max(0, map_->height() * 32 - 380));
    }
    if (left_down_) {
        if (inside) {
            dragging_ = true;
            drag_x_ = wx;
            drag_y_ = wy;
        } else {
            sidebar_->left_down(mouse_x_, mouse_y_, *world_, player_, mode_);
        }
    }
    if (!left_up_) return;
    // The left release, in the order of FUN_004433a0 (0x444bfc..0x445d99):
    // the pressed sidebar buttons go up; a drag box (press in the playfield,
    // at least 4 px on both axes, 0x444cbc) selects and sets mode 0 in every
    // mode, cancelling placement, repair, sell and the strike modes; then a
    // release on the build lists (0x444d57) and on the radar (0x445010), in
    // every mode; only then the mode's own click (modes 1..5), else mode 0.
    const bool was_drag = dragging_;
    dragging_ = false;
    if (was_drag && std::abs(wx - drag_x_) >= 4 && std::abs(wy - drag_y_) >= 4) {
        sidebar_->left_up(-1, -1, *world_, player_, mode_); // any release lifts pressed buttons
        if (world_->box_select(player_, drag_x_, drag_y_, wx, wy)) world_->unit_response(false); // FUN_004a5ae0, FUN_0049f240(0)
        mode_ = mode::kNormal; // 0x444d48 / 0x4455a7: also when nothing was selected
        return;
    }
    sidebar_->left_up(mouse_x_, mouse_y_, *world_, player_, mode_);
    if (radar_cell(rx, ry)) {
        // Release on the radar with units selected: a move order to that cell
        // (FUN_00459450 at 0x4450d1), then the acknowledge (0x445d8f,
        // FUN_0049f240(1)); no click puff. The mode is kept.
        if (world_->any_unit_selected(player_)) {
            world_->post(sim::make_order(sim::order::kMove, player_, rx, ry));
            world_->unit_response(true);
        }
        return;
    }
    if (mode_ == mode::kPlace) {
        // docs/re/12 section 4: left release in mode 1 on the playfield
        // (0x4450ef); outside it the mode stays.
        if (mouse_x_ >= 480 || mouse_y_ <= 20) return;
        if (!place_valid_) {
            world_->voice(player_, "CANTBUILD");
            return;
        }
        const int cx = (scroll_x_ + mouse_x_) / 32;
        const int ry2 = mouse_y_ + scroll_y_ - 20;
        const int cy = ry2 < 32 ? 1 : ry2 / 32;
        auto& slot = world_->player(player_).building_slot;
        slot.placing = true;
        world_->post(sim::make_order(sim::order::kPlace, player_, slot.item, cx, cy)); // order 0x26
        mode_ = mode::kNormal;
        return;
    }
    if (mode_ == mode::kAirstrike || mode_ == mode::kDeathHand) {
        // docs/re/35 section 3.3 / docs/re/41 section 3.3: in the playfield,
        // every ready slot of the kind is marked used and order 0x30 goes to
        // the cell under the mouse (no shroud test); then mode 0. Outside
        // the playfield nothing happens and the mode stays (0x445470).
        if (mouse_x_ >= render::kPlayfieldWidth || mouse_y_ <= render::kPlayfieldY) return;
        const int kind = mode_ == mode::kAirstrike ? 9 : 10;
        bool any = false;
        for (auto& s : world_->player(player_).unit_slots) {
            if (s.item >= 0 && templates_.unit_records[size_t(s.item)].kind == kind &&
                s.progress == sim::kProgressDone && !s.used) {
                s.used = true;
                any = true;
            }
        }
        if (any) {
            const unsigned tx = unsigned(scroll_x_ + mouse_x_) >> 5;
            const unsigned ty = unsigned(scroll_y_ - render::kPlayfieldY + mouse_y_) >> 5;
            world_->post(sim::make_order(sim::order::kSpecial, player_, kind, tx, ty));
        }
        mode_ = mode::kNormal;
        return;
    }
    if (!inside) return;
    const int cx = wx / render::kCellSize, cy = wy / render::kCellSize;
    if (cx >= map_->width() || cy >= map_->height()) return;
    if (mode_ == mode::kRepair || mode_ == mode::kSell) {
        // docs/re/21 sections 2.1 / 3.1 (0x4451a4 / 0x445351). The mode stays
        // on after the click: neither branch writes 0x516438.
        const int bi = building_at(cx, cy);
        if (bi < 0) {
            // Repair mode on a unit (0x445261): an own damaged vehicle, with a
            // repair pad owned, goes to the nearest pad (order 0x21, pad -1),
            // with its acknowledge voice, then the flash (0x445332).
            const int ui = world_->unit_at_pixel(wx, wy, false); // FUN_004a5270(.., 0) at 0x445292
            if (mode_ != mode::kRepair || ui < 0) return;
            const auto& u = world_->units()[size_t(ui)];
            const auto& ur = templates_.unit_records[size_t(u.record)];
            bool pad = false;
            for (const auto& b : world_->buildings()) {
                pad = pad || (b.owner == player_ && !b.removed && templates_.building_records[size_t(b.record)].kind == 6);
            }
            if (pad && u.owner == player_ && u.hit_points < ur.hit_points && !ur.infantry) {
                world_->unit_voice(u.record, true);
                world_->units()[size_t(ui)].flash = 7; // FUN_0049f310
                world_->post(sim::make_order(sim::order::kRepairUnits, player_, u.index, 1));
            }
            return;
        }
        if (mode_ == mode::kRepair) {
            // Own, record flag 0x200, hp below the record's (0x445202..0x445254):
            // order 0x22, which toggles the repair, so a second click stops it.
            if (world_->can_repair_building(player_, bi)) {
                world_->post(sim::make_order(sim::order::kRepairBuilding, player_, bi));
            }
        } else if (world_->can_sell_building(player_, bi) && !world_->lost) {
            // Own, flags & 6 = 0 (0x4453e0); FUN_0045a1a0 posts nothing once
            // the local player has lost. Else only the dead capture cheat.
            world_->post(sim::make_order(sim::order::kSell, player_, bi));
        }
        return;
    }
    if (mode_ != mode::kNormal) return;
    // The order, then the click puff at the release point, then the
    // acknowledge (0x445d87, docs/re/46 section 2.1).
    auto move_with_puff = [&]() {
        world_->post(sim::make_order(sim::order::kMove, player_, cx, cy));
        add_click_puff(wx, wy);
        world_->unit_response(true);
    };
    if (world_->shrouded(cx, cy)) { // doc 16 section 5.1
        if (world_->any_unit_selected(player_)) move_with_puff();
        return;
    }

    const int ui = unit_at(wx, wy);
    const SDL_Keymod mods = SDL_GetModState();
    const bool ctrl = (mods & SDL_KMOD_CTRL) != 0, alt = (mods & SDL_KMOD_ALT) != 0;
    // FUN_004a58b0: an armed unit is selected (record +0x0c != 0xff).
    auto any_armed = [&]() {
        for (const auto& s : world_->units()) {
            if (s.owner == player_ && s.selected && templates_.unit_records[size_t(s.record)].weapon != 0xff) return true;
        }
        return false;
    };
    // A unit target with units selected (0x4458a4, docs/re/46 section 3): it
    // flashes; Alt on non-own infantry with any crusher selected (+0x35) and
    // a drivable cell (0x2000) moves there instead (no puff); else attack.
    auto attack_or_crush = [&](int ti) {
        auto& t = world_->units()[size_t(ti)];
        t.flash = 7; // FUN_0049f310
        bool crusher = false;
        for (const auto& s : world_->units()) {
            crusher |= s.owner == player_ && s.selected && templates_.unit_records[size_t(s.record)].crusher;
        }
        if (alt && templates_.unit_records[size_t(t.record)].infantry && t.owner != player_ && crusher &&
            (map_->at(cx, cy).flags & 0x2000)) {
            world_->post(sim::make_order(sim::order::kMove, player_, cx, cy));
        } else {
            world_->post(sim::make_order(sim::order::kAttackUnit, player_, t.index)); // order 0x14
        }
        world_->unit_response(true);
    };
    if (ctrl && world_->any_unit_selected(player_)) {
        // Force attack (docs/re/10 section 4, docs/re/46 section 4): Ctrl on
        // any unit attacks it (order 0x14, or the Alt crush move); on a
        // building: an own one needs an armed unit selected (else nothing),
        // then the saboteur / engineer flags apply (0x15); on an empty cell
        // the armed units attack the ground (0x16) with a puff, else nothing.
        if (ui >= 0) {
            attack_or_crush(ui);
            return;
        }
        if (const int fb = building_at(cx, cy); fb >= 0) {
            if (turret_click_building(fb)) return; // the selected turret first (docs/re/38 section 3.3)
            const auto& b = world_->buildings()[size_t(fb)];
            if (b.owner == player_ && !any_armed()) return; // 0x445c33
            if ((all_kind(4) && !(b.flags & 0x200)) || (all_kind(3) && !(b.flags & 0x400))) return;
            world_->buildings()[size_t(fb)].flash = 7;
            world_->post(sim::make_order(sim::order::kAttackBuilding, player_, fb));
            world_->unit_response(true);
            return;
        }
        if (!any_armed()) return; // 0x445d3a: nothing, no voice
        world_->post(sim::make_order(sim::order::kAttackGround, player_, cx, cy));
        add_click_puff(wx, wy);
        world_->unit_response(true);
        return;
    }
    if (ui >= 0) {
        const auto& u = world_->units()[size_t(ui)];
        if (u.owner == player_ && !ctrl) {
            // Own unit, no Ctrl (0x4456cf; Ctrl takes the branch below). Every
            // way ends in FUN_004a5a50(owner, idx, add), whose own tests (not
            // 0x140, not dying, state 8 only mid-step) may refuse the unit,
            // then the selection voice FUN_0049f240(0) (0x445854 / 0x445852 /
            // 0x44583b -> 0x445d91), except the MCV and Devastator cases.
            auto pick = [&](bool add) {
                if (!add) world_->view_unit(size_t(ui)); // the same tests, deselect all first
                else if (world_->selectable(u, player_)) world_->select_unit(size_t(ui), true);
                world_->unit_response(false);
            };
            if (shift) {
                pick(true); // toggle, then the voice (0x4456f2)
            } else if (u.selected) {
                // The selected unit clicked again (FUN_004433a0): an MCV
                // deploys when the block around the mouse cell is clear
                // (order 0x2F), else *_CANTDEPLOY. A Devastator not yet
                // counting down self-destructs (0x445789, docs/re/37 section
                // 1.3) with the selection voice. A saboteur cloaks once charged
                // (+0x29 >= 0xa0, docs/re/32 section 1.9), else it is selected
                // alone; the selection voice either way (0x44582a).
                const int kind = templates_.unit_records[size_t(u.record)].kind;
                if (kind == 6) {
                    if (world_->can_deploy(cx, cy)) world_->post(sim::make_order(sim::order::kDeploy, player_, u.index));
                    else world_->voice(player_, "CANTDEPLOY");
                } else if (kind == 7 && u.state != 0x1f) {
                    world_->post(sim::make_order(sim::order::kDeploy, player_, u.index));
                    world_->unit_response(false);
                } else if (kind == 4) {
                    if (u.load >= 0xa0) {
                        world_->post(sim::make_order(sim::order::kDeploy, player_, u.index));
                        world_->unit_response(false);
                    } else {
                        pick(false); // 0x44583b
                    }
                } else if (kind == 0xe) {
                    // A thumper deploys when the mouse cell is sand (0x4457c1,
                    // docs/re/54 section 4.3), else it is selected alone
                    // (0x44583b); the selection voice either way, no CANTDEPLOY.
                    if (world_->map().at(cx, cy).flags & sim::cell::kSand) {
                        world_->post(sim::make_order(sim::order::kDeploy, player_, u.index));
                        world_->unit_response(false);
                    } else {
                        pick(false);
                    }
                } else if (kind != 7) {
                    pick(false); // any other kind: selected alone, voice (0x445852)
                }
            } else {
                pick(false);
            }
        }
        // The click code treats every owner whose alliance value is not 0 (enemy
        // or neutral) as a target (docs/re/10 section 4).
        if (u.owner != player_ || ctrl) {
            // FUN_004433a0 (docs/re/10 section 4, docs/re/38 section 3.2): an
            // allied unit is selected; an enemy / neutral one (or Ctrl on
            // any) is attacked by the selected units; else a selected turret
            // in range of the clicked cell attacks it (flash, order 0x1E, no
            // voice); else it is selected to view it. The Alt crush move is
            // in attack_or_crush (docs/re/46 section 3).
            if (world_->alliance(player_, u.owner) == 0 && !ctrl) {
                world_->view_unit(size_t(ui));
            } else if (world_->any_unit_selected(player_)) {
                attack_or_crush(ui); // flash, attack or the Alt crush move, voice
            } else if (const int t = world_->selected_turret(player_);
                       t >= 0 && world_->building_in_range(world_->buildings()[size_t(t)], cx, cy, false)) {
                world_->units()[size_t(ui)].flash = 7; // FUN_0049f310
                world_->post(sim::make_order(sim::order::kTurretUnit, player_, u.index));
            } else {
                world_->view_unit(size_t(ui)); // FUN_004a5a50(owner, idx, 0), no voice
            }
        }
        return;
    }
    const int bi = building_at(cx, cy);
    if (bi >= 0) {
        // A building under the cursor (docs/re/10 section 3, docs/re/30, 38, 46).
        const auto& b = world_->buildings()[size_t(bi)];
        const auto& br = templates_.building_records[size_t(b.record)];
        // An enemy building, or Ctrl on any building (own and allied too):
        // the selected turret first (docs/re/38 section 3.3).
        if ((ctrl || (b.owner != player_ && world_->alliance(player_, b.owner) != 0)) && turret_click_building(bi)) {
            return;
        }
        if (b.owner != player_ && world_->alliance(player_, b.owner) != 0 && world_->any_unit_selected(player_)) {
            // docs/re/30 section 2.1: all saboteurs need flag 0x200, all
            // engineers 0x400; mixed groups attack; the building flashes
            // (FUN_0049f320). A failed flag test ends the click with no voice
            // (0x445c58 / 0x445c9c jump to 0x445d99, past the acknowledge).
            if ((all_kind(4) && !(b.flags & 0x200)) || (all_kind(3) && !(b.flags & 0x400))) return;
            world_->buildings()[size_t(bi)].flash = 7;
            world_->post(sim::make_order(sim::order::kAttackBuilding, player_, bi)); // order 0x15
            world_->unit_response(true);
            return;
        }
        if (b.owner == player_ && all_harvesters() && br.kind == 4) {
            world_->post(sim::make_order(sim::order::kDock, player_, bi)); // order 0x17
            world_->unit_response(true);
            world_->buildings()[size_t(bi)].flash = 7; // after the acknowledge (0x445a8e)
            return;
        }
        if (b.owner == player_ && br.kind == 6 && !infantry_only()) {
            // Own repair pad with a vehicle selected (doc 10 section 3): order
            // 0x21 for the selected vehicles, the acknowledge, a flash.
            world_->post(sim::make_order(sim::order::kRepairUnits, player_, bi));
            world_->unit_response(true);
            world_->buildings()[size_t(bi)].flash = 7; // FUN_0049f320, after the acknowledge
            return;
        }
        // Own, allied, or (with nothing selected) enemy buildings with record
        // flag 0x200 are selected (docs/re/10 section 3), unless building up or
        // selling. The own selected building again: set primary (FUN_00459ea0).
        // With Ctrl (nothing selected, no turret) a building is only selected,
        // never set primary (0x445cf3, docs/re/46 section 4).
        if (!(br.flags & 0x200)) return;
        if (b.owner == player_ && b.selected && !ctrl) {
            if (world_->can_be_primary(player_, bi) && !(b.flags & 0x80)) {
                world_->post(sim::make_order(sim::order::kPrimary, player_));
                world_->voice(player_, "PRIMARY");
            }
            return;
        }
        world_->select_building_checked(size_t(bi)); // FUN_004a5ab0
        return;
    }
    if (world_->any_unit_selected(player_)) move_with_puff();
}

bool GameScreen::turret_click_building(int bi) {
    // docs/re/38 section 3.3: with a selected turret, a building click (enemy,
    // or Ctrl on any) orders only the turret: in range of the building's
    // closest cell -> flash, order 0x1F; else select it if record flag 0x200;
    // no voice. False when no turret is selected.
    const int t = world_->selected_turret(player_);
    if (t < 0) return false;
    int cx, cy;
    auto& b = world_->buildings()[size_t(bi)];
    if (world_->building_in_range_of(world_->buildings()[size_t(t)], bi, cx, cy)) {
        b.flash = 7; // FUN_0049f320
        world_->post(sim::make_order(sim::order::kTurretBuilding, player_, bi));
    } else if (templates_.building_records[size_t(b.record)].flags & 0x200) {
        world_->select_building_checked(size_t(bi));
    }
    return true;
}

bool GameScreen::infantry_only() const {
    // FUN_004a5940 is "a non-infantry unit is selected"; this is its negation.
    for (const auto& u : world_->units()) {
        if (u.owner == player_ && u.selected && !templates_.unit_records[size_t(u.record)].infantry) return false;
    }
    return true;
}

bool GameScreen::all_kind(int kind) const {
    // FUN_004a59c0: at least one selected unit, all of the kind.
    bool any = false;
    for (const auto& u : world_->units()) {
        if (u.owner != player_ || !u.selected) continue;
        if (templates_.unit_records[size_t(u.record)].kind != kind) return false;
        any = true;
    }
    return any;
}

bool GameScreen::any_kind(int kind) const {
    for (const auto& u : world_->units()) {
        if (u.owner == player_ && u.selected && templates_.unit_records[size_t(u.record)].kind == kind) return true;
    }
    return false;
}

bool GameScreen::all_harvesters() const {
    bool any = false;
    for (const auto& u : world_->units()) {
        if (u.owner != player_ || !u.selected) continue;
        if (!world_->is_harvester(u)) return false;
        any = true;
    }
    return any;
}

bool GameScreen::menu_pauses() const {
    return menus_.ingame_active() && !menus_.ingame_has("GAMEMESSAGE") && !menus_.ingame_has("PAGE_INGAME");
}

int GameScreen::cursor_type() const {
    if (mode_ == mode::kPlace || menu_pauses()) return kCursorNormal; // the arrow, also at the edges (doc 12)
    // The edge cursor of this pass's scroll (mode 0, not dragging; it
    // replaces the cursor chosen before it, edge_scroll()).
    if (edge_cursor_ >= 0 && mode_ == mode::kNormal && !dragging_) return edge_cursor_;
    if (!kPlayfield.contains(mouse_x_, mouse_y_)) {
        int rx, ry; // cursor 6 over a working radar with units selected (doc 10)
        return world_->any_unit_selected(player_) && radar_cell(rx, ry) ? 6 : kCursorNormal;
    }
    if (mode_ == mode::kAirstrike) return 9; // the airstrike target ring, any cell (docs/re/35 section 3.2)
    if (mode_ == mode::kDeathHand) return 0x1e; // the skull, any cell (docs/re/41 section 3.2)
    const int wx = mouse_x_ - render::kPlayfieldX + scroll_x_, wy = mouse_y_ - render::kPlayfieldY + scroll_y_;
    const int cx = wx / render::kCellSize, cy = wy / render::kCellSize;
    if (cx >= map_->width() || cy >= map_->height()) return kCursorNormal;
    if (mode_ == mode::kRepair || mode_ == mode::kSell) {
        // docs/re/10 section 5. The mode cursors come before the shroud
        // test (0x4434f5 branches on mode != 0 first), so they show over
        // shroud too. Repair (0x443b44): an own building with hp below the
        // record's and record flag 0x200 -> 0xb; sell: an own building -> 10.
        const int bi = building_at(cx, cy);
        const bool own = bi >= 0 && world_->buildings()[size_t(bi)].owner == player_;
        if (mode_ == mode::kSell) return own ? 10 : 7;
        if (own && world_->can_repair_building(player_, bi)) return 0xb;
        if (bi < 0) {
            // No building (0x443b9d, docs/re/10 "Hit test"): the unit at the
            // cell's corner pixel, skip_cloaked 0; 0xb for an own damaged
            // vehicle when a repair pad (kind 6) is owned.
            const int ui = world_->unit_at_pixel(cx * render::kCellSize, cy * render::kCellSize, false);
            if (ui >= 0) {
                const auto& u = world_->units()[size_t(ui)];
                const auto& ur = templates_.unit_records[size_t(u.record)];
                bool pad = false;
                for (const auto& b : world_->buildings()) {
                    pad = pad || (b.owner == player_ && !b.removed && templates_.building_records[size_t(b.record)].kind == 6);
                }
                if (pad && u.owner == player_ && u.hit_points < ur.hit_points && !ur.infantry) return 0xb;
            }
        }
        return 8;
    }
    if (world_->shrouded(cx, cy)) return world_->any_unit_selected(player_) ? kCursorMove : kCursorNormal;

    const auto& units = world_->units();
    const bool selected = world_->any_unit_selected(player_);
    bool armed = false, infantry = false;
    for (const auto& s : units) {
        if (s.owner != player_ || !s.selected) continue;
        const auto& r = templates_.unit_records[size_t(s.record)];
        armed |= r.weapon != 0xff;
        infantry |= r.infantry;
    }
    const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;

    // FUN_004433a0 (mode 0), docs/re/10 section 5 as corrected in doc 19.
    const uint32_t f = map_->at(cx, cy).flags;
    const int cell_owner = int(f & 7);
    auto turret_or = [&](int bi, int other) { // 0x400 cells: walls and turrets
        if (bi >= 0 && templates_.building_records[size_t(world_->buildings()[size_t(bi)].record)].kind == 0x10) {
            return kCursorSelect;
        }
        return other;
    };
    if (!selected) {
        if (const int t = world_->selected_turret(player_); t >= 0) { // docs/re/38 section 2
            if (!(f & 0x3f8)) return kCursorNormal; // no ground attack
            const bool ctrl_held = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
            const auto& tb = world_->buildings()[size_t(t)];
            const int sb = (f & 0x10) ? building_at(cx, cy) : -1;
            if (world_->alliance(player_, cell_owner) != 0 || ctrl_held) {
                if (sb >= 0) {
                    const auto& b = world_->buildings()[size_t(sb)];
                    if (b.selected && b.owner == player_) return kCursorNormal;
                    int bx, by;
                    return world_->building_in_range_of(tb, sb, bx, by) ? kCursorAttack : kCursorNormal;
                }
                return world_->building_in_range(tb, cx, cy, false) ? kCursorAttack : kCursorNormal; // the mouse cell
            }
            if (!(f & 0x400)) return kCursorSelect;
            if (sb >= 0 && templates_.building_records[size_t(world_->buildings()[size_t(sb)].record)].kind == 0x10) {
                return kCursorSelect;
            }
            return kCursorNormal; // a wall
        }
        if (!(f & 0x3f8)) return kCursorNormal;
        if (const int sb = building_at(cx, cy); sb >= 0 && unit_at(wx, wy) < 0) {
            // The own selected building itself: 0xc when it can become primary.
            const auto& b = world_->buildings()[size_t(sb)];
            if (b.selected && b.owner == player_) {
                return world_->can_be_primary(player_, sb) && !(b.flags & 0x80) ? 0xc : kCursorNormal;
            }
        }
        if (f & 0x400) return turret_or(building_at(cx, cy), kCursorNormal);
        if (cell_owner == player_ || !(f & 0x3e8)) return kCursorSelect;
        // FUN_004a5270(cell * 32, .., 1) at 0x443b02: found or not only.
        return world_->unit_at_pixel(cx * render::kCellSize, cy * render::kCellSize, true) >= 0 ? kCursorSelect
                                                                                                : kCursorNormal;
    }
    const int ui = unit_at(wx, wy);
    const bool ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
    // A unit as a target (docs/re/46 section 3.1): Alt on non-own infantry
    // with a crusher selected (+0x35) and a drivable cell -> 1 (crush move),
    // else 2 if armed (FUN_004a58b0), else 3; without Alt the armed test
    // skips the target itself (FUN_004a58f0).
    auto unit_target_cursor = [&](int ti) {
        const auto& t = units[size_t(ti)];
        if ((SDL_GetModState() & SDL_KMOD_ALT) && templates_.unit_records[size_t(t.record)].infantry &&
            t.owner != player_) {
            bool crusher = false;
            for (const auto& s : units) {
                crusher |= s.owner == player_ && s.selected && templates_.unit_records[size_t(s.record)].crusher;
            }
            if (crusher && (f & 0x2000)) return kCursorMove;
            return armed ? kCursorAttack : kCursorNoMove;
        }
        bool armed_other = false;
        for (const auto& s : units) {
            armed_other |= &s != &t && s.owner == player_ && s.selected &&
                           templates_.unit_records[size_t(s.record)].weapon != 0xff;
        }
        return armed_other ? kCursorAttack : kCursorNoMove;
    };
    if (ctrl) { // force attack: any unit or building, or the ground with an armed unit
        if (ui >= 0) return unit_target_cursor(ui);
        if (const int fb = building_at(cx, cy); fb >= 0) {
            // docs/re/46 section 4.1: the saboteur / engineer cursors need a
            // building that is not own.
            const auto& b = world_->buildings()[size_t(fb)];
            if (armed) return kCursorAttack;
            if (any_kind(4)) return (b.owner != player_ && (b.flags & 0x200)) ? 0x1f : kCursorNoMove;
            if (any_kind(3)) return (b.owner != player_ && (b.flags & 0x400)) ? 4 : kCursorNoMove;
            return kCursorNoMove;
        }
        if (armed) return kCursorAttack;
    }
    if (ui >= 0) {
        const auto& u = units[size_t(ui)];
        if (u.owner != player_ || !u.selected) {
            if (u.owner != player_ && world_->alliance(player_, u.owner) == 0) return kCursorSelect; // allied
            if (u.owner == player_) return kCursorSelect;
            return unit_target_cursor(ui);
        }
        if (shift) return kCursorSelect;
        if (templates_.unit_records[size_t(u.record)].kind == 6) return world_->can_deploy(cx, cy) ? 0xc : 0xd;
        if (templates_.unit_records[size_t(u.record)].kind == 7) return 0xc; // always, counting down too (0x4435d2)
        if (templates_.unit_records[size_t(u.record)].kind == 4) return u.load >= 0xa0 ? 0xc : 0xd;
        // A thumper: deploy over sand, else no-deploy (docs/re/54 section 4.3).
        if (templates_.unit_records[size_t(u.record)].kind == 0xe) return (world_->map().at(cx, cy).flags & sim::cell::kSand) ? 0xc : 0xd;
        return kCursorNormal;
    }
    const int bi = building_at(cx, cy);
    if (bi >= 0) {
        const auto& b = world_->buildings()[size_t(bi)];
        const auto& r = templates_.building_records[size_t(b.record)];
        if (b.owner == player_) {
            if (r.kind == 6 && !infantry_only()) return 4; // repair pad
            if (r.kind == 4 && all_harvesters()) return 4; // dock
            return (f & 0x400) ? turret_or(bi, kCursorNoMove) : kCursorSelect;
        }
        if (world_->alliance(player_, b.owner) == 0) return (f & 0x400) ? turret_or(bi, kCursorNoMove) : kCursorSelect;
        // docs/re/30 section 2.1 (FUN_004a57f0: any selected unit of a kind).
        if (armed) return kCursorAttack;
        if (any_kind(4)) return (b.flags & 0x200) ? 0x1f : kCursorNoMove;
        if (any_kind(3)) return (b.flags & 0x400) ? 4 : kCursorNoMove;
        return kCursorNoMove;
    }
    if ((f & sim::cell::kVehicles) && (f & sim::cell::kInfantry)) {
        return (f & sim::cell::kSpiceLevelMask) && all_harvesters() ? kCursorAttack : kCursorMove;
    }
    if ((f & sim::cell::kInfantry) && infantry) return kCursorWalkOnly;
    return kCursorNoMove;
}

int GameScreen::puff_type() const {
    // EX_PUFF: Templates.bin special id 0x39 (0x6d6111).
    const auto* ids = templates_.block(0x6d60d8);
    return ids && ids->bytes.size() > 0x39 && ids->bytes[0x39] != 0xff ? int(ids->bytes[0x39]) : -1;
}

void GameScreen::add_click_puff(int wx, int wy) {
    // FUN_00456d50 (docs/re/46 section 2.2): the first free slot (frame >=
    // frames), else the one with the largest frame (first of equals). On that
    // reuse the exe clears index 10's counter, which is the red health-bar
    // colour 0x51653c: red bars turn black until the next mission (kept).
    const int type = puff_type();
    if (type < 0) return;
    const unsigned frames = unsigned(templates_.art.explosion_frames[size_t(type)]);
    for (auto& p : puffs_) {
        if (p.frame >= frames) {
            p = ClickPuff{wx, wy, 0, 0};
            return;
        }
    }
    size_t best = 0;
    for (size_t i = 1; i < puffs_.size(); ++i) {
        if (puffs_[i].frame > puffs_[best].frame) best = i;
    }
    puffs_[best].x = wx;
    puffs_[best].y = wy;
    puffs_[best].frame = 0; // the counter is kept
    red_bar_black_ = true;
}

void GameScreen::draw_click_puffs() {
    // FUN_00428220: no tick update; each draw shows the frame (darken blit,
    // as an explosion at height 0) and every 3rd draw advances it.
    const int type = puff_type();
    if (type < 0) return;
    const unsigned frames = unsigned(templates_.art.explosion_frames[size_t(type)]);
    for (auto& p : puffs_) {
        if (p.frame >= frames) continue;
        const size_t entry = size_t(templates_.art.explosion_first[size_t(type)]) + p.frame;
        sprites_->draw_blit(screen_, entry, p.x - scroll_x_ + render::kPlayfieldX, p.y + render::kPlayfieldY - scroll_y_,
                            -1, render::Blit::Darken, kPlayfield);
        if (++p.counter > 2) {
            p.counter = 0;
            ++p.frame;
        }
    }
}

void GameScreen::draw_effects(bool high) {
    // FUN_004282f0: explosions below 20 px (pass 0) or from 20 px (pass 1),
    // per player in list order, at (x, y - height), image (type, frame).
    for (const auto& e : world_->effects) {
        if ((e.height >= 20) != high) continue;
        if (e.flags & 0x820) continue; // hidden / waiting for the start delay
        const size_t entry = size_t(templates_.art.explosion_first[size_t(e.explosion)] + e.frame);
        render::Blit blit = render::Blit::Keyed;
        if (e.flags & 2) blit = render::Blit::Darken;
        else if (e.flags & 4) blit = render::Blit::Brighten;
        else if (e.flags & 0x10) blit = render::Blit::Translucent;
        else if (e.flags & 0x200) blit = render::Blit::Additive;
        // The red / green tints (0x40 / 0x80) are not drawn: no explosion
        // type in the 1.06 Templates.bin has them (test in test_combat.cpp).
        sprites_->draw_blit(screen_, entry, e.x - scroll_x_ + render::kPlayfieldX,
                            e.y - e.height + render::kPlayfieldY - scroll_y_, (e.flags & 8) ? world_->colour_of(e.owner) : -1, blit,
                            kPlayfield);
    }
}

void GameScreen::draw_aircraft(int pass) {
    // FUN_0042a5d0 (doc 14 section 5.5).
    auto image = [&](const sim::Unit& u, int record) -> long {
        const auto& r = templates_.unit_records[size_t(record)];
        if (r.base_element < 0 || size_t(r.base_element) >= templates_.art.unit.size()) return -1;
        const auto& el = templates_.art.unit[size_t(r.base_element)];
        if (el.rotations <= 0) return -1;
        const int frame = std::min<int>(u.frame, el.frames - 1);
        return el.first + frame * el.rotations + r.facing_to_rotation[u.facing & 31] % el.rotations;
    };
    auto sx = [&](const sim::Unit& u) { return (u.x >> 16) - scroll_x_ + render::kPlayfieldX; };
    auto gy = [&](const sim::Unit& u) { return (u.y >> 16) - scroll_y_ + render::kPlayfieldY; };
    if (pass == 0) {
        for (const auto& u : world_->units()) {
            if (!(u.flags & 0x140) || (u.flags & 0x200000)) continue;
            const long img = image(u, u.record);
            if (img < 0) continue;
            if ((u.flags & 0x100) && !(u.flags & 0x40)) { // carried unit
                sprites_->draw(screen_, size_t(img), sx(u), gy(u) - ((u.altitude >> 16) & 0xff), world_->colour_of(u.owner), kPlayfield);
                continue;
            }
            if ((u.flags & 0x40) && !(u.flags & 0x20000)) { // shadow at the ground point
                sprites_->draw_blit(screen_, size_t(img), sx(u), gy(u), -1, render::Blit::Shadow, kPlayfield);
            }
            if ((u.flags & 0x20000000) && u.delivery >= 0) { // delivery cargo, 10 px lower
                const auto& d = world_->deliveries(u.owner)[size_t(u.delivery)];
                // Nothing clears 0x20000000, so after the drop the exe reads
                // record 0xfe here: its Templates.bin bytes are zero (element
                // 0, rotation table 0), i.e. the first LIGHT INF image with
                // the carryall's frame (doc 14 section 5.5).
                if (!d.records.empty() && (d.records[0] >= 0 || d.records[0] == -2)) {
                    long c = -1;
                    if (d.records[0] >= 0) {
                        c = image(u, d.records[0]);
                    } else if (!templates_.art.unit.empty() && templates_.art.unit[0].rotations > 0) {
                        const auto& el = templates_.art.unit[0];
                        c = el.first + std::min<int>(u.frame, el.frames - 1) * el.rotations;
                    }
                    if (c >= 0) {
                        sprites_->draw(screen_, size_t(c), sx(u), gy(u) - (((u.altitude - 10 * 0x10000) >> 16) & 0xff),
                                       world_->colour_of(u.owner), kPlayfield);
                    }
                }
            }
        }
    }
    for (const auto& u : world_->units()) {
        if (!(u.flags & 0x40) || (u.flags & 0x200000)) continue;
        if (((u.altitude >> 16) >= 20) != (pass == 1)) continue;
        const long img = image(u, u.record);
        if (img >= 0) {
            sprites_->draw(screen_, size_t(img), sx(u), gy(u) - ((u.altitude >> 16) & 0xff), world_->colour_of(u.owner), kPlayfield);
        }
    }
}

void GameScreen::draw_particles() {
    // FUN_0045d550: one pixel at (round(x), round(y)), not on full shroud.
    // Colour: the tile pixel stored when the particle was made, darkened by
    // FUN_0045d650(pixel, 0.85f): for a 555 surface (0x798384 == 0xcaca,
    // our screen format) r = p >> 10 & 31, g = p >> 5 & 31, b = p & 31; each
    // is FILD, FMUL by the float 0.85f, truncated (_ftol) and packed back as
    // r << 10 | g << 5 | b (bit 15 dropped). docs/re/17 section 4.1.
    auto& px = screen_.pixels();
    for (const auto& p : world_->particles) {
        const int x = int(std::lround(p.x)), y = int(std::lround(p.y));
        const int cx = x >> 5, cy = y >> 5;
        if (cx < 0 || cy < 0 || cx >= map_->width() || cy >= map_->height() || world_->shrouded(cx, cy)) continue;
        const int sx = x - scroll_x_ + render::kPlayfieldX, sy = y - scroll_y_ + render::kPlayfieldY;
        if (!kPlayfield.contains(sx, sy)) continue;
        uint16_t c = 0;
        if (p.tile >= 0 && size_t(p.tile) < tiles_.size() && !tiles_[size_t(p.tile)].pixels.empty()) {
            const auto& img = tiles_[size_t(p.tile)];
            const size_t i = size_t((p.pixel >> 5) & 31) * img.width + size_t(p.pixel & 31);
            if (img.bits_per_pixel == 16 && 2 * i + 1 < img.pixels.size()) {
                c = uint16_t(img.pixels[2 * i] | img.pixels[2 * i + 1] << 8);
            }
        }
        // x87: int * 0.85f on the FPU stack, then truncation. In double the
        // product is exact (5-bit channel times a 24-bit mantissa), as there.
        auto dk = [&](int s) { return uint16_t(int(double((c >> s) & 0x1f) * double(0.85f)) << s); };
        px[size_t(sy) * 640 + size_t(sx)] = uint16_t(dk(10) | dk(5) | dk(0));
    }
}

void GameScreen::draw_bullets() {
    // FUN_00428070: after the objects, under the shroud.
    for (const auto& b : world_->bullets) {
        if (b.delay > 0 || b.dead) continue;
        const auto& br = templates_.bullets[size_t(b.type)];
        const size_t entry = size_t(templates_.art.bullet_first[br.image % 64] + b.frame);
        const int sx = (b.x >> 16) - scroll_x_ + render::kPlayfieldX;
        const int gy = (b.y >> 16) - scroll_y_ + render::kPlayfieldY;
        if (b.flags & 0x800) { // the sonic lens (docs/re/36 section 4)
            draw_lens(br.image % 64, entry, sx, gy - (b.height >> 16));
            continue;
        }
        if (b.flags & 2) sprites_->draw_blit(screen_, entry, sx, gy, -1, render::Blit::Shadow, kPlayfield);
        sprites_->draw_blit(screen_, entry, sx, gy - (b.height >> 16), world_->colour_of(b.owner),
                            (b.flags & 0x200) ? render::Blit::Additive : render::Blit::Keyed, kPlayfield);
    }
}

void GameScreen::draw_lens(int image, size_t entry, int ox, int oy) {
    // FUN_0042ada0 "SetLensImage" + the keyed blit FUN_0042fac0 (1, 1).
    // The lens table (FUN_0042ace0, n = 16): inside the radius-8 circle,
    // source offset trunc(2/3 d) from the centre; identity outside.
    static const std::array<std::array<uint8_t, 2>, 256> kLens = [] {
        std::array<std::array<uint8_t, 2>, 256> t{};
        constexpr int n = 16, r = n >> 1;
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const int dx = i - r, dy = j - r;
                auto& e = t[size_t(j * n + i)];
                if (std::sqrt(double(dx * dx + dy * dy)) > double(r)) {
                    e = {uint8_t(i), uint8_t(j)};
                } else {
                    e = {uint8_t(r - int(double(dx) * (-2.0 / 3.0))), uint8_t(r - int(double(dy) * (-2.0 / 3.0)))};
                }
            }
        return t;
    }();
    const auto& img = sprites_->entry(entry);
    auto it = lens_.find(image);
    if (it == lens_.end()) { // the image's own (leftover) pixels until the first capture
        std::array<uint16_t, 256> init{};
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) init[size_t(y * 16 + x)] = sprites_->colour(entry, x, y);
        it = lens_.emplace(image, init).first;
    }
    auto& buf = it->second;
    const int x = ox - img.offset_x, y = oy - img.offset_y; // the image's top-left
    auto& px = screen_.pixels();
    const int W = screen_.width(), H = screen_.height();
    // Bounds against the whole surface; outside, the old capture is drawn.
    if (x >= 0 && y >= 0 && x + 16 <= W && y + 16 <= H) {
        for (int k = 0; k < 256; ++k) {
            const auto& e = kLens[size_t(k)];
            buf[size_t(k)] = px[size_t(y + e[1]) * size_t(W) + size_t(x + e[0])];
        }
    }
    // Keyed blit, clipped to the playfield: magenta skipped, blue = the
    // shadow, which on this path keeps only the low byte (0x42fe9b).
    for (int j = 0; j < 16; ++j) {
        const int ty = y + j;
        if (ty < kPlayfield.y || ty >= kPlayfield.y + kPlayfield.h || ty < 0 || ty >= H) continue;
        for (int i = 0; i < 16; ++i) {
            const int tx = x + i;
            if (tx < kPlayfield.x || tx >= kPlayfield.x + kPlayfield.w || tx < 0 || tx >= W) continue;
            const uint16_t c = buf[size_t(j * 16 + i)];
            uint16_t& d = px[size_t(ty) * size_t(W) + size_t(tx)];
            if (c == 0x7c1f) continue;
            if (c == 0x001f) {
                d = uint16_t(((d >> 1) & 0x3def) & 0xff);
                continue;
            }
            d = c;
        }
    }
}

void GameScreen::draw_craters() {
    // FUN_00423d20: after each visible tile, a crater when the counter is
    // > 0x10 (not under shroud): DATA.R16 114 + k on buildable cells, 146 + k
    // on sand; k = (counter >> 4) + 16 on odd (x + y); jittered by the tables.
    const int x0 = scroll_x_ / 32, y0 = scroll_y_ / 32;
    for (int cy = y0; cy <= y0 + render::kPlayfieldHeight / 32 + 1 && cy < map_->height(); ++cy) {
        for (int cx = x0; cx <= x0 + render::kPlayfieldWidth / 32 + 1 && cx < map_->width(); ++cx) {
            const auto& c = map_->at(cx, cy);
            if (c.marks <= 0x10 || (c.state & 0xf) == 1) continue;
            const int k = (c.marks >> 4) + 16 * ((cx + cy) & 1);
            int entry;
            if (c.flags & sim::cell::kBuildable) entry = 114 + k;
            else if (c.flags & sim::cell::kSand) entry = 146 + k;
            else continue;
            const int j = (cx + cy) & 15;
            const int sx = cx * 32 - scroll_x_ + render::kPlayfieldX + crater_jx_[size_t(j)];
            const int sy = cy * 32 - scroll_y_ + render::kPlayfieldY + crater_jy_[size_t(j)];
            // FUN_00430770 flags (1, 1) at 0x423f3b -> FUN_0042fac0: index 0 transparent,
            // index 1 the half-dark screen pixel, the rest the palette; no
            // image offsets (all crater entries have 0). That is our keyed draw.
            sprites_->draw(screen_, size_t(entry), sx, sy, -1, kPlayfield);
        }
    }
}

void GameScreen::draw_top_left_text() {
    // FUN_004255e0: font 1 (FONTCOL) in the "FontCol" colours (0x51676c,
    // FONTCOL.FPL: red, black edge). The lines start at y 44 instead of 24
    // while the PAGE_INGAME or GAMEMESSAGE menu is open (docs/re/47 section B).
    const auto& colours = assets_.fontcol_colours();
    if (world_->mission_timer != -1) {
        const int t = world_->mission_timer;
        char buf[16];
        std::snprintf(buf, sizeof buf, "%02d:%02d", t / 1500, (t / 25) % 60);
        screen_.draw_text_indexed(assets_.fontcol_font(), colours, 0x1a4, 24, buf);
    }
    const auto& entries = assets_.text().entries();
    int y = menus_.ingame_has("PAGE_INGAME") || menus_.ingame_has("GAMEMESSAGE") ? 44 : 24;
    for (const auto& m : world_->messages) { // newest first
        // FUN_00425720: at most 127 characters, no wrapping or cutting (the
        // exe's cut loop never runs; glyphs past the 640 px surface are
        // skipped by the text blit).
        // Some messages index text.uib directly instead of using the lookup;
        // localise that path too, otherwise in-game messages stay English.
        std::string line = m.key.empty()
                               ? (m.text < entries.size() ? i18n::localize(entries[m.text].second) : std::string{})
                               : assets_.lookup_text(m.key); // FUN_0048dc10, any case
        if (!m.prefix.empty()) line = m.prefix + " " + line + " " + m.arg; // "%s %s %s" (0x4580f0)
        if (!m.literal.empty()) line = m.literal;
        if (!line.empty())
            screen_.draw_text_indexed(assets_.fontcol_font(), colours, 8, y,
                                      i18n::utf8::truncate(line, 0x7f));
        y += 0x14;
    }
}

void GameScreen::draw_shroud() {
    // FUN_00423fb0: full shroud = opaque black 32x32; edge shapes = the
    // darken blit of DATA.R16 entry 38 + state byte.
    const int x0 = scroll_x_ >> 5, y0 = scroll_y_ >> 5;
    for (int cy = y0; cy < map_->height() && (cy * 32 - scroll_y_) < render::kPlayfieldHeight; ++cy) {
        for (int cx = x0; cx < map_->width() && (cx * 32 - scroll_x_) < render::kPlayfieldWidth; ++cx) {
            const uint8_t st = map_->at(cx, cy).state;
            const int sx = cx * 32 - scroll_x_ + render::kPlayfieldX, sy = cy * 32 - scroll_y_ + render::kPlayfieldY;
            if ((st & 0xf) == 0) continue;
            if ((st & 0xf) == 1) {
                fill_clipped(screen_, {sx, sy, 32, 32}, kPlayfield, 0);
                continue;
            }
            sprites_->draw_blit(screen_, size_t(38 + st), sx + sprites_->entry(size_t(38 + st)).offset_x,
                                sy + sprites_->entry(size_t(38 + st)).offset_y, -1, render::Blit::Darken, kPlayfield);
        }
    }
}

void GameScreen::draw_selection(bool building, size_t index) {
    // FUN_00428e30 draws a selected item's bracket right after the item's
    // images (item +0x21), so later items of the sorted list cover it; the
    // infantry pass (FUN_0042af00) the same. Called per item by draw_objects.
    // Pips (FUN_00428e30): `filled` of `total` at (x - 1 + 3k, y - 1 + H),
    // DATA.R16 16 = filled (0x6d8d8c), 15 = empty (0x6d8d88).
    auto pips = [&](int x, int y, int size_class, int filled, int total) {
        int px = x - 1;
        for (int k = 0; k < total; ++k) {
            px += 3;
            sprites_->draw_at(screen_, k < filled ? 16 : 15, px, y - 1 + kBracketHeight[size_class], kPlayfield);
        }
    };
    auto bracket = [&](int x, int y, int size_class, int hp, int max_hp) {
        const auto& top = sprites_->entry(kBracketTop + size_t(size_class));
        const auto& right = sprites_->entry(kBracketRight);
        const auto& bottom_right = sprites_->entry(kBracketBottomRight);
        const int tw = int(top.width), h = kBracketHeight[size_class];
        const int by = y - int(right.height) + 4 + h;
        sprites_->draw_at(screen_, kBracketTop + size_t(size_class), x, y, kPlayfield);
        sprites_->draw_at(screen_, kBracketLeft, x, y + 4, kPlayfield);
        sprites_->draw_at(screen_, kBracketRight, x - int(right.width) + tw, y + 4, kPlayfield);
        sprites_->draw_at(screen_, kBracketBottomLeft, x, by, kPlayfield);
        sprites_->draw_at(screen_, kBracketBottomRight, x - int(bottom_right.width) + tw, by, kPlayfield);
        // Health bar (FUN_0044acc0): RGB(200,0,0) / RGB(220,220,0) / RGB(0,200,0).
        const int len = ((tw - 3) * hp) / std::max(max_hp, 1);
        const uint16_t c = hp < max_hp / 3   ? (red_bar_black_ ? uint16_t(0) : ui::to_rgb555({200, 0, 0}))
                           : hp < max_hp / 2 ? ui::to_rgb555({220, 220, 0})
                                             : ui::to_rgb555({0, 200, 0});
        fill_clipped(screen_, {x + 1, y + 1, len, 2}, kPlayfield, c);
    };
    if (building) {
        const auto& b = world_->buildings()[index];
        if (!b.selected || b.removed) return;
        const auto& r = templates_.building_records[size_t(b.record)];
        if (r.base_element < 0 || size_t(r.base_element) >= templates_.art.building.size()) return;
        const int s = std::min<int>(r.size_class, 5);
        // FUN_00428e30: the origin comes from the draw item's body image (the
        // current frame, or the build-up frame that replaces it); the base
        // image only when there is no body. An empty R16 entry loads as a null
        // image, so it counts as no body: the Starports' body frame is empty.
        const auto& e = templates_.art.building[size_t(r.base_element)];
        long body = e.frames > 0 ? e.base + 1 + std::min(b.frame, e.frames - 1) : -1;
        if ((b.flags & 6) && (body < 0 || b.buildup / 3 < r.buildup_base)) {
            body = templates_.art.anim1_first[r.anim1 % 100] + b.buildup / 3;
        }
        if (body >= 0 && (size_t(body) >= sprites_->size() || sprites_->entry(size_t(body)).width == 0)) body = -1;
        // Below half hp (not building up or selling) the damaged image, when
        // there is one, replaces it (0x7907a8, as render/objects.cpp).
        if (body >= 0 && b.hit_points < r.hit_points / 2 && !(b.flags & 6)) {
            const int df = (r.kind == 0xe || r.kind == 0x10) ? b.wall_frame : 0; // +0x20
            const long dmg = e.base + 1 + e.frames + std::min(df, e.frames - 1);
            if (size_t(dmg) < sprites_->size() && sprites_->entry(size_t(dmg)).width > 0) body = dmg;
        }
        const auto& img = sprites_->entry(size_t(body >= 0 ? body : e.base));
        const int x = (b.x >> 16) + render::kPlayfieldX - scroll_x_;
        int y = (b.y >> 16) + render::kPlayfieldY - scroll_y_ - img.offset_y;
        if (r.kind == 5) y -= 6;
        if (r.second_element >= 0) y -= 12;
        bracket(x, y, s, b.hit_points, r.hit_points);
        // The primary marker (0x429f69 in FUN_00428e30, docs/re/10 section 8):
        // DATA.R16 18 (0x6e2df4) at (x + 2, y - h(right) - 4 + H) on the local
        // player's building with flag 0x80.
        if (b.owner == player_ && (b.flags & 0x80)) {
            sprites_->draw_at(screen_, 18, x + 2, y - int(sprites_->entry(kBracketRight).height) - 4 + kBracketHeight[s],
                              kPlayfield);
        }
        if (b.owner == player_ && (r.kind == 4 || r.kind == 5)) {
            // Refinery 16 / silo 8 slots: spice * n / capacity, at most n - 1.
            const auto& pl = world_->player(player_);
            const int n = r.kind == 4 ? 16 : 8;
            const int filled = pl.capacity == 0 ? 0
                                                : std::min(int(uint32_t(pl.spice) * uint32_t(n) / uint32_t(pl.capacity)), n - 1);
            pips(x, y, s, filled, n - 1);
        }
        // The upgrade marker (0x42a12b): DATA.R16 110 (0x782058) at (x + 2,
        // y + 6) when the local player's upgrade level of the building's name
        // (+0x24544[rec+0x88]) is not 0 (docs/re/21 section 2.3, docs/re/28).
        if (b.owner == player_ && r.name_index < 100 &&
            world_->player(player_).upgrade_level[size_t(r.name_index)] != 0) {
            sprites_->draw_at(screen_, 110, x + 2, y + 6, kPlayfield);
        }
        return;
    }
    {
        const auto& u = world_->units()[index];
        if (!u.selected) return;
        const auto& r = templates_.unit_records[size_t(u.record)];
        const int s = std::min<int>(r.size_class, 5);
        const auto& top = sprites_->entry(kBracketTop + size_t(s));
        const int x = (u.x >> 16) + render::kPlayfieldX - scroll_x_ - int(top.width) / 2;
        int y = (u.y >> 16) + render::kPlayfieldY - scroll_y_ - (kBracketHeight[s] * 5 >> 3);
        if (r.infantry) y -= 8;
        bracket(x, y, s, u.hit_points, r.hit_points);
        const bool has_pips = u.owner == player_ && (r.kind == 1 || r.kind == 4);
        if (u.owner == player_ && r.kind == 1) pips(x, y, s, u.load, 7); // harvester load
        if (u.owner == player_ && r.kind == 4) {
            // Saboteur charge (0x42a2xx): 5 pips upwards from (x, y + H - 2),
            // 3 px apart, filled = +0x29 >> 5 first.
            int py = y + 1 + kBracketHeight[s];
            for (int k = 0; k < 5; ++k) {
                py -= 3;
                sprites_->draw_at(screen_, k < (u.load >> 5) ? 16 : 15, x, py, kPlayfield);
            }
        }
        if (u.owner == player_ && u.ai_group < 10) {
            // The control group digit (DATA.R16 17 + group) at (x + 2, y - 6 + H), -10 with pips.
            sprites_->draw_at(screen_, size_t(17 + u.ai_group), x + 2, y - (has_pips ? 10 : 6) + kBracketHeight[s],
                              kPlayfield);
        }
        // Waiting for a carryall (state 0x12): DATA.R16 112 (0x782060) at
        // (x + 2, y + 6), after the digit (0x42a45e, own units only).
        if (u.owner == player_ && u.state == 0x12) sprites_->draw_at(screen_, 112, x + 2, y + 6, kPlayfield);
    }
}

void GameScreen::draw() {
    compose();
    present();
}

void GameScreen::compose() {
    render::draw_terrain(*map_, tiles_, screen_, scroll_x_, scroll_y_);
    draw_craters();

    // Placement grid (FUN_00428ca0 -> FUN_004285c0 / FUN_004288d0), drawn
    // before the objects. Mode 1 ends when the building slot is empty.
    const auto& slot = world_->player(player_).building_slot;
    if (mode_ == mode::kPlace && slot.item < 0) mode_ = mode::kNormal;
    place_valid_ = false;
    if (mode_ == mode::kPlace && mouse_y_ >= 20) {
        std::vector<sim::PlaceCell> cells;
        const int cx = (scroll_x_ + mouse_x_) / 32, cy = (mouse_y_ - 20 + scroll_y_) / 32;
        place_valid_ = world_->placement_check(player_, slot.item, cx, cy, cells);
        for (const auto& c : cells) {
            sprites_->draw(screen_, c.look, c.cx * 32 - scroll_x_ + render::kPlayfieldX,
                           c.cy * 32 + render::kPlayfieldY - scroll_y_, -1, kPlayfield);
        }
    }

    std::vector<sim::Object> objects, infantry_pass;
    // The world object behind each draw item (building?, index), for its bracket.
    std::vector<std::pair<bool, size_t>> object_refs, infantry_refs;
    objects.reserve(world_->buildings().size() + world_->units().size());
    for (const auto& b : world_->buildings()) {
        if (b.removed || (b.flags & 0x100000)) continue; // not drawn (0x428efd)
        object_refs.push_back({true, size_t(&b - world_->buildings().data())});
        sim::Object o;
        o.building = true;
        o.type = b.type;
        o.record = b.record;
        o.owner = b.owner;
        o.colour = world_->colour_of(b.owner);
        o.house = b.house;
        o.cell_x = b.cell_x;
        o.cell_y = b.cell_y;
        o.x = b.x;
        o.y = b.y;
        // FUN_00428e30: walls and turrets show the wall frame (+0x20).
        const int bk = templates_.building_records[size_t(b.record)].kind;
        o.frame = (bk == 0xe || bk == 0x10) ? b.wall_frame : b.frame;
        o.damage_frame = (bk == 0xe || bk == 0x10) ? b.wall_frame : 0;
        o.hit_points = b.hit_points;
        o.flags = b.flags;
        o.facing = b.turret_facing;
        o.buildup = b.buildup;
        o.anim = b.anim;
        o.repair_blink = (b.flags & 1) && ((uint32_t(b.slot) + world_->tick_count()) & 0x20);
        o.flash = b.flash;
        objects.push_back(o);
    }
    for (const auto& u : world_->units()) {
        if (u.flags & 0x200140) continue; // aircraft, carried, hidden: own passes (doc 14 section 5.1)
        sim::Object o;
        o.type = u.type;
        o.record = u.record;
        o.owner = u.owner;
        o.colour = world_->colour_of(u.owner);
        o.house = u.house;
        o.cell_x = u.cur_x;
        o.cell_y = u.cur_y;
        o.x = u.x;
        o.y = u.y;
        o.altitude = u.altitude;
        o.facing = u.facing;
        o.turret_facing = u.turret_facing;
        o.frame = u.frame;
        o.anim_mode = u.anim_mode;
        o.hit_points = u.hit_points;
        o.flags = u.flags;
        o.fade = u.fade;
        o.index = u.slot; // +0x8c, the pool slot (the shimmer phase)
        o.flash = u.flash;
        // FUN_0042af00: infantry in a vehicle cell (flag 0x1000000, not
        // 0x200000) are left out of the sorted list and drawn first, under
        // the buildings and the other objects (e.g. under a crushing tank).
        o.infantry_pass = (u.flags & 0x1000000) && templates_.unit_records[size_t(u.record)].infantry;
        (o.infantry_pass ? infantry_pass : objects).push_back(o);
        (o.infantry_pass ? infantry_refs : object_refs).push_back({false, size_t(&u - world_->units().data())});
    }
    auto bracket_of = [this](const std::vector<std::pair<bool, size_t>>& refs) {
        return [this, &refs](size_t i) { draw_selection(refs[i].first, refs[i].second); };
    };
    render::draw_objects(infantry_pass, templates_, *sprites_, screen_, scroll_x_, scroll_y_, player_,
                         world_->tick_count(), 3, bracket_of(infantry_refs));
    // FUN_00428ca0's order (doc 16 section 1): the infantry pass
    // (FUN_0042af00), the building foundations (FUN_00427d50), the pixel
    // particles (FUN_0045d550), then FUN_00428e30 (cell objects, bodies and
    // units).
    render::draw_objects(objects, templates_, *sprites_, screen_, scroll_x_, scroll_y_, player_,
                         world_->tick_count(), 1);
    draw_particles();
    // FUN_00428e30's first pass: the cell objects (crates, spice blooms),
    // DATA.R16 102 + image at the cell's top-left (docs/re/31 section 7),
    // FUN_00430770 flags (1, 1) at 0x428eb3: keyed with the index-1 shadow,
    // as our draw (the entries have no offsets).
    for (const auto& e : map_->objects()) {
        if (e.active != 1) continue;
        sprites_->draw(screen_, size_t(102 + e.image), e.x * 32 - scroll_x_ + render::kPlayfieldX,
                       e.y * 32 + render::kPlayfieldY - scroll_y_, -1, kPlayfield);
    }
    render::draw_objects(objects, templates_, *sprites_, screen_, scroll_x_, scroll_y_, player_, world_->tick_count(), 2,
                         bracket_of(object_refs));
    // The flash counters go down once per drawn frame for the items in the
    // draw list (on screen +- 128 px, FUN_0042aa90); a selected building
    // twice (the bracket code too). UI state, not sim state (docs/re/44 section 4).
    auto in_view = [&](int32_t x, int32_t y) {
        const int sx = (x >> 16) - scroll_x_ + render::kPlayfieldX, sy = (y >> 16) - scroll_y_ + render::kPlayfieldY;
        return sx - 128 < kPlayfield.x + kPlayfield.w && kPlayfield.x < sx + 128 && sy - 128 < kPlayfield.y + kPlayfield.h &&
               kPlayfield.y < sy + 128;
    };
    for (auto& b : world_->buildings()) {
        if (b.removed || (b.flags & 0x100000) || b.flash == 0 || !in_view(b.x, b.y)) continue;
        if (b.selected && b.flash != 0) --b.flash;
        if (b.flash != 0) --b.flash;
    }
    for (auto& u : world_->units()) {
        if ((u.flags & 0x200140) || u.flash == 0 || !in_view(u.x, u.y)) continue;
        // The infantry pass shows the grey but never counts down (docs/re/44 section 4.3).
        if ((u.flags & 0x1000000) && templates_.unit_records[size_t(u.record)].infantry) continue;
        --u.flash;
    }
    draw_effects(false);
    draw_aircraft(0);
    draw_effects(true);
    draw_aircraft(1);
    draw_bullets();
    draw_click_puffs(); // after the bullets, before the shroud (docs/re/46 section 2.4)
    draw_shroud();
    sidebar_->draw_banner(screen_, world_->won, world_->lost);
    if (dragging_ && left_held_) {
        // FUN_00423c50: drag start to mouse, 0x5162b8 = RGB(198,181,165),
        // four inclusive lines (FUN_00430d10 -> FUN_004308f0) with the clip
        // rect (0, 21, 479, 400): FUN_004308f0 clamps the ends to
        // [left, right - 1] x [top, bottom - 1], so x 0..478 and y 21..399
        // (column 479 never gets a line).
        const ui::Rect clip{0, 21, 479, 379};
        const uint16_t c = ui::to_rgb555({0xc6, 0xb5, 0xa5});
        const int x0 = drag_x_ - scroll_x_ + render::kPlayfieldX, y0 = drag_y_ - scroll_y_ + render::kPlayfieldY;
        const ui::Rect r{std::min(x0, mouse_x_), std::min(y0, mouse_y_), std::abs(mouse_x_ - x0) + 1,
                         std::abs(mouse_y_ - y0) + 1};
        fill_clipped(screen_, {r.x, r.y, r.w, 1}, clip, c);
        fill_clipped(screen_, {r.x, r.y + r.h - 1, r.w, 1}, clip, c);
        fill_clipped(screen_, {r.x, r.y, 1, r.h}, clip, c);
        fill_clipped(screen_, {r.x + r.w - 1, r.y, 1, r.h}, clip, c);
    }
    sidebar_->draw(screen_, *world_, player_, scroll_x_, scroll_y_);
    draw_top_left_text();
}

void GameScreen::present() {
    // The cursor goes on a copy; the game surface keeps the rest (the exe
    // saves and restores the cursor's background).
    frame_.pixels() = screen_.pixels();

    if (!menu_pauses()) draw_tooltip(frame_); // drawn last, under the cursor

    // Cursor (docs/re/10 §5): drawn at mouse - 24 - image offset.
    const size_t entry = size_t(cursor_type() * 8 + cursor_frame_);
    if (entry < cursors_->size()) {
        const auto& img = cursors_->entry(entry);
        cursors_->draw_at(frame_, entry, mouse_x_ - 24 - img.offset_x, mouse_y_ - 24 - img.offset_y, kScreen);
    }

    // FUN_0042f800: the back buffer is blitted shake px lower (0x5178d4);
    // the rows it leaves are black. The cursor shakes too: FUN_00423a20 draws
    // it into the back buffer 0x5178b4 (0x44948b) before that blit (0x4495e5
    // / 0x449628).
    if (const int dy = world_->shake_y; dy != 0) {
        auto& px = frame_.pixels();
        const size_t row = size_t(kWidth), n = size_t(std::abs(dy)) * row;
        if (dy > 0) {
            std::move_backward(px.begin(), px.end() - ptrdiff_t(n), px.end());
            std::fill(px.begin(), px.begin() + ptrdiff_t(n), uint16_t(0));
        } else {
            std::move(px.begin() + ptrdiff_t(n), px.end(), px.begin());
            std::fill(px.end() - ptrdiff_t(n), px.end(), uint16_t(0));
        }
    }
    SDL_UpdateTexture(texture_, nullptr, frame_.pixels().data(), kWidth * 2);
    display_.begin_frame();
    SDL_RenderTexture(display_.renderer(), texture_, nullptr, nullptr);
    display_.end_frame();
}

int GameScreen::text_index(const std::string& key) const {
    const auto& entries = assets_.text().entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& k = entries[i].first;
        if (k.size() == key.size() &&
            std::equal(k.begin(), k.end(), key.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            })) {
            return int(i);
        }
    }
    return 0;
}

void GameScreen::update_tooltip() {
    // FUN_0044b5c0 (docs/re/11 section 11, docs/re/16 section 5.2).
    const auto& entries = assets_.text().entries();
    auto text_of = [&](int index) -> std::string { // FUN_0048dcc0(index, 0)
        if (index < 0 || size_t(index) >= entries.size()) return {};
        // Sidebar / playfield tooltips read the raw table here; route the
        // value through the localisation pack like every other lookup.
        return i18n::localize(entries[size_t(index)].second);
    };
    auto name_index = [&](const std::string& key) { // the name tables: 0 -> -1 (none)
        const int i = text_index(key);
        return i == 0 ? -1 : i;
    };
    const formats::Rgb yellow{230, 230, 30}; // 0x5174fc (0x1e, 0xe6, 0xe6)
    const formats::Rgb grey{150, 150, 150};  // 0x5175d0
    const int mx = mouse_x_, my = mouse_y_;
    const int wx = scroll_x_ + mx, wy = scroll_y_ + my; // camera + screen
    const bool still = wx == tip_last_x_ && wy == tip_last_y_;
    std::string text;
    int cost = 0, ax = 0, ay = 0, limit_x = 0;
    const int limit_y = kHeight;
    formats::Rgb colour = yellow;
    bool show = false;
    const int player = player_;
    const bool in_play = !(mx < 1 || mx >= render::kPlayfieldWidth || my <= render::kPlayfieldY || my >= kHeight);
    if (!in_play) {
        int rec;
        bool unit;
        int ucost = 0;
        if (sidebar_->starport_item_at(mx, my, *world_, player, rec, ucost, ax, ay, limit_x)) {
            // The Starport page: the name, then the current price.
            text = text_of(name_index(templates_.unit_name[templates_.unit_records[size_t(rec)].name_index]));
            cost = ucost;
            show = !text.empty();
        } else if (sidebar_->upgrade_item_at(mx, my, *world_, player, rec, ucost, ax, ay, limit_x)) {
            // The upgrade page: the name, then the raw upgrade cost (or none).
            text = text_of(name_index(templates_.building_name[templates_.building_records[size_t(rec)].name_index]));
            cost = ucost;
            show = !text.empty();
        } else if (sidebar_->list_item_at(mx, my, *world_, player, rec, unit, ax, ay, limit_x)) {
            // Build-list cells show at once: the name, then "$cost".
            const std::string& key = unit ? templates_.unit_name[templates_.unit_records[size_t(rec)].name_index]
                                          : templates_.building_name[templates_.building_records[size_t(rec)].name_index];
            text = text_of(name_index(key));
            cost = unit ? world_->unit_cost(player, rec) : world_->building_cost(player, rec);
            show = !text.empty();
        } else if (still && ++tip_counter_ > 0x11) {
            // Buttons 4-8 at (button x, 160), left of the button.
            static const struct {
                int control;
                const char* key;
            } kButtons[] = {{4, "REPAIR"}, {5, "SELLBUILDING"}, {6, "MapToggle"}, {7, "GUARD"}, {8, "RETREAT"}};
            bool found = false;
            for (const auto& b : kButtons) {
                if (b.control == 6 && menus_.global("gGameType") == 0) continue;
                if (!sidebar_->over_control(b.control, mx, my)) continue;
                text = text_of(text_index(b.key));
                ax = limit_x = sidebar_->control_x(b.control);
                ay = 0xa0;
                found = show = true;
                break;
            }
            if (!found && mx >= 485 && my >= 172 && mx <= 485 + 10 && my <= kHeight) {
                // The power bar: "POWER n%" ("%s %d%%") at the mouse. The test is
                // 485 <= x <= 485 + width of 0x6d6130 (UI_ENG.R16 extra 0, 10 px)
                // and 172 <= y <= 400, all inclusive (0x44b5c0).
                text = text_of(text_index("POWER")) + " " + std::to_string(world_->player(player).power_percent) + "%";
                ax = mx;
                ay = my;
                limit_x = 485;
                show = true;
            }
        }
    } else if (still && ++tip_counter_ >= 0x12) {
        ax = mx + 14;
        ay = my + 14;
        limit_x = render::kPlayfieldWidth;
        cost = -1;
        const int cx = wx / 32, cy = (wy - render::kPlayfieldY) / 32;
        if (cx < map_->width() && cy < map_->height()) {
            // FUN_004a5270 at the mouse's world pixel (0x44b763, includes
            // cloaked units): the nearest infantry to the mouse, not to the
            // cell corner. A cloaked unit of a side that is not friendly then
            // shows no tooltip at all (docs/re/32 section 1.5).
            const int ui = world_->unit_at_pixel(wx, wy - render::kPlayfieldY, false);
            const int bi = ui < 0 ? world_->building_at_cell(cx, cy) : -1;
            if (world_->shrouded(cx, cy)) {
                text = text_of(text_index("Unrevealed"));
                show = true;
            } else if (ui >= 0) {
                const auto& u = world_->units()[size_t(ui)];
                const int a = world_->alliance(player, u.owner);
                if (a == 0) {
                    text = text_of(name_index(templates_.unit_name[templates_.unit_records[size_t(u.record)].name_index]));
                    show = !text.empty();
                } else if (!(u.flags & 4)) {
                    text = text_of(text_index(a == 2 ? "NeutralUnit" : "EnemyUnit"));
                    show = true;
                }
            } else if (bi >= 0) {
                const auto& b = world_->buildings()[size_t(bi)];
                const int a = world_->alliance(player, b.owner);
                if (a == 0) {
                    text = text_of(name_index(
                        templates_.building_name[templates_.building_records[size_t(b.record)].name_index]));
                } else {
                    text = text_of(text_index(a == 2 ? "NeutralStructure" : "EnemyStructure"));
                }
                show = !text.empty();
            } else {
                // The terrain: the tileset's text id, in grey.
                const uint32_t id = tile_text_[map_->at(cx, cy).tile % 800];
                if (id != 0 && id != 0xffffffff) text = text_of(int(id));
                colour = grey;
                show = !text.empty();
            }
        }
    }
    if (show) {
        // The box: text width + 5 by font height + 2, the cost box below it
        // (one row of overlap), moved up / left to fit the limits.
        const auto& font = assets_.small_font();
        const int fh = int(font.height);
        tip_.text = text;
        tip_.cost = cost > 0 ? "$" + std::to_string(cost) : std::string();
        tip_.rgb = colour;
        tip_.colour = ui::to_rgb555(colour);
        int x0 = ax, y0 = ay;
        int x1 = x0 + ui::text_width(font, text) + 5, y1 = y0 + fh + 2;
        int cx1 = x1, cy0 = y1, cy1 = y1;
        if (!tip_.cost.empty()) {
            cx1 = x0 + ui::text_width(font, tip_.cost) + 5;
            cy0 = y1 - 1;
            cy1 = cy0 + fh;
        }
        const int bottom = tip_.cost.empty() ? y1 : cy1;
        const int right = std::max(x1, tip_.cost.empty() ? x1 : cx1);
        int dy = bottom > limit_y ? limit_y - bottom : 0;
        int dx = right > limit_x ? limit_x - right : 0;
        tip_.box = {x0 + dx, y0 + dy, x1 - x0, y1 - y0};
        tip_.cost_box = {x0 + dx, cy0 + dy, cx1 - x0, cy1 - cy0};
        tip_.shown = true;
    } else if (tip_.shown) {
        tip_.shown = false;
        tip_counter_ = 0; // only a tooltip going away resets the counter (quirk)
    }
    tip_last_x_ = wx;
    tip_last_y_ = wy;
}

void GameScreen::draw_tooltip(ui::Surface& out) const {
    // FUN_00423b40: colour-0 boxes with a border in the tooltip colour, the
    // text in font 0 at x + 3. FUN_0042d660 / FUN_0042d1f0 take {left, top,
    // right, bottom} with right and bottom exclusive: the fill covers the
    // w x h box and the border lines sit on its outer pixels.
    if (!tip_.shown) return;
    auto box = [&](const ui::Rect& r) {
        out.fill(r, 0);
        out.fill({r.x, r.y, 1, r.h}, tip_.colour);
        out.fill({r.x, r.y + r.h - 1, r.w, 1}, tip_.colour);
        out.fill({r.x + r.w - 1, r.y, 1, r.h}, tip_.colour);
        out.fill({r.x, r.y, r.w, 1}, tip_.colour);
    };
    const auto& font = assets_.small_font();
    box(tip_.box);
    if (!tip_.cost.empty()) {
        box(tip_.cost_box);
        const int len = std::min(tip_.cost_box.w, tip_.box.w) - 2;
        out.fill({tip_.cost_box.x + 1, tip_.cost_box.y, len, 1}, 0);
        out.draw_text_solid(font, tip_.cost_box.x + 3, tip_.cost_box.y, tip_.cost, tip_.colour);
    }
    out.draw_text_solid(font, tip_.box.x + 3, tip_.box.y, tip_.text, tip_.colour);
}

std::array<MenuRunner::SideStats, 8> GameScreen::score_stats() const {
    // FUN_0043ec70 builds a record only for sides with +0x24250 set. The
    // mission set-up adds all 8 sides (0x448828); only FUN_0046e6b0 (a
    // network leave, or the surrender order 0x1e in FUN_00457810) clears it,
    // so in single player every side counts.
    std::array<MenuRunner::SideStats, 8> out{};
    for (int s = 0; s < 8; ++s) {
        const auto& pl = world_->player(s);
        auto& o = out[size_t(s)];
        o.active = world_->player_active(s);
        o.house = pl.house;
        o.units_lost = pl.units_lost;
        o.buildings_lost = pl.buildings_lost;
        o.spice = pl.spice_harvested;
        // The skirmish SCORE page (docs/re/56 section 1): its rows, the slot
        // colour, the kills and the finishing place.
        o.ai = world_->ai_active(s);
        o.row = o.ai || (skirmish_ && s == player_);
        if (net_) {
            // MULTIPLAYER RESULTS (docs/re/58 section 6.8): every human of the
            // start (the lobby's "present" flag; one who left keeps the row)
            // and every active AI; a human side the AI took over says "Computer".
            o.row = o.ai || s < int(net_->start.state.players.size());
            o.name = world_->net_names[size_t(s)];
        }
        o.colour = world_->colour_of(s);
        o.units_killed = pl.units_killed;
        o.buildings_killed = pl.buildings_killed;
        o.place = world_->place_[size_t(s)];
    }
    return out;
}

void GameScreen::set_camera(int x, int y, bool minus_one) {
    // Clamp to 0 (values < 1) and to map * 32 - view (- 1 for FUN_0046cbd0 / Home).
    const int mx = map_->width() * render::kCellSize - render::kPlayfieldWidth - (minus_one ? 1 : 0);
    const int my = map_->height() * render::kCellSize - render::kPlayfieldHeight - (minus_one ? 1 : 0);
    scroll_x_ = std::max(0, std::min(x < 1 ? 0 : x, mx));
    scroll_y_ = std::max(0, std::min(y < 1 ? 0 : y, my));
}

void GameScreen::handle_keys() {
    // FUN_004433a0's hotkey block (docs/re/25 part A); no menu is open here.
    const std::vector<int> pressed = std::move(pressed_keys_);
    pressed_keys_.clear();
    auto was = [&](int key) { return std::find(pressed.begin(), pressed.end(), key) != pressed.end(); };
    const bool* k = SDL_GetKeyboardState(nullptr);
    const bool ctrl = k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL];
    const bool shift = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
    const bool alt = k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT];
    const int half_w = render::kPlayfieldWidth / 2, half_h = render::kPlayfieldHeight / 2;
    if (!end_voice_) {
        if (was(SDLK_S)) world_->post(sim::make_order(sim::order::kStop, player_));
        if (was(SDLK_X)) world_->post(sim::make_order(sim::order::kScatter, player_));
        if (was(SDLK_P)) world_->post(sim::make_order(sim::order::kPrimary, player_));
        if (was(SDLK_G)) { // as sidebar control 7
            bool armed = false;
            for (const auto& u : world_->units()) {
                armed = armed || (u.owner == player_ && u.selected && u.state != 0x11 &&
                                  templates_.unit_records[size_t(u.record)].weapon != 0xff);
            }
            if (armed) {
                world_->post(sim::make_order(sim::order::kGuard, player_));
                world_->voice(player_, "GUARDING");
            }
        }
        if (was(SDLK_R)) { // as sidebar control 8
            int rx, ry;
            if (world_->any_unit_selected(player_) && world_->retreat_destination(player_, rx, ry)) {
                world_->post(sim::make_order(sim::order::kRetreat, player_, rx, ry));
                world_->voice(player_, "RETREAT");
            }
        }
        if (was(SDLK_H)) {
            // HomeView(gSideId, .., 1) at 0x446011 (docs/re/25 A.8): a chosen
            // MCV also becomes the only selection.
            int hx, hy;
            if (world_->home_view(player_, hx, hy, true)) set_camera(hx, hy, true);
        }
        if (was(SDLK_E)) { // the drag-box selection over the whole view, no voice
            world_->box_select(player_, scroll_x_, scroll_y_, scroll_x_ + render::kPlayfieldWidth,
                               scroll_y_ + render::kPlayfieldHeight);
        }
        int cx, cy;
        if (was(SDLK_N)) {
            world_->next_unit(player_);
            if (world_->selection_centre(player_, cx, cy)) set_camera(cx - half_w, cy - half_h, false);
        }
        if (was(SDLK_T)) mode_ = mode::kRepair;
        if (was(SDLK_Y)) mode_ = mode::kSell;
        if (was(SDLK_HOME) && world_->selection_centre(player_, cx, cy)) set_camera(cx - half_w, cy - half_h, false);
    }
    // The digits, held (re-applied every loop; the voice once per press).
    for (int d = 0; d < 10; ++d) {
        const SDL_Scancode sc = d == 0 ? SDL_SCANCODE_0 : SDL_Scancode(SDL_SCANCODE_1 + d - 1);
        if (!k[sc]) {
            group_said_[size_t(d)] = 0;
            continue;
        }
        if (ctrl) {
            world_->assign_group(player_, d);
        } else if (!alt && !shift) {
            // The other sides are deselected only here, with the voice
            // (0x4462f7..0x446356), not on the held repeats.
            if (world_->select_group(player_, d, true) && group_said_[size_t(d)] == 0) {
                group_said_[size_t(d)] = 1;
                world_->unit_response(false);
                world_->deselect_other_sides(player_);
            }
        } else if (shift && !alt) {
            // Quirk: the voice test is != 0 (0x44624c), so it adds silently:
            // the flag is cleared whenever the digit is up, and only this
            // block or the plain digit sets it, so with Shift it is 0 here
            // unless the digit was first pressed alone and Shift added while
            // it stays held.
            if (world_->select_group(player_, d, false) && group_said_[size_t(d)] != 0) {
                group_said_[size_t(d)] = 1;
                world_->unit_response(false);
                world_->deselect_other_sides(player_);
            }
        } else if (alt) {
            if (world_->select_group(player_, d, true)) {
                if (group_said_[size_t(d)] == 0) {
                    world_->unit_response(false);
                    group_said_[size_t(d)] = 1;
                }
                int px, py;
                if (world_->group_position(player_, d, px, py)) set_camera(px - half_w, py - half_h, true);
            }
        }
    }
    if (net_) {
        // A network game: the A key (FUN_00459b50) and, at the end of
        // FUN_004433a0, F1..F6 chat (docs/re/58 sections 6.5, 6.6).
        if (was(SDLK_A)) net_ally_key();
        for (int f = 0; f < 6; ++f) {
            if (was(int(SDLK_F1) + f)) {
                net_chat_key(f);
                break;
            }
        }
    }
    // Bookmarks F9-F12 (held): Ctrl stores the camera, else it is restored.
    for (int b = 0; b < 4; ++b) {
        if (!k[SDL_SCANCODE_F9 + b]) continue;
        if (ctrl) {
            bookmarks_[size_t(b)] = {scroll_x_, scroll_y_};
        } else {
            scroll_x_ = bookmarks_[size_t(b)][0];
            scroll_y_ = bookmarks_[size_t(b)][1];
        }
    }
}

std::vector<uint8_t> GameScreen::save_blob() {
    std::vector<uint8_t> out;
    auto put = [&](int32_t v) { for (int b = 0; b < 4; ++b) out.push_back(uint8_t(uint32_t(v) >> (8 * b))); };
    const auto state = world_->save_state();
    put(int32_t(state.size()));
    out.insert(out.end(), state.begin(), state.end());
    put(scroll_x_);
    put(scroll_y_);
    for (int v : crater_jx_) put(v);
    for (int v : crater_jy_) put(v);
    for (const auto& b : bookmarks_) {
        put(b[0]);
        put(b[1]);
    }
    for (int v : sidebar_->view_state()) put(v);
    put(int32_t(track_.size()));
    out.insert(out.end(), track_.begin(), track_.end());
    return out;
}

bool GameScreen::load_blob(std::span<const uint8_t> data) {
    size_t pos = 0;
    bool ok = true;
    // A load from the menus goes through the mission start FUN_00448550,
    // which clears the puffs (FUN_00448470) and rebuilds the health colours
    // (FUN_0044acc0): this screen's fresh state. An in-mission load calls
    // FUN_004418b0 at once (docs/re/27 section 5) and resets neither: the
    // previous screen's puffs and red colour go on (docs/re/46 section 2.5).
    if (carry_ == GameSession::Carry::Load) {
        puffs_ = session_.puffs;
        red_bar_black_ = session_.red_bar_black;
    }
    carry_ = GameSession::Carry::None;
    auto get = [&]() -> int32_t {
        if (pos + 4 > data.size()) {
            ok = false;
            return 0;
        }
        uint32_t v = 0;
        for (int b = 0; b < 4; ++b) v |= uint32_t(data[pos + size_t(b)]) << (8 * b);
        pos += 4;
        return int32_t(v);
    };
    const int32_t n = get();
    if (!ok || n < 0 || pos + size_t(n) > data.size()) return false;
    // 0x54804c is not part of the exe's save: the loaded game goes on with
    // this run's counter (docs/re/16 section 6.1).
    const uint32_t reveal_counter = world_->reveal_counter();
    if (!world_->load_state(data.subspan(pos, size_t(n)))) return false;
    world_->set_reveal_counter(reveal_counter);
    pos += size_t(n);
    scroll_x_ = get();
    scroll_y_ = get();
    // The crater jitter tables are not in the exe's save either (no reader of
    // 0x503f88 / 0x502ed8 in FUN_004418b0): keep this set-up's tables.
    for (int k = 0; k < 32; ++k) (void)get();
    for (auto& b : bookmarks_) {
        b[0] = get();
        b[1] = get();
    }
    std::array<int, 3> sv{};
    for (int& v : sv) v = get();
    const int32_t tn = get();
    if (!ok || tn < 0 || pos + size_t(tn) > data.size()) return false;
    const std::string track(reinterpret_cast<const char*>(data.data() + pos), size_t(tn));
    sidebar_->set_view_state(sv);
    track_ = track; // load_saved restarts it
    mode_ = mode::kNormal;
    menu_closed_redraw();
    return ok;
}

bool GameScreen::load_saved(const SaveGame& g) {
    // FUN_004418b0 (full) then *_GAMELOADED (docs/re/27 section 5).
    if (!load_blob(g.state)) {
        SDL_Log("saved game state does not fit mission %s", mission_.c_str());
        return false;
    }
    // The common block also brings back the speeds and volumes.
    menus_.set_global("gLimitedModelPct", g.common.game_speed);
    menus_.set_global("gScrollSize", long(g.common.scroll_speed));
    menus_.set_global("gCDVolume", long(g.common.cd_volume));
    menus_.set_global("gSFXVolume", long(g.common.sfx_volume));
    apply_options();
    // The end of FUN_004418b0 (0x441d24-0x441d5b): the voice queue is
    // emptied (FUN_0046f980), then the saved track restarts from its start
    // even when it is the one playing (FUN_004700e0, at gCDVolume: gGameState
    // is 2 for both the in-game and the main-menu load), or an empty name
    // stops the music.
    voices_.clear();
    if (track_.empty()) {
        music_.stop();
    } else {
        music_.play(track_ + ".aud");
        music_.set_volume(settings_.music_volume);
    }
    SDL_Log("loaded %s at tick %u", mission_.c_str(), unsigned(world_->tick_count()));
    // FUN_004418b0 rebuilds the shown radar from the shroud bytes (only cells
    // at 0), then the set-up path runs FUN_0049f4d0 after *_GAMELOADED
    // (FUN_00448550: the load at 0x448bc4, the reveal at 0x448c36; docs/re/16
    // sections 4.3 and 6.1).
    world_->rebuild_shown_radar();
    world_->voice(player_, "GAMELOADED");
    world_->reveal_start();
    return true;
}

void GameScreen::open_ingame_menu() {
    // FUN_00470e60(INGAME): the OPTIONS sliders read these globals.
    menus_.set_global("gLimitedModelPct", settings_.game_speed);
    menus_.set_global("gScrollSize", settings_.scroll_speed);
    menus_.set_global("gCDVolume", settings_.music_volume);
    menus_.set_global("gSFXVolume", settings_.sfx_volume);
    dragging_ = false;
    // 0x444c34: INGAME_MP whenever the network flag is set.
    menus_.ingame_open(net_ ? "INGAME_MP" : menus_.global("gGameType") == 1 ? "INGAME_SKIRMISH" : "INGAME");
}

void GameScreen::apply_options() {
    // The SetValue*Special setters write the globals on release
    // (docs/re/20 section 3); the game reads them here.
    const int speed = std::clamp(int(menus_.global("gLimitedModelPct")), 0, 100);
    const int scroll = std::clamp(int(menus_.global("gScrollSize")), 4, 40);
    const int music = std::clamp(int(menus_.global("gCDVolume")), 0, 100);
    const int sfx = std::clamp(int(menus_.global("gSFXVolume")), 0, 100);
    if (speed == settings_.game_speed && scroll == settings_.scroll_speed && music == settings_.music_volume &&
        sfx == settings_.sfx_volume) {
        return;
    }
    if (speed != settings_.game_speed) {
        // Callback 0x45b590: stores the speed global at once (0x45b5a8), so
        // the next rebuild here uses it (FUN_0045b610 reads 0x4e3b18); in a
        // network game it also posts order 0x0B {now + latency, speed} and
        // every machine applies it then (net_ui).
        scheduler_.set_speed(speed);
        if (net_ && net_->lockstep) {
            net_->lockstep->speed(uint32_t(speed));
            for (auto& p : net_->lockstep->take_packets()) net_->transport->send(p, SDL_GetTicks());
        }
    }
    if (music != settings_.music_volume) music_.set_volume(music);  // applies at once, even paused
    // gSFXVolume: read by every later sample, not by those playing (docs/re/59 section 3.3).
    settings_.game_speed = speed;
    settings_.scroll_speed = scroll;
    settings_.music_volume = music;
    settings_.sfx_volume = sfx;
    settings_.save(settings_file_);
}

void GameScreen::next_track() {
    // FUN_00481490: the MUSIC folder's .aud files this house may hear, in
    // FindFirstFile order, not sorted by the exe; NTFS and the CD's ISO 9660
    // list by upper-case name, as we sort (docs/re/15 section 2),
    // then rand() % count until it differs from the current track.
    static const char* const kCommon[] = {"ARAKATAK", "FIGHTPWR", "FREMEN",  "LANDSAND", "ROBOTIX", "SPICESCT",
                                          "UNDERCON", "WAITGAME", "SOLDAPPR", "PLOTTING", "AMBUSH"};
    auto upper = [](std::string s) {
        for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
        return s;
    };
    std::vector<std::string> pool;
    if (auto dir = gd_.find("MUSIC")) {
        for (const auto& f : std::filesystem::directory_iterator(*dir)) {
            const std::string file = upper(f.path().filename().string());
            if (file.size() < 5 || file.substr(file.size() - 4) != ".AUD") continue;
            const std::string base = file.substr(0, file.size() - 4);
            bool ok = (side_ == 0 && base == "ATREGAIN") || (side_ == 1 && (base == "HARK_BAT" || base == "RISEHARK")) ||
                      (side_ == 2 && base == "ENTORDOS");
            for (const char* c : kCommon) ok |= base == c;
            if (ok) pool.push_back(file);
        }
    }
    std::sort(pool.begin(), pool.end());
    // No track (the music page's Stop): the exe gets no end signal then;
    // the guard keeps the pick loop below from spinning forever.
    if (pool.size() < 2 || track_.empty()) return;
    const std::string current = upper(track_);
    std::string pick;
    do {
        pick = pool[rng_.next() % pool.size()];
    } while (pick.compare(0, current.size(), current) == 0);
    track_ = pick.substr(0, pick.size() - 4);
    music_.play(pick);
    music_.set_volume(settings_.music_volume); // the pick restores gCDVolume (docs/re/48 A.5)
}

void GameScreen::play_sounds() {
    // Six Miles channels (docs/re/59): voices queue for channel 0
    // (FUN_0046f820), effects pick one of channels 1-5 by their position
    // (FUN_0046f540). In the exe both happen inside the tick; here, in the
    // same order, after it. The channel pick reads the channels' status
    // as the tick left it, and no voice starts during a tick (FUN_0046faa0
    // runs at the top of the next pass, 0x448f78), so the result is the
    // same; only the start moves by under one pass.
    // First the sidebar's BUTTON and scroll sounds, queued by the input
    // handler (FUN_004433a0) before the tick: FUN_0046f820(id, 0, 0, 0)
    // (0x444712, 0x4445d5, 0x44492c). Mode 0 needs empty rings, so the
    // order against the tick's voices matters.
    for (const auto& name : sidebar_->sounds) queue_voice(name, true, 0);
    sidebar_->sounds.clear();
    for (auto e : world_->sounds) {
        if (e.sample >= 0) e.name = assets_.sample_name(size_t(e.sample)).value_or("");
        if (e.name.empty()) continue;
        if (e.voice || e.mode0) {
            queue_voice(e.name, e.mode0, e.prio);
            continue;
        }
        if (e.unique) { // FUN_0046f4f0: not while any channel plays it
            sync_channels();
            if (audio::sample_playing(channels_, e.name)) continue;
        }
        if (e.cx >= 0) play_effect(e.name, e.cx, e.cy); // every effect of the sim has a cell
    }
    world_->sounds.clear();
    service_speech();
}

void GameScreen::sync_channels() {
    channels_[0].playing = speech_.playing();
    for (size_t i = 0; i < fx_.size(); ++i) channels_[i + 1].playing = fx_[i]->playing();
}

void GameScreen::queue_voice(const std::string& name, bool mode0, int prio) {
    sync_channels();
    voices_.push(name, mode0, prio, channels_[0].playing ? channels_[0].sample : std::string());
}

void GameScreen::play_effect(const std::string& name, int cx, int cy) {
    // FUN_0046f540: pan and volume from the cell against the view's centre
    // cell, then the first idle channel of 1-5, or the quietest one if it
    // is quieter than this sound (mode 2).
    sync_channels();
    const auto pv = audio::positional(cx, cy, scroll_x_, scroll_y_, render::kPlayfieldWidth,
                                      render::kPlayfieldHeight, map_->width(), settings_.sfx_volume);
    const int c = audio::pick_channel(channels_, 1, 5, 2, pv.volume);
    if (c < 0) return;
    auto& player = *fx_[size_t(c - 1)];
    player.stop(); // FUN_0046f170 stops the channel before it loads the sample
    channels_[size_t(c)] = {};
    const auto f = assets_.sample_file(name);
    if (!cache_sample(c, name, f && player.in_sound_rs(*f))) return; // silenced
    if (f && player.play_file(*f, pv.volume, pv.pan)) channels_[size_t(c)] = {true, pv.volume, name};
}

bool GameScreen::cache_sample(int channel, const std::string& name, bool in_rs) {
    // FUN_0046f170 -> FUN_0046f080 (docs/re/59 section 1): the sample must
    // get a cache slot, stamped with gGameTicks, or nothing plays.
    int id = -1;
    for (size_t i = 0;; ++i) {
        const auto n = assets_.sample_name(i);
        if (!n) break;
        if (*n == name) {
            id = int(i);
            break;
        }
    }
    if (id < 0) return false;
    auto& cache = audio::sample_cache();
    cache.now = world_->tick_count();
    sync_channels();
    std::array<bool, audio::kChannels> playing{};
    for (size_t i = 0; i < playing.size(); ++i) playing[i] = channels_[i].playing;
    const int s = cache.load(id, in_rs, playing);
    if (s < 0) return false;
    cache.channel_slot[size_t(channel)] = s;
    return true;
}

void GameScreen::service_speech() {
    // FUN_0046faa0: start the next queued voice (ring 1 first) when none is
    // playing, at gSFXVolume (gGameState 2) and pan 0x40.
    if (voices_.empty() || speech_.playing()) return;
    const auto name = voices_.pop();
    channels_[0] = {};
    const auto f = assets_.sample_file(*name);
    // No GameSFX\<file>.AUD to stream: FUN_0046f210 on channel 0 (mode 0),
    // through the sample cache (BUTTON, S_MONEYTICK, S_ICONSCROLL, ...).
    if (f && !speech_.has_aud(*f) && !cache_sample(0, *name, speech_.in_sound_rs(*f))) return;
    if (f && speech_.play_file(*f, settings_.sfx_volume, audio::kPanCentre)) {
        channels_[0] = {true, settings_.sfx_volume, *name};
    }
}

void GameScreen::net_pass(uint64_t now_ms) {
    // Every loop pass of a network game, menus open or not (doc 57 section
    // 1.1): GetNetworkOrders, ProcessPulseSignal (stall, timeout, drop),
    // the pulse; before tick 0 the start sync (FUN_00448230).
    auto& n = *net_;
    for (auto& d : n.transport->poll(now_ms, n.lockstep->resend_ms())) n.lockstep->on_message(d.from, d.data, now_ms);
    for (auto& f : n.transport->take_foreign()) { // late lobby repeats (the start)
        if (n.host) n.host->handle(f, now_ms);
        if (n.client) n.client->handle(f, now_ms);
    }
    if (n.host) n.host->tick(now_ms); // the start, repeated until every client confirmed
    if (!net_started_) {
        const auto s = n.lockstep->start_sync(now_ms);
        net_started_ = s == net::Lockstep::StartSync::Done;
        net_alone_ = s == net::Lockstep::StartSync::Alone;
    } else {
        n.lockstep->pass(now_ms);
    }
    scheduler_.set_stalled(net_started_ && !n.lockstep->may_tick());
    // The 1-second timer's network part: loop passes, the 9-second report.
    if (net_second_ms_ == 0) net_second_ms_ = now_ms;
    ++net_passes_;
    while (now_ms - net_second_ms_ >= 1000) {
        net_second_ms_ += 1000;
        n.lockstep->second(net_passes_);
        net_passes_ = 0;
    }
    for (auto& p : n.lockstep->take_packets()) n.transport->send(p, now_ms);
}

void GameScreen::net_line(const std::string& text) {
    sim::World::Message m;
    m.literal = text;
    world_->messages.insert(world_->messages.begin(), m); // FUN_00425720: newest first, 5 kept
    if (world_->messages.size() > 5) world_->messages.resize(5);
}

void GameScreen::net_ui() {
    // Every pass of a network game (the UI part of FUN_00448550; docs/re/58
    // section 6): received chat, the lost-player lines, the timeout and
    // out-of-sync boxes, speed changes, the defeat line.
    auto& ls = *net_->lockstep;
    auto key_line = [&](const char* key) {
        sim::World::Message m;
        m.key = key;
        world_->messages.insert(world_->messages.begin(), m);
        if (world_->messages.size() > 5) world_->messages.resize(5);
    };
    for (const auto& c : ls.take_chats()) {
        // FUN_0045c740, misc type 0x12: S_CHATMSG, then the line; a text
        // with 0xAC is cut there (strtok FUN_004b08b0 with "\xAC") and the
        // key's text appended (strcat): "<name> " + " has left the game."
        // gives two spaces, since the text.uib values start with one.
        std::string text = c.text;
        if (const auto k = text.find('\xAC'); k != std::string::npos) {
            text = text.substr(0, k) + assets_.lookup_text(text.substr(k + 1));
        }
        world_->sounds.push_back(sim::chat_sound());
        net_line(text);
    }
    if (ls.take_lost_player()) key_line("LostOnePlayer");
    if (ls.take_lost_some()) key_line("LostSomePlayers");
    // Order 0x0B: the speed at the first pass with its tick reached.
    for (int v; (v = ls.take_speed(world_->tick_count())) >= 0;) {
        scheduler_.set_speed(v);
        settings_.game_speed = v; // the speed global, as the options page shows it
    }
    // The loss path (0x449f24): "<name> has been defeated." to everybody.
    if (world_->lost && !defeated_said_) {
        defeated_said_ = true;
        ls.chat(-1, world_->net_names[size_t(player_)] + " \xAC" + "PlayerDefeated");
    }
    if (world_->won) quit_kind_ = 0xff; // the leave order says "won"
    // An order that arrived after its tick (FUN_0045d120, doc 57 section
    // 1.4): "Model Out Of Sync" into gWigNetError (never shown) and
    // WIGNET_ERROR at once, once (FUN_0045d2c0); its OK -> MAINMENU.
    if (world_->model_out_of_sync() && !wignet_box_) {
        wignet_box_ = true;
        menus_.ingame_open("WIGNET_ERROR");
    }
    // Out of sync (doc 57 section 2.2): the line, then on the next pass the
    // OUT_OF_SYNC box over the running game.
    if (ls.out_of_sync_tick() >= 0) {
        if (!oos_line_) {
            oos_line_ = true;
            if (!world_->won && !world_->lost && !surrendered_) key_line("ModelOutOfSync");
        } else if (!oos_box_) {
            oos_box_ = true;
            menus_.ingame_open("OUT_OF_SYNC");
        }
    }
    // MP_TIMEOUT (doc 58 section 6.3): opened while the timeout seconds run,
    // its counter "Retrying .. %d seconds" with 6 - seconds; closed with
    // QuitMenu (every menu) when the player is back.
    if (ls.shows_timeout_box()) {
        if (!menus_.ingame_has("MP_TIMEOUT")) menus_.ingame_jump("MP_TIMEOUT");
        std::string text = assets_.lookup_text("MPRetry");
        if (const auto k = text.find("%d"); k != std::string::npos) {
            text.replace(k, 2, std::to_string(std::max(0, 6 - int(ls.timeout().seconds))));
        }
        menus_.ingame_set_text("MP_TIMEOUT", "timeoutCounter", text);
    } else if (menus_.ingame_has("MP_TIMEOUT")) {
        menus_.ingame_close();
        menu_closed_redraw();
    }
    for (auto& p : ls.take_packets()) net_->transport->send(p, SDL_GetTicks());
}

void GameScreen::net_found_box() {
    // FOUND_PLAYERS (foundMP.uil, doc 58 section 3.6): the original shows it
    // during the pre-game handshake (FUN_004470a0). Ours has the handshake
    // in the lobby, so the box stands over the start sync, the one wait
    // where a player sees the others come in: "Connecting %d players" (the
    // humans), "Found %d players" (ready so far, me included), "Countdown
    // .. %d seconds" to our start-sync timeout (20 s; the original's
    // handshake counted 60 s). Closed when the game starts.
    auto& ls = *net_->lockstep;
    if (net_started_ || net_alone_) {
        if (found_box_) {
            found_box_ = false;
            if (menus_.ingame_has("FOUND_PLAYERS")) {
                menus_.ingame_close();
                menu_closed_redraw();
            }
        }
        return;
    }
    if (!found_box_) {
        found_box_ = true;
        menus_.ingame_open("FOUND_PLAYERS");
    }
    int humans = 0;
    for (int p = 0; p < 8; ++p) humans += world_->human_active(p) ? 1 : 0;
    auto with = [&](const char* key, int value) {
        std::string text = assets_.lookup_text(key);
        if (const auto k = text.find("%d"); k != std::string::npos) text.replace(k, 2, std::to_string(value));
        return text;
    };
    menus_.ingame_set_text("FOUND_PLAYERS", "mp_connecting", with("ConnectingPlayers", humans));
    menus_.ingame_set_text("FOUND_PLAYERS", "mp_found", with("FoundPlayers", ls.sync_found()));
    menus_.ingame_set_text("FOUND_PLAYERS", "mp_countdown", with("MPCountdown", ls.sync_seconds_left(SDL_GetTicks())));
}

void GameScreen::net_menu_command(const std::string& cmd) {
    // The in-game network pages' commands (docs/re/58 sections 6.2, 6.5).
    if (!net_) return;
    auto& ls = *net_->lockstep;
    const std::string& me = world_->net_names[size_t(player_)];
    if (cmd == "AbortGame") {
        // FUN_00492eb0: "<name> has left the game." to all; quit kind 0 (the
        // AI takes over my side when the leave order runs).
        ls.chat(-1, me + " \xAC" + "PlayerLeft");
        quit_kind_ = 0;
    } else if (cmd == "LeftGame") {
        // FUN_00492e20: "<name> has retired from the game.", order 0x31 (my
        // side is destroyed), quit kind 1; the game goes on to the loss.
        ls.chat(-1, me + " \xAC" + "PlayerRetired");
        world_->post(sim::make_order(sim::order::kRetire, player_));
        quit_kind_ = 1;
        surrendered_ = true;
    } else if (cmd == "SendInGameMsgTo") {
        // FUN_0047bae0: "<my name> : <text>", shown here, sent reliably to
        // the target or to all. An empty text too: Enter runs the focused
        // edit box's commands with no test (FUN_0043cb70 '\r'), and the
        // command (0x48317e) calls FUN_0047bae0 at once, so "<my name> : "
        // goes out.
        const std::string text = menus_.global_string("gNetMessageString");
        const std::string line = me + " : " + text;
        net_line(line);
        ls.chat(chat_target_, line);
    }
    for (auto& p : ls.take_packets()) net_->transport->send(p, SDL_GetTicks());
}

void GameScreen::net_chat_key(int k) {
    // The end of FUN_004433a0 (docs/re/58 section 6.5): F1..F5 (not after a
    // loss) to the k-th other active human in side order; F6 to all (also
    // after a loss). Then GAMEMESSAGE over the game.
    std::string label;
    if (k < 5) {
        if (world_->lost || surrendered_) return;
        std::vector<int> others;
        const int humans = int(net_->start.state.players.size());
        for (int p = 0; p < humans && p < 8; ++p) {
            if (p != player_ && world_->human_active(p)) others.push_back(p);
        }
        if (k >= int(others.size())) return;
        chat_target_ = others[size_t(k)];
        label = world_->net_names[size_t(chat_target_)] + " : ";
    } else {
        chat_target_ = -1;
        label = assets_.lookup_text("ToAll");
    }
    menus_.set_global_string("gSendingToPlayer", label);
    menus_.set_global_string("gNetMessageString", "");
    dragging_ = false;
    menus_.ingame_open("GAMEMESSAGE");
}

void GameScreen::net_ally_key() {
    // FUN_00459b50 (docs/re/58 section 6.6): the first selected unit of
    // another side (sides in order), else its first selected building,
    // names the target; order 0x1B. Not after a loss.
    if (world_->lost || surrendered_) return;
    for (int s = 0; s < 8; ++s) {
        if (s == player_) continue;
        bool hit = false;
        for (const auto& u : world_->units()) {
            if (u.owner == s && u.selected && u.state != 0x11) hit = true;
        }
        for (const auto& b : world_->buildings()) {
            if (b.owner == s && b.selected && !b.removed && !b.dying) hit = true;
        }
        if (!hit) continue;
        world_->post(sim::make_order(sim::order::kAlly, player_, s));
        return;
    }
}

void GameScreen::net_leave() {
    // On leaving the loop (0x44a114): the leave order 9 with the quit kind,
    // then up to 10 s (100 x 100 ms) for every reliable message to be acked.
    if (!net_ || !net_->lockstep || net_left_) return;
    net_left_ = true;
    auto& n = *net_;
    n.lockstep->leave(quit_kind_);
    for (auto& p : n.lockstep->take_packets()) n.transport->send(p, SDL_GetTicks());
    for (int i = 0; i < 100 && n.transport->unacked() > 0; ++i) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
        }
        n.transport->poll(SDL_GetTicks(), n.lockstep->resend_ms());
        SDL_Delay(100);
    }
}

bool GameScreen::sim_tick_due(uint64_t now_ms) {
    if (!scheduler_.tick_due(now_ms)) return false;
    return !net_ || (net_started_ && net_->lockstep->may_tick());
}

void GameScreen::sim_tick() {
    if (net_) net_->lockstep->before_tick(uint32_t(scheduler_.tick_limit())); // the host's latency update
    world_->tick();
    if (net_) {
        auto& n = *net_;
        n.lockstep->after_tick();
        if (world_->latency() != logged_latency_) { // the order delay as it settles (doc 57 section 1.5)
            SDL_Log("network: tick %u latency %u -> %u ticks", unsigned(world_->tick_count()), unsigned(logged_latency_),
                    unsigned(world_->latency()));
            logged_latency_ = world_->latency();
        }
        scheduler_.set_cap(int(n.lockstep->rate_cap()));
        for (int p = 0; p < 8; ++p) {
            if (!world_->human_active(p)) n.transport->set_gone(p);
        }
        for (auto& pk : n.lockstep->take_packets()) n.transport->send(pk, SDL_GetTicks());
    }
    const uint64_t now = SDL_GetTicks();
    tick_times_.push_back(now);
    while (!tick_times_.empty() && now - tick_times_.front() >= 1000) tick_times_.erase(tick_times_.begin());
    if (net_) {
        // A network game (doc 57 section 3.9): the counter is not reloaded
        // while the game runs (0x449aa9). The end block runs in every tick
        // after the end: a counter of 0 on entry ends the game in that tick
        // (0x449c9a won, 0x449f2f lost); else it counts down. A win sets it
        // once to SinglePlayerDelay x ticks (0x449b96) and the same tick
        // already counts down: the banner, then SCORE. A loss counts down
        // from -1 and makes it 0 only when the game is over or no human is
        // alive (0x449f57): a defeated player watches the rest of the game,
        // and the end comes on the next tick.
        if (world_->won || world_->lost) {
            if (world_->won && !net_win_counted_) {
                end_delay_ = 2 * int(tick_times_.size());
                net_win_counted_ = true;
            }
            if (end_delay_ == 0) {
                net_end_ = true;
            } else {
                --end_delay_;
                // After a win, holding Esc (keystate[VK_ESCAPE] 0x797e93,
                // 0x449ca5) ends the delay (docs/re/19 4.4).
                if (world_->won && SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_ESCAPE]) end_delay_ = 0;
                if (world_->lost && (world_->game_over() || !world_->human_alive())) end_delay_ = 0;
            }
        }
    } else if (!world_->won && !world_->lost) {
        // 0x516540 = SinglePlayerDelay (VARS.BIN 0x38 = 2) x ticks in the last second.
        end_delay_ = 2 * int(tick_times_.size());
    } else {
        // The end block, every tick after the end (0x449c9f won, 0x449f36
        // lost; the same order as the network branch above): a counter of 0
        // on entry ends the game in this tick; else it counts down. After a
        // win, holding Esc (keystate[VK_ESCAPE] 0x797e93, 0x449ca5) makes it
        // 0; after a loss it becomes 0 once the game is over or no human is
        // alive (0x449f57: 0x6b63c8 / 0x6b7050), in every game type. So the
        // jump comes the tick after the counter reaches 0 (docs/re/19 4.4,
        // docs/re/52 5.4).
        if (end_delay_ == 0) {
            sp_end_ = true;
        } else {
            --end_delay_;
            if (world_->won && SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_ESCAPE]) end_delay_ = 0;
            if (world_->lost && (world_->game_over() || !world_->human_alive())) end_delay_ = 0;
        }
    }
}

GameScreen::Result GameScreen::run() {
    const Result r = run_loop();
    if (net_ && net_started_) net_leave(); // every way out of a network game (0x44a114)
    return r;
}

GameScreen::Result GameScreen::run_loop() {
    SDL_HideCursor(); // the game draws its own (MOUSE.R16)
    uint64_t next_frame = SDL_GetTicksNS();
    for (;;) {
        left_down_ = left_up_ = right_down_ = right_up_ = false;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (display_mode::handle_event(ev)) continue;
            if (ev.type == SDL_EVENT_QUIT) {
                SDL_ShowCursor();
                return Result::Quit;
            }
            // The key-down array 0x798390: Esc and Space (VK 0x20, written for
            // every WM_KEYDOWN at 0x4a33aa) open the in-game menu (docs/re/20 1.1).
            if (menus_.ingame_active() && (ev.type == SDL_EVENT_TEXT_INPUT || ev.type == SDL_EVENT_KEY_DOWN)) {
                menus_.ingame_key_event(ev); // edit boxes and Enter (FUN_0043cb70 / FUN_0043cdf0)
            }
            // The window procedure (0x4a333b) fills the key-down array only
            // while no menu is open, except for Esc and Enter.
            const bool keys_to_game = !menus_.ingame_active() || (ev.type == SDL_EVENT_KEY_DOWN &&
                                      (ev.key.key == SDLK_ESCAPE || ev.key.key == SDLK_RETURN));
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && keys_to_game) {
                if (ev.key.key == SDLK_ESCAPE) esc_key_ = true;
                if (ev.key.key == SDLK_SPACE) space_key_ = true;
                pressed_keys_.push_back(int(ev.key.key));
            }
            float fx, fy;
            if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                fx = ev.motion.x;
                fy = ev.motion.y;
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                fx = ev.button.x;
                fy = ev.button.y;
                const bool down = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    left_held_ = down;
                    (down ? left_down_ : left_up_) = true;
                } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                    (down ? right_down_ : right_up_) = true;
                }
            } else {
                continue;
            }
            const SDL_FPoint p = display_.window_to_screen(fx, fy);
            mouse_x_ = std::clamp(int(std::floor(p.x)), 0, kWidth - 1);
            mouse_y_ = std::clamp(int(std::floor(p.y)), 0, kHeight - 1);
        }
        if (net_) {
            net_pass(SDL_GetTicks());
            net_found_box();
            if (net_started_) net_ui();
            if (net_alone_) {
                // Nobody else came (doc 57 section 3.3): NO_FIND_PLAYERS until
                // its OK, then ABORT_MP_GAME (back to the lobby).
                if (!alone_box_) {
                    alone_box_ = true;
                    menus_.ingame_open("NO_FIND_PLAYERS");
                } else if (!menus_.ingame_active()) {
                    music_.stop();
                    SDL_ShowCursor();
                    return Result::Back;
                }
            }
        }

        // "Popup UI checks": with no menu open and the mission not ended,
        // Esc / Space open INGAME; Esc inside a menu goes to the menu.
        bool menu_esc = false;
        if (esc_key_ || space_key_) {
            if (!menus_.ingame_active()) {
                // A defeated network player still gets INGAME_MP (Abort).
                if (!end_voice_ || net_) open_ingame_menu();
            } else if (esc_key_) {
                menu_esc = true;
            }
            esc_key_ = space_key_ = false;
        }
        if (menu_pauses()) {
            // Paused (single player): no input, no ticks, no game-view
            // redraw; music and the speech queue keep running.
            music_.service();
            if (music_.take_ended() && side_ >= 0) next_track();
            if (net_) {
                // A network game never pauses under a menu (doc 57 section
                // 1.1): the ticks go on, only the player's input stops.
                if (sim_tick_due(SDL_GetTicks())) sim_tick();
                play_sounds();
            } else {
                scheduler_.paused(SDL_GetTicks());
            }
            // keydown[] is not cleared while a menu is open: the window
            // procedure only adds Esc / Enter then (0x4a333b), and a hotkey
            // pressed in the pass that opened the menu stays until the
            // hotkey block runs again after it closes (FUN_00442540, the
            // only clear, runs at the front end / mission start).
            service_speech();
            if (++cursor_counter_ >= 3) {
                cursor_counter_ = 0;
                cursor_frame_ = (cursor_frame_ + 1) & 7;
            }
            // 0x448fb7: while paused the game view is drawn only when
            // 0x5178ac is set, before the UI manager (0x449204); the UI
            // update then redraws every menu over it (0x4726d4: 0x5178ac
            // and gGameState 2 -> FUN_0043ca50).
            if (redraw_view_) {
                compose();
                menus_.ingame_redraw_all();
            }
            const auto event = menus_.ingame_frame(screen_, float(mouse_x_), float(mouse_y_), left_down_, left_up_,
                                                   left_held_, menu_esc, right_up_);
            if (event == MenuRunner::IngameEvent::Load || event == MenuRunner::IngameEvent::LoadMission) {
                music_.stop();
                SDL_ShowCursor();
                if (event == MenuRunner::IngameEvent::Load) { // the next screen continues them (load_blob)
                    session_.carry = GameSession::Carry::Load;
                    session_.puffs = puffs_;
                    session_.red_bar_black = red_bar_black_;
                }
                return event == MenuRunner::IngameEvent::Load ? Result::Load : Result::LoadMission;
            }
            apply_options();
            if (event == MenuRunner::IngameEvent::Abort) {
                // S_MISSIONABORT is never heard: the UI manager (0x449204)
                // queues it after this pass's speech start (0x448f78), and
                // the loop's end empties the voice queue (FUN_0046f980 at
                // 0x44a0ef) before FUN_0046faa0 runs again (docs/re/59).
                const std::string v = menus_.take_ingame_voice();
                music_.stop();
                SDL_ShowCursor();
                (void)v;
                return Result::Back;
            }
            if (event == MenuRunner::IngameEvent::Lost) { // REPLAY_MISSION No (skirmish)
                music_.stop();
                SDL_ShowCursor();
                return Result::Lost;
            }
            if (event == MenuRunner::IngameEvent::Restart) {
                music_.stop();
                SDL_ShowCursor();
                session_.carry = GameSession::Carry::Restart; // 0x797e1c: the puffs are not reset
                session_.puffs = puffs_;
                return Result::Restart;
            }
            // FUN_00473130 (0x4732e0): a menu close sets 0x5178ac and
            // 0x5178b0. The pass that closed it shows the remaining menus
            // over the old picture (e.g. INGAME over RECAP's remains); the
            // next pass redraws the game view under them. Then 0x449429:
            // 0x5178b0 counts down, at 0 it clears 0x5178ac.
            if (menus_.take_closed()) menu_closed_redraw();
            if (redraw_count_ == 0) redraw_view_ = false;
            else --redraw_count_;
            if (!menus_.ingame_active()) {
                redraw_view_ = false;
                redraw_count_ = 0;
                draw();
            } else {
                present();
            }
            // The tab block (0x4494c2) runs on every pass, paused or not.
            sidebar_->update_tabs(*world_, player_);
            next_frame += kFrameNs;
            const uint64_t now_ns = SDL_GetTicksNS();
            if (next_frame > now_ns) SDL_DelayNS(next_frame - now_ns);
            else next_frame = now_ns;
            continue;
        }

        // "Update Music buffers" at the top of the exe's pass (0x448f47),
        // then the UI manager (0x449204) starts the next track in campaign
        // games (FUN_00481460, rand()); both come before the orders
        // (FUN_004433a0, 0x449852) and the tick (0x449a5e).
        music_.service();
        if (music_.take_ended() && side_ >= 0) next_track();

        // FUN_004433a0: orders, the hotkeys, then edge scrolling (no scroll keys in retail).
        handle_mouse();
        handle_keys();
        update_tooltip();
        if (menus_.ingame_active()) {
            // GAMEMESSAGE over the running game: the UI manager takes the
            // mouse too; Esc is QuitMenu (0x4497f7, command 0xDB).
            if (menu_esc) {
                menus_.ingame_close();
            } else {
                menus_.ingame_frame(screen_, float(mouse_x_), float(mouse_y_), left_down_, left_up_, left_held_, false,
                                    right_up_);
            }
        }
        // FUN_004433a0: a press on OPTIONS (0x444832) and its release
        // (0x444c97, after the open or not) clear the click-outside flag.
        // A right release clears it too, menu or not (FUN_00471250, 0x4a373a).
        if (sidebar_->take_options_pressed() || right_up_) menus_.ingame_clear_outside();
        if (sidebar_->take_options()) {
            const bool open = !end_voice_ || net_;
            if (open) open_ingame_menu();
            menus_.ingame_clear_outside();
            if (open) {
                present();
                continue;
            }
        }
        if (!menus_.ingame_active()) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            sidebar_->keys(keys[SDL_SCANCODE_UP], keys[SDL_SCANCODE_DOWN], *world_, player_);
        }
        edge_scroll();

        if (++cursor_counter_ >= 3) { // the cursor animates every 3 loops
            cursor_counter_ = 0;
            cursor_frame_ = (cursor_frame_ + 1) & 7;
        }

        // Main loop "Game AI processing": at most one tick per pass.
        world_->view_x = scroll_x_;
        world_->view_y = scroll_y_;
        if (sim_tick_due(SDL_GetTicks())) sim_tick();
        play_sounds();
        if ((world_->won || world_->lost) && !end_voice_ && net_) {
            end_voice_ = true; // network games: no voice (gGameType 0 / 1 only, doc 57 section 3.9)
            // Both network end paths (0x449c54..0x449c7f, 0x449eaf..0x449eda;
            // only with 0x7984c4, so never in skirmish), once with the
            // 0x517400 guard, in the tick that saw the end: FUN_00455290(i,
            // 0) and (i, 1) for every side 0..7, then the whole map opens
            // (FUN_0044c480, docs/re/16 section 6.3).
            world_->defeat_check_all();
            world_->open_map();
        } else if ((world_->won || world_->lost) && !end_voice_) {
            // Once: the house voice, and the game waits until it ends
            // (a busy wait in the exe: no ticks, nothing drawn).
            end_voice_ = true;
            const int house = world_->player(player_).house;
            const std::string name =
                std::string(house == 1 ? "H_" : house == 2 ? "O_" : "S_") + (world_->lost ? "MISSIONFAILED" : "MISSIONWON");
            // FUN_004762f0 queues it (FUN_0046f820); FUN_0046faa0 runs once
            // (starting it if no voice plays), then the loop waits while the
            // speech channel plays: the voice, or the one already playing
            // (the queued voice then plays after the wait).
            queue_voice(name, false, 0); // FUN_0046f900(S, O, H, 1, 0, 0)
            service_speech();
            while (speech_.playing()) {
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (display_mode::handle_event(ev)) continue;
                    if (ev.type == SDL_EVENT_QUIT) return Result::Quit;
                }
                music_.service();
                SDL_Delay(10);
            }
            // Won: QuitMenu is queued with the voice (docs/re/19 section 4.4),
            // closing a menu that runs with the game (GAMEMESSAGE); a pausing
            // menu cannot be open on a tick. Lost: no QuitMenu.
            if (world_->won && menus_.ingame_active()) menus_.ingame_close();
        }
        if (end_voice_ && sp_end_ && skirmish_ && world_->lost) {
            // REPLAY_MISSION over the game (not if already on top): Yes =
            // RestartGame, No = the front end at MAINMENU + SCORE. Opened by
            // FUN_00470e60, which sets the pause flag: the game pauses
            // behind it (docs/re/52 section 5.4).
            if (!menus_.ingame_has("REPLAY_MISSION")) menus_.ingame_open("REPLAY_MISSION");
        } else if (end_voice_ && (net_ ? net_end_ : sp_end_)) {
            music_.stop();
            SDL_ShowCursor();
            return world_->lost ? Result::Lost : Result::Won;
        }
        compose();
        sidebar_->update_tabs(*world_, player_); // 0x4494c2, after the draw
        if (menus_.ingame_active()) { // the UI manager draws the menus over the view every loop
            menus_.ingame_redraw_all();
            menus_.ingame_draw(screen_);
        }
        present();

        next_frame += kFrameNs;
        const uint64_t now_ns = SDL_GetTicksNS();
        if (next_frame > now_ns) {
            SDL_DelayNS(next_frame - now_ns);
        } else {
            next_frame = now_ns;
        }
    }
}

} // namespace d2k::app
