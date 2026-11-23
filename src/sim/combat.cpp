#include <cstdio>
#include <cstdlib>
// Combat: target search, firing, bullets, damage, deaths, the attack and
// guard states, and turret buildings. RE: docs/re/18-combat.md.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include "sim/world.hpp"

namespace d2k::sim {

namespace {
constexpr uint32_t kHit = 1, kFired = 2;
constexpr uint32_t kAircraft = 0x40, kCarried = 0x100;
constexpr uint32_t kUnitBits = 0x3e8;
constexpr uint32_t kWormBit = 0x1000000;
constexpr int32_t kPx = 0x10000;

// The flight / muzzle step tables (FUN_0044b090): sin / cos per facing.
int32_t step_dx(int facing, int px) { return int32_t(std::sin(double(facing & 31) * 6.283185308 * 0.03125) * double(px * 32 * 0x800)); }
int32_t step_dy(int facing, int px) { return int32_t(std::cos(double(facing & 31) * 6.283185308 * 0.03125) * double(px * 32 * 0x800)); }

// The search spiral (0x4eb2d8 / 0x4eb2dc / 0x4eb2e0).
constexpr int kSpiralDx[4] = {-1, 0, 1, 0}, kSpiralDy[4] = {0, 1, 0, -1}, kSpiralGrow[4] = {0, 1, 0, 1};

// Infantry animation modes (0x4e9530): first and last frame.
constexpr int kInfModes[16][2] = {{0, 0},   {1, 6},   {7, 11},  {12, 12}, {13, 13}, {14, 16}, {17, 21}, {12, 12},
                                  {7, 11},  {22, 33}, {22, 29}, {22, 29}, {22, 29}, {22, 29}, {22, 29}, {22, 29}};
} // namespace

int facing32(int ax, int ay, int bx, int by) {
    return ((angle12(ax - bx, by - ay) + 0x40) & 0xfff) >> 7; // FUN_0045dfe0
}

void World::set_armour(std::span<const uint8_t> data) { armour_ = formats::ArmourTable::parse(data); }

void World::set_smoke_points(std::span<const uint8_t> data) {
    // FUN_0046ad80: 100 rows of 33 bytes: n, x[8] (i8), y[8] (i8), type[8], period[8].
    if (data.size() != 3300) throw std::runtime_error("BUILEXP.BIN: expected 3300 bytes");
    smoke_points_.assign(100, SmokePoints{});
    for (size_t row = 0; row < 100; ++row) {
        const uint8_t* p = data.data() + row * 33;
        auto& s = smoke_points_[row];
        s.n = std::min<uint8_t>(p[0], 8);
        for (int i = 0; i < 8; ++i) {
            s.x[i] = int8_t(p[1 + i]);
            s.y[i] = int8_t(p[9 + i]);
            s.type[i] = p[17 + i];
            s.period[i] = p[25 + i];
        }
    }
}

bool World::enemy(int a, int b) const { return a >= 0 && a < 8 && b >= 0 && b < 8 && alliance_[size_t(a)][size_t(b)] == 1; }

bool World::building_alive(int b) const {
    return b >= 0 && size_t(b) < buildings_.size() && !buildings_[size_t(b)].removed;
}

int World::bullet_range(int w) const {
    return w >= 0 && w != 0xff && size_t(w) < templates_.bullets.size() ? templates_.bullets[size_t(w)].range : 0;
}

int World::percent(int weapon, int armour) const {
    const auto& b = templates_.bullets[size_t(weapon)];
    if (b.warhead >= armour_.warheads.size()) return 100;
    return armour_.warheads[b.warhead].percent[size_t(armour) % 12];
}

int World::choose_weapon(int prim, int sec, int64_t dist2, bool air_target, int armour) const {
    // FUN_0049e9d0.
    auto eff = [&](int w) { return int64_t(percent(w, armour)) * templates_.bullets[size_t(w)].damage; };
    const bool has_sec = sec != 0xff && size_t(sec) < templates_.bullets.size();
    if (air_target) {
        if (templates_.bullets[size_t(prim)].anti_air) return prim;
        if (has_sec && templates_.bullets[size_t(sec)].anti_air) return sec;
        return prim;
    }
    if (!has_sec) return prim;
    const int64_t rp = bullet_range(prim), rs = bullet_range(sec);
    if (rp < rs) return (dist2 <= rp * rp && eff(prim) > eff(sec)) ? prim : sec;
    return (dist2 <= rs * rs && eff(sec) >= eff(prim)) ? sec : prim;
}

bool World::can_attack_cell(const Unit& u, int cx, int cy, bool air) const {
    // FUN_00497270 / FUN_004970f0: cell centre to cell centre, inclusive.
    const auto& r = templates_.unit_records[size_t(u.record)];
    int range = 0;
    if (air) {
        if (r.weapon != 0xff && templates_.bullets[r.weapon].anti_air) range = bullet_range(r.weapon);
        if (r.weapon2 != 0xff && templates_.bullets[r.weapon2].anti_air) range = std::max(range, bullet_range(r.weapon2));
        if (range == 0) return false;
    } else {
        range = std::max(bullet_range(r.weapon), bullet_range(r.weapon2));
    }
    const int64_t dx = (int64_t(cx) - u.cur_x) * 32, dy = (int64_t(cy) - u.cur_y) * 32;
    return dx * dx + dy * dy <= int64_t(range) * range;
}

int World::target_unit_in_range(int px, int py, int side, int r, int prim, int sec, bool air, bool worms) const {
    // FUN_004a4900: the ground pass, then (only with r == 0) the air pass
    // and the worm pass; spiral from the start cell, which is not tested.
    // Returns a unit id or -1.
    const bool any_r = r == 0;
    if (r == 0) r = std::max(bullet_range(prim), bullet_range(sec));
    int n = (2 * r) / 32 + 2;
    int64_t r2 = int64_t(r) * r;
    auto spiral = [&](auto test) -> int {
        int x = px >> 5, y = py >> 5, leg = 1, left = 1, dir = 0;
        while (leg < n) {
            x += kSpiralDx[dir];
            y += kSpiralDy[dir];
            if (--left == 0) {
                leg += kSpiralGrow[dir];
                left = leg;
                dir = (dir + 1) & 3;
            }
            if (x < 0 || y < 0 || x >= map_.width() || y >= map_.height()) continue;
            const int found = test(x, y);
            if (found >= 0) return found;
        }
        return -1;
    };
    auto in_r = [&](int x, int y) {
        const int64_t dx = int64_t(x * 32 + 16) - px, dy = int64_t(y * 32 + 16) - py;
        return dx * dx + dy * dy <= r2;
    };
    int found = spiral([&](int x, int y) -> int {
        const uint32_t f = map_.at(x, y).flags;
        if (!(f & kUnitBits) || !enemy(side, int(f & 7))) return -1;
        bool inf = false;
        const int ui = unit_in_cell(x, y, inf, true); // includeCloaked = 0 for every caller
        if (ui < 0 || units_[size_t(ui)].state == 0x11 || !in_r(x, y)) return -1;
        return units_[size_t(ui)].index;
    });
    if (found >= 0 || !any_r) return found;
    const auto anti_air = [&](int w) { return w != 0xff && w >= 0 && templates_.bullets[size_t(w)].anti_air; };
    if (air && (anti_air(prim) || anti_air(sec))) {
        // The range: both anti-air: the larger; only the primary: its own;
        // only the secondary: the ground pass's max (quirk, kept).
        if (sec != 0xff && sec >= 0 && anti_air(prim) && anti_air(sec)) {
            r = std::max(bullet_range(prim), bullet_range(sec));
        } else if (anti_air(prim)) {
            r = bullet_range(prim);
        }
        n = (2 * r) / 32 + 2;
        r2 = int64_t(r) * r;
        const auto* ids = templates_.block(0x6d60d8);
        const int frigate = ids && ids->bytes.size() > 14 ? ids->bytes[14] : -1; // 0x6d60e6
        found = spiral([&](int x, int y) -> int {
            if (!(map_.at(x, y).flags & 0x800000) || !in_r(x, y)) return -1;
            // Enemy aircraft whose current or next column and row are here
            // (mixed pairs count), side by side in list order.
            for (int p = 0; p < 8; ++p) {
                if (!enemy(side, p)) continue;
                for (const auto& u : units_) {
                    if (u.owner != p || !(u.flags & 0x40) || u.record == frigate) continue;
                    if ((u.to_x == x || u.cur_x == x) && (u.cur_y == y || u.to_y == y)) return u.index;
                }
            }
            return -1;
        });
        if (found >= 0) return found;
    }
    if (!worms) return -1;
    r = std::max(bullet_range(prim), bullet_range(sec));
    n = (2 * r) / 32 + 2;
    r2 = int64_t(r) * r;
    return spiral([&](int x, int y) -> int {
        if (!(map_.at(x, y).flags & kWormBit) || !in_r(x, y)) return -1;
        for (const auto& u : units_) {
            if (templates_.unit_records[size_t(u.record)].kind == 5 && u.state == 0x1b && !(u.flags & 4) &&
                u.cur_x == x && u.cur_y == y) {
                return u.index;
            }
        }
        return -1;
    });
}

int World::target_building_in_range(int cx, int cy, int side, int r, bool turrets_only, int& tx, int& ty) const {
    // FUN_004a4fd0: start cell tested first; enemy building cells, no walls.
    const int n = (2 * r) / 32 + 2;
    auto test = [&](int x, int y) -> int {
        if (x < 0 || y < 0 || x >= map_.width() || y >= map_.height()) return -1;
        const uint32_t f = map_.at(x, y).flags;
        if (!(f & 0x10) || !enemy(side, int(f & 7))) return -1;
        const int b = building_at_cell(x, y);
        if (b < 0) return -1;
        const int kind = templates_.building_records[size_t(buildings_[size_t(b)].record)].kind;
        if (kind == 0xe || (turrets_only && kind != 0x10)) return -1;
        tx = x;
        ty = y;
        return b;
    };
    int found = test(cx, cy);
    if (found >= 0) return found;
    int x = cx, y = cy, leg = 1, left = 1, dir = 0;
    while (leg < n) {
        x += kSpiralDx[dir];
        y += kSpiralDy[dir];
        if (--left == 0) {
            leg += kSpiralGrow[dir];
            left = leg;
            dir = (dir + 1) & 3;
        }
        found = test(x, y);
        if (found >= 0) return found;
    }
    return -1;
}

int World::building_at_cell(int x, int y) const {
    // FUN_004a5640: a building cell (0x10), searched in the list of the
    // cell's owner (so a captured building's old object is not found),
    // skipping kind 0xF (concrete); the first whose footprint covers the
    // cell (FUN_004a5570). No pixel test (docs/re/10 "Hit test").
    if (x < 0 || y < 0 || x >= map_.width() || y >= map_.height()) return -1;
    const uint32_t f = map_.at(x, y).flags;
    if (!(f & 0x10)) return -1;
    for (const auto& b : buildings_) {
        if (b.removed || b.owner != int(f & 7)) continue;
        const auto& r = templates_.building_records[size_t(b.record)];
        if (r.kind == 0xf) continue;
        const int col = x - b.cell_x, row = y - b.cell_y;
        if (col >= 0 && col < 4 && row >= 0 && row < 4 && (r.footprint & (1u << (row * 4 + col)))) return b.index;
    }
    return -1;
}

void World::closest_cell_manhattan(int building, int x, int y, int& cx, int& cy) const {
    // FUN_0049d170 "ClosestBuildingTile" (damage): Manhattan distance, the
    // first cell wins ties (strict <).
    const auto& b = buildings_[size_t(building)];
    const auto& r = templates_.building_records[size_t(b.record)];
    int best = 0x800;
    cx = b.cell_x;
    cy = b.cell_y;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (!(r.footprint & (1u << (row * 4 + col)))) continue;
            const int d = std::abs(b.cell_y + row - y) + std::abs(b.cell_x + col - x);
            if (d < best) {
                best = d;
                cx = b.cell_x + col;
                cy = b.cell_y + row;
            }
        }
    }
}

