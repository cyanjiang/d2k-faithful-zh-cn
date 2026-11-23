#include "render/sprites.hpp"

#include <algorithm>

namespace d2k::render {

SpriteBank::SpriteBank(std::vector<formats::R8Image> entries, std::span<const uint8_t> colours)
    : entries_(std::move(entries)), palettes_(entries_.size()) {
    std::array<uint16_t, 256> last{};
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].palette) last = *entries_[i].palette;
        palettes_[i] = last;
    }
    for (size_t h = 0; h < 8; ++h) {
        for (size_t c = 0; c < 16; ++c) {
            size_t o = (h * 16 + c) * 2;
            if (o + 1 < colours.size()) house_colours_[h][c] = uint16_t(colours[o] | colours[o + 1] << 8);
        }
    }
}

void SpriteBank::draw(ui::Surface& out, size_t index, int x, int y, int house_colour, const ui::Rect& clip) const {
    if (index >= entries_.size()) return;
    const auto& img = entries_[index];
    if (img.empty() || img.pixels.empty()) return;
    std::array<uint16_t, 256> pal = palettes_[index];
    if (house_colour >= 0 && house_colour < 8) {
        std::copy(house_colours_[size_t(house_colour)].begin(), house_colours_[size_t(house_colour)].end(),
                  pal.begin() + 240);
    }
    const int x0 = x - img.offset_x, y0 = y - img.offset_y;
    const int cx0 = std::max(clip.x, 0), cy0 = std::max(clip.y, 0);
    const int cx1 = std::min(clip.x + clip.w, out.width()), cy1 = std::min(clip.y + clip.h, out.height());
    auto& px = out.pixels();
    for (uint32_t sy = 0; sy < img.height; ++sy) {
        const int ty = y0 + int(sy);
        if (ty < cy0 || ty >= cy1) continue;
        for (uint32_t sx = 0; sx < img.width; ++sx) {
            const int tx = x0 + int(sx);
            if (tx < cx0 || tx >= cx1) continue;
            uint16_t& d = px[size_t(ty) * out.width() + tx];
            const size_t si = size_t(sy) * img.width + sx;
            if (img.bits_per_pixel == 8) {
                const uint8_t i = img.pixels[si];
                if (i == 0) continue;                            // transparent
                if (i == 1) {                                    // shadow
                    d = uint16_t((d >> 1) & 0x3def);
                    continue;
                }
                d = pal[i];
            } else {
                const uint16_t c = uint16_t(img.pixels[2 * si] | img.pixels[2 * si + 1] << 8);
                if (c == 0x7c1f) continue;                      // see ui::Surface::blit
                if (c == 0x001f) {
                    d = uint16_t((d >> 1) & 0x3def);
                    continue;
                }
                d = c;
            }
        }
    }
}

void SpriteBank::draw_shimmer(ui::Surface& out, size_t index, int x, int y, size_t pattern, int level,
                              const ui::Rect& clip) const {
    if (index >= entries_.size() || pattern >= entries_.size()) return;
    const auto& img = entries_[index];
    const auto& pat = entries_[pattern];
    if (img.empty() || img.pixels.empty() || img.bits_per_pixel != 8) return;
    const int x0 = x - img.offset_x, y0 = y - img.offset_y;
    const int cx0 = std::max(clip.x, 0), cy0 = std::max(clip.y, 0);
    const int cx1 = std::min(clip.x + clip.w, out.width()), cy1 = std::min(clip.y + clip.h, out.height());
    auto& px = out.pixels();
    for (uint32_t sy = 0; sy < img.height; ++sy) {
        const int ty = y0 + int(sy);
        if (ty < cy0 || ty >= cy1) continue;
        for (uint32_t sx = 0; sx < img.width; ++sx) {
            const int tx = x0 + int(sx);
            if (tx < cx0 || tx >= cx1) continue;
            if (img.pixels[size_t(sy) * img.width + sx] == 0) continue;
            int p = sx < pat.width && sy < pat.height && !pat.pixels.empty() ? pat.pixels[size_t(sy) * pat.width + sx] : 0;
            p = p > level ? p - level : 0;
            uint16_t& d = px[size_t(ty) * size_t(out.width()) + size_t(tx)];
            const int r = std::max(int((d >> 10) & 0x1f) - p, 0), g = std::max(int((d >> 5) & 0x1f) - p, 0),
                      b = std::max(int(d & 0x1f) - p, 0);
            d = uint16_t(r << 10 | g << 5 | b);
        }
    }
}

