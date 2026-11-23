// The computer player's base rebuilding: the initial-building list,
// MonitorBuildings and RebuildBuildings with its concrete quarters.
// RE: docs/re/22-ai-core.md section 3.4.
#include "sim/world.hpp"

#include <cstring>

namespace d2k::sim {

namespace {
// AI block fields (doc 22 sections 1.2 / 1.3 / 3.4).
constexpr size_t kPractice = 0x03, kMaySpend = 0x09, kWaitCounter = 0x0C, kStartNext = 0x1F;
constexpr size_t kInitial = 0x5AC;          // 16-byte entries: x, y, u16 index, u32, u32 timer, record, alive
constexpr size_t kInitialCount = 0xF0C;
constexpr size_t kRectX0 = 0xF2C, kRectY0 = 0xF30, kRectX1 = 0xF34, kRectY1 = 0xF38;
constexpr size_t kCashStashBuildings = 0x1068; // CashStash[BuildBuildings]
constexpr size_t kTimeBeforeRebuilding = 0x153C; // u32[100] by record
constexpr size_t kBuildRatio = 0x16CC;      // f32[100] by name group
constexpr size_t kMaxBuildings = 0x185C;    // u32[100] by name group
constexpr size_t kRebuildFlag = 0x1D45;
constexpr size_t kCellX = 0x1D47, kCellY = 0x1D48, kRebuilding = 0x1D49, kRecord = 0x1D4A;
constexpr size_t kPlaceTries = 0x1D4C, kQuarters = 0x1D50;
constexpr int kKindWall = 0xE, kKindConcrete = 0xF;
// The search spiral (0x4eb2d8 / 0x4eb2dc / 0x4eb2e0).
constexpr int kSpiralDx[4] = {-1, 0, 1, 0}, kSpiralDy[4] = {0, 1, 0, -1}, kSpiralGrow[4] = {0, 1, 0, 1};
} // namespace

void World::ai_initial_buildings(int s) {
    // FUN_0041e920 "GetInitialBuildingPositions", at activation, unless
    // practice: MaxBuildings is zeroed and recounted from the side's
    // buildings; every non-wall building is listed with its top-left cell.
    if (ai_u8(s, kPractice)) return;
    for (size_t g = 0; g < 100; ++g) set_ai_u32(s, kMaxBuildings + 4 * g, 0);
    set_ai_u8(s, kInitialCount, 0);
    for (const auto& b : buildings_) {
        if (b.owner != s || b.removed) continue;
        const auto& r = templates_.building_records[size_t(b.record)];
        if (r.kind == kKindWall) continue;
        const int n = ai_u8(s, kInitialCount);
        if (n >= 0x96) break; // "Too many initial buildings"
        const size_t e = kInitial + size_t(n) * 0x10;
        for (size_t k = 0; k < 0x10; ++k) set_ai_u8(s, e + k, 0);
        set_ai_u8(s, e + 0xC, uint8_t(b.record));
        set_ai_u8(s, e + 0x0, uint8_t((b.x >> 16) >> 5));
        set_ai_u8(s, e + 0x1, uint8_t(((b.y >> 16) >> 5) - (r.height_px >> 5)));
        set_ai_u8(s, e + 0x2, uint8_t(b.slot & 0xff)); // +0x8c, the pool slot (0x41ea10)
        set_ai_u8(s, e + 0x3, uint8_t((b.slot >> 8) & 0xff));
        set_ai_u8(s, e + 0xD, 1);
        const size_t g = kMaxBuildings + 4 * size_t(r.name_index);
        set_ai_u32(s, g, ai_u32(s, g) + 1);
        set_ai_u8(s, kInitialCount, uint8_t(n + 1));
    }
}

void World::ai_monitor_buildings(int s) {
    // FUN_0041eab0 "MonitorBuildings", when tick & 7 == side: queued sells
    // (flag 0x2000000 without 0x4000000), then dead initial buildings.
    if (int(tick_count_ & 7) != s) return;
    for (size_t i = 0; i < buildings_.size(); ++i) {
        Building& b = buildings_[i];
        if (b.owner != s || b.removed) continue;
        if ((b.flags & 0x2000000) && !(b.flags & 0x4000000)) {
            b.flags &= ~0x2000000u;
            order_sell(s, b.index); // MonitorBuildings sell: the handler FUN_00456ee0 directly (0x41eb14)
            buildings_[i].flags |= 0x4000000;
        }
    }
    const int n = ai_u8(s, kInitialCount);
    for (int k = 0; k < n; ++k) {
        const size_t e = kInitial + size_t(k) * 0x10;
        const int slot = ai_u8(s, e + 2) | (ai_u8(s, e + 3) << 8);
        // 0x41eb2b..0x41eb59: the slot's +0x30 dword (side + 0x60 + slot * 0x94) is 0x11, with no kind test, read
        // only in this side's tick & 7 pass. The slot is never refreshed: a
        // building sold (dying for one DoAI pass) or captured (one or two)
        // is mostly missed, and the entry then fires for whichever building
        // of the side holds the slot when that one is seen dying. A unit's
        // +0x30 (attacker side, fade, attacker) or a free slot never reads
        // 0x11; a bullet or explosion would need that exact raw dword there
        // (not modelled, docs/re/50 section 6).
        const Building* b = building_in_slot(s, slot);
        if (!b || !b->dying || !ai_u8(s, e + 0xD)) continue;
        set_ai_u8(s, e + 0xD, 0);
        set_ai_u32(s, e + 4, 0);
        set_ai_u32(s, e + 8, ai_u32(s, kTimeBeforeRebuilding + 4 * size_t(ai_u8(s, e + 0xC))));
    }
}

void World::ai_near_own(int s, int x, int y, uint8_t& f) const {
    // FUN_00428c30: bit 1 = an own building cell, bit 2 = own concrete
    // (owner bits 17-19); row 0 is skipped.
    if (x < 0 || x >= map_.width() || y <= 0 || y >= map_.height()) return;
    const uint32_t c = map_.at(x, y).flags;
    if ((c & 0x10) && int(c & 7) == s) f |= 1;
    if ((c & 0x800) && int((c >> 17) & 7) == s) f |= 2;
}

uint32_t World::ai_can_place_building(int s, int record, int x, int y) const {
    // FUN_00421000 "CanAIPlaceBuilding": every cell of the 0x50 mask must be
    // inside (1..size-1), an own building or own concrete must be within 2
    // cells of one of them, and none may hold a unit or building
    // (0x13f8) or be unbuildable. Returns 1 + the number of scanned
    // neighbour cells without a building, or 0.
    const uint16_t mask = templates_.building_records[size_t(record)].place_mask;
    const int W = map_.width(), H = map_.height();
    uint8_t near = 0;
    bool out = false;
    uint32_t free_cells = 0;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (!(mask & (1u << (row * 4 + col)))) continue;
            const int cx = x + col, cy = y + row;
            if (!(cx < W && cx > 0 && cy < H && cy > 0)) {
                out = true;
                continue;
            }
            const int y0 = cy - 2 < 1 ? 0 : cy - 2, y1 = std::min(cy + 3, H);
            const int x0 = cx - 2 < 1 ? 0 : cx - 2, x1 = std::min(cx + 3, W);
            for (int yy = y0; yy < y1; ++yy) {
                for (int xx = x0; xx < x1; ++xx) {
                    ai_near_own(s, xx, yy, near);
                    if (!(map_.at(xx, yy).flags & 0x10)) ++free_cells;
                }
            }
        }
    }
    if (!(near & 1) || out) return 0;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (!(mask & (1u << (row * 4 + col)))) continue;
            const int cx = x + col, cy = y + row;
            if (cx >= W || cy >= H) continue;
            const uint32_t c = map_.at(cx, cy).flags;
            if ((c & 0x13f8) || !(c & 0x8000) || cy < 1) return 0;
        }
    }
    return free_cells + 1;
}