void World::closest_cell(int building, int x, int y, int& cx, int& cy) const {
    // FUN_00497390 / FUN_0049d170: the occupied cell closest to (x, y) by
    // squared distance; once 0 is found, every later cell replaces it (quirk).
    const auto& b = buildings_[size_t(building)];
    const auto& r = templates_.building_records[size_t(b.record)];
    int64_t best = -1;
    cx = b.cell_x;
    cy = b.cell_y;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (!(r.footprint & (1u << (row * 4 + col)))) continue;
            const int64_t dx = b.cell_x + col - x, dy = b.cell_y + row - y, d = dx * dx + dy * dy;
            if (best < 0 || d < best || best == 0) {
                best = d;
                cx = b.cell_x + col;
                cy = b.cell_y + row;
            }
        }
    }
}

void World::spawn_effect_at(int e, int32_t x, int32_t y, int owner, int frame, uint32_t caller_flags, int delay,
                            int32_t height, int32_t move_x, int32_t move_y) {
    if (e < 0 || e >= 64) return;
    const int slot = pool_pop(owner);
    if (slot < 0) return; // FUN_00456df0: nothing created (docs/re/50)
    Effect fx;
    fx.slot = uint16_t(slot);
    fx.explosion = e;
    fx.frame = frame;
    fx.flags = templates_.explosion_flags[size_t(e)] ^ caller_flags;
    fx.counter = (caller_flags & 0x20) ? delay : 0;
    fx.x = x;
    fx.y = y;
    fx.height = height >> 16;
    fx.owner = owner;
    if (fx.flags & 0x1000) { // dx / dy stored only with the final flag
        fx.move_x = move_x;
        fx.move_y = move_y;
    }
    effects.push_back(fx);
}

void World::play_at(int sample, int cx, int cy) {
    if (sample < 0) return;
    SoundEvent s;
    s.sample = sample;
    s.cx = cx;
    s.cy = cy;
    sounds.push_back(s);
}

void World::shooter_reveal(int cx, int cy) {
    // Doc 16 section 3.5.
    if (cx < 0 || cy < 0 || cx >= map_.width() || cy >= map_.height()) return;
    map_.at(cx, cy).state = 0;
    edge_pass(cx - 2, cy - 2, cx + 2, cy + 2);
}

// ---- bullets --------------------------------------------------------------------

int World::add_bullet(int side, int type, int firer, int x0, int y0, int tx, int ty, int target) {
    // FUN_00456a30.
    const int slot = pool_pop(side);
    if (slot < 0) return -1; // no message (docs/re/50)
    const auto& br = templates_.bullets[size_t(type)];
    Bullet b;
    b.slot = uint16_t(slot);
    b.type = type;
    b.owner = side;
    b.firer = firer;
    b.target = target;
    b.flags = br.flags;
    b.speed = br.speed;
    // The points are u16 and the maths is 32-bit and wraps (docs/re/41
    // section 6.1): a negative sum of squares gives NaN, which _ftol turns
    // into distance 0. The Death Hand's bomblets depend on it.
    const int ux0 = x0 & 0xffff, uy0 = y0 & 0xffff, utx = tx & 0xffff, uty = ty & 0xffff;
    b.tx = utx;
    b.ty = uty;
    b.x = ux0 * kPx;
    b.y = uy0 * kPx;
    b.height = 6 * kPx;
    const int32_t ddx = utx - ux0, ddy = uty - uy0;
    const int32_t q = int32_t(uint32_t(ddx) * uint32_t(ddx) + uint32_t(ddy) * uint32_t(ddy));
    const int dist = q < 0 ? 0 : int(std::sqrt(double(q)));
    b.ticks = br.speed ? int32_t(uint32_t(dist) << 16) / br.speed : 0;
    if (b.flags & 0x800) b.ticks = br.speed ? int32_t((int64_t(br.range) << 16) / br.speed) : 0;
    if (dist == 0) {
        if (!(b.flags & 0x100)) {
            return -1; // the slot was popped but is never linked or freed: lost (docs/re/50 section 4)
        }
    } else {
        b.dx = int32_t(uint32_t(br.speed) * uint32_t(ddx)) / dist;
        b.dy = int32_t(uint32_t(br.speed) * uint32_t(ddy)) / dist;
    }
    if (b.flags & 1) {
        b.vz = -(b.ticks * 0x2000) / 2;
        b.flags |= 2;
    }
    if (b.flags & 0x100) {
        // Falls from the firer's altitude (object +0x3c). For a building
        // firer the exe reads the building's bytes there; every caller with
        // a building firer (debris) overwrites the height.
        b.flags |= 2;
        b.vz = 0;
        const int fu = firer >= 0 ? find_unit(firer) : -1;
        b.height = fu >= 0 ? units_[size_t(fu)].altitude : 0;
    }
    if (b.flags & 0x20) b.ticks *= 2;
    const int frames = templates_.art.bullet_frames[size_t(br.image) % 64];
    b.frame = (frames < 2 || (b.flags & 0x80)) ? 0 : facing32(x0, y0, tx, ty) / (32 / frames);
    b.rot = b.last_rot = b.frame;
    bullets.push_back(b);
    return int(bullets.size()) - 1;
}

void World::tick_shake() {
    // FUN_00446690, in the tick block: while the counter runs, one rand()
    // per tick picks the offset -2..2; 0 when it ends.
    if (shake_ticks == 0) return;
    if (--shake_ticks != 0) {
        shake_y = int(rng_.next() % 5) - 2;
    } else {
        shake_y = 0;
    }
}

void World::tick_bullets(int p) {
    // FUN_0049be60 for the bullets of side p, in list order (World::tick:
    // an appended bullet runs in this pass unless the current one was the
    // last when its update began). `bullets` is reserved for the whole pool,
    // so `b` stays valid when the damage adds debris. A bullet that ends is
    // freed right after its update (FUN_00458e90, docs/re/50).
    for (size_t bi = 0; bi < bullets.size(); ++bi) {
        if (bullets[bi].owner != p || bullets[bi].dead) continue;
        bool more = false;
        for (size_t j = bullets.size(); j-- > bi + 1 && !more;) more = bullets[j].owner == p && !bullets[j].dead;
        tick_bullet(bullets[bi]);
        if (bullets[bi].dead) pool_push(p, bullets[bi].slot);
        if (!more) break;
    }
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return b.dead; }),
                  bullets.end());
}

