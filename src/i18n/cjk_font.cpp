#include "i18n/cjk_font.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace d2k::i18n {
namespace {
uint16_t rd16(const std::vector<uint8_t>& b, std::size_t& p) {
    if (p + 2 > b.size()) throw std::runtime_error("CKF truncated (u16)");
    const uint16_t v = uint16_t(b[p] | (uint16_t(b[p + 1]) << 8));
    p += 2;
    return v;
}
uint32_t rd32(const std::vector<uint8_t>& b, std::size_t& p) {
    if (p + 4 > b.size()) throw std::runtime_error("CKF truncated (u32)");
    const uint32_t v = uint32_t(b[p]) | (uint32_t(b[p + 1]) << 8) | (uint32_t(b[p + 2]) << 16) |
                       (uint32_t(b[p + 3]) << 24);
    p += 4;
    return v;
}
} // namespace

CjkFont CjkFont::load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        CjkFont f;
        f.error_ = "cannot open " + file.string();
        return f;
    }
    const std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    constexpr char kMagic[8] = {'D', '2', 'K', 'C', 'K', 'F', '1', '\0'};
    if (b.size() < 20 || std::memcmp(b.data(), kMagic, 8) != 0)
        throw std::runtime_error("CKF: bad magic in " + file.string());

    std::size_t p = 8;
    CjkFont f;
    f.cell_w_ = rd16(b, p);
    f.cell_h_ = rd16(b, p);
    f.baseline_ = rd16(b, p);
    (void)rd16(b, p); // reserved
    const uint32_t count = rd32(b, p);
    const std::size_t cell = std::size_t(f.cell_w_) * f.cell_h_;
    if (f.cell_w_ == 0 || f.cell_h_ == 0 || f.cell_w_ > 64 || f.cell_h_ > 64 || count > 100000)
        throw std::runtime_error("CKF: implausible dimensions/count");
    if (p + std::size_t(count) * (6 + cell) != b.size())
        throw std::runtime_error("CKF: size mismatch in " + file.string());

    f.glyphs_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        CjkGlyph g;
        g.cp = rd32(b, p);
        g.advance = rd16(b, p);
        g.alpha.assign(b.begin() + std::ptrdiff_t(p), b.begin() + std::ptrdiff_t(p + cell));
        p += cell;
        f.index_[g.cp] = f.glyphs_.size();
        f.glyphs_.push_back(std::move(g));
    }
    return f;
}

const CjkGlyph* CjkFont::find(uint32_t cp) const {
    const auto it = index_.find(cp);
    return it == index_.end() ? nullptr : &glyphs_[it->second];
}

} // namespace d2k::i18n
