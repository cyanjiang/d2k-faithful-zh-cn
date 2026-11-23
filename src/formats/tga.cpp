#include "formats/tga.hpp"

#include "formats/byte_reader.hpp"

namespace d2k::formats {

Image16 parse_tga16(std::span<const uint8_t> data) {
    ByteReader r(data);
    uint8_t id_length = r.u8();
    uint8_t colour_map_type = r.u8();
    uint8_t image_type = r.u8();
    r.skip(5 + 4); // colour map spec, x/y origin
    Image16 img;
    img.width = r.u16();
    img.height = r.u16();
    uint8_t bpp = r.u8();
    uint8_t descriptor = r.u8();
    if (colour_map_type != 0 || image_type != 2 || bpp != 16) {
        throw FormatError("TGA: only uncompressed 16-bit true-colour is supported");
    }
    r.skip(id_length);
    // Some files (e.g. WWLOGO.TGA) carry extra bytes after the pixels; ignore.
    auto px = r.bytes(size_t(img.width) * img.height * 2);
    img.pixels.resize(size_t(img.width) * img.height);
    const bool top_down = (descriptor & 0x20) != 0;
    for (uint32_t y = 0; y < img.height; ++y) {
        uint32_t dst_row = top_down ? y : img.height - 1 - y;
        for (uint32_t x = 0; x < img.width; ++x) {
            size_t s = (size_t(y) * img.width + x) * 2;
            img.pixels[size_t(dst_row) * img.width + x] = uint16_t((px[s] | (px[s + 1] << 8)) & 0x7FFF);
        }
    }
    return img;
}

} // namespace d2k::formats
