// In-game menu details closed against the 1.06 exe (docs/re/20, 27).
#include <doctest/doctest.h>

#include "ui/render.hpp"

using namespace d2k;

TEST_CASE("value bar drag: FUN_0048c9c0's float formula and the -0.01f quirk") {
    // hzhole is 92 px wide: thumb 18, track 74 (docs/re/20 section 3).
    // Volume 0..100 step 5: exactly half way is 50 in exact maths, but the
    // float -0.01 gives 49.999..., truncated to 49, rounded down to 45.
    CHECK(ui::valuebar_drag_value(37, 74, 0, 100, 5) == 45);
    // Scroll 4..40 step 2: 4 + 18 = 22 exactly -> 21 -> 20.
    CHECK(ui::valuebar_drag_value(37, 74, 4, 40, 2) == 20);
    // Off the whole numbers it is the plain truncation: 100 * 38 / 74 =
    // 51.35 -> 51 -> 50.
    CHECK(ui::valuebar_drag_value(38, 74, 0, 100, 5) == 50);
    // Game speed 15..100 step 5 never lands on a whole number (85 / 74).
    CHECK(ui::valuebar_drag_value(1, 74, 15, 100, 5) == 15);   // 16.15
    CHECK(ui::valuebar_drag_value(73, 74, 15, 100, 5) == 95);  // 98.85
    CHECK(ui::valuebar_drag_value(44, 74, 15, 100, 5) == 65);  // 65.54
}

TEST_CASE("value bar thumb: at the mouse while dragged, else at the value") {
    ui::ElementState s;
    s.vmin = 0;
    s.vmax = 100;
    s.step = 5;
    s.value = 50;
    const ui::Rect bar{290, 181, 92, 20};
    CHECK(ui::valuebar_thumb(bar, s).x == 290 + (74 * 50) / 100);
    s.drag_thumb_x = 300;
    CHECK(ui::valuebar_thumb(bar, s).x == 300);
    CHECK(ui::valuebar_thumb(bar, s).w == 18);
}
