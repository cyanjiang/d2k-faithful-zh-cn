// The front end's logic that runs without a window: the campaign score
// count-up and the high-score table (docs/re/24), value bars (docs/re/55),
// skirmish colours and places (docs/re/51, 56).
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "campaign/score.hpp"
#include "formats/file_io.hpp"
#include "platform/game_data.hpp"
#include "sim/game_setup.hpp"
#include "sim/world.hpp"
#include "ui/render.hpp"

using namespace d2k;

TEST_CASE("score: the points are the exact product, truncated (docs/re/24 section 2.4)") {
    // 1.3f = 1.29999995...; 10 x 1.3f = 12.9999995 -> 12. A float32
    // product rounds to 13.0.
    CHECK(campaign::score_points(10, 1.3f) == 12);
    CHECK(campaign::score_points(150, 1.0f) == 150);
    CHECK(campaign::score_points(0, 5.0f) == 0);
    CHECK(campaign::score_points(3, 1.5f) == 4);
}

TEST_CASE("score: targets from every active side (FUN_0043ec70, docs/re/24 section 2.3)") {
    std::array<campaign::ScoreSide, 8> sides{};
    sides[0] = {true, 4, 1, 3000};  // the player
    sides[1] = {true, 16, 3, 1000}; // the enemy
    sides[2] = {false, 99, 99, 99}; // not in the mission: not counted
    campaign::ScoreCount c;
    campaign::score_targets(c, sides, 0);
    CHECK(c.our_t[0] == (20 - 4) * 150 / 20); // 120
    CHECK(c.their_t[0] == 150 - 120);
    CHECK(c.our_t[1] == (4 - 1) * 100 / 4); // 75
    CHECK(c.their_t[1] == 25);
    CHECK(c.our_t[2] == 50 * 3000 / 4000); // 37
    CHECK(c.their_t[2] == 13);
    // Nothing lost by anybody: both targets 0.
    std::array<campaign::ScoreSide, 8> quiet{};
    quiet[0] = {true, 0, 0, 0};
    campaign::score_targets(c, quiet, 0);
    CHECK(c.our_t == std::array<int32_t, 3>{});
    CHECK(c.their_t == std::array<int32_t, 3>{});
}

TEST_CASE("score: the count-up pauses 2 s after a phase it counted, not after an empty one (FUN_0043f030)") {
    campaign::ScoreCount c;
    c.our_t = {2, 0, 1};
    c.their_t = {1, 0, 0};
    uint32_t t = 1000;
    auto r = campaign::score_step(c, t); // units: 1 / 1
    CHECK(r.busy);
    CHECK(r.tick);
    CHECK(r.our_moved[0]);
    CHECK(r.their_moved[0]);
    CHECK_FALSE(c.pausing);
    r = campaign::score_step(c, t += 16); // units: 2 / 1, done -> pause
    CHECK(r.our_moved[0]);
    CHECK_FALSE(r.their_moved[0]);
    CHECK(c.pausing);
    CHECK(c.our_total == 2);
    CHECK(c.their_total == 1);
    r = campaign::score_step(c, t + 1999); // still pausing: nothing, no totals
    CHECK(r.busy);
    CHECK_FALSE(r.tick);
    CHECK_FALSE(r.totals);
    // After the pause the empty buildings phase is passed in the same
    // call: the spice counts at once, with no second pause.
    r = campaign::score_step(c, t += 2000);
    CHECK(r.busy);
    CHECK(r.our_moved[2]);
    CHECK_FALSE(c.pausing);
    CHECK(c.our_total == 3);
    // "Done" comes one call after the last step, without new totals.
    r = campaign::score_step(c, t += 16);
    CHECK_FALSE(r.busy);
    CHECK_FALSE(r.totals);
    CHECK_FALSE(r.tick);
}

TEST_CASE("score: the skip sets every counter to target - 1, also in phases already counted (FUN_0043fb60)") {
    campaign::ScoreCount c;
    c.our_t = {3, 2, 0};
    c.their_t = {1, 0, 4};
    c.our_c = {3, 1, 0}; // the units were done, the buildings were counting
    c.their_c = {1, 0, 0};
    campaign::score_skip_counters(c);
    CHECK(c.our_c == std::array<int32_t, 3>{2, 1, 0});
    CHECK(c.their_c == std::array<int32_t, 3>{0, 0, 3});
    // Stepping to the end: the units count again (and pause), then the
    // buildings (and pause), then the spice.
    uint32_t t = 0;
    int calls = 0;
    while (campaign::score_step(c, t).busy) {
        t += 1;
        ++calls;
        REQUIRE(calls < 10000);
    }
    CHECK(c.our_c == c.our_t);
    CHECK(c.their_c == c.their_t);
    CHECK(t >= 4000); // two 2-second pauses inside the loop
}