bool World::ai_can_place_concrete(int s, int record, int x, int y) const {
    // FUN_004212a0 "CanAIPlaceConcrete": near an own building or own
    // concrete (within 2 cells of the slab's 0x54 cells), and one of the
    // slab cells (row > 0) is buildable without concrete.
    const uint16_t mask = templates_.building_records[size_t(record)].footprint;
    const int W = map_.width(), H = map_.height();
    uint8_t near = 0;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (!(mask & (1u << (row * 4 + col)))) continue;
            const int cx = x + col, cy = y + row;
            if (cx >= W || cy >= H) continue;
            const int y0 = cy - 2 < 1 ? 0 : cy - 2, y1 = std::min(cy + 3, H);
            const int x0 = cx - 2 < 1 ? 0 : cx - 2, x1 = std::min(cx + 3, W);
            for (int yy = y0; yy < y1; ++yy) {
                for (int xx = x0; xx < x1; ++xx) ai_near_own(s, xx, yy, near);
            }
        }
    }
    if (near == 0) return false;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (!(mask & (1u << (row * 4 + col)))) continue;
            const int cx = x + col, cy = y + row;
            if (cx >= W || cy >= H || cy <= 0) continue;
            const uint32_t c = map_.at(cx, cy).flags;
            if (!(c & 0x800) && (c & 0x8000)) return true;
        }
    }
    return false;
}

