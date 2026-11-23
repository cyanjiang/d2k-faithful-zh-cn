#include "sim/world.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace d2k::sim {

namespace {
// Cell occupancy bits (docs/re/09-unit-movement.md §6).
constexpr uint32_t kOwnerMask = 0x7;
constexpr uint32_t kVehicle = 0x8;
constexpr uint32_t kBuilding = 0x10;
constexpr uint32_t kInfantryMask = 0x3e0;
constexpr uint32_t kInfantryOwnerShift = 25;
constexpr uint32_t kWorm = 0x1000000;
constexpr uint32_t kConcrete = 0x800;

// Unit flags (+0x08).
constexpr uint32_t kTurning = 0x20;
constexpr uint32_t kAircraft = 0x40;
constexpr uint32_t kStepReserved = 0x10000000;

// Infantry sub-slot pixel positions in a cell (0x4d7028; same as 0x4c93e8).
constexpr int kSubpos[5][2] = {{16, 21}, {23, 14}, {23, 26}, {7, 26}, {7, 14}};

// 8 directions, index d = facing / 4: N, NE, E, SE, S, SW, W, NW.
constexpr int kDir[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};

int ftol(double v) { return int(v); } // MSVC __ftol truncates toward zero
} // namespace

int angle12(int dx, int dy) {
    // FUN_0049e160: octant-reduced polynomial atan, 0..0xfff (0 = north,
    // clockwise), coefficients at 0x4e95b0..0x4e95c0.
    if (dx == 0 && dy == 0) return 0;
    int oct = 0;
    if (dx < 0) {
        oct = 1;
        dx = -dx;
    }
    if (dy < 0) {
        oct += 2;
        dy = -dy;
    }
    int t = dy < dx ? (dy << 15) / dx : (dx << 15) / dy;
    const int t2 = t * t >> 15, t3 = t2 * t >> 15, t4 = t3 * t >> 15, t5 = t4 * t >> 15;
    int a = ((((109 * t2) >> 15) + ((10429 * t) >> 15)) - ((4290 * t3) >> 15)) + ((2246 * t4) >> 15) -
            ((300 * t5) >> 15);
    if (dy < dx) a = 0x4000 - a;
    const unsigned u = unsigned((a >> 4) - 0x800) & 0xfff;
    switch (oct) {
    case 0: return int(u);
    case 1: return int(0xfff - u);
    case 2: return int((0x7ff - u) & 0xfff);
    default: return int((u - 0x800) & 0xfff);
    }
}

int facing8(int ax, int ay, int bx, int by) {
    // Facing8 (doc 09 section 5.3): ((angle + 0x40) & 0xfff) >> 7, a multiple of 4.
    return ((((angle12(ax - bx, by - ay) + 0x40) & 0xfff) >> 7) & ~3);
}

SpeedTable parse_speed_table(std::span<const uint8_t> data) {
    SpeedTable t{};
    if (data.size() >= sizeof(t)) std::memcpy(t.data(), data.data(), sizeof(t));
    return t;
}

World::World(GameMap& map, const formats::Templates& templates, const ExeTables& tables,
             const SpeedTable& speed, const std::vector<Object>& placed, Rng& rng,
             std::span<const uint8_t> circles, std::span<const uint8_t> vars_bin, int local)
    : map_(map), templates_(templates), tables_(tables), speed_(speed), rng_(rng) {
    if (!circles.empty()) set_circles(circles);
    if (!vars_bin.empty()) set_vars(vars_bin);
    // gSideId is set before the map loads: the pre-placed buildings and
    // concrete of the local player reveal as they are created (doc 16 section 2).
    local_player = local;
    for (const auto& o : placed) {
        if (o.building) {
            // Map buildings are created pre-placed (FUN_004563b0 flag 5).
            players_[size_t(o.owner)].house = o.house;
            create_building(o.owner, o.record, o.cell_x, o.cell_y, true);
            continue;
        }
        add_map_unit(o);
    }
}

void World::add_map_unit(const Object& o) {
    // A map unit (FUN_00469790 -> FUN_00455870 with x1 = x2).
    const int slot = pool_pop(o.owner);
    if (slot < 0) {
        reached_unit_limit();
        return;
    }
    Unit u;
    u.index = next_unit_id_++;
    u.slot = uint16_t(slot);
    u.owner = o.owner;
    u.house = o.house;
    u.type = o.type;
    u.record = o.record;
    const auto& r = templates_.unit_records[size_t(u.record)];
    u.speed = r.speed;
    u.hit_points = o.hit_points;
    u.flags = r.initial_flags;
    u.cur_x = u.to_x = u.dest_x = uint8_t(o.cell_x);
    u.cur_y = u.to_y = u.dest_y = uint8_t(o.cell_y);
    u.x = o.x;
    u.y = o.y;
    u.facing = 0; // FUN_00455870: map-placed units face 0
    if (r.kind == 2) { // map carryalls start off the map too (x1, y1) = (x2, y2)
        launch_carryall(u, o.cell_x, o.cell_y, o.cell_x, o.cell_y);
        units_.push_back(u);
        return;
    }
    if (r.kind == 5) {
        // FUN_00455870 for kind 5: no occupancy; asleep and submerged
        // for SandWormInitialSleep; never drawn (0x200000).
        u.timer = uint8_t(vars.worm_initial_sleep);
        u.state = 0x1d;
        u.flags |= 0x200000 | 4;
        units_.push_back(u);
        return;
    }
    occupy(u, o.cell_x, o.cell_y, true);
    u.home_x = u.cur_x;
    u.home_y = u.cur_y;
    if (r.kind == 1) { // FUN_00455870 with x1 = x2: harvesters harvest
        u.refinery = building_slot(least_used_refinery(o.owner));
        u.state = 0xc;
    } else if (o.owner != local_player && !network && r.kind != 2 && r.kind != 8 && r.kind != 10) {
        // FUN_00455870 0x455c98..0x455cc7: not gSideId and not a network
        // game (0x7984c4 at 0x455ca5): other players' units guard their
        // area. Only campaign maps reach this line (skirmish and network
        // games add only the map's worms, which return above), so the
        // network test never changes a result; it is kept as the exe has it.
        u.state = 3;
    }
    units_.push_back(u);
}

// ---- occupancy --------------------------------------------------------------

int World::free_slot(int cx, int cy, int owner, int from) const {
    const uint32_t f = map_.at(cx, cy).flags;
    if ((f & kInfantryMask) && int((f >> kInfantryOwnerShift) & 7) != owner) return -1;
    for (int s = from; s >= 0; --s) {
        if (!(f & (0x20u << s))) return s;
    }
    return -1;
}

void World::occupy(Unit& u, int cx, int cy, bool placing) {
    const auto& r = templates_.unit_records[size_t(u.record)];
    auto& f = map_.at(cx, cy).flags;
    if (r.kind == 5) { // sandworm
        f |= kWorm;
        return;
    }
    if (!r.infantry) {
        f = (f & ~kOwnerMask) | uint32_t(u.owner) | kVehicle;
        return;
    }
    int slot = free_slot(cx, cy, u.owner, 4);
    if (slot < 0) slot = 4;
    f = (f & ~kOwnerMask) | (0x20u << slot) | uint32_t(u.owner); // FUN_00494260: owner bits replaced, not ORed
    f = (f & ~(7u << kInfantryOwnerShift)) | (uint32_t(u.owner) << kInfantryOwnerShift);
    u.slot_next = uint8_t(slot);
    if (placing) {
        // FUN_00455870: infantry stand at their slot (offset - 16 from the centre).
        u.slot_cur = uint8_t(slot);
        u.x = int32_t(cx * 32 + kSubpos[slot][0]) << 16;
        u.y = int32_t(cy * 32 + kSubpos[slot][1]) << 16;
    }
}

