#include "app/sidebar.hpp"

#include <algorithm>
#include <cmath>

#include "formats/file_io.hpp"
#include "formats/r8.hpp"

namespace d2k::app {

namespace {
constexpr int kUiExtra = 45; // UI bank: 15 controls x 3 images, then the extras
constexpr int kExtraPowerEmpty = 0, kExtraRadarLogo = 6, kExtraRadarGrille = 9;

// Layout (data globals, docs/re/11-sidebar.md §3-6).
constexpr int kCreditsX = 390, kCreditsY = 0;  // 0x4e4214 / 0x795560 (0 in retail)
constexpr int kPowerX = 485, kPowerY = 172;    // 0x4e420c / 0x4e4210
constexpr int kPowerH = 200;
constexpr int kRadarX = 497, kRadarY = 8, kRadarSize = 128; // 0x4e41e4 / 0x4e41e8
constexpr int kListY = 184, kCellW = 60, kRowStep = 47, kRows = 4;
constexpr int kListX[2] = {501, 563};          // 0x4e41fc / 0x4e4204
constexpr int kListBottom = 372;

// DATA.R16 fixed entries.
constexpr size_t kEmptyCell = 4;   // 0x6d60d0
constexpr size_t kDigits = 27;     // 0x6e5720[0..9]
constexpr size_t kDigitBlank = 37; // 0x6e5748

enum Look { kNormal, kGrey, kProgress };

uint16_t rgb(int r, int g, int b) { return uint16_t(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)); }

void put(ui::Surface& s, int x, int y, uint16_t c, const ui::Rect& clip) {
    if (!clip.contains(x, y) || x < 0 || y < 0 || x >= s.width() || y >= s.height()) return;
    s.pixels()[size_t(y) * size_t(s.width()) + size_t(x)] = c;
}

// The 4-line rectangle (FUN_00430d10), corners inclusive.
void rect(ui::Surface& s, int x0, int y0, int x1, int y1, uint16_t c, const ui::Rect& clip) {
    for (int x = x0; x <= x1; ++x) {
        put(s, x, y0, c, clip);
        put(s, x, y1, c, clip);
    }
    for (int y = y0; y <= y1; ++y) {
        put(s, x0, y, c, clip);
        put(s, x1, y, c, clip);
    }
}

// The grey of FUN_004326b0 (555 screen): v = (r + g + b) / 6 + 3 per 5-bit
// channel, on every pixel.
uint16_t grey(uint16_t c) {
    const uint32_t v = ((c & 0x1f) + ((c >> 5) & 0x1f) + ((c >> 10) & 0x1f)) / 6 + 3;
    return uint16_t(v << 10 | v << 5 | v);
}

// The 8.8 tangent table at 0x6d91a8 (FUN_0044b090): entry i (0..89) =
// ftol(tan(i * 6.283185308 / 360) * 256); entry 90 copies entry 89.
const std::array<uint32_t, 91>& tan_table() {
    static const std::array<uint32_t, 91> t = [] {
        std::array<uint32_t, 91> v{};
        for (int i = 0; i < 90; ++i) {
            v[size_t(i)] = uint16_t(int32_t(std::tan(double(i) * 6.283185308 * 0.002777777777777778) * 256.0));
        }
        v[90] = v[89];
        return v;
    }();
    return t;
}

// The cells FUN_004263b0 copies in full colour for the wedge from p3 to p4
// degrees (clockwise from 12 o'clock), as one call from 0 (the exe paints
// the wedge piece by piece, prev..progress, which covers the same cells).
// Per quadrant, rows run out from the centre row c = h/2 and copy columns
// [m*T(a) >> 8, m*T(b) >> 8) from the centre column w/2, clipped to w/2:
//   Q1 (0..90):    rows c - k, k = 0..h/2-1, m = k     (row c copies nothing)
//   Q2 (90..180):  rows c + k, k = 0..h/2-2, m = k + 1
//   Q3 (180..270): rows c + k, k = 0..h/2-2, m = k + 1 (left side)
//   Q4 (270..360): rows c - k, k = 0..h/2-1, m = k     (left side)
// So the top row and the last row(s) are never coloured.
std::vector<uint8_t> wedge_mask(int w, int h, int p3, int p4) {
    std::vector<uint8_t> m(size_t(w) * size_t(h), 0);
    const auto& t = tan_table();
    const int cx = w / 2, c = h / 2, half = w / 2;
    auto clip = [&](uint32_t v) { return std::min<int>(half, int(v >> 8)); };
    auto mark = [&](int y, int x0, int x1) { // [x0, x1) relative to cx
        if (y < 0 || y >= h) return;
        for (int x = x0; x < x1; ++x) {
            if (cx + x >= 0 && cx + x < w) m[size_t(y) * size_t(w) + size_t(cx + x)] = 1;
        }
    };
    if (p3 < 90) {
        const uint32_t a = t[size_t(p3)], b = p4 < 90 ? t[size_t(p4)] : t[89];
        for (int k = 0; k < h / 2; ++k) mark(c - k, clip(uint32_t(k) * a), clip(uint32_t(k) * b));
    }
    if (p3 < 180 && p4 > 90) {
        const uint32_t hi = p3 > 90 ? t[size_t(180 - p3)] : t[89];
        const uint32_t lo = p4 < 180 ? t[size_t(180 - p4)] : t[0];
        for (int k = 0; k + 1 < h / 2; ++k) mark(c + k, clip(uint32_t(k + 1) * lo), clip(uint32_t(k + 1) * hi));
    }
    if (p3 < 270 && p4 > 180) {
        const uint32_t s3 = p3 > 180 ? t[size_t(p3 - 180)] : t[0];
        const uint32_t s4 = p4 < 270 ? t[size_t(p4 - 180)] : t[89];
        for (int k = 0; k + 1 < h / 2; ++k) {
            mark(c + k, -clip(uint32_t(k + 1) * s4), -clip(uint32_t(k + 1) * s3));
        }
    }
    if (p4 > 270) {
        const uint32_t s3 = p3 > 270 ? t[size_t(360 - p3)] : t[89];
        const uint32_t s4 = p4 < 360 ? t[size_t(360 - p4)] : t[0];
        for (int k = 0; k < h / 2; ++k) mark(c - k, -clip(uint32_t(k) * s3), -clip(uint32_t(k) * s4));
    }
    return m;
}
} // namespace

