#include "ui/surface.hpp"

#include <algorithm>
#include <string>

#include "i18n/i18n.hpp"
#include "i18n/utf8.hpp"

namespace d2k::ui {

void Surface::clear(uint16_t c) { std::fill(pixels_.begin(), pixels_.end(), c); }

void Surface::fill(const Rect& r, uint16_t c) {
    int x0 = std::max(r.x, 0), y0 = std::max(r.y, 0);
    int x1 = std::min(r.x + r.w, width_), y1 = std::min(r.y + r.h, height_);
    for (int y = y0; y < y1; ++y) {
        std::fill(pixels_.begin() + y * width_ + x0, pixels_.begin() + y * width_ + x1, c);
    }
}

void Surface::frame(const Rect& r, uint16_t tl, uint16_t br) {
    fill({r.x, r.y, r.w, 1}, tl);
    fill({r.x, r.y, 1, r.h}, tl);
    fill({r.x, r.y + r.h - 1, r.w, 1}, br);
    fill({r.x + r.w - 1, r.y, 1, r.h}, br);
}

UiImage UiImage::from(const formats::Image16& img) {
    UiImage out;
    out.width = img.width;
    out.height = img.height;
    out.pixels = img.pixels;
    return out;
}

UiImage UiImage::from(const formats::Image8& img) {
    UiImage out;
    out.width = img.width;
    out.height = img.height;
    out.paletted = true;
    out.indices = img.indices;
    out.pixels.resize(img.indices.size());
    // FUN_0042f430 (16-bit mode): each BMP palette colour through
    // FUN_0042f3d0, which truncates every field (>> 3).
    for (size_t i = 0; i < img.indices.size(); ++i) out.pixels[i] = to_rgb555(img.palette[img.indices[i]]);
    return out;
}

void Surface::blit(const UiImage& img, const Rect& src, int dx, int dy, BlitMode mode) {
    // 16-bit keys are compared in the screen format; a 565 screen sees a
    // 555 TGA's magenta (0x7c1f) as 0xf81f and blue stays 0x001f. On a 555
    // screen the exe would never match 0xf81f; no 1.06 UI TGA contains
    // either colour, so both screens draw the same (docs/formats/fonts.md).
    constexpr uint16_t kMagenta = 0x7c1f, kBlue = 0x001f;
    constexpr uint16_t kHalfMask = 0x3def; // (dst & 0x7bde) >> 1 for 555
    for (int y = 0; y < src.h; ++y) {
        int ty = dy + y, sy = src.y + y;
        if (ty < 0 || ty >= height_ || sy < 0 || sy >= int(img.height)) continue;
        for (int x = 0; x < src.w; ++x) {
            int tx = dx + x, sx = src.x + x;
            if (tx < 0 || tx >= width_ || sx < 0 || sx >= int(img.width)) continue;
            size_t si = size_t(sy) * img.width + size_t(sx);
            uint16_t& d = pixels_[size_t(ty) * width_ + tx];
            uint16_t c = img.pixels[si];
            if (mode != BlitMode::Opaque) {
                bool transparent = img.paletted ? img.indices[si] == 0 : c == kMagenta;
                bool shadow = mode == BlitMode::KeyedShadow &&
                              (img.paletted ? img.indices[si] == 1 : c == kBlue);
                if (transparent) continue;
                if (shadow) {
                    d = uint16_t((d >> 1) & kHalfMask);
                    continue;
                }
            }
            d = c;
        }
    }
}

namespace {
// One resolved character: either the original 8-bpp FNT glyph or an alpha
// glyph from the optional Unicode sidecar font.
struct PenGlyph {
    const uint8_t* pixels = nullptr; // null = space / missing glyph
    int w = 0, h = 0;
    int advance = 0;
    int y_offset = 0;
    bool cjk = false; // alpha 0..255 instead of a font palette index
};

PenGlyph resolve(const formats::Font& font, const i18n::CjkFont* cjk, const i18n::utf8::Decoded& d) {
    PenGlyph g;
    if (!d.multibyte) {
        // cp1252 byte (or ASCII): exactly the original FUN_0042dc00 lookup.
        if (const auto* fg = font.glyph(static_cast<unsigned char>(d.cp))) {
            g.pixels = fg->pixels.data();
            g.w = int(fg->width);
            g.h = int(fg->height);
            g.advance = g.w + int(font.spacing());
        } else {
            g.advance = int(font.space_width());
        }
        return g;
    }
    if (cjk) {
        if (const auto* cg = cjk->find(d.cp)) {
            g.pixels = cg->alpha.data();
            g.w = cjk->cell_w();
            g.h = cjk->cell_h();
            g.advance = int(cg->advance);
            g.y_offset = cjk->baseline();
            g.cjk = true;
            return g;
        }
    }
    g.advance = int(font.space_width());
    return g;
}

// Walks the text by character. With English-only (no language pack) every
// decoded value is single-byte and this is identical to the old byte loop.
template <class F>
int for_each_glyph(const formats::Font& font, const std::string& text, int x, F&& draw) {
    const i18n::CjkFont* cjk = i18n::cjk_font();
    int pen = x;
    std::size_t p = 0;
    while (p < text.size()) {
        const auto d = i18n::utf8::decode(text, p);
        const PenGlyph g = resolve(font, cjk, d);
        if (g.pixels) draw(pen, g);
        pen += g.advance;
    }
    return pen - x;
}

uint16_t blend(uint16_t dst, uint16_t col, int a /* 0..32 */) {
    uint32_t out = 0;
    for (const uint32_t m : {0x7c00u, 0x03e0u, 0x001fu}) {
        out |= (((dst & m) * uint32_t(32 - a) + (col & m) * uint32_t(a)) >> 5) & m;
    }
    return uint16_t(out);
}

uint16_t blend255(uint16_t dst, uint16_t col, uint8_t a) {
    if (a == 0) return dst;
    if (a == 255) return col;
    uint32_t out = 0;
    for (const uint32_t m : {0x7c00u, 0x03e0u, 0x001fu}) {
        out |= (((dst & m) * uint32_t(255 - a) + (col & m) * uint32_t(a)) / 255) & m;
    }
    return uint16_t(out);
}
} // namespace

int text_width(const formats::Font& font, const std::string& text) {
    return for_each_glyph(font, text, 0, [](int, const PenGlyph&) {});
}

int Surface::draw_text_solid(const formats::Font& font, int x, int y, const std::string& text, uint16_t colour) {
    // FUN_0042dc00 with a colour (param 7 = -1) -> FUN_00431e40: every
    // nonzero glyph pixel in the colour; a glyph that does not fit is skipped.
    return for_each_glyph(font, text, x, [&](int pen, const PenGlyph& g) {
        if (!g.cjk) {
            if (pen >= 0 && y >= 0 && pen + g.w < width() && y + g.h <= height()) {
                for (int gy = 0; gy < g.h; ++gy) {
                    for (int gx = 0; gx < g.w; ++gx) {
                        if (g.pixels[size_t(gy * g.w + gx)] != 0)
                            pixels()[size_t((y + gy) * width() + pen + gx)] = colour;
                    }
                }
            }
            return;
        }
        for (int gy = 0; gy < g.h; ++gy) {
            for (int gx = 0; gx < g.w; ++gx) {
                const uint8_t a = g.pixels[size_t(gy * g.w + gx)];
                const int px = pen + gx, py = y + g.y_offset + gy;
                if (a == 0 || px < 0 || py < 0 || px >= width() || py >= height()) continue;
                uint16_t& d = pixels()[size_t(py) * width() + px];
                d = blend255(d, colour, a);
            }
        }
    });
}

int Surface::draw_text_alpha(const formats::Font& font, int x, int y, const std::string& text, uint16_t colour,
                             int alpha) {
    return for_each_glyph(font, text, x, [&](int pen, const PenGlyph& g) {
        if (!g.cjk) {
            for (int gy = 0; gy < g.h; ++gy) {
                for (int gx = 0; gx < g.w; ++gx) {
                    const int px = pen + gx, py = y + gy;
                    if (g.pixels[size_t(gy * g.w + gx)] == 0 || px < 0 || py < 0 || px >= width() ||
                        py >= height()) {
                        continue;
                    }
                    uint16_t& d = pixels()[size_t(py) * width() + px];
                    d = blend(d, colour, alpha);
                }
            }
            return;
        }
        for (int gy = 0; gy < g.h; ++gy) {
            for (int gx = 0; gx < g.w; ++gx) {
                const uint8_t a = g.pixels[size_t(gy * g.w + gx)];
                const int px = pen + gx, py = y + g.y_offset + gy;
                if (a == 0 || px < 0 || py < 0 || px >= width() || py >= height()) continue;
                uint16_t& d = pixels()[size_t(py) * width() + px];
                d = blend255(d, colour, uint8_t((int(a) * alpha) / 32));
            }
        }
    });
}

int Surface::draw_text_indexed(const formats::Font& font, const std::array<uint16_t, 16>& colours, int x, int y,
                               const std::string& text) {
    // FONTCOL: index 1 is the black edge, 2..15 the foreground colour. For
    // Unicode glyphs we only have an alpha mask, so use the first foreground
    // colour (2) and fall back to the edge colour when it is transparent.
    const uint16_t cjk_colour = colours[2] != 0 ? colours[2] : colours[1];
    return for_each_glyph(font, text, x, [&](int pen, const PenGlyph& g) {
        if (!g.cjk) {
            if (pen >= 0 && y >= 0 && pen + g.w < width() && y + g.h <= height()) {
                for (int gy = 0; gy < g.h; ++gy) {
                    for (int gx = 0; gx < g.w; ++gx) {
                        const uint8_t idx = g.pixels[size_t(gy * g.w + gx)];
                        if (idx == 0) continue;
                        // FONTCOL glyphs use indices 1..14 only (checked on every
                        // 1.06 glyph), inside the 16-entry handle.
                        if (idx < 16) pixels()[size_t((y + gy) * width() + pen + gx)] = colours[idx];
                    }
                }
            }
            return;
        }
        for (int gy = 0; gy < g.h; ++gy) {
            for (int gx = 0; gx < g.w; ++gx) {
                const uint8_t a = g.pixels[size_t(gy * g.w + gx)];
                const int px = pen + gx, py = y + g.y_offset + gy;
                if (a == 0 || px < 0 || py < 0 || px >= width() || py >= height()) continue;
                uint16_t& d = pixels()[size_t(py) * width() + px];
                d = blend255(d, cjk_colour, a);
            }
        }
    });
}

} // namespace d2k::ui