TEST_CASE("score: ours is divided by d per part and in total (docs/re/24 section 2.4)") {
    campaign::ScoreCount c;
    c.d = 2;
    c.m = 1.0f;
    c.our_t = {3, 0, 0};
    c.our_c = {2, 0, 0};
    c.their_t = {0, 0, 0};
    campaign::score_step(c, 0); // ours 3
    CHECK(campaign::score_our_text(c, 0) == "1"); // 3 / 2
    CHECK(c.our_total == 0);                       // (3 / 2) / 2
}

TEST_CASE("score: the bar width is counter x 100 / max") {
    CHECK(campaign::score_bar_width(150, 0) == 100);
    CHECK(campaign::score_bar_width(1, 0) == 0);
    CHECK(campaign::score_bar_width(2, 0) == 1);
    CHECK(campaign::score_bar_width(50, 1) == 50);
    CHECK(campaign::score_bar_width(1, 2) == 2);
}

TEST_CASE("high scores: rank thresholds (FUN_0043fc00)") {
    CHECK(campaign::score_rank(0) == 0);
    CHECK(campaign::score_rank(249) == 0);
    CHECK(campaign::score_rank(250) == 1);
    CHECK(campaign::score_rank(299) == 1);
    CHECK(campaign::score_rank(300) == 2);
    CHECK(campaign::score_rank(350) == 3);
    CHECK(campaign::score_rank(500) == 4);
    CHECK(campaign::score_rank(650) == 5);
    CHECK(campaign::score_rank(800) == 6);
    CHECK(campaign::score_rank(950) == 7);
    CHECK(campaign::score_rank(1100) == 8);
    CHECK(campaign::score_rank(1200) == 9);
    CHECK(campaign::score_rank(1300) == 10);
    CHECK(campaign::score_rank(1349) == 10);
    CHECK(campaign::score_rank(1350) == 11);
    CHECK(campaign::score_rank(0xffffffffu) == 11);
}

TEST_CASE("high scores: insertion, ties, house and mission (FUN_0043fc90)") {
    campaign::HighScores t{};
    // An empty table: the first slot.
    CHECK(campaign::high_score_insert(t, 300, 1, 2) == 0);
    CHECK(t[0].used);
    CHECK(t[0].score == 300);
    CHECK(t[0].rank == 2);
    CHECK(t[0].house == 1);
    CHECK(t[0].mission == 1); // gMissionNumber - 1 (already the next mission)
    // An equal score does not pass an existing entry.
    CHECK(campaign::high_score_insert(t, 300, 0, 3) == 1);
    // A higher score goes on top; the others move down.
    CHECK(campaign::high_score_insert(t, 500, 2, 5) == 0);
    CHECK(t[1].score == 300);
    CHECK(t[1].house == 1);
    CHECK(t[2].house == 0);
    // Mission number 1 (mission 0 after the subtraction) is stored as 1;
    // a side outside 0..2 is house 3.
    CHECK(campaign::high_score_insert(t, 100, 7, 1) == 3);
    CHECK(t[3].mission == 1);
    CHECK(t[3].house == 3);
    // Full of higher scores: no row.
    for (int i = 0; i < 6; ++i) campaign::high_score_insert(t, 1000, 0, 2);
    CHECK(campaign::high_score_insert(t, 999, 0, 2) == -1);
    // The sixth slot drops out when a better one arrives.
    t = {};
    for (uint32_t s = 6; s >= 1; --s) campaign::high_score_insert(t, s * 10, 0, 2);
    CHECK(t[5].score == 10);
    CHECK(campaign::high_score_insert(t, 65, 0, 2) == 0);
    CHECK(t[5].score == 20);
}

TEST_CASE("high scores: hs.dat holds the first 5 slots (FUN_0043eee0 / FUN_0043efd0)") {
    campaign::HighScores t{};
    campaign::high_score_insert(t, 777, 2, 4);
    t[0].name = "Paul";
    campaign::high_score_insert(t, 10, 0, 2);
    const std::vector<uint8_t> d = campaign::high_scores_to_file(t);
    CHECK(d.size() == 0x44c);
    CHECK(d[0] == 1);
    CHECK(d[8] == 'P');
    CHECK(d[0xd8] == uint8_t(777 & 0xff));
    const campaign::HighScores back = campaign::high_scores_from_file(d);
    CHECK(back[0].name == "Paul");
    CHECK(back[0].score == 777);
    CHECK(back[0].rank == 5); // 650 <= 777 < 800
    CHECK(back[0].house == 2);
    CHECK(back[0].mission == 3);
    CHECK(back[1].score == 10);
    CHECK_FALSE(back[5].used);
    CHECK(back[5].house == 3);
    // No file: every slot unused, house 3.
    const campaign::HighScores none = campaign::high_scores_from_file({});
    for (const auto& h : none) {
        CHECK_FALSE(h.used);
        CHECK(h.house == 3);
        CHECK(h.name.empty());
    }
}

