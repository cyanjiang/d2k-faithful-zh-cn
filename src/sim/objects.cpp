#include "sim/objects.hpp"

namespace d2k::sim {

std::vector<Object> place_map_objects(const formats::MapFile& map, const formats::TileData& tile_data,
                                      const formats::Templates& templates,
                                      const std::array<uint8_t, 8>& houses, const ExeTables& tables) {
    (void)tables; // the sub-position is applied by World::occupy
    std::vector<Object> out;
    for (int cy = 0; cy < map.height; ++cy) {
        for (int cx = 0; cx < map.width; ++cx) {
            const uint16_t special = map.at(cx, cy).special;
            // Specials the mission set-up handles itself (spice, start
            // positions, blooms) never reach the object table.
            if (special <= 3 || special == 0x17 || (special >= 0x29 && special <= 0x2d)) continue;
            formats::MapObject mo;
            if (!tile_data.object(special, mo)) continue;
            Object o;
            o.building = mo.building;
            o.type = mo.type;
            o.owner = mo.owner & 7;
            o.house = houses[size_t(o.owner)];
            o.cell_x = cx;
            o.cell_y = cy;
            if (o.building) {
                o.record = templates.building_record(o.type, o.house);
                if (o.record < 0) continue;
                const auto& r = templates.building_records[size_t(o.record)];
                o.x = int32_t(cx * 32) << 16;
                o.y = int32_t(cy * 32 + r.height_px) << 16;
                // The record's full hit points. The World creates the building
                // with FUN_004563b0 (flag 5), which lays its concrete and then
                // applies the concrete rule (docs/re/12 section 6 step 5).
                o.hit_points = r.hit_points;
            } else {
                o.record = templates.unit_record(o.type, o.house);
                if (o.record < 0) continue;
                const auto& r = templates.unit_records[size_t(o.record)];
                // FUN_00455870 starts every unit at the cell centre
                // (cx * 32 + 16 + x offset, 0 for map units). A soldier then
                // takes the first free sub-position from slot 4 down (its
                // 0x200 >> k loop) and moves by subpos[slot] - 16 (table
                // 0x4c93e8); World::occupy does that on creation.
                o.x = int32_t(cx * 32 + 16) << 16;
                o.y = int32_t(cy * 32 + 16) << 16;
                o.hit_points = r.hit_points;
            }
            out.push_back(o);
        }
    }
    return out;
}

} // namespace d2k::sim