void World::tick_bullet(Bullet& b) {
    // FUN_0049be60 head (0x49be92..0x49beee), every update before the delay:
    // the firer (+0x14, a slot of the bullet's own side) is dropped when it
    // is dying (a unit or building in state 0x11) or no longer a unit or
    // building. The impact then records no attacker (docs/re/50 section 6).
    if (b.firer >= 0) {
        const int fi = find_unit(b.firer);
        if (fi < 0 || units_[size_t(fi)].state == 0x11) b.firer = -1;
    } else if (b.firer <= -1000) {
        const size_t bi = size_t(-1000 - b.firer);
        if (bi >= buildings_.size() || buildings_[bi].removed || buildings_[bi].dying) b.firer = -1;
    }
    {
        if (b.delay > 0) {
            --b.delay;
            return;
        }
        const int cx = (b.x >> 16) >> 5, cy = (b.y >> 16) >> 5;
        if (b.x < 0 || b.y < 0 || cx >= map_.width() || cy >= map_.height()) {
            b.dead = true;
            return;
        }
        const auto& br = templates_.bullets[size_t(b.type)];
        if ((b.flags & 0x1000) && (map_.at(cx, cy).flags & 0x400)) {
            // Detonates over a wall / turret cell: damage and the impact
            // explosion at the bullet's own pixel and height; removed.
            b.dead = true;
            damage_at(b.x >> 16, b.y >> 16, b.height, b.type, b.owner, b.firer, (b.flags & 0x40) != 0);
            if (br.impact >= 0) {
                play_at(templates_.explosion_sound[size_t(br.impact)], cx, cy);
                spawn_effect_at(br.impact, b.x >> 16, b.y >> 16, b.owner, 0, 0, 0, b.height);
            }
            return;
        }
        if (b.flags & 0x800) {
            // Sonic (docs/re/36 section 2.2): in the second half of the fixed
            // flight, damage at the current pixel before the move, every tick.
            const uint32_t T = br.speed ? uint32_t((uint64_t(uint32_t(br.range)) << 16) / uint32_t(br.speed)) : 0;
            if ((uint32_t(b.ticks) << 1) < T) damage_at(b.x >> 16, b.y >> 16, b.height, b.type, b.owner, b.firer);
        }
        if ((b.flags & 0x10) && br.trail >= 0) spawn_effect_at(br.trail, b.x >> 16, b.y >> 16, b.owner, 0, 0, 0, b.height);
        if (b.flags & 0x20) {
            tick_homing(b);
            return;
        }
        if (b.flags & 0x80) {
            const int frames = std::max(1, templates_.art.bullet_frames[size_t(br.image) % 64]);
            b.frame = (b.frame + 1) % frames;
        }
        bool impact = false;
        if (!(b.flags & 0x100)) {
            if (b.ticks == 0) impact = true;
            else --b.ticks;
        } else {
            // Falling: gravity 0x2000; the target follows the bullet. On the
            // ground, a 0x400 bullet (debris) bounces with half the speed
            // unless rand() & 3 == 1 or it is slow (vz <= 6553.6).
            b.vz += 0x2000;
            b.tx = b.x >> 16;
            b.ty = b.y >> 16;
            b.height -= b.vz;
            if (b.height < 1) {
                if (!(b.flags & 0x400) || (rng_.next() & 3) == 1 || double(b.vz) <= 6553.6) {
                    b.height = 0;
                    impact = true;
                } else {
                    b.vz = -(b.vz / 2);
                }
            }
        }
        if (!impact) {
            if (b.flags & 1) {
                b.vz += 0x2000;
                b.height -= b.vz;
            }
            b.x += b.dx;
            b.y += b.dy;
            return;
        }
        // Impact on the (scattered) target pixel.
        b.dead = true;
        {
            const auto* ids = templates_.block(0x6d60d8);
            if (ids && ids->bytes.size() > 0x32 && b.type == ids->bytes[0x32] && b.tx >= 0 && b.ty >= 0 &&
                (b.tx >> 5) < map_.width() && (b.ty >> 5) < map_.height()) {
                spice_lands(b.tx, b.ty); // a SpiceExpl blob (0x6d610a)
            }
        }
        if (b.tx >= 0 && b.ty >= 0 && (b.tx >> 5) < map_.width() && (b.ty >> 5) < map_.height()) {
            damage_at(b.tx, b.ty, b.height, b.type, b.owner, b.firer, (b.flags & 0x40) != 0);
        }
        if (br.impact >= 0) {
            play_at(templates_.explosion_sound[size_t(br.impact)], b.tx >> 5, b.ty >> 5);
            // Height passed as height >> 16 into a 16.16 field: on the ground (quirk).
            spawn_effect_at(br.impact, b.tx, b.ty, b.owner, 0, 0, 0);
        }
    }
}

void World::tick_homing(Bullet& b) {
    // FUN_0049be60, flag 0x20 (docs/re/18 section 4.3).
    const auto& br = templates_.bullets[size_t(b.type)];
    int32_t goal_h = 0;
    if (b.target >= 0) {
        const int ti = find_unit(b.target);
        if (ti >= 0) {
            const Unit& t = units_[size_t(ti)];
            b.tx = t.x >> 16;
            b.ty = t.y >> 16;
            goal_h = t.altitude;
            if (t.state == 0x11) b.target = -1;
        } else {
            b.target = -1;
        }
    }
    const int dcx = (b.tx >> 5) - ((b.x >> 16) >> 5), dcy = (b.ty >> 5) - ((b.y >> 16) >> 5);
    const int d2 = dcx * dcx + dcy * dcy;
    const int turns = d2 >= 17 ? 1 : d2 >= 15 ? 2 : d2 >= 13 ? 3 : d2 >= 11 ? 4 : d2 >= 9 ? 6 : d2 >= 7 ? 8 : 32;
    b.speed = br.speed;
    const int frames = std::max(1, int(templates_.art.bullet_frames[size_t(br.image) % 64]));
    // (x - tx << 16) >> 16 and (ty << 16 - y) >> 16, truncating toward zero.
    const int32_t ax = int32_t(b.x - int32_t(uint32_t(b.tx) << 16)), ay = int32_t(int32_t(uint32_t(b.ty) << 16) - b.y);
    const int goal = angle12(ax / 0x10000, ay / 0x10000) / (0x1000 / frames);
    int rot = b.rot;
    for (int k = 0; k < turns; ++k) {
        if (rot == goal) continue;
        int next = rot + 1;
        if (uint8_t((rot - goal + frames) % frames) <= uint8_t((goal + frames - rot) % frames)) next = rot - 1;
        rot = (next + frames) % frames;
    }
    rot &= 0xff;
    if (std::abs(b.last_rot - rot) == 16 && b.ticks != 0) b.ticks = 1; // a U-turn
    b.rot = b.last_rot = rot;
    if (b.flags & 0x80) b.frame = (b.frame + 1) % frames;
    else b.frame = rot;
    const int f = int(uint8_t(32 / frames) * rot) & 31;
    const int s = (b.speed * 32) / 0x10000;
    b.x += flight_dx(f, s);
    b.y -= flight_dy(f, s);
    if (b.x < 0 || ((b.x >> 16) >> 5) >= map_.width() || b.y < 0 || ((b.y >> 16) >> 5) >= map_.height()) {
        b.dead = true; // off the map: removed, no impact
        return;
    }
    if (goal_h != 0) {
        int32_t d = (goal_h - b.height) / 2;
        d = std::clamp(d, -0x20000, 0x20000);
        b.height += d;
    }
    const int32_t old = b.ticks--;
    if (old != 0) {
        // Not yet in the target's 8 px block: keep flying.
        if (((int32_t(uint32_t(b.tx) << 16) + 4) >> 19) != ((b.x + 4) >> 19)) return;
        if (((int32_t(uint32_t(b.ty) << 16) + 4) >> 19) != ((b.y + 4) >> 19)) return;
    }
    // Impact at the bullet's own pixel and height.
    b.dead = true;
    const int px = b.x >> 16, py = b.y >> 16;
    damage_at(px, py, b.height, b.type, b.owner, b.firer, (b.flags & 0x40) != 0);
    if (br.impact >= 0) {
        play_at(templates_.explosion_sound[size_t(br.impact)], px >> 5, py >> 5);
        spawn_effect_at(br.impact, px, py, b.owner, 0, 0, 0, b.height);
    }
}

// ---- damage -------------------------------------------------------------------

void World::damage_tile(int x, int y, int32_t height, int type) {
    // FUN_0049d2e0, after the building and unit loops (docs/re/26 section 2):
    // ground hits on a walkable cell without spice or a building add
    // (percent vs armour 0 x damage / 100) / 40; concrete breaks once the
    // counter passes 0x10.
    if (height > 0x60000) return;
    const int cx = x >> 5, cy = y >> 5;
    if (cx < 0 || cy < 0 || cx >= map_.width() || cy >= map_.height()) return;
    auto& c = map_.at(cx, cy);
    if (!(c.flags & cell::kInfantry) || (c.flags & cell::kSpiceLevelMask) || (c.flags & 0x10)) return;
    const auto& br = templates_.bullets[size_t(type)];
    if (br.warhead >= armour_.warheads.size()) return;
    const uint32_t inc = (uint32_t(armour_.warheads[br.warhead].percent[0]) * uint32_t(br.damage) / 100u) / 40u;
    if (uint32_t(c.marks) + inc < 0xff) {
        c.marks = uint8_t(c.marks + inc);
        if (c.marks > 0x10 && (c.flags & 0x800u) && (c.flags & cell::kBuildable)) {
            static const uint16_t kBroken[8] = {552, 553, 554, 555, 556, 572, 573, 574}; // 0x4e0af0
            c.flags &= ~0x800u; // concrete
            c.tile = kBroken[rng_.next() & 7];
        }
    } else {
        c.marks = 0xff; // saturated; the concrete check is skipped
    }
}

bool World::on_building_cell(const Unit& u) const {
    // FUN_0049d280: the current cell, or the reserved "to" cell, is a building cell.
    if (map_.at(u.cur_x, u.cur_y).flags & 0x10) return true;
    return (u.flags & 0x10000000) && u.to_x < map_.width() && u.to_y < map_.height() &&
           (map_.at(u.to_x, u.to_y).flags & 0x10);
}

bool World::deviation_runs(const Unit& u) const {
    // FUN_0049fb40: deviated, and the original side still has buildings or units.
    if (!(u.flags & 0x1000)) return false;
    const int s = u.orig_side & 7;
    return has_buildings_[size_t(s)] || has_units_[size_t(s)];
}

