// Building repair and selling, survivors, guard and retreat orders.
// RE: docs/re/21-sell-repair-guard-retreat.md.
#include <algorithm>

#include "sim/world.hpp"

namespace d2k::sim {

namespace {
constexpr uint32_t kRepairing = 1, kBuildingUp = 2, kSelling = 4, kRemove = 8;
constexpr uint32_t kNoRubble = 0x10000, kSold = 0x20000, kSurvivorsOut = 0x80000, kNoSell = 0x1000000;
constexpr uint32_t kRealBuilding = 0x200; // record flag (rec+0x58)
} // namespace

bool World::can_repair_building(int p, int index) const {
    // The mode-2 click and cursor (0x445202..0x445254, 0x443b50..0x443b96):
    // the building in the cell (FUN_004a5640, a dying one too) is the local
    // side's, its hp is below the record's and the record has flag 0x200.
    if (index < 0 || size_t(index) >= buildings_.size()) return false;
    const Building& b = buildings_[size_t(index)];
    const auto& r = templates_.building_records[size_t(b.record)];
    return !b.removed && b.owner == p && (r.flags & kRealBuilding) && b.hit_points < r.hit_points;
}

bool World::can_sell_building(int p, int index) const {
    // The mode-3 click (0x4453b3..0x4453ed): the local side's building with
    // flags & 6 = 0 (not building up, not selling).
    if (index < 0 || size_t(index) >= buildings_.size()) return false;
    const Building& b = buildings_[size_t(index)];
    return !b.removed && b.owner == p && !(b.flags & (kBuildingUp | kSelling));
}

namespace {
// FUN_004a4740(side, index): the side is active and the slot holds a
// building (type 2; a dying one too, until the slot is freed).
bool holds_building_of(const std::vector<Building>& bs, int p, int index) {
    return index >= 0 && size_t(index) < bs.size() && bs[size_t(index)].owner == p && !bs[size_t(index)].removed;
}
} // namespace

void World::order_repair_building(int p, int index) {
    // Order 0x22 (ProcessOrder case 0xf), FUN_00456fb0: toggles flag 1 of
    // any building of the side; the click did the tests.
    if (!player_active(p) || !holds_building_of(buildings_, p, index)) return;
    buildings_[size_t(index)].flags ^= kRepairing;
}

void World::order_sell(int p, int index) {
    // Order 0x23 (ProcessOrder case 0x10), FUN_00456ee0: any building of
    // the side that is not being entered (0x1000000, set only by an
    // engineer / saboteur at 0x496f5a) and not in state 0x11. The handler
    // does not test flags & 6 (the click does), and the AI calls it directly.
    // The poster FUN_0045a1a0 refuses once the local player has lost; that
    // test is the UI's (game_screen.cpp), as it reads the local side
    // (docs/design/multiplayer-local-audit.md B3).
    if (!player_active(p) || !holds_building_of(buildings_, p, index)) return;
    Building& b = buildings_[size_t(index)];
    const auto& r = templates_.building_records[size_t(b.record)];
    if ((b.flags & kNoSell) || b.dying) return;
    // 0x714a98 indexed by the record (obj +0x34), as at creation; walls: 0,
    // removed at once. u8 arithmetic.
    const uint8_t n = uint8_t(templates_.art.anim1_frames[size_t(b.record % 100)] * 3);
    b.buildup = n;
    if (n == 0) {
        b.flags |= kRemove;
    } else {
        b.flags |= kSelling;
        b.buildup = uint8_t(n - 1);
        b.selected = false; // +0x35 = 0 (with +0x40 / +0x42 / +0x44, which we do not keep)
    }
    // Kind 0xe = the walls (records 17..19): no SELL voice.
    if (p == local_player && r.kind != 0xe) voice(p, "SELL");
}

void World::do_repairs() {
    // FUN_0044a9f0, once per tick for player tick & 7: +R hp per step at
    // ((cost * R) >> 1) / max (min 1), while cash + spice covers it.
    const int p = int(tick_count_ & 7);
    const int32_t R = vars.repair_points;
    for (auto& b : buildings_) {
        if (b.owner != p || b.removed || !(b.flags & kRepairing)) continue;
        const auto& r = templates_.building_records[size_t(b.record)];
        const int32_t max = r.hit_points;
        if (b.hit_points >= max) {
            b.flags &= ~kRepairing;
            continue;
        }
        uint32_t cost = (uint32_t(building_cost(p, b.record)) * uint32_t(R)) >> 1;
        cost = cost / uint32_t(std::max(max, 1));
        if (cost == 0) cost = 1;
        const auto& pl = players_[size_t(p)];
        if (uint32_t(pl.cash + pl.spice) < cost) {
            b.flags &= ~kRepairing; // stops silently
            continue;
        }
        spend(p, int32_t(cost));
        b.hit_points += R;
        if (b.hit_points >= max) {
            b.hit_points = max;
            b.flags &= ~kRepairing;
        }
    }
}

bool World::tick_selling(Building& b) {
    // FUN_0049b7f0, flags & 6 = 4: the build-up animation backwards, one
    // step per tick; survivors at the frame the base shows from; then
    // state 0x11 + flag 8, "sold" (0x20000), removal on the next tick.
    // Returns true when the update stops here this tick.
    const auto& r = templates_.building_records[size_t(b.record)];
    if ((b.flags & (kBuildingUp | kSelling)) == kSelling) {
        // 0x49b86f: the construction yard (kind 2) stops at 60 (frame 20 of
        // its MCV-unpacking animation), every other building at 1.
        if (b.buildup == 1 || (r.kind == 2 && b.buildup == 60)) {
            b.dying = true;
            b.death_timer = 0;
            b.flags |= kRemove;
        } else {
            --b.buildup;
        }
        if (!(b.flags & kSurvivorsOut) && b.buildup / 3 == r.buildup_base) release_survivors(b);
    }
    if ((b.flags & kRemove) && !(b.flags & kSold)) {
        b.dying = true; // FUN_0044b1d0: state 0x11
        b.death_timer = 0;
        b.flags |= kSold;
        return true;
    }
    return false;
}

void World::release_survivors(Building& b) {
    // FUN_004977a0: up to 4 of the first infantry record walk out of
    // random footprint cells; 20 tries shared by all of them.
    const auto& r = templates_.building_records[size_t(b.record)];
    if (!(r.flags & kRealBuilding) || (b.flags & 0x2000)) return;
    b.flags |= kSurvivorsOut;
    const int cx = (b.x >> 16) >> 5, cy = ((b.y >> 16) - r.height_px) >> 5;
    int inf = -1;
    for (size_t i = 0; i < templates_.unit_records.size(); ++i) {
        if (templates_.unit_records[i].infantry) {
            inf = int(i);
            break;
        }
    }
    if (inf < 0) return;
    const uint32_t ucost = uint32_t(std::max(unit_cost(b.owner, inf), 1));
    uint32_t count = (uint32_t(building_cost(b.owner, b.record)) / 3) / ucost;
    if (count > 3) count = 4;
    int tries = 0;
    for (uint32_t k = 0; k < count; ++k) {
        int x = 0, y = 0, fx = 0, fy = 0;
        for (;;) {
            const uint32_t rnd = rng_.next();
            x = cx + int(rnd & 3);
            y = cy + int((rnd & 0xf) >> 2);
            fx = x;
            fy = y;
            const bool found = free_cell_3x3(fx, fy);
            ++tries;
            const bool in_fp = (r.footprint >> (rnd & 0xf)) & 1;
            if (in_fp && found && x < map_.width() && y < map_.height() && !(map_.at(x, y).flags & 0x3e8)) break;
            if (tries >= 20) break;
        }
        if (tries < 20) create_unit(b.owner, inf, x, y, fx, fy);
    }
}

bool World::free_cell_3x3(int& x, int& y) {
    // FUN_004979a0: the cells around (x, y) with no unit / building and the
    // drive bit; each hit is kept, then rand() & 1 decides whether to stop.
    const int ox = x, oy = y;
    const int y0 = std::max(y - 1, 0), y1 = std::min(y + 2, map_.height() - 1);
    const int x0 = std::max(x - 1, 0), x1 = std::min(x + 2, map_.width() - 1);
    for (int cy = y0; cy < y1; ++cy) {
        for (int cx = x0; cx < x1; ++cx) {
            const uint32_t f = map_.at(cx, cy).flags;
            if ((f & 0x3f8) || !(f & cell::kVehicles)) continue;
            x = cx;
            y = cy;
            if (rng_.next() & 1) return x != ox || y != oy;
        }
    }
    return x != ox || y != oy;
}

void World::reassign_primary(const Building& gone) {
    // FUN_0046d9a0: a primary slot that pointed at this building takes
    // FUN_0046e740's pick: the first building in the side's list (creation
    // order) with the same name (+0x88) and another index. It does not test
    // the state, so a dying building can become primary.
    auto& pl = players_[size_t(gone.owner)];
    const int name = templates_.building_records[size_t(gone.record)].name_index;
    for (size_t k = 0; k < pl.primary.size(); ++k) {
        if (pl.primary[k] != gone.index) continue;
        pl.primary[k] = -1;
        for (auto& o : buildings_) {
            if (o.index == gone.index || o.owner != gone.owner || o.removed) continue;
            if (templates_.building_records[size_t(o.record)].name_index != name) continue;
            pl.primary[k] = o.index;
            o.flags |= 0x80;
            break;
        }
        if (k == 13 && pl.primary[k] < 0) starport_lost(gone.owner); // the last Starport (docs/re/34 section 6.2)
    }
}

void World::sold_refund(const Building& b) {
    // FUN_00458e90: half the price (minus a harvester for a refinery) into
    // pending cash when a sold building is removed.
    if (!(b.flags & kSold)) return;
    const auto& r = templates_.building_records[size_t(b.record)];
    int32_t c = building_cost(b.owner, b.record);
    if (r.kind == 4) {
        const auto* ids = templates_.block(0x6d60d8);
        if (ids && ids->bytes.size() > 0x10) c -= unit_cost(b.owner, ids->bytes[0x10]); // 0x6d60e8
    }
    refund(b.owner, c >> 1);
}

void World::order_guard(int p) {
    // Order 0x18: selected armed units that take orders: area guard (3)
    // around their "to" cell, target cleared.
    for (auto& u : units_) {
        if (u.owner != p || !u.selected || u.state == 0x11) continue;
        if (templates_.unit_records[size_t(u.record)].weapon == 0xff) continue;
        if (!(u.state != 8 && u.state != 0x14 && !(u.flags & 0x400140))) continue; // FUN_0049f4a0(u, 1)
        if (!set_state(u, 3)) continue;
        u.next_state = 1;
        u.home_x = u.to_x;
        u.home_y = u.to_y;
        u.target_kind = 0;
    }
}

bool World::retreat_destination(int p, int& x, int& y) const {
    // FUN_0046e3f0: the average cell of the buildings within distance² < 100
    // of the first primary slot's building (else the first building).
    int centre = players_[size_t(p)].primary[2]; // +0x26098, the construction yard slot
    if (centre < 0 || buildings_[size_t(centre)].removed) {
        centre = -1;
        for (const auto& b : buildings_) {
            if (b.owner == p && !b.removed) {
                centre = b.index;
                break;
            }
        }
    }
    if (centre < 0) {
        // No building: the pixel cell of the side's first unit that is not
        // an aircraft (0x40), clamped to W - 1 / H - 1; with none, the
        // centre of the box +0x262e8..+0x262eb. Returns 0 either way (the UI
        // callers test it, the AI ones use the cell).
        for (const auto& u : units_) {
            if (u.owner != p || u.remove || (u.flags & 0x40)) continue;
            x = std::min((u.x >> 16) >> 5, map_.width() - 1);
            y = std::min((u.y >> 16) >> 5, map_.height() - 1);
            return false;
        }
        int x0, x1, y0, y1;
        base_box(p, x0, x1, y0, y1);
        x = x0 + (x1 - x0) / 2;
        y = y0 + (y1 - y0) / 2;
        return false;
    }
    auto cell = [](const Building& b, int& cx, int& cy) {
        cx = (b.x >> 16) >> 5;
        cy = (b.y >> 16) >> 5;
    };
    int ccx, ccy;
    cell(buildings_[size_t(centre)], ccx, ccy);
    int sx = 0, sy = 0, n = 0;
    for (const auto& b : buildings_) {
        if (b.owner != p || b.removed) continue;
        int bx, by;
        cell(b, bx, by);
        if ((bx - ccx) * (bx - ccx) + (by - ccy) * (by - ccy) >= 100) continue;
        sx += bx;
        sy += by;
        ++n;
    }
    x = n ? sx / n : ccx;
    y = n ? sy / n : ccy;
    x = std::clamp(x, 0, map_.width() - 1);
    y = std::clamp(y, 0, map_.height() - 1);
    return true;
}

void World::order_retreat(int p, int x, int y) {
    // Order 0x1a: harvesters return to a refinery when the player owns
    // one; everything else moves to (x, y).
    bool refinery = false;
    for (const auto& b : buildings_) {
        if (b.owner == p && !b.removed && templates_.building_records[size_t(b.record)].kind == 4) refinery = true;
    }
    for (auto& u : units_) {
        if (u.owner != p || !u.selected || u.state == 0x11) continue;
        if (!(u.state != 8 && u.state != 0x14 && !(u.flags & 0x400140))) continue; // FUN_0049f4a0(u, 1)
        if (templates_.unit_records[size_t(u.record)].kind == 1 && refinery) {
            set_state(u, 0xd);
            continue;
        }
        if (set_state(u, state::kMove)) {
            u.next_state = 1;
            u.dest_x = uint8_t(x);
            u.dest_y = uint8_t(y);
        }
    }
}

// ---- unit repair (docs/re/21 section 4) -------------------------------------

int World::nearest_kind(int p, int kind, int x, int y, int& cx, int& cy) const {
    // FUN_004a5f80: u16 Manhattan distance from (x, y) to the far corner
    // (+0x18 / +0x19, the largest x / y of the 0x54 cells), strictly less
    // than the best so far (start 0x4000).
    int best = -1;
    uint16_t best_d = 0x4000;
    for (const auto& b : buildings_) {
        if (b.owner != p || b.removed) continue;
        const auto& r = templates_.building_records[size_t(b.record)];
        if (r.kind != kind) continue;
        int mx = b.cell_x, my = b.cell_y;
        for (int k = 0; k < 16; ++k) {
            if (!(r.footprint & (1u << k))) continue;
            mx = std::max(mx, b.cell_x + (k & 3));
            my = std::max(my, b.cell_y + (k >> 2));
        }
        const uint16_t d = uint16_t(std::abs(my - y) + std::abs(mx - x));
        if (d < best_d) {
            best_d = d;
            best = b.index;
            cx = b.cell_x + r.spawn_x;
            cy = b.cell_y + r.spawn_y;
        }
    }
    return best;
}

void World::order_repair_units(int p, int pad) {
    // FUN_004595e0 + ProcessOrder case 0xe: the selected non-infantry units
    // that take orders go to state 0x18 with the pad.
    for (auto& u : units_) {
        if (u.owner != p || !u.selected || u.state == 0x11 || u.remove) continue;
        if (templates_.unit_records[size_t(u.record)].infantry || !takes_orders(u)) continue;
        if (!set_state(u, 0x18)) continue;
        u.next_state = 1;
        u.repair_pad = building_slot(pad); // 0x457aed: the record's index as it is
    }
}

void World::order_repair_unit(int p, int unit_id) {
    // FUN_00459690: one unit, pad 0xffff (the nearest).
    const int i = find_unit(unit_id);
    if (i < 0) return;
    Unit& u = units_[size_t(i)];
    if (u.owner != p || templates_.unit_records[size_t(u.record)].infantry || !takes_orders(u)) return;
    if (!set_state(u, 0x18)) return;
    u.next_state = 1;
    u.repair_pad = -1;
}

void World::unit_voice(int record, bool ack) {
    const int house = players_[size_t(local_player)].house;
    if (house > 2) return;
    const auto& r = templates_.unit_records[size_t(record)];
    const size_t k = size_t(house * 3) + tick_count_ % 3;
    SoundEvent e;
    e.voice = true;
    e.mode0 = true;
    e.sample = int(ack ? r.ack_voice[k] : r.select_voice[k]);
    sounds.push_back(e);
}

void World::tick_repair(Unit& u) {
    const auto& r = templates_.unit_records[size_t(u.record)];
    auto& pl = players_[size_t(u.owner)];
    const bool local = u.owner == local_player;
    if (u.state == 0x18) {
        // Case 0x16: drive to the pad's entry cell.
        auto arrive = [&] {
            if (local) {
                voice(u.owner, "REPAIRUNIT");
                if (pl.cash + pl.spice == 0) voice(u.owner, "LOWMONEY");
            }
            set_state(u, 0x19);
        };
        // 0x49a4a8: GetBuilding(own side, +0x5a) and record kind 6, no state
        // test: a dying pad still counts, and a newer building in its slot
        // is followed (docs/re/50 section 6).
        const int pi = repair_pad_of(u);
        const bool valid = pi >= 0 && templates_.building_records[size_t(buildings_[size_t(pi)].record)].kind == 6;
        if (!valid) {
            int cx = 0, cy = 0;
            const int pad = nearest_kind(u.owner, 6, u.cur_x, u.cur_y, cx, cy);
            if (pad < 0) {
                if (u.steps == 0) set_state(u, state::kIdle);
                else move_step(u);
                return;
            }
            u.dest_x = uint8_t(cx);
            u.dest_y = uint8_t(cy);
            u.repair_pad = building_slot(pad);
            if (move_step(u) == 0) {
                arrive();
                return;
            }
        } else {
            const Building& b = buildings_[size_t(pi)];
            const auto& br = templates_.building_records[size_t(b.record)];
            u.dest_x = uint8_t(b.cell_x + br.spawn_x);
            u.dest_y = uint8_t(b.cell_y + br.spawn_y);
            if (move_step(u) == 0 || (u.cur_x == u.dest_x && u.cur_y == u.dest_y) ||
                ((u.flags & 0x10000000u) && u.to_x == u.dest_x && u.to_y == u.dest_y)) {
                arrive();
                return;
            }
        }
        // Not arrived: far away between steps, a carryall request (docs/re/33 section 2.1).
        carry_request(u, vars.repair_carry_distance);
        return;
    }
    if (u.state == 0x19) {
        // Case 0x17: on the pad.
        if (u.steps != 0) {
            move_step(u);
            return;
        }
        if (u.hit_points == r.hit_points) {
            u.next_state = 2;
            if (local) voice(u.owner, "UNITREPAIRED");
            set_state(u, 0x1a);
            return;
        }
        const int bi = building_at_cell(u.cur_x, u.cur_y); // FUN_004a5640: any building
        if (bi < 0 || buildings_[size_t(bi)].owner != u.owner) {
            set_state(u, 0x1a);
            return;
        }
        const uint32_t R2 = uint32_t(vars.unit_repair_points);
        uint32_t c = uint32_t((uint64_t(uint32_t(unit_cost(u.owner, u.record))) * R2) / uint32_t(r.hit_points));
        c = (c & ~1u) < 2 ? 1 : c >> 1;
        if (!spend(u.owner, int32_t(c))) return;
        buildings_[size_t(bi)].flags |= 0x20; // one animation cycle
        const int32_t hp = u.hit_points + int32_t(R2);
        if (hp > r.hit_points) {
            u.hit_points = r.hit_points;
        } else {
            u.hit_points = hp;
            if (local) {
                const uint32_t money = uint32_t(pl.cash + pl.spice);
                if (money == 0) {
                    if (!pl.low_money_said) {
                        pl.low_money_said = true;
                        voice(u.owner, "LOWMONEY");
                    }
                } else if (money > 99) {
                    pl.low_money_said = false;
                }
            }
        }
        return;
    }
    // State 0x1a (case 0x18): off the pad, or nudge a friendly neighbour.
    if (u.timer) --u.timer;
    int x, y;
    if (free_adjacent_cell(u, u.cur_x, u.cur_y, 0, x, y)) {
        u.dest_x = uint8_t(x);
        u.dest_y = uint8_t(y);
        set_state(u, state::kMoveAttack); // 8: leave the building
        return;
    }
    if (u.timer != 0) return;
    const int nx = int(rng_.next() % 3) + u.cur_x - 1;
    const int ny = int(rng_.next() % 3) + u.cur_y - 1;
    if (nx < 0 || ny < 0 || nx >= map_.width() || ny >= map_.height()) return;
    bool inf = false;
    const int vi = unit_in_cell(nx, ny, inf); // FUN_004a5270(cell * 32, ..)
    if (vi < 0 || &units_[size_t(vi)] == &u) return;
    Unit& v = units_[size_t(vi)];
    if (alliance_[size_t(u.owner)][size_t(v.owner)] != 0 || !takes_orders(v)) return;
    set_state(v, 9);
    u.timer = 0x32;
}

} // namespace d2k::sim
