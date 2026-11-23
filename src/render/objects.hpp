#pragma once
// Draws placed units and buildings: building foundations first
// (BlitBuildingFoundation, FUN_00427d50), then building bodies and units
// (FUN_00428e30 via FUN_0042aa90). RE: docs/re/08-templates.md

#include <functional>
#include <vector>

#include "formats/templates.hpp"
#include "render/sprites.hpp"
#include "sim/objects.hpp"
#include "ui/surface.hpp"

namespace d2k::render {

// `scroll_x`/`scroll_y` as for draw_terrain. The house colour of a player is
// its slot number (0x5175d8 starts as 0..7). The second pass is ordered
// like FUN_0042aa90's list: by y, equal keys in reverse add order.
// `local` and `tick` drive the stealth drawing of units (docs/re/32 section 1.7).
// `passes`: 1 = the foundations only (FUN_00427d50), 2 = the bodies and
// units only (FUN_00428e30's sorted list), 3 = both. The game screen draws
// the pixel particles and the cell objects between the two (doc 16 section 1).
// `after_item(i)` runs right after item objects[i] of the second pass is
// drawn: FUN_00428e30 draws a selected item's bracket there (item +0x21,
// after the item's images, before the next item), so later items cover it
// (docs/re/10 section 8).
void draw_objects(const std::vector<sim::Object>& objects, const formats::Templates& templates,
                  const SpriteBank& sprites, ui::Surface& screen, int scroll_x, int scroll_y, int local = 0,
                  uint32_t tick = 0, int passes = 3, const std::function<void(size_t)>& after_item = {});

} // namespace d2k::render
