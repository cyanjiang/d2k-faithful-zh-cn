#include "formats/bmp.hpp"

#include <cstdlib>

#include "formats/byte_reader.hpp"

namespace d2k::formats {

Image8 parse_bmp8(std::span<const uint8_t> data) {
    ByteReader r(data);
    if (r.u8() != 'B' || r.u8() != 'M') throw FormatError("BMP: bad magic");
    r.skip(8);
    uint32_t pixel_offset = r.u32();
    uint32_t header_size = r.u32();
    int32_t width = int32_t(r.u32());
    int32_t height = int32_t(r.u32());
    r.u16(); // planes
    uint16_t bpp = r.u16();
    uint32_t compression = r.u32();
    r.skip(12);
    uint32_t colours = r.u32();
    if (bpp != 8 || compression != 0 || width <= 0 || height == 0) {
        throw FormatError("BMP: only 8-bit uncompressed images are supported");
    }
    if (colours == 0 || colours > 256) colours = 256;

    Image8 img;
    img.width = uint32_t(width);
    img.height = uint32_t(std::abs(height));
    ByteReader pal(data.subspan(14 + header_size));
    for (uint32_t i = 0; i < colours; ++i) {
        uint8_t b = pal.u8(), g = pal.u8(), rr = pal.u8();
        pal.u8();
        img.palette[i] = {rr, g, b};
    }
    const size_t stride = (img.width + 3) & ~size_t(3);
    if (pixel_offset + stride * img.height > data.size()) throw FormatError("BMP: truncated");
    img.indices.resize(size_t(img.width) * img.height);
    for (uint32_t y = 0; y < img.height; ++y) {
        uint32_t src_row = height > 0 ? img.height - 1 - y : y;
        const uint8_t* src = data.data() + pixel_offset + stride * src_row;
        std::copy(src, src + img.width, img.indices.begin() + std::ptrdiff_t(size_t(y) * img.width));
    }
    return img;
}

} // namespace d2k::formats