void World::damage_at(int x, int y, int32_t height, int type, int attacker, int firer, bool deviate) {
    // FUN_0049d2e0.
    const auto& br = templates_.bullets[size_t(type)];
    // A neutral side hit by an attacker turns hostile (FUN_0049f870), not
    // by SpiceExpl blobs (0x6d610a): building hit / destroyed, unit hit / killed.
    const auto* ids = templates_.block(0x6d60d8);
    const int spice_expl = ids && ids->bytes.size() > 0x32 ? ids->bytes[0x32] : -1;
    auto provoke = [&](int victim) {
        if (attacker >= 0 && attacker < 8 && type != spice_expl && alliance_[size_t(victim)][size_t(attacker)] == 2) {
            make_hostile(victim, attacker);
        }
    };
    const bool air = height > 0x60000;
    if (!air && x >= 0 && y >= 0 && (x >> 5) < map_.width() && (y >> 5) < map_.height()) {
        // A ripe bloom at the impact cell bursts, and nothing else is hit
        // (docs/re/31 section 6.1): the burst's rand() calls, then the re-arm's.
        int oi = -1;
        const int t = find_cell_object(x >> 5, y >> 5, oi);
        if (t >= 8 && t <= 10) {
            bloom_burst(x >> 5, y >> 5, t - 4);
            bloom_rearm(oi);
            return;
        }
    }
    if (br.warhead >= armour_.warheads.size()) return;
    const auto& wh = armour_.warheads[br.warhead];
    const int64_t R = wh.radius;
    auto amount = [&](int dx, int dy, int armour, int64_t& dmg) {
        if (std::abs(dx) >= R || std::abs(dy) >= R) return false;
        const double d = std::sqrt(double(int64_t(dx) * dx + int64_t(dy) * dy));
        if (d >= double(R)) return false;
        const uint32_t base = uint32_t(wh.percent[size_t(armour) % 12]) * uint32_t(br.damage) / 100u;
        double t = double(R) - d;
        t = t / double(R);
        dmg = int64_t(double(base) * t);
        return true;
    };
    for (int p = 0; p < 8; ++p) {
        if (!air) {
            for (auto& b : buildings_) {
                if (b.owner != p || b.removed || b.dying || (b.flags & 6)) continue;
                int cx, cy;
                closest_cell_manhattan(b.index, x >> 5, y >> 5, cx, cy); // FUN_0049d170
                int64_t dmg = 0;
                const auto& r = templates_.building_records[size_t(b.record)];
                if (!amount(x - (cx * 32 + 16), y - (cy * 32 + 16), r.armour, dmg)) continue;
                            if (b.owner != attacker) {
                    b.flags |= 0x1000;
                    if (b.owner == local_player) voice(b.owner, "UNDERATTACK", 300);
                }
                if (dmg < b.hit_points) {
                    provoke(b.owner);
                    // FUN_0049d2e0 (0x49d65e): the first attacker of another
                    // side is kept (+0x50 / +0x52) as its pool slot, a unit's
                    // or a turret's, never cleared (docs/re/50 section 6).
                    if (b.attacker < 0 && type != spice_expl && attacker != b.owner) {
                        b.attacker_side = attacker;
                        b.attacker = firer_slot(firer);
                    }
                    b.hit_points = std::max<int32_t>(b.hit_points - int32_t(dmg), 0);
                    if (b.hit_points < r.hit_points / 2 && (r.flags & 0x200)) { // smoke (doc 18 section 2.3)
                        b.smoke = uint8_t((rng_.next() & 0x7f) + 0x40);
                        b.smoke_points |= rng_.next();
                    }
                    continue;
                }
                // FUN_0049d2e0 0x49d647: unless SPICEEXPL, the attacker is
                // recorded (firer 0xffff too) and gets the kill, whatever
                // its side; the owner always loses it (docs/re/60 section 2).
                if (type != spice_expl) {
                    b.attacker_side = attacker;
                    b.attacker = firer_slot(firer);
                    if (attacker >= 0 && attacker < 8) count_building_killed(attacker, b.record, b.owner);
                }
                ++players_[size_t(b.owner)].buildings_lost;
                if (b.owner == local_player && r.kind != 0xe) voice(b.owner, "BUILDINGLOST");
                provoke(b.owner);
                destroy_building(b.index);
            }
        }
        for (size_t i = 0; i < units_.size(); ++i) {
            Unit& u = units_[i];
            if (u.owner != p || u.remove) continue;
            if ((u.flags & 0x8000000) || (u.flags & kCarried) || u.state == 8 || u.state == 0x11 || u.state == 0x17) continue;
            const bool is_air = (u.flags & kAircraft) != 0;
            if (is_air != air) continue;
            const auto& r = templates_.unit_records[size_t(u.record)];
            int64_t dmg = 0;
            if (!amount(x - (u.x >> 16), y - (u.y >> 16), r.armour, dmg)) continue;
            u.flags |= kHit;
            if (dmg < u.hit_points) {
                u.hit_points -= int32_t(dmg);
                provoke(u.owner);
                if (u.flags & 0x2000000) {
                    u.flags &= ~0x2000000u; // the first hit after a conversion: no voice
                } else if (r.kind == 1 && u.owner == local_player && firer >= 0) {
                    voice(u.owner, "HARVATTACK", 300);
                }
                if (r.kind != 5 && u.hit_points < r.hit_points / 2) u.smoke = uint8_t((rng_.next() & 0x7f) + 0x40); // line 0x1dba
                if (u.attacker < 0 && type != spice_expl && attacker != u.owner) {
                    u.attacker_side = attacker;
                    u.attacker = firer;
                }
                if (deviate && r.kind != 5) { // the Deviator (docs/re/40 section 2)
                    u.flags &= ~kHit;
                    if (attacker == p || r.infantry || on_building_cell(u)) {
                        u.hit_age = 1;
                    } else {
                        if (!(u.flags & 0x1000)) u.orig_side = uint8_t(p);
                        const int id = convert_unit(p, attacker, u.index, false);
                        const int k = id >= 0 ? find_unit(id) : -1;
                        if (k >= 0) {
                            units_[size_t(k)].delivery_entry = 0xf0; // the deviation timer (+0x2e)
                            units_[size_t(k)].flags |= 0x1000;
                        }
                    }
                }
                continue;
            }
            provoke(u.owner);
            // 0x49d927: unless SPICEEXPL, the attacker is recorded and gets
            // the kill (a worm's "kill" too), indexed by the victim's side.
            if (type != spice_expl) {
                u.attacker_side = attacker;
                u.attacker = firer;
                if (attacker >= 0 && attacker < 8) count_unit_killed(attacker, u.record, p);
            }
            if (r.kind == 5) { // a worm cannot die
                u.hit_points = 1;
                continue;
            }
            if (r.infantry) set_inf_anim(u, 9 + wh.infdeath); // SetInfAnim: the frame is only clamped
            if (u.owner == local_player) voice(u.owner, "UNITLOST");
            // 0x49da35: a deviated unit's loss goes to its original side (docs/re/40 section 4.1).
            count_unit_lost((u.flags & 0x1000) ? (u.orig_side & 7) : u.owner, u.record);
            kill_unit(u);
        }
    }
    damage_tile(x, y, height, type); // the tail of FUN_0049d2e0: the impact cell's crater counter
}

void World::kill_unit(Unit& u) {
    // FUN_0049cde0. It counts nothing: the loss / kill counters belong to
    // the callers that count them (damage, crush, the entrant; docs/re/60),
    // so a carryall's cargo and a removed side's units are not counted.
    const auto& r = templates_.unit_records[size_t(u.record)];
    // A carried cargo keeps its cells untouched at removal (its pick-up
    // freed them); a loaded carryall kills its cargo first and gives taken
    // requests back (docs/re/33 section 4.1).
    if (!(u.flags & 0x40) && (u.flags & 0x100)) u.flags |= 0x400000;
    // Kind 2 (carryall): the cargo and the taken request slots (+0x26611).
    // The delivery table (+0x262ec) is not touched: its only writers are
    // FUN_00454120, FUN_004563b0, FUN_00457140 and the drop FUN_0049e3e0,
    // so a delivery whose carryall dies stays active and started and its
    // slot is lost (docs/re/18 section 2.2.1).
    if (r.kind == 2) carryall_killed(u); // creates no units, so `u` stays valid
    if (r.infantry && (u.anim_mode < 9 || u.anim_mode > 13)) set_inf_anim(u, int(rng_.next() & 3) + 9); // line 0x1bd2
    // The pixel and its cell, both truncated toward zero as the exe's
    // signed divisions (an aircraft can be off the map, x < 0).
    const int px = u.x / 65536, py = u.y / 65536;
    const int pcx = px / 32, pcy = py / 32;
    if (r.death_explosion >= 0 && u.state != 0x11) {
        // FUN_0049cde0: aircraft explosions get flag 0x1000 and move one
        // flight step (DX, -DY) at the unit's speed and facing.
        const uint32_t mf = (u.flags & kAircraft) ? 0x1000u : 0u;
        const int s = int((uint32_t(u.speed) & 0x7ffffff) >> 11);
        const int32_t mx = flight_dx(u.facing, s), my = -flight_dy(u.facing, s);
        spawn_effect_at(templates_.explosion_index("EX_WHITEN1"), px, py, u.owner, 0, mf, 0, 0, mx, my);
        play_at(templates_.explosion_sound[size_t(r.death_explosion)], pcx, pcy);
        spawn_effect_at(r.death_explosion, px, py, u.owner, 0, mf, 0, u.altitude, mx, my);
    }
    if (r.infantry && u.state != 0x17) {
        // 0x49cf8a: "S_MANDYING%d" with rand() [line 0x1bfb] % 9 + 1, at
        // the pixel's cell; independent of the explosion branch.
        const std::string s = "S_MANDYING" + std::to_string(1 + int(rng_.next() % 9));
        SoundEvent e;
        e.name = s;
        e.cx = pcx;
        e.cy = pcy;
        sounds.push_back(e);
    }
    u.hit_points = 0;
    set_state(u, 0x11);
    // +0x2F is written only by CreateUnit (0x4559b4 / 0x4559ba: infantry
    // 0x32, else 10) and decremented only in state 0x11 (0x49a09c), so at
    // death it still holds the creation value, which we set here.
    u.countdown = uint8_t(r.infantry ? 0x32 : 10);
    if (r.kind == 0xb || r.kind == 4) {
        // Sardaukar and saboteurs: SARDDEATH (0x6d60f0) at the top-left
        // pixel of the unit's cell.
        for (size_t k = 0; k < templates_.bullet_name.size(); ++k) {
            if (templates_.bullet_name[k] == "SARDDEATH") {
                damage_at(u.cur_x * 32, u.cur_y * 32, 0, int(k), u.owner, -1);
                break;
            }
        }
    }
}