void World::vacate(Unit& u, int cx, int cy, uint8_t slot) {
    const auto& r = templates_.unit_records[size_t(u.record)];
    auto& f = map_.at(cx, cy).flags;
    if (r.kind == 5) {
        f &= ~kWorm;
    } else if (!r.infantry) {
        f &= ~kVehicle;
        // Infantry still in the cell keep their owner in the low bits.
        if (f & kInfantryMask) f = (f & ~kOwnerMask) | ((f >> kInfantryOwnerShift) & 7);
    } else {
        f &= ~(0x20u << slot);
    }
}

bool World::passable(int cx, int cy, const Unit& u, int mode) const {
    // FUN_0045d990 (CanUnitUseSquare). Modes: 0 = units block (crushers may
    // enter hostile infantry, infantry may share own cells with a free
    // slot); 1 = own / allied units that are moving (FUN_0049e140) can be
    // routed through; 2 = own / allied units are ignored; 3 = units are
    // ignored. "Allied" = alliance table 0x798830[side][cell owner] == 0.
    if (cx < 0 || cy < 0 || cx >= map_.width() || cy >= map_.height()) return false;
    const auto& r = templates_.unit_records[size_t(u.record)];
    const uint32_t f = map_.at(cx, cy).flags;
    if (r.kind == 5) return (f & cell::kSand) && !(f & kWorm);
    const int owner = int(f & kOwnerMask);
    const bool hostile = alliance_[size_t(u.owner)][size_t(owner)] != 0;
    const uint32_t units = f & 0x3f8;
    // A vehicle may enter its own side's factory exit cells (0x10000000).
    if (!r.infantry && !(f & 0x3e8) && (f & kBuilding) && (f & 0x10000000) && owner == u.owner) return true;
    // The targets' own cells (FUN_0045d990, before the mode rules):
    // state 0x18 the repair pad's cells (no unit there, own building);
    // state 5 the attacked building's cells; state 4, for cells not next to
    // the unit, the attacked unit's cell (FUN_004a5200: not a carryall,
    // aircraft kinds 8-10 or a worm, not 0x400140).
    if (u.state == 0x18 && !(f & 0x3e8) && (f & kBuilding) && owner == u.owner && repair_pad_of(u) >= 0 &&
        building_at_cell(cx, cy) == repair_pad_of(u)) {
        return true;
    }
    if (u.state == 5 && (f & kBuilding) && u.target_kind == 2 && building_at_cell(cx, cy) == u.target) return true;
    if (u.state == 4 && (std::abs(u.cur_x - cx) > 1 || std::abs(u.cur_y - cy) > 1) && (f & 0x3e8) &&
        u.target_kind == 1) {
        const int ti = find_unit(u.target);
        if (ti >= 0) {
            const Unit& t = units_[size_t(ti)];
            const int k = templates_.unit_records[size_t(t.record)].kind;
            const bool on = (t.cur_x == cx && t.cur_y == cy) || ((t.flags & 0x10000000) && t.to_x == cx && t.to_y == cy);
            if (on && k != 2 && k != 8 && k != 9 && k != 10 && k != 5 && !(t.flags & 0x400140)) return true;
        }
    }
    const uint32_t terrain = r.infantry ? cell::kInfantry : cell::kVehicles;
    auto moving_unit = [&]() { // FUN_004a5270 + FUN_0049e140
        bool inf = false;
        const int ui = unit_in_cell(cx, cy, inf);
        if (ui < 0) return false;
        const Unit& v = units_[size_t(ui)];
        return v.steps != 0 || v.route_count != 0 || v.state == state::kMove;
    };
    auto crush_tail = [&]() { return !(f & kVehicle) && (f & kInfantryMask) && r.crusher; };
    switch (mode) {
    case 0:
        if (!(f & terrain) || (f & 0x18)) return false;
        if (!(f & kInfantryMask)) return true;
        if (!r.infantry) return r.crusher && hostile;
        return owner == u.owner && free_slot(cx, cy, u.owner, 4) >= 0;
    case 1:
        if (!(f & terrain) || (f & kBuilding)) return false;
        if (!units) return true;
        if (r.infantry) {
            if ((f & kInfantryMask) && owner == u.owner && free_slot(cx, cy, u.owner, 4) >= 0) return true;
            if (hostile) return false;
            return moving_unit();
        }
        if (!hostile) return moving_unit();
        return crush_tail();
    case 2:
        if (!(f & terrain) || (f & kBuilding)) return false;
        if (!units || !hostile) return true;
        return r.infantry ? false : crush_tail();
    default: // 3
        if (!(f & terrain)) return false;
        if (f & 0x400) return hostile; // walls / turrets: passable only when hostile (attack-through), FUN_0045d990 mode 3
        return !(f & kBuilding);
    }
}

bool World::free_adjacent_cell(const Unit& u, int x, int y, int mode, int& ox, int& oy) const {
    // FUN_0045dee0: the facing's 8-way direction first, then +4 / -4
    // alternately, 4 pairs. FUN_0045de30 steps one cell in a direction.
    auto step = [&](int dir) {
        const int d = (dir & 0x1f) >> 2;
        ox = x + kDir[d][0];
        oy = y + kDir[d][1];
        return passable(ox, oy, u, mode);
    };
    const int start = u.facing & ~3;
    if (step(start)) return true;
    int up = start, down = start;
    for (int k = 0; k < 4; ++k) {
        up = up + 4 > 0x1f ? 0 : up + 4;
        if (step(up)) return true;
        down = down - 4 < 0 ? 0x1c : down - 4;
        if (step(down)) return true;
    }
    return false;
}

// ---- orders and selection ---------------------------------------------------

bool World::set_state(Unit& u, int s) {
    // FUN_0049deb0 (SetState). Returns false when the change is refused.
    const auto& r = templates_.unit_records[size_t(u.record)];
    auto in = [](int v, std::initializer_list<int> l) { return std::find(l.begin(), l.end(), v) != l.end(); };
    switch (r.kind) {
    case 1: case 3: case 4: case 5: case 6: case 7: case 0xb: case 0xc: case 0xd: case 0xe: case 0x10:
        break;
    case 2: case 8: case 9: case 10: // aircraft
        if (in(s, {3, 4, 5, 6, 8, 9, 10, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x12, 0x13, 0x17, 0x18, 0x19, 0x1a, 0x1b,
                   0x1c, 0x1d, 0x1e, 0x1f, 0x20})) {
            return false;
        }
        break;
    default:
        if (in(s, {0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x15, 0x16, 0x17, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20})) return false;
        break;
    }
    if (r.weapon == 0xff) { // unarmed: no attack states
        if (s == 3 || s == 4 || s == 6 || s == 10) return false;
        if (s == 5 && r.kind != 4 && r.kind != 3) return false;
    }
    // The route fields are cleared even when the change is refused below.
    u.route_count = 0;
    u.flags = (u.flags & 0x3fffffff) | 0x80000000;
    u.path_length = 0;
    u.waiting = 0;
    if (s != 0xa) u.blocked = 0;
    const int cur = u.state;
    u.repath_delay = 0;
    if (cur == s) return true;
    if (cur == 0x11) return false;
    if ((cur == 0x1f || cur == 0x17 || cur == 0x1e) && s != 0x11) return false;
    if (s == 9 && (cur == 0x19 || cur == 0x1a || cur == 0xb)) return false;
    // Docked / unloading harvesters: 0xe -> 0xf / 0x10, 0xf -> 0x10, 0x10 -> 0xb (dock frame 0 only).
    if (cur == 0xe && s != 0x11 && s != 0xf && s != 0x10) return false;
    if (cur == 0xf && s != 0x11 && s != 0x10) return false;
    if (cur == 0x10 && s != 0x11 && (s != 0xb || u.frame != 0)) return false;
    if (s == 0x11) u.selected = false;
    if (s == 9) u.next_state = cur == 0x12 ? 2 : cur; // come back to the old state
    if (s == 2 && u.next_state != 1) {
        u.state = u.next_state == 0x12 ? 2 : u.next_state;
        u.next_state = 1;
        return true;
    }
    u.state = s;
    return true;
}

