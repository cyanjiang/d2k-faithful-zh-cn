// The sandworm (kind 5): sleep, travel, decide, eating vehicles on sand, the
// sand ripple particles and the worm sign. RE: docs/re/17-sandworm.md.
#include <algorithm>
#include <cstdlib>

#include "sim/world.hpp"

namespace d2k::sim {

namespace {
constexpr uint32_t kSubmerged = 4;
constexpr uint32_t kSand = cell::kSand;
constexpr uint32_t kVehicle = 0x8;
namespace ws {
constexpr int kTravel = 0x1b, kDecide = 0x1c, kSleep = 0x1d;
} // namespace ws
} // namespace

void World::set_vars(std::span<const uint8_t> v) {
    // bin\VARS.BIN, read straight into 0x6b8818 (docs/re/13 section 7, 17 section 1).
    if (v.size() < 0x41) return;
    auto i32 = [&](size_t o) { return int32_t(v[o] | v[o + 1] << 8 | v[o + 2] << 16 | uint32_t(v[o + 3]) << 24); };
    vars.unload_delay = i32(0x00);
    vars.blob_value = i32(0x04);
    vars.load_delay = i32(0x08);
    vars.starport_update_delay = i32(0x0c);
    vars.starport_stock_delay = i32(0x10);
    vars.starport_stock_prob = i32(0x14);
    vars.starport_variation = i32(0x18);
    vars.starport_frigate_delay = i32(0x1c);
    vars.refinery_fx_x = i32(0x20);
    vars.refinery_fx_y = i32(0x24);
    vars.drive_distance = i32(0x28);
    vars.repair_points = i32(0x30);
    vars.unit_repair_points = i32(0x34);
    vars.repair_carry_distance = i32(0x2c);
    vars.number_of_fremen = v[0x3c];
    vars.death_hand_accuracy = v[0x4d];
    vars.devastator_explode_delay = v[0x43];
    vars.worm_appetite = v[0x3d];
    vars.worm_initial_sleep = v[0x3e];
    vars.worm_fed_sleep = v[0x3f];
    vars.worm_shot_sleep = v[0x40];
    if (v.size() >= 0x48) vars.chase_give_up = i32(0x44);
    if (v.size() >= 0x4c) vars.crate_cash = i32(0x48);
}

int World::unit_in_cell(int x, int y, bool& infantry, bool skip_cloaked) const {
    // FUN_004a5270(cell * 32, ...): the cell's top-left pixel.
    return unit_at_pixel(x * 32, y * 32, skip_cloaked, &infantry);
}

int World::click_unit(int player, int px, int py) const {
    // FUN_004433a0 at 0x445683 (click) / 0x443562 (cursor), docs/re/10 "Hit
    // test": skip_cloaked when the cell's owner bits are another side's and
    // it has unit bits (0x3e8); a cloaked unit of a side that is not allied
    // to the player counts as none (docs/re/32 section 1.5).
    const int cx = px >> 5, cy = py >> 5; // the callers' cell (0x443492 / 0x4455f0)
    if (px < 0 || py < 0 || cx >= map_.width() || cy >= map_.height()) return -1;
    const uint32_t f = map_.at(cx, cy).flags;
    const int ui = unit_at_pixel(px, py, int(f & 7) != player && (f & 0x3e8));
    if (ui >= 0 && (units_[size_t(ui)].flags & 4) && alliance(player, units_[size_t(ui)].owner) != 0) return -1;
    return ui;
}

int World::unit_at_pixel(int px, int py, bool skip_cloaked, bool* infantry_out) const {
    // FUN_004a5270 (docs/re/10 "Hit test"). The cell is (px >> 5, py >> 5),
    // each capped at 0x7f. Only the units of the cell's owner (owner bits
    // 0..2) are searched: the first ground vehicle in list order on (or
    // stepping into) the cell; else that owner's infantry on the cell
    // nearest to the pixel (Manhattan distance in px, < 0x800, the first on
    // ties). No sprite or pixel box test. The owner bits can be stale (no
    // one clears them when the last unit leaves), so an infantry of another
    // side in that cell is not found.
    bool infantry_local = false;
    bool& infantry = infantry_out ? *infantry_out : infantry_local;
    infantry = false;
    // The exe keeps the low byte of px >> 5 (unsigned) and caps it at 0x7f.
    // A cell outside our map is no unit (the exe would read past the row).
    auto cell_of = [](int p) {
        const uint8_t c = uint8_t(uint32_t(p) >> 5);
        return c > 0x7e ? 0x7f : int(c);
    };
    const int x = cell_of(px), y = cell_of(py);
    if (x >= map_.width() || y >= map_.height()) return -1;
    const uint32_t f = map_.at(x, y).flags;
    const int owner = int(f & 7);
    if (f & kVehicle) {
        for (size_t i = 0; i < units_.size(); ++i) {
            const auto& u = units_[i];
            const auto& r = templates_.unit_records[size_t(u.record)];
            if (u.owner != owner || r.infantry || u.remove) continue;
            if (r.kind == 2 || r.kind == 5 || r.kind == 8 || r.kind == 9 || r.kind == 10) continue;
            if (u.flags & 0x400140) continue;
            if ((u.cur_x == x && u.cur_y == y) || ((u.flags & 0x10000000) && u.to_x == x && u.to_y == y)) {
                if (skip_cloaked && (u.flags & 4)) return -1; // a cloaked vehicle counts as none
                return int(i);
            }
        }
    }
    if (!(f & kVehicle) && (f & 0x3e0)) {
        int best = -1;
        uint32_t best_d = 0x800;
        for (size_t i = 0; i < units_.size(); ++i) {
            const auto& u = units_[i];
            if (u.owner != owner || !templates_.unit_records[size_t(u.record)].infantry || u.remove) continue;
            if (u.flags & 0x400140) continue;
            if (skip_cloaked && (u.flags & 4)) continue;
            if (!((u.cur_x == x && u.cur_y == y) || ((u.flags & 0x10000000) && u.to_x == x && u.to_y == y))) continue;
            const uint32_t d = uint32_t(std::abs((u.y >> 16) - py) + std::abs((u.x >> 16) - px));
            if (d < best_d) {
                best_d = d;
                best = int(i);
            }
        }
        infantry = best >= 0;
        return best;
    }
    return -1;
}

bool World::nearest_vehicle_on_sand(int cx, int cy, int& vx, int& vy) const {
    // FUN_0044d630: the cell itself if it has a vehicle; else the sand cell
    // with a vehicle at the smallest |dx| + |dy| (first found wins a tie).
    if (map_.at(cx, cy).flags & kVehicle) {
        vx = cx;
        vy = cy;
        return true;
    }
    const int w = map_.width(), h = map_.height();
    int best = w + h;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t f = map_.at(x, y).flags;
            if (!(f & kSand) || !(f & kVehicle)) continue;
            const int d = std::abs(x - cx) + std::abs(y - cy);
            if (d < best) {
                best = d;
                vx = x;
                vy = y;
            }
        }
    }
    return best < w + h;
}