void World::destroy_building(int index, bool flag_2000) {
    // FUN_0049ca10 (docs/re/18 section 2.3).
    Building& b = buildings_[size_t(index)];
    const auto& r = templates_.building_records[size_t(b.record)];
    if (r.death_explosion >= 0) {
        play_at(templates_.explosion_sound[size_t(r.death_explosion)], b.cell_x, b.cell_y);
        if (r.shakes) shake_ticks = 10;
        int cells = 0;
        for (int k = 0; k < 16; ++k) cells += (r.footprint >> k) & 1;
        const auto* ids = templates_.block(0x6d60d8);
        const int debris_count = ids && ids->bytes.size() > 0x31 ? ids->bytes[0x31] : 0; // 0x6d6109
        // Debris origin: the building's top-left px + half its image frame.
        int ox = b.x >> 16, oy = (b.y >> 16) - r.height_px;
        if (r.base_element >= 0 && size_t(r.base_element) < building_frame.size()) {
            ox += building_frame[size_t(r.base_element)].first >> 1;
            oy += building_frame[size_t(r.base_element)].second >> 1;
        }
        for (int k = 0; k < 2 * cells; ++k) {
            int col, row;
            do {
                col = int(rng_.next() & 3);
                row = int(rng_.next() & 3);
            } while (!(r.footprint & (1u << (row * 4 + col))));
            const int px = (b.cell_x + col) * 32 + int(rng_.next() & 31);
            const int py = (b.cell_y + row) * 32 + int(rng_.next() & 31);
            const int delay = (rng_.next() & 31) != 0 ? 1 : 0;
            spawn_effect_at(r.death_explosion, px, py, b.owner, 0, 0x20, delay);
            if (debris_count == 0) continue;
            // One debris bullet (DEBRIS..DEBRIS4, 0x6d60f1) per explosion:
            // delay (r & 15) + 8 ticks, then it flies off with random speed
            // and bounces (flag 0x400).
            const int type = ids->bytes[size_t(0x19 + rng_.next() % uint32_t(debris_count))];
            const int wait = int(rng_.next() & 0xf) + 8;
            const int bi = add_bullet(b.owner, type, -1000 - b.index, ox, oy, ox, oy, -1);
            if (bi < 0) continue;
            Bullet& d = bullets[size_t(bi)];
            d.delay = wait;
            d.dx = int32_t((rng_.next() & 0x1f) - 0x10) * 0x2000;
            d.dy = int32_t((rng_.next() & 0x1f) - 0x10) * 0x2000;
            d.height = int32_t(rng_.next() & 3) << 16;
            d.vz = (-2 - int32_t(rng_.next() & 1)) * 0x10000;
            d.flags |= 0x400;
        }
    }
    b.dying = true;
    b.death_timer = 30;
    if (flag_2000) b.flags |= 0x2000; // FUN_0046e6b0's removal
    b.hit_points = 0;
}

void World::remove_building(int index) {
    // The building leaves the map (FUN_0049b7f0 state 0x11). A captured
    // building's old object (0x10000) leaves its cells to the new one.
    Building& b = buildings_[size_t(index)];
    if (!(b.flags & 0x10000)) clear_building_cells(b);
    b.removed = true;
    reassign_primary(b);
    sold_refund(b);
    rebuild_lists(b.owner);
}

void World::clear_building_cells(const Building& b) {
    const auto& r = templates_.building_records[size_t(b.record)];
    // FUN_0044df60: unless the record has flag 0x400000, the place-mask cells
    // become buildable again and the footprint cells buildable, walkable and
    // drivable (0xe000); 0x400 and the building / exit flags are cleared.
    // The owner bits stay; the radar pixel becomes RGB(60, 50, 50) whatever
    // the ground (0x44e0a9, docs/re/11 section 4); then the wall neighbours
    // (FUN_0044e6d0).
    const bool restore = !(r.flags & 0x400000);
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            const int x = b.cell_x + col, y = b.cell_y + row;
            if (x >= map_.width() || y >= map_.height()) continue;
            auto& f = map_.at(x, y).flags;
            if (r.place_mask & (1u << (row * 4 + col))) {
                if (restore) f |= cell::kBuildable;
                f &= ~0x400u;
            }
            if (r.footprint & (1u << (row * 4 + col))) {
                f &= ~0x10000010u;
                if (restore) f |= 0xe000u;
                map_.at(x, y).radar = radar_colour::kDrivable;
            }
        }
    }
    update_walls_around(b.cell_x, b.cell_y);
    // FUN_0044dee0: then the skirt tiles go (docs/re/12 section 6 step 15).
    if (r.flags & 0x200000) clear_skirt(b.record, b.cell_x, b.cell_y);
}

// ---- firing -------------------------------------------------------------------

bool World::unit_fire(Unit& u, int tx, int ty, int kind, int target) {
    // FUN_00495b90. Returns true when it fired.
    const auto& r = templates_.unit_records[size_t(u.record)];
    int ux = u.x >> 16, uy = u.y >> 16;
    if (ux == tx && uy == ty) return false;
    const int f = facing32(ux, uy, tx, ty);
    if (r.has_turret) {
        if (u.turret_facing != f) {
            turn_turret(u, f);
            if (u.flags & 0x8000) { // turret locked to the body (SIEGE_TANK's record flags): the hull turns, no delay
                u.turn_counter = 0;
                turn_body(u, f);
            }
            return false;
        }
    } else if (u.facing != f) {
        turn_body(u, f);
        return false;
    }
    if (is_stealthed(u)) { // a cloaked unit's shot only starts the fade-in (docs/re/32 section 1.4)
        uncloak(u);
        return false;
    }
    const int aim = r.has_turret ? u.turret_facing : u.facing;
    if (r.muzzle_flash >= 0) spawn_effect_at(r.muzzle_flash, ux, uy, u.owner, aim, 0, 0);
    ux += step_dx(aim, r.muzzle_offset) >> 16;
    uy -= step_dy(aim, r.muzzle_offset) >> 16;
    const int64_t ddx = int64_t(tx) - ux, ddy = int64_t(ty) - uy;
    const int dist = int(uint16_t(int(std::sqrt(double(ddx * ddx + ddy * ddy)))));
    int armour = 0;
    bool air = false;
    if (kind == 1) {
        const int ti = find_unit(target);
        if (ti >= 0) {
            armour = templates_.unit_records[size_t(units_[size_t(ti)].record)].armour;
            air = (units_[size_t(ti)].flags & kAircraft) != 0;
        }
    } else if (kind == 2 && building_alive(target)) {
        armour = templates_.building_records[size_t(buildings_[size_t(target)].record)].armour;
    }
    const int weapon = choose_weapon(r.weapon, r.weapon2, int64_t(dist) * dist, air, armour);
    u.counter_2b = 0; // +0x2b: ticks / 8 since it fired
    const int n = dist >> 5;
    if (n != 0) {
        tx += int(rng_.next() % uint32_t(8 * n)) - 4 * n;
        ty += int(rng_.next() % uint32_t(8 * n)) - 4 * n;
    }
    play_at(templates_.bullets[size_t(weapon)].sound, ux >> 5, uy >> 5);
    const int target_owner = kind == 1 && find_unit(target) >= 0 ? units_[size_t(find_unit(target))].owner
                             : kind == 2 && building_alive(target) ? buildings_[size_t(target)].owner
                                                                   : -1;
    if (target_owner == local_player) shooter_reveal(u.to_x, u.to_y);
    add_bullet(u.owner, weapon, u.index, ux, uy, tx, ty, kind == 1 ? target : -1);
    u.flags |= kFired;
    u.reload = r.reload;
    if (r.infantry) set_inf_anim(u, 2); // SetInfAnim: prone fire 6 when prone
    return true;
}

void World::turn_turret(Unit& u, int dir) {
    // FUN_00493f40: flag 0x8000 locks the turret to the body. Otherwise the
    // goal is stored (+0x56) and the turret steps once every rec+0x0f + 1
    // ticks, the short way round; at exactly half a turn it steps +1 when
    // the goal is below the facing, else -1 (as the body, FUN_00493eb0).
    if (u.flags & 0x8000) {
        u.turret_facing = u.facing;
        return;
    }
    u.turret_goal = uint8_t(dir);
    if (u.turret_facing == dir) return;
    if (u.turret_counter != 0) {
        --u.turret_counter;
        return;
    }
    const int d = dir - int(u.turret_facing);
    const int step = std::abs(d) < 16 ? (d > 0 ? 1 : -1) : (d > 0 ? -1 : 1);
    u.turret_facing = uint8_t((u.turret_facing + step + 32) & 31);
    u.turret_counter = templates_.unit_records[size_t(u.record)].turret_turn_delay;
}

void World::turret_wander(Unit& u) {
    // FUN_00494210: a turreted unit that reached its turret goal picks a new
    // one when rand() < 31 (goal = that value & 0x1F, rand line 0x39e), then
    // turns toward the goal.
    if (!templates_.unit_records[size_t(u.record)].has_turret) return;
    if (u.turret_facing == u.turret_goal) {
        const uint32_t v = rng_.next();
        if (v < 0x1f) u.turret_goal = uint8_t(v & 0x1f);
    }
    turn_turret(u, u.turret_goal);
}