Sidebar::Sidebar(const platform::GameData& gd, ui::Assets& assets, const formats::Templates& templates,
                 const render::SpriteBank& data)
    : assets_(assets), templates_(templates), data_(data) {
    auto res = formats::parse_ui_resource(formats::read_file(gd.find("Data/UI_ENG.R16").value()));
    std::vector<formats::R8Image> images;
    for (auto& c : res.controls) {
        for (auto& img : c.images) images.push_back(img);
    }
    for (auto& img : res.extra) images.push_back(img);
    ui_ = std::make_unique<render::SpriteBank>(std::move(images), std::span<const uint8_t>{});
    // Mission start: all states 1, "last drawn" 3, then the second tab set
    // and PURCHASE off.
    state_.fill(1);
    drawn_.fill(3);
    for (int c : {9, 10, 12, 13, 14}) state_[size_t(c)] = 0;
    // Positions come from the file.
    for (size_t i = 0; i < 15; ++i) pos_[i] = {res.controls[i].x, res.controls[i].y};}

size_t Sidebar::control_image(int control, int state) const {
    // File order: state 1, state 2, state 0.
    const int k = state == 1 ? 0 : state == 2 ? 1 : 2;
    return size_t(control * 3 + k);
}

bool Sidebar::hit(int control, int x, int y) const {
    if (state_[size_t(control)] != 1) return false;
    const auto& off = ui_->entry(control_image(control, 0));
    const int cx = pos_[size_t(control)][0], cy = pos_[size_t(control)][1];
    if (x < cx || y < cy || x >= cx + int(off.width) || y >= cy + int(off.height)) return false;
    return ui_->index_at(control_image(control, 1), x - cx, y - cy) != 0;
}

int Sidebar::list_count(const sim::World& world, int player, int column) const {
    const auto& pl = world.player(player);
    return int(column == 0 ? pl.building_list.size() : pl.unit_list.size());
}

void Sidebar::scroll(int column, int delta, bool page, const sim::World& world, int player) {
    // Scroll buttons (doc 11 section 5): S_ICONSCROLL when it moves, else S_ENDSCROLL.
    const int count = list_count(world, player, column);
    int& s = scroll_[size_t(column)];
    const bool moves = delta > 0 ? kRows + s < count : s >= 1;
    sounds.push_back(moves ? "S_ICONSCROLL" : "S_ENDSCROLL");
    if (delta > 0) {
        if (kRows + s < count) {
            if (!page) s += 1;
            else if (count - (kRows + s) < kRows) s = count - 1; // the clamp makes it count - 4
            else s += kRows;
        }
    } else {
        if (!page) {
            if (s >= 1) s -= 1;
        } else {
            s = s < kRows ? 0 : s - kRows;
        }
    }
}

// ---- input ------------------------------------------------------------------