bool World::nearest_thumper(int cx, int cy, int& tx, int& ty) const {
    // FUN_0044d500: a sand cell with infantry whose unit is a thumping
    // thumper (kind 0xe, state 0x20).
    const int w = map_.width(), h = map_.height();
    int best = w + h;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t f = map_.at(x, y).flags;
            if (!(f & kSand) || !(f & 0x3e0)) continue;
            bool inf = false;
            const int ui = unit_in_cell(x, y, inf);
            if (ui < 0 || templates_.unit_records[size_t(units_[size_t(ui)].record)].kind != 0xe ||
                units_[size_t(ui)].state != 0x20) {
                continue;
            }
            const int d = std::abs(x - cx) + std::abs(y - cy);
            if (d < best) {
                best = d;
                tx = x;
                ty = y;
            }
        }
    }
    return best < w + h;
}

void World::ripple(int32_t x, int32_t y) {
    // FUN_0045d730: 4 tries in a 12 px circle; two particles per hit.
    for (int k = 0; k < 4; ++k) {
        const int dx = 12 - int(rng_.next() % 25), dy = 12 - int(rng_.next() % 25);
        if (dx * dx + dy * dy >= 144) continue;
        // 0x45d82f: FILD qword (rand() % 200), FMUL the double 0.001 at
        // 0x4c9450, FSTP float; the sign follows dx / dy.
        float vx = float(double(rng_.next() % 200) * 0.001);
        if (dx < 0) vx = -vx;
        float vy = float(double(rng_.next() % 200) * 0.001);
        if (dy < 0) vy = -vy;
        // The colour is taken once per accepted try, before the two life
        // rolls: the pixel (x % 32, y % 32) of the worm cell's tile as it is
        // now (0x45d884..0x45d8dd), darkened when drawn (FUN_0045d650).
        const int32_t tile = map_.at(x >> 5, y >> 5).tile, pixel = ((y & 31) << 5) | (x & 31);
        const int life1 = int(rng_.next() % 30) + 10;
        auto add = [&](float pvx, float pvy, int life) {
            if (particles.size() >= 4096) return; // the list is full: dropped
            particles.push_back({float(x + dx), float(y + dy), pvx, pvy, life, tile, pixel});
        };
        add(vx, vy, life1);
        const int life2 = int(rng_.next() % 30) + 50; // rolled after the first add (0x45d941)
        add(0.0f, 0.0f, life2);
    }
}

