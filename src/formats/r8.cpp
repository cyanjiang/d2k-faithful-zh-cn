#include "formats/r8.hpp"

#include <string>

#include "formats/byte_reader.hpp"

namespace d2k::formats {

namespace {

// Sanity limit; the largest image in the 1.06 data is far below this.
constexpr uint32_t kMaxDimension = 4096;

} // namespace

// One entry, as SetTImageToResource (1.06 FUN_00460550) reads it: pixel
// data only when the image handle is nonzero, a palette only for type 1
// with a palette handle (type 2 then reuses the last type-1 palette).
R8Image parse_r8_entry(ByteReader& r, size_t index) {
    R8Image img;
    size_t entry_pos = r.pos();
    img.type = r.u8();
    if (img.type == 0) return img;
    if (img.type != 1 && img.type != 2) {
        throw FormatError("R8 entry " + std::to_string(index) + " at offset " +
                          std::to_string(entry_pos) + ": unknown type " + std::to_string(img.type));
    }
    {
        img.width = r.u32();
        img.height = r.u32();
        img.offset_x = r.i32();
        img.offset_y = r.i32();
        img.image_handle = r.u32();
        img.palette_handle = r.u32();
        img.bits_per_pixel = r.u8();
        img.frame_height = r.u8();
        img.frame_width = r.u8();
        r.u8(); // padding, always 0 in 1.06 data
        if (img.width > kMaxDimension || img.height > kMaxDimension ||
            (img.bits_per_pixel != 8 && img.bits_per_pixel != 16)) {
            throw FormatError("R8 entry " + std::to_string(index) + " at offset " +
                              std::to_string(entry_pos) + ": implausible header");
        }
        if (img.image_handle != 0) {
            auto px = r.bytes(size_t(img.width) * img.height * (img.bits_per_pixel / 8));
            img.pixels.assign(px.begin(), px.end());
        }
        if (img.type == 1 && img.palette_handle != 0) {
            r.u32(); // varies (1, 32, 252, ...); read by nothing (FUN_00460550 and the blit use only +4)
            r.u32(); // runtime handle
            std::array<uint16_t, 256> pal{};
            for (auto& c : pal) {
                c = r.u16();
            }
            img.palette = pal;
        }
    }
    return img;
}

std::vector<R8Image> parse_r8(std::span<const uint8_t> data) {
    std::vector<R8Image> images;
    ByteReader r(data);
    while (r.remaining() > 0) images.push_back(parse_r8_entry(r, images.size()));
    return images;
}

UiResource parse_ui_resource(std::span<const uint8_t> data) {
    ByteReader r(data);
    UiResource ui;
    size_t n = 0;
    for (auto& c : ui.controls) {
        c.x = r.i32();
        c.y = r.i32();
        for (auto& img : c.images) img = parse_r8_entry(r, n++);
    }
    for (auto& img : ui.extra) img = parse_r8_entry(r, n++);
    if (r.remaining() != 0) throw FormatError("UI resource: trailing bytes");
    return ui;
}

} // namespace d2k::formats