int World::auto_fire(Unit& u) {
    // FUN_00496480: 1 = busy (reloading or shooting), 0 = nothing to do.
    const auto& r = templates_.unit_records[size_t(u.record)];
    if (u.reload) return 1;
    if (r.weapon == 0xff) return 0;
    if (u.target_kind == 1) {
        const int ti = find_unit(u.target);
        if (ti >= 0 && units_[size_t(ti)].state != 0x11 && enemy(u.owner, units_[size_t(ti)].owner) &&
            can_attack_cell(u, (units_[size_t(ti)].x >> 16) >> 5, (units_[size_t(ti)].y >> 16) >> 5,
                            (units_[size_t(ti)].flags & kAircraft) != 0)) {
            unit_fire(u, units_[size_t(ti)].x >> 16, units_[size_t(ti)].y >> 16, 1, u.target);
            return 1;
        }
    } else if (u.target_kind == 2 && building_alive(u.target) && !buildings_[size_t(u.target)].dying) {
        int cx, cy;
        closest_cell(u.target, (u.x >> 16) >> 5, (u.y >> 16) >> 5, cx, cy);
        if (can_attack_cell(u, cx, cy, false)) {
            unit_fire(u, cx * 32 + 16, cy * 32 + 16, 2, u.target);
            return 1;
        }
    }
    const int found = target_unit_in_range(u.cur_x * 32 + 16, u.cur_y * 32 + 16, u.owner, 0, r.weapon, r.weapon2, true, true);
    if (found >= 0) {
        u.target_kind = 1;
        u.target = found;
        const auto& t = units_[size_t(find_unit(found))];
        if (templates_.unit_records[size_t(t.record)].kind == 5) {
            // 0x4966b2: FUN_004a5270 on the worm's pixel position (the target
            // search's x / y outputs); a unit there: no shot (docs/re/17 section 5.4).
            if (unit_at_pixel(t.x >> 16, t.y >> 16, false) >= 0) return 1;
        }
        unit_fire(u, t.x >> 16, t.y >> 16, 1, found);
        return 1;
    }
    int bx, by;
    const int range = std::max(bullet_range(r.weapon), bullet_range(r.weapon2));
    const int b = target_building_in_range(u.cur_x, u.cur_y, u.owner, range, true, bx, by);
    if (b >= 0) {
        u.target_kind = 2;
        u.target = b;
        unit_fire(u, bx * 32 + 16, by * 32 + 16, 2, b);
        return 1;
    }
    return 0;
}

bool World::target_valid(const Unit& u) const {
    // FUN_00497b90 head (0x497d93): a unit target is dropped when it is dying
    // (0x11), when it has flags 0x400104 (4 = a submerged worm) and is not
    // allied (alliance table != 0), or when it is carried (0x100) and not an
    // aircraft (0x40). A building target is dropped when it is dying.
    if (u.target_kind == 1) {
        const int ti = find_unit(u.target);
        if (ti < 0) return false;
        const Unit& t = units_[size_t(ti)];
        if (t.state == 0x11) return false;
        if ((t.flags & 0x400104) && alliance_[size_t(u.owner)][size_t(t.owner)] != 0) return false;
        if ((t.flags & 0x100) && !(t.flags & 0x40)) return false;
        return true;
    }
    if (u.target_kind == 2) return building_alive(u.target) && !buildings_[size_t(u.target)].dying;
    return false;
}

void World::tick_combat_state(Unit& u) {
    const auto& r = templates_.unit_records[size_t(u.record)];
    const bool slot = (uint32_t(u.slot) & 7) == (tick_count_ & 7);
    switch (u.state) {
    case state::kIdle: // guard: never chases
        if (r.infantry) set_inf_anim(u, 0); // 0x498129, every tick (keeps prone and idle modes)
        if (r.kind == 1) return;
        if (u.target_kind != 0 && !target_valid(u)) u.target_kind = 0;
        if ((u.target_kind != 0 || slot) && !is_stealthed(u)) auto_fire(u); // a cloaked guard never auto-fires
        // No turret wander here: FUN_00494210's only caller is state 3 (0x498d9f).
        return;

    case 3: { // area guard / hunt around +0x50 / +0x51
        if (u.target_kind != 0 && !target_valid(u)) u.target_kind = 0;
        if (u.target_kind == 1 && u.steps == 0) {
            const Unit& t = units_[size_t(find_unit(u.target))];
            const int dx = (u.x - t.x) >> 21, dy = (u.y - t.y) >> 21;
            if (dx * dx + dy * dy < 36 && templates_.unit_records[size_t(t.record)].kind != 5) {
                if (attack_unit(u) == 0) u.target_kind = 0;
            } else {
                u.target_kind = 0;
            }
            return;
        }
        if (u.target_kind == 2 && u.steps == 0) {
            if (attack_building(u) == 0) u.target_kind = 0;
            return;
        }
        if (u.steps != 0) {
            move_step(u);
            return;
        }
        if (!slot) {
            move_step(u);
            return;
        }
        if (auto_fire(u)) {
            move_step(u);
            return;
        }
        const int found = target_unit_in_range(u.home_x * 32 + 16, u.home_y * 32 + 16, u.owner, 192, r.weapon,
                                               r.weapon2, false, true);
        if (found >= 0) {
            u.target_kind = 1;
            u.target = found;
            attack_unit(u);
            return;
        }
        u.dest_x = u.home_x;
        u.dest_y = u.home_y;
        // At the guard cell (MoveStep returned 0): SetInfAnim(0) (0x498d6d);
        // then, unless the unit can cloak (flag 0x10, 0x498d75), the auto-fire
        // again (0x498d8e) and, when it does nothing, the turret wander
        // (0x498d9f, docs/re/18 section 3.6).
        if (move_step(u) != 0) return;
        if (r.infantry) set_inf_anim(u, 0);
        if (!(u.flags & 0x10) && auto_fire(u) == 0) turret_wander(u);
        return;
    }
    case 4:
        if (attack_unit(u) == 0) {
            set_state(u, state::kMove);
            u.next_state = 1;
            u.target_kind = 0;
        }
        return;
    case 5:
        if (attack_building(u) == 0) {
            set_state(u, state::kMove);
            u.next_state = 1;
            u.target_kind = 0;
        }
        return;
    case 6: { // attack ground at dest
        if (u.steps != 0 || !can_attack_cell(u, u.dest_x, u.dest_y, false)) {
            move_step(u);
            return;
        }
        if (u.reload) return;
        if (u.cur_x == u.dest_x && u.cur_y == u.dest_y) {
            set_state(u, state::kIdle);
            return;
        }
        unit_fire(u, u.dest_x * 32 + 16, u.dest_y * 32 + 16, 0, -1);
        return;
    }
    default:
        return;
    }
}

int World::attack_unit(Unit& u) {
    // FUN_00496b00.
    if (u.steps != 0) {
        move_step(u);
        return 1;
    }
    const int ti = find_unit(u.target);
    if (u.target_kind != 1 || ti < 0 || units_[size_t(ti)].state == 0x11) return 0;
    const Unit& t = units_[size_t(ti)];
    if (can_attack_cell(u, t.cur_x, t.cur_y, (t.flags & kAircraft) != 0)) {
        u.dest_x = u.cur_x;
        u.dest_y = u.cur_y;
        if (u.reload == 0) unit_fire(u, t.x >> 16, t.y >> 16, 1, u.target);
        return 1;
    }
    u.dest_x = t.to_x;
    u.dest_y = t.to_y;
    move_step(u);
    return 1;
}

int World::attack_building(Unit& u) {
    // FUN_00496d10 (armed units; engineers and saboteurs enter).
    const int kind = templates_.unit_records[size_t(u.record)].kind;
    if (kind == 3 || kind == 4) return enter_building(u);
    if (u.target_kind != 2 || !building_alive(u.target) || buildings_[size_t(u.target)].dying) return 0;
    int cx, cy;
    closest_cell(u.target, (u.x >> 16) >> 5, (u.y >> 16) >> 5, cx, cy);
    if (u.steps == 0 && can_attack_cell(u, cx, cy, false)) {
        u.dest_x = u.cur_x;
        u.dest_y = u.cur_y;
        if (u.reload == 0) unit_fire(u, cx * 32 + 16, cy * 32 + 16, 2, u.target);
        return 1;
    }
    u.dest_x = uint8_t(cx);
    u.dest_y = uint8_t(cy);
    move_step(u);
    return 1;
}

void World::order_attack_unit(int owner, int target_id) {
    // Order 0x14, ProcessOrder case 1: the target must be a unit without
    // flags 0x400140 (no state test); per id: a unit, FUN_0049f4a0(u, 1),
    // armed, not the target, and SetState(4) succeeds; only then the next
    // state, target side (+0x26) and target (+0x0c) are written.
    const int ti = find_unit(target_id);
    if (ti < 0 || units_[size_t(ti)].remove || (units_[size_t(ti)].flags & 0x400140)) return;
    for (auto& u : units_) {
        if (u.owner != owner || !u.selected || u.remove || u.index == target_id) continue;
        if (templates_.unit_records[size_t(u.record)].weapon == 0xff) continue;
        if (!takes_orders_forced(u) || !set_state(u, 4)) continue;
        u.next_state = 1;
        u.target_kind = 1;
        u.target = target_id;
    }
}

void World::order_attack_building(int owner, int building) {
    // Order 0x15 (FUN_00459800 + ProcessOrder case 2): not harvesters,
    // MCVs or thumpers; engineers / saboteurs not at an own building.
    // SetState refuses state 5 for other unarmed units.
    if (!building_alive(building)) return;
    const bool own = buildings_[size_t(building)].owner == owner;
    for (auto& u : units_) {
        if (u.owner != owner || !u.selected || u.state == 0x11 || u.remove) continue;
        const int k = templates_.unit_records[size_t(u.record)].kind;
        if (k == 1 || k == 6 || k == 0xe || (own && (k == 3 || k == 4))) continue;
        if (!takes_orders_forced(u) || !set_state(u, 5)) continue;
        u.next_state = 1;
        u.target_kind = 2;
        u.target = building;
    }
}

void World::order_attack_ground(int owner, int cx, int cy) {
    // Order 0x16 (FUN_004598d0 + ProcessOrder case 3): the selected armed
    // units that take orders go to state 6 with the cell as dest.
    for (auto& u : units_) {
        if (u.owner != owner || !u.selected || u.state == 0x11 || u.remove) continue;
        if (templates_.unit_records[size_t(u.record)].weapon == 0xff) continue;
        if (!takes_orders_forced(u) || !set_state(u, 6)) continue;
        u.next_state = 1;
        u.dest_x = uint8_t(cx);
        u.dest_y = uint8_t(cy);
    }
}

