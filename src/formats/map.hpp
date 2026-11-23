#pragma once
// Dune 2000 maps (MISSIONS\*.MAP, Data\maps\*.MAP) and mission files
// (*.MIS), as read by 1.06 LoadMapData (FUN_0044c6d0) and the mission
// loader (FUN_0044f8e0). Format: docs/formats/maps.md

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::formats {

struct MapCell {
    uint16_t tile = 0;    // index into the tileset (BLOX*.R16 entry)
    uint16_t special = 0; // spice, start position, unit/building to place, ...
};

struct MapFile {
    uint16_t width = 0, height = 0; // at most 128 each
    std::vector<MapCell> cells;     // row-major

    const MapCell& at(int x, int y) const { return cells[size_t(y) * width + size_t(x)]; }
};

MapFile parse_map(std::span<const uint8_t> data);

// The .MIS file is a fixed 68,066-byte dump of exe globals; only the parts
// understood so far are exposed.
// Mission script records (docs/re/19-mission-script.md section 1).
struct MissionCondition {      // 0x1c bytes at 0x10058 (0x6b8280)
    uint32_t a = 0, b = 0, c = 0; // period/time, countdown/counter, count/value
    int32_t x = 0, y = 0;      // REVEALED cell
    float ratio = 0;           // CASUALTIES
    uint8_t side = 0, type = 0, building = 0, unit = 0; // unit = UNITEXISTS record / TIMER operator
};
struct MissionEvent {          // 0x48 bytes at 0xEE58 (0x6b7058)
    uint32_t x = 0, y = 0, value = 0;
    uint8_t conditions = 0, type = 0, count = 0, side = 0;
    uint8_t other = 0, allegiance = 0, action = 0;
    std::array<uint8_t, 14> cond{}, negate{};
    std::array<uint8_t, 20> units{};
    uint32_t message = 0;      // text.uib entry index
};

struct MissionFile {
    std::array<uint8_t, 8> tech_level{};   // per player slot (0x6b87c8)
    std::array<uint32_t, 8> credits{};     // per player slot (0x6b87d0)
    std::array<uint8_t, 8> house{};        // house of each player slot (0x8cd4f0)
    // alliance[a][b]: 0 allied, 1 enemy, 2 neutral (0x798830; .MIS 0xEE18,
    // read by FUN_0044f8e0).
    std::array<std::array<uint8_t, 8>, 8> alliance{};
    // The AI blocks (file +0x58, 8 x 0x1DB8, read into 0x4f4118; doc 22).
    std::array<std::vector<uint8_t>, 8> ai;
    std::vector<MissionCondition> conditions; // count at 0x10729
    std::vector<MissionEvent> events;         // count at 0x10728
    int32_t timer = -1;                       // 0x1072a: mission timer (-1 none)
    std::string tileset;                   // e.g. "BLOXTREE" (0x5166a0)
    std::string tile_attributes;           // e.g. "TILEATR1" -> bin\TILEATR1.BIN (0x5165c8)
};

MissionFile parse_mission(std::span<const uint8_t> data);

// bin\TILEATR*.BIN: 800 u32 tile attributes, then 800 u32 text ids.
struct TileAttributes {
    std::array<uint32_t, 800> flags{};
    // text.uib index of each tile's tooltip (0x516770; 0 / -1 = none).
    std::array<uint32_t, 800> text{};
    static TileAttributes parse(std::span<const uint8_t> data);
};

} // namespace d2k::formats
