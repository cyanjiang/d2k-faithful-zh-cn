// Building placement, building creation and update, and unit spawn.
// RE: docs/re/12-placement-and-spawn.md.
#include <algorithm>
#include <cstring>
#include <iterator>

#include "sim/world.hpp"

namespace d2k::sim {

namespace {
constexpr uint32_t kOwnerMask = 0x7;
constexpr uint32_t kBuilding = 0x10;
constexpr uint32_t kConcrete = 0x800;
constexpr uint32_t kBuildable = cell::kBuildable;
constexpr int kConcreteOwnerShift = 17;
constexpr int kKindYard = 2, kKindRefinery = 4, kKindRepair = 6, kKindLight = 7, kKindHeavy = 8,
              kKindConcrete = 15, kKindWall = 14, kKindTurret = 16;
constexpr uint16_t kConcreteTiles[3] = {0x28b, 0x29f, 0x2b3};

template <typename F>
void for_mask(uint32_t mask, F f) {
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (mask & (1u << (row * 4 + col))) f(col, row);
        }
    }
}
} // namespace

void World::voice(int p, const char* base, uint32_t limit) {
    if (p != local_player) return;
    // FUN_0046f900: house byte 0x8cd4f0[local] 0 -> S_ (Atreides), 1 -> H_,
    // 2 -> O_; any other house plays nothing (no else branch).
    const int house = players_[size_t(p)].house;
    if (house < 0 || house > 2) return;
    const std::string name = std::string(house == 1 ? "H_" : house == 2 ? "O_" : "S_") + base;
    if (limit) { // FUN_0046f820: skip if played less than `limit` ticks ago
        auto it = last_voice_.find(name);
        if (it != last_voice_.end() && tick_count_ - it->second < limit) return;
        last_voice_[name] = tick_count_;
    }
    // The mode and ring every FUN_0046f900 call site pushes (docs/re/59
    // section 3.2): these are mode 0, REINFORCE and DEATHLAUNCHED ring 1,
    // all others mode 1 in ring 0.
    static const char* const kMode0[] = {"PRIMARY",  "RETREAT", "GUARDING", "PORTORDER",    "CANCELLED",  "BUILDING",
                                         "TRAINING", "ONHOLD",  "SELL",     "BUILDUPGRADE", "REPAIRUNIT", "UNITREPAIRED"};
    SoundEvent e{name, -1, -1, true};
    e.mode0 = std::any_of(std::begin(kMode0), std::end(kMode0), [&](const char* m) { return std::strcmp(m, base) == 0; });
    e.prio = std::strcmp(base, "REINFORCE") == 0 || std::strcmp(base, "DEATHLAUNCHED") == 0 ? 1 : 0;
    sounds.push_back(e);
}

void World::unit_response(bool ack) {
    const int house = players_[size_t(local_player)].house;
    if (house > 2) return; // "house > 3" error in the exe
    int best = -1;
    uint32_t best_priority = 0;
    for (const auto& u : units_) {
        if (u.owner != local_player || !u.selected) continue;
        const uint32_t pr = templates_.unit_records[size_t(u.record)].voice_priority;
        if (best_priority <= pr) {
            best = u.record;
            best_priority = pr;
        }
    }
    if (best < 0) return;
    const auto& r = templates_.unit_records[size_t(best)];
    const size_t k = size_t(house * 3) + tick_count_ % 3;
    SoundEvent e;
    e.voice = true;
    e.mode0 = true;
    e.sample = int(ack ? r.ack_voice[k] : r.select_voice[k]);
    sounds.push_back(e);
}

// ---- placement --------------------------------------------------------------