bool World::ai_concrete_already(int x, int y) const {
    // FUN_00421a90 "ConcreteAlready": the 2 x 2 at (x, y) is all concrete;
    // false within 2 cells of the right / bottom edge.
    if (x >= map_.width() - 2 || y >= map_.height() - 2) return false;
    for (int yy = y; yy < y + 2; ++yy) {
        for (int xx = x; xx < x + 2; ++xx) {
            if (!(map_.at(xx, yy).flags & 0x800)) return false;
        }
    }
    return true;
}

bool World::ai_no_friendly_units(int s, int record, int x, int y) const {
    // FUN_00423500 "NoFriendlyUnits": no buildable slab cell holds an own
    // unit or building (0x3f8, cell owner = side).
    const uint16_t mask = templates_.building_records[size_t(record)].footprint;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (!(mask & (1u << (row * 4 + col)))) continue;
            const int cx = x + col, cy = y + row;
            if (cx >= map_.width() || cy >= map_.height()) continue;
            const uint32_t c = map_.at(cx, cy).flags;
            if ((c & 0x8000) && (c & 0x3f8) && int(c & 7) == s) return false;
        }
    }
    return true;
}

int World::ai_best_building(int s) const {
    // FUN_00420d50 "GetBestBuildingToBuild": the lowest (owned + 1) /
    // BuildRatio over the build list; a name group, or -1.
    const auto& pl = players_[size_t(s)];
    const uint32_t money = uint32_t(pl.cash + pl.spice);
    int pick = -1;
    float best = 100000.0f;
    for (int rec : pl.building_list) {
        const auto& r = templates_.building_records[size_t(rec)];
        const int g = r.name_index;
        // The group counter P+0x245C1[g] (0x420dd3): buildings being built,
        // sold or dying count until they leave the map; a captured one stops
        // counting at once (flag 0x10000, see owned_building_kind).
        uint32_t owned = 0;
        for (const auto& b : buildings_) {
            if (b.owner == s && !b.removed && !(b.flags & 0x10000) &&
                templates_.building_records[size_t(b.record)].name_index == g) {
                ++owned;
            }
        }
        const float score = float(owned + 1) / ai_f32(s, kBuildRatio + 4 * size_t(g));
        if (!(money > ai_u32(s, kCashStashBuildings) || ai_u8(s, kMaySpend))) continue;
        if (!(score < best)) continue;
        if (owned >= ai_u32(s, kMaxBuildings + 4 * size_t(g))) continue;
        const int mine = templates_.building_record(g, pl.house); // FUN_0046c450
        if (mine < 0 || templates_.building_records[size_t(mine)].kind == kKindConcrete) continue;
        if (!ai_u8(s, kPractice) && !ai_need_building(s, g)) continue;
        pick = g;
        best = score;
    }
    return pick;
}