void Sidebar::left_down(int x, int y, sim::World& world, int player, int& mode) {
    for (int c = 0; c < 15; ++c) {
        if (!hit(c, x, y)) continue;
        state_[size_t(c)] = 2;
        switch (c) {
        case 4:
            mode = mode == mode::kRepair ? mode::kNormal : mode::kRepair;
            sounds.push_back("BUTTON");
            break;
        case 6:
            // The map toggle 0x51643c (FUN_004433a0 0x44471f): not in the
            // campaign; no sound (docs/re/56 section 4.1).
            if (!world.campaign) map_toggle_ = !map_toggle_;
            break;
        case 5:
            mode = mode == mode::kSell ? mode::kNormal : mode::kSell;
            sounds.push_back("BUTTON");
            break;
        case 0: case 1: case 2: case 3:
            if (page_ == 1) scroll(c / 2, c % 2 ? 1 : -1, false, world, player);
            break;
        case 7: { // guard (FUN_00459980): needs an own armed unit selected
            bool armed = false;
            for (const auto& u : world.units()) {
                if (u.owner == player && u.selected && u.state != 0x11 &&
                    templates_.unit_records[size_t(u.record)].weapon != 0xff) {
                    armed = true;
                }
            }
            if (armed) {
                world.post(sim::make_order(sim::order::kGuard, player));
                world.voice(player, "GUARDING");
            }
            break;
        }
        case 8: { // retreat (FUN_00459ab0): an own unit selected and a destination
            int rx, ry;
            if (world.any_unit_selected(player) && world.retreat_destination(player, rx, ry)) {
                world.post(sim::make_order(sim::order::kRetreat, player, rx, ry));
                world.voice(player, "RETREAT");
            }
            break;
        }
        case 9: // UPGRADE: page 3 (doc 11 section 5): 0-3 -> 0, 9 -> 0, 12 -> 1
            if (page_ == 2) { // also leaves the Starport page: 13, 14 -> 0, 10 -> 1, the cart cancelled
                state_[13] = state_[14] = 0;
                state_[10] = 1;
                if (!world.cart_empty(player)) {
                    world.post(sim::make_order(sim::order::kStarportCancel, player)); // order 0x2c
                    world.voice(player, "CANCELLED");
                }
            }
            page_ = 3;
            for (int k = 0; k < 4; ++k) state_[size_t(k)] = 0;
            state_[9] = 0;
            state_[12] = 1;
            break;
        case 12: // MAIN: page 1 (0-3 -> 1, 9 -> 1, 12 -> 0)
            if (page_ == 3) {
                page_ = 1;
                for (int k = 0; k < 4; ++k) state_[size_t(k)] = 1;
                state_[9] = 1;
                state_[12] = 0;
            }
            break;
        case 10: // STARPORT: page 2 (docs/re/11 section 5)
            if (page_ == 1) {
                for (int k = 0; k < 4; ++k) state_[size_t(k)] = 0;
            } else if (page_ == 3) {
                state_[9] = 1;
                state_[12] = 0;
            }
            page_ = 2;
            state_[10] = 0;
            state_[13] = state_[14] = 1;
            break;
        case 13: // MAIN from the Starport page: the cart is cancelled
            if (page_ == 2) {
                page_ = 1;
                for (int k = 0; k < 4; ++k) state_[size_t(k)] = 1;
                state_[10] = 1;
                state_[13] = state_[14] = 0;
                drawn_scroll_ = {-1, -1};
                if (!world.cart_empty(player)) {
                    world.post(sim::make_order(sim::order::kStarportCancel, player)); // order 0x2c
                    world.voice(player, "CANCELLED");
                }
            }
            break;
        case 14: // PURCHASE: page 1, the cart is ordered
            if (page_ == 2) {
                page_ = 1;
                for (int k = 0; k < 4; ++k) state_[size_t(k)] = 1;
                state_[13] = state_[14] = 0;
                drawn_scroll_ = {-1, -1};
                if (!world.cart_empty(player)) {
                    world.player(player).purchase_posted = true; // FUN_0045a2b0
                    world.post(sim::make_order(sim::order::kStarportBuy, player));             // order 0x2b
                    world.voice(player, "PORTORDER");
                }
            }
            break;
        case 11: // OPTIONS: opens the menu on release (left_up, 0x444bfc)
            options_pressed_ = true; // 0x444823: the press clears 0x797590
            break;
        default: break;
        }
        return;
    }
}

bool Sidebar::list_item_at(int x, int y, const sim::World& world, int player, int& record, bool& unit, int& ax,
                           int& ay, int& limit) const {
    // FUN_0044b5c0: list area x in (501, 563 + 60), y in (184, 184 + 4 x 47).
    if (page_ != 1) return false; // pages 2 / 3: starport_item_at / upgrade_item_at
    if (!(x > kListX[0] && x < kListX[1] + kCellW && y > kListY && y < kListY + kRows * kRowStep)) return false;
    const int col = (x - kListX[0]) / kCellW;
    ax = kCellW / 2 + kListX[0] + kCellW * col;
    const int row = (y - kListY) / kRowStep;
    ay = kRowStep / 2 + kListY + kRowStep * row;
    const auto& pl = world.player(player);
    if (x < kListX[0] + kCellW) { // left column: buildings
        const size_t i = size_t(scroll_[0] + row);
        if (i >= pl.building_list.size()) return false;
        record = pl.building_list[i];
        unit = false;
        limit = kListX[0];
        return true;
    }
    if (x > kListX[1] && y > kListY) { // right column: units (0x4e4208)
        const size_t i = size_t(row + scroll_[1]);
        if (i >= pl.unit_list.size()) return false;
        record = pl.unit_list[i];
        unit = true;
        limit = kListX[1];
        return true;
    }
    return false;
}

void Sidebar::left_up(int x, int y, sim::World& world, int player, int& mode) {
    if (state_[11] == 2) options_released_ = true; // FUN_004433a0 0x444bfc
    for (auto& s : state_) {
        if (s == 2) s = 1;
    }
    if (page_ == 2) {
        // A Starport cell posts 0x29 (FUN_0045a200), not while a delivery is
        // pending or a purchase is posted.
        const int i = upgrade_index(x, y);
        const auto& pl = world.player(player);
        if (i >= 0 && !pl.delivery_pending && !pl.purchase_posted) {
            const int rec = pl.starport_list[size_t(i)];
            world.post(sim::make_order(sim::order::kStarportAdd, player, rec));
        }
        return;
    }
    if (page_ == 3) {
        // 0x444d9f: an available cell posts 0x2D with the name index.
        const int i = upgrade_index(x, y);
        const int rec = i >= 0 ? world.player(player).upgrade_list[size_t(i)] : -1;
        if (rec >= 0) {
            const int name = templates_.building_records[size_t(rec)].name_index;
            if (world.can_upgrade(player, name)) world.post(sim::make_order(sim::order::kStartUpgrade, player, name));
        }
        return;
    }
    if (page_ != 1 || y <= kListY || y >= kListBottom) return;
    int column;
    if (x > kListX[0] && x < kListX[0] + kCellW) column = 0;
    else if (x >= kListX[0] + kCellW && x < kListX[1] + kCellW) column = 1; // 561..623 counts as right
    else return;
    const int row = (y - kListY) / kRowStep;
    const auto& pl = world.player(player);
    const auto& list = column == 0 ? pl.building_list : pl.unit_list;
    const size_t i = size_t(row + scroll_[size_t(column)]);
    if (i >= list.size()) return;
    const int item = list[i];
    if (column == 0) {
        const auto& s = pl.building_slot;
        if (s.item < 0 || s.progress < sim::kProgressDone) world.post(sim::make_order(sim::order::kStartBuilding, player, item));
        else if (!s.placing) mode = mode::kPlace; // the grid: GameScreen (docs/re/12)
        return;
    }
    if (!world.is_special(item)) {
        world.post(sim::make_order(sim::order::kStartUnit, player, item));
        return;
    }
    // A ready, unused special (docs/re/35 section 3.1): the airstrike asks
    // for a target (mode 4); its used byte is set by the target click.
    const int kind = templates_.unit_records[size_t(item)].kind;
    bool found = false;
    for (auto& s : world.player(player).unit_slots) {
        if (s.item == item && s.progress == sim::kProgressDone && !s.used) {
            found = true;
            if (kind == 4 || kind == 12) s.used = true;
        }
    }
    if (!found) return;
    if (kind == 9) mode = mode::kAirstrike;
    else if (kind == 10) mode = mode::kDeathHand;
    else if (kind == 4 || kind == 12) world.post(sim::make_order(sim::order::kSpecial, player, kind, 0, 0)); // order 0x30, no cursor, no sound

}

