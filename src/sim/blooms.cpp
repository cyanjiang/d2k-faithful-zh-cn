// Spice blooms and the cell-object table they share with crates.
// RE: docs/re/31-spice-blooms.md.
#include "sim/world.hpp"

#include <cmath>

namespace d2k::sim {

namespace {
// The flight step tables (FUN_0044b090): trunc(sin / cos(angle) * s * 0x800).
int32_t step_table(bool cos_axis, int facing, int s) {
    const double a = double(facing & 31) * 6.283185308 * 0.03125;
    return int32_t((cos_axis ? std::cos(a) : std::sin(a)) * double(s * 0x800));
}
} // namespace

int World::find_cell_object(int x, int y, int& index) const {
    const auto& t = map_.objects();
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i].active && t[i].x == x && t[i].y == y) {
            index = int(i);
            return t[i].type;
        }
    }
    return 0xff;
}

void World::bloom_rearm(int i) {
    // FUN_0044f110: back to an unripe bloom while bursts are left, else gone.
    auto& e = map_.objects()[size_t(i)];
    if (e.regrow != 0) {
        --e.regrow;
        e.image = 4;
        e.timer = rng_.next() % 1000 + 2000;
        e.type = 7;
    } else {
        e.active = 0;
        map_.at(e.x, e.y).flags &= ~0x1000u;
    }
}

int World::pick_up_cell_object(int x, int y) {
    // FUN_0044e9d0: unripe blooms stay; ripe ones re-arm (or go); crates go.
    auto& t = map_.objects();
    for (size_t i = 0; i < t.size(); ++i) {
        auto& e = t[i];
        if (!e.active || e.x != x || e.y != y) continue;
        const int type = e.type;
        if (type == 7) return 7;
        if (type >= 8 && type <= 10) {
            bloom_rearm(int(i));
        } else {
            e.active = 0;
            map_.at(x, y).flags &= ~0x1000u;
        }
        return type;
    }
    return 0x0b;
}

