#pragma once
// 16-bit uncompressed TGA, as used for all UI graphics in Data\UI_GFX
// (16-bit display mode). Format: docs/formats/ui-graphics.md

#include <cstdint>
#include <span>
#include <vector>

namespace d2k::formats {

// RGB555 pixels, top row first (xRRRRRGGGGGBBBBB; the alpha bit is never set
// in the 1.06 data and is cleared here).
struct Image16 {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint16_t> pixels;

    uint16_t at(uint32_t x, uint32_t y) const { return pixels[size_t(y) * width + x]; }
};

Image16 parse_tga16(std::span<const uint8_t> data);

} // namespace d2k::formats