bool World::ai_need_building(int s, int group) const {
    // FUN_004218e0 "NeedBuilding": a dead initial building of this group.
    const int n = ai_u8(s, kInitialCount);
    for (int k = 0; k < n; ++k) {
        const size_t e = kInitial + size_t(k) * 0x10;
        if (ai_u8(s, e + 0xD) == 0 &&
            templates_.building_records[size_t(ai_u8(s, e + 0xC))].name_index == group) {
            return true;
        }
    }
    return false;
}

bool World::ai_alternative_place(int& x, int& y) const {
    // FUN_00421b60 "FindAlternativePlace": the last cell of x-1..x+1,
    // y-1..y+1 (clamped) with no unit or building and drivable (0x2000).
    bool found = false;
    const int y0 = y - 1 < 1 ? 0 : y - 1, y1 = std::min(y + 2, map_.height() - 1);
    const int x0 = x - 1 < 1 ? 0 : x - 1, x1 = std::min(x + 2, map_.width() - 1);
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            const uint32_t c = map_.at(xx, yy).flags;
            if (!(c & 0x3f8) && (c & 0x2000)) {
                x = xx;
                y = yy;
                found = true;
            }
        }
    }
    return found;
}

bool World::ai_new_position(int s, int record, int& out_x, int& out_y) const {
    // FUN_00421950 "FindNewPositonForBuilding": the old cell of a dead
    // initial building of this record (the last that fits wins); else a
    // cell near it (FindAlternativePlace, which moves the probe each time).
    bool any_building = false;
    for (const auto& b : buildings_) any_building = any_building || (b.owner == s && !b.removed);
    if (!any_building) return false; // the side's building list is empty
    const int n = ai_u8(s, kInitialCount);
    int px = 0, py = 0;
    bool found = false;
    for (int k = 0; k < n; ++k) {
        const size_t e = kInitial + size_t(k) * 0x10;
        if (ai_u8(s, e + 0xD) != 0 || ai_u8(s, e + 0xC) != record) continue;
        px = ai_u8(s, e + 0);
        py = ai_u8(s, e + 1);
        if (ai_can_place_building(s, record, px, py)) {
            found = true;
            out_x = px;
            out_y = py;
        }
    }
    if (found) return true;
    for (int k = 0; k < n; ++k) {
        const size_t e = kInitial + size_t(k) * 0x10;
        if (ai_u8(s, e + 0xD) != 0 || ai_u8(s, e + 0xC) != record) continue;
        ai_alternative_place(px, py); // from the probe left by the first pass
        if (ai_can_place_building(s, record, px, py)) {
            found = true;
            out_x = px;
            out_y = py;
        }
    }
    return found;
}

bool World::ai_random_position(int s, int record, int& out_x, int& out_y, int tries) {
    // FUN_00420e90 "FindRandomBuildingPosition": `tries` random cells in the
    // base box (x min - 4 .. x max + 2, y likewise), the best
    // CanAIPlaceBuilding score wins. One rand() for x, then one for y.
    bool any_building = false;
    for (const auto& b : buildings_) any_building = any_building || (b.owner == s && !b.removed);
    if (!any_building) return false;
    int bx0, bx1, by0, by1;
    base_box(s, bx0, bx1, by0, by1);
    const int x0 = bx0 - 4 < 1 ? 0 : bx0 - 4, y0 = by0 - 4 < 1 ? 0 : by0 - 4;
    const int x1 = std::min(bx1 + 2, map_.width() - 1), y1 = std::min(by1 + 2, map_.height() - 1);
    uint32_t best = 0;
    for (int k = 0; k < tries; ++k) {
        const uint32_t rx = rng_.next(), ry = rng_.next();
        // Unsigned DIV (0x420f8b, 0x420fad), the cell added as a byte. An
        // inverted box (docs/re/53 section 1.6: the "x0 == 0 and y0 == 0"
        // case with no units) gives a huge divisor, so the cell is
        // (rand + x0) & 0xFF, tried like any other. A zero width is an
        // integer division fault in the exe (a crash); that try is skipped.
        if (x1 == x0 || y1 == y0) continue;
        const int x = uint8_t(rx % uint32_t(x1 - x0) + uint32_t(x0));
        const int y = uint8_t(ry % uint32_t(y1 - y0) + uint32_t(y0));
        const uint32_t v = ai_can_place_building(s, record, x, y);
        if (best < v) {
            out_x = x;
            out_y = y;
            best = v;
        }
    }
    return best != 0;
}

