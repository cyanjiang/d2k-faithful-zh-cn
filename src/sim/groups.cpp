// Control groups and the keyboard orders: assign / select / centre on a
// group, stop, scatter, primary, next unit. RE: docs/re/25 part A.
#include "sim/world.hpp"

namespace d2k::sim {

namespace {
// 8 directions, index = facing / 4: N, NE, E, SE, S, SW, W, NW (FUN_0045de30).
constexpr int kDirs8[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};

bool excluded_from_orders(int s) {
    return s == 0x19 || s == 0x1a || s == 8 || s == 9 || s == 0x14 || s == 0x13 || s == 0x17;
}
} // namespace

void World::assign_group(int p, int d) {
    // FUN_0044b170: needs a selected own unit; the old members leave, the
    // selection joins (unit +0x28, the field the AI uses for its groups).
    if (!any_unit_selected(p)) return;
    for (auto& u : units_) {
        if (u.owner != p) continue;
        if (u.ai_group == d) u.ai_group = 0xff;
        if (u.selected) u.ai_group = uint8_t(d);
    }
}

bool World::select_group(int p, int d, bool exclusive) {
    // FUN_0046cb40: the own side only. Dying members (state 0x11) are
    // selected too; only state 8 (leaving the factory) is skipped.
    bool any = false;
    for (const auto& u : units_) any = any || (u.owner == p && u.ai_group == d && u.state != 8 && !u.remove);
    if (!any) return false;
    for (auto& u : units_) {
        if (u.owner != p) continue;
        if (u.ai_group == d && u.state != 8) {
            if (!(u.flags & 0x140)) u.selected = true;
        } else if (exclusive) {
            u.selected = false;
        }
    }
    for (auto& b : buildings_) {
        if (b.owner == p) b.selected = false;
    }
    return true;
}

void World::deselect_other_sides(int p) {
    // FUN_004433a0 0x446314 / 0x446273: for every side s != p, FUN_0046cb00
    // (units) and FUN_0046cb20 (buildings). Only in the digit's voice block.
    for (auto& u : units_) {
        if (u.owner != p) u.selected = false;
    }
    for (auto& b : buildings_) {
        if (b.owner != p) b.selected = false;
    }
}

bool World::group_position(int p, int d, int& px, int& py) const {
    // FUN_0046cbd0: the first unit of the group in list order (any state).
    for (const auto& u : units_) {
        if (u.owner == p && u.ai_group == d && !u.remove) {
            px = u.x >> 16;
            py = u.y >> 16;
            return true;
        }
    }
    return false;
}

void World::order_stop(int p) {
    // S: order 0x1c (FUN_004576b0): the selected units finish their step and
    // stop; order 0x1d puts selected buildings in state 2.
    for (auto& u : units_) {
        if (u.owner != p || !u.selected || u.state == 0x11 || u.state == 0x14 || (u.flags & 0x400140)) continue;
        if (excluded_from_orders(u.state)) continue;
        if (!set_state(u, state::kMove)) continue;
        u.next_state = 1;
        u.dest_x = u.to_x;
        u.dest_y = u.to_y;
    }
    // Order 0x1d (FUN_00457710): building state 2, the target is kept
    // (docs/re/38 section 6). The exe's list is shared with the units (64).
    int n = 0;
    for (const auto& u : units_) n += u.owner == p && u.selected && u.state != 0x11;
    for (auto& b : buildings_) {
        if (b.owner != p || !b.selected || b.removed || b.dying || n >= 64) continue;
        ++n;
        b.state = 2;
    }
}

void World::order_scatter(int p) {
    // X: order 0x19 (FUN_00457580): up to 8 random neighbours (one rand()
    // per try), else a free adjacent cell, else move aside (state 9).
    for (auto& u : units_) {
        if (u.owner != p || !u.selected || u.state == 0x11 || u.state == 0x14 || (u.flags & 0x400140)) continue;
        if (excluded_from_orders(u.state)) continue;
        bool done = false;
        for (int k = 0; k < 8 && !done; ++k) {
            const int d = int(rng_.next() & 7);
            const int nx = u.cur_x + kDirs8[d][0], ny = u.cur_y + kDirs8[d][1];
            if (passable(nx, ny, u, 0) && set_state(u, state::kMove)) {
                u.next_state = 1;
                u.dest_x = uint8_t(nx);
                u.dest_y = uint8_t(ny);
                done = true;
            }
        }
        if (done) continue;
        int x, y;
        if (free_adjacent_cell(u, u.cur_x, u.cur_y, 0, x, y)) {
            if (set_state(u, state::kMove)) {
                u.next_state = 1;
                u.dest_x = uint8_t(x);
                u.dest_y = uint8_t(y);
            }
        } else if (free_adjacent_cell(u, u.cur_x, u.cur_y, 2, x, y)) {
            if (set_state(u, 9)) {
                u.next_state = state::kMove;
                u.dest_x = uint8_t(x);
                u.dest_y = uint8_t(y);
            }
        }
    }
}

bool World::can_be_primary(int p, int bi) const {
    // FUN_004432f0: the side owns more than one building of this name
    // (player +0x245c1) and the kind has a primary slot (not 10).
    const auto& r = templates_.building_records[size_t(buildings_[size_t(bi)].record)];
    switch (r.kind) {
    case 2: case 7: case 8: case 9: case 0xb: case 0xc: case 0xd: case 0x11: case 0x12: case 0x13: break;
    default: return false;
    }
    int count = 0;
    for (const auto& b : buildings_) {
        if (b.owner == p && !b.removed && templates_.building_records[size_t(b.record)].name_index == r.name_index) {
            ++count;
        }
    }
    return count > 1;
}

void World::order_primary(int p) {
    // FUN_00459ea0 (P, or a click on the selected building): order 0x20
    // with the selected buildings; ProcessOrder only uses the first one,
    // FUN_0044b1f0. The old primary loses 0x90 (kinds 2, 7-11) or 0x80
    // (kinds 12, 13, 17-19); the new one gets 0x80.
    auto& pl = players_[size_t(p)];
    for (auto& b : buildings_) {
        if (b.owner != p || !b.selected || b.removed) continue;
        const int kind = templates_.building_records[size_t(b.record)].kind;
        const bool wide = kind == 2 || (kind >= 7 && kind <= 11);
        if (wide || kind == 12 || kind == 13 || (kind >= 17 && kind <= 19)) {
            const int old = pl.primary[size_t(kind)];
            if (old >= 0) buildings_[size_t(old)].flags &= wide ? ~0x90u : ~0x80u;
            pl.primary[size_t(kind)] = b.index;
            b.flags |= 0x80;
        }
        return;
    }
}

void World::next_unit(int p) {
    // N: FUN_004a5de0: the next valid unit after the first selected one,
    // else the first valid unit; no voice.
    auto valid = [&](const Unit& u) {
        return u.owner == p && !u.remove && u.state != 8 && u.state != 0x11 && !(u.flags & 0x140);
    };
    int first = -1;
    for (size_t i = 0; i < units_.size(); ++i) {
        if (valid(units_[i]) && units_[i].selected) {
            first = int(i);
            break;
        }
    }
    deselect_all();
    int pick = -1;
    if (first >= 0) {
        for (size_t i = size_t(first) + 1; i < units_.size() && pick < 0; ++i) {
            if (valid(units_[i])) pick = int(i);
        }
    }
    for (size_t i = 0; i < units_.size() && pick < 0; ++i) {
        if (valid(units_[i])) pick = int(i);
    }
    if (pick >= 0) units_[size_t(pick)].selected = true;
}

bool World::selection_centre(int p, int& px, int& py) const {
    // FUN_004a5c00 (units), else FUN_004a5cd0 (buildings): the mean position.
    uint32_t sx = 0, sy = 0, n = 0;
    for (const auto& u : units_) {
        if (u.owner == p && u.selected && !u.remove) {
            sx += uint32_t(u.x >> 16);
            sy += uint32_t(u.y >> 16);
            ++n;
        }
    }
    if (n == 0) {
        for (const auto& b : buildings_) {
            if (b.owner != p || !b.selected || b.removed) continue;
            const auto& r = templates_.building_records[size_t(b.record)];
            sx += uint32_t((b.x >> 16) + (r.width_px >> 1));
            sy += uint32_t((b.y >> 16) - (r.height_px >> 1));
            ++n;
        }
    }
    if (n == 0) return false;
    px = int(sx / n);
    py = int(sy / n);
    return true;
}

} // namespace d2k::sim
