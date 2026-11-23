#include "sim/tick_scheduler.hpp"

#include <algorithm>

namespace d2k::sim {

namespace {
// MSVC __ftol: truncation toward zero.
int ftol(float v) { return int(v); }
} // namespace

TickScheduler::TickScheduler(int speed) : speed_(speed) { build(); }

void TickScheduler::set_stalled(bool stalled) {
    if (stalled == stalled_) return;
    stalled_ = stalled;
    build();
}

void TickScheduler::set_cap(int cap) {
    if (cap <= 0 || cap == cap_) return;
    cap_ = cap;
    build();
}

void TickScheduler::build() {
    const int kAdjust = stalled_ ? 200 : 0; // 0x6b9888 (network stall)
    pattern_.fill(0);
    tick_limit_ = 0;
    if (cap_ == 0) return;
    float x;
    if (speed_ <= 7) x = 8.0f;
    else if (speed_ <= 0x17) x = 10.0f;
    else if (speed_ <= 0x27) x = 12.0f;
    else if (speed_ <= 0x3b) x = 15.0f;
    else if (speed_ <= 0x4e) x = 20.0f;
    else if (speed_ <= 0x5e) x = 25.0f;
    else x = float(cap_);
    // x = min(x, cap) - adjust
    if (!(x <= float(cap_))) x = float(cap_);
    x = x - float(kAdjust);
    tick_limit_ = std::max(ftol(x), 0);
    // step = base / x; no ticks when x <= 0 (single player)
    if (!(x > 0.0f)) return;
    float step = float(base_) / x;
    if (step < 1.0f) step = 1.0f; // 0x45b739: fcom 1.0f (0x4c9444), below -> 1.0
    // (The x <= 0 "force" branch at 0x45b728 tests 0x5179cc, which no
    // instruction writes: never taken.)
    float acc = 0.0f;
    int i = 0;
    do {
        pattern_[size_t(i)] = 1;
        acc = acc + step;
        i = ftol(acc) & 0xffff;
    } while (i < 100);
    slot_ = 0;
}

bool TickScheduler::tick_due(uint64_t now_ms) {
    if (!started_) {
        started_ = true;
        second_start_ms_ = now_ms;
    }
    ++frames_this_second_; // "Blitting Game View" comes before the message pump
    timer(now_ms);
    uint8_t slot;
    if (slot_ >= 100) {
        // Single player wraps at once; a network game only once the
        // 1-second timer allowed it (FUN_0045b790).
        if (network_ && !wrap_ok_) return false;
        wrap_ok_ = false;
        slot = pattern_[0];
        slot_ = 1;
    } else {
        slot = pattern_[size_t(slot_++)];
    }
    if (!slot || ticks_this_second_ >= tick_limit_) return false;
    ++ticks_this_second_;
    return true;
}

void TickScheduler::paused(uint64_t now_ms) {
    if (!started_) {
        started_ = true;
        second_start_ms_ = now_ms;
    }
    frames_this_second_ = 0; // 0x6b9860 = 0 every pass while a menu is open
    timer(now_ms);
}

void TickScheduler::timer(uint64_t now_ms) {
    // WM_TIMER, every 1000 ms (exe 0x4a3796..0x4a382b, single player).
    while (now_ms - second_start_ms_ >= 1000) {
        second_start_ms_ += 1000;
        wrap_ok_ = true; // 0x6b986c
        if (seconds_ >= 8) {
            // Single player: FUN_0045c480, the cap follows the base every
            // 9th second; a network game reports instead (the host's
            // latency record sets the cap, set_cap).
            if (!network_) {
                cap_ = base_;
                build();
            }
            seconds_ = 0;
        } else {
            ++seconds_;
        }
        ticks_this_second_ = 0;
        if (frames_this_second_ != 0 && frames_this_second_ != base_) { // FUN_0045b570
            base_ = frames_this_second_;
            build();
        }
        frames_this_second_ = 0;
    }
}

} // namespace d2k::sim