bool World::placement_check(int p, int record, int cx, int cy, std::vector<PlaceCell>& cells) const {
    cells.clear();
    const auto& r = templates_.building_records[size_t(record)];
    const bool concrete = r.kind == kKindConcrete;
    const uint32_t mask = concrete ? r.footprint : r.place_mask;
    if (cy < 2) cy = 1; // row 0 is never used
    const int w = map_.width(), h = map_.height();

    // Pass 1, the near test (FUN_00428c30): own building or own concrete
    // within 2 cells of some mask cell; row 0 is skipped.
    bool near = false, off_map = false;
    for_mask(mask, [&](int col, int row) {
        const int x = cx + col, y = cy + row;
        if (x >= w || y >= h) {
            off_map = true;
            return;
        }
        for (int sy = std::max(y - 2, 0); sy <= std::min(y + 2, h - 1); ++sy) {
            if (sy == 0) continue;
            for (int sx = std::max(x - 2, 0); sx <= std::min(x + 2, w - 1); ++sx) {
                const uint32_t f = map_.at(sx, sy).flags;
                if ((f & kBuilding) && int(f & kOwnerMask) == p) near = true;
                if ((f & kConcrete) && int((f >> kConcreteOwnerShift) & 7) == p) near = true;
            }
        }
    });
    if (off_map) near = false;

    // Pass 2, per cell.
    bool all = !off_map, any = false;
    for_mask(mask, [&](int col, int row) {
        const int x = cx + col, y = cy + row;
        const bool inside = x < w && y < h && y >= 1;
        if (!inside) {
            all = false;
            if (!concrete) cells.push_back({x, y, 1}); // concrete does not draw off-map cells
            return;
        }
        const uint32_t f = map_.at(x, y).flags;
        bool ok = (f & 0x13f8) == 0 && (f & kBuildable) && near;
        if (concrete && (f & kConcrete)) ok = false;
        any |= ok;
        all &= ok;
        cells.push_back({x, y, uint8_t(!ok ? 1 : (!concrete && (f & kConcrete)) ? 2 : 0)});
    });
    return concrete ? any : all;
}

void World::lay_concrete(int p, int record, int cx, int cy, uint16_t mask) {
    for_mask(mask, [&](int col, int row) {
        const int x = cx + col, y = cy + row;
        if (x >= map_.width() || y >= map_.height()) return;
        // The local player reveals around every mask cell inside the map,
        // before the slab checks, so row 0 is revealed too (FUN_00456210:
        // FUN_0044e330 comes before the `row != 0` test; doc 16 section 2).
        if (p == local_player) reveal_building(record, x, y);
        if (y == 0) return;
        auto& c = map_.at(x, y);
        if ((c.flags & 0x13f8) || !(c.flags & kBuildable) || (c.flags & kConcrete)) return;
        c.marks = 0; // FUN_00456210: the crater counter (+9) is cleared (docs/re/26 section 3)
        c.tile = kConcreteTiles[rng_.next() % 3];
        c.flags = (c.flags & ~(7u << kConcreteOwnerShift)) | kConcrete | (uint32_t(p) << kConcreteOwnerShift);
    });
}

void World::place_building(int p, int record, int cx, int cy) {
    auto& pl = players_[size_t(p)];
    pl.building_slot.placing = false;
    const auto& r = templates_.building_records[size_t(record)];
    if (r.kind == kKindConcrete) {
        // No server-side check: the near rule is the client grid's.
        pl.building_slot = ProductionSlot{};
        lay_concrete(p, record, cx, cy, r.footprint);
        if (p == local_player) sounds.push_back({"S_BUILDCONC", cx, cy, false});
        return;
    }
    // FUN_00457740: inside the map and no building / unit on the 0x50 cells.
    bool ok = true;
    for_mask(r.place_mask, [&](int col, int row) {
        const int x = cx + col, y = cy + row;
        if (x >= map_.width() || y >= map_.height() || (map_.at(x, y).flags & 0x3f8)) ok = false;
    });
    if (!ok) return; // the slot stays ready
    pl.building_slot = ProductionSlot{};
    create_building(p, record, cx, cy, false);
    if (p == local_player) sounds.push_back({"S_BUILDUP", cx, cy, false});
}

