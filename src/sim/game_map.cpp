#include "sim/game_map.hpp"

#include <algorithm>

namespace d2k::sim {

namespace {
constexpr uint16_t kThinSpiceTile = 0x319;
constexpr uint16_t kThickSpiceTile = 0x2ed;
constexpr uint32_t kThinSpiceFlags = 0x216000;  // level 2 + sand / walk / drive
constexpr uint32_t kThickSpiceFlags = 0x416000; // level 4
constexpr uint32_t kBloomFlags = 0x217000;      // level 2 + sand / walk / drive + 0x1000 (cell object)
} // namespace

GameMap::GameMap(const formats::MapFile& map, const formats::TileAttributes& attributes,
                 const ExeTables& tables, Rng& rng)
    : width_(map.width), height_(map.height), cells_(map.cells.size()), tables_(tables) {
    // FUN_00466140 fills the thick-spice pattern once during game data
    // set-up (tests build the map this way; the game passes the pattern).
    for (auto& p : thick_pattern_) p = uint8_t(rng.next() & 3);
    build(map, attributes, rng);
}

GameMap::GameMap(const formats::MapFile& map, const formats::TileAttributes& attributes,
                 const ExeTables& tables, Rng& rng, const std::array<uint8_t, 64>& pattern)
    : width_(map.width), height_(map.height), cells_(map.cells.size()), tables_(tables), thick_pattern_(pattern) {
    build(map, attributes, rng);
}

void GameMap::build(const formats::MapFile& map, const formats::TileAttributes& attributes, Rng& rng) {
    // LoadMapData + FUN_00469790: flags become the tile's attributes, then
    // the map's special values turn into spice.
    for (size_t i = 0; i < cells_.size(); ++i) {
        Cell& c = cells_[i];
        c.tile = map.cells[i].tile;
        c.flags = c.tile < attributes.flags.size() ? attributes.flags[c.tile] : 0;
        switch (map.cells[i].special) {
        case 1:
            c.tile = kThinSpiceTile;
            c.flags = kThinSpiceFlags;
            break;
        case 2:
            c.tile = kThickSpiceTile;
            c.flags = kThickSpiceFlags;
            break;
        case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: {
            // A spice bloom (docs/re/31 section 2): one rand() for its timer,
            // an unripe bloom (type 7, image 4) with the regrow count; the
            // flags are then assigned, so 0x1000 stays even when the table
            // was full.
            static constexpr uint8_t kRegrow[5] = {5, 10, 25, 50, 255};
            const uint32_t t = rng.next() % 1000 + 2000;
            add_object(int(i % size_t(width_)), int(i / size_t(width_)), t, 7, 4, kRegrow[map.cells[i].special - 0x29]);
            c.tile = kThinSpiceTile;
            c.flags = kBloomFlags;
            break;
        }
        default:
            // 0x17 start positions, 0x3c-0x3e and the unit / building table:
            // placed with objects (M3/M4).
            break;
        }
    }
    // FUN_00469790 colours the radar (0x469b12) before it retiles the spice;
    // the tiles do not matter to it.
    fill_radar();
    smooth_spice({0, 0, width_, height_});
}

void GameMap::fill_radar() {
    for (auto& c : cells_) {
        const uint32_t f = c.flags;
        if (f & 0x10) c.radar = uint8_t(f & 7);
        else if (f & cell::kSpiceLevelMask) c.radar = radar_colour::kSpice;
        else if (f & cell::kSand) c.radar = radar_colour::kSand;
        else if (f & cell::kVehicles) c.radar = radar_colour::kDrivable;
        else c.radar = radar_colour::kOther;
    }
}

bool GameMap::add_object(int x, int y, uint32_t timer, int type, int image, int regrow) {
    for (auto& e : objects_) {
        if (e.active) continue;
        e = CellObject{uint8_t(x), uint8_t(y), uint8_t(type), 1, uint8_t(image), uint8_t(regrow), timer};
        at(x, y).flags |= 0x1000;
        return true;
    }
    return false;
}

void GameMap::smooth_spice(CellRect r) {
    r.x0 = std::max(r.x0, 0);
    r.y0 = std::max(r.y0, 0);
    r.x1 = std::min(r.x1, width_);
    r.y1 = std::min(r.y1, height_);
    auto has = [&](int x, int y) { return (at(x, y).flags & cell::kSpiceLevelMask) != 0; };
    for (int y = r.y0; y < r.y1; ++y) {
        for (int x = r.x0; x < r.x1; ++x) {
            Cell& c = at(x, y);
            uint32_t level = c.flags & cell::kSpiceLevelMask;
            if (level > 0x200000) {
                c.tile = tables_.thick_spice[thick_pattern_[size_t((y & 7) * 8 + (x & 7))]];
                continue;
            }
            if (level == 0) continue;
            // Thin spice: neighbours with spice, the map edge counting as
            // spice. NW 1, N 2, NE 4, W 8, E 0x10, SW 0x20, S 0x40, SE 0x80.
            uint8_t mask = 0;
            if (x < 1) {
                mask = 0x29;
            } else {
                if (has(x - 1, y)) mask = 8;
                if (y < height_ - 1 && has(x - 1, y + 1)) mask |= 0x20;
                if (y > 0 && has(x - 1, y - 1)) mask |= 1;
            }
            if (x < width_ - 1) {
                if (has(x + 1, y)) mask |= 0x10;
                if (y > 0 && has(x + 1, y - 1)) mask |= 4;
                if (y < height_ - 1 && has(x + 1, y + 1)) mask |= 0x80;
            } else {
                mask |= 0x94;
            }
            if (y < height_ - 1) {
                if (has(x, y + 1)) mask |= 0x40;
            } else {
                mask |= 0xe0;
            }
            if (y < 1) {
                mask |= 7;
            } else if (has(x, y - 1)) {
                mask |= 2;
            }
            c.tile = tables_.spice_edge[mask];
        }
    }
}

} // namespace d2k::sim
