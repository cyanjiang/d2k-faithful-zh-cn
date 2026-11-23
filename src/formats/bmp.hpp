#pragma once
// 8-bit palettised Windows BMP (Data\UI_GFX\*.BMP: all 275 in 1.06 are
// 8 bpp, uncompressed, bottom-up). Format: docs/formats/fonts.md#ui-graphics

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "formats/palette.hpp"

namespace d2k::formats {

struct Image8 {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> indices; // top row first
    std::array<Rgb, 256> palette{};

    uint8_t at(uint32_t x, uint32_t y) const { return indices[size_t(y) * width + x]; }
};

Image8 parse_bmp8(std::span<const uint8_t> data);

} // namespace d2k::formats