int World::create_building(int p, int record, int cx, int cy, bool preplaced, bool captured, bool no_crane) {
    const int slot = pool_pop(p);
    if (slot < 0) {
        reached_unit_limit();
        return -1;
    }
    auto& pl = players_[size_t(p)];
    const auto& r = templates_.building_records[size_t(record)];
    Building b;
    b.index = int(buildings_.size());
    b.slot = uint16_t(slot);
    b.owner = p;
    b.house = pl.house;
    b.type = r.name_index;
    b.record = record;
    b.cell_x = cx;
    b.cell_y = cy;
    b.x = int32_t(cx * 32) << 16;
    b.y = int32_t(cy * 32 + r.height_px) << 16;
    b.flags = r.flags;

    // Concrete under it: pre-placed buildings and construction yards (not
    // for a capture, flag 6).
    if (!(r.flags & 0x400000) && (preplaced || (r.kind == kKindYard && !captured))) {
        lay_concrete(p, record, cx, cy, r.place_mask);
    }

    // Hit points by concrete cover (FUN_0044dac0). The map loader creates
    // its buildings here too (flag 5), after the concrete above, so a map
    // building partly off concrete starts lower and decays (docs/re/12
    // section 6 step 5). The crater bytes are cleared for every record.
    b.hit_points = r.hit_points;
    int on = 0, total = 0;
    for_mask(r.footprint, [&](int col, int row) {
        const int x = cx + col, y = cy + row;
        // FUN_0044dac0 has no bounds test (a cell past the right edge reads
        // the next row); no caller can reach it in 1.06: the player's order
        // 0x26 and the MCV deploy test their whole 0x50 mask inside the map,
        // a capture reuses a placed building's cells, and no 1.06 map
        // building overhangs the map (golden test in test_placement.cpp).
        if (x >= map_.width() || y >= map_.height()) return;
        ++total;
        if (map_.at(x, y).flags & kConcrete) ++on;
    });
    for_mask(r.place_mask, [&](int col, int row) {
        const int x = cx + col, y = cy + row;
        if (x < map_.width() && y < map_.height()) map_.at(x, y).marks = 0; // FUN_0044dac0
    });
    if (!(r.flags & 0x400000)) {
        if (total > 0) b.hit_points = ((on * r.hit_points) >> 1) / total + r.hit_points / 2;
        if (on != total) b.flags |= 0x8000;
    }
    // Build-up animation: the frame count table 0x714a98 is indexed by the
    // record here (FUN_004563b0, obj +0x34), not by rec +0x8a; records
    // without an animation (walls) have 0 and appear at once.
    if (!preplaced && templates_.art.anim1_frames[size_t(b.record % 100)] != 0) {
        b.buildup = 0;
        b.flags |= 2;
    }
    buildings_.push_back(b); // FUN_0046bd60: in the list before the cells (the pool count of a conversion)

    // Primary building of its kind.
    if (r.kind < 20 && pl.primary[r.kind] < 0 &&
        (r.kind == kKindYard || (r.kind >= 7 && r.kind <= 13) || (r.kind >= 17 && r.kind <= 19))) {
        pl.primary[r.kind] = b.index;
        buildings_.back().flags |= 0x80;
    }
    // One crane cycle on the primary construction yard.
    if (!preplaced && pl.primary[kKindYard] >= 0 && !no_crane) buildings_[size_t(pl.primary[kKindYard])].flags |= 0x20;
    rebuild_lists(p);
    if (p == local_player) reveal_building(record, cx, cy); // step 10

    // Map cells (FUN_0044dbd0): per footprint cell, the units of another
    // side are converted first (step 12), then the cell is written.
    for_mask(r.place_mask, [&](int col, int row) {
        const int x = cx + col, y = cy + row;
        if (x < map_.width() && y < map_.height()) map_.at(x, y).flags &= ~kBuildable;
    });
    for_mask(r.footprint, [&](int col, int row) {
        const int x = cx + col, y = cy + row;
        if (x >= map_.width() || y >= map_.height()) return;
        convert_footprint_units(p, x, y);
        auto& f = map_.at(x, y).flags;
        f = (f & ~0x8007u) | uint32_t(p) | kBuilding;
        const bool exit = (r.kind == kKindRepair && !(col == 1 && row == 1)) ||
                          (r.kind == kKindHeavy && col == 0 && row == 2) ||
                          (r.kind == kKindLight && col == 2 && row == 1);
        if (exit) f |= 0x10000000;
        if ((r.kind == kKindWall || r.kind == kKindTurret) && col == 0 && row == 0) f |= 0x400;
        map_.at(x, y).radar = uint8_t(p); // 0x44de4f: the owner colour
    });
    if (r.kind == kKindRefinery && !captured) {
        // A free harvester by carryall onto the dock cell (doc 13 section 5),
        // unless flag 6 (a capture). The map loader (0x469c05) passes flag 5
        // = 1 but flag 6 = 0, so map-placed refineries get one at mission start.
        const auto* ids = templates_.block(0x6d60d8);
        const int harvester = ids && ids->bytes.size() > 16 ? ids->bytes[16] : -1; // 0x6d60e8 (a record)
        // FUN_004563b0: entry +0x35 = 1 (carryall), +0x36 = 4 (no REINFORCE
        // voice; also the AI group mode of the delivery), records {0x6d60e8, 0xff}.
        if (harvester >= 0) queue_delivery(p, cx + 2, cy + 1, {harvester}, 1, true, 4);
    }
    if (r.kind == kKindWall || r.kind == kKindTurret) {
        // FUN_004563b0: the wall frame from the neighbours, then theirs.
        buildings_[size_t(b.index)].wall_frame = uint8_t(wall_frame_at(cx, cy));
        update_walls_around(cx, cy);
    }
    if (r.flags & 0x200000) write_skirt(record, cx, cy); // step 15
    return b.index;
}

