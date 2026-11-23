#pragma once
// Palettes used by Dune 2000.
//
//  - bin\PALETTE.BIN: 256 x RGB, 6-bit VGA components (0..63), 768 bytes.
//  - Embedded R8 palettes: 256 x uint16 RGB555 (see r8.hpp).
// Details: docs/formats/r8.md

#include <array>
#include <cstdint>
#include <span>

namespace d2k::formats {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};

using Palette = std::array<Rgb, 256>;

// Parses a 768-byte 6-bit VGA palette.
Palette parse_vga_palette(std::span<const uint8_t> data);

// Expands a 6-bit VGA component to 8 bits as the exe programs the
// DirectDraw palette (FUN_004a3f50: each component << 2, peFlags 4). Only
// the 8-bit display mode uses PALETTE.BIN (FUN_0046aa00 runs only when the
// depth is 8, 0x4485c9); we use it for the R8 tools.
constexpr uint8_t vga6_to_8(uint8_t v) {
    return static_cast<uint8_t>(v << 2);
}

constexpr Rgb rgb565_to_rgb(uint16_t c) {
    uint8_t r = static_cast<uint8_t>((c >> 11) & 31);
    uint8_t g = static_cast<uint8_t>((c >> 5) & 63);
    uint8_t b = static_cast<uint8_t>(c & 31);
    return {static_cast<uint8_t>((r << 3) | (r >> 2)),
            static_cast<uint8_t>((g << 2) | (g >> 4)),
            static_cast<uint8_t>((b << 3) | (b >> 2))};
}

// R16 pixel data is xRRRRRGGGGGBBBBB (verified: BLOXBASE.R16 pixels match
// the R8 version's PALETTE.BIN colours to within rounding).
constexpr Rgb rgb555_to_rgb(uint16_t c) {
    uint8_t r = static_cast<uint8_t>((c >> 10) & 31);
    uint8_t g = static_cast<uint8_t>((c >> 5) & 31);
    uint8_t b = static_cast<uint8_t>(c & 31);
    return {static_cast<uint8_t>((r << 3) | (r >> 2)), static_cast<uint8_t>((g << 3) | (g >> 2)),
            static_cast<uint8_t>((b << 3) | (b >> 2))};
}

} // namespace d2k::formats
