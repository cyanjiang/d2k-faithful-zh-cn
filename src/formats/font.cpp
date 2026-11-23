#include "formats/font.hpp"

#include "formats/byte_reader.hpp"

namespace d2k::formats {

namespace {
constexpr size_t kSlots = 256;
constexpr size_t kGlyphDataOffset = 8 + kSlots * 4; // the 0x408-byte block
} // namespace

Font::Font() {
    for (size_t i = 0; i < char_map.size(); ++i) char_map[i] = uint8_t(i);
}

void Font::set_char_map(std::span<const uint8_t> font_bin) {
    if (font_bin.size() != char_map.size()) throw FormatError("font map must be 256 bytes");
    for (size_t i = 0; i < char_map.size(); ++i) char_map[i] = font_bin[i];
}

const Glyph* Font::glyph(unsigned char c) const {
    // FUN_0042dc00: map[c] == 0x20 advances by the space width; any other
    // value is the glyph slot map[c] - first char.
    const uint8_t m = char_map[c];
    if (m == 0x20 || m < first_char || size_t(m - first_char) >= glyphs.size()) return nullptr;
    const Glyph& g = glyphs[size_t(m - first_char)];
    return g.width == 0 ? nullptr : &g;
}

Font parse_font(std::span<const uint8_t> data) {
    ByteReader r(data);
    Font f;
    for (auto& b : f.header) b = r.u8();
    f.first_char = f.header[2];
    f.height = r.u32();
    r.seek(kGlyphDataOffset);
    // FUN_0042d6f0 (retail branch): 256 records; (0, 0) leaves the slot empty.
    size_t used = 0;
    for (size_t i = 0; i < kSlots; ++i) {
        Glyph g;
        g.width = r.u32();
        g.height = r.u32();
        if (g.width != 0 || g.height != 0) {
            if (g.width > 64 || g.height != f.height) throw FormatError("font: implausible glyph");
            auto px = r.bytes(size_t(g.width) * g.height);
            g.pixels.assign(px.begin(), px.end());
            used = i + 1;
        }
        f.glyphs.push_back(std::move(g));
    }
    f.glyphs.resize(used);
    if (r.remaining() != 0) throw FormatError("font: trailing data");
    return f;
}

std::array<uint16_t, 16> parse_font_palette(std::span<const uint8_t> data) {
    if (data.size() != 1024) throw FormatError("font palette must be 1024 bytes");
    std::array<uint16_t, 16> out{};
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = uint16_t((data[i * 4 + 2] >> 3) << 10 | (data[i * 4 + 1] >> 3) << 5 | data[i * 4] >> 3);
    }
    return out;
}

ColourTable ColourTable::parse(std::span<const uint8_t> data) {
    ByteReader r(data);
    uint32_t n = r.u32();
    ColourTable t;
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t len = r.u16();
        std::string name = r.fixed_string(len);
        uint8_t b = r.u8(), g = r.u8(), rr = r.u8();
        r.u8();
        t.entries_.emplace_back(std::move(name), Rgb{rr, g, b});
    }
    if (r.remaining() != 0) throw FormatError("colours: trailing bytes");
    return t;
}

std::optional<Rgb> ColourTable::get(const std::string& name) const {
    for (const auto& [n, c] : entries_) {
        if (n == name) return c;
    }
    return std::nullopt;
}

} // namespace d2k::formats
