#pragma once
// UI ANIMATION playback, after CAnimation (1.06 FUN_0048cec0 / FUN_0048d2b0).
// RE notes: docs/re/03-ui-scripts.md#animations

#include <cstdint>
#include <string>

namespace d2k::ui {

// Values match the exe's mode field (+0x77c) and the order of the
// "Anim..." state names (AnimStopped = 0, AnimLoopForwardCycle = 1, ...).
enum class AnimMode : int {
    Stopped = 0,
    LoopForward = 1,
    LoopBackward = 2,
    PingPong = 3,
    ForwardOnce = 4,
    BackwardOnce = 5,
};

// Parses a state name such as "AnimLoopForwardCycle"; unknown -> Stopped.
AnimMode anim_mode_from_name(const std::string& name);

struct AnimationPlayer {
    int frames = 1;
    int frame = 0;
    AnimMode mode = AnimMode::Stopped;
    uint32_t period_ms = 100; // 1000 / fps (integer division, as the exe)
    uint64_t last_ms = 0;     // time of the last step
    bool forward = true;      // ping-pong direction
    bool hold_last = true;    // parser always passes 1: "once" modes keep the end frame

    void set_mode(AnimMode m, uint64_t now_ms);
    // Steps at most one frame when a full period has elapsed (the exe
    // advances one frame per update and restarts the period).
    void update(uint64_t now_ms);
};

} // namespace d2k::ui