bool Sidebar::right_down(int x, int y, sim::World& world, int player) {
    if (page_ == 2) {
        // A Starport cell posts 0x2a (remove one), not while a delivery is pending.
        const int i = upgrade_index(x, y);
        if (i < 0) return false;
        const int rec = world.player(player).starport_list[size_t(i)];
        if (!world.player(player).delivery_pending) world.post(sim::make_order(sim::order::kStarportRemove, player, rec));
        return true;
    }
    if (page_ == 3) {
        // 0x444287: any cell of the grid posts 0x2E with the record (-1
        // matches nothing).
        const int i = upgrade_index(x, y);
        if (i < 0) return false;
        const int rec = world.player(player).upgrade_list[size_t(i)];
        world.post(sim::make_order(sim::order::kHoldUpgrade, player, rec));
        return true;
    }
    if (page_ != 1) return false;
    if (y > kListY && y < kListBottom) {
        const int row = (y - kListY) / kRowStep;
        const auto& pl = world.player(player);
        if (x > kListX[0] && x < kListX[0] + kCellW) {
            const size_t i = size_t(row + scroll_[0]);
            if (i < pl.building_list.size()) {
                world.player(player).building_slot.placing = true; // +0x2652d = 1
                const int rec = pl.building_list[i];
                world.post(sim::make_order(sim::order::kHoldBuilding, player, rec)); // order 0x25
                return true;
            }
        } else if (x > kListX[1] && x < kListX[1] + kCellW) {
            const size_t i = size_t(row + scroll_[1]);
            if (i < pl.unit_list.size()) {
                if (const int rec = pl.unit_list[i]; !world.is_special(rec)) {
                    world.post(sim::make_order(sim::order::kHoldUnit, player, rec));
                }
                return true;
            }
        }
    }
    for (int c = 0; c < 4; ++c) {
        if (!hit(c, x, y)) continue;
        state_[size_t(c)] = 2;
        scroll(c / 2, c % 2 ? 1 : -1, true, world, player);
        return true;
    }
    return false;
}

void Sidebar::right_up() {
    for (int c = 0; c < 4; ++c) {
        if (state_[size_t(c)] == 2) state_[size_t(c)] = 1;
    }
}

void Sidebar::keys(bool up, bool down, const sim::World& world, int player) {
    if (page_ != 1) return;
    for (int c = 0; c < 2; ++c) {
        if (down) {
            if (kRows + scroll_[size_t(c)] < list_count(world, player, c)) ++scroll_[size_t(c)];
        } else if (up && scroll_[size_t(c)] >= 1) {
            --scroll_[size_t(c)];
        }
    }
}

// ---- drawing ----------------------------------------------------------------

void Sidebar::draw(ui::Surface& s, sim::World& world, int player, int scroll_x, int scroll_y) {
    const auto& pl = world.player(player);
    draw_radar(s, world, player, scroll_x, scroll_y);
    draw_power(s, pl);
    draw_credits(s, pl);
    // Clamp the list scroll, then page 1.
    for (int c = 0; c < 2; ++c) {
        const int count = list_count(world, player, c);
        if (count < kRows + scroll_[size_t(c)]) scroll_[size_t(c)] = std::max(count - kRows, 0);
    }
    if (page_ == 1) draw_lists(s, world, player);
    if (page_ == 2) draw_starport(s, world, player);
    if (page_ == 3) draw_upgrades(s, world, player);
    draw_controls(s);
    // The tab update (update_tabs) is not part of the draw: the game screen
    // calls it on every pass after the sidebar draw.
}

