#pragma once
// The in-game sidebar and top bar: credits, power bar, radar box, the
// buttons and tabs of UI_ENG.R16 and the build lists (page 1).
// RE: docs/re/11-sidebar.md.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "formats/templates.hpp"
#include "platform/game_data.hpp"
#include "render/sprites.hpp"
#include "sim/world.hpp"
#include "ui/assets.hpp"
#include "ui/surface.hpp"

namespace d2k::app {

// Playfield modes (0x516438, docs/re/10-ingame-input.md).
namespace mode {
constexpr int kNormal = 0, kPlace = 1, kRepair = 2, kSell = 3, kAirstrike = 4, kDeathHand = 5;
} // namespace mode

class Sidebar {
public:
    Sidebar(const platform::GameData& gd, ui::Assets& assets, const formats::Templates& templates,
            const render::SpriteBank& data);

    // Once per loop, after the playfield (FUN_00448550 steps 5-10).
    void draw(ui::Surface& screen, sim::World& world, int player, int scroll_x, int scroll_y);
    // The Starport / upgrade tab states (0x4494c2..0x449574), once per loop
    // pass after the draw, menu open or not.
    void update_tabs(sim::World& world, int player);

    // Mouse in screen pixels. `mode` is the playfield mode.
    void left_down(int x, int y, sim::World& world, int player, int& mode);
    void left_up(int x, int y, sim::World& world, int player, int& mode);
    // FUN_0044b5c0, page 1: the build-list cell under (x, y): its record
    // and whether it is a unit; the anchor (cell centre) and the right
    // limit (the column's x). False when there is none.
    bool list_item_at(int x, int y, const sim::World& world, int player, int& record, bool& unit, int& ax,
                      int& ay, int& limit) const;
    // FUN_00443040(n, 0): the mouse is on control n (state 1, opaque pixel).
    bool over_control(int n, int x, int y) const { return hit(n, x, y); }
    // The upgrade page's cell under the mouse for the tooltip (FUN_0044b5c0
    // page 3): the record, and the raw cost[level + 1] when it can be
    // upgraded (0 otherwise; no difficulty factor, a 1.06 quirk).
    bool upgrade_item_at(int x, int y, const sim::World& world, int player, int& record, int& cost, int& ax, int& ay,
                         int& limit) const;
    // The Starport page's cell (page 2): the unit record and its current price.
    bool starport_item_at(int x, int y, const sim::World& world, int player, int& record, int& price, int& ax,
                          int& ay, int& limit) const;
    int control_x(int n) const { return pos_[size_t(n)][0]; }
    // True once after the OPTIONS control (11) was released while pressed.
    bool take_options() {
        const bool o = options_released_;
        options_released_ = false;
        return o;
    }
    // True once after a left press on the OPTIONS control (0x444823).
    bool take_options_pressed() {
        const bool o = options_pressed_;
        options_pressed_ = false;
        return o;
    }
    // Returns true when the press was used here (build cells, scroll arrows).
    bool right_down(int x, int y, sim::World& world, int player);
    void right_up();
    // Up / Down keys held this loop (page 1).
    void keys(bool up, bool down, const sim::World& world, int player);

    bool over_sidebar(int x, int y) const { return x >= 480 || y < 20; }
    // FUN_00428ca0: "MISSION ACCOMPLISHED" / "MISSION FAILED" (extras 4 / 5),
    // centred in the playfield; failed wins over won.
    void draw_banner(ui::Surface& s, bool won, bool lost) const;

    std::vector<std::string> sounds; // samples.uib names, drained by the game screen
    // Saved games: the list page and the two column scrolls.
    std::array<int, 3> view_state() const { return {page_, scroll_[0], scroll_[1]}; }
    void set_view_state(const std::array<int, 3>& v) {
        page_ = v[0];
        scroll_ = {v[1], v[2]};
        drawn_scroll_ = {-1, -1};
    }

private:
    size_t control_image(int control, int state) const; // UI bank index
    bool hit(int control, int x, int y) const;          // FUN_00443040
    void scroll(int column, int delta_rows, bool page, const sim::World& world, int player);
    void draw_controls(ui::Surface& s);
    void draw_radar(ui::Surface& s, const sim::World& world, int player, int scroll_x, int scroll_y);
    // 0x795518[p] (FUN_00461fb0): the u16 at 0x786c58 + colour * 0x20.
    // COLOURS.BIN is loaded at 0x786c48 (FUN_00461d10), so that is shade 8
    // of the side's 16-colour ramp.
    uint16_t player_colour(const sim::World& world, int side) const {
        return data_.house_colour(world.colour_of(side), 8);
    }
    void draw_power(ui::Surface& s, const sim::Player& pl);
    void draw_credits(ui::Surface& s, const sim::Player& pl);
    void draw_lists(ui::Surface& s, const sim::World& world, int player);
    void draw_upgrades(ui::Surface& s, const sim::World& world, int player); // FUN_004278a0
    void draw_starport(ui::Surface& s, const sim::World& world, int player); // FUN_00427690
    int upgrade_index(int x, int y) const;   // the page 3 cell, -1 outside the grid
    int upgrade_pulse_ = 0;                  // 0x512d74
    void draw_icon(ui::Surface& s, size_t entry, int x, int y, int look, int degrees, const ui::Rect& clip);
    void draw_label(ui::Surface& s, const std::string& key, int x, int y, uint16_t colour);
    void draw_solid_label(ui::Surface& s, const std::string& key, int x, int y, uint16_t colour);
    int list_count(const sim::World& world, int player, int column) const;

    ui::Assets& assets_;
    const formats::Templates& templates_;
    const render::SpriteBank& data_;
    std::unique_ptr<render::SpriteBank> ui_; // UI_ENG.R16: 15 x 3 control images, then 10 extras
    std::array<uint8_t, 15> state_{};        // control +0x14
    std::array<uint8_t, 15> drawn_{};        // control +0x15
    int page_ = 1;                           // 0x516430
    std::array<int, 2> scroll_{};            // 0x516440 / 0x516444
    std::array<int, 2> drawn_scroll_{};      // 0x516448 / 0x51644c
    int radar_counter_ = 1;                  // 0x51647c
    bool map_toggle_ = false;                // 0x51643c: the score box instead of the radar (docs/re/56 4)
    std::string net_name_;                   // gNetPlayerName for that box
public:
    bool map_toggle() const { return map_toggle_; }
    void set_net_name(std::string n) { net_name_ = std::move(n); }
private:
    int pulse_ = 0;                          // 0x512d70
    std::array<std::array<int, 2>, 15> pos_{}; // control +0x0c / +0x10
    bool options_released_ = false;
    bool options_pressed_ = false;
};

} // namespace d2k::app