void World::order_move(int owner, int cell_x, int cell_y) {
    // ProcessOrder case 0: each id that is a unit and passes
    // FUN_0049f4a0(u, 1) (ebx = 1 at 0x457897); the fields are written only
    // when SetState succeeds.
    for (auto& u : units_) {
        if (u.owner != owner || !u.selected || u.remove || !takes_orders_forced(u)) continue;
        if (is_harvester(u) && (map_.at(cell_x, cell_y).flags & cell::kSpiceLevelMask)) {
            // A harvester sent onto spice (doc 13 section 8.1).
            if (!set_state(u, 0xb)) continue;
            u.next_state = 1;
            u.dest_x = u.home_x = uint8_t(cell_x);
            u.dest_y = u.home_y = uint8_t(cell_y);
            continue;
        }
        if (!set_state(u, state::kMove)) continue;
        u.next_state = 1;
        u.dest_x = uint8_t(cell_x);
        u.dest_y = uint8_t(cell_y);
    }
}

bool World::selectable(const Unit& u, int player) const {
    // FUN_004a5ae0 / FUN_004a5a50: +0x10 is the unit's STATE. Not dying
    // (0x11), and in the factory-exit state 8 only while mid-step (+0x4f).
    if (u.owner != player || (u.flags & 0x140)) return false;
    if (u.state == 0x11) return false;
    if (u.state == 8 && u.steps == 0) return false;
    return true;
}

void World::deselect_all() {
    for (auto& u : units_) u.selected = false;
    for (auto& b : buildings_) b.selected = false;
}

void World::select_unit(size_t i, bool toggle) {
    if (toggle) { // FUN_004a5a50(..., 1): buildings stay selected (docs/re/38 section 1)
        units_[i].selected = !units_[i].selected;
        return;
    }
    deselect_all();
    units_[i].selected = true;
}

void World::view_unit(size_t i) {
    // FUN_004a5a50(owner, idx, 0) for any owner: not carried / aircraft
    // (0x140), not in the factory exit before its first step, not dying;
    // everything else is deselected first.
    const Unit& u = units_[i];
    if ((u.flags & 0x140) || (u.state == 8 && u.steps == 0) || u.state == 0x11) return;
    deselect_all();
    units_[i].selected = true;
}

void World::select_building_checked(size_t i) {
    // FUN_004a5ab0: everything is deselected first, then the building is
    // selected unless it is dying, building up or being sold (flags & 6).
    deselect_all();
    const Building& b = buildings_[i];
    if (!b.dying && !(b.flags & 6)) buildings_[i].selected = true;
}

void World::select_building(size_t i) {
    deselect_all();
    buildings_[i].selected = true;
}

bool World::box_select(int player, int x0, int y0, int x1, int y1) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    // FUN_004a5ae0: the first pass ("any unit inside?") tests only the state
    // and the position, not flags 0x140, so a box with only own aircraft
    // (or carried units) still deselects everything and then selects
    // nothing (docs/re/35 section 8).
    auto inside = [&](const Unit& u) {
        if (u.owner != player || u.remove) return false;
        if ((u.state == 8 && u.steps == 0) || u.state == 0x11) return false;
        const int ux = u.x >> 16, uy = u.y >> 16;
        return ux > x0 && ux < x1 && uy > y0 && uy < y1;
    };
    if (std::none_of(units_.begin(), units_.end(), inside)) return false;
    deselect_all(); // FUN_004a5790
    for (auto& u : units_) {
        if (inside(u) && !(u.flags & 0x140)) u.selected = true;
    }
    return true;
}

bool World::any_unit_selected(int player) const {
    return std::any_of(units_.begin(), units_.end(), [&](const Unit& u) { return u.owner == player && u.selected; });
}

// ---- movement ---------------------------------------------------------------

int32_t World::cell_speed(const Unit& u, int cx, int cy) const {
    const auto& r = templates_.unit_records[size_t(u.record)];
    const uint32_t f = map_.at(cx, cy).flags;
    int32_t v;
    if (f & kConcrete) {
        v = ftol(double(r.speed) * 1.1); // 1.1 is a double constant (0x4d7060)
    } else {
        const size_t i = size_t((f >> 29) * 4 + r.speed_class);
        v = ftol(double(r.speed) * double(i < speed_.size() ? speed_[i] : 1.0f));
    }
    return v;
}

bool World::turn_body(Unit& u, int dir) {
    if (u.facing == dir) return false;
    const auto& r = templates_.unit_records[size_t(u.record)];
    if (r.infantry) set_inf_anim(u, 0); // FUN_00493eb0 calls SetInfAnim(0)
    if (u.turn_counter > 0) {
        --u.turn_counter;
        return true;
    }
    const int d = dir - int(u.facing);
    int step;
    if (std::abs(d) < 16) {
        step = d > 0 ? 1 : -1;
    } else {
        step = d > 0 ? -1 : 1; // |d| = 16: +1 when the goal is below (FUN_00493eb0, verified)
    }
    u.facing = uint8_t((u.facing + step + 32) & 31);
    u.turn_counter = r.turn_delay;
    return true;
}

