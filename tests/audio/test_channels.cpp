#include <doctest/doctest.h>

#include <initializer_list>
#include <tuple>
#include <utility>

#include "audio/channels.hpp"

using namespace d2k;

// docs/re/59-sound-channels.md

namespace {
audio::PanVolume at(int cx, int cy, int scroll_x, int scroll_y, int map_w, int sfx = 100) {
    return audio::positional(cx, cy, scroll_x, scroll_y, 480, 380, map_w, sfx);
}
} // namespace

TEST_CASE("FUN_0046f540: on screen, centred at volume 50") {
    // View at (0, 0): the centre cell is (7, 5); the box is 7 x 5 cells around it.
    for (auto [x, y] : std::initializer_list<std::pair<int, int>>{{7, 5}, {0, 5}, {14, 5}, {7, 0}, {7, 10}, {0, 0}, {14, 10}}) {
        const auto p = at(x, y, 0, 0, 64);
        CHECK(p.pan == 0x40);
        CHECK(p.volume == 50);
    }
    // Scrolled by whole and partial cells: 100 / 32 = 3, centre (10, 8).
    CHECK(at(10, 8, 100, 100, 64).volume == 50);
    CHECK(at(17, 13, 100, 100, 64).volume == 50);
}

TEST_CASE("FUN_0046f540: off screen, pan and fall-off by the map width") {
    // 13 cells to the right: pan 64 - 13*64/64 = 51, mirrored to 77; volume 35 - 455/64 = 28.
    auto p = at(20, 5, 0, 0, 64);
    CHECK(p.pan == 77);
    CHECK(p.volume == 28);
    // The same distance to the left keeps 51.
    p = at(20, 5, 26 * 32, 0, 64); // centre (33, 5)
    CHECK(p.pan == 51);
    CHECK(p.volume == 28);
    // Only below the box: pan stays centred, volume 35 - 6*35/64 = 32.
    p = at(7, 11, 0, 0, 64);
    CHECK(p.pan == 0x40);
    CHECK(p.volume == 32);
    // Far right: pan 64 - 56 = 8 -> 16 -> 112; volume 35 - 1960/64 = 5.
    p = at(63, 60, 0, 0, 64);
    CHECK(p.pan == 112);
    CHECK(p.volume == 5);
    // A narrow map: pan 64 - 33*64/40 = 12 -> 16 -> 112; volume 35 - 1155/40 = 7.
    p = at(40, 5, 0, 0, 40);
    CHECK(p.pan == 112);
    CHECK(p.volume == 7);
    // Distance >= map width (a tall map): volume 5, no cut-off.
    p = at(7, 40, 0, 0, 10);
    CHECK(p.pan == 0x40);
    CHECK(p.volume == 5);
}

TEST_CASE("FUN_0046f540: the SFX volume scales last, truncating") {
    CHECK(at(7, 5, 0, 0, 64, 75).volume == 37);  // 50 * 75 / 100
    CHECK(at(20, 5, 0, 0, 64, 33).volume == 9);  // 28 * 33 / 100
    CHECK(at(7, 5, 0, 0, 64, 0).volume == 0);
}

TEST_CASE("FUN_0046f210: channel pick") {
    audio::Channels ch{};
    CHECK(audio::pick_channel(ch, 1, 5, 2, 50) == 1); // all idle: the first
    ch[1] = {true, 50, "A"};
    ch[2] = {true, 50, "B"};
    CHECK(audio::pick_channel(ch, 1, 5, 2, 50) == 3);
    ch[0] = {true, 0, "S"};                          // speech is never asked for here
    for (int c = 1; c <= 5; ++c) ch[size_t(c)] = {true, 40, "X"};
    ch[2].volume = 20;
    ch[4].volume = 20;
    CHECK(audio::pick_channel(ch, 1, 5, 2, 25) == 2);  // the first quietest, strictly quieter
    CHECK(audio::pick_channel(ch, 1, 5, 2, 20) == -1); // equal: dropped
    CHECK(audio::pick_channel(ch, 1, 5, 0, 5) == 2);   // mode 0: always cuts the quietest
    CHECK(audio::pick_channel(ch, 1, 5, 1, 127) == -1); // mode 1: never
    CHECK(audio::pick_channel(ch, 0, 0, 0, 50) == 0);  // the speech fallback (FUN_0046faa0)
    CHECK(audio::pick_channel(ch, 3, 1, 1, 50) == -1); // last < first: last = first (busy)
    ch[3].playing = false;
    CHECK(audio::pick_channel(ch, 3, 1, 1, 50) == 3);
    CHECK(audio::pick_channel(ch, 1, 6, 0, 50) == -1); // channel 6 does not exist
}

TEST_CASE("FUN_0046f4f0: a sample on any channel") {
    audio::Channels ch{};
    ch[0] = {true, 100, "S_THUMPER"};
    CHECK(audio::sample_playing(ch, "S_THUMPER"));
    ch[0].playing = false;
    CHECK_FALSE(audio::sample_playing(ch, "S_THUMPER"));
    ch[5] = {true, 5, "EXPL1"};
    CHECK(audio::sample_playing(ch, "EXPL1"));
}

