#include "render/terrain.hpp"

namespace d2k::render {

void draw_terrain(const sim::GameMap& map, const std::vector<formats::R8Image>& tiles,
                  ui::Surface& screen, int scroll_x, int scroll_y) {
    auto& out = screen.pixels();
    const int map_w = map.width() * kCellSize, map_h = map.height() * kCellSize;
    for (int py = 0; py < kPlayfieldHeight; ++py) {
        const int wy = scroll_y + py;
        uint16_t* row = out.data() + size_t(py + kPlayfieldY) * screen.width() + kPlayfieldX;
        for (int px = 0; px < kPlayfieldWidth; ++px) {
            const int wx = scroll_x + px;
            uint16_t c = 0;
            if (wx >= 0 && wy >= 0 && wx < map_w && wy < map_h) {
                const auto& t = tiles[map.at(wx / kCellSize, wy / kCellSize).tile];
                if (!t.pixels.empty() && t.bits_per_pixel == 16) {
                    size_t si = (size_t(wy % kCellSize) * t.width + size_t(wx % kCellSize)) * 2;
                    c = uint16_t(t.pixels[si] | t.pixels[si + 1] << 8);
                }
            }
            row[px] = c;
        }
    }
}

} // namespace d2k::render