void World::convert_footprint_units(int p, int x, int y) {
    // FUN_0044dbd0 (0x44dcb0): FUN_004a5270 names the cell's side and
    // gates on a unit of it being there; FUN_004a54a0 then walks all that
    // side's units whose current or "to" cell is this one (no worms, not
    // 0x400140; aircraft too). Another side's: a loss, then ConvertUnit to
    // the placer (infantry are removed). Own units stay (docs/re/12 step 12).
    bool infantry = false;
    if (unit_in_cell(x, y, infantry) < 0) return;
    const int side = int(map_.at(x, y).flags & 7);
    if (side == p) return;
    std::vector<int> ids;
    for (const auto& u : units_) {
        if (u.owner != side || u.remove) continue;
        if (templates_.unit_records[size_t(u.record)].kind == 5 || (u.flags & 0x400140)) continue;
        if ((u.cur_x == x && u.cur_y == y) || (u.to_x == x && u.to_y == y)) ids.push_back(u.index);
    }
    for (int id : ids) {
        const int k = find_unit(id);
        if (k < 0 || (units_[size_t(k)].flags & 0x400140)) continue;
        count_unit_lost(side, units_[size_t(k)].record);
        convert_unit(side, p, id, false);
    }
}

void World::write_skirt(int record, int cx, int cy) {
    // FUN_0044f1b0: the two bottom rows of a 2- or 3-wide record get the
    // skirt tiles, the concrete set on concrete cells (docs/re/12 step 15).
    const auto& r = templates_.building_records[size_t(record)];
    const int w = r.width_px >> 5, h = r.height_px >> 5;
    if (w != 2 && w != 3) return; // "Building %s can't have a skirt"
    const int base = w == 2 ? 0x269 : 0x263, base_concrete = w == 2 ? 0x289 : 0x283;
    for (int k = 0; k < 2; ++k) {
        const int y = cy + h - 2 + k;
        for (int c = 0; c < w; ++c) {
            const int x = cx + c;
            if (x < 0 || y < 0 || x >= map_.width() || y >= map_.height()) continue;
            auto& cell = map_.at(x, y);
            cell.tile = uint16_t(((cell.flags & kConcrete) ? base_concrete : base) + c + 0x14 * k);
        }
    }
}

void World::clear_skirt(int record, int cx, int cy) {
    // FUN_0044f2f0: the same cells, in the same order; concrete gets 0x28b,
    // any other cell a rubble tile 0x4e0af0[rand() & 7] (line 0x971).
    static const uint16_t kBroken[8] = {552, 553, 554, 555, 556, 572, 573, 574}; // 0x4e0af0
    const auto& r = templates_.building_records[size_t(record)];
    const int w = r.width_px >> 5, h = r.height_px >> 5;
    if (w != 2 && w != 3) return;
    for (int k = 0; k < 2; ++k) {
        const int y = cy + h - 2 + k;
        for (int c = 0; c < w; ++c) {
            const int x = cx + c;
            if (x < 0 || y < 0 || x >= map_.width() || y >= map_.height()) continue;
            auto& cell = map_.at(x, y);
            if (cell.flags & kConcrete) {
                cell.tile = 0x28b;
            } else {
                cell.tile = kBroken[rng_.next() & 7];
            }
        }
    }
}