TEST_CASE("FUN_0046f820: the voice rings") {
    audio::VoiceQueue q;
    // mode 1: no duplicates in the ring, none of the voice playing; 4 per ring.
    CHECK(q.push("S_A", false, 0, ""));
    CHECK_FALSE(q.push("S_A", false, 0, ""));
    CHECK_FALSE(q.push("S_B", false, 0, "S_B"));
    CHECK(q.push("S_B", false, 0, "S_X"));
    CHECK(q.push("S_C", false, 0, ""));
    CHECK(q.push("S_D", false, 0, ""));
    CHECK_FALSE(q.push("S_E", false, 0, "")); // full at 4
    CHECK(q.push("S_A", false, 1, ""));       // the other ring is not searched
    // Ring 1 plays first, then ring 0 in order.
    CHECK(*q.pop() == "S_A");
    CHECK(*q.pop() == "S_A");
    CHECK(*q.pop() == "S_B");
    CHECK(*q.pop() == "S_C");
    CHECK(*q.pop() == "S_D");
    CHECK_FALSE(q.pop().has_value());
    CHECK(q.empty());
}

TEST_CASE("FUN_0046f820 mode 0") {
    audio::VoiceQueue q;
    CHECK(q.push("BUTTON", true, 0, "S_UNITREADY"));      // waits behind a playing voice
    CHECK_FALSE(q.push("BUTTON", true, 0, ""));           // only when both rings are empty
    q.clear();
    CHECK_FALSE(q.push("S_MONEYTICK", true, 0, "S_UNITREADY")); // never behind a voice
    CHECK_FALSE(q.push("S_CHATMSG", true, 0, "S_MONEYTICK"));   // nor behind the tick
    CHECK(q.push("S_MONEYTICK", true, 0, ""));                   // idle: queued
    q.clear();
    CHECK(q.push("S_LOWPOWER", false, 0, ""));
    CHECK_FALSE(q.push("S_POWERDOWN", true, 0, ""));
}

TEST_CASE("Miles volume as DSBVOLUME (MSS32.DLL FUN_2000de00 / FUN_2000de70)") {
    CHECK(audio::miles_volume_db(0) == -10000);
    CHECK(audio::miles_volume_db(127) == 0);
    CHECK(audio::miles_volume_db(200) == 0); // clamped to 127
    CHECK(audio::miles_volume_db(-3) == -10000);
    // trunc(-2000 * log10(127 / v))
    CHECK(audio::miles_volume_db(100) == -207);
    CHECK(audio::miles_volume_db(50) == -809);
    CHECK(audio::miles_volume_db(35) == -1119);
    CHECK(audio::miles_volume_db(5) == -2809);
    CHECK(audio::volume_gain(0) == 0.0f);
    CHECK(audio::volume_gain(127) == 1.0f);
    // About v / 127, a little louder by the truncation.
    CHECK(audio::volume_gain(50) == doctest::Approx(0.394003).epsilon(1e-5));
    for (int v = 1; v < 127; ++v) {
        CHECK(audio::volume_gain(v) >= float(v) / 127.0f * 0.9999f);
        CHECK(audio::volume_gain(v) < float(v) / 127.0f * 1.0012f);
    }
}

TEST_CASE("Miles pan as DSBPAN (MSS32.DLL FUN_2000def0)") {
    CHECK(audio::miles_pan_db(64) == 0);
    CHECK(audio::miles_pan_db(63) == 0); // 63 is centre too
    CHECK(audio::miles_pan_db(0) == -10000);
    CHECK(audio::miles_pan_db(127) == 10000);
    CHECK(audio::miles_pan_db(16) == -1190);  // trunc(-2000 * log10(63 / 16))
    CHECK(audio::miles_pan_db(112) == 1246);  // -trunc(-2000 * log10(63 / 15))
    CHECK(audio::miles_pan_db(65) == 13);     // -trunc(-2000 * log10(63 / 62))
    CHECK(audio::miles_pan_db(77) == -audio::miles_pan_db(50));
}

TEST_CASE("pan gains: the near side at full gain, the far side about pan / 63") {
    auto [l, r] = audio::pan_gains(64);
    CHECK(l == 1.0f);
    CHECK(r == 1.0f);
    std::tie(l, r) = audio::pan_gains(63);
    CHECK(l == 1.0f);
    CHECK(r == 1.0f);
    std::tie(l, r) = audio::pan_gains(16);
    CHECK(l == 1.0f);
    CHECK(r == doctest::Approx(0.254097).epsilon(1e-5)); // 10^(-1190 / 2000), 16 / 63 = 0.2540
    std::tie(l, r) = audio::pan_gains(112);
    CHECK(l == doctest::Approx(0.238232).epsilon(1e-5)); // 10^(-1246 / 2000), 15 / 63 = 0.2381
    CHECK(r == 1.0f);
    std::tie(l, r) = audio::pan_gains(0);
    CHECK(l == 1.0f);
    CHECK(r == 0.0f);
    std::tie(l, r) = audio::pan_gains(127);
    CHECK(l == 0.0f);
    CHECK(r == 1.0f);
}