bool World::ai_place_to_move(int x0, int y0, int x1, int y1, int range, int& x, int& y) const {
    // FUN_00423220 "GetPlaceToMoveTo": spiral from (x, y) to the first cell
    // outside the rectangle that is walkable and drivable with no unit or
    // building (flags & 0x63f8 == 0x6000).
    int cx = x, cy = y, dir = 0, leg = 1, left = 1;
    const int limit = range * 2 + 2;
    while (leg < limit) {
        cx += kSpiralDx[dir];
        cy += kSpiralDy[dir];
        if (cx >= 0 && cy >= 0 && cx < map_.width() && cy < map_.height() &&
            (cx < x0 || cy < y0 || cx > x1 || cy > y1) && (map_.at(cx, cy).flags & 0x63f8) == 0x6000) {
            x = cx;
            y = cy;
            return true;
        }
        if (--left == 0) {
            leg += kSpiralGrow[dir];
            dir = (dir + 1) & 3;
            left = leg;
        }
    }
    return false;
}

void World::ai_kick_units(int s, int x0, int y0, int x1, int y1, int exclude) {
    // FUN_00423100 "KickUnitsOutofArea": the first own unit in the
    // rectangle (by its pixel cell) that takes orders and is idle or
    // guarding is sent out of it. The exclusion is an object pointer: the
    // rebuild passes 0 (nothing excluded), DealWithMCV its MCV.
    for (auto& u : units_) {
        if (u.owner != s || u.remove || u.index == exclude) continue;
        const int ux = (u.x >> 16) >> 5, uy = (u.y >> 16) >> 5;
        if (!(x0 <= ux && ux <= x1 && y0 <= uy && uy <= y1)) continue;
        // FUN_0049f4a0(u, 0).
        if (u.state == 0x12 || u.state == 8 || u.state == 0x14 || (u.flags & 0x400140)) continue;
        if (u.state != 2 && u.state != 3) continue;
        ai_clear_selection(s);
        u.selected = true;
        int tx = ux, ty = uy;
        ai_place_to_move(x0 - 1, y0 - 1, x1 + 1, y1 + 1, 8, tx, ty);
        post_order(s, [=, this] { order_move(s, tx, ty); });
        return;
    }
}

bool World::ai_rebuild_give_up(int s) {
    // FUN_0041f5c0: while the building slot is empty, count the passes;
    // after 10 x max(order latency 0x4e3ae8, 1) give up: 2 in single
    // player (docs/re/49 section 4.3), so 20 passes.
    const auto& pl = players_[size_t(s)];
    if (pl.building_slot.item >= 0) {
        set_ai_u32(s, kWaitCounter, 0);
        return false;
    }
    const uint32_t c = ai_u32(s, kWaitCounter) + 1;
    set_ai_u32(s, kWaitCounter, c);
    if (c <= 10u * kOrderLatency) return false;
    set_ai_u8(s, kRebuilding, 0);
    const auto v1 = pl.building_slot.item; // evaluated at post time
    post_order(s, [=, this] { hold_or_cancel_building(s, v1); }); // order 0x25 on the empty slot
    set_ai_u8(s, kQuarters, 0);
    set_ai_u32(s, kWaitCounter, 0);
    return true;
}