void World::set_inf_anim(Unit& u, int mode) {
    // FUN_00494050: mode 0 does not interrupt a firing mode with a target, or
    // modes >= 0xe; prone adds 4 to modes < 3; the frame is only clamped into
    // the mode's range (so a walk cycle keeps going); +0x25 is left alone.
    if (mode == 0 && (u.anim_mode >= 0xe || ((u.anim_mode == 2 || u.anim_mode == 6) && u.target_kind != 0))) return;
    u.anim_mode = (u.prone && mode < 3) ? mode + 4 : mode;
    const int first = kInfModes[u.anim_mode & 15][0], last = kInfModes[u.anim_mode & 15][1];
    if (u.frame < first || u.frame > last) u.frame = uint8_t(first);
}

void World::tick_infantry_anim(Unit& u) {
    // FUN_004940d0 (docs/re/45 section 2.1), every mode: the frame advances
    // every 3rd tick (+0x25). When it leaves the mode's range: the death
    // modes (9..13) hold the last frame; hit this slot (+0x2a == 0) rolls
    // 1 in 5 to go prone (a failed roll keeps the mode); prone holds while
    // +0x2a < 40; else stand. Never hit for 255 slots: 2 in 400 for an idle
    // move (a turn toward facing 8 / 24, or idle animation 14 / 15).
    if (++u.timer < 3) return;
    u.timer = 0;
    ++u.frame;
    const int first = kInfModes[u.anim_mode & 15][0], last = kInfModes[u.anim_mode & 15][1];
    if (u.frame >= first && u.frame <= last) return;
    if (u.anim_mode > 8 && u.anim_mode < 0xe) {
        u.frame = uint8_t(last);
        return;
    }
    if (u.hit_age == 0) {
        if (rng_.next() % 5 == 0) { // 0x494354
            u.prone = true;
            u.anim_mode = 4;
        }
    } else if (u.prone && u.hit_age < 40) {
        u.anim_mode = 4;
    } else {
        u.prone = false;
        u.anim_mode = 0;
    }
    if (u.hit_age == 0xff) {
        const uint32_t r = rng_.next() % 400; // 0x49437b
        if (r < 2) {
            if (u.facing < 8 || u.facing > 24) {
                turn_step(u, u.facing < 8 ? 8 : 24); // FUN_004954b0
                u.frame = uint8_t(kInfModes[u.anim_mode & 15][0]);
                return;
            }
            u.anim_mode = int(14 + r);
        }
    }
    u.frame = uint8_t(kInfModes[u.anim_mode & 15][0]);
}

void World::turn_step(Unit& u, int goal) {
    // FUN_004954b0: one body turn step toward `goal` when the turn counter
    // (+0x1a) is 0 (then reloaded from rec +0x03); no SetInfAnim, no rand().
    if (u.facing == goal) return;
    if (u.turn_counter != 0) {
        --u.turn_counter;
        return;
    }
    u.turn_counter = templates_.unit_records[size_t(u.record)].turn_delay;
    const int up = (goal - int(u.facing)) & 31, down = (int(u.facing) - goal) & 31;
    u.facing = uint8_t((u.facing + (up < down ? 1 : -1) + 32) & 31);
}

// ---- buildings ------------------------------------------------------------------

int World::selected_turret(int p) const {
    // FUN_004a5860: the first own selected building with a turret element
    // (no dying test).
    for (const auto& b : buildings_) {
        if (b.owner == p && b.selected && !b.removed && templates_.building_records[size_t(b.record)].second_element != -1) {
            return b.index;
        }
    }
    return -1;
}

bool World::building_in_range(const Building& b, int cx, int cy, bool air) const {
    // FUN_00497140 (docs/re/38 section 5.1).
    const auto& r = templates_.building_records[size_t(b.record)];
    if (r.weapon == 0xff) return false;
    const int r1 = bullet_range(r.weapon), r2 = r.weapon2 != 0xff ? bullet_range(r.weapon2) : 0;
    const bool aa1 = templates_.bullets[size_t(r.weapon)].anti_air;
    const bool aa2 = r.weapon2 != 0xff && templates_.bullets[size_t(r.weapon2)].anti_air;
    int range;
    if (!air) range = std::max(r1, r2);
    else if (aa1) range = (aa2 && r2 > r1) ? r2 : r1;
    else if (aa2) range = r2;
    else return false;
    const int ox = (b.x >> 16) + 16, oy = (b.y >> 16) - r.height_px + 16;
    const int64_t dx = int64_t(ox) - (cx * 32 + 16), dy = int64_t(oy) - (cy * 32 + 16);
    return dx * dx + dy * dy <= int64_t(range) * range;
}

bool World::building_in_range_of(const Building& b, int target, int& cx, int& cy) const {
    // FUN_00497490: the target building's closest cell to the shooter's
    // top-left cell (FUN_00497390), then the range test.
    const auto& r = templates_.building_records[size_t(b.record)];
    const int bx = ((b.x >> 16) + 16) >> 5, by = ((b.y >> 16) - r.height_px + 16) >> 5;
    closest_cell(target, bx, by, cx, cy);
    return building_in_range(b, cx, cy, false);
}

void World::order_turret_attack_unit(int p, int unit_id) {
    // Order 0x1E (FUN_00459d20, case 0xB): not for a unit being removed,
    // carried or in the air; any side (own included); no range test.
    const int ti = find_unit(unit_id);
    if (ti < 0 || (units_[size_t(ti)].flags & 0x400140)) return;
    for (auto& b : buildings_) {
        if (b.owner != p || !b.selected || b.removed || b.dying) continue;
        if (templates_.building_records[size_t(b.record)].second_element == -1) continue;
        b.state = 4;
        b.target_kind = 1;
        b.target = unit_id;
    }
}

void World::order_turret_attack_building(int p, int index) {
    // Order 0x1F (FUN_00459de0, case 0xC): any building but the turret
    // itself; no range test.
    if (index < 0 || size_t(index) >= buildings_.size() || buildings_[size_t(index)].removed) return;
    for (auto& b : buildings_) {
        if (b.owner != p || !b.selected || b.removed || b.dying || b.index == index) continue;
        if (templates_.building_records[size_t(b.record)].second_element == -1) continue;
        b.state = 5;
        b.target_kind = 2;
        b.target = index;
    }
}

void World::tick_building_combat(Building& b) {
    const auto& r = templates_.building_records[size_t(b.record)];
    if (b.reload) --b.reload;
    if (b.target_kind == 1) {
        const int ti = find_unit(b.target);
        if (ti < 0 || units_[size_t(ti)].state == 0x11 || (units_[size_t(ti)].flags & 0x200004) ||
            ((units_[size_t(ti)].flags & kCarried) && !(units_[size_t(ti)].flags & kAircraft))) {
            b.target_kind = 0;
        }
    } else if (b.target_kind == 2 && (!building_alive(b.target) || buildings_[size_t(b.target)].dying)) {
        b.target_kind = 0;
    }
    switch (b.state) {
    case 4: { // FUN_004975d0: an ordered attack on a unit, every tick
        const int ti = b.target_kind == 1 ? find_unit(b.target) : -1;
        bool keep = false;
        if (ti >= 0 && units_[size_t(ti)].state != 0x11) {
            const Unit& t = units_[size_t(ti)];
            if (building_in_range(b, t.cur_x, t.cur_y, (t.flags & kAircraft) != 0)) {
                keep = true;
                if (b.reload == 0) building_fire(b, t.x >> 16, t.y >> 16, 1, b.target);
            }
        }
        if (!keep) b.state = 2; // the target is kept
        return;
    }
    case 5: { // FUN_004976b0: an ordered attack on a building, every tick
        bool keep = false;
        if (b.target_kind == 2 && building_alive(b.target) && !buildings_[size_t(b.target)].dying) {
            int cx, cy;
            // Out of range is a fatal error in the exe ("Building out of
            // range - should not happen"); here a crash fix: back to state 2.
            if (building_in_range_of(b, b.target, cx, cy)) {
                keep = true;
                if (b.reload == 0) building_fire(b, cx * 32 + 16, cy * 32 + 16, 2, b.target);
            }
        }
        if (!keep) b.state = 2;
        return;
    }
    default:
        break;
    }
    if (r.weapon == 0xff) return;
    if ((uint32_t(b.slot) & 7) != (tick_count_ & 7) && b.target_kind == 0) return;
    turret_auto_fire(b);
}