void Sidebar::update_tabs(sim::World& world, int player) {
    // The game loop's tab block ("Game UI Polling", 0x4494c2..0x449574):
    // after the sidebar draw (the controls' FUN_00427cc0 at 0x4491e0), the
    // tooltip and the cursor, before "Blit to Screen", on every pass, also
    // while a menu is open (only the tooltip in front of it tests the menu
    // flag). So a tab state it changes shows from the next frame on: after
    // MAIN on page 3 set control 9 back to 1 (doc 28 section 3.1), the
    // UPGRADE tab is seen for one frame before this update blanks it.
    const auto& pl = world.player(player);
    // Tabs (game loop): the Starport tab needs a Starport (kind 13).
    bool starport = false;
    for (const auto& b : world.buildings()) {
        if (b.owner == player && !b.removed && templates_.building_records[size_t(b.record)].kind == 13) starport = true;
    }
    if (!starport) {
        state_[10] = 0;
        if (page_ == 2) { // 0x4494c2: back to page 1, order 0x2c without a cart test or voice
            page_ = 1;
            for (int k = 0; k < 4; ++k) state_[size_t(k)] = 1;
            state_[13] = state_[14] = 0;
            drawn_scroll_ = {-1, -1};
            world.post(sim::make_order(sim::order::kStarportCancel, player));
        }
    } else if (state_[10] == 0 && page_ == 1) {
        state_[10] = 1;
    }
    // The upgrade tab (0x449542, FUN_00446760): blank when nothing in the
    // upgrade list can be upgraded; back on only on page 1.
    bool upgradable = false;
    for (int rec : pl.upgrade_list) {
        if (rec >= 0 && world.can_upgrade(player, templates_.building_records[size_t(rec)].name_index)) upgradable = true;
    }
    if (!upgradable) state_[9] = 0;
    else if (state_[9] == 0 && page_ == 1) state_[9] = 1;
}

void Sidebar::draw_banner(ui::Surface& s, bool won, bool lost) const {
    if (!won && !lost) return;
    const size_t img = size_t(kUiExtra + (lost ? 5 : 4));
    const auto& e = ui_->entry(img);
    // FUN_00428ca0: the blit (FUN_00430770, flags 1, 1) reads only the
    // image's width and height, so the entry offsets do not apply.
    const int x = (480 - int(e.width)) / 2, y = (380 - int(e.height)) / 2 + 20;
    ui_->draw_at(s, img, x, y, {0, 20, 480, 380});
}

void Sidebar::draw_controls(ui::Surface& s) {
    // FUN_00427cc0: off images first, then the other changed states.
    // FUN_004305f0 with flags (1, 0): 8-bit index 0 transparent (index 1 a
    // normal colour), 16-bit 0xf81f transparent. In 1.06 no control image
    // has index 1 or a key pixel in its 16-bit image, so keyed index 0 is all.
    auto blit = [&](int c, int state) {
        const size_t img = control_image(c, state);
        const auto& e = ui_->entry(img);
        const int x0 = pos_[size_t(c)][0], y0 = pos_[size_t(c)][1];
        for (int y = 0; y < int(e.height); ++y) {
            for (int x = 0; x < int(e.width); ++x) {
                if (e.bits_per_pixel == 8 && ui_->index_at(img, x, y) == 0) continue;
                put(s, x0 + x, y0 + y, ui_->colour(img, x, y), {0, 0, 640, 400});
            }
        }
    };
    for (int c = 0; c < 15; ++c) {
        if (state_[size_t(c)] == 0 && drawn_[size_t(c)] != 0) {
            blit(c, 0);
            drawn_[size_t(c)] = 0;
        }
    }
    for (int c = 0; c < 15; ++c) {
        if (state_[size_t(c)] != 0 && state_[size_t(c)] != drawn_[size_t(c)]) {
            blit(c, state_[size_t(c)]);
            drawn_[size_t(c)] = state_[size_t(c)];
        }
    }
}