void SpriteBank::draw_dissolve(ui::Surface& out, size_t index, int x, int y, int house_colour, size_t mask,
                               const ui::Rect& clip) const {
    if (index >= entries_.size() || mask >= entries_.size()) return;
    const auto& img = entries_[index];
    const auto& m = entries_[mask];
    if (img.empty() || img.pixels.empty() || img.bits_per_pixel != 8) return;
    std::array<uint16_t, 256> pal = palettes_[index];
    if (house_colour >= 0 && house_colour < 8) {
        std::copy(house_colours_[size_t(house_colour)].begin(), house_colours_[size_t(house_colour)].end(),
                  pal.begin() + 240);
    }
    const int x0 = x - img.offset_x, y0 = y - img.offset_y;
    const int cx0 = std::max(clip.x, 0), cy0 = std::max(clip.y, 0);
    const int cx1 = std::min(clip.x + clip.w, out.width()), cy1 = std::min(clip.y + clip.h, out.height());
    auto& px = out.pixels();
    for (uint32_t sy = 0; sy < img.height; ++sy) {
        const int ty = y0 + int(sy);
        if (ty < cy0 || ty >= cy1) continue;
        for (uint32_t sx = 0; sx < img.width; ++sx) {
            const int tx = x0 + int(sx);
            if (tx < cx0 || tx >= cx1) continue;
            uint16_t& d = px[size_t(ty) * size_t(out.width()) + size_t(tx)];
            pal[0] = d;
            pal[1] = uint16_t((d & 0x7bde) >> 1);
            const uint8_t mk = sx < m.width && sy < m.height && !m.pixels.empty() ? m.pixels[size_t(sy) * m.width + sx] : 0;
            d = pal[img.pixels[size_t(sy) * img.width + sx] & mk];
        }
    }
}

void SpriteBank::draw_grey(ui::Surface& out, size_t index, int x, int y, int house_colour, const ui::Rect& clip) const {
    // FUN_004326b0 with flag 1 (docs/re/44 section 3.2): index 0 transparent;
    // index 1 is the half-bright screen pixel; every colour becomes the grey
    // v = (r + g + b) / 6 + 3 of its 5-bit fields.
    if (index >= entries_.size()) return;
    const auto& img = entries_[index];
    if (img.empty() || img.pixels.empty() || img.bits_per_pixel != 8) return;
    std::array<uint16_t, 256> pal = palettes_[index];
    if (house_colour >= 0 && house_colour < 8) {
        std::copy(house_colours_[size_t(house_colour)].begin(), house_colours_[size_t(house_colour)].end(),
                  pal.begin() + 240);
    }
    const int x0 = x - img.offset_x, y0 = y - img.offset_y;
    const int cx0 = std::max(clip.x, 0), cy0 = std::max(clip.y, 0);
    const int cx1 = std::min(clip.x + clip.w, out.width()), cy1 = std::min(clip.y + clip.h, out.height());
    auto& px = out.pixels();
    for (uint32_t sy = 0; sy < img.height; ++sy) {
        const int ty = y0 + int(sy);
        if (ty < cy0 || ty >= cy1) continue;
        for (uint32_t sx = 0; sx < img.width; ++sx) {
            const int tx = x0 + int(sx);
            if (tx < cx0 || tx >= cx1) continue;
            const uint8_t i = img.pixels[size_t(sy) * img.width + sx];
            if (i == 0) continue;
            uint16_t& d = px[size_t(ty) * size_t(out.width()) + size_t(tx)];
            const uint16_t c = i == 1 ? uint16_t((d & 0x7bde) >> 1) : pal[i];
            const uint32_t v = (((c >> 10) & 0x1f) + ((c >> 5) & 0x1f) + (c & 0x1f)) / 6 + 3;
            d = uint16_t(v << 10 | v << 5 | v);
        }
    }
}

