// Harvesters: finding and eating spice, returning to a refinery, docking and
// unloading. RE: docs/re/13-harvesting.md (FUN_00497b90 states 0xb..0x10).
#include <algorithm>
#include <cstdlib>

#include "sim/world.hpp"

namespace d2k::sim {

namespace {
constexpr uint32_t kSpiceMask = cell::kSpiceLevelMask;
constexpr uint32_t kUnitOrBuilding = 0x3f8;
constexpr uint32_t kStepReserved = 0x10000000; // unit flag: the "to" cell is reserved
constexpr int kKindRefinery = 4;
constexpr int kCapacity = 7;       // loads; no record field
constexpr int kDockFacing = 12;

namespace st {
constexpr int kIdle = 2, kToSpice = 0xb, kHarvest = 0xc, kToRefinery = 0xd, kDock = 0xe, kUnload = 0xf,
              kUndock = 0x10;
} // namespace st
} // namespace

bool World::valid_refinery(int building) const {
    return building >= 0 && size_t(building) < buildings_.size() && !buildings_[size_t(building)].removed &&
           templates_.building_records[size_t(buildings_[size_t(building)].record)].kind == kKindRefinery;
}

int World::own_refinery(const Unit& u) const {
    // FUN_004a4740(side, +0x58) then the kind-4 test: the building now in
    // that slot of the harvester's own side (docs/re/50 section 6). A
    // deviated or captured harvester keeps the old side's slot number
    // (ConvertUnit copies the first 0x7C bytes), which names whatever
    // building the new side holds in that slot.
    const int r = refinery_of(u);
    return valid_refinery(r) ? r : -1;
}
bool World::dock_cell(int building, int& x, int& y) const {
    if (!valid_refinery(building)) return false;
    const auto& b = buildings_[size_t(building)];
    const auto& r = templates_.building_records[size_t(b.record)];
    x = (b.x >> 16) / 32 + r.spawn_x; // record +0x60 / +0x61: (2, 1)
    y = ((b.y >> 16) - r.height_px) / 32 + r.spawn_y;
    return true;
}

int World::least_used_refinery(int p) const {
    int best = -1, best_count = 0;
    for (const auto& b : buildings_) {
        if (b.owner != p || !valid_refinery(b.index)) continue;
        // FUN_004a5e80 counts the side's harvesters (record kind 1) whose
        // +0x58 slot holds this building.
        int count = 0;
        for (const auto& u : units_) {
            count += u.owner == p && !u.remove && u.refinery == int(b.slot) &&
                     templates_.unit_records[size_t(u.record)].kind == 1;
        }
        if (best < 0 || count < best_count) {
            best = b.index;
            best_count = count;
        }
    }
    return best;
}

int World::nearest_refinery(int p, int x, int y) const {
    int best = -1, best_d = 0;
    for (const auto& b : buildings_) {
        if (b.owner != p || !valid_refinery(b.index)) continue;
        // FUN_004a5f80 measures to +0x18 / +0x19: the largest x / y of the
        // building's mask-0x54 cells (doc 12 section 8), not the top-left.
        const uint16_t fp = templates_.building_records[size_t(b.record)].footprint;
        int mx = b.cell_x, my = b.cell_y;
        for (int k = 0; k < 16; ++k) {
            if (!(fp & (1u << k))) continue;
            mx = std::max(mx, b.cell_x + (k & 3));
            my = std::max(my, b.cell_y + (k >> 2));
        }
        const int d = std::abs(mx - x) + std::abs(my - y);
        if (best < 0 || d < best_d) {
            best = b.index;
            best_d = d;
        }
    }
    return best;
}

bool World::free_cell_near(int p, int& x, int& y) const {
    // FUN_0049e290: the last cell in the 3x3 (rows, then columns) with no
    // unit / building bits that vehicles can drive on: found, true. Else a
    // second pass writes the last cell of the 3x3 that carries this
    // player's owner bits, is not a building-only cell ((f & 0x3f8) != 0x10)
    // and vehicles can drive on, and returns false all the same; the
    // callers ignore the result and use the coordinates (docs/re/13
    // section 5.3).
    const int x0 = std::max(x - 1, 0), x1 = std::min(x + 2, map_.width());
    const int y0 = std::max(y - 1, 0), y1 = std::min(y + 2, map_.height());
    int fx = -1, fy = -1;
    for (int sy = y0; sy < y1; ++sy) {
        for (int sx = x0; sx < x1; ++sx) {
            const uint32_t f = map_.at(sx, sy).flags;
            if (!(f & kUnitOrBuilding) && (f & cell::kVehicles)) {
                fx = sx;
                fy = sy;
            }
        }
    }
    if (fx >= 0) {
        x = fx;
        y = fy;
        return true;
    }
    for (int sy = y0; sy < y1; ++sy) {
        for (int sx = x0; sx < x1; ++sx) {
            const uint32_t f = map_.at(sx, sy).flags;
            if (int(f & 7) == (p & 0xff) && (f & kUnitOrBuilding) != 0x10 && (f & cell::kVehicles)) {
                x = sx;
                y = sy;
            }
        }
    }
    return false;
}

bool World::prefer_thick(int& x, int& y) const {
    const int level = map_.spice_level(x, y);
    if (level == 3 || level == 4) return false;
    const int x0 = x, y0 = y;
    bool matched = false;
    for (int sy = std::max(y0 - 1, 0); sy <= std::min(y0 + 1, map_.height() - 1); ++sy) {
        for (int sx = std::max(x0 - 1, 0); sx <= std::min(x0 + 1, map_.width() - 1); ++sx) {
            if (map_.spice_level(sx, sy) >= 3 && (sx != x || sy != y)) {
                x = sx; // each match overwrites the output
                y = sy;
                matched = true;
            }
        }
    }
    return matched;
}

bool World::find_spice(const Unit& u, int& x, int& y) const {
    if (map_.at(u.home_x, u.home_y).flags & kSpiceMask) {
        x = u.home_x;
        y = u.home_y;
        return true;
    }
    int px = u.home_x, py = u.home_y;
    if (const int ri = refinery_of(u); valid_refinery(ri)) {
        const auto& b = buildings_[size_t(ri)];
        px = (b.x >> 16) / 32;
        py = (b.y >> 16) / 32;
    }
    const int w = map_.width(), h = map_.height();
    int best = w + h, best_p = 0, bx = -1, by = -1;
    for (int cy = 0; cy < h; ++cy) {
        for (int cx = 0; cx < w; ++cx) {
            const uint32_t f = map_.at(cx, cy).flags;
            if (!(f & kSpiceMask) || (f & kUnitOrBuilding)) continue;
            const int d = std::abs(cx - u.home_x) + std::abs(cy - u.home_y);
            const int dp = std::abs(cx - px) + std::abs(cy - py);
            if (d < best || (d == best && bx >= 0 && dp < best_p)) {
                best = d;
                best_p = dp;
                bx = cx;
                by = cy;
            }
        }
    }
    if (bx < 0) return false;
    x = bx;
    y = by;
    prefer_thick(x, y);
    return true;
}

void World::eat_spice(int x, int y) {
    auto& c = map_.at(x, y);
    const int level = map_.spice_level(x, y);
    int next;
    uint16_t tile;
    switch (level) {
    case 7: case 6: next = 2; tile = 0x2ed; break;
    case 5: next = 4; tile = 0x2ed; break;
    case 4: next = 3; tile = 0x2ed; break;
    case 3: next = 2; tile = 0x319; break;
    case 2: next = 1; tile = 0x319; break;
    default: next = 0; tile = tables_.harvested_sand[rng_.next() & 7]; break;
    }
    c.tile = tile;
    c.flags = (c.flags & ~kSpiceMask) | (uint32_t(next) << cell::kSpiceLevelShift);
    // 1 -> 0: the radar pixel becomes sand (0x499500, FUN_004620b0 with
    // 0x5173f0); the other steps leave it (docs/re/13 section 3.1).
    if (next == 0) c.radar = radar_colour::kSand;
    map_.smooth_spice({x - 1, y - 1, x + 2, y + 2});
}

void World::order_dock(int owner, int building) {
    for (auto& u : units_) {
        // ProcessOrder case 4: FUN_0049f4a0(u, 1), fields only when SetState succeeds.
        if (u.owner != owner || !u.selected || u.remove || !is_harvester(u) || !takes_orders_forced(u)) continue;
        if (!set_state(u, st::kToRefinery)) continue;
        u.next_state = 1;
        u.refinery = building_slot(building); // case 4 (0x457a14): the record's index as it is
    }
}

void World::tick_harvester(Unit& u) {
    auto& pl = players_[size_t(u.owner)];
    auto wait = [&] {
        if (u.timer == 0) return false;
        --u.timer;
        return true;
    };
    switch (u.state) {
    case st::kToSpice:
        if (move_step(u) == 0) set_state(u, st::kHarvest);
        else carry_request(u, vars.drive_distance); // not arrived: maybe a carryall (docs/re/33 section 2.1)
        return;

    case st::kHarvest: {
        if (wait()) return;
        if (u.load > kCapacity - 1) {
            set_state(u, st::kToRefinery);
            return;
        }
        int nx = u.cur_x, ny = u.cur_y;
        if (prefer_thick(nx, ny)) {
            // FUN_00497b90 case 10: passable (FUN_0045d990), or the unit that
            // FUN_004a5270 finds on N's top-left pixel is not a harvester
            // (record kind != 1): go to N. Else state 0xb with dest and home
            // unchanged, so the next tick arrives where it stands and comes
            // back here: it waits beside the thick cell and does not eat its
            // own (docs/re/13 section 1.2).
            bool go = passable(nx, ny, u, 0);
            if (!go) {
                bool inf = false;
                const int vi = unit_in_cell(nx, ny, inf);
                go = vi >= 0 && templates_.unit_records[size_t(units_[size_t(vi)].record)].kind != 1;
            }
            if (go) {
                u.dest_x = u.home_x = uint8_t(nx);
                u.dest_y = u.home_y = uint8_t(ny);
            }
            set_state(u, st::kToSpice);
            return;
        }
        u.home_x = u.cur_x;
        u.home_y = u.cur_y;
        if (!(map_.at(u.cur_x, u.cur_y).flags & kSpiceMask)) {
            int x, y;
            if (find_spice(u, x, y)) {
                set_state(u, st::kToSpice);
                u.dest_x = u.home_x = uint8_t(x);
                u.dest_y = u.home_y = uint8_t(y);
            } else {
                set_state(u, u.load != 0 ? st::kToRefinery : st::kIdle);
            }
            return;
        }
        eat_spice(u.cur_x, u.cur_y);
        u.timer = uint8_t(vars.load_delay);
        ++u.load;
        // Dust by body facing: EX_HARVDUST0..7 (0x6d6119[facing >> 2]).
        const std::string dust = "EX_HARVDUST" + std::to_string((u.facing >> 2) & 7);
        spawn_effect(dust.c_str(), u.x >> 16, u.y >> 16, u.owner);
        return;
    }

    case st::kToRefinery: {
        // Every path ends with the carryall check (docs/re/33 section 2.1).
        [&] {
            int dx, dy;
            if (!valid_refinery(refinery_of(u))) {
                const int r = nearest_refinery(u.owner, u.cur_x, u.cur_y);
                if (r < 0) {
                    if (u.steps == 0) set_state(u, st::kIdle);
                    else move_step(u);
                    return;
                }
                dock_cell(r, dx, dy);
                u.dest_x = uint8_t(dx);
                u.dest_y = uint8_t(dy);
                u.refinery = building_slot(r);
                if (move_step(u) == 0) {
                    u.frame = 0;
                    set_state(u, st::kDock);
                }
                return;
            }
            dock_cell(refinery_of(u), dx, dy);
            u.dest_x = uint8_t(dx);
            u.dest_y = uint8_t(dy);
            move_step(u);
            if ((u.cur_x == dx && u.cur_y == dy) || ((u.flags & kStepReserved) && u.to_x == dx && u.to_y == dy)) {
                u.frame = 0;
                set_state(u, st::kDock);
            }
            // 0x499900..0x499952 (FUN_00497b90 case 0xb): the unit FUN_004a5270 finds
            // on the dock cell's top-left pixel moves aside (state 9) when it
            // takes orders (FUN_0049f4a0(v, 0)), is idle or guarding (state 2
            // or 3) and its side is allied to ours (0x798830[ours * 8 +
            // its] == 0). Only on this "own refinery valid" path.
            bool inf = false;
            const int vi = unit_in_cell(dx, dy, inf);
            if (vi >= 0) {
                Unit& v = units_[size_t(vi)];
                if (takes_orders(v) && (v.state == 2 || v.state == 3) &&
                    alliance_[size_t(u.owner)][size_t(v.owner)] == 0) {
                    set_state(v, 9);
                }
            }
        }();
        carry_request(u, vars.drive_distance);
        return;
    }

    case st::kDock: {
        if (u.steps != 0) {
            move_step(u);
            return;
        }
        int dx, dy;
        if (u.load == 0 || !dock_cell(refinery_of(u), dx, dy) || u.cur_x != dx || u.cur_y != dy) {
            set_state(u, st::kUndock);
            return;
        }
        if (turn_body(u, kDockFacing)) return;
        if (u.timer == 0) {
            u.timer = 5;
            ++u.frame;
        } else {
            --u.timer;
        }
        const int harv = templates_.explosion_index("EX_HARV");
        const int last = harv >= 0 ? templates_.art.explosion_frames[size_t(harv)] - 1 : 10;
        if (u.frame >= last) {
            set_state(u, st::kUnload);
            u.timer = uint8_t(vars.unload_delay);
        }
        return;
    }

    case st::kUnload: {
        if (wait()) return;
        int dx, dy;
        if (dock_cell(refinery_of(u), dx, dy) && u.cur_x == dx && u.cur_y == dy && u.load != 0) {
            u.timer = uint8_t(vars.unload_delay);
            pl.pending_spice += vars.blob_value; // FUN_0046c790
            --u.load;
            const auto& b = buildings_[size_t(refinery_of(u))];
            spawn_effect("EX_REF", (b.x >> 16) + vars.refinery_fx_x, (b.y >> 16) + vars.refinery_fx_y, u.owner);
            pl.spice_harvested += vars.blob_value; // +0x2477c (the score, docs/re/24)
            return;
        }
        set_state(u, st::kUndock);
        return;
    }

    case st::kUndock: {
        if (wait()) return;
        u.timer = 5;
        if (u.frame != 0) {
            --u.frame;
            return;
        }
        int x, y;
        if (find_spice(u, x, y)) {
            set_state(u, st::kToSpice);
            u.dest_x = uint8_t(x);
            u.dest_y = uint8_t(y);
            return;
        }
        x = u.dest_x;
        y = u.dest_y;
        free_cell_near(u.owner, x, y);
        set_state(u, st::kToSpice);
        u.dest_x = uint8_t(x);
        u.dest_y = uint8_t(y);
        return;
    }
    default:
        return;
    }
}

} // namespace d2k::sim