void World::ai_rebuild_next(int s) {
    // FUN_0041f560: concrete while quarters remain, else the building.
    const auto& pl = players_[size_t(s)];
    if (ai_u8(s, kQuarters) != 0) {
        if (!pl.building_list.empty()) {
            const auto v1 = pl.building_list[0]; // evaluated at post time
            post_order(s, [=, this] { start_building(s, v1); }); // order 0x24
        }
    } else {
        const auto v1 = ai_u8(s, kRecord); // evaluated at post time
        post_order(s, [=, this] { start_building(s, v1); });
    }
    set_ai_u8(s, kStartNext, 0);
}

void World::ai_rebuild_place(int s) {
    // FUN_0041ef30: lay the next concrete quarter, or place the building.
    auto& pl = players_[size_t(s)];
    if (pl.building_list.empty()) return;
    const int concrete = pl.building_list[0];
    const int x = ai_u8(s, kCellX), y = ai_u8(s, kCellY), rec = ai_u8(s, kRecord);
    if (ai_u8(s, kQuarters) == 0) {
        if (ai_can_place_building(s, rec, x, y)) {
            const int item = pl.building_slot.item;
            if (item == rec && !pl.building_slot.placing) {
                pl.building_slot.placing = true;
                post_order(s, [=, this] { place_building(s, rec, x, y); }); // order 0x26
            }
            set_ai_u8(s, kRebuilding, 0);
            post_order(s, [=, this] { hold_or_cancel_building(s, item); }); // order 0x25 (after the placement: nothing left)
            set_ai_u8(s, kQuarters, 0);
            set_ai_u8(s, kStartNext, 0);
            set_ai_u8(s, 0x0B, 0);
            return;
        }
        const uint32_t tries = ai_u32(s, kPlaceTries) + 1;
        set_ai_u32(s, kPlaceTries, tries);
        if (tries < 0x33) return;
        int nx, ny;
        if (ai_random_position(s, rec, nx, ny, 8)) {
            set_ai_u8(s, kCellX, uint8_t(nx));
            set_ai_u8(s, kCellY, uint8_t(ny));
        }
        return;
    }
    // Quarters: TL (x, y), TR (x + 2, y), BL (x, y + 2), BR (x + 2, y + 2).
    static constexpr int kQx[4] = {0, 2, 0, 2}, kQy[4] = {0, 0, 2, 2};
    for (int q = 0; q < 4; ++q) {
        const uint8_t bit = uint8_t(1u << q);
        if (!(ai_u8(s, kQuarters) & bit)) continue;
        const int qx = x + kQx[q], qy = y + kQy[q];
        if (!(ai_can_place_concrete(s, concrete, qx, qy) || ai_concrete_already(qx, qy))) continue;
        if (!ai_no_friendly_units(s, concrete, qx, qy)) continue;
        if (!ai_concrete_already(qx, qy) && !pl.building_slot.placing) {
            pl.building_slot.placing = true;
            post_order(s, [=, this] { place_building(s, concrete, qx, qy); }); // order 0x26
            placed_this_pass_ = true;
        }
        set_ai_u8(s, kQuarters, uint8_t(ai_u8(s, kQuarters) & ~bit));
        set_ai_u8(s, kStartNext, 1);
        break;
    }
    if (placed_this_pass_) return; // +0x2652d set: the placement is under way
    // Nothing laid: drop the first pending quarter that cannot take
    // concrete; with none left, the slab in the slot is cancelled.
    for (int q = 0; q < 4; ++q) {
        const uint8_t bit = uint8_t(1u << q);
        if (!(ai_u8(s, kQuarters) & bit)) continue;
        if (ai_can_place_concrete(s, concrete, x + kQx[q], y + kQy[q])) {
            if (q == 3) return;
            continue;
        }
        const uint8_t left = uint8_t(ai_u8(s, kQuarters) & ~bit);
        set_ai_u8(s, kQuarters, left);
        if (left == 0) post_order(s, [=, this] { hold_or_cancel_building(s, concrete); }); // order 0x25
        return;
    }
}

