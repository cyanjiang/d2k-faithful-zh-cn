#pragma once
// Software 16-bit (RGB555) drawing surface, like the original's back buffer.

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "formats/bmp.hpp"
#include "formats/font.hpp"
#include "formats/palette.hpp"
#include "formats/tga.hpp"

namespace d2k::ui {

constexpr uint16_t to_rgb555(formats::Rgb c) {
    return uint16_t(((c.r >> 3) << 10) | ((c.g >> 3) << 5) | (c.b >> 3));
}

// A UI_GFX image as the exe holds it: 16-bit from a TGA, or 8-bit
// palettised from a BMP (FUN_00491330 tries <name>.tga, then <name>.bmp).
struct UiImage {
    uint32_t width = 0;
    uint32_t height = 0;
    bool paletted = false;
    std::vector<uint16_t> pixels;  // RGB555 (8-bit: the palette colour)
    std::vector<uint8_t> indices;  // 8-bit only

    static UiImage from(const formats::Image16& img);
    static UiImage from(const formats::Image8& img);
};

// Flags of the exe's blitter (FUN_00430770 / FUN_0042fac0).
enum class BlitMode {
    Opaque,      // (0, 0)
    Keyed,       // (1, 0): 8-bit index 0 / 16-bit magenta skipped
    KeyedShadow, // (1, 1): as Keyed, plus 8-bit index 1 / 16-bit blue = shadow
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

class Surface {
public:
    Surface(int width, int height) : width_(width), height_(height), pixels_(size_t(width) * height, 0) {}

    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<uint16_t>& pixels() const { return pixels_; }
    std::vector<uint16_t>& pixels() { return pixels_; }

    void clear(uint16_t c = 0);
    void fill(const Rect& r, uint16_t c);
    void frame(const Rect& r, uint16_t top_left, uint16_t bottom_right);

    // Blits the src rect of an image to (dx, dy), clipped to the surface.
    void blit(const UiImage& img, const Rect& src, int dx, int dy, BlitMode mode);

    // Draws text with a palettised font. Returns the advance width.
    // BlitFontChar (FUN_00431f50) through a 16-entry colour handle
    // (FUN_0042e1c0): glyph index 0 keeps the background, the others are
    // written with colours[index]. A glyph that does not fit (x + w >= width
    // or y + h > height) or starts off the surface is skipped whole.
    // FUN_0042dc00 / FUN_00431e40: solid-colour text (every nonzero glyph
    // pixel); glyphs that do not fit are skipped.
    int draw_text_solid(const formats::Font& font, int x, int y, const std::string& text, uint16_t colour);
    int draw_text_indexed(const formats::Font& font, const std::array<uint16_t, 16>& colours, int x, int y,
                          const std::string& text);
    // FUN_0042e110 -> FUN_00432060: translucent solid-colour text, alpha
    // 0..32. Per 5-bit field: ((dst * (32 - a) + col * a) >> 5). The exe
    // does not clip; pixels outside the surface are dropped here.
    int draw_text_alpha(const formats::Font& font, int x, int y, const std::string& text, uint16_t colour,
                        int alpha);

private:
    int width_, height_;
    std::vector<uint16_t> pixels_;
};

int text_width(const formats::Font& font, const std::string& text);

} // namespace d2k::ui
