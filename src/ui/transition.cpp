#include "ui/transition.hpp"

namespace d2k::ui {

namespace {
constexpr uint16_t kR = 0x7c00, kG = 0x03e0, kB = 0x001f;
} // namespace

// FUN_00472b50 (16-bit, 555 branch): per-field weighted sum, >> 5.
void tween_blend(const Surface& old_img, const Surface& new_img, Surface& out, const Rect& r,
                 uint8_t alpha) {
    const uint32_t a = alpha, ia = uint8_t(32 - alpha);
    const auto& o = old_img.pixels();
    const auto& n = new_img.pixels();
    auto& d = out.pixels();
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            size_t i = size_t(y) * out.width() + x;
            uint32_t nv = n[i], ov = o[i];
            uint16_t red = uint16_t(((nv & kR) * a + (ov & kR) * ia) >> 5) & kR;
            uint16_t grn = uint16_t(((nv & kG) * a + (ov & kG) * ia) >> 5) & kG;
            uint16_t blu = uint16_t(((ov & kB) * ia + (nv & kB) * a) >> 5) & kB;
            d[i] = uint16_t(blu | grn | red);
        }
    }
}

// FUN_00472db0: halves every field of a checkerboard half of the pixels,
// in place. The parity flag carries across rows exactly as in the exe.
void fade_out_step(Surface& screen, const Rect& r, uint8_t step) {
    auto& d = screen.pixels();
    bool flag = (step & 1) == 0;
    for (int y = r.y; y < r.y + r.h; ++y) {
        flag = !flag;
        for (int x = r.x; x < r.x + r.w; ++x) {
            flag = !flag;
            if (flag) continue;
            size_t i = size_t(y) * screen.width() + x;
            uint16_t p = d[i];
            d[i] = uint16_t(((p >> 1) & 0xf) | ((p >> 1) & 0x1e0) | ((p >> 1) & 0x3c00));
        }
    }
}

// FUN_00472f20: writes the new image shifted right by (10 - step) / 2 to a
// checkerboard half of the pixels.
void fade_in_step(const Surface& new_img, Surface& screen, const Rect& r, uint8_t step) {
    const auto& n = new_img.pixels();
    auto& d = screen.pixels();
    const int shift = (10 - int(step)) / 2;
    bool flag = (step & 1) == 0;
    for (int y = r.y; y < r.y + r.h; ++y) {
        flag = !flag;
        for (int x = r.x; x < r.x + r.w; ++x) {
            flag = !flag;
            if (flag) continue;
            size_t i = size_t(y) * screen.width() + x;
            uint16_t p = n[i];
            d[i] = uint16_t((((p & kB) >> shift) & kB) | (((p & kG) >> shift) & kG) |
                            (((p & kR) >> shift) & kR));
        }
    }
}

void Transition::select(int mode, const Rect& rect, bool opening) {
    mode_ = mode;
    rect_ = rect;
    step_ = 0;
    active_ = true;
    opening_ = opening;
}

void Transition::start(const Surface& screen, Surface new_screen) {
    if (mode_ == Skip) return;
    active_ = true;
    old_ = screen;
    new_ = std::move(new_screen);
    step_ = 0;
    faded_in_ = false;
    faded_out_ = false;
}

void Transition::finish(Surface& screen) {
    step_ = 0;
    active_ = false;
    if (new_) screen.pixels() = new_->pixels();
}

bool Transition::step(Surface& screen, uint64_t now_ms) {
    if (!active_) return false;
    if (!old_ || !new_ || (mode_ != Tween && mode_ != FadeIn && mode_ != FadeOut)) {
        step_ = 0;
        active_ = false;
        return false;
    }
    if (mode_ == Tween) {
        if (step_ > 0x1f) {
            finish(screen);
            return false;
        }
        tween_blend(*old_, *new_, screen, rect_, step_);
        step_ = uint8_t(step_ + 4);
        if (step_ > 0x20) step_ = 0x20;
        return true;
    }

    // FADE: one step per period (GetTickCount, absolute difference).
    uint64_t dt = now_ms >= last_ms_ ? now_ms - last_ms_ : last_ms_ - now_ms;
    if (dt < period_ms_) return true;
    last_ms_ = now_ms;
    if (!faded_out_ && !faded_in_ && opening_) mode_ = mode_ == FadeIn ? FadeOut : FadeIn;
    if (step_ > 10) {
        finish(screen);
        return false;
    }
    if (mode_ == FadeOut) {
        faded_out_ = true;
        fade_out_step(screen, rect_, step_);
        if (step_ > 9) step_ = 0;
    } else {
        faded_in_ = true;
        fade_in_step(*new_, screen, rect_, step_);
    }
    if (++step_ > 10) step_ = 10;
    if (step_ > 9) {
        step_ = 0;
        if (!faded_out_) {
            mode_ = FadeOut;
            return true;
        }
        if (!faded_in_) {
            mode_ = FadeIn;
            return true;
        }
        finish(screen);
        return false;
    }
    return true;
}

} // namespace d2k::ui