int World::drive_over(Unit& u) {
    // FUN_0049ebf0: any ground unit but worms, on its "to" cell; the
    // re-arm comes before the burst here (docs/re/31 section 6.2). Crates:
    // docs/re/54 section 3. Returns 1 when units were created on the cell.
    if (templates_.unit_records[size_t(u.record)].kind == 5) return 0;
    const int tx = u.to_x, ty = u.to_y;
    if (tx >= map_.width() || ty >= map_.height() || !(map_.at(tx, ty).flags & 0x1000)) return 0;
    const int side = u.owner;
    const int t = pick_up_cell_object(tx, ty);
    if (t >= 8 && t <= 10) bloom_burst(tx, ty, t - 4);
    if (t > 5) return 0; // 7 (an unripe bloom) and 0x0b: nothing
    const auto* ids = templates_.block(0x6d60d8);
    auto id = [&](uint32_t addr) { return ids && ids->bytes.size() > addr - 0x6d60d8 ? int(ids->bytes[addr - 0x6d60d8]) : 0xff; };
    const int px = tx * 32 + 16, py = ty * 32 + 16;
    const bool local = side == local_player;
    // The explosion for everyone (owner = the picker's side, height 0, no
    // flags); its sound only for the local side, at the crate cell.
    auto boom = [&](int ex) {
        if (ex == 0xff) return;
        spawn_effect_at(ex, px, py, side, 0, 0, 0);
        if (local) play_at(templates_.explosion_sound[size_t(ex) & 63], tx, ty);
    };
    switch (t) {
    case 0: // cash: VARS CrateCash into the pending cash
        refund(side, vars.crate_cash);
        boom(id(0x6d610e)); // EX_CASH
        break;
    case 1: { // explosion: EX_WHITEN1 (no 0xFF test), then the CRATE bullet's damage
        const int ex = id(0x6d6112);
        spawn_effect_at(ex, px, py, side, 0, 0, 0);
        if (local) play_at(templates_.explosion_sound[size_t(ex) & 63], tx, ty);
        const int crate = id(0x6d60ef);
        if (crate != 0xff) damage_at(px, py, 0, crate, side, -1, false);
        break;
    }
    case 2: // reveal map
        boom(id(0x6d610d)); // EX_CRATEREVEAL
        if (local) {
            // FUN_0044c480 (every shroud byte 0), and the true radar shown.
            open_map();
            for (int y = 0; y < map_.height(); ++y) {
                for (int x = 0; x < map_.width(); ++x) map_.at(x, y).radar_known = true;
            }
        }
        break;
    case 3: // no map
        boom(id(0x6d610f)); // EX_CRATENOMAP
        if (local) {
            // The shown radar black, every shroud byte 1, then FUN_0049f4d0.
            for (int y = 0; y < map_.height(); ++y) {
                for (int x = 0; x < map_.width(); ++x) {
                    map_.at(x, y).state = 1;
                    map_.at(x, y).radar_known = false;
                }
            }
            reveal_start();
        }
        break;
    case 4: { // units: one vehicle, or five infantry each rolled again
        const auto& urs = templates_.unit_records;
        const auto* nb = templates_.block(0x6d9b50);
        const int names = nb && !nb->bytes.empty() ? nb->bytes[0] : 0; // the unit name count, 26 in 1.06
        if (names == 0) break;
        const int house = players_[size_t(side)].house;
        auto kind_of = [&](int rec) { return rec >= 0 ? urs[size_t(rec)].kind : 5; };
        int rec;
        do {
            do rec = templates_.unit_record(int(rng_.next() % uint32_t(names)), house); // line 0x22e0
            while (kind_of(rec) == 5);
        } while (kind_of(rec) == 8 || kind_of(rec) == 9 || kind_of(rec) == 2 || kind_of(rec) == 10);
        if (!urs[size_t(rec)].infantry) {
            create_unit(side, rec, tx, ty, tx, ty);
        } else {
            for (int k = 0; k < 5; ++k) {
                do rec = templates_.unit_record(int(rng_.next() % uint32_t(names)), house); // line 0x22eb
                while (rec < 0 || !urs[size_t(rec)].infantry);
                create_unit(side, rec, tx, ty, tx, ty);
            }
        }
        return 1;
    }
    case 5: // stealth (FUN_0049f1b0, docs/re/32 section 1.8)
        for (auto& v : units_) {
            if (v.owner != side) continue;
            const int dx = int(v.cur_x) - tx, dy = int(v.cur_y) - ty;
            if (dx * dx + dy * dy >= 9) continue;
            if (templates_.unit_records[size_t(v.record)].kind == 4) {
                v.load = 0xa0; // a saboteur: a full charge
            } else if (!is_stealthed(v)) {
                v.fade = 0;
                v.flags |= 0x10;
            }
        }
        boom(id(0x6d6110)); // EX_CRATESTEALTH
        break;
    default:
        break;
    }
    return 0;
}

void World::add_random_crate() {
    // FUN_0044e7c0 / FUN_0044e800 (docs/re/54 section 1.2): x, y until the
    // cell is clear walkable ground (no object, unit or building), then
    // the type from the thresholds 0x4e0e18; lifetime 5000 ticks. The
    // rand() calls run even when the table is full (nothing is added).
    static const int kThreshold[6] = {50, 65, 70, 75, 95, 100};
    int x = 0, y = 0;
    for (int tries = 0;; ++tries) {
        x = int(rng_.next() % uint32_t(map_.width()));  // line 0x751
        y = int(rng_.next() % uint32_t(map_.height())); // line 0x752
        if ((map_.at(x, y).flags & 0x73f8) == 0x6000) break;
        if (tries > 100000) return; // the exe loops forever (crash fix)
    }
    const int r = int(rng_.next() % 100); // line 0x755
    int type = 0;
    while (type < 5 && r >= kThreshold[type]) ++type;
    map_.add_object(x, y, 5000, type, 0, 0);
}

