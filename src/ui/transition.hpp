#pragma once
// Menu transitions (FADE / TWEEN / SKIP), after CUIManager 1.06
// FUN_00472730 (start) and FUN_00472870 (step). docs/re/04-menu-flow.md

#include <cstdint>
#include <optional>

#include "ui/surface.hpp"

namespace d2k::ui {

class Transition {
public:
    // Values of the manager's mode field (+0x120) and of menus.uib.
    enum Mode : int { FadeIn = 0, FadeOut = 1, Tween = 2, Skip = 3 };

    // Called when a menu opens (mode = entering value, full-screen rect,
    // opening = true) or closes (leaving value, its rect, opening = false).
    // Marks the transition pending, as the exe sets +0x11d.
    void select(int mode, const Rect& rect, bool opening);

    // FUN_00472730: snapshots the current screen and the newly drawn menus.
    // Does nothing for SKIP.
    void start(const Surface& screen, Surface new_screen);

    // FUN_00472870: advances one step, drawing into `screen`. Returns true
    // while the transition owns the screen (the UI update stops there).
    bool step(Surface& screen, uint64_t now_ms);

    bool pending() const { return active_; }
    // Whether the selected mode draws a transition (not SKIP).
    bool animates() const { return mode_ == Tween || mode_ == FadeIn || mode_ == FadeOut; }
    // True once start() has taken over the screen (not for SKIP).
    bool owns_screen() const {
        return active_ && old_ && new_ && (mode_ == Tween || mode_ == FadeIn || mode_ == FadeOut);
    }

private:
    void finish(Surface& screen);

    int mode_ = Skip;
    Rect rect_{0, 0, 640, 400};
    bool active_ = false;     // +0x11d
    bool opening_ = false;    // +0x136
    bool faded_out_ = false;  // +0x135
    bool faded_in_ = false;   // +0x134
    uint8_t step_ = 0;        // +0x11c
    uint32_t period_ms_ = 12; // +0x138, set by the constructor
    uint64_t last_ms_ = 0;    // +0x13c
    std::optional<Surface> old_, new_;
};

// The per-step pixel operations, exposed for tests. All work on RGB555
// inside `r` (x/y/w/h) of equally sized surfaces.
void tween_blend(const Surface& old_img, const Surface& new_img, Surface& out, const Rect& r,
                 uint8_t alpha);
void fade_out_step(Surface& screen, const Rect& r, uint8_t step);
void fade_in_step(const Surface& new_img, Surface& screen, const Rect& r, uint8_t step);

} // namespace d2k::ui
