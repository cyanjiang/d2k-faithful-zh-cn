// FUN_0046f080's sample cache (docs/re/59 section 1.1).
#include <doctest/doctest.h>

#include "audio/channels.hpp"

using d2k::audio::kChannels;
using d2k::audio::SampleCache;

namespace {
std::array<bool, kChannels> idle() { return {}; }

SampleCache full_cache(uint32_t stamp_base) {
    SampleCache c;
    for (int i = 0; i < 16; ++i) {
        c.now = stamp_base + uint32_t(i);
        REQUIRE(c.load(100 + i, true, idle()) == i);
    }
    return c;
}
} // namespace

TEST_CASE("sample cache: free slots, hits keep their stamp, a missing file fails") {
    SampleCache c;
    c.now = 5;
    CHECK(c.load(7, true, idle()) == 0);
    CHECK(c.load(9, false, idle()) == -1); // not in SOUND.RS: the slot stays free
    CHECK(c.slot[1].id == -1);
    c.now = 50;
    CHECK(c.load(7, true, idle()) == 0); // hit
    CHECK(c.slot[0].stamp == 5);         // not refreshed
}

TEST_CASE("sample cache: a full cache reloads the first oldest slot below now") {
    auto c = full_cache(10); // stamps 10..25
    c.now = 30;
    CHECK(c.load(200, true, idle()) == 0);
    CHECK(c.slot[0].id == 200);
    CHECK(c.slot[0].stamp == 30);
    CHECK(c.load(201, true, idle()) == 1);
}

TEST_CASE("sample cache: playing channels protect their slots") {
    auto c = full_cache(10);
    c.channel_slot[3] = 0;
    std::array<bool, kChannels> playing{};
    playing[3] = true;
    c.now = 30;
    CHECK(c.load(200, true, playing) == 1); // slot 0 stamped 30 first
    CHECK(c.slot[0].stamp == 30);
}

TEST_CASE("sample cache: a streaming voice without a slot fails every reload") {
    auto c = full_cache(10);
    std::array<bool, kChannels> playing{};
    playing[0] = true; // channel 0 streams an AUD; its slot index is still -1
    c.now = 30;
    CHECK(c.load(200, true, playing) == -1);
    CHECK(c.load(105, true, playing) == 5); // a hit still plays
}

TEST_CASE("sample cache: stamps from a longer earlier game block reloads") {
    auto c = full_cache(1000); // the last game ended near tick 1015
    c.now = 3;                 // the next game's tick count restarted at 0
    CHECK(c.load(200, true, idle()) == -1);
    c.now = 1001;
    CHECK(c.load(200, true, idle()) == 0); // only slot 0 (1000) is below now
    c.reset();
    CHECK(c.slot[0].id == -1);
    CHECK(c.now == 1001);
}
