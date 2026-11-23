#pragma once
// Fonts (Data\FONT.FNT, Data\FONTCOL.FNT + FONTCOL.FPL, bin\FONT.BIN) and
// the named UI colour table (UI_DATA\colours.uib). Format: docs/formats/fonts.md

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "formats/palette.hpp"

namespace d2k::formats {

struct Glyph {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // width*height indices; 0 = empty
};

// The 0x408-byte block FUN_0042d6f0 reads straight into 0x513b20 + font *
// 0x408: [0] loaded flag, [1] space width, [2] first character, [3] spacing,
// u32 height, 256 runtime glyph pointers. Then 256 records {u32 w, u32 h,
// w*h bytes}; (0, 0) = no glyph.
struct Font {
    uint8_t header[4] = {}; // loaded flag, space width, first char, spacing
    uint32_t height = 0;
    uint8_t first_char = 0;   // glyphs[i] is mapped character first_char + i
    std::vector<Glyph> glyphs; // slot order; empty slots have width 0
    // bin\FONT.BIN (FUN_0042e450): character -> glyph character; 0x20 = a
    // space. Identity until set_char_map.
    std::array<uint8_t, 256> char_map{};

    Font();
    void set_char_map(std::span<const uint8_t> font_bin);
    // FUN_0042dc00: nullptr when the character maps to a space.
    const Glyph* glyph(unsigned char c) const;
    uint32_t space_width() const { return header[1]; }
    // Extra pixels after each glyph (0x513b23; FONT.FNT 1, FONTCOL.FNT 0).
    uint32_t spacing() const { return header[3]; }
};

Font parse_font(std::span<const uint8_t> data);

// FONTCOL.FPL: 256 x {B, G, R, A}; FUN_0042e1c0 converts the first 16 to
// screen colours (FUN_0042f3d0: each field truncated), the rest is unused.
// Returns those 16 colours as RGB555.
std::array<uint16_t, 16> parse_font_palette(std::span<const uint8_t> data);

// colours.uib: u32 count, then (counted name, B, G, R, 0).
class ColourTable {
public:
    static ColourTable parse(std::span<const uint8_t> data);
    std::optional<Rgb> get(const std::string& name) const;
    bool is_transparent(const std::string& name) const { return name == "Transparent"; }
    size_t size() const { return entries_.size(); }

private:
    std::vector<std::pair<std::string, Rgb>> entries_;
};

} // namespace d2k::formats