int World::move_step(Unit& u) {
    const auto& r = templates_.unit_records[size_t(u.record)];
    if (u.steps == 0 && u.cur_x == u.dest_x && u.cur_y == u.dest_y) return 0;

    if (u.steps == 0) {
        // Auto-fire between steps (0x4943f3, docs/re/18 section 3.6), in
        // every state that steps: only while the unit waits (+0x78 != 0), in
        // its 8-tick slot or with a target, not hidden by stealth
        // (FUN_0049f5f0), not in state 0xA and not a worm (rec+0x2d != 5).
        // It runs after the "at the destination" return above.
        if (u.repath_delay != 0 && (((uint32_t(u.slot) & 7) == (tick_count_ & 7)) || u.target_kind != 0) &&
            !is_stealthed(u) && u.state != 0xa && r.kind != 5) {
            auto_fire(u);
        }
        if (u.to_x == u.cur_x && u.to_y == u.cur_y) {
            next_square(u);
            // FUN_0049ebf0 on the "to" cell, before the passability test
            // (docs/re/54 section 2.2): new units on the cell -> pick again;
            // otherwise a picker killed by an explosion crate stops here.
            if (drive_over(u)) {
                u.route_count = 0;
                next_square(u);
            } else if (u.state == 0x11) {
                return 1;
            }
            if (u.to_x == u.cur_x && u.to_y == u.cur_y) return 1; // waiting
        }
        const int dir = facing8(u.cur_x, u.cur_y, u.to_x, u.to_y);
        const int diff = (int(u.facing) - dir + 32) & 31;
        // FUN_00494260 clears the turn latch 0x20 only when the unit faces
        // the step exactly (0x49450f; also 0x494894 below).
        if (diff == 0) u.flags &= ~kTurning;
        if (!(u.flags & kTurning) && (diff < 7 || diff > 24)) {
            if (!passable(u.to_x, u.to_y, u, 0) || (u.flags & 0x40000000)) {
                if (!(u.flags & 0x40000000)) {
                    u.blocked = 1; // 0x494c8e
                } else {
                    // 0x494560: a fresh order that had to turn first re-checks
                    // its cached route in mode 0 (0x494c76 / 0x494c80).
                    u.flags &= ~0x40000000u;
                    bool any = false;
                    for (int k = 0; k < u.route_count && !any; ++k) {
                        any = !passable(u.route_x[size_t(k)], u.route_y[size_t(k)], u, 0);
                    }
                    u.blocked = any ? 1 : 0;
                }
                u.steps = 0;
                u.route_count = 0; // 0x494c95
                u.to_x = u.cur_x;
                u.to_y = u.cur_y;
                return 1;
            }
            u.flags |= kStepReserved;
            occupy(u, u.to_x, u.to_y, false);
            if (r.infantry) {
                u.step_x = int32_t(u.to_x * 32 + kSubpos[u.slot_next][0]) << 16;
                u.step_y = int32_t(u.to_y * 32 + kSubpos[u.slot_next][1]) << 16;
            } else {
                u.step_x = int32_t(u.to_x * 32 + 16) << 16;
                u.step_y = int32_t(u.to_y * 32 + 16) << 16;
            }
            int32_t speed = cell_speed(u, u.to_x, u.to_y);
            if (!r.infantry) {
                if (u.hit_points * 3 < r.hit_points) {
                    speed = ftol(double(float(speed)) * double(0.85f));
                } else if (u.hit_points * 2 < r.hit_points) {
                    speed = ftol(double(float(speed)) * double(0.66f));
                }
            } else if (u.anim_mode == 5) {
                speed >>= 1;
            }
            if (speed <= 0) speed = 1;
            u.speed = speed;
            const uint32_t dist = uint32_t(std::max(std::abs(u.x - u.step_x), std::abs(u.y - u.step_y)));
            u.steps = uint8_t(std::min<uint32_t>(dist / uint32_t(speed), 255));
            u.half = uint8_t(u.steps >> 1);
        }
    }

    // Turn toward the step (or keep facing while idle).
    const bool stepping = !(u.to_x == u.cur_x && u.to_y == u.cur_y) || u.steps > 0;
    const int dir = stepping && !(u.to_x == u.cur_x && u.to_y == u.cur_y)
                        ? facing8(u.cur_x, u.cur_y, u.to_x, u.to_y)
                        : u.facing;
    const int diff = (int(u.facing) - dir + 32) & 31;
    if (diff == 0) u.flags &= ~kTurning; // 0x494894: the only other clear of 0x20
    const bool can_move = !(u.flags & kTurning) && (diff < 7 || diff > 24) && u.steps > 0;
    if (!can_move && stepping) {
        // 0x4948ac..0x4948c8: a fresh order (0x80000000) that cannot move
        // becomes 0x40000000 (its route is checked again at the next step
        // start, above), and the latch is set.
        if (u.flags & 0x80000000u) u.flags = (u.flags & 0x3fffffffu) | 0x40000000u;
        u.flags |= kTurning;
    }
    turn_body(u, dir);
    // 0x4948d7: a turreted unit not yet at its destination turns the turret
    // toward the destination cell (8-way, FUN_0045e020), every call.
    if (r.has_turret && (u.cur_x != u.dest_x || u.cur_y != u.dest_y)) {
        turn_turret(u, facing8(u.cur_x, u.cur_y, u.dest_x, u.dest_y));
    }
    if (!can_move) return 1;

    u.flags &= ~0xc0000000u;
    if (r.infantry) set_inf_anim(u, 1); // walk (FUN_004940d0 advances the frame)
    const int32_t s = u.speed;
    if ((u.x >> 16) < ((u.step_x - s / 2) >> 16)) u.x += s;
    else if ((u.x >> 16) > ((u.step_x + s / 2) >> 16)) u.x -= s;
    if ((u.y >> 16) < ((u.step_y - s / 2) >> 16)) u.y += s;
    else if ((u.y >> 16) > ((u.step_y + s / 2) >> 16)) u.y -= s;
    --u.steps;
    if (u.steps < u.half) {
        u.half = 0;
        vacate(u, u.cur_x, u.cur_y, u.slot_cur);
        u.slot_cur = u.slot_next;
        u.cur_x = u.to_x;
        u.cur_y = u.to_y;
        u.flags &= ~kStepReserved;
    }
    return 1;
}

void World::next_square(Unit& u) {
    // FUN_004936a0 (GetNextSquare). `p3` is the unit's pool slot (+0x8c,
    // docs/re/50), which staggers the waits.
    const uint32_t p3 = uint32_t(u.slot);
    if (u.repath_delay) --u.repath_delay;
    const int cx = u.cur_x, cy = u.cur_y;
    if (cx == u.dest_x && cy == u.dest_y) {
        u.route_count = 0;
        u.blocked = 0;
        return;
    }
    if (u.blocked >= 2 && u.state == state::kMove && std::abs(u.dest_x - cx) < 3 && std::abs(u.dest_y - cy) < 3) {
        u.route_count = 0; // close enough
        u.blocked = 0;
        u.dest_x = uint8_t(cx);
        u.dest_y = uint8_t(cy);
        return;
    }
    std::vector<std::array<int, 2>> path;
    // Installs a new route (at most 12 cells, the next one last).
    auto install = [&](size_t from) {
        const size_t n = std::min<size_t>(path.size() - from, 12);
        u.route_count = uint8_t(n);
        for (size_t i = 0; i < n; ++i) {
            u.route_x[n - 1 - i] = uint8_t(path[from + i][0]);
            u.route_y[n - 1 - i] = uint8_t(path[from + i][1]);
        }
    };
    // A mode-0 search (units block), accepted when it is shorter than the
    // remaining route + 10. 1 = installed, 0 = too long, -1 = no route.
    auto reroute = [&]() {
        // FindPath in mode 0, smoothed; only a full route counts. The cache
        // gets len - 1 cells from the path's first cell (0x493bd4).
        if (find_path(u, 0, path) != 1) return -1;
        smooth_path(u, path);
        const int n = int(path.size());
        if (n >= int(u.path_length) + 10) return 0;
        u.blocked = 0;
        u.path_length = uint16_t(n);
        const int keep = std::min(n - 1, 12);
        u.route_count = uint8_t(std::max(keep, 0));
        for (int i = 0; i < keep; ++i) {
            u.route_x[size_t(keep - 1 - i)] = uint8_t(path[size_t(i)][0]);
            u.route_y[size_t(keep - 1 - i)] = uint8_t(path[size_t(i)][1]);
        }
        return 1;
    };
    if (u.route_count > 0) {
        const int nx = u.route_x[u.route_count - 1], ny = u.route_y[u.route_count - 1];
        if (passable(nx, ny, u, 0)) {
            u.repath_delay = 0;
            --u.route_count;
            if (u.path_length) --u.path_length;
            u.to_x = uint8_t(nx);
            u.to_y = uint8_t(ny);
            if (u.waiting) {
                u.waiting = 0;
                if (u.blocked != 1) u.blocked = 0;
            }
            return;
        }
        if (!passable(nx, ny, u, u.blocked)) {
            // Terrain, a building, or blocked in the current mode.
            if (u.blocked == 0) {
                const int res = reroute();
                if (res == 1) return;
                if (res == 0) {
                    u.blocked = 1; // too long: route through moving friends next time
                    return;
                }
            }
            u.route_count = 0; // search again on the next call
            return;
        }
        // Blocked only by units.
        if (u.repath_delay == 0 && u.state != 0xa) {
            const int res = reroute();
            if (res == 0 && u.waiting) {
                if (u.path_length < 10) u.route_count = 0;
                else u.path_length = uint16_t(u.path_length - 10);
            } else if (res < 0) {
                if (!u.waiting) {
                    u.repath_delay = uint16_t((p3 & 0x3f) + 800);
                } else if (u.blocked < 2) {
                    ++u.blocked;
                } else if (u.state == 0xd) {
                    u.blocked = 0;
                } else {
                    u.route_count = 0; // give up
                    u.blocked = 0;
                    u.dest_x = uint8_t(cx);
                    u.dest_y = uint8_t(cy);
                }
            }
        }
        if (u.blocked >= 2 && (map_.at(nx, ny).flags & 0x3f8)) {
            const int owner = int(map_.at(nx, ny).flags & kOwnerMask);
            if (alliance_[size_t(u.owner)][size_t(owner)] == 1) {
                // Attack the blocker (state 0xa); the destination is kept in +0x50.
                u.home_x = u.dest_x;
                u.home_y = u.dest_y;
                u.next_state = u.state;
                u.dest_x = uint8_t(nx);
                u.dest_y = uint8_t(ny);
                set_state(u, 0xa);
            } else {
                ask_to_scatter(u, nx, ny); // FUN_00497af0
            }
        }
        if (u.repath_delay == 0) u.repath_delay = uint16_t((p3 & 0xf) + 0x50);
        u.waiting = 1;
        return;
    }
    if (u.repath_delay) return;
    // FindPath in the current mode, then always the smoothing (0x493c0f).
    const int code = find_path(u, u.blocked, path);
    smooth_path(u, path);
    const int len = int(path.size());
    if (code == 1 && len == 0) {
        // FUN_004936a0: +0x76 = 0, no route cached; the exe then takes a
        // stale cell left in the static path buffer as the next cell
        // (0x493e49). Only a wall-follow that ends in a combination the exe
        // merely logs gives this; not modelled (the unit stays this call).
        u.path_length = 0;
        return;
    }
    if (code != 1 && len == 0) {
        ++u.blocked;
        u.repath_delay = uint16_t((p3 & 7) + 8);
        const auto& r = templates_.unit_records[size_t(u.record)];
        if (u.blocked > 3 || (u.blocked == 3 && r.weapon == 0xff)) { // unarmed units give up sooner
            u.route_count = 0;
            u.blocked = 0;
            u.dest_x = uint8_t(cx);
            u.dest_y = uint8_t(cy);
            u.repath_delay = uint16_t((p3 & 0xf) + 0x50);
        }
        return;
    }
    // A (possibly partial) route. If its first cell is blocked in mode 0,
    // the unit stays this tick and the whole path goes into the route.
    if (!passable(path[0][0], path[0][1], u, 0)) {
        u.path_length = uint16_t(len + 1);
        install(0);
        return;
    }
    u.path_length = uint16_t(len);
    u.to_x = uint8_t(path[0][0]);
    u.to_y = uint8_t(path[0][1]);
    install(1);
}