void SpriteBank::draw_at(ui::Surface& out, size_t index, int x, int y, const ui::Rect& clip) const {
    if (index >= entries_.size()) return;
    const auto& img = entries_[index];
    draw(out, index, x + img.offset_x, y + img.offset_y, -1, clip);
}

void SpriteBank::draw_blit(ui::Surface& out, size_t index, int x, int y, int house_colour, Blit blit,
                           const ui::Rect& clip) const {
    if (blit == Blit::Keyed) {
        draw(out, index, x, y, house_colour, clip);
        return;
    }
    if (index >= entries_.size()) return;
    const auto& img = entries_[index];
    if (img.empty() || img.pixels.empty() || img.bits_per_pixel != 8) return; // TODO: 16-bit images
    std::array<uint16_t, 256> pal = palettes_[index];
    if (house_colour >= 0 && house_colour < 8) {
        std::copy(house_colours_[size_t(house_colour)].begin(), house_colours_[size_t(house_colour)].end(),
                  pal.begin() + 240);
    }
    const int x0 = x - img.offset_x, y0 = y - img.offset_y;
    const int cx0 = std::max(clip.x, 0), cy0 = std::max(clip.y, 0);
    const int cx1 = std::min(clip.x + clip.w, out.width()), cy1 = std::min(clip.y + clip.h, out.height());
    auto& px = out.pixels();
    auto ch = [](uint16_t c, int s) { return int((c >> s) & 0x1f); };
    for (uint32_t sy = 0; sy < img.height; ++sy) {
        const int ty = y0 + int(sy);
        if (ty < cy0 || ty >= cy1) continue;
        for (uint32_t sx = 0; sx < img.width; ++sx) {
            const int tx = x0 + int(sx);
            if (tx < cx0 || tx >= cx1) continue;
            uint16_t& d = px[size_t(ty) * out.width() + tx];
            const uint8_t i = img.pixels[size_t(sy) * img.width + sx];
            const uint16_t s = pal[i];
            switch (blit) {
            case Blit::Shadow:
                if (i != 0) d = uint16_t((d >> 1) & 0x3def);
                break;
            case Blit::Translucent:
                if (i != 0) {
                    d = uint16_t(((ch(d, 10) + ch(s, 10)) / 2) << 10 | ((ch(d, 5) + ch(s, 5)) / 2) << 5 |
                                 ((ch(d, 0) + ch(s, 0)) / 2));
                }
                break;
            case Blit::Additive:
                d = uint16_t(std::min(ch(d, 10) + ch(s, 10), 31) << 10 | std::min(ch(d, 5) + ch(s, 5), 31) << 5 |
                             std::min(ch(d, 0) + ch(s, 0), 31));
                break;
            case Blit::Darken: { // idx read as a signed char
                const int v = int(int8_t(i));
                d = uint16_t(std::clamp(ch(d, 10) - v, 0, 31) << 10 | std::clamp(ch(d, 5) - v, 0, 31) << 5 |
                             std::clamp(ch(d, 0) - v, 0, 31));
                break;
            }
            case Blit::Brighten:
                d = uint16_t(std::min(ch(d, 10) + i, 31) << 10 | std::min(ch(d, 5) + i, 31) << 5 |
                             std::min(ch(d, 0) + i, 31));
                break;
            default:
                break;
            }
        }
    }
}

uint16_t SpriteBank::colour(size_t index, int sx, int sy) const {
    if (index >= entries_.size()) return 0;
    const auto& img = entries_[index];
    if (sx < 0 || sy < 0 || sx >= int(img.width) || sy >= int(img.height) || img.pixels.empty()) return 0;
    const size_t si = size_t(sy) * img.width + size_t(sx);
    if (img.bits_per_pixel == 8) return palettes_[index][img.pixels[si]];
    return uint16_t(img.pixels[2 * si] | img.pixels[2 * si + 1] << 8);
}

uint8_t SpriteBank::index_at(size_t index, int sx, int sy) const {
    if (index >= entries_.size()) return 0;
    const auto& img = entries_[index];
    if (img.bits_per_pixel != 8 || sx < 0 || sy < 0 || sx >= int(img.width) || sy >= int(img.height) ||
        img.pixels.empty()) {
        return 0;
    }
    return img.pixels[size_t(sy) * img.width + size_t(sx)];
}

} // namespace d2k::render