void World::ai_rebuild(int s) {
    // FUN_0041ebc0 "RebuildBuildings": every 16 ticks ((tick + side) & 15),
    // with a construction yard's build list.
    if (((tick_count_ + uint32_t(s)) & 0xf) != 0) return;
    auto& pl = players_[size_t(s)];
    if (pl.building_list.empty()) return;
    const uint32_t money = uint32_t(pl.cash + pl.spice);
    if (ai_u32(s, kCashStashBuildings) < money) set_ai_u8(s, kMaySpend, 0);
    if (ai_u8(s, kRebuilding) == 0) {
        set_ai_u8(s, kStartNext, 0);
        const int g = ai_best_building(s);
        if (g < 0) return;
        const int rec = templates_.building_record(g, pl.house); // FUN_0046c450
        if (rec < 0) return;
        int x = 0, y = 0;
        bool found;
        if (ai_u8(s, kPractice) == 0) found = ai_new_position(s, rec, x, y);
        else found = ai_random_position(s, rec, x, y, 8);
        if (!found || pl.building_slot.item >= 0) return;
        set_ai_u8(s, kCellX, uint8_t(x));
        set_ai_u8(s, kCellY, uint8_t(y));
        set_ai_u8(s, kRecord, uint8_t(rec));
        set_ai_u8(s, kRebuilding, 1);
        set_ai_u32(s, kPlaceTries, 0);
        const auto v1 = pl.building_list[0]; // evaluated at post time
        post_order(s, [=, this] { start_building(s, v1); }); // order 0x24: the first item, a concrete slab
        const auto& r = templates_.building_records[size_t(rec)];
        uint8_t quarters = 1;
        int rx1 = x + 2, ry1 = y + 2;
        if (r.width_px > 0x40) {
            quarters |= 2;
            rx1 += 2;
        }
        if (r.height_px > 0x40) {
            quarters |= 4;
            ry1 += 2;
            if (r.width_px > 0x40) quarters |= 8;
        }
        set_ai_u8(s, kQuarters, quarters);
        set_ai_u32(s, kRectX0, uint32_t(x));
        set_ai_u32(s, kRectY0, uint32_t(y));
        set_ai_u32(s, kRectX1, uint32_t(rx1));
        set_ai_u32(s, kRectY1, uint32_t(ry1));
        return;
    }
    ai_kick_units(s, int(ai_u32(s, kRectX0) & 0xff), int(ai_u32(s, kRectY0) & 0xff), int(ai_u32(s, kRectX1) & 0xff),
                  int(ai_u32(s, kRectY1) & 0xff));
    if (ai_rebuild_give_up(s)) return;
    // The exe's orders run at the next order pass, so the slot read after
    // FUN_0041ef30 is the one before it.
    const bool slot_empty = pl.building_slot.item < 0;
    placed_this_pass_ = false;
    if (!slot_empty && pl.building_slot.progress == kProgressDone && !pl.building_slot.placing) ai_rebuild_place(s);
    if (ai_u8(s, kStartNext) != 0 && slot_empty) ai_rebuild_next(s);
}

void World::ai_upgrade_rebuild_slots() {
    // Saves before version 21 stored the building index in the initial
    // building entries; the exe stores the slot (+0x8c, docs/re/50).
    for (int s = 0; s < 8; ++s) {
        const int n = ai_u8(s, kInitialCount);
        for (int k = 0; k < n; ++k) {
            const size_t e = kInitial + size_t(k) * 0x10;
            const int index = ai_u8(s, e + 2) | (ai_u8(s, e + 3) << 8);
            if (index >= int(buildings_.size())) continue;
            const int slot = building_slot(index);
            set_ai_u8(s, e + 2, uint8_t(slot & 0xff));
            set_ai_u8(s, e + 3, uint8_t((slot >> 8) & 0xff));
        }
    }
}
} // namespace d2k::sim
