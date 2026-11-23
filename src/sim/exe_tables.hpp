#pragma once
// Constant tables the game keeps only inside its executable, read at
// runtime from the player's own 1.06 DUNE2000.DAT (never shipped).
// Addresses and meaning: docs/re/06-terrain.md

#include <array>
#include <cstdint>

#include "formats/pe_image.hpp"

namespace d2k::sim {

struct ExeTables {
    // FUN_0044ec50: thin spice tile by 8-neighbour mask (0x4e0c18).
    std::array<uint16_t, 256> spice_edge{};
    // FUN_0044ec50: thick spice tiles, picked by the random 8x8 pattern (0x4e0b10).
    std::array<uint16_t, 4> thick_spice{};
    // Sand tiles a fully harvested cell turns into, picked at random (0x4e9520).
    std::array<uint16_t, 8> harvested_sand{};
    // Infantry positions inside a cell, 5 (x, y) pairs in pixels (0x4c93e8;
    // the unit code subtracts 16 to get offsets from the centre).
    std::array<std::array<int32_t, 2>, 5> infantry_subpos{};

    // FUN_0044cd10: shroud shape by 8-neighbour shroud mask (0x4e0b18).
    std::array<uint8_t, 256> shroud_shape{};

    static ExeTables load(const formats::PeImage& exe);
};

} // namespace d2k::sim
