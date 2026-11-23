#include "formats/palette.hpp"

#include "formats/byte_reader.hpp"

namespace d2k::formats {

Palette parse_vga_palette(std::span<const uint8_t> data) {
    if (data.size() != 768) {
        throw FormatError("VGA palette must be 768 bytes, got " + std::to_string(data.size()));
    }
    Palette pal{};
    for (size_t i = 0; i < 256; ++i) {
        uint8_t c[3] = {data[i * 3], data[i * 3 + 1], data[i * 3 + 2]};
        for (uint8_t v : c) {
            if (v > 63) {
                throw FormatError("VGA palette component out of 6-bit range");
            }
        }
        pal[i] = {vga6_to_8(c[0]), vga6_to_8(c[1]), vga6_to_8(c[2])};
    }
    return pal;
}

} // namespace d2k::formats
