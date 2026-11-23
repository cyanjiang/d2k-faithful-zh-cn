#pragma once
// The game map (1.06 gGameMap at 0x517df0: 12-byte cells, row table at
// 0x547df0) as built by LoadMapData (FUN_0044c6d0) and the mission setup
// (FUN_00469790). RE: docs/re/06-terrain.md

#include <array>
#include <cstdint>
#include <vector>

#include "formats/map.hpp"
#include "sim/exe_tables.hpp"
#include "sim/rng.hpp"

namespace d2k::sim {

// Bits of a cell's flags word (cell +4). Only those understood so far.
namespace cell {
constexpr uint32_t kSpiceLevelMask = 0x700000; // bits 20-22: 0 none, 1-2 thin, 3+ thick
constexpr int kSpiceLevelShift = 20;
constexpr uint32_t kSand = 0x10000;            // TILEATR "sand"
constexpr uint32_t kBuildable = 0x8000;        // TILEATR "build on"
constexpr uint32_t kInfantry = 0x4000;         // TILEATR "walk on"
constexpr uint32_t kVehicles = 0x2000;         // TILEATR "drive on"
} // namespace cell

// The colour of a cell in the true radar image (0x781e54, docs/re/11
// section 4): 0-7 = a building of that owner (0x795518[owner]), else one
// of the terrain colours below. The sim keeps it; the sidebar draws it.
namespace radar_colour {
constexpr uint8_t kSpice = 8;     // 0x517898 RGB(100, 100, 100)
constexpr uint8_t kSand = 9;      // 0x5173f0 RGB(30, 30, 30)
constexpr uint8_t kDrivable = 10; // 0x5165c0 RGB(60, 50, 50)
constexpr uint8_t kOther = 11;    // 0x516530 RGB(70, 70, 70)
} // namespace radar_colour

struct Cell {
    uint16_t tile = 0;    // +0
    uint32_t flags = 0;   // +4 (on load: the map's special value, then tile attributes)
    uint8_t state = 0x11; // +8 (low nibble 1 = not drawn; the map load writes 0x11 and +9 = 0, 0x44c867)
    uint8_t marks = 0;    // +9: the crater / damage counter (whole byte, docs/re/26)
    // The shown radar has this cell's pixel (copied by the shroud edge pass,
    // docs/re/16-shroud.md section 4.3); black until then.
    bool radar_known = false;
    // The true radar pixel (radar_colour codes). Set by fill_radar and then only
    // by the exe's four pixel writers (building placed / freed, spice eaten
    // to 0, a thin bloom blob), not from the flags: docs/re/11 section 4.
    // Not saved: a loaded game re-colours it (FUN_004418b0).
    uint8_t radar = radar_colour::kOther;
};

// A cell object (0x517b98, 30 x 12 bytes; docs/re/31): crates 0-5, spice
// bloom 7 (unripe) / 8-10 (ripe). Cell flag 0x1000 = an object is here.
struct CellObject {
    uint8_t x = 0, y = 0, type = 0, active = 0;
    uint8_t image = 0;   // +4: index into 0x7951a8 (DATA.R16 102 + image)
    uint8_t regrow = 0;  // +5: bursts left (blooms)
    uint32_t timer = 0;  // +8
};

struct CellRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // half-open
};

class GameMap {
public:
    // Builds the map from the .MAP data: tiles, tile attributes, and spice
    // (special 1 = thin, 2 = thick, 0x29-0x2d = bloom spots), then smooths
    // the spice. Units, buildings and start positions are not placed yet.
    GameMap(const formats::MapFile& map, const formats::TileAttributes& attributes,
            const ExeTables& tables, Rng& rng);
    // With the thick-spice pattern given (FUN_00466140 ran earlier); the
    // rng then only serves the blooms.
    GameMap(const formats::MapFile& map, const formats::TileAttributes& attributes,
            const ExeTables& tables, Rng& rng, const std::array<uint8_t, 64>& pattern);

    int width() const { return width_; }
    int height() const { return height_; }
    const Cell& at(int x, int y) const { return cells_[size_t(y) * width_ + x]; }
    Cell& at(int x, int y) { return cells_[size_t(y) * width_ + x]; }

    int spice_level(int x, int y) const {
        return int((at(x, y).flags & cell::kSpiceLevelMask) >> cell::kSpiceLevelShift);
    }

    std::array<uint8_t, 64>& thick_pattern() { return thick_pattern_; } // saved games
    std::array<CellObject, 30>& objects() { return objects_; }
    const std::array<CellObject, 30>& objects() const { return objects_; }
    // FUN_0044e8e0: the first free entry; false when all 30 are used.
    bool add_object(int x, int y, uint32_t timer, int type, int image, int regrow);

    // FUN_0044ec50: re-picks the spice tiles inside `r` (clipped).
    void smooth_spice(CellRect r);
    // FUN_00462140 (16-bit branch): every cell's true radar colour from its
    // flags. Mission start and after a load only.
    void fill_radar();

private:
    void build(const formats::MapFile& map, const formats::TileAttributes& attributes, Rng& rng);
    int width_ = 0, height_ = 0;
    std::vector<Cell> cells_;
    const ExeTables& tables_;
    std::array<uint8_t, 64> thick_pattern_{}; // 0x790510: random 0..3 per (x&7, y&7)
    std::array<CellObject, 30> objects_{};    // 0x517b98
};

} // namespace d2k::sim
