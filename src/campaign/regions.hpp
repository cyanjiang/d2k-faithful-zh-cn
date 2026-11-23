#pragma once
// The 27 territories of the campaign map. The exe keeps their shapes in a
// table at 0x4be3f4 (1.06): 0x48-byte records {i32 x, i32 y, u32 count,
// POINT points[7]}; x/y is where the territory's image is drawn, the
// polygon is its click region (CreatePolygonRgn, ALTERNATE).

#include <array>
#include <vector>

#include "formats/pe_image.hpp"

namespace d2k::campaign {

struct MapPoint {
    int x = 0, y = 0;
};

struct Region {
    int x = 0, y = 0;                // image position
    std::vector<MapPoint> polygon;   // click region
    MapPoint label;                  // centroid - (16, 32), as CreateGlobe

    // PtInRegion on the polygon (FUN_0042cf10): an even-odd test at the
    // integer point, so left / top edges are inside, right / bottom outside.
    bool contains(int px, int py) const;
};

// Reads the table from the player's 1.06 exe.
std::array<Region, 27> load_regions(const formats::PeImage& exe);

} // namespace d2k::campaign