bool World::turret_auto_fire(Building& b) {
    // FUN_00496790 (docs/re/18 section 6.1, docs/re/38 section 5.5): a
    // stored target of another side (no alliance test) is fired at while in
    // range; else the unit search, else the building search; no hit keeps
    // the old target.
    const auto& r = templates_.building_records[size_t(b.record)];
    if (b.reload) return true;
    if (b.target_kind == 1) {
        const int ti = find_unit(b.target);
        if (ti >= 0 && units_[size_t(ti)].owner != b.owner) {
            const Unit& t = units_[size_t(ti)];
            if (building_in_range(b, (t.x >> 16) >> 5, (t.y >> 16) >> 5, false)) {
                building_fire(b, t.x >> 16, t.y >> 16, 1, b.target);
                return true;
            }
        }
    } else if (b.target_kind == 2 && building_alive(b.target)) {
        const Building& t = buildings_[size_t(b.target)];
        if (t.owner != b.owner && templates_.building_records[size_t(t.record)].kind == 0x10) {
            int cx, cy;
            if (building_in_range_of(b, b.target, cx, cy)) {
                building_fire(b, cx * 32 + 16, cy * 32 + 16, 2, b.target);
                return true;
            }
        }
    }
    const int ox = (b.x >> 16) + 16, oy = (b.y >> 16) - r.height_px + 16;
    const int found = target_unit_in_range(ox, oy, b.owner, 0, r.weapon, r.weapon2, true, true);
    if (found >= 0) {
        const Unit& t = units_[size_t(find_unit(found))];
        if (templates_.unit_records[size_t(t.record)].kind == 5) { // a worm with a unit on its pixel: hold fire
            // 0x496a29: FUN_004a5270 on the worm's pixel position (docs/re/17 section 5.4).
            if (unit_at_pixel(t.x >> 16, t.y >> 16, false) >= 0) return true;
        }
        b.target_kind = 1;
        b.target = found;
        building_fire(b, t.x >> 16, t.y >> 16, 1, found);
        return true;
    }
    // Buildings: turrets only unless the owner's AI is active (0x4f4118[side
    // * 0x1DB8 + 1]). Verified in FUN_00496790 (local_4).
    const bool turrets_only = !ai_u8(b.owner, 1);
    const int range = std::max(bullet_range(r.weapon), bullet_range(r.weapon2));
    int bx, by;
    const int tb = target_building_in_range(ox >> 5, oy >> 5, b.owner, range, turrets_only, bx, by);
    if (tb >= 0) {
        b.target_kind = 2;
        b.target = tb;
        building_fire(b, bx * 32 + 16, by * 32 + 16, 2, tb);
        return true;
    }
    return false;
}

void World::building_fire(Building& b, int tx, int ty, int kind, int target) {
    // FUN_00496030.
    const auto& r = templates_.building_records[size_t(b.record)];
    const int ox = (b.x >> 16) + 16, oy = (b.y >> 16) - r.height_px + 16;
    const int f = facing32(ox, oy, tx, ty);
    if (!r.can_fire || (r.needs_power && players_[size_t(b.owner)].power_percent <= 99)) return;
    if (b.turret_facing != f) {
        if (b.turn_counter != 0) {
            --b.turn_counter;
            return;
        }
        b.turn_counter = r.turn_delay;
        const int cw = (f - b.turret_facing + 32) % 32, ccw = (b.turret_facing - f + 32) % 32;
        b.turret_facing = uint8_t((b.turret_facing + (cw < ccw ? 1 : -1) + 32) & 31);
        return;
    }
    int armour = 0;
    bool air = false;
    if (kind == 1 && find_unit(target) >= 0) {
        const Unit& t = units_[size_t(find_unit(target))];
        armour = templates_.unit_records[size_t(t.record)].armour;
        air = (t.flags & kAircraft) != 0;
    }
    const int64_t ddx = int64_t(tx) - ox, ddy = int64_t(ty) - oy;
    const int dist = int(std::sqrt(double(ddx * ddx + ddy * ddy)));
    const int weapon = choose_weapon(r.weapon, r.weapon2, int64_t(dist) * dist, air, armour);
    const int n = dist >> 5;
    if (n != 0) {
        tx += int(rng_.next() % uint32_t(8 * n)) - 4 * n;
        ty += int(rng_.next() % uint32_t(8 * n)) - 4 * n;
    }
    if (r.muzzle_flash >= 0) {
        // At the turret offset, moved by half the frame width (image +0x1a)
        // of the current turret image right and up; frame = the turret facing.
        int h = 0;
        if (r.second_element >= 0 && size_t(r.second_element) < element_frame_w.size()) {
            const int rot = templates_.building[size_t(b.record)][size_t(0x64 + (b.turret_facing & 31))];
            h = element_frame_w[size_t(r.second_element)][size_t(rot & 31)] >> 1;
        }
        spawn_effect_at(r.muzzle_flash, (b.x >> 16) + r.spawn_x + h, (b.y >> 16) + r.spawn_y - h, b.owner,
                        b.turret_facing, 0, 0);
    }
    play_at(templates_.bullets[size_t(weapon)].sound, ox >> 5, oy >> 5);
    const int target_owner = kind == 1 && find_unit(target) >= 0 ? units_[size_t(find_unit(target))].owner : -1;
    if (target_owner == local_player) {
        shooter_reveal(((b.x >> 16) + 16) >> 5, ((b.y >> 16) - r.height_px + 16) >> 5);
    }
    add_bullet(b.owner, weapon, -1000 - b.index, ox, oy, tx, ty, kind == 1 ? target : -1);
    b.reload = r.reload;
}

bool World::is_stealthed(const Unit& u) const {
    if (!(u.flags & 0x10)) return false;
    if (u.flags & 4) return true;
    return u.fade > 1; // still fading in
}

bool World::uncloak(Unit& u) {
    if ((u.flags & 4) && u.fade == 0) {
        u.fade = 22;
        u.flags &= ~4u;
        return true;
    }
    return false;
}

bool World::cloak(Unit& u) {
    if (!(u.flags & 0x10)) return false;
    u.fade = u.fade == 0 ? 22 : uint8_t(22 - u.fade);
    u.flags |= 4;
    return true;
}

bool World::stealth_blocker(int cx, int cy, int side, int r) const {
    // FUN_0049f750: foreign infantry or a foreign armed turret (0x400 cell)
    // in the square; the owner test is "!= side" (allies count); the last
    // map column and row are never scanned (quirk).
    const int x0 = std::max(0, cx - r), x1 = std::min(cx + r + 1, map_.width() - 1);
    const int y0 = std::max(0, cy - r), y1 = std::min(cy + r + 1, map_.height() - 1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const uint32_t f = map_.at(x, y).flags;
            if (int(f & 7) == side) continue;
            if (f & 0x3e0) return true;
            if (f & 0x400) {
                const int b = building_at_cell(x, y);
                if (b >= 0 && templates_.building_records[size_t(buildings_[size_t(b)].record)].weapon != 0xff) return true;
            }
        }
    }
    return false;
}

void World::tick_stealth(Unit& u) {
    // FUN_0049f610 (docs/re/32 section 1.3).
    const uint32_t f = u.flags;
    if (!(f & 0x10) && u.fade == 0) {
        u.flags &= ~4u;
        return;
    }
    if ((f & 4) && u.owner != local_player) u.selected = false;
    if (u.fade != 0) --u.fade;
    if (u.fade != 0) return;
    bool revealed = false;
    if (!(f & 4)) {
        if (stealth_blocker(u.cur_x, u.cur_y, u.owner, 1)) revealed = true;
    } else if (u.hit_age < 5 || u.state == 0x20 || u.state == 0x1f) {
        uncloak(u);
        revealed = true;
    } else if (stealth_blocker(u.cur_x, u.cur_y, u.owner, 1)) {
        uncloak(u);
        revealed = true;
    }
    const auto& r = templates_.unit_records[size_t(u.record)];
    if (uint32_t(u.hit_points) < uint32_t(r.hit_points) / 3 && is_stealthed(u)) {
        uncloak(u);
        return;
    }
    if (!revealed && !(u.flags & 4)) {
        if (u.hit_age >= 5 && u.state != 0x20 && u.state != 0x1f && (r.reload >> 3) + 1 < u.counter_2b &&
            u.state != 0x12 && u.state != 0x13) {
            cloak(u);
        }
    }
}

void World::uncloak_last_units(int side) {
    // FUN_00455480: a side without buildings whose remaining ground units
    // are all cloaked uncloaks the first of them.
    if (has_buildings_[size_t(side)]) return;
    bool visible = false;
    Unit* first = nullptr;
    for (auto& u : units_) {
        if (u.owner != side || u.remove) continue;
        if ((u.flags & 0x40) || templates_.unit_records[size_t(u.record)].kind == 5) continue;
        if (u.flags & 4) {
            if (!first) first = &u;
        } else {
            visible = true;
        }
    }
    if (first && !visible) uncloak(*first);
}

void World::crush_check(Unit& u) {
    // The head of FUN_00497b90 for infantry: in a cell with the vehicle bit,
    // flag 0x1000000 and FUN_0049fa50 (every unit of the cell's owner, list
    // order, in the same current cell and < 16 px away, Chebyshev, crushes
    // it); else the flag is cleared.
    const uint32_t f = map_.at(u.cur_x, u.cur_y).flags;
    if (!(f & 0x8)) {
        u.flags &= ~0x1000000u;
        return;
    }
    u.flags |= 0x1000000;
    const int owner = int(f & 7);
    for (size_t i = 0; i < units_.size(); ++i) {
        const Unit& v = units_[i];
        if (v.owner != owner || v.remove) continue;
        if (v.cur_x != u.cur_x || v.cur_y != u.cur_y) continue;
        const int d = std::max(std::abs((u.x >> 16) - (v.x >> 16)), std::abs((u.y >> 16) - (v.y >> 16)));
        if (d < 16) crush(u, owner, v.index);
    }
}

void World::crush(Unit& u, int side, int by) {
    // FUN_0049f8f0: not when dying or carried / inside (0x400140). Both
    // reserved infantry slots are cleared: the current cell's, and the next
    // cell's while the step is reserved (0x10000000).
    if (u.state == 0x11 || (u.flags & 0x400140)) return;
    u.attacker_side = side;
    u.attacker = by;
    // The counters (docs/re/60 section 2): the victim's side loses it; the
    // crusher's side gets the kill, indexed by its own side (the exe's quirk).
    count_unit_lost(u.owner, u.record);
    count_unit_killed(side, u.record, side);
    set_inf_anim(u, 13);
    kill_unit(u);
    SoundEvent e;
    e.name = "S_CRUSHMAN";
    e.cx = u.cur_x;
    e.cy = u.cur_y;
    sounds.push_back(e);
    u.flags |= 0x400000;
    map_.at(u.cur_x, u.cur_y).flags &= ~(0x20u << (u.slot_cur & 0x1f));
    if ((u.flags & 0x10000000) && u.to_x < map_.width() && u.to_y < map_.height()) {
        map_.at(u.to_x, u.to_y).flags &= ~(0x20u << (u.slot_next & 0x1f));
    }
}

} // namespace d2k::sim