void World::ask_to_scatter(const Unit& u, int x, int y) {
    // FUN_00497af0: every unit of the cell's owner in the cell that takes
    // orders (FUN_0049f4a0) and is not moving (state 7) gets state 9.
    const uint32_t f = map_.at(x, y).flags;
    if (!(f & 0x3f8)) return;
    const int owner = int(f & kOwnerMask);
    for (auto& v : units_) {
        if (&v == &u || v.owner != owner || v.remove) continue;
        if (!((v.cur_x == x && v.cur_y == y) || ((v.flags & 0x10000000) && v.to_x == x && v.to_y == y))) continue;
        if (!takes_orders(v) || v.state == state::kMove) continue;
        set_state(v, 9);
    }
}

bool World::takes_orders(const Unit& v) const {
    // FUN_0049f4a0(v, 0).
    return v.state != 0x12 && v.state != state::kMoveAttack && v.state != 0x14 && !(v.flags & 0x400140);
}

void World::tick_scatter(Unit& u) {
    // State 9 (FUN_00497b90 case 9): step to a free adjacent cell, or ask
    // the friendly neighbours to move first.
    if (u.steps != 0) {
        move_step(u);
        return;
    }
    int x, y;
    if (free_adjacent_cell(u, u.cur_x, u.cur_y, 0, x, y)) {
        u.dest_x = uint8_t(x);
        u.dest_y = uint8_t(y);
        if (u.next_state == 3) {
            u.home_x = uint8_t(x);
            u.home_y = uint8_t(y);
        }
        if (move_step(u) != 0) {
            if (u.flags & kStepReserved) set_state(u, state::kMove);
            return;
        }
    } else {
        bool pushed = false;
        for (int dir = 0; dir < 0x20; dir += 4) {
            const int nx = u.cur_x + kDir[dir >> 2][0], ny = u.cur_y + kDir[dir >> 2][1];
            if (nx < 0 || ny < 0 || nx >= map_.width() || ny >= map_.height()) continue;
            bool inf = false;
            const int vi = unit_in_cell(nx, ny, inf);
            if (vi < 0) continue;
            Unit& v = units_[size_t(vi)];
            if (alliance_[size_t(u.owner)][size_t(v.owner)] != 0 || !takes_orders(v)) continue;
            if (v.state == state::kMove || v.state == 0x1a || v.state == 0xb || v.state == 9) continue;
            set_state(v, 9);
            pushed = true;
        }
        if (pushed) return;
    }
    const int ns = u.next_state;
    set_state(u, (ns != 1 && ns != 0x12) ? ns : state::kIdle);
}

void World::tick_attack_blocker(Unit& u) {
    // State 0xa (FUN_00497b90 case 10): between steps, when the blocking
    // cell is empty or ours, go back to the saved destination and state;
    // else attack the cell (FUN_00496ff0).
    if (u.steps != 0) {
        move_step(u);
        return;
    }
    const uint32_t f = map_.at(u.dest_x, u.dest_y).flags;
    if (!(f & 0x3f8) || int(f & kOwnerMask) == u.owner) {
        u.dest_x = u.home_x;
        u.dest_y = u.home_y;
        u.blocked = 0;
        set_state(u, u.next_state);
        return;
    }
    if (!can_attack_cell(u, u.dest_x, u.dest_y, false)) {
        move_step(u);
        return;
    }
    if (u.reload) return;
    if (u.cur_x == u.dest_x && u.cur_y == u.dest_y) return;
    unit_fire(u, u.dest_x * 32 + 16, u.dest_y * 32 + 16, 0, -1);
}

// ---- tick -------------------------------------------------------------------

void World::post_order(int side, std::function<void()> run) {
    // FUN_0045bf00 (docs/re/49 section 1): execute tick = tick + latency;
    // the record keeps the ids of the side's selected units.
    PendingOrder o;
    o.rec.tick = tick_count_ + latency_;
    o.rec.side = uint8_t(side);
    capture_selection(o.rec);
    o.run = std::move(run);
    pending_orders_.push_back(std::move(o));
}

void World::capture_selection(Order& o) const {
    // The posters (FUN_00459450 ...) walk the side's list and take the first
    // 64 selected units (+0x19) that are not dying (0x11), with their own
    // filter: 0x14 armed (rec +0x0c != -1, FUN_004596f0), 0x21 not infantry
    // (rec +0x11, FUN_004595e0); then up to 64 selected buildings (+0x35)
    // that are not dying (FUN_00459c40). Further ones get no order.
    constexpr size_t kMaxIds = 0x40;
    o.ids.clear();
    o.bids.clear();
    for (const auto& u : units_) {
        if (o.ids.size() >= kMaxIds) break;
        if (u.owner != o.side || !u.selected || u.remove || u.state == 0x11) continue;
        const auto& r = templates_.unit_records[size_t(u.record)];
        if (o.type == order::kAttackUnit && r.weapon == 0xff) continue;
        if (o.type == order::kRepairUnits && o.b == 0 && r.infantry) continue;
        o.ids.push_back(u.index);
    }
    // 0x1d carries the unit ids first and does not reset the count
    // (FUN_00459c40, doc 38 section 6): buildings fill what is left of 64.
    const size_t bmax = o.type == order::kStop ? kMaxIds - o.ids.size() : kMaxIds;
    for (size_t i = 0; i < buildings_.size(); ++i) {
        if (o.bids.size() >= bmax) break;
        const auto& b = buildings_[i];
        if (b.owner == o.side && b.selected && !b.removed && !b.dying) o.bids.push_back(int(i));
    }
}