void World::tick_particles() {
    // FUN_0045d4f0: once per tick.
    for (auto& p : particles) {
        if (p.life == 0) continue;
        p.x += p.vx;
        p.y += p.vy;
        --p.life;
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0; }),
                    particles.end());
}

void World::tick_worm(Unit& u) {
    switch (u.state) {
    case ws::kSleep:
        u.flags |= kSubmerged;
        if (u.timer != 0) {
            if (++u.turret_counter > 100) { // +0x1b sub-counter
                u.turret_counter = 0;
                --u.timer;
            }
            return;
        }
        u.hit_points = templates_.unit_records[size_t(u.record)].hit_points;
        u.dest_x = uint8_t(rng_.next() % uint32_t(map_.width())); // x first, then y
        u.dest_y = uint8_t(rng_.next() % uint32_t(map_.height()));
        set_state(u, ws::kTravel);
        return;

    case ws::kTravel: {
        const auto& r = templates_.unit_records[size_t(u.record)];
        if (u.hit_points < (r.hit_points >> 1)) {
            set_state(u, ws::kSleep);
            u.timer = uint8_t(vars.worm_shot_sleep);
            return;
        }
        if (move_step(u) == 0) {
            bool infantry = false;
            const int vi = unit_in_cell(u.cur_x, u.cur_y, infantry);
            if (vi < 0 || infantry) {
                u.flags &= ~kSubmerged;
                u.timer = 0;
                set_state(u, ws::kDecide);
                return;
            }
            // Eat (doc 17 section 3.2).
            Unit& v = units_[size_t(vi)];
            const int owner = v.owner;
            set_state(v, 0x11);
            // The eat code does not write +0x2f: it keeps the value unit
            // create gave it (10 for a vehicle, 0x4559ba; no other writer of
            // a unit's +0x2f). The victim stays drawn and keeps its cell bit
            // 8 until the state-0x11 removal update (docs/re/17 section 3.2).
            v.countdown = 10;
            count_unit_lost(owner, v.record); // 0x49ab81: +0x24a08, +0x24a10[rec]
            u.timer = 50;
            u.flags &= ~kSubmerged;
            set_state(u, ws::kDecide);
            const int ex = r.death_explosion;
            if (ex >= 0 && templates_.explosion_sound[size_t(ex)] >= 0) {
                SoundEvent s;
                s.sample = templates_.explosion_sound[size_t(ex)];
                s.cx = u.cur_x;
                s.cy = u.cur_y;
                sounds.push_back(s);
            }
            if (owner == local_player) voice(owner, "WORMATTACK");
            if (ex >= 0) {
                spawn_effect(templates_.explosion_name[size_t(ex)].c_str(), u.x >> 16, u.y >> 16, u.owner);
                spawn_effect(templates_.explosion_name[size_t(ex + 1)].c_str(), u.x >> 16, u.y >> 16, u.owner);
            }
            ++u.load; // meals (+0x29), never reset
            return;
        }
        if (u.blocked == 0) {
            if (!(u.flags & kSubmerged) && (u.half == 0 || u.steps < 2 * u.half)) {
                ripple(u.x >> 16, u.y >> 16);
                if (rng_.next() % 80 == 0) worm_sign(u);
            }
        } else {
            u.flags &= ~kSubmerged;
            u.blocked = 0;
            u.timer = 0;
            int x, y;
            if (free_adjacent_cell(u, u.cur_x, u.cur_y, 0, x, y)) { // FUN_0045dee0 (sand, no worm)
                u.dest_x = uint8_t(x);
                u.dest_y = uint8_t(y);
            } else {
                set_state(u, ws::kSleep);
                u.timer = uint8_t(vars.worm_shot_sleep);
            }
        }
        return;
    }

    case ws::kDecide: {
        if (u.timer != 0) {
            --u.timer;
            return;
        }
        int tx, ty, vx, vy;
        if (nearest_thumper(u.cur_x, u.cur_y, tx, ty)) {
            if (nearest_vehicle_on_sand(tx, ty, vx, vy) && std::abs(tx - vx) < 5 && std::abs(ty - vy) < 5) {
                tx = vx;
                ty = vy;
            }
            set_state(u, ws::kTravel);
            u.dest_x = uint8_t(tx);
            u.dest_y = uint8_t(ty);
            return;
        }
        if (nearest_vehicle_on_sand(u.cur_x, u.cur_y, vx, vy)) {
            if (u.load < vars.worm_appetite) {
                set_state(u, ws::kTravel);
                u.dest_x = uint8_t(vx);
                u.dest_y = uint8_t(vy);
            } else {
                set_state(u, ws::kSleep);
                u.timer = uint8_t(vars.worm_fed_sleep);
            }
            return;
        }
        const uint8_t x = uint8_t(rng_.next() % uint32_t(map_.width()));
        const uint8_t y = uint8_t(rng_.next() % uint32_t(map_.height()));
        set_state(u, ws::kTravel);
        u.dest_x = x;
        u.dest_y = y;
        return;
    }
    default:
        return;
    }
}

void World::worm_sign(const Unit& u) {
    // Doc 17 section 4.2: EX_WORMSN1..6, then the speech when on screen and
    // the cell is open.
    const std::string name = "EX_WORMSN" + std::to_string(1 + int(rng_.next() % 6));
    spawn_effect(name.c_str(), u.x >> 16, u.y >> 16, u.owner);
    const int px = u.x >> 16, py = u.y >> 16;
    if (px < view_x || px > view_x + 480 || py < view_y || py > view_y + 380) return;
    if (map_.at(u.cur_x, u.cur_y).state != 0) return;
    voice(local_player, "WORMSIGN", 2000);
    const int e = templates_.explosion_index(name);
    if (e >= 0 && templates_.explosion_sound[size_t(e)] >= 0) {
        SoundEvent s; // only if that sample is not playing on any channel (FUN_0046f4f0, 0x49af0b)
        s.sample = templates_.explosion_sound[size_t(e)];
        s.unique = true;
        s.cx = u.cur_x;
        s.cy = u.cur_y;
        sounds.push_back(s);
    }
}

} // namespace d2k::sim
