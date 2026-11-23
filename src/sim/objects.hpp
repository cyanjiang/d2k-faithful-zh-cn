#pragma once
// Units and buildings placed from a map's special values (FUN_00469790 ->
// unit creation FUN_00455870, building creation FUN_004563b0).
// RE: docs/re/08-templates.md. Only what drawing needs so far.

#include <array>
#include <cstdint>
#include <vector>

#include "formats/map.hpp"
#include "formats/templates.hpp"
#include "sim/exe_tables.hpp"

namespace d2k::sim {

struct Object {
    bool building = false;
    int type = 0;      // unit / building type id (name index)
    int record = 0;    // template record for the owner's house
    int owner = 0;     // player slot 0..7
    int colour = -1;   // colour slot 0x5175d8[owner]; -1 = the owner (render only)
    int house = 0;     // house of the owner (.MIS house byte)
    int cell_x = 0, cell_y = 0;
    int32_t x = 0, y = 0; // 16.16 pixels: unit = cell centre, building = bottom-left of footprint
    int32_t altitude = 0;  // units +0x3c (16.16); part of the draw-list sort key (render only)
    int turret_facing = 0; // units: +0x55, the turret image's facing (FUN_00428e30)
    int facing = 0;       // 0..31. CreateUnit (0x455a55): 0 (north) when spawn cell = move cell, as for map units
    int frame = 0;        // animation frame (obj +0x1e / +0x39; walls / turrets: +0x20)
    int damage_frame = 0; // buildings: the damaged image's frame (+0x20: 0 except walls / turrets)
    int anim_mode = 0;    // units: +0x20 (infantry animation mode)
    bool repair_blink = false; // buildings: repairing and (index + tick) & 0x20
    int hit_points = 0;
    // Buildings (docs/re/12 section 7): object flags +0x28, build-up +0x3e
    // and animation +0x3f counters.
    uint32_t flags = 0;       // buildings +0x28; units +0x08 (stealth bits 4 / 0x10)
    int buildup = 0, anim = 0;
    int fade = 0, index = 0;  // units: stealth fade +0x31, own index +0x8c (the shimmer phase)
    int flash = 0;            // units +0x1D / buildings +0x38: grey while flash & 2 (docs/re/44)
    // Infantry in a vehicle cell (unit flag 0x1000000): drawn by the separate
    // pass FUN_0042af00 before the buildings (stealth quirk: see draw_objects).
    bool infantry_pass = false;
};

// Places the map's objects. `houses` = the .MIS house byte per player slot.
// Sandworms and other special objects are included; they have no graphic
// element (drawn specially, TODO).
std::vector<Object> place_map_objects(const formats::MapFile& map, const formats::TileData& tile_data,
                                      const formats::Templates& templates,
                                      const std::array<uint8_t, 8>& houses, const ExeTables& tables);

} // namespace d2k::sim