void World::count_unit_lost(int side, int record) {
    auto& pl = players_[size_t(side & 7)];
    ++pl.units_lost;
    if (record >= 0 && size_t(record) < pl.units_lost_by_record.size()) ++pl.units_lost_by_record[size_t(record)];
}

void World::count_unit_killed(int side, int record, int index_side) {
    auto& pl = players_[size_t(side & 7)];
    ++pl.units_killed;
    const size_t i = size_t(record) * 8 + size_t(index_side & 7);
    if (record >= 0 && i < pl.units_killed_by_record.size()) ++pl.units_killed_by_record[i];
}

void World::count_building_killed(int side, int record, int victim) {
    auto& pl = players_[size_t(side & 7)];
    ++pl.buildings_killed;
    const size_t i = size_t(record) * 8 + size_t(victim & 7);
    if (record >= 0 && i < pl.buildings_killed_by_record.size()) ++pl.buildings_killed_by_record[i];
}

int World::wall_frame_at(int x, int y) const {
    // FUN_0044e580: N 1, S 2, W 4, E 8 for neighbours with cell flag 0x400
    // (walls and turrets, FUN_0044e540), through a fixed table.
    auto wall = [&](int cx, int cy) {
        return cx >= 0 && cy >= 0 && cx < map_.width() && cy < map_.height() && (map_.at(cx, cy).flags & 0x400);
    };
    static constexpr int kFrame[16] = {0, 3, 4, 5, 2, 12, 13, 9, 1, 11, 15, 8, 6, 7, 10, 14};
    const int mask = (wall(x, y - 1) ? 1 : 0) | (wall(x, y + 1) ? 2 : 0) | (wall(x - 1, y) ? 4 : 0) |
                     (wall(x + 1, y) ? 8 : 0);
    return kFrame[mask];
}

void World::update_walls_around(int x, int y) {
    // FUN_0044e6d0: the 8 neighbours that are wall / turret buildings get
    // their frame again.
    for (int cy = y - 1; cy < y + 2; ++cy) {
        for (int cx = x - 1; cx < x + 2; ++cx) {
            if ((cx == x && cy == y) || cx < 0 || cy < 0 || cx >= map_.width() || cy >= map_.height()) continue;
            if (!(map_.at(cx, cy).flags & 0x400)) continue;
            const int bi = building_at_cell(cx, cy);
            if (bi < 0) continue;
            const int kind = templates_.building_records[size_t(buildings_[size_t(bi)].record)].kind;
            if (kind == kKindWall || kind == kKindTurret) buildings_[size_t(bi)].wall_frame = uint8_t(wall_frame_at(cx, cy));
        }
    }
}