void Sidebar::draw_radar(ui::Surface& s, const sim::World& world, int player, int scroll_x, int scroll_y) {
    const auto& pl = world.player(player);
    bool radar = false;
    for (const auto& b : world.buildings()) {
        if (b.owner == player && !b.removed && templates_.building_records[size_t(b.record)].kind == 1) radar = true;
    }
    const ui::Rect box{kRadarX, kRadarY, kRadarSize, kRadarSize};
    if (map_toggle_) {
        // FUN_00425c60 (docs/re/56 section 4.2), redrawn 16 ticks of every 32:
        // the grille, "Name" / "Score" headers, then a row per side in side
        // order: the local side (gNetPlayerName) and every AI not yet beaten
        // ("Computer"), in the player colour; the score is the kills.
        if ((world.tick_count() & 0x10) == 0) {
            const size_t g = size_t(kUiExtra + kExtraRadarGrille);
            for (int y = 0; y < kRadarSize; ++y) {
                for (int x = 0; x < kRadarSize; ++x) put(s, kRadarX + x, kRadarY + y, ui_->colour(g, x, y), box);
            }
            // Font 0 (`font`, FONT.FNT; FUN_00470710 loads it at 0x470865),
            // every glyph pixel solid in the colour (FUN_0042dc00 ->
            // FUN_00431e40).
            const auto& font = assets_.small_font();
            auto left = [&](int x, int y, const std::string& t, uint16_t c) { s.draw_text_solid(font, x, y, t, c); };
            auto right = [&](int x, int y, const std::string& t, uint16_t c) {
                s.draw_text_solid(font, x - ui::text_width(font, t), y, t, c);
            };
            const uint16_t head = rgb(200, 200, 220); // 0x4ddf38 through FUN_0042f3d0
            left(kRadarX, kRadarY, assets_.lookup_text("UI_Name"), head);
            right(kRadarX + 128, kRadarY, assets_.lookup_text("UI_Score"), head);
            int y = kRadarY + 20;
            for (int p = 0; p < 8; ++p) {
                // Sides that are not me, have no active AI and are no active
                // human (+0x37) are skipped first.
                if (p != player && !world.ai_active(p) && !world.human_active(p)) continue;
                const bool unplaced = world.place_[size_t(p)] == 0xff;
                std::string name;
                if (world.ai_active(p) && unplaced) name = assets_.lookup_text("UI_Computer");
                else if (world.network && unplaced) name = world.net_names[size_t(p)]; // 0x798630 + s * 0x3c
                else if (!world.network && p == player) name = net_name_;             // skirmish: gNetPlayerName
                else continue; // network: a side with a place goes, my own row too
                const uint16_t c = player_colour(world, p); // 0x795518[p], used as is
                const auto& sp = world.player(p);
                left(kRadarX, y, name, c);
                right(kRadarX + 128, y, std::to_string(sp.units_killed + sp.buildings_killed), c);
                y += 12;
            }
            radar_counter_ = 1;
        }
        return;
    }
    if (!radar || pl.power_percent < 100) {
        // FUN_00425fa0: fade the house logo in over 65 loops.
        if (radar_counter_ > 64) return;
        const int a = radar_counter_;
        const size_t logo = size_t(kUiExtra + kExtraRadarLogo + (pl.house > 2 ? 0 : pl.house));
        for (int y = 0; y < kRadarSize; ++y) {
            for (int x = 0; x < kRadarSize; ++x) {
                uint16_t& d = s.pixels()[size_t(kRadarY + y) * 640 + size_t(kRadarX + x)];
                const uint16_t l = ui_->colour(logo, x, y);
                auto ch = [&](int shift) {
                    const int lv = (l >> shift) & 0x1f, dv = (d >> shift) & 0x1f;
                    return uint16_t(((lv * a + dv * (64 - a)) >> 6) << shift);
                };
                d = uint16_t(ch(10) | ch(5) | ch(0));
            }
        }
        ++radar_counter_;
        return;
    }
    // FUN_00426000.
    if (radar_counter_ != 0) {
        const size_t g = size_t(kUiExtra + kExtraRadarGrille);
        for (int y = 0; y < kRadarSize; ++y) {
            for (int x = 0; x < kRadarSize; ++x) put(s, kRadarX + x, kRadarY + y, ui_->colour(g, x, y), box);
        }
        radar_counter_ = 0;
    }
    const auto& map = world.map();
    const int x0 = kRadarX + (kRadarSize - map.width()) / 2, y0 = kRadarY + (kRadarSize - map.height()) / 2;
    // The shown radar image 0x6d7ba0: black until the shroud reveals the
    // cell, then the true pixel the sim keeps (Cell::radar, set at the
    // start / load and by the exe's pixel writers, docs/re/11 section 4).
    // Player colour = COLOURS.BIN entry 0 of the player's colour (0x795518
    // from 0x786c58, assumed to hold COLOURS.BIN).
    for (int cy = 0; cy < map.height(); ++cy) {
        for (int cx = 0; cx < map.width(); ++cx) {
            if (!map.at(cx, cy).radar_known) { // shown radar starts black (doc 16 section 4.3)
                put(s, x0 + cx, y0 + cy, 0, box);
                continue;
            }
            const uint8_t code = map.at(cx, cy).radar;
            uint16_t c;
            if (code < 8) c = player_colour(world, code);
            else if (code == sim::radar_colour::kSpice) c = rgb(100, 100, 100);
            else if (code == sim::radar_colour::kSand) c = rgb(30, 30, 30);
            else if (code == sim::radar_colour::kDrivable) c = rgb(60, 50, 50);
            else c = rgb(70, 70, 70);
            put(s, x0 + cx, y0 + cy, c, box);
        }
    }
    for (const auto& u : world.units()) {
        const auto& r = templates_.unit_records[size_t(u.record)];
        if (u.owner != player && (u.flags & 4)) continue;
        if (u.to_x >= map.width() || u.to_y >= map.height() || map.at(u.to_x, u.to_y).state != 0) continue;
        if ((u.flags & 0x40) && r.kind != 9) continue;
        const uint16_t c = r.kind == 5 ? rgb(0xc6, 0xb5, 0xa5) : player_colour(world, u.owner);
        put(s, x0 + u.to_x, y0 + u.to_y, c, box);
    }
    const int vx = x0 + scroll_x / 32, vy = y0 + scroll_y / 32;
    rect(s, vx, vy, vx + 14, vy + 11, rgb(0, 200, 0), box);
}

void Sidebar::draw_power(ui::Surface& s, const sim::Player& pl) {
    // FUN_00425860, drawn whole here (the exe redraws only changed rows,
    // with the same result). Fills from the bottom, 1 px per percent.
    const int v = std::clamp(pl.power_percent, 0, kPowerH);
    const int level = v < 51 ? 1 : v < 100 ? 2 : 3;
    const ui::Rect all{0, 0, 640, 400};
    for (int y = 0; y < kPowerH; ++y) {
        const size_t img = size_t(kUiExtra + (y < kPowerH - v ? kExtraPowerEmpty : level));
        const auto& e = ui_->entry(img);
        for (int x = 0; x < int(e.width); ++x) put(s, kPowerX + x, kPowerY + y, ui_->colour(img, x, y), all);
    }
}

void Sidebar::draw_credits(ui::Surface& s, const sim::Player& pl) {
    // FUN_00425560 / FUN_004254a0: blit flags (1, 1), but DATA.R16 27-37
    // have no index 0 or 1 pixel in 1.06, so drawing them opaque is the same.
    const int value = std::min(pl.spice + pl.cash, 9'999'999);
    int div = 10'000'000;
    bool leading = true;
    for (int i = 0; i < 8; ++i, div /= 10) {
        const int d = (value / div) % 10;
        if (d != 0 || i == 7) leading = false;
        const size_t entry = leading ? kDigitBlank : kDigits + size_t(d);
        draw_icon(s, entry, kCreditsX + 10 * i, kCreditsY, kNormal, 0, {0, 0, 640, 400});
    }
}

