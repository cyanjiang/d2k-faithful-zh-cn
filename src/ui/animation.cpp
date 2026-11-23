#include "ui/animation.hpp"

namespace d2k::ui {

AnimMode anim_mode_from_name(const std::string& name) {
    if (name == "AnimLoopForwardCycle") return AnimMode::LoopForward;
    if (name == "AnimLoopBackwardCycle") return AnimMode::LoopBackward;
    if (name == "AnimLoopToggleCycle") return AnimMode::PingPong;
    if (name == "AnimLoopForwardOnce") return AnimMode::ForwardOnce;
    if (name == "AnimLoopBackwardOnce") return AnimMode::BackwardOnce;
    // AnimLoopToggleOnce (6) and AnimStep* (7..11) exist (0x4ca820) but no
    // 1.06 script names them and the code starts only modes 1 and 4.
    return AnimMode::Stopped;
}

// FUN_0048d1f0 + FUN_0048d240: only a change of mode has an effect; forward
// modes rewind to frame 0, backward modes to the last frame. The frame
// timer is not reset.
void AnimationPlayer::set_mode(AnimMode m, uint64_t /*now_ms*/) {
    if (m == mode) return;
    mode = m;
    switch (m) {
    case AnimMode::LoopForward:
    case AnimMode::PingPong:
    case AnimMode::ForwardOnce:
        frame = 0;
        forward = true;
        break;
    case AnimMode::LoopBackward:
    case AnimMode::BackwardOnce:
        frame = frames - 1;
        forward = false;
        break;
    case AnimMode::Stopped:
        break;
    }
}

void AnimationPlayer::update(uint64_t now_ms) {
    if (now_ms - last_ms < period_ms) return;
    last_ms = now_ms;
    switch (mode) {
    case AnimMode::Stopped:
        return;
    case AnimMode::LoopForward:
        frame = frame + 1 >= frames ? 0 : frame + 1;
        return;
    case AnimMode::LoopBackward:
        frame = frame - 1 < 0 ? frames - 1 : frame - 1;
        return;
    case AnimMode::PingPong:
        if (forward) {
            if (++frame >= frames) { frame = frames - 1; forward = false; }
        } else {
            if (--frame < 0) { frame = 0; forward = true; }
        }
        return;
    case AnimMode::ForwardOnce:
        if (frame + 1 >= frames) {
            mode = AnimMode::Stopped;
            if (!hold_last) frame = 0;
        } else {
            ++frame;
        }
        return;
    case AnimMode::BackwardOnce:
        if (frame - 1 < 0) {
            mode = AnimMode::Stopped;
            if (!hold_last) frame = frames - 1;
        } else {
            --frame;
        }
        return;
    }
}

} // namespace d2k::ui