Order World::post(Order o) {
    // FUN_0045bf00: in a network game a human's order is numbered from the
    // side's counter (shared with its leave / latency records) and sent.
    o.tick = tick_count_ + latency_;
    capture_selection(o);
    encode_refs(o);
    const bool sent = network && human_[size_t(o.side & 7)];
    if (sent) o.seq = ++posted_seq_[size_t(o.side & 7)];
    PendingOrder p;
    p.rec = o;
    pending_orders_.push_back(std::move(p));
    if (sent && on_post) on_post(o);
    return o;
}

void World::dispatch(const Order& o) {
    // ProcessOrder (FUN_00457810): the handler of each record type.
    const int s = o.side;
    switch (o.type) {
    case order::kMove: order_move(s, o.a, o.b); break;
    case order::kAttackUnit: order_attack_unit(s, o.a); break;
    case order::kAttackBuilding: order_attack_building(s, o.a); break;
    case order::kAttackGround: order_attack_ground(s, o.a, o.b); break;
    case order::kDock: order_dock(s, o.a); break;
    case order::kGuard: order_guard(s); break;
    case order::kScatter: order_scatter(s); break;
    case order::kRetreat: order_retreat(s, o.a, o.b); break;
    case order::kStop: order_stop(s); break;
    case order::kTurretUnit: order_turret_attack_unit(s, o.a); break;
    case order::kTurretBuilding: order_turret_attack_building(s, o.a); break;
    case order::kPrimary: order_primary(s); break;
    case order::kRepairUnits:
        if (o.b == 1) order_repair_unit(s, o.a);
        else order_repair_units(s, o.a);
        break;
    case order::kRepairBuilding: order_repair_building(s, o.a); break;
    case order::kSell: order_sell(s, o.a); break;
    case order::kStartBuilding: start_building(s, o.a); break;
    case order::kHoldBuilding: hold_or_cancel_building(s, o.a); break;
    case order::kPlace: place_building(s, o.a, o.b, o.c); break;
    case order::kStartUnit: start_unit(s, o.a); break;
    case order::kHoldUnit: hold_or_cancel_unit(s, o.a); break;
    case order::kStarportAdd: starport_add(s, o.a); break;
    case order::kStarportRemove: starport_remove(s, o.a); break;
    case order::kStarportBuy: starport_purchase(s); break;
    case order::kStarportCancel: starport_cancel(s); break;
    case order::kStartUpgrade: start_upgrade(s, o.a); break;
    case order::kHoldUpgrade: hold_or_cancel_upgrade(s, o.a); break;
    case order::kDeploy: order_deploy(s, o.a); break;
    case order::kSpecial: order_special(s, o.a, o.b, o.c); break;
    case order::kAlly: order_ally(s, o.a); break;
    case order::kRetire:
        // Case 0x1E: the side is removed and gets its finishing place.
        remove_side(s);
        if (place_[size_t(s)] == 0xff) place_[size_t(s)] = next_place_++; // FUN_0044c4f0
        break;
    case order::kAiTakeOver: let_ai_take_over(s); break; // case 0x1F
    default: break; // unknown types are dropped (the exe drops types > 0x34)
    }
}

void World::with_selection(const Order& o, const std::function<void()>& run) {
    // The side's selection becomes the record's ids for the handler (dead
    // ones are gone, a changed selection does not matter), then is restored.
    std::vector<std::pair<int, bool>> saved, saved_b;
    for (auto& u : units_) {
        if (u.owner != o.side) continue;
        saved.emplace_back(u.index, u.selected);
        u.selected = std::find(o.ids.begin(), o.ids.end(), u.index) != o.ids.end();
    }
    for (size_t i = 0; i < buildings_.size(); ++i) {
        auto& b = buildings_[i];
        if (b.owner != o.side) continue;
        saved_b.emplace_back(int(i), b.selected);
        b.selected = std::find(o.bids.begin(), o.bids.end(), int(i)) != o.bids.end();
    }
    run();
    for (const auto& [id, sel] : saved) {
        const int i = find_unit(id);
        if (i >= 0 && units_[size_t(i)].owner == o.side) units_[size_t(i)].selected = sel;
    }
    for (const auto& [i, sel] : saved_b) {
        if (buildings_[size_t(i)].owner == o.side) buildings_[size_t(i)].selected = sel;
    }
}

namespace {
// The record types whose `a` names another object (record +0x20, side +0x22).
int ref_kind(const Order& o) {
    switch (o.type) {
    case order::kAttackUnit: case order::kTurretUnit: case order::kDeploy: return 1;
    case order::kRepairUnits: return o.b == 1 ? 1 : 2;
    case order::kAttackBuilding: case order::kTurretBuilding: case order::kDock:
    case order::kRepairBuilding: case order::kSell: return 2;
    default: return 0;
    }
}
} // namespace

void World::encode_refs(Order& o) const {
    // The exe's record keeps the target's (side, slot) (+0x22 / +0x20), not
    // the object: the handler 2 ticks later acts on whatever object of that
    // kind holds the slot then (docs/re/50 section 6). `c` (unused by these
    // types) carries the side.
    const int k = ref_kind(o);
    if (k == 0) return;
    o.c = -1;
    if (k == 1) {
        const int i = o.a >= 0 ? find_unit(o.a) : -1;
        o.a = i >= 0 ? units_[size_t(i)].slot : -1;
        if (i >= 0) o.c = units_[size_t(i)].owner;
    } else if (o.a >= 0 && size_t(o.a) < buildings_.size()) {
        o.c = buildings_[size_t(o.a)].owner;
        o.a = buildings_[size_t(o.a)].slot;
    } else {
        o.a = -1;
    }
}

Order World::decode_refs(const Order& o) const {
    Order r = o;
    const int k = ref_kind(o);
    if (k == 0) return r;
    if (o.a < 0 || o.c < 0 || o.c > 7) {
        r.a = -1;
    } else if (k == 1) {
        const Unit* u = unit_in_slot(o.c, o.a);
        r.a = u ? u->index : -1;
    } else {
        const Building* b = building_in_slot(o.c, o.a);
        r.a = b ? b->index : -1;
    }
    r.c = 0;
    return r;
}

void World::execute(const Order& o) {
    const Order r = decode_refs(o);
    // A slot that holds no object of the kind: the handler does nothing
    // (GetUnit / GetBuilding null); the pad of 0x21 falls back to the nearest.
    if (ref_kind(o) != 0 && r.a < 0 && !(o.type == order::kRepairUnits && o.b == 0)) return;
    with_selection(r, [&] { dispatch(r); });
}

void World::run_orders() {
    if (network) {
        run_orders_network();
        return;
    }
    // FUN_0045d120(0): every order due this tick, in post order.
    size_t done = 0;
    while (done < pending_orders_.size() && pending_orders_[done].rec.tick <= tick_count_) {
        PendingOrder o = std::move(pending_orders_[done++]);
        if (o.run) with_selection(o.rec, o.run);
        else execute(o.rec);
    }
    pending_orders_.erase(pending_orders_.begin(), pending_orders_.begin() + std::ptrdiff_t(done));
}