namespace {
std::unique_ptr<sim::Game> skirmish_game(const platform::GameData& gd, sim::Rng& rng, int ai) {
    sim::GameSetup s;
    s.type = sim::GameSetup::Type::Skirmish;
    s.mission = "2PLAY1";
    s.tech = 7;
    s.credits = 5000;
    s.ai_players = ai;
    s.unit_count = 6;
    s.colour[0] = 2;
    return sim::build_game(s, rng, [&gd](const std::string& p) -> std::optional<std::vector<uint8_t>> {
        auto f = gd.find(p);
        if (!f) return std::nullopt;
        return formats::read_file(*f);
    });
}
} // namespace

TEST_CASE("golden: skirmish sides 6 and 7 get colour 0 (records past the lobby's six, docs/re/51 section 4.2) (skips without data)") {
    if (!std::filesystem::exists(std::filesystem::path(D2K_GAMEDATA_DIR) / "Data/maps/2PLAY1.MAP")) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    sim::Rng rng(1);
    auto g = skirmish_game(gd, rng, 1);
    CHECK(g->world->side_colour[0] == 2);
    CHECK(g->world->side_colour[6] == 0);
    CHECK(g->world->side_colour[7] == 0);
}

TEST_CASE("golden: a loaded skirmish places every dead side again in side order (FUN_004418b0 resets the places, docs/re/56 section 2) (skips without data)") {
    if (!std::filesystem::exists(std::filesystem::path(D2K_GAMEDATA_DIR) / "Data/maps/2PLAY1.MAP")) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    sim::Rng rng(1);
    auto g = skirmish_game(gd, rng, 2);
    auto& w = *g->world;
    w.tick();
    for (int p = 3; p < 8; ++p) CHECK(w.place_[size_t(p)] == p - 3);
    // Side 2 is beaten after the unused sides: place 5.
    for (int i = 0; i < 64 && w.place_[2] == 0xff; ++i) {
        for (auto& u : w.units()) {
            if (u.owner == 2) u.remove = true;
        }
        w.tick();
    }
    REQUIRE(w.place_[2] == 5);
    // The places are not in the save; the load resets them (0x441f08) and
    // the first tick places the dead sides 2..7 again in side order.
    const auto saved = w.save_state();
    sim::Rng rng2(77);
    auto g2 = skirmish_game(gd, rng2, 2);
    REQUIRE(g2->world->load_state(saved));
    for (int p = 0; p < 8; ++p) CHECK(g2->world->place_[size_t(p)] == 0xff);
    g2->world->tick();
    for (int p = 2; p < 8; ++p) CHECK(g2->world->place_[size_t(p)] == p - 2);
    CHECK(g2->world->place_[0] == 0xff);
    CHECK(g2->world->place_[1] == 0xff);
}
TEST_CASE("value bar drag: the exe's float expression and the thumb that follows the mouse (FUN_0048c9c0, docs/re/55 section 2.4)") {
    const ui::Rect bar{226, 272, 92, 12}; // the skirmish unit-count bar (scrbar is 92 wide)
    ui::ElementState s;
    s.vmin = 0;
    s.vmax = 10;
    s.step = 1;
    s.value = 6;
    // Halfway: 0.5 x 100.0f x 10 x 1 x -0.01f = -4.9999999 -> 4, not 5.
    CHECK(ui::valuebar_drag(bar, s, 226 + 37));
    CHECK(s.value == 4);
    CHECK(s.drag_thumb_x == 263);
    CHECK(ui::valuebar_thumb(bar, s).x == 263); // the thumb stays where the mouse put it
    // Past the left end: the minimum, the thumb from the value.
    CHECK(ui::valuebar_drag(bar, s, 200));
    CHECK(s.value == 0);
    CHECK(s.drag_thumb_x == -1);
    CHECK(ui::valuebar_thumb(bar, s).x == 226);
    // Again past the end: no change, nothing to redraw.
    CHECK_FALSE(ui::valuebar_drag(bar, s, 210));
    // Past the right end (x + w - w/5 = 300): the maximum.
    CHECK(ui::valuebar_drag(bar, s, 300));
    CHECK(s.value == 10);
}

TEST_CASE("value bar steps are unsigned and recompute the thumb only on a change (FUN_0048c640 / FUN_0048c450)") {
    ui::ElementState s;
    s.vmin = 0;
    s.vmax = 10;
    s.step = 1;
    s.value = 0;
    s.drag_thumb_x = 230;
    CHECK(ui::valuebar_step(s, -1));
    CHECK(s.value == 10);
    CHECK(s.drag_thumb_x == -1);
    CHECK_FALSE(ui::valuebar_step(s, 1)); // already the maximum
    CHECK(ui::valuebar_step(s, -1));
    CHECK(s.value == 9);
    // A minimum above the step clamps normally.
    ui::ElementState t;
    t.vmin = 1;
    t.vmax = 7;
    t.step = 1;
    t.value = 1;
    CHECK_FALSE(ui::valuebar_step(t, -1));
    CHECK(t.value == 1);
}