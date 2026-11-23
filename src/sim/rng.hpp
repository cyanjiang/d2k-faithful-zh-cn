#pragma once
// The game's random number generator: the MSVC C runtime rand()
// (1.06 FUN_004b1810, state at 0x4ed870, seeded by srand FUN_004b1800).
// Single-player games seed it from the clock (FUN_004470a0).

#include <cstdint>

namespace d2k::sim {

class Rng {
public:
    explicit Rng(uint32_t seed = 1) : state_(seed) {}
    void seed(uint32_t s) { state_ = s; }
    uint32_t state() const { return state_; }

    // 0 .. 0x7fff
    uint32_t next() {
        state_ = state_ * 0x343fd + 0x269ec3;
        return (state_ >> 16) & 0x7fff;
    }

private:
    uint32_t state_;
};

} // namespace d2k::sim
