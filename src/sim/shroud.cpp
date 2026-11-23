// The shroud: one byte per cell (cell +8) for the local player; reveal
// circles from CIRCLES.BIN, the edge pass and the per-tick unit reveal.
// RE: docs/re/16-shroud.md.
#include <algorithm>
#include <stdexcept>

#include "sim/world.hpp"

namespace d2k::sim {

void World::set_circles(std::span<const uint8_t> data) {
    // CIRCLES.BIN: masks for r = 0..7, (2r+1)^2 bytes each; 0 = reveal, 0xff = keep.
    size_t off = 0;
    for (int r = 0; r < 8; ++r) {
        const size_t n = size_t(2 * r + 1) * size_t(2 * r + 1);
        if (off + n > data.size()) throw std::runtime_error("CIRCLES.BIN: too short");
        circles_[size_t(r)].assign(data.begin() + std::ptrdiff_t(off), data.begin() + std::ptrdiff_t(off + n));
        off += n;
    }
}

void World::edge_pass(int x0, int y0, int x1, int y1) {
    // FUN_0044cd10: row by row, in place; off-map neighbours count as shroud.
    const int w = map_.width(), h = map_.height();
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, w);
    y1 = std::min(y1, h);
    auto shaped = [&](int x, int y) { return (map_.at(x, y).state & 0xf) != 0; };
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            auto& c = map_.at(x, y);
            if (c.state & 0xf) {
                uint32_t m = 0;
                if (x == 0) m = 0x29;
                if (x == w - 1) m |= 0x94;
                if (y == h - 1) m |= 0xe0;
                if (y == 0) m |= 0x07;
                if (y > 0) {
                    if (x > 0 && shaped(x - 1, y - 1)) m |= 0x01;
                    if (shaped(x, y - 1)) m |= 0x02;
                    if (x < w - 1 && shaped(x + 1, y - 1)) m |= 0x04;
                }
                if (x > 0 && shaped(x - 1, y)) m |= 0x08;
                if (x < w - 1 && shaped(x + 1, y)) m |= 0x10;
                if (y < h - 1) {
                    if (x > 0 && shaped(x - 1, y + 1)) m |= 0x20;
                    if (shaped(x, y + 1)) m |= 0x40;
                    if (x < w - 1 && shaped(x + 1, y + 1)) m |= 0x80;
                }
                c.state = uint8_t((((y & 1) << 1 | (x & 1)) << 4) | tables_.shroud_shape[m]);
            }
            if ((c.state & 0xf) != 1) c.radar_known = true; // copy the radar pixel
        }
    }
}

void World::reveal_circle(int cx, int cy, int r) {
    // FUN_0044e3b0.
    if (r < 0 || r > 7 || circles_[size_t(r)].empty()) return; // r > 7: fatal in the exe
    const auto& mask = circles_[size_t(r)];
    const int n = 2 * r + 1;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= map_.width() || y >= map_.height()) continue;
            map_.at(x, y).state &= mask[size_t((dy + r) * n + (dx + r))];
        }
    }
    edge_pass(cx - r - 1, cy - r - 1, cx + r + 2, cy + r + 2);
}

void World::reveal_building(int record, int cx, int cy) {
    // FUN_0044e330: radius record +0x0e, centre (cx + w/64, cy + h/64).
    const auto& r = templates_.building_records[size_t(record)];
    reveal_circle(cx + r.width_px / 64, cy + r.height_px / 64, r.view);
}

void World::reveal_units() {
    // FUN_0044cff0: once per tick; each unit of a player p with
    // alliance[p][local] == 0 (0x798830, the local player included) reveals
    // when index & 7 == counter & 7. Allied buildings never reveal.
    ++reveal_counter_;
    for (const auto& u : units_) {
        if (alliance_[size_t(u.owner)][size_t(local_player)] != 0 ||
            (uint32_t(u.slot) & 7) != (reveal_counter_ & 7)) {
            continue;
        }
        const int view = templates_.unit_records[size_t(u.record)].view;
        if (view == 0 || (u.flags & 0x100)) continue;
        reveal_circle(u.to_x, u.to_y, view);
    }
}

void World::reveal_start() {
    // FUN_0049f4d0: 8 unit sweeps, then the local player's buildings.
    for (int i = 0; i < 8; ++i) reveal_units();
    for (const auto& b : buildings_) {
        if (b.owner != local_player) continue;
        const auto& r = templates_.building_records[size_t(b.record)];
        reveal_building(b.record, (b.x >> 16) >> 5, ((b.y >> 16) - r.height_px) >> 5);
    }
}

void World::open_map() {
    // FUN_0044c480: every shroud byte 0. Used by the "reveal map" crate (which
    // then copies the radar itself) and at the end of a network game.
    for (int y = 0; y < map_.height(); ++y) {
        for (int x = 0; x < map_.width(); ++x) map_.at(x, y).state = 0;
    }
}

void World::rebuild_shown_radar() {
    // FUN_004418b0 (0x441e4c / 0x441ebb): FUN_00462140 clears the shown radar
    // to black, then each cell whose whole shroud byte is 0 gets its true
    // pixel. Edge cells stay black until a later edge pass covers them.
    for (int y = 0; y < map_.height(); ++y) {
        for (int x = 0; x < map_.width(); ++x) map_.at(x, y).radar_known = map_.at(x, y).state == 0;
    }
}

} // namespace d2k::sim
