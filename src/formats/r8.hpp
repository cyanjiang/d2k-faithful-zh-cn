#pragma once
// R8 / R16 image containers (DATA.R8, BLOX*.R8, MOUSE.R16, ...).
// A file is a flat sequence of entries with no file header or index; the
// sprite number is the entry's position. Format: docs/formats/r8.md

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace d2k::formats {

struct R8Image {
    // 0 = empty placeholder (1 byte on disk), 1 = image,
    // 2 = image without embedded palette data: it takes the palette of the
    // last type-1 entry that carried one (FUN_00460550, 0x79556c).
    uint8_t type = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    // Signed hotspot: the image is drawn at (x - offset_x, y - offset_y)
    // (FUN_00428e30, FUN_0042dc00); 1,080 DATA.R16 entries have offset_x < 0.
    int32_t offset_x = 0;
    int32_t offset_y = 0;
    uint32_t image_handle = 0;   // runtime pointer saved by the tool; ignore
    uint32_t palette_handle = 0; // nonzero = has a palette (embedded if type 1)
    uint8_t bits_per_pixel = 0;  // 8 = palette indices, 16 = RGB555 pixels
    uint8_t frame_height = 0;
    uint8_t frame_width = 0;
    // width*height bytes (8 bpp) or width*height uint16 LE (16 bpp), raw;
    // empty when the entry carries no pixel data (image_handle == 0).
    std::vector<uint8_t> pixels;
    // Embedded RGB555 palette (type 1 with palette_handle != 0); 555, not
    // 565: see docs/re/11-sidebar.md.
    std::optional<std::array<uint16_t, 256>> palette;

    bool empty() const { return type == 0; }
};

std::vector<R8Image> parse_r8(std::span<const uint8_t> data);

// Data/UI_<lang>.R16 / .R8, the in-game interface (1.06 FUN_00460ec0):
// 15 controls of {i32 x, i32 y, 3 images}, then 10 more images.
// The controls and images are named and used in docs/re/07-ingame-screen.md.
struct UiResource {
    struct Control {
        int32_t x = 0, y = 0;
        std::array<R8Image, 3> images; // in file order (exe slots -3, -2, -4)
    };
    std::array<Control, 15> controls;
    std::array<R8Image, 10> extra;
};

UiResource parse_ui_resource(std::span<const uint8_t> data);

} // namespace d2k::formats
