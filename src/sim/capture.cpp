// MCV deploy, engineers and saboteurs (docs/re/30-mcv-engineers.md).

#include "sim/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace d2k::sim {

namespace {
constexpr uint32_t kStepReserved = 0x10000000;
constexpr uint32_t kHidden = 0x200000, kGone = 0x400000;
constexpr int kSubpos[5][2] = {{16, 21}, {23, 14}, {23, 26}, {7, 26}, {7, 14}}; // 0x4d7028
constexpr int kDir[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
} // namespace

// ---- MCV deploy ---------------------------------------------------------------

bool World::can_deploy(int x, int y) const {
    // FUN_00443250: the 3 x 3 block around (x, y) inside the map (row 0
    // fails: a pointer compare against &rowtab[1]), buildable and free; the
    // centre cell only needs the bounds.
    for (int row = y - 1; row <= y + 1; ++row) {
        for (int col = x - 1; col <= x + 1; ++col) {
            if (col < 0 || col >= map_.width()) return false;
            if (row < 1 || row >= map_.height()) return false;
            const uint32_t f = map_.at(col, row).flags;
            if ((!(f & 0x8000) || (f & 0x3f8)) && !(col == x && row == y)) return false;
        }
    }
    return true;
}

bool World::takes_orders_forced(const Unit& u) const {
    // FUN_0049f4a0(u, 1): state 0x12 is allowed.
    return u.state != state::kMoveAttack && u.state != 0x14 && !(u.flags & 0x400140);
}

void World::order_deploy(int p, int unit_id) {
    // Order 0x2F, ProcessOrder case 0x1C (FUN_00457810).
    const int i = find_unit(unit_id);
    if (i < 0) return;
    Unit& u = units_[size_t(i)];
    if (u.owner != p || u.state == 0x11 || u.remove || !takes_orders_forced(u)) return;
    switch (templates_.unit_records[size_t(u.record)].kind) {
    case 6: // MCV
        if (set_state(u, 0x1E)) u.next_state = 1;
        break;
    case 4: // saboteur: the stealth ability (docs/re/32)
        if (!(u.flags & 0x10)) {
            u.flags |= 0x10;
            sounds.push_back({"S_STEALTHUP", u.to_x, u.to_y, false});
        }
        break;
    case 7: // Devastator: the self-destruct countdown (docs/re/37 section 1.3)
        if (set_state(u, 0x1F)) {
            u.next_state = 1;
            u.explode_delay = vars.devastator_explode_delay;
        }
        break;
    case 0xe: // Thumper: thumping, not sticky (docs/re/54 section 4.3)
        if (set_state(u, 0x20)) u.next_state = 1;
        break;
    default:
        break;
    }
}

void World::tick_thump(Unit& u) {
    // FUN_00497b90 state 0x20 (0x49b45f, docs/re/54 section 4.4), after the
    // infantry animation tick: face direction 8, mode 8 (frames 7..11), hold
    // frame 8 for 60 updates (+0x29), and a puff (EX_THPUFF) on frame 7.
    if (u.steps != 0) { // finish the current step first
        u.dest_x = u.home_x;
        u.dest_y = u.home_y;
        move_step(u);
        return;
    }
    if (turn_body(u, 8)) return;
    if (!templates_.unit_records[size_t(u.record)].infantry) return; // the exe's fatal "Invalid type for thumper"
    set_inf_anim(u, 8);
    const int first = 7; // InfModes[8].first
    if (u.frame == first + 1) {
        if (++u.load < 60) {
            u.timer = 0; // hold the frame
        } else {
            u.load = 0;
            u.timer = 3; // frame 9 next update
        }
    }
    if (u.frame == first) {
        const auto* ids = templates_.block(0x6d60d8);
        const int ex = ids && ids->bytes.size() > 0x4f ? ids->bytes[0x4f] : 0xff; // EX_THPUFF 0x6d6127
        if (ex != 0xff) spawn_effect_at(ex, u.x >> 16, u.y >> 16, u.owner, 0, 0, 0);
        // S_THUMPER when the thumper's pixel is in the view, its cell is not
        // shrouded and the sample is not playing already.
        const int px = u.x >> 16, py = u.y >> 16;
        if (px >= view_x && px < view_x + 480 && py >= view_y && py < view_y + 380 &&
            map_.at(u.cur_x, u.cur_y).state == 0) {
            SoundEvent s;
            s.name = "S_THUMPER";
            s.cx = u.cur_x;
            s.cy = u.cur_y;
            s.unique = true;
            sounds.push_back(s);
        }
    }
}

void World::tick_self_destruct(Unit& u) {
    // FUN_00497b90 case 0x1D (0x49b32d-0x49b45a, docs/re/37 section 1.4).
    if (u.steps != 0) { // finish the step first: no sparks, no countdown
        u.dest_x = u.home_x;
        u.dest_y = u.home_y;
        move_step(u);
        return;
    }
    const auto* ids = templates_.block(0x6d60d8);
    auto id = [&](size_t o) { return ids && ids->bytes.size() > o ? int(ids->bytes[o]) : -1; };
    if (rng_.next() % 3 == 0) { // line 0x182a
        const int k = int(rng_.next() % 3); // line 0x182b
        const int spark = id(0x51 + size_t(k)); // EX_DEVSPARK1..3 (0x6d6129)
        if (spark >= 0) spawn_effect_at(spark, u.x >> 16, u.y >> 16, u.owner, 0, 0, 0);
    }
    if (u.explode_delay != 0) {
        --u.explode_delay;
        return;
    }
    // Detonate: not a KillUnit (no flash, no death explosion, no voice, hp
    // kept), but the loss is counted (0x49b3f7, docs/re/60).
    set_state(u, 0x11);
    // +0x2F still holds CreateUnit's 10 (0x4559ba; the only other writer is
    // the state-0x11 decrement 0x49a09c), so this is the exe's value.
    u.countdown = 10;
    if (const int ex = id(0x54); ex >= 0) spawn_effect_at(ex, u.x >> 16, u.y >> 16, u.owner, 0, 0, 0); // EX_DEVDEATH
    count_unit_lost(u.owner, u.record);
    // Bullet DEVASTATOR (0x6d610b): 5000, W_PLASMA_DEATH, a ground hit.
    if (const int b = id(0x33); b >= 0) damage_at(u.x >> 16, u.y >> 16, 6, b, u.owner, u.index);
}

void World::tick_deploy(Unit& u) {
    // FUN_00497b90 case 0x1C (0x49b160-0x49b304).
    if (u.steps != 0) { // finish the step
        u.dest_x = u.home_x;
        u.dest_y = u.home_y;
        move_step(u);
        return;
    }
    if (turn_body(u, 0x1D)) return;
    const int house = players_[size_t(u.owner)].house;
    int yard = -1;
    for (size_t r = 0; r < templates_.building_records.size(); ++r) { // a plain first match
        const auto& br = templates_.building_records[r];
        if (((1u << house) & br.house_mask) && br.kind == 2) {
            yard = int(r);
            break;
        }
    }
    if (yard < 0) return; // "Cannot find correct construction yard"
    if (!can_deploy(u.cur_x, u.cur_y)) {
        u.state = state::kIdle; // a plain write, not SetState
        return;
    }
    map_.at(u.cur_x, u.cur_y).flags &= ~8u;
    const float ratio = float(double(u.hit_points) / double(templates_.unit_records[size_t(u.record)].hit_points));
    const int b = create_building(u.owner, yard, u.cur_x - 1, u.cur_y - 1, false);
    if (u.owner == local_player) sounds.push_back({"S_BUILDUP", u.cur_x, u.cur_y, false});
    if (b >= 0) {
        const uint32_t max = uint32_t(templates_.building_records[size_t(yard)].hit_points);
        buildings_[size_t(b)].hit_points = int32_t(double(max) * double(ratio));
    }
    // Removed silently: not drawn, no cell clean-up, no loss counted.
    u.flags |= kHidden | kGone;
    set_state(u, 0x11);
    u.countdown = 10;
}

void World::ai_mcvs(int s) {
    // FUN_00422140 "DoMCVs" (docs/re/30 section 1.6).
    const bool no_list = players_[size_t(s)].building_list.empty();
    if (!no_list) set_ai_u8(s, 0x19EC, 1);
    if (tick_count_ & 0x3F) return;
    for (size_t i = 0; i < units_.size(); ++i) {
        const Unit& u = units_[i];
        if (u.owner != s || u.remove || templates_.unit_records[size_t(u.record)].kind != 6) continue;
        if (u.state != state::kIdle || !takes_orders(u)) continue;
        const int cx = int((u.x / 32) / 65536), cy = int((u.y / 32) / 65536);
        if (!can_deploy(cx, cy)) {
            ai_deal_with_mcv(s, units_[i]); // FUN_00422ec0
            continue;
        }
        if (u.delivery_entry != 0 || no_list) order_deploy(s, u.index);
    }
}

bool World::ai_mcv_site(int s, int x, int y) const {
    // FUN_00423020 "CanIDeployMCVHere": 2 cells from every edge; the 3 x 3
    // block around (x, y) has flags 0xa000 and no other side's objects.
    if (x < 2 || y < 2 || x > map_.width() - 2 || y > map_.height() - 2) return false;
    for (int cy = y - 1; cy < y + 2; ++cy) {
        for (int cx = x - 1; cx < x + 2; ++cx) {
            const uint32_t f = map_.at(cx, cy).flags;
            if ((f & 0xa000) != 0xa000) return false;
            if ((f & 0x3f8) && int(f & 7) != s) return false;
        }
    }
    return true;
}

void World::ai_deal_with_mcv(int s, Unit& u) {
    // FUN_00422ec0 "DealWithMCV" (docs/re/30 section 1.6): the MCV's cell,
    // else a spiral out to leg 13 (step, test, then count down); a site
    // found away from the MCV gets a move order, the rectangle and the
    // rebuild flag. Units are then kicked out of the 3 x 3 box.
    static constexpr int kDx[4] = {-1, 0, 1, 0}, kDy[4] = {0, 1, 0, -1}, kGrow[4] = {0, 1, 0, 1};
    int x = int((u.x / 32) / 65536), y = int((u.y / 32) / 65536);
    if (!ai_mcv_site(s, x, y)) {
        int leg = 1, left = 1, dir = 0;
        for (;;) {
            x += kDx[dir];
            y += kDy[dir];
            if (ai_mcv_site(s, x, y)) break;
            if (--left == 0) {
                leg += kGrow[dir];
                dir = (dir + 1) & 3;
                left = leg;
            }
            if (leg > 13) return;
        }
        ai_clear_selection(s);
        if (u.state == 0x11) return;
        u.selected = true;
        set_ai_u32(s, 0xF2C, uint32_t(x - 1));
        set_ai_u32(s, 0xF34, uint32_t(x + 1));
        set_ai_u32(s, 0xF30, uint32_t(y - 1));
        set_ai_u32(s, 0xF38, uint32_t(y + 1));
        order_move(s, x, y); // FUN_00459450
        set_ai_u8(s, 0x1D49, 1);
    }
    ai_kick_units(s, (x - 1) & 0xff, (y - 1) & 0xff, (x + 1) & 0xff, (y + 1) & 0xff, u.index);
}

// ---- engineers and saboteurs ----------------------------------------------------

bool World::entry_cell(int px, int py, const Building& b, int& ex, int& ey) const {
    // FUN_004a47f0: the first footprint cell (bit order) within one cell of
    // the unit's cell.
    const auto& r = templates_.building_records[size_t(b.record)];
    const int bx = (b.x >> 16) / 32, by = ((b.y >> 16) - r.height_px) / 32;
    const int ux = px >> 5, uy = py >> 5;
    for (int k = 0; k < 16; ++k) {
        if (!(r.footprint & (1u << k))) continue;
        const int cx = bx + (k & 3), cy = by + (k >> 2);
        if (std::abs(ux - cx) < 2 && std::abs(uy - cy) < 2) {
            ex = cx;
            ey = cy;
            return true;
        }
    }
    return false;
}

int World::enter_building(Unit& u) {
    // FUN_00496d10 for kinds 3 / 4 (docs/re/30 section 2.3).
    const int kind = templates_.unit_records[size_t(u.record)].kind;
    if (u.target_kind != 2 || u.target < 0 || !building_alive(u.target) || buildings_[size_t(u.target)].dying) {
        if (u.steps) {
            move_step(u);
            return 1;
        }
        return 0;
    }
    Building& b = buildings_[size_t(u.target)];
    int ex, ey;
    if (entry_cell(u.x >> 16, u.y >> 16, b, ex, ey) && u.steps == 0) {
        if ((b.flags & 0x100000C) || (kind == 3 && !(b.flags & 0x400)) || (kind == 4 && !(b.flags & 0x200))) {
            set_state(u, state::kIdle);
            return 1;
        }
        // FUN_0044e120: off the map cells.
        vacate(u, u.cur_x, u.cur_y, u.slot_cur);
        if (u.flags & kStepReserved) vacate(u, u.to_x, u.to_y, u.slot_next);
        u.dest_x = uint8_t(ex);
        u.dest_y = uint8_t(ey);
        set_state(u, 0x17);
        u.flags |= kGone;
        b.flags |= 0x1000000; // one entrant at a time; also blocks selling
        uncloak(u); // 0x496f64
        return 1;
    }
    int cx, cy;
    closest_cell(u.target, (u.x >> 16) >> 5, (u.y >> 16) >> 5, cx, cy);
    u.dest_x = uint8_t(cx);
    u.dest_y = uint8_t(cy);
    move_step(u);
    return 1;
}

int World::move_simple(Unit& u, bool clear_old, bool reserve) {
    // FUN_00495560 "MoveUnitSimple": one straight step toward +0x48 / +0x49.
    // Returns 0 when there.
    const auto& r = templates_.unit_records[size_t(u.record)];
    if (u.steps == 0) {
        if (u.cur_x == u.dest_x && u.cur_y == u.dest_y) return 0;
        if (u.state != 0x17 && !passable(u.dest_x, u.dest_y, u, 0)) return 1;
        const int d = facing8(u.cur_x, u.cur_y, u.dest_x, u.dest_y) / 4; // FUN_0045e060
        u.to_x = uint8_t(u.cur_x + kDir[d][0]);
        u.to_y = uint8_t(u.cur_y + kDir[d][1]);
        auto& f = map_.at(u.to_x, u.to_y).flags;
        uint32_t per = uint32_t(r.speed);
        if (!r.infantry) {
            u.step_x = int32_t(u.to_x * 32 + 16) << 16;
            u.step_y = int32_t(u.to_y * 32 + 16) << 16;
        } else {
            int slot = u.slot_cur;
            if (f & 0x3f8 & (0x20u << slot)) { // the first free sub-slot from 4 down
                uint32_t bit = 0x200;
                slot = 4;
                while ((f & 0x3f8 & bit) && bit >= 0x20) {
                    bit >>= 1;
                    --slot;
                }
                slot = std::max(slot, 0); // "Can't find a free slot for a soldier"
            }
            u.step_x = int32_t(u.to_x * 32 + kSubpos[slot][0]) << 16;
            u.step_y = int32_t(u.to_y * 32 + kSubpos[slot][1]) << 16;
            u.slot_next = uint8_t(slot);
            per = uint32_t(u.speed);
        }
        const int32_t dist = std::max(std::abs(u.x - u.step_x), std::abs(u.y - u.step_y));
        u.steps = uint8_t(uint32_t(dist) / std::max<uint32_t>(per, 1));
        u.half = uint8_t(u.steps >> 1);
        if (reserve) {
            if (!r.infantry) {
                f = (f & ~7u) | uint32_t(u.owner) | 8u;
            } else {
                f = (f & ~7u) | (0x20u << u.slot_next) | uint32_t(u.owner);
                f = (f & 0xf1ffffffu) | (uint32_t(u.owner) << 25);
            }
            u.flags |= kStepReserved;
        }
    }
    const int dir = (u.cur_x == u.to_x && u.cur_y == u.to_y) ? u.facing : facing8(u.cur_x, u.cur_y, u.to_x, u.to_y);
    if (u.facing != dir) {
        turn_body(u, dir);
        if (r.has_turret) turn_turret(u, dir);
        return 1;
    }
    if (r.infantry) set_inf_anim(u, 1);
    const int32_t s = r.speed, h = s / 2;
    if ((u.x >> 16) < ((u.step_x - h) >> 16)) u.x += s;
    else if ((u.x >> 16) > ((u.step_x + h) >> 16)) u.x -= s;
    if ((u.y >> 16) < ((u.step_y - h) >> 16)) u.y += s;
    else if ((u.y >> 16) > ((u.step_y + h) >> 16)) u.y -= s;
    if (r.has_turret) turn_turret(u, dir);
    --u.steps; // u8: a 0-step move wraps (as the exe)
    if (u.steps < u.half) {
        u.half = 0;
        if (clear_old) {
            vacate(u, u.cur_x, u.cur_y, u.slot_cur);
            u.slot_cur = u.slot_next;
        }
        u.cur_x = u.to_x;
        u.cur_y = u.to_y;
        u.flags &= ~kStepReserved;
    }
    return 1;
}

void World::make_hostile(int a, int b) {
    // FUN_0049f870.
    alliance_[size_t(a)][size_t(b)] = 1;
    alliance_[size_t(b)][size_t(a)] = 1;
    if (b == local_player) {
        Message m;
        m.key = "NeutralBecomeHostile" + std::to_string(players_[size_t(a)].house);
        messages.insert(messages.begin(), m);
        if (messages.size() > 5) messages.resize(5);
    }
}

void World::tick_enter(Unit& u) {
    // FUN_00497b90 case 0x15 (docs/re/30 section 2.4).
    if (move_simple(u, false, false)) return;
    const int side = u.owner;
    const int kind = templates_.unit_records[size_t(u.record)].kind;
    // FUN_004a4740: any building object still in the slot, a destroyed one
    // counting down too (BuildingDestroyed FUN_0049ca10 sets state 0x11 and
    // +0x3b = 0x1e, not flag 4 / 8). Such a building is captured as well;
    // the copy keeps state 0x11 (capture_building).
    if (u.target_kind == 2 && u.target >= 0 && size_t(u.target) < buildings_.size() &&
        !buildings_[size_t(u.target)].removed && !(buildings_[size_t(u.target)].flags & 0xC)) {
        const int bi = u.target;
        const int victim = buildings_[size_t(bi)].owner;
        if (kind == 3) {
            if (capture_building(victim, side, bi)) {
                // 0x49a218 (docs/re/60): the built counter +0x24784 is not kept.
                ++players_[size_t(victim)].buildings_lost;
                count_building_killed(side, buildings_[size_t(bi)].record, victim);
                if (alliance_[size_t(victim)][size_t(side)] == 2) make_hostile(victim, side);
                if (side == local_player) {
                    voice(side, "CAPTURE");
                } else if (victim == local_player) {
                    voice(victim, "BUILDINGLOST");
                    Message m;
                    m.key = "BuildingEngineered";
                    messages.insert(messages.begin(), m);
                    if (messages.size() > 5) messages.resize(5);
                }
            }
        } else {
            ++players_[size_t(victim)].buildings_lost; // 0x49a365
            count_building_killed(side, buildings_[size_t(bi)].record, victim);
            if (alliance_[size_t(victim)][size_t(side)] == 2) make_hostile(victim, side);
            destroy_building(bi);
            if (victim == local_player) voice(victim, "BUILDINGLOST");
        }
    }
    u.flags |= kHidden;
    set_inf_anim(u, 0);
    count_unit_lost(side, u.record); // 0x49a45f: the entrant counts as lost, no kill
    kill_unit(u);
}

int World::convert_unit(int from, int to, int unit_id, bool keep_state) {
    // FUN_0046d2e0. Returns the new unit's id, or -1.
    const int oi = find_unit(unit_id);
    if (oi < 0) return -1;
    if (units_[size_t(oi)].owner != from || (units_[size_t(oi)].flags & kGone)) return -1;
    {
        Unit& o = units_[size_t(oi)];
        if (templates_.unit_records[size_t(o.record)].infantry) { // removed, not converted
            vacate(o, o.cur_x, o.cur_y, o.slot_cur);
            if (o.flags & kStepReserved) vacate(o, o.to_x, o.to_y, o.slot_next);
            o.flags |= kHidden | kGone;
            set_state(o, 0x11);
            o.countdown = 0x32;
            return -1;
        }
        vacate(o, o.cur_x, o.cur_y, 0);
        if (o.flags & kStepReserved) vacate(o, o.to_x, o.to_y, 0);
    }
    const Unit old = units_[size_t(oi)];
    const int ni = create_unit(to, old.record, old.cur_x, old.cur_y, old.cur_x, old.cur_y);
    if (ni < 0) return -1;
    Unit& c = units_[size_t(ni)];
    const int id = c.index;
    const uint16_t slot = c.slot; // +0x8c is past the copied 0x7C bytes
    const int house = c.house;
    c = old; // the 0x7C bytes
    c.index = id;
    c.slot = slot;
    c.owner = to;
    c.house = house;
    c.remove = false;
    c.selected = false;
    c.attacker_side = -1;
    c.attacker = -1;
    c.hit_age = 0xff;
    c.target_kind = 0;
    c.target = -1;
    c.next_state = 2;
    c.dest_x = c.to_x;
    c.dest_y = c.to_y;
    c.ai_group = 0xff;
    Unit& o = units_[size_t(find_unit(unit_id))];
    o.flags |= kHidden | kGone;
    if (!keep_state) set_state(c, state::kMove);
    if (c.flags & kStepReserved) {
        auto& f = map_.at(c.to_x, c.to_y).flags;
        f = (f & ~7u) | uint32_t(to) | 8u;
    }
    c.flags |= 0x2000000;
    set_state(o, 0x11);
    o.countdown = 10;
    return id;
}

bool World::capture_building(int victim, int captor, int index) {
    // FUN_0046d6d0 (docs/re/30 section 2.5).
    if (buildings_[size_t(index)].flags & 0xC) return false;
    const int record = buildings_[size_t(index)].record;
    const auto& r = templates_.building_records[size_t(record)];
    const int bx = buildings_[size_t(index)].cell_x, by = buildings_[size_t(index)].cell_y;
    const bool was_dying = buildings_[size_t(index)].dying; // copied with the 0x7C bytes
    clear_building_cells(buildings_[size_t(index)]); // FUN_0044dee0 / FUN_0044df60
    const int n = create_building(captor, record, bx, by, false, true, true);
    if (n < 0) return false; // the tiles are already gone (as the exe)
    {
        const Building old = buildings_[size_t(index)];
        Building& nb = buildings_[size_t(n)];
        const uint16_t slot = nb.slot; // +0x8c is past the copied 0x7C bytes
        nb = old; // the 0x7C bytes; +0x3c (the original side) is kept
        nb.index = n;
        nb.slot = slot;
        nb.owner = captor;
        nb.attacker_side = -1;
        nb.attacker = -1;
    }
    if (buildings_[size_t(n)].selected) {
        deselect_all();
        buildings_[size_t(n)].selected = true;
    }
    storage_pass(); // FUN_0044abe0 (0x46d853): the drip and the capacity of every side
    if (r.kind == 4 || r.kind == 5) {
        auto& v = players_[size_t(victim)];
        if (v.capacity != 0) {
            const uint32_t a = uint32_t(v.spice * 2000) / uint32_t(v.capacity);
            players_[size_t(captor)].pending_spice += int32_t(a); // FUN_0046c790
            spend(victim, int32_t(a));
        }
    }
    Building& old = buildings_[size_t(index)];
    old.death_timer = 1;
    old.dying = true; // FUN_0049e120(0x11)
    old.flags |= 0x110000;
    Building& nb = buildings_[size_t(n)];
    if (nb.flags & 0x80) nb.flags &= ~0x90u;
    nb.flags = (nb.flags & ~0x01000001u) | 0x2000000u;
    std::vector<int> docked;
    for (const auto& h : units_) {
        if (h.owner == victim && !h.remove && templates_.unit_records[size_t(h.record)].kind == 1 && h.state == 0xF &&
            h.refinery == int(buildings_[size_t(index)].slot)) {
            docked.push_back(h.index);
        }
    }
    for (int id : docked) {
        count_unit_lost(victim, units_[size_t(find_unit(id))].record); // 0x46d925
        const int c = convert_unit(victim, captor, id, true);
        if (c >= 0) units_[size_t(find_unit(c))].refinery = building_slot(n); // 0x46d973: the new building's slot
    }
    // FUN_0049e120(new, 2) is refused for state 0x11: a building captured
    // while dying (destroyed during the walk-in) keeps the copied death
    // countdown and is removed when it runs out.
    if (!was_dying) buildings_[size_t(n)].dying = false;
    return true;
}

} // namespace d2k::sim