void World::tick_buildings(int p) {
    // FUN_0049b7f0 for the buildings of side p, in list order (World::tick).
    for (size_t bi = 0; bi < buildings_.size(); ++bi) {
        Building& b = buildings_[bi];
        if (b.owner != p || b.removed) continue;
        const auto& r = templates_.building_records[size_t(b.record)];
        if (b.dying) { // +0x3b counts down, then the building is removed
            // Destroyed buildings release survivors at 0x19 (doc 21 section 3.4).
            if (b.death_timer == 0x19 && !(b.flags & 0x10000) && !(b.flags & 0x80000)) release_survivors(b);
            if (b.death_timer > 0) { // removed on the update after it reaches 0
                --b.death_timer;
                continue;
            }
            // The exe's area hit here (name +0x88 == 0x6d60e0, bullet 0x6d60ee)
            // never fires in 1.06: 0x6d60e0 (Templates.bin ids byte 8) is 0xff.
            remove_building(b.index);
            // FUN_0049b7f0 returned 0: FUN_00458e90 frees the slot at once
            // (FUN_0046bdb0), before the side's next building updates; it is
            // the next slot this side hands out (docs/re/50 section 5).
            pool_push(b.owner, b.slot);
            continue;
        }
        // FUN_0049b7f0: +0x41 counts up every 8 ticks (staggered by the
        // slot, +0x8c); a hit (flag 0x1000) resets it.
        if (((tick_count_ ^ uint32_t(b.slot)) & 7) == 0 && b.hit_age < 255) ++b.hit_age;
        if (b.flags & 0x1000) {
            b.hit_age = 0;
            b.flags &= ~0x1000u;
        }
        // Not fully on concrete: -1 hp every 16 ticks down to half (before the
        // selling return and the smoke block, docs/re/43 section 3.3).
        if ((b.flags & 0x8000) && b.hit_points > r.hit_points / 2 && (tick_count_ & 0xf) == 0) --b.hit_points;
        if (tick_selling(b)) continue;
        // Damage smoke and flames (0x49baac, docs/re/43 section 3.3): two
        // rand() per BUILEXP point every tick while the timer runs: a puff
        // when ((r & 1) + tick) % period == 0 and the point is on; then the
        // point goes out at 1 in 128 below timer 50, or above half hp.
        if (b.smoke != 0 && (r.flags & 0x200) && !(b.flags & 6) && size_t(b.record) < smoke_points_.size()) {
            const auto& sp = smoke_points_[size_t(b.record)];
            const int e = r.base_element;
            const int h = e >= 0 && size_t(e) < element_frame0_h.size() ? element_frame0_h[size_t(e)] : 0;
            for (int i = 0; i < sp.n; ++i) {
                const uint32_t r1 = rng_.next();
                if (sp.period[i] != 0 && ((r1 & 1) + tick_count_) % sp.period[i] == 0 && (b.smoke_points & (1u << i))) {
                    spawn_effect_at(sp.type[i], (b.x >> 16) + sp.x[i], (b.y >> 16) + sp.y[i] - h, b.owner, 0, 0, 0);
                }
                const uint32_t r2 = rng_.next();
                if (((r2 & 0x7f) == 0 && b.smoke < 50) || b.hit_points > int(uint32_t(r.hit_points) >> 1)) {
                    b.smoke_points &= ~(1u << i);
                }
            }
            if ((tick_count_ & 3) == 0) --b.smoke;
        }
        tick_building_combat(b);
        if (b.flags & 2) { // build-up: 3 ticks per frame
            ++b.buildup;
            if (b.buildup / 3 >= templates_.art.anim1_frames[r.anim1 % 100]) b.flags &= ~6u;
        } else if ((b.flags & 0x30) && (b.flags & 0x40) && b.hit_points >= r.hit_points / 2 && !(b.flags & 4)) {
            // Active animation; the rate is +0x85 of the record whose index
            // is the animation index (as the exe reads it).
            const size_t ai = r.anim2 % 100;
            const int rate = ai < templates_.building_records.size()
                                 ? std::max<int>(templates_.building_records[ai].anim_rate, 1)
                                 : 1;
            ++b.anim;
            if (b.anim / rate >= templates_.art.anim2_frames[ai]) {
                b.anim = 0;
                b.flags &= ~0x20u;
            }
        }
    }
}

// ---- unit spawn -------------------------------------------------------------

int World::find_factory(int unit_record, int p, bool check, int& sx, int& sy, int& mx, int& my) const {
    const auto& ur = templates_.unit_records[size_t(unit_record)];
    const auto& pl = players_[size_t(p)];
    const auto* ids = templates_.block(0x6d60d8);
    if (!ids || ids->bytes.size() < 13) return -1;
    const auto& id = ids->bytes;
    const int g = ur.prereq1;
    std::vector<int> kinds;
    if (g == id[3]) kinds = {7};
    else if (g == id[4]) kinds = {8};
    else if (g == id[5] || g == id[6]) kinds = {9, 10};
    else if (g == id[1]) kinds = {11};
    else if (g == id[2]) kinds = {12};
    else if (g == id[10]) kinds = {19, 17, 18};
    else if (g == id[11]) kinds = {17, 19, 18};
    else if (g == id[12]) kinds = {18, 17, 19};
    else return -1;
    int primary = -1;
    for (int k : kinds) {
        if (pl.primary[size_t(k)] >= 0) {
            primary = pl.primary[size_t(k)];
            break;
        }
    }
    if (primary < 0) return -1;

    auto cells = [&](const Building& b) {
        const auto& br = templates_.building_records[size_t(b.record)];
        const int bx = (b.x >> 16) / 32, by = ((b.y >> 16) - br.height_px) / 32;
        sx = bx + br.spawn_x;
        sy = by + br.spawn_y;
        mx = bx + br.exit_x;
        my = by + br.exit_y;
    };
    auto usable = [&](const Building& b) {
        const auto& br = templates_.building_records[size_t(b.record)];
        if (!(ur.prereq1_mask & br.house_mask)) return false;
        if (!check) return true;
        cells(b);
        return sx >= 0 && sy >= 0 && sx < map_.width() && sy < map_.height() &&
               (map_.at(sx, sy).flags & 0x3e8) == 0;
    };
    if (usable(buildings_[size_t(primary)])) {
        cells(buildings_[size_t(primary)]);
        return primary;
    }
    for (const auto& b : buildings_) { // creation order
        if (b.owner != p || b.removed || templates_.building_records[size_t(b.record)].name_index != g) continue;
        if (usable(b)) {
            cells(b);
            return b.index;
        }
    }
    return -1;
}

