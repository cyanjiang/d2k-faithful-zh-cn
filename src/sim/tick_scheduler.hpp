#pragma once
// Simulation tick pacing, single player. FUN_0045b610 builds a 100-slot
// pattern and FUN_0045b790 walks it once per main-loop pass. A 1-second
// WM_TIMER (exe 0x4a3796..) resets the tick count, makes the frames drawn in
// the last second the base rate (FUN_0045b570) and, every 9th second, the cap
// (FUN_0045c480). So the speed buckets are ticks per real second, whatever
// the loop rate. RE: docs/re/02-timing-and-arithmetic.md

#include <array>
#include <cstdint>

namespace d2k::sim {

class TickScheduler {
public:
    // `speed` = the options slider 0..100 (default 75, 0x4e3b18).
    explicit TickScheduler(int speed = 75);

    // Called once per main-loop pass (one drawn frame) with the current
    // time; true when a simulation tick should run now.
    bool tick_due(uint64_t now_ms);
    // A loop pass with a menu open (single player, docs/re/20 section 1.3):
    // the frame count is held at 0 and the pattern is not walked, but the
    // 1-second timer still runs.
    void paused(uint64_t now_ms);
    // The options slider: stored now, used by the next rebuild (the next
    // 1-second timer that sees a changed frame count; docs/re/20 section 3).
    void set_speed(int speed) { speed_ = speed; }

    // Network games (docs/re/57 section 1.8): the pattern does not wrap
    // after its 100th slot until the next 1-second timer (0x6b986c); the
    // 9th-second cap update is the host's latency record instead
    // (set_cap); a stall rebuilds the pattern with x - 200 (0x6b9888), so
    // no tick at all, and the end of the stall rebuilds it again.
    void set_network(bool on) { network_ = on; }
    void set_stalled(bool stalled);
    void set_cap(int cap);

    // Ticks per second at the current base and cap (0x6b9894).
    int tick_limit() const { return tick_limit_; }
    const std::array<uint8_t, 100>& pattern() const { return pattern_; }
    int base() const { return base_; }
    int cap() const { return cap_; }

private:
    void build(); // FUN_0045b610
    void timer(uint64_t now_ms);

    int speed_ = 75;                     // 0x4e3b18
    int base_ = 100;                     // 0x4e3b14: frames drawn in the last second
    int cap_ = 60;                       // 0x6b9864 (0x3c from FUN_0045a7a0)
    std::array<uint8_t, 100> pattern_{}; // 0x6b95e0
    int tick_limit_ = 0;                 // 0x6b9894
    int slot_ = 0;                       // 0x6b9868
    int ticks_this_second_ = 0;          // 0x6b985c
    int frames_this_second_ = 0;         // 0x6b9860
    int seconds_ = 0;                    // 0x6b987c
    uint64_t second_start_ms_ = 0;       // the 1000 ms WM_TIMER
    bool started_ = false;
    bool network_ = false;
    bool stalled_ = false;               // adjust 0x6b9888 = 200
    bool wrap_ok_ = false;               // 0x6b986c: set by the 1-second timer
};

} // namespace d2k::sim