void Sidebar::draw_icon(ui::Surface& s, size_t entry, int x, int y, int look, int degrees, const ui::Rect& clip) {
    if (entry >= data_.size()) return;
    const auto& e = data_.entry(entry);
    std::vector<uint8_t> wedge;
    if (look == kProgress) wedge = wedge_mask(int(e.width), int(e.height), 0, degrees);
    for (int py = 0; py < int(e.height); ++py) {
        for (int px = 0; px < int(e.width); ++px) {
            uint16_t c = data_.colour(entry, px, py);
            if (look == kGrey || (look == kProgress && !wedge[size_t(py) * e.width + size_t(px)])) c = grey(c);
            put(s, x + px, y + py, c, clip);
        }
    }
}

void Sidebar::draw_label(ui::Surface& s, const std::string& key, int x, int y, uint16_t colour) {
    // FUN_0042e0b0: x + (60 - width) / 2, font 1 (FONTCOL) through the
    // colour handle 0x5175cc (FUN_00431f50: index 0 transparent, else
    // handle[index]). FUN_00426e80 first fills handle entries 2..15 with
    // the pulse colour (0x426f71, REP STOSD of 7 dwords); entries 0 and 1
    // stay FONTCOL.FPL's (1 = the black edge).
    const std::string text = assets_.lookup_text(key);
    const auto& font = assets_.fontcol_font();
    const int w = ui::text_width(font, text);
    auto colours = assets_.fontcol_colours();
    for (size_t i = 2; i < colours.size(); ++i) colours[i] = colour;
    s.draw_text_indexed(font, colours, x + (kCellW - w) / 2, y, text);
}

void Sidebar::draw_solid_label(ui::Surface& s, const std::string& key, int x, int y, uint16_t colour) {
    // FUN_0042e050 -> FUN_0042dc00 (colour -1 path): x + (60 - width) / 2,
    // font 0 ("font", FONT.FNT), every nonzero glyph pixel in the colour.
    const std::string text = assets_.lookup_text(key);
    const int w = ui::text_width(assets_.small_font(), text);
    s.draw_text_solid(assets_.small_font(), x + (kCellW - w) / 2, y, text, colour);
}

void Sidebar::draw_lists(ui::Surface& s, const sim::World& world, int player) {
    const auto& pl = world.player(player);
    pulse_ = pulse_ >= 40 ? 0 : pulse_ + 1;
    const uint16_t pulse = rgb(pulse_ + 20, pulse_ + 210, pulse_ + 20);

    for (int column = 0; column < 2; ++column) {
        const auto& list = column == 0 ? pl.building_list : pl.unit_list;
        const int x = kListX[column];
        const ui::Rect clip{x, kListY, kCellW, kListBottom - kListY};
        int first = scroll_[size_t(column)], rows = kRows, y0 = kListY;
        const bool half = scroll_[size_t(column)] != drawn_scroll_[size_t(column)];
        if (half) { // one in-between frame, half a row offset
            if (scroll_[size_t(column)] > drawn_scroll_[size_t(column)]) first -= 1;
            rows = 5;
            y0 = kListY - 23;
        }
        drawn_scroll_[size_t(column)] = scroll_[size_t(column)];
        for (int r = 0; r < rows; ++r) {
            const int y = y0 + kRowStep * r;
            const int i = first + r;
            if (i < 0 || i >= int(list.size())) {
                draw_icon(s, kEmptyCell, x, y, kNormal, 0, clip);
                continue;
            }
            const int item = list[size_t(i)];
            const sim::ProductionSlot* slot = nullptr;
            int look = kNormal;
            size_t icon;
            if (column == 0) {
                icon = size_t(templates_.art.building_icons + item);
                if (pl.building_slot.item >= 0) {
                    if (pl.building_slot.item == item) {
                        slot = &pl.building_slot;
                        look = kProgress;
                    } else {
                        look = kGrey;
                    }
                }
            } else {
                icon = size_t(templates_.art.unit_icons + item);
                const auto& ur = templates_.unit_records;
                bool busy = world.is_special(item);
                for (const auto& sl : pl.unit_slots) {
                    if (sl.item >= 0 && ur[size_t(sl.item)].prereq1 == ur[size_t(item)].prereq1 &&
                        !world.is_special(sl.item)) {
                        busy = true;
                    }
                }
                if (busy) {
                    look = kGrey;
                    for (const auto& sl : pl.unit_slots) {
                        if (sl.item == item) {
                            slot = &sl;
                            look = kProgress;
                            break;
                        }
                    }
                }
            }
            draw_icon(s, icon, x, y, look, slot ? slot->progress / 64 : 0, clip);
            if (!slot || half) continue;
            if (slot->hold) draw_label(s, "Hold", x, y + 16, pulse);
            if (slot->progress >= sim::kProgressDone) {
                draw_label(s, "Ready", x, y + 16, pulse);
                // FUN_0042d140 on {X, Y, X + 59, Y + 46}: lines at x = X, X + 58
                // and y = Y, Y + 45 (its right / bottom edges are exclusive).
                rect(s, x, y, x + 58, y + 45, pulse, clip);
            }
        }
    }
}