void World::tick() {
    // Network games: the tick's sync value, one rand() before the counter
    // goes up (FUN_00448550 0x449a15, debug line 0x19f8; docs/re/57 section 2).
    if (network) sync_ = uint16_t(rng_.next());
    ++tick_count_;
    if (mission_timer != -1) --mission_timer; // 0x449a49
    for (auto& m : messages) --m.ticks; // message lines last 200 ticks
    messages.erase(std::remove_if(messages.begin(), messages.end(), [](const Message& m) { return m.ticks <= 0; }),
                   messages.end());
    // The tick block (FUN_00448550 0x449a43..0x449b28): counter, timer, the
    // AI of every active slot (0x449a7a), the script FUN_00453b90
    // (0x449a8d) with FUN_00455480 (0x449a92), the cell objects, the
    // specials, the shake, FUN_0044aca0 (orders, objects, repairs, power,
    // storage), the unit reveal FUN_0044cff0 (0x449aeb, inside
    // tick_economy), the production, the tick & 7 player, the places.
    tick_ai();      // FUN_0041b610 per active slot, before the script (doc 22 section 2.2)
    tick_script();  // FUN_00453b90 (0x449a8d), right after the DoAI loop; ends with FUN_00455480 (0x449a92)
    tick_cell_objects(); // FUN_0044ea70, after the defeat checks (docs/re/31 section 3)
    auto_start_specials(); // FUN_004466e0, before FUN_00446690 (docs/re/35 section 2.1)
    tick_shake();   // FUN_00446690, after FUN_00453b90 / FUN_00455480 in the tick block
    run_orders();   // FUN_0044aca0 starts with the due orders (docs/re/49 section 2)
    // FUN_00458e90: side by side, the units, buildings, bullets and
    // explosions of side 0, then of side 1, ... (verified, docs/re/43
    // section 5). Each list is walked like the exe's linked list, whose
    // next pointer is read before each update: an object appended during
    // the pass (a drop, a conversion, a bullet, debris) is updated in the
    // same pass, unless the object being updated was the last one when its
    // update began. The pools hold at most kPoolSize objects per side, so
    // these reserves keep every reference valid during the tick.
    units_.reserve(size_t(8 * kPoolSize));
    bullets.reserve(size_t(8 * kPoolSize));
    effects.reserve(size_t(8 * kPoolSize));
    auto update_unit = [&](size_t ui) {
        Unit& u = units_[ui];
        auto tick_2b = [this](Unit& v) {
            if (v.flags & 2) {
                v.counter_2b = 0;
                v.flags &= ~2u;
            } else if ((tick_count_ & 7) == (uint32_t(v.slot) & 7) && v.counter_2b < 255) {
                ++v.counter_2b;
            }
        };
        const auto& r = templates_.unit_records[size_t(u.record)];
        // The head of FUN_00497b90 runs for every unit, aircraft, worms and
        // dying units included, in this order (verified
        // 2026-10-16); then the state switch.
        // Infantry only (the rec+0x11 test at the top): the crush test
        // (flag 0x1000000, FUN_0049fa50), then the animation tick
        // FUN_004940d0 (0x497c33). A crushed soldier goes on through the
        // head and the state-0x11 case in this same update.
        if (r.infantry) {
            crush_check(u);
            tick_infantry_anim(u);
        }
        // Self-repair (0x497c3b, docs/re/37 section 1.5): units with flag
        // 0x800000 (from the record: harvester, MCV, Devastator, carryalls)
        // regain 1 hp every 4 ticks while below half their maximum.
        if ((u.flags & 0x800000) && u.hit_points * 2 < templates_.unit_records[size_t(u.record)].hit_points &&
            (tick_count_ & 3) == 0) {
            ++u.hit_points;
        }
        if (r.kind == 4 && (tick_count_ & 0xf) == 0) {
            // The saboteur's stealth charge (+0x29, docs/re/32 section 1.9).
            if (!(u.flags & 0x10)) {
                if (u.load < 0xa0) ++u.load;
            } else if (u.load == 0) {
                if (u.flags & 4) {
                    u.flags &= ~0x10u;
                    uncloak(u);
                }
            } else {
                u.load = uint8_t(u.load < 4 ? 0 : u.load - 4);
            }
        }
        if (u.reload) --u.reload; // +0x1c (doc 18 section 3.7)
        tick_stealth(u);          // FUN_0049f610: also clears flag 4 of a unit that cannot cloak
        // The attacker (+0x30 / +0x32): dropped (+0x32 = 0xFFFF,
        // the side stays) when that unit or building is in state 0x11. A
        // freed slot has type 0, so the exe only logs "Attacked by strange
        // type" and keeps it: a removed attacker is kept here too.
        if (u.attacker != -1) {
            if (u.attacker <= -1000) {
                const size_t bi = size_t(-1000 - u.attacker);
                if (bi < buildings_.size() && !buildings_[bi].removed && buildings_[bi].dying) u.attacker = -1;
            } else if (const int ai = find_unit(u.attacker); ai >= 0 && units_[size_t(ai)].state == 0x11) {
                u.attacker = -1;
            }
        }
        // Then every unit but the carryall checks its target (0x497d93).
        if (r.kind != 2 && u.target_kind != 0 && !target_valid(u)) u.target_kind = 0;
        // Damage smoke (0x497e81, docs/re/43 section 2): every non-infantry
        // unit with a timer, aircraft and dying ones too. Above half hp the
        // timer is cleared; else one rand() for the puff test, two more
        // (y, then x) for a puff of EX_SMOKE3 at the unit's height.
        if (u.smoke != 0 && !r.infantry) {
            if (u.hit_points > int(uint32_t(r.hit_points) >> 1)) {
                u.smoke = 0;
            } else {
                const uint32_t r0 = rng_.next();
                if ((((r0 & 1) + tick_count_) & 3) == 0) {
                    const int32_t y = u.y / 65536 + int32_t(rng_.next() & 3) - 2;
                    const int32_t x = u.x / 65536 + int32_t(rng_.next() & 7) - 4;
                    const auto* ids = templates_.block(0x6d60d8);
                    if (ids && ids->bytes.size() > 59) spawn_effect_at(ids->bytes[59], x, y, u.owner, 0, 0, 0, u.altitude);
                }
                if ((tick_count_ & 3) == 0) --u.smoke;
            }
        }
        // +0x2c: every 8th tick (not aircraft), +1 while the unit's pixel
        // position is in its current cell, else 0.
        if ((tick_count_ & 7) == (uint32_t(u.slot) & 7) && !(u.flags & 0x40)) {
            if (u.cur_x == (u.x >> 16) >> 5 && u.cur_y == (u.y >> 16) >> 5) {
                if (u.since_stop < 255) ++u.since_stop;
            } else {
                u.since_stop = 0;
            }
        }
        // +0x2a: 0 when hit (flag 1), else +1 every 8th tick (index slot)
        // unless carried (FUN_00497b90).
        if (u.flags & 1) {
            u.hit_age = 0;
            u.flags &= ~1u; // flag 1 (hit this tick) is cleared here
            // A hit costs a deviated unit 30 steps of its timer (docs/re/40 section 3).
            if (deviation_runs(u)) u.delivery_entry = uint8_t(u.delivery_entry < 30 ? 0 : u.delivery_entry - 30);
        } else if ((tick_count_ & 7) == (uint32_t(u.slot) & 7) && !(u.flags & 0x100)) {
            if (u.hit_age < 255) ++u.hit_age;
            if (u.hit_age > 100) u.flags &= ~8u; // 0x498034: DoProtection may trigger again (doc 29)
            if (deviation_runs(u)) {
                if (u.delivery_entry != 0) {
                    --u.delivery_entry;
                } else if (!on_building_cell(u) && !(u.flags & 0x400000)) {
                    // The revert: back to the original side, flag 0x80 for its AI.
                    u.flags = (u.flags & ~0x1000u) | 0x80;
                    if (u.owner != u.orig_side) {
                        const int id = u.index;
                        convert_unit(u.owner, u.orig_side, id, false); // reserve(n + 16) above keeps `u` valid
                    }
                }
            }
        }
        tick_2b(u); // +0x2b and flag 2 (0x49808f)
        if (u.flags & kAircraft) {
            tick_aircraft(u);
            return;
        }
        if (u.state == 0x11) { // removal countdown (switch case 0xF, doc 14 section 3.3)
            // GRENDEATH (0x499de2, docs/re/37 section 3.5): a unit whose art
            // name is "Grenadier" blasts its cell when +0x2f is 0x2e, i.e. in
            // its 5th update here; bullet 0x21, own side, firer 0xFFFF.
            if (u.countdown == 0x2e && art_name_is(u.record, "Grenadier") && templates_.bullets.size() > 0x21) {
                damage_at(u.cur_x * 32, u.cur_y * 32, 0, 0x21, u.owner, -1);
            }
            // The zero test comes before the decrement (0x499e31 / 0x49a09a):
            // a unit stays countdown + 1 updates in state 0x11.
            if (u.countdown == 0) {
                if (!u.remove) {
                    if (!(u.flags & 0x400000)) { // 0x400000: already off the map cells
                        vacate(u, u.cur_x, u.cur_y, u.slot_cur);
                        if (u.flags & kStepReserved) vacate(u, u.to_x, u.to_y, u.slot_next);
                    }
                    u.remove = true;
                }
            } else {
                --u.countdown;
            }
            return;
        }
        if (r.kind == 5) {
            tick_worm(u);
            return;
        }
        bool handled = false;
        if (u.state == state::kIdle || (u.state >= 3 && u.state <= 6)) {
            if (!(r.kind == 1 && u.state == state::kIdle)) {
                handled = u.state != state::kIdle; // the attack states move by themselves
                tick_combat_state(u);
            }
        }
        const bool harvester = r.kind == 1;
        if (handled) {
            // Moved (or not) by the combat state this tick.
        } else if (u.state == state::kMoveAttack && !(u.flags & kAircraft)) {
            // State 8 (factory exit): once off building cells and between
            // steps, a harvester harvests (0xc), others idle (doc 13).
            // FUN_00497b90 case 6: a unit of our own or an ally standing on
            // the exit cell is asked to move aside (FUN_00497af0); a hostile
            // one is driven onto when the cell is passable (crushers).
            const int dx = u.dest_x, dy = u.dest_y;
            const int vi = unit_at_cell(dx, dy);
            bool go = vi < 0 || &units_[size_t(vi)] == &u;
            if (!go) {
                const Unit& v = units_[size_t(vi)];
                if (v.owner == u.owner) {
                    go = templates_.unit_records[size_t(v.record)].infantry && r.infantry &&
                         (free_slot(dx, dy, u.owner, 4) >= 0 || (u.to_x == dx && u.to_y == dy));
                } else if (alliance_[size_t(u.owner)][size_t(v.owner)] != 0 && passable(dx, dy, u, 0)) {
                    go = true;
                }
                if (!go && alliance_[size_t(u.owner)][size_t(v.owner)] == 0) ask_to_scatter(u, dx, dy);
            }
            // FUN_00495560(u, side, 1, 1), MoveUnitSimple (0x4990af): straight
            // cell steps toward the exit cell, no route, the record speed.
            if (go && move_simple(u, true, true) == 0) set_state(u, harvester ? 0xc : state::kIdle);
            // The FUN_0049d280 tail, every time: between steps and off
            // building cells (current or reserved), the state ends.
            if (u.state == state::kMoveAttack && u.steps == 0 && !on_building_cell(u)) {
                set_state(u, harvester ? 0xc : state::kIdle);
            }
        } else if (u.state == 9 && !(u.flags & kAircraft)) {
            tick_scatter(u);
        } else if (u.state >= 0x18 && u.state <= 0x1a && !(u.flags & kAircraft)) {
            tick_repair(u);
        } else if (u.state == 0x12 && !(u.flags & kAircraft)) {
            tick_waiting(u); // waiting for a carryall
        } else if (u.state == 0x13) {
            // carried: the carryall moves it
        } else if (u.state == 0x1e && !(u.flags & kAircraft)) {
            tick_deploy(u);
        } else if (u.state == 0x20 && !(u.flags & kAircraft)) {
            tick_thump(u);
        } else if (u.state == 0x1f && !(u.flags & kAircraft)) {
            tick_self_destruct(u);
        } else if (u.state == 0x17 && !(u.flags & kAircraft)) {
            tick_enter(u);
        } else if (u.state == 0xa && !(u.flags & kAircraft)) {
            tick_attack_blocker(u);
        } else if (u.state == state::kMove && !(u.flags & kAircraft)) {
            // The auto-fire while moving runs inside move_step (0x4943f3).
            if (move_step(u) == 0) {
                const int s = (u.next_state == 1 || u.next_state == 7 || u.next_state == 0x12) ? state::kIdle
                                                                                             : u.next_state;
                set_state(u, s);
            }
        }
        if (harvester && u.state >= 0xb && u.state <= 0x10) tick_harvester(u);
        // Idle harvesters harvest only when standing on spice (doc 13 section 1.7).
        if (harvester && u.state == state::kIdle && (map_.at(u.cur_x, u.cur_y).flags & cell::kSpiceLevelMask)) {
            set_state(u, 0xc);
        }
    };
    for (int p = 0; p < 8; ++p) {
        for (size_t ui = 0; ui < units_.size(); ++ui) {
            if (units_[ui].owner != p || units_[ui].remove) continue;
            bool more = false; // a later unit of this side is linked now
            for (size_t j = units_.size(); j-- > ui + 1 && !more;) more = units_[j].owner == p && !units_[j].remove;
            update_unit(ui);
            // The update ended the countdown: FUN_0046bdb0 frees the slot at
            // once, so the side's next object takes it (docs/re/50 section 5).
            if (units_[ui].remove) pool_push(p, units_[ui].slot);
            if (!more) break;
        }
        tick_buildings(p);
        tick_bullets(p);
        tick_effects(p);
    }
    // FUN_0044a9f0, called by FUN_0044aca0 (0x449ad8) right after the object
    // pass FUN_00458e90 and before the power FUN_0044aae0, the storage
    // FUN_0044abe0 and the production FUN_004467e0 (0x449af0); it uses no rand().
    do_repairs();
    tick_economy(); // power, storage, the unit reveal (0x449aeb), production
    // The Starport prices / stock, then the deliveries, for one active
    // player per tick: tick & 7 (FUN_00448550, 0x449b0b).
    if (player_active(int(tick_count_ & 7))) {
        request_monitor(int(tick_count_ & 7)); // FUN_0046d180
        tick_starport(int(tick_count_ & 7));
        tick_deliveries(int(tick_count_ & 7));
    }
    assign_places(); // FUN_0044c520 at 0x449b28, after that block (docs/re/56 section 2)
    // FUN_0045d4f0 at 0x44a057, at the end of the tick block; it touches only
    // the particles and draws no rand(), so its place has no other effect.
    tick_particles();
    for (const auto& u : units_) {
        if (u.remove) carryall_removed(u);
    }
    units_.erase(std::remove_if(units_.begin(), units_.end(), [](const Unit& u) { return u.remove; }),
                 units_.end());
}

} // namespace d2k::sim