void World::bloom_burst(int x, int y, int n) {
    // FUN_0044ef10: 2 n^2 tries; each draws three rand() (direction, delay,
    // distance); a sand / spice target cell (not row / column 0) takes a
    // SpiceExpl blob, owners round-robin over the active sides from side 1,
    // with the y jitter drawn before the x jitter.
    const auto* ids = templates_.block(0x6d60d8);
    const int blob = ids && ids->bytes.size() > 0x32 ? ids->bytes[0x32] : -1; // 0x6d610a
    int side = 0;
    for (int k = 0; k < 2 * n * n; ++k) {
        const uint32_t r1 = rng_.next(), r2 = rng_.next(), r3 = rng_.next();
        const int a = int(r1 & 0x1f);
        const int s = int(r3 % uint32_t(n));
        const int tx = x + step_table(false, a, s) / 2048; // C division toward zero
        const int ty = y + step_table(true, a, s) / 2048;
        if (!(tx > 0 && tx < map_.width() && ty > 0 && ty < map_.height())) continue;
        if (!(map_.at(tx, ty).flags & 0x710000)) continue;
        int guard = 0;
        do side = (side + 1) & 7;
        while (!player_active(side) && ++guard < 8);
        const uint32_t jy = rng_.next() & 0x1f;
        const uint32_t jx = rng_.next() & 0x1f;
        if (blob < 0) continue;
        const int bi = add_bullet(side, blob, -1, x * 32 + 16, y * 32 + 16, tx * 32 + int(jx), ty * 32 + int(jy), -1);
        if (bi >= 0) bullets[size_t(bi)].delay = int32_t(r2 & 0xf);
    }
    if (local_player >= 0) sounds.push_back({"S_SPICEMOUND", x, y, false}); // positional, once
}

void World::spice_lands(int px, int py) {
    // FUN_0049be60, SpiceExpl impact: level 4 when the whole 3 x 3 has
    // spice, else level 2 (radar updated); tile 0x31a, craters cleared,
    // the 3 x 3 retiled.
    const int tx = px >> 5, ty = py >> 5;
    auto& c = map_.at(tx, ty);
    if (!(c.flags & cell::kSand)) {
        return; // the exe's fatal "Spice falling onto non sandy surface" (crash fix: ignore)
    }
    auto has = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < map_.width() && y < map_.height() && (map_.at(x, y).flags & cell::kSpiceLevelMask);
    };
    bool thick = true;
    for (int y = ty - 1; y <= ty + 1; ++y) {
        for (int x = tx - 1; x <= tx + 1; ++x) thick = thick && has(x, y);
    }
    c.tile = 0x31a;
    c.marks = 0;
    if (thick) {
        c.flags = (c.flags & ~0x300000u) | 0x400000u;
    } else {
        c.flags = (c.flags & ~0x500000u) | 0x200000u;
        c.radar = radar_colour::kSpice; // 0x49c8ac: the thin branch only
    }
    map_.smooth_spice({tx - 1, ty - 1, tx + 2, ty + 2});
}

void World::tick_cell_objects() {
    // FUN_0044ea70, every tick: timers; unripe blooms grow on a clear cell
    // (no spice, no unit); a ripe large bloom on a clear cell bursts.
    const auto* ids = templates_.block(0x6d60d8);
    const int ex = ids && ids->bytes.size() > 0x3f ? ids->bytes[0x3f] : 0xff; // EX_SPICEBLOOM 0x6d6117
    auto& t = map_.objects();
    int crates = 0; // active entries of a crate type (< 6), before their timers run
    for (const auto& e : t) crates += e.active == 1 && e.type < 6;
    for (size_t i = 0; i < t.size(); ++i) {
        auto& e = t[i];
        if (e.active != 1) continue;
        if (--e.timer != 0) continue;
        auto& c = map_.at(e.x, e.y);
        if (e.type < 7 || e.type > 10) { // a crate expires
            e.active = 0;
            c.flags &= ~0x1000u;
        } else if (e.type <= 9) {
            if ((c.flags & 0x7003f8) == 0) {
                ++e.image;
                ++e.type;
                // The owner is tick & 7 (the nearby side search's result is
                // unused). 0x798628 (the side count) is 8 in every game: the
                // set-up creates sides 0..7 (0x448826..0x448835) and a load
                // restores the saved 8, so every side's effect list is
                // updated and drawn (docs/re/31 section 3).
                if (ex != 0xff) spawn_effect_at(ex, e.x * 32 + 16, e.y * 32, int(tick_count_ & 7), 0, 0, 0);
            }
            e.timer = rng_.next() % 1000 + 2000;
        } else if ((c.flags & 0x7003f8) == 0) {
            bloom_burst(e.x, e.y, 6);
            bloom_rearm(int(i));
        } else {
            e.timer = rng_.next() % 1000 + 2000;
        }
    }
    // At most one new crate per tick when there are fewer than max_crates
    // (0 in the campaign), counted before the timers ran (docs/re/54 1.2).
    if (crates < max_crates) add_random_crate();
}

} // namespace d2k::sim