bool Sidebar::starport_item_at(int x, int y, const sim::World& world, int player, int& record, int& price, int& ax,
                               int& ay, int& limit) const {
    // FUN_0044b5c0 page 2: the name, then "$price" (none when 0).
    if (page_ != 2) return false;
    if (!(x > kListX[0] && x < kListX[1] + kCellW && y > kListY && y < kListY + kRows * kRowStep)) return false;
    const int col = std::min((x - kListX[0]) / kCellW, 1);
    const int row = (y - kListY) / kRowStep;
    const auto& pl = world.player(player);
    record = pl.starport_list[size_t(row * 2 + col)];
    if (record < 0) return false;
    price = int(pl.price[size_t(record)]);
    ax = kCellW / 2 + kListX[0] + kCellW * col;
    ay = kRowStep / 2 + kListY + kRowStep * row;
    limit = kListX[col];
    return true;
}

void Sidebar::draw_starport(ui::Surface& s, const sim::World& world, int player) {
    // FUN_00427690: 8 icons, greyed without stock, while a delivery is
    // pending or with more than 5 in the cart; then the cart count digit
    // (DATA.R16 17 + n) at (x + 4, y + 4), also "0".
    const auto& pl = world.player(player);
    uint32_t total = 0;
    for (int r : pl.starport_list) {
        if (r >= 0) total += pl.cart[size_t(r)];
    }
    for (int i = 0; i < 8; ++i) {
        const int r = pl.starport_list[size_t(i)];
        const int x = kListX[i & 1], y = kListY + kRowStep * (i >> 1);
        const ui::Rect clip{x, kListY, kCellW, kListBottom - kListY};
        if (r < 0) {
            draw_icon(s, kEmptyCell, x, y, kNormal, 0, clip);
            continue;
        }
        const bool grey = pl.stock[size_t(r)] == 0 || pl.delivery_pending || total > 5;
        draw_icon(s, size_t(templates_.art.unit_icons + r), x, y, grey ? kGrey : kNormal, 0, clip);
        const size_t digit = 17 + size_t(std::min<uint32_t>(pl.cart[size_t(r)], 9));
        if (digit < data_.size()) data_.draw_at(s, digit, x + 4, y + 4, clip);
    }
}

int Sidebar::upgrade_index(int x, int y) const {
    // The page 3 grid (0x444287 / 0x444d9f): 501 < x < 623, 184 < y < 372;
    // column 1 when x > 561.
    if (!(x > kListX[0] && x < kListX[1] + kCellW && y > kListY && y < kListBottom)) return -1;
    const int row = (y - kListY) / kRowStep;
    return row * 2 + (x > kListX[0] + kCellW ? 1 : 0);
}

bool Sidebar::upgrade_item_at(int x, int y, const sim::World& world, int player, int& record, int& cost, int& ax,
                              int& ay, int& limit) const {
    if (page_ != 3) return false;
    if (!(x > kListX[0] && x < kListX[1] + kCellW && y > kListY && y < kListY + kRows * kRowStep)) return false;
    const int col = std::min((x - kListX[0]) / kCellW, 1); // the tooltip's column rule differs at x = 561
    const int row = (y - kListY) / kRowStep;
    record = world.player(player).upgrade_list[size_t(row * 2 + col)];
    if (record < 0) return false;
    const int name = templates_.building_records[size_t(record)].name_index;
    if (!world.owns_name(player, name)) return false;
    cost = 0;
    if (world.can_upgrade(player, name)) {
        const int level = world.player(player).upgrade_level[size_t(name)];
        cost = templates_.building_records[size_t(record)].cost[size_t(std::min(level + 1, 3))];
    }
    ax = kCellW / 2 + kListX[0] + kCellW * col;
    ay = kRowStep / 2 + kListY + kRowStep * row;
    limit = kListX[col];
    return true;
}

void Sidebar::draw_upgrades(ui::Surface& s, const sim::World& world, int player) {
    // FUN_004278a0: 8 cells, column i & 1, row i >> 1.
    const auto& pl = world.player(player);
    upgrade_pulse_ = upgrade_pulse_ >= 40 ? 0 : upgrade_pulse_ + 1;
    const uint16_t pulse = rgb(upgrade_pulse_ + 20, upgrade_pulse_ + 210, upgrade_pulse_ + 20);
    const auto& slot = pl.upgrade_slot;
    for (int i = 0; i < 8; ++i) {
        const int x = kListX[i & 1], y = kListY + kRowStep * (i >> 1);
        const ui::Rect clip{x, kListY, kCellW, kListBottom - kListY};
        const int rec = pl.upgrade_list[size_t(i)];
        if (rec < 0 || !world.owns_name(player, templates_.building_records[size_t(rec)].name_index)) {
            draw_icon(s, kEmptyCell, x, y, kNormal, 0, clip);
            continue;
        }
        const size_t icon = size_t(templates_.art.building_icons + rec);
        if (slot.item >= 0) {
            if (slot.item == rec) {
                // The exe's progress buffer 0x789194 is shared with page 1's
                // building slot (docs/re/28 section 3.2), but FUN_004278a0
                // re-fills it from this icon and draws the wedge from 0 on every
                // entry to page 3 (third argument 1 unless the last page was 3,
                // 0x44912a), so drawing from the icon each loop looks the same.
                draw_icon(s, icon, x, y, kProgress, slot.progress / 64, clip);
                if (slot.hold) draw_solid_label(s, "Hold", x, y + 16, pulse); // FUN_0042e050, font 0
            } else {
                draw_icon(s, icon, x, y, kGrey, 0, clip);
            }
            continue;
        }
        const auto& r = templates_.building_records[size_t(rec)];
        const int level = pl.upgrade_level[size_t(r.name_index)];
        const bool ok = level < 3 && r.tech[size_t(level + 1)] <= pl.tech;
        draw_icon(s, icon, x, y, ok ? kNormal : kGrey, 0, clip);
    }
}

} // namespace d2k::app
