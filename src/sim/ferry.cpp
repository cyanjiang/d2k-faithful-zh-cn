// The carryall ferry: a player's carryalls lift its own vehicles
// (docs/re/33-carryall-ferry.md). No rand() anywhere.

#include "sim/world.hpp"

#include <algorithm>

namespace d2k::sim {

namespace {
constexpr uint32_t kAircraft = 0x40, kLoaded = 0x100, kJob = 0x200, kDelivery = 0x800, kLeaving = 0x4000;
constexpr uint32_t kStepReserved = 0x10000000;
constexpr int32_t kPx = 0x10000;
constexpr int32_t kAltTop = 60 * kPx, kAltDrop = 10 * kPx;
} // namespace

void World::launch_carryall(Unit& u, int x1, int y1, int x2, int y2) {
    // FUN_00455870, kind 2 (docs/re/33 section 3.1): 4 cells past the map
    // edge nearest (x2, y2), flying to (x1, y1) at 60 px.
    auto& pl = players_[size_t(u.owner)];
    ++pl.carry_owned;
    u.altitude = kAltTop;
    u.flags |= kAircraft;
    pl.carry_count = uint8_t(std::min(pl.carry_count + 1, 32));
    ++pl.carry_live;
    int ex = x2, ey = y2;
    const int f = edge_point(ex, ey, 0);
    u.facing = u.turret_facing = uint8_t(f);
    u.x = int32_t(int16_t(ex)) * (32 * kPx); // the cell corner, signed
    u.y = int32_t(int16_t(ey)) * (32 * kPx);
    u.cur_x = u.to_x = u.home_x = uint8_t(ex);
    u.cur_y = u.to_y = u.home_y = uint8_t(ey);
    u.dest_x = uint8_t(x1);
    u.dest_y = uint8_t(y1);
    u.state = state::kIdle;
    set_state(u, state::kMove);
}

void World::carryall_removed(const Unit& u) {
    // FUN_00458e90 at removal: owned (not delivery ones) and live counts.
    if (templates_.unit_records[size_t(u.record)].kind != 2) return;
    auto& pl = players_[size_t(u.owner)];
    if (!(u.flags & kDelivery) && pl.carry_owned > 0) --pl.carry_owned;
    if (pl.carry_live > 0) --pl.carry_live;
}

int World::find_carry_slot(int p, const Unit& u) const {
    // FUN_0046cca0 "FindCarryallSlot".
    const auto& pl = players_[size_t(p)];
    if (u.state == 0x12) return 0; // (low byte 0, not 0xFF; unreachable)
    if (pl.carry_owned == 0) return 0xff;
    for (int i = 0; i < pl.carry_count; ++i) {
        if (pl.carry[size_t(i)].state == 0) return i;
    }
    return 0xff;
}

bool World::add_to_queue(int p, const Unit& u, int s, int kind, int saved) {
    // FUN_0046cd00 "AddToQueue".
    auto& pl = players_[size_t(p)];
    for (int i = 0; i < pl.carry_count; ++i) {
        auto& e = pl.carry[size_t(i)];
        if (e.unit == u.index && e.state != 0) {
            if (e.state != 1) return false;
            e.state = 0; // an older waiting request is dropped
        }
    }
    auto& e = pl.carry[size_t(s & 31)];
    e.state = 1;
    e.saved = uint32_t(saved);
    e.age = 0;
    e.from_x = u.cur_x;
    e.from_y = u.cur_y;
    e.to_x = u.dest_x;
    e.to_y = u.dest_y;
    e.unit = u.index;
    e.kind = uint8_t(kind);
    return true;
}

int World::cell_dist2(const Unit& u, int x, int y) const {
    // FUN_00421800: the cell of the pixel position (C signed divisions).
    const int cx = int((u.x / 32) / 65536), cy = int((u.y / 32) / 65536);
    return (cx - int(uint8_t(x))) * (cx - int(uint8_t(x))) + (cy - int(uint8_t(y))) * (cy - int(uint8_t(y)));
}

int World::find_request_for(int p, const Unit& c) const {
    // FUN_0046ce00 + FUN_0046ce70: the first waiting slot for which no other
    // idle ferry carryall is strictly nearer to its "from" cell.
    const auto& pl = players_[size_t(p)];
    for (int i = 0; i < pl.carry_count; ++i) {
        const auto& e = pl.carry[size_t(i)];
        if (e.state != 1) continue;
        const uint32_t d = uint32_t(cell_dist2(c, e.from_x, e.from_y));
        bool closest = true;
        for (const auto& v : units_) {
            if (v.owner != p || v.remove || templates_.unit_records[size_t(v.record)].kind != 2) continue;
            if (v.state != state::kIdle || (v.flags & kDelivery)) continue;
            if (uint32_t(cell_dist2(v, e.from_x, e.from_y)) < d) {
                closest = false;
                break;
            }
        }
        if (closest) return i;
    }
    return 0xff;
}

void World::request_monitor(int p) {
    // FUN_0046d180 "RequestMonitor", for player tick & 7 after the units.
    auto& pl = players_[size_t(p)];
    const int n = pl.carry_count;
    if (n > 0 && pl.carry_live < n && pl.carry[size_t(n - 1)].state == 0) pl.carry_count = uint8_t(n - 1);
    for (int i = 0; i < pl.carry_count; ++i) {
        auto& s = pl.carry[size_t(i)];
        if (s.state == 3) s.state = 0;
        if (s.state == 0) continue;
        const int ui = find_unit(s.unit);
        const int st = ui >= 0 ? units_[size_t(ui)].state : 0x11;
        if (st == 0x11) {
            s.state = 3;
            if (ui >= 0) set_state(units_[size_t(ui)], int(s.saved)); // refused, but its side effects run
        } else if (st != 0x12 && st != 0x13) {
            s.state = 3;
        }
        if (s.state == 1 || s.state == 2) {
            ++s.age;
            if (s.age > 79) { // 640 ticks
                if (ui >= 0) set_state(units_[size_t(ui)], int(s.saved));
                s.state = 3;
            }
        }
    }
}

void World::carry_request(Unit& u, int threshold) {
    // The lift request (0x4999dd / 0x49a781, docs/re/33 section 2.1): far
    // from the destination, between steps, the "to" cell not reserved.
    const int dx = int(u.cur_x) - int(u.dest_x), dy = int(u.cur_y) - int(u.dest_y);
    if (dx * dx + dy * dy <= threshold || u.steps != 0 || (u.flags & kStepReserved)) return;
    const int s = find_carry_slot(u.owner, u);
    if (s == 0xff) return;
    if (add_to_queue(u.owner, u, s, 100, u.state)) set_state(u, 0x12);
}

void World::tick_waiting(Unit& u) {
    // State 0x12 (FUN_00497b90 case 0x10): back to the follow-up state when
    // no slot holds this unit at its cell; the 8-tick fire check.
    const auto& pl = players_[size_t(u.owner)];
    bool found = false;
    for (int i = 0; i < pl.carry_count; ++i) {
        const auto& e = pl.carry[size_t(i)];
        if (e.unit == u.index && e.state != 0 && e.from_x == u.cur_x && e.from_y == u.cur_y) found = true;
    }
    if (!found) set_state(u, state::kIdle);
    u.since_stop = 0; // +0x2c
    if (is_stealthed(u)) uncloak(u); // docs/re/32 section 1.4
    if (((uint32_t(u.slot) & 7) == (tick_count_ & 7) || u.target_kind != 0) && !is_stealthed(u)) auto_fire(u);
}

bool World::ferry_idle(Unit& u) {
    // State 2, ferry branch (kind 2 or 8 without 0x800, docs/re/33 section
    // 3.2). Returns true when it handled the update.
    auto& pl = players_[size_t(u.owner)];
    if (u.flags & kLoaded) {
        const auto& e = pl.carry[size_t(u.carry_slot & 31)];
        if (u.to_x == e.to_x && u.to_y == e.to_y) {
            set_state(u, 0x15);
        } else {
            u.flags &= ~kJob;
            u.dest_x = e.to_x;
            u.dest_y = e.to_y;
            set_state(u, state::kMove);
        }
        return true;
    }
    if (u.flags & kJob) { // arrived over the unit
        set_state(u, 0x15);
        return true;
    }
    const int s = find_request_for(u.owner, u);
    if (s == 0xff) {
        if ((tick_count_ & 7) == (uint32_t(u.slot) & 7)) u.turret_facing = uint8_t((u.turret_facing + 1) & 31);
        flight_step(u, 1); // the slow circle (doc 14 section 1.7)
        return true;
    }
    auto& e = pl.carry[size_t(s)];
    u.dest_x = e.from_x;
    u.dest_y = e.from_y;
    u.carry_unit = e.unit;
    u.carry_saved = uint8_t(e.saved);
    u.carry_slot = int8_t(s);
    e.state = 2;
    e.carryall = u.index;
    set_state(u, state::kMove);
    u.flags |= kJob;
    return true;
}

bool World::ferry_job_cancelled(Unit& u) {
    // State 7 (docs/re/33 section 3.3): the slot was marked done.
    if (!(u.flags & kJob) || (u.flags & kDelivery)) return false;
    const auto& e = players_[size_t(u.owner)].carry[size_t(u.carry_slot & 31)];
    if (e.state != 3 || (u.flags & kLoaded)) return false;
    u.flags &= ~kJob;
    set_state(u, state::kIdle);
    return true;
}

void World::ferry_descend(Unit& u) {
    // State 0x15, ferry branch (docs/re/33 sections 3.4 - 3.6).
    auto& pl = players_[size_t(u.owner)];
    auto& e = pl.carry[size_t(u.carry_slot & 31)];
    if (e.state == 3 && !(u.flags & kLoaded)) {
        u.flags &= ~kJob;
        set_state(u, 0x16);
        return;
    }
    if (u.flags & kLoaded) { // the drop cell, every tick, with the carryall's own test
        if (!passable(u.dest_x, u.dest_y, u, 0)) {
            int x = u.dest_x, y = u.dest_y;
            spiral_free_cell(x, y, 8);
            u.dest_x = uint8_t(x);
            u.dest_y = uint8_t(y);
            e.to_x = u.dest_x;
            e.to_y = u.dest_y;
            set_state(u, 0x16);
            return;
        }
    }
    const int ti = find_unit(u.carry_unit);
    if (u.altitude == kAltTop && !(u.flags & kLoaded) && ti >= 0) {
        const uint8_t want = units_[size_t(ti)].facing;
        if (u.facing != want) { // TurnAir (FUN_004954b0): 1 only when already facing
            turn_air(u, want);
            return;
        }
    }
    if (u.altitude > kAltDrop) {
        u.altitude -= kPx;
        return;
    }
    if (!(u.flags & kLoaded)) {
        // PickUp: only a unit still waiting (state 0x12), not carried.
        Unit* t = ti >= 0 ? &units_[size_t(ti)] : nullptr;
        if (t && !(t->flags & kLoaded) && t->state == 0x12) {
            auto& c = map_.at(t->cur_x, t->cur_y).flags;
            c &= ~8u;
            if (c & 0x3e0u) c = (c & ~7u) | ((c >> 25) & 7u);
            u.flags |= kLoaded;
            t->selected = false;
            t->flags |= kLoaded;
            set_state(*t, 0x13);
            e.state = 4;
            set_state(u, 0x16);
        } else {
            u.flags = (u.flags & ~kJob) | kLeaving; // gone, moved on or carried: give up
            set_state(u, 0x16);
        }
        return;
    }
    // Drop.
    if (ti >= 0) {
        Unit& t = units_[size_t(ti)];
        t.flags &= ~kLoaded;
        t.altitude = 0;
        set_state(t, u.carry_saved);
        auto& c = map_.at(t.cur_x, t.cur_y).flags;
        c = (c & ~7u) | uint32_t(u.owner) | 8u;
        if (templates_.unit_records[size_t(t.record)].infantry) { // dead in 1.06
            auto& cc = map_.at(u.cur_x, u.cur_y).flags;
            cc = (cc & 0xf1ffffffu) | (uint32_t(u.owner) << 25);
        }
    }
    e.state = 0; // freed directly
    u.flags &= ~kLoaded;
    set_state(u, 0x16);
}

void World::carry_tail(Unit& u) {
    // The tail of the unit update: the cargo follows its carryall, 10 px lower.
    const int ti = find_unit(u.carry_unit);
    if (ti < 0) return;
    Unit& t = units_[size_t(ti)];
    if (!(t.flags & kLoaded)) return;
    t.to_x = u.to_x;
    t.to_y = u.to_y;
    t.cur_x = u.cur_x;
    t.cur_y = u.cur_y;
    t.x = u.x;
    t.steps = 0;
    t.y = u.y;
    t.altitude = u.altitude - kAltDrop;
    t.facing = u.facing;
    t.turret_facing = u.turret_facing;
}

void World::carryall_killed(Unit& u) {
    // KillUnit for a carryall (docs/re/33 section 4.1, steps 2 and 4).
    if ((u.flags & 0x140) == 0x140 && u.carry_unit >= 0) {
        const int ti = find_unit(u.carry_unit);
        if (ti >= 0 && units_[size_t(ti)].state != 0x11) kill_unit(units_[size_t(ti)]);
    }
    auto& pl = players_[size_t(u.owner)];
    for (int i = 0; i < pl.carry_count; ++i) {
        auto& e = pl.carry[size_t(i)];
        if (e.state != 2 || e.carryall != u.index) continue;
        const int wi = find_unit(e.unit);
        if (wi >= 0) set_state(units_[size_t(wi)], int(e.saved));
        e.state = 3;
    }
}

} // namespace d2k::sim
