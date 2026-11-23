#pragma once
// Optional Unicode bitmap font for localised text.
//
// The original FONT.FNT / FONTCOL.FNT hold at most 256 single-byte glyphs.
// Localised strings are UTF-8, so non-ASCII characters are drawn from this
// sidecar font instead. ASCII keeps using the original font (pixel-identical).
//
// CKF file (little endian, produced by tools/make_cjk_font.py):
//   char[8]  magic "D2KCKF1\0"
//   u16      cell_w, cell_h        (alpha bitmap size, e.g. 16x16)
//   u16      baseline              (top offset added to the pen y)
//   u16      reserved
//   u32      count
//   count x { u32 codepoint, u16 advance, u8[cell_w*cell_h] alpha }

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2k::i18n {

struct CjkGlyph {
    uint32_t cp = 0;
    uint16_t advance = 0;
    std::vector<uint8_t> alpha; // cell_w * cell_h, 0 = transparent
};

class CjkFont {
public:
    static CjkFont load(const std::filesystem::path& file); // throws std::runtime_error
    const CjkGlyph* find(uint32_t cp) const;
    int cell_w() const { return cell_w_; }
    int cell_h() const { return cell_h_; }
    int baseline() const { return baseline_; }
    bool valid() const { return !glyphs_.empty(); }
    std::size_t size() const { return glyphs_.size(); }
    const std::string& error() const { return error_; }

private:
    int cell_w_ = 0, cell_h_ = 0, baseline_ = 0;
    std::vector<CjkGlyph> glyphs_;
    std::unordered_map<uint32_t, std::size_t> index_;
    std::string error_;
};

} // namespace d2k::i18n