std::array<std::vector<uint16_t>, 8> World::fresh_pools() {
    // The side reset (0x46ba3d..0x46ba61) chains slots 0..999 and leaves the
    // head at slot 999, so a fresh side hands out 999, 998, ... (docs/re/50).
    std::array<std::vector<uint16_t>, 8> p;
    for (auto& v : p) {
        v.resize(kPoolSize);
        for (int i = 0; i < kPoolSize; ++i) v[size_t(i)] = uint16_t(i);
    }
    return p;
}

int World::pool_pop(int side) {
    // FUN_0046bd40: the head of the free list, or none.
    auto& v = pool_free_[size_t(side & 7)];
    if (v.empty()) return -1;
    const int s = v.back();
    v.pop_back();
    return s;
}

void World::pool_push(int side, int slot) {
    // FUN_0046bdb0: the freed slot becomes the head (LIFO).
    pool_free_[size_t(side & 7)].push_back(uint16_t(slot));
}

void World::upgrade_pools() {
    // Saves before version 21 kept no slots: hand them out again per side
    // (buildings, units, bullets, explosions in list order), then turn the
    // stored building indices / unit ids of the slot references into slots.
    pool_free_ = fresh_pools();
    for (auto& b : buildings_) {
        if (!b.removed) b.slot = uint16_t(std::max(pool_pop(b.owner), 0));
    }
    for (auto& u : units_) u.slot = uint16_t(std::max(pool_pop(u.owner), 0));
    for (auto& b : bullets) b.slot = uint16_t(std::max(pool_pop(b.owner), 0));
    for (auto& e : effects) e.slot = uint16_t(std::max(pool_pop(e.owner), 0));
    for (auto& u : units_) {
        u.refinery = u.refinery >= 0 && size_t(u.refinery) < buildings_.size() ? building_slot(u.refinery) : -1;
        u.repair_pad = u.repair_pad >= 0 && size_t(u.repair_pad) < buildings_.size() ? building_slot(u.repair_pad) : -1;
    }
    for (auto& b : buildings_) {
        if (b.attacker >= 0) b.attacker = firer_slot(b.attacker);
    }
    ai_upgrade_rebuild_slots();
}

const Unit* World::unit_in_slot(int side, int slot) const {
    // A unit whose update ended its countdown (`remove`) has given its slot back.
    for (const auto& u : units_) {
        if (u.owner == side && u.slot == slot && !u.remove) return &u;
    }
    return nullptr;
}

const Building* World::building_in_slot(int side, int slot) const {
    for (const auto& b : buildings_) {
        if (b.owner == side && b.slot == slot && !b.removed) return &b;
    }
    return nullptr;
}

int World::firer_slot(int firer) const {
    if (firer >= 0) {
        const int i = find_unit(firer);
        return i >= 0 ? units_[size_t(i)].slot : -1;
    }
    if (firer <= -1000 && size_t(-1000 - firer) < buildings_.size()) return buildings_[size_t(-1000 - firer)].slot;
    return -1;
}

void World::reached_unit_limit() {
    // CreateUnit 0x455fab / CreateBuilding 0x4569c9: AddMessage, no sound.
    Message m;
    m.key = "ReachedUnitLimit";
    messages.insert(messages.begin(), m);
    if (messages.size() > 5) messages.resize(5);
}

