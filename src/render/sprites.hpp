#pragma once
// DATA.R16 sprites for units and buildings, drawn like FUN_00427d50 /
// FUN_00428e30: offset by the image's R8 header offsets, blit flags (1,1)
// (index 0 transparent, index 1 shadow), house colours in palette entries
// 240..255 from bin\COLOURS.BIN. RE: docs/re/08-templates.md

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "formats/r8.hpp"
#include "ui/surface.hpp"

namespace d2k::render {

// Blits of explosions and shadows (docs/re/14 section 5.2), 8-bit images.
enum class Blit {
    Keyed,       // index 0 transparent, 1 shadow (flags 1, 1)
    Shadow,      // FUN_00434620: idx != 0 -> (dst >> 1) & 0x3def
    Translucent, // 0x10, FUN_004340e0: idx != 0 -> (dst + src) / 2 per channel
    Additive,    // 0x200, FUN_004349a0: every pixel, min(dst + src, 31)
    Darken,      // 0x2: every pixel, max(channel - idx, 0)
    Brighten,    // 0x4: every pixel, min(channel + idx, 31)
};

class SpriteBank {
public:
    // `data_r16` = Data\DATA.R16, `colours` = Data\bin\COLOURS.BIN.
    SpriteBank(std::vector<formats::R8Image> entries, std::span<const uint8_t> colours);

    size_t size() const { return entries_.size(); }
    const formats::R8Image& entry(size_t i) const { return entries_[i]; }

    // Draws entry `index` with its origin at screen (x, y), i.e. at
    // (x - offset_x, y - offset_y), clipped to `clip`. `house_colour` picks
    // the 16 remap colours (0..7).
    void draw(ui::Surface& out, size_t index, int x, int y, int house_colour, const ui::Rect& clip) const;
    // Same, but with the image's top-left at (x, y) (UI pieces blitted
    // without their header offsets).
    void draw_at(ui::Surface& out, size_t index, int x, int y, const ui::Rect& clip) const;
    // The 555 colour of image pixel (sx, sy), index 0 included (its palette
    // colour). 0 outside the image.
    uint16_t colour(size_t index, int sx, int sy) const;
    // The raw 8-bit index (0 for 16-bit images or outside).
    uint8_t index_at(size_t index, int sx, int sy) const;
    // Draws entry `index` with its origin at (x, y) like draw(), with a blit.
    void draw_blit(ui::Surface& out, size_t index, int x, int y, int house_colour, Blit blit,
                   const ui::Rect& clip) const;
    // Stealth blits (docs/re/32 section 1.7), 8-bit sprites, origin as draw():
    // FUN_00432d80 / FUN_004353b0: where the sprite pixel is not 0, the
    // screen R, G, B each -= max(pattern - level, 0) (clamped at 0); the
    // pattern lines up with the sprite's pixels.
    void draw_shimmer(ui::Surface& out, size_t index, int x, int y, size_t pattern, int level, const ui::Rect& clip) const;
    // FUN_00434ea0: palette 0 = the screen pixel, 1 = it at half brightness;
    // every pixel becomes palette[sprite & mask].
    void draw_dissolve(ui::Surface& out, size_t index, int x, int y, int house_colour, size_t mask,
                       const ui::Rect& clip) const;
    // FUN_004326b0 flag 1: the target flash, a grey silhouette (docs/re/44).
    void draw_grey(ui::Surface& out, size_t index, int x, int y, int house_colour, const ui::Rect& clip) const;
    // COLOURS.BIN colour i (0..15) of house colour h (0..7).
    uint16_t house_colour(int h, int i) const { return house_colours_[size_t(h & 7)][size_t(i & 15)]; }

private:
    std::vector<formats::R8Image> entries_;
    std::vector<std::array<uint16_t, 256>> palettes_; // resolved: type 2 uses the last type-1 palette
    std::array<std::array<uint16_t, 16>, 8> house_colours_{};
};

} // namespace d2k::render
