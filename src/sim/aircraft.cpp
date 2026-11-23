// Aircraft flight, the carryall delivery, and explosion / effect objects.
// RE: docs/re/14-aircraft-and-effects.md, docs/re/13-harvesting.md section 5.
#include <algorithm>
#include <cmath>

#include "sim/world.hpp"

namespace d2k::sim {

namespace {
constexpr uint32_t kAircraft = 0x40;
constexpr uint32_t kLoaded = 0x100, kJob = 0x200, kDelivery = 0x800, kLeaving = 0x4000;
constexpr uint32_t kCargoShown = 0x20000000;
constexpr uint32_t kAirOver = 0x800000; // cell flag: an aircraft over the cell
constexpr int32_t kPx = 0x10000;
constexpr int32_t kAltTop = 60 * kPx, kAltDrop = 10 * kPx;

// The flight step tables (FUN_0044b090): DX/DY[facing][s], s in 1/32 px.
struct StepTables {
    std::array<std::array<int32_t, 512>, 32> dx{}, dy{};
    StepTables() {
        for (int f = 0; f < 32; ++f) {
            const double a = double(f) * 6.283185308 * 0.03125;
            for (int s = 0; s < 512; ++s) {
                dx[size_t(f)][size_t(s)] = int32_t(std::sin(a) * double(s * 0x800));
                dy[size_t(f)][size_t(s)] = int32_t(std::cos(a) * double(s * 0x800));
            }
        }
    }
};
const StepTables& steps() {
    static const StepTables t;
    return t;
}

// cell(p) = (p >> 16) / 32, truncating toward zero, stored as a byte.
uint8_t cell_of(int32_t p) { return uint8_t(int8_t(((p >> 16) / 32) & 0xff)); }
} // namespace

int32_t flight_dx(int facing, int s) { return steps().dx[size_t(facing & 31)][size_t(s & 511)]; }
int32_t flight_dy(int facing, int s) { return steps().dy[size_t(facing & 31)][size_t(s & 511)]; }

// ---- flight -----------------------------------------------------------------

void World::turn_air(Unit& u, int want) {
    if (u.facing == want) return;
    if (u.turn_counter != 0) {
        --u.turn_counter;
        return;
    }
    u.turn_counter = templates_.unit_records[size_t(u.record)].turn_delay;
    const int cw = (want - u.facing + 32) % 32, ccw = (u.facing - want + 32) % 32;
    u.facing = uint8_t((u.facing + (cw < ccw ? 1 : -1) + 32) & 31);
}

int World::flight_step(Unit& u, int mode) {
    const auto& r = templates_.unit_records[size_t(u.record)];
    int32_t speed = u.speed;
    bool straight;
    if (u.state == 0x11) {
        straight = true;
    } else if (mode == 1) {
        straight = false;
        speed = r.speed;
    } else {
        if (r.kind == 9 || r.kind == 15) { // wings (docs/re/35 section 5.1)
            if (u.wing_timer == 0) {
                u.wing_timer = 4;
                if (u.wing_dir == 0) {
                    if (u.frame < 2) {
                        ++u.frame;
                    } else {
                        u.wing_dir = 1;
                        u.wing_timer = 20;
                    }
                } else if (u.frame == 0) {
                    u.wing_dir = 0;
                } else {
                    --u.frame;
                }
            } else {
                --u.wing_timer;
            }
        }
        if ((u.to_x >= map_.width() + 4 || u.to_y >= map_.height() + 4) && (u.flags & kDelivery) &&
            (u.flags & kLeaving)) {
            set_state(u, 0x11);
            u.countdown = 10;
            return 1;
        }
        const int32_t tx = (int32_t(u.dest_x) * 32 + 16) * kPx, ty = (int32_t(u.dest_y) * 32 + 16) * kPx;
        const uint8_t cx = cell_of(u.x), cy = cell_of(u.y);
        if (r.kind != 9 && cx == u.dest_x && cy == u.dest_y) { // centring, 1 px per axis
            bool moved = false;
            auto centre = [&](int32_t& p, int32_t t) {
                if ((p >> 16) / 2 < (t >> 16) / 2) {
                    p += kPx;
                    moved = true;
                } else if ((p >> 16) / 2 > (t >> 16) / 2) {
                    p -= kPx;
                    moved = true;
                }
            };
            centre(u.x, tx);
            centre(u.y, ty);
            return moved ? 1 : 0;
        }
        const int ddx = int(u.dest_x) - int(cx), ddy = int(u.dest_y) - int(cy);
        const int d2 = ddx * ddx + ddy * ddy;
        if (!(u.flags & 0x10000) && (u.flags & 0xb00) && d2 <= 16) {
            speed = speed <= kPx ? kPx : int32_t(double(speed) * 0.95);
        } else {
            speed = r.speed;
        }
        straight = false;
        if (r.kind == 9) {
            // The bombing run (docs/re/35 section 5.1): signed cells from the
            // position before the move; straight within d2 < 32, a bomb every
            // 8 global ticks within d2 < 16; no bombs left: leave.
            const int sx = (u.x >> 16) / 32, sy = (u.y >> 16) / 32;
            const int bx = int(u.dest_x) - sx, by = int(u.dest_y) - sy;
            const int b2 = bx * bx + by * by;
            if (b2 < 32) {
                if (b2 < 16 && (tick_count_ & 7) == 0 && u.bombs != 0) {
                    const int px = u.x >> 16, py = u.y >> 16;
                    add_bullet(u.owner, r.weapon, u.index, px, py, px, py, -1);
                    --u.bombs;
                }
                straight = true;
            }
            if (u.bombs == 0) {
                straight = true;
                u.flags |= kLeaving;
            }
        }
        if (cx == u.dest_x || cy == u.dest_y) u.flags &= ~0x400u;
        u.turret_facing = uint8_t(angle12((u.x - tx) >> 16, (ty - u.y) >> 16) >> 7); // desired heading +0x55
        u.flags |= 0x400;
    }
    u.speed = speed;
    if (!straight) turn_air(u, u.turret_facing);
    const auto& t = steps();
    const size_t s = size_t((uint32_t(speed) & 0x7ffffff) >> 11) & 511;
    u.x += t.dx[u.facing & 31][s];
    u.y -= t.dy[u.facing & 31][s];
    auto in_map = [&](uint8_t x, uint8_t y) { return x < map_.width() && y < map_.height(); };
    if (in_map(u.to_x, u.to_y)) map_.at(u.to_x, u.to_y).flags &= ~kAirOver;
    u.cur_x = u.to_x = cell_of(u.x);
    u.cur_y = u.to_y = cell_of(u.y);
    if (in_map(u.to_x, u.to_y)) map_.at(u.to_x, u.to_y).flags |= kAirOver;
    u.steps = 0;
    return 1;
}

int World::edge_point(int& x, int& y, int k) const {
    // FUN_0049e6b0 (docs/re/35 section 4.1): n = 4 + k cells past the nearer
    // map edge, top / bottom on ties (unsigned compare); returns the facing
    // toward the map. Only one coordinate changes.
    const int n = 4 + k, w = map_.width(), h = map_.height();
    const int ex = x > w / 2 ? w - x : x, ey = y > h / 2 ? h - y : y;
    if (uint32_t(ey) <= uint32_t(ex)) {
        if (y <= h / 2) {
            y = -n;
            return 16;
        }
        y = h + n;
        return 0;
    }
    if (x <= w / 2) {
        x = -n;
        return 8;
    }
    x = w + n;
    return 24;
}

void World::launch_death_hand(int side, int tx, int ty) {
    // FUN_0049e860: from the primary Harkonnen palace (else the charge is
    // lost), at its record's +0x62 / +0x63 pixel offset from the centre of
    // the cell left of / below its bottom-left corner; hidden for 21 updates.
    const auto* ids = templates_.block(0x6d60d8);
    const int rec = ids && ids->bytes.size() > 0x13 ? ids->bytes[0x13] : -1; // 0x6d60eb
    const int pi = players_[size_t(side)].primary[17];
    if (rec < 0 || pi < 0 || size_t(pi) >= buildings_.size() || buildings_[size_t(pi)].removed) return;
    Building& b = buildings_[size_t(pi)];
    const auto& br = templates_.building_records[size_t(b.record)];
    const int cx = ((b.x / 65536) / 32), cy = ((b.y / 65536) / 32);
    const int idx = create_unit(side, rec, cx, cy, cx, cy);
    if (idx < 0) return;
    Unit& u = units_[size_t(idx)];
    u.x = int32_t(cx * 32 + 16 + br.exit_x) << 16;
    u.y = int32_t(cy * 32 + 16 + br.exit_y) << 16;
    u.dest_x = uint8_t(tx);
    u.dest_y = uint8_t(ty);
    u.bombs = 20; // +0x29: the launch delay
    u.flags |= 0x200000;
    set_state(u, state::kMove);
    b.flags |= 0x20; // one cycle of the palace's active animation
    voice(local_player, "DEATHLAUNCHED"); // every player hears it, in the local house's voice
}

int World::death_hand_step(Unit& u) {
    // FUN_00494cd0 (docs/re/41 section 5.1).
    if (u.flags & 0x200000) { // waiting at the palace
        if (u.bombs == 0) u.flags &= ~0x200000u;
        else --u.bombs;
        return 1;
    }
    const auto* ids = templates_.block(0x6d60d8);
    const int trail = ids && ids->bytes.size() > 0x3d ? ids->bytes[0x3d] : 0xff; // 0x6d6115 EX_DHM
    if (trail != 0xff) spawn_effect_at(trail, u.x / 65536, u.y / 65536, u.owner, 0, 0, 0, u.altitude);
    const int s = int((uint32_t(u.speed) & 0x7ffffff) >> 11);
    const int32_t mdx = flight_dx(u.facing, s), mdy = flight_dy(u.facing, s);
    u.x += mdx;
    u.y -= mdy;
    if (u.facing == 0) { // flying north
        if ((u.y / 32) / 65536 < -2) {
            u.facing = 16;
            u.x = int32_t(uint32_t(u.dest_x) << 21);
        }
        return 1;
    }
    if (u.y >= int32_t(uint32_t(u.dest_y) << 21) && !(u.flags & 0x8000)) {
        u.flags |= 0x8000;
        // The burst (0x494de3): EX_BANG14 and EX_DEATHHAND at 60 px with one
        // move of (DX, -DY); no sound; 40 DHBOMBs, 4 rand() each.
        const auto& r = templates_.unit_records[size_t(u.record)];
        const int px = u.x / 65536, py = u.y / 65536;
        spawn_effect_at(r.death_explosion, px, py, u.owner, 0, 0x1000, 0, u.altitude, mdx, -mdy);
        const int burst = ids && ids->bytes.size() > 0x3e ? ids->bytes[0x3e] : 0xff; // 0x6d6116 EX_DEATHHAND
        if (burst != 0xff) spawn_effect_at(burst, px, py, u.owner, 0, 0x1000, 0, u.altitude, mdx, -mdy);
        for (int k = 0; k < 40; ++k) {
            const int dir = int(rng_.next() & 0x1f);
            const int delay = int(rng_.next() & 7);
            const int st = int(rng_.next() & 0xff);
            const int bi = add_bullet(u.owner, r.weapon, u.index, px, py, px + flight_dx(dir, st), py + flight_dy(dir, st), -1);
            if (bi >= 0) {
                bullets[size_t(bi)].delay = delay;
                bullets[size_t(bi)].vz = int32_t(rng_.next() & 0xffff);
            }
        }
        return 0;
    }
    return 1;
}

void World::launch_airstrike(int side, int ox, int oy, int tx, int ty) {
    // FUN_0049e770: 3 ornithopters 4, 7 and 10 cells past the nearest edge,
    // in a line from the home-view cell, flying in toward (tx, ty).
    const auto* ids = templates_.block(0x6d60d8);
    const int rec = ids && ids->bytes.size() > 0x12 ? ids->bytes[0x12] : -1; // 0x6d60ea
    if (rec < 0) return;
    for (int i = 0; i < 3; ++i) {
        const int idx = create_unit(side, rec, 5, 5, 5, 5);
        if (idx < 0) continue;
        Unit& u = units_[size_t(idx)];
        int x = int16_t(ox), y = int16_t(oy);
        const int f = edge_point(x, y, 3 * i);
        u.x = int32_t(uint32_t(int16_t(x)) << 21); // the cell corner
        u.y = int32_t(uint32_t(int16_t(y)) << 21);
        u.dest_x = uint8_t(tx);
        u.dest_y = uint8_t(ty);
        u.altitude = 0x3c0000;
        u.bombs = 12;
        u.flags |= 0x10800;
        set_state(u, state::kMove);
        u.facing = uint8_t(f);
        u.turret_facing = uint8_t(f);
    }
}

bool World::spiral_free_cell(int& x, int& y, int r) const {
    // FUN_004a60e0: west 1, south 1, east 2, north 2, west 3, ... until the
    // run reaches 2r + 2; first cell with flags & 0x63f8 == 0x6000.
    static const int kDx[4] = {-1, 0, 1, 0}, kDy[4] = {0, 1, 0, -1};
    int cx = x, cy = y, run = 1;
    for (int leg = 0; run < 2 * r + 2; ++leg) {
        for (int i = 0; i < run; ++i) {
            cx += kDx[leg & 3];
            cy += kDy[leg & 3];
            if (cx >= 0 && cy >= 0 && cx < map_.width() - 1 && cy < map_.height() - 1 &&
                (map_.at(cx, cy).flags & 0x63f8) == 0x6000) {
                x = cx;
                y = cy;
                return true;
            }
        }
        if (leg & 1) ++run;
    }
    return false;
}

// ---- deliveries --------------------------------------------------------------

void World::queue_delivery(int p, int x, int y, std::vector<int> records, int type, bool silent, int action) {
    auto& table = deliveries_[size_t(p)];
    Delivery* e = nullptr;
    for (auto& d : table) {
        if (!d.active) {
            e = &d;
            break;
        }
    }
    if (!e) {
        if (table.size() >= 10) return;
        table.emplace_back();
        e = &table.back();
    }
    *e = Delivery{};
    e->x = x;
    e->y = y;
    e->active = true;
    e->due = tick_count_;
    e->records = std::move(records);
    e->type = type;
    e->action = action;
    e->silent = silent;
}

void World::tick_deliveries(int p) {
    auto& table = deliveries_[size_t(p)];
    auto& pl = players_[size_t(p)];
    const auto* ids = templates_.block(0x6d60d8);
    for (size_t i = 0; i < table.size(); ++i) {
        auto& e = table[i];
        if (p == local_player && e.type == 2 && e.active && !e.started) {
            // The FRIGATEIN countdown (FUN_0046def0), each voice at most once
            // per T / 2 ticks; IN1 at the launch.
            const uint32_t T = uint32_t(vars.starport_frigate_delay);
            const char* v = "FRIGATEIN1";
            if (e.due > tick_count_) {
                const uint32_t left = e.due - tick_count_;
                v = left < T / 4 ? "FRIGATEIN2" : left < T / 2 ? "FRIGATEIN3" : left < 3 * T / 4 ? "FRIGATEIN4" : "FRIGATEIN5";
            }
            voice(p, v, T / 2);
        }
        if (!e.active || e.started || e.due > tick_count_) continue;
        e.started = true;
        int x = e.x, y = e.y;
        int record = -1, dest_x = e.x, dest_y = e.y;
        if (e.type == 2) {
            const int frigate = ids && ids->bytes.size() > 14 ? ids->bytes[14] : -1; // 0x6d60e6
            if (frigate < 0) continue;
            if (owned_record(p, frigate) != 0) { // one frigate per side on the map
                e.started = false;
                continue;
            }
            if (pl.primary[13] < 0) {
                // FUN_0046eac0; the entry stays active and started (slot leak), +0x262a5 stays.
                refund(p, int32_t(pl.starport_money));
                pl.starport_money = 0;
                return;
            }
            const Building& s = buildings_[size_t(pl.primary[13])];
            dest_x = ((s.x >> 16) / 32) + 1; // the centre cell (y is the bottom edge)
            dest_y = ((s.y >> 16) / 32) - 2;
            x = dest_x;
            y = dest_y;
            record = frigate;
        } else {
            record = ids && ids->bytes.size() > 17 ? ids->bytes[17] : -1; // 0x6d60e9 carryall
            if (record < 0) continue;
        }
        const int facing = edge_point(x, y, 0);
        const int carryall = record;
        const int slot = pool_pop(p);
        if (slot < 0) { // CreateUnit fails: the entry stays started, the delivery is lost (docs/re/50)
            reached_unit_limit();
            continue;
        }
        Unit u;
        u.index = next_unit_id_++;
        u.slot = uint16_t(slot);
        u.owner = p;
        u.house = players_[size_t(p)].house;
        u.record = carryall;
        const auto& r = templates_.unit_records[size_t(carryall)];
        u.type = r.name_index;
        u.speed = r.speed;
        u.hit_points = r.hit_points;
        u.flags = r.initial_flags | kAircraft;
        u.altitude = kAltTop;
        u.x = int32_t(x * 32) * kPx; // the cell corner
        u.y = int32_t(y * 32) * kPx;
        u.cur_x = u.to_x = cell_of(u.x);
        u.cur_y = u.to_y = cell_of(u.y);
        u.dest_x = uint8_t(dest_x);
        u.dest_y = uint8_t(dest_y);
        u.facing = u.turret_facing = uint8_t(facing);
        u.delivery = int(i);
        u.flags |= 0x8000b00;
        if (e.type == 1) {
            int count = 0;
            for (int rec : e.records) count += rec >= 0;
            if (count == 1) u.flags |= kCargoShown;
        }
        set_state(u, state::kMove);
        u.next_state = 1;
        if (e.type == 1) { // a delivery carryall adds a request slot and a live count, not an owned one
            pl.carry_count = uint8_t(std::min(pl.carry_count + 1, 32));
            ++pl.carry_live;
        }
        if (e.type == 1 && !e.silent) voice(p, "REINFORCE");
        if (e.type == 2) pl.delivery_pending = true;
        units_.push_back(u);
    }
}

int World::drop_step(Unit& u) {
    auto& e = deliveries_[size_t(u.owner)][size_t(u.delivery)];
    if (tick_count_ % (e.type == 1 ? 4 : 8) != 0) return 1;
    const uint32_t f = map_.at(u.to_x, u.to_y).flags;
    if (f & (e.type == 1 ? 0x3f8u : 0x3e8u)) {
        ++u.counter_2b;
        if (e.type == 1 && u.counter_2b > 40) {
            set_state(u, 0x16);
            return 1;
        }
        if (u.counter_2b < 81) return 1;
        e.active = false;
        return 0;
    }
    u.counter_2b = 0;
    size_t k = 0;
    while (k < e.records.size() && e.records[k] == -2) ++k;
    if (k >= e.records.size() || e.records[k] < 0) {
        e.active = false;
        return 0;
    }
    const int rec = e.records[k];
    if (difficulty == 0 && (k & 1) && alliance_[size_t(u.owner)][size_t(local_player)] != 0) {
        e.records[k] = -2; // easy: every odd unit of a non-allied delivery is skipped
        return 1;
    }
    const int x = u.to_x, y = u.to_y, owner = u.owner, entry = u.delivery;
    const uint8_t facing = u.facing;
    // create_unit may reallocate units_: `u` is not used after this.
    const int ui = create_unit(owner, rec, x, y, x, y);
    if (ui >= 0) {
        Unit& d = units_[size_t(ui)];
        d.facing = facing;
        int fx = x, fy = y;
        if (e.type == 2) {
            // Off the Starport's centre cell through state 0x1a (doc 21
            // section 4); a harvester harvests afterwards.
            const int kind = templates_.unit_records[size_t(rec)].kind;
            if (kind == 1) {
                d.state = d.next_state = 0xb;
                d.refinery = building_slot(least_used_refinery(owner)); // FUN_004a5e80
            } else {
                d.next_state = 2;
            }
            if (kind != 2) set_state(d, 0x1a);
        } else if (templates_.unit_records[size_t(rec)].kind == 1) {
            d.next_state = 0xb;
            const int ref = nearest_refinery(owner, x, y);
            d.refinery = building_slot(ref);
            if (ref >= 0) dock_cell(ref, fx, fy);
        }
        if (e.type == 1) {
            free_cell_near(owner, fx, fy);
            set_state(d, state::kMove);
            d.dest_x = uint8_t(fx);
            d.dest_y = uint8_t(fy);
            d.next_state = templates_.unit_records[size_t(rec)].kind == 1 ? 0xb : 1;
        }
        // "Just delivered" (0xc0000 for the last record) and the entry
        // (FUN_0049e3e0); the AI groups such units by delivery.
        size_t next = k + 1;
        while (next < e.records.size() && e.records[next] == -2) ++next;
        const bool last = next >= e.records.size() || e.records[next] < 0;
        d.flags |= last ? 0xc0000u : 0x40000u;
        d.delivery_entry = uint8_t(entry);
    }
    e.records[k] = -2;
    return 1;
}

void World::tick_aircraft(Unit& u) {
    const auto& r = templates_.unit_records[size_t(u.record)];
    const int id = u.index;
    tick_aircraft_state(u);
    // The tail of the unit update (docs/re/33 section 3.7): a loaded ferry
    // carryall carries its cargo along; not on the removal tick.
    const int i = find_unit(id);
    if (i < 0) return;
    Unit& c = units_[size_t(i)];
    if (r.kind == 2 && !c.remove && (c.flags & kLoaded) && !(c.flags & kDelivery)) carry_tail(c);
}

void World::tick_aircraft_state(Unit& u) {
    const auto& r = templates_.unit_records[size_t(u.record)];
    switch (u.state) {
    case state::kMove:
        if (u.flags & 0x20000) { // the Death Hand
            if (death_hand_step(u) == 0) {
                set_state(u, 0x11);
                u.countdown = 10;
            }
            return;
        }
        if (ferry_job_cancelled(u)) return;
        if (r.kind == 8 && (u.flags & kLoaded) && players_[size_t(u.owner)].primary[13] >= 0) {
            buildings_[size_t(players_[size_t(u.owner)].primary[13])].flags |= 0x20; // one more animation cycle
        }
        if (flight_step(u, 0) == 0) set_state(u, state::kIdle);
        return;
    case 0x11: // dying / leaving: straight on, then removed
        // The zero test first (0x499e31), then the decrement and the flight
        // step (0x49a09a): countdown steps, removal in the update after.
        if (u.countdown == 0) {
            // FUN_0049f330(+0x4a, +0x4b) at 0x499e69. Every aircraft writer
            // of the cells sets +0x4a/+0x4b and +0x4c/+0x4d together
            // (CreateUnit 0x455a94 / 0x455c0b, the flight step 0x495476;
            // the Death Hand step writes none), so cur == to here.
            if (u.cur_x < map_.width() && u.cur_y < map_.height()) map_.at(u.cur_x, u.cur_y).flags &= ~kAirOver;
            u.remove = true;
            return;
        }
        --u.countdown;
        if (u.flags & 0x20000) death_hand_step(u); // the result is ignored; no burst twice (0x8000)
        else flight_step(u, 0);
        return;
    case state::kIdle:
        if ((r.kind == 2 || r.kind == 8) && (u.flags & kDelivery)) {
            if ((u.flags & kLoaded) && !(u.flags & kLeaving)) {
                if (u.to_x == u.dest_x && u.to_y == u.dest_y) set_state(u, 0x15);
                else set_state(u, state::kMove);
            } else if (!(u.flags & kJob)) {
                int x = u.to_x, y = u.to_y;
                edge_point(x, y, 0);
                u.dest_x = uint8_t(x); // read unsigned: -4 becomes 252 (doc 14 section 3.2)
                u.dest_y = uint8_t(y);
                set_state(u, state::kMove);
            }
        }
        else if (r.kind == 2 || r.kind == 8) ferry_idle(u); // the ferry branch, also the idle circle
        return;
    case 0x15: { // descend and drop
        if (!(u.flags & kDelivery)) {
            ferry_descend(u);
            return;
        }
        if (r.kind == 8 && !(u.flags & kLeaving)) {
            const int sp = players_[size_t(u.owner)].primary[13];
            if (sp < 0) { // the Starport is gone: leave
                u.flags = (u.flags & ~0x200u) | kLeaving;
                u.speed = r.speed;
                set_state(u, 0x16);
                return;
            }
            const Building& s = buildings_[size_t(sp)];
            const uint8_t cx = uint8_t(((s.x >> 16) / 32) + 1), cy = uint8_t(((s.y >> 16) / 32) - 2);
            if (cx != u.to_x || cy != u.to_y) { // another primary Starport: re-route
                set_state(u, 0x16);
                u.speed = r.speed;
                u.dest_x = cx;
                u.dest_y = cy;
                return;
            }
        }
        if (u.altitude != kAltDrop && r.kind == 2 && !passable(u.dest_x, u.dest_y, u, 0)) {
            int x = u.dest_x, y = u.dest_y;
            spiral_free_cell(x, y, 8);
            u.dest_x = uint8_t(x);
            u.dest_y = uint8_t(y);
            set_state(u, 0x16);
            return;
        }
        if (u.altitude > kAltDrop) {
            u.altitude -= kPx;
            return;
        }
        if (drop_step(u) == 0) {
            if (r.kind == 8) { // FUN_0046eaf0: the money is now spent
                players_[size_t(u.owner)].delivery_pending = false;
                players_[size_t(u.owner)].starport_money = 0;
            }
            set_state(u, 0x16);
            u.flags = (u.flags & ~0x300u) | kLeaving;
            // 0x4989xx: for an AI side the delivery's group gets "start"
            // (flag 1) and the deploy action as its mode.
            if (u.delivery >= 0) ai_delivery_done(u.owner, u.delivery);
        }
        return;
    }
    case 0x16:
        if (u.altitude < kAltTop) u.altitude += kPx;
        else set_state(u, state::kIdle);
        return;
    default:
        return;
    }
}

// ---- effects ------------------------------------------------------------------

void World::spawn_effect(const char* name, int32_t x, int32_t y, int owner) {
    const int e = templates_.explosion_index(name);
    if (e < 0) return;
    const int slot = pool_pop(owner);
    if (slot < 0) return; // FUN_00456df0: nothing created (docs/re/50)
    Effect fx;
    fx.slot = uint16_t(slot);
    fx.explosion = e;
    fx.flags = templates_.explosion_flags[size_t(e)]; // ^ caller flags (0 here)
    fx.x = x;
    fx.y = y;
    fx.owner = owner;
    effects.push_back(fx);
}

void World::tick_effects(int side) {
    // FUN_0049dd80: one frame every 3 ticks; removed at the frame count.
    // The side's explosions in creation order; one that ends is freed right
    // after its update (FUN_00458e90, docs/re/50).
    for (auto& e : effects) {
        if (e.owner != side || e.frame < 0) continue;
        tick_effect(e);
        if (e.frame < 0) pool_push(side, e.slot);
    }
    effects.erase(std::remove_if(effects.begin(), effects.end(), [](const Effect& e) { return e.frame < 0; }),
                  effects.end());
}

void World::tick_effect(Effect& e) {
    {
        if (e.flags & 0x1000) { // the one-time move (16.16 position in the exe)
            e.x = int32_t((int64_t(e.x) * 0x10000 + e.move_x) >> 16);
            e.y = int32_t((int64_t(e.y) * 0x10000 + e.move_y) >> 16);
            e.move_x = e.move_y = 0;
        }
        if (e.flags & 0x400) { // blink mode
            const uint8_t mask = templates_.explosion_blink[size_t(e.explosion)];
            if (mask & (1u << (e.counter & 7))) e.flags &= ~0x800u;
            else e.flags |= 0x800;
            if (++e.counter == 8) e.frame = -1;
            return;
        }
        if (e.counter == 0) {
            e.flags &= ~0x20u;
            e.counter = 2;
            if (++e.frame >= templates_.art.explosion_frames[size_t(e.explosion)]) e.frame = -1;
        } else {
            --e.counter;
        }
        if (e.frame >= 0 && (e.flags & 1)) e.height += 2;
    }
}

} // namespace d2k::sim