int World::create_unit(int p, int record, int x1, int y1, int x2, int y2) {
    const auto& r = templates_.unit_records[size_t(record)];
    const bool ground = r.kind != 2 && r.kind != 8 && r.kind != 9 && r.kind != 10 && r.kind != 5;
    if (x1 < 0 || y1 < 0 || x1 >= map_.width() || y1 >= map_.height()) return -1;
    const uint32_t f = map_.at(x1, y1).flags;
    if (ground && (f & kBuilding) && int(f & kOwnerMask) != p) return -1;
    if (!player_active(p)) return -1; // +0x24250 == 0 (0x45591b): a removed side gets no units
    const int slot = pool_pop(p);
    if (slot < 0) {
        reached_unit_limit();
        return -1;
    }
    Unit u;
    u.index = next_unit_id_++;
    u.slot = uint16_t(slot);
    u.owner = p;
    u.house = players_[size_t(p)].house;
    u.type = r.name_index;
    u.record = record;
    u.speed = r.speed;
    u.hit_points = r.hit_points;
    u.flags = r.initial_flags;
    u.state = state::kIdle;
    u.next_state = 1;
    u.turn_counter = r.turn_delay;
    u.turret_counter = r.turret_turn_delay;
    u.x = int32_t(x1 * 32 + 16) << 16;
    u.y = int32_t(y1 * 32 + 16) << 16;
    u.cur_x = u.to_x = uint8_t(x1);
    u.cur_y = u.to_y = uint8_t(y1);
    u.dest_x = uint8_t(x2);
    u.dest_y = uint8_t(y2);
    u.home_x = uint8_t(x1); // +0x50 / +0x51
    u.home_y = uint8_t(y1);
    if (r.kind == 1) u.refinery = building_slot(least_used_refinery(p)); // FUN_004a5e80 returns the slot
    // Stealth ability (0x455f43): kind 12 (Fremen) and the "Stealth Raider" art.
    if (r.kind == 12 || templates_.unit_name[r.name_index] == "STEALTH_RAIDER") u.flags |= 0x10;
    if (r.kind == 10) { // the Death Hand missile (docs/re/41 section 4)
        u.flags |= 0x20040;
        u.altitude = 0x3c0000;
        u.facing = 0;
        u.bombs = 8; // +0x29
        u.state = state::kMove;
        u.dest_x = uint8_t(map_.width() / 2);
        u.dest_y = uint8_t(map_.height() / 2);
        units_.push_back(u);
        return int(units_.size()) - 1;
    }
    if (r.kind == 9 || r.kind == 15) { // aircraft from the start, 60 px up (doc 12 section 8.3)
        u.flags |= 0x40;
        u.altitude = 0x3c0000;
    }
    if (r.kind == 2) { // a carryall flies in from the map edge (docs/re/33 section 3.1)
        launch_carryall(u, x1, y1, x2, y2);
        units_.push_back(u);
        return int(units_.size()) - 1;
    }
    // FUN_0045e020: the direction to the move cell, a multiple of 4.
    u.facing = (x1 == x2 && y1 == y2) ? 0 : uint8_t(facing8(x1, y1, x2, y2));
    if (ground) occupy(u, x1, y1, true);
    if (x1 != x2 || y1 != y2) {
        set_state(u, state::kMoveAttack);
        u.dest_x = uint8_t(x2);
        u.dest_y = uint8_t(y2);
    } else if (r.kind == 1) {
        u.state = 0xc;
    } else if (p != local_player && !network && r.kind != 2 && r.kind != 8 && r.kind != 0xa) {
        // FUN_00455870 (0x455c90..): spawn = move cell, not gSideId, not a
        // network game (docs/design/multiplayer-local-audit.md B2) and not
        // kind 2 / 8 / 0xa: SetState 3 (guard); refused for unarmed units.
        set_state(u, 3);
    }
    // TODO: harvester refinery link, carryall / aircraft / Death Hand
    // starts, Fremen and stealth cloak flag.
    units_.push_back(u);
    return int(units_.size()) - 1;
}

void World::spawn_ready(int p, ProductionSlot& s) {
    int sx, sy, mx, my;
    if (find_factory(s.item, p, true, sx, sy, mx, my) < 0) return;
    // FUN_004467e0 (0x446f48 / 0x44702c): the result of FUN_00455870 is not
    // tested; the slot is cleared and *_UNITREADY plays even when the creation failed
    // (pool full: ReachedUnitLimit), so that unit is lost without a refund.
    create_unit(p, s.item, sx, sy, mx, my);
    s = ProductionSlot{};
    voice(p, "UNITREADY");
}

} // namespace d2k::sim
