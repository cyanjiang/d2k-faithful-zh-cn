#pragma once
// Draws the map into the playfield like 1.06 FUN_00423d20: 32x32 tiles
// from the mission's tileset, offset by the scroll position.
// RE: docs/re/06-terrain.md, docs/re/07-ingame-screen.md

#include <vector>

#include "formats/r8.hpp"
#include "sim/game_map.hpp"
#include "ui/surface.hpp"

namespace d2k::render {

// The playfield (screen 640x400, top bar 20, sidebar 160).
constexpr int kPlayfieldX = 0;
constexpr int kPlayfieldY = 20;      // 0x4e41dc
constexpr int kPlayfieldWidth = 480; // 0x786d48
constexpr int kPlayfieldHeight = 380; // 0x6dc538
constexpr int kCellSize = 32;

// `scroll_x`/`scroll_y` is the map pixel at the playfield's top-left corner.
// Cells outside the map are left black. TODO: craters / damage overlay
// (cell marks), shroud (state nibble 1).
void draw_terrain(const sim::GameMap& map, const std::vector<formats::R8Image>& tiles,
                  ui::Surface& screen, int scroll_x, int scroll_y);

} // namespace d2k::render
