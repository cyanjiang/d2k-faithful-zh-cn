// The campaign score screen's numbers (SCORE_SINGLE) and the high-score
// table, without any drawing. RE: docs/re/24-score-and-next-mission.md
// (FUN_0043ec70 targets, FUN_0043f030 count step, FUN_0043fb60 skip,
// FUN_0043fc00 rank, FUN_0043fc90 insertion, FUN_0043eee0 / FUN_0043efd0
// GameSave\hs.dat).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace d2k::campaign {

// One side's end-of-mission numbers (the player struct fields FUN_0043ec70 reads).
struct ScoreSide {
    bool active = false;          // +0x24250
    uint32_t units_lost = 0;      // +0x24a08
    uint32_t buildings_lost = 0;  // +0x24a0c
    uint32_t spice = 0;           // +0x2477c
};

// The count-up state. Index 0 units, 1 buildings, 2 spice.
struct ScoreCount {
    std::array<int32_t, 3> our_t{}, their_t{}; // targets (record +0x20.. / 0x516250..)
    std::array<int32_t, 3> our_c{}, their_c{}; // counters (record +0x2C.. / 0x516244..)
    float m = 1.0f;                             // the campaign record's multiplier (+0x30)
    uint32_t d = 1;                             // 2 when the local player lost (0x6b8278)
    bool pausing = false;                       // 0x516280
    uint32_t pause_ms = 0;                      // 0x516284 (GetTickCount)
    int32_t our_total = 0, their_total = 0;     // 0x516264 / 0x516268
};

// FUN_0043ec70: the targets from every active side's numbers; the
// counters start at 0.
void score_targets(ScoreCount& s, const std::array<ScoreSide, 8>& sides, int local);

// ftol(counter * m) on the x87 at 53-bit precision: the exact product.
int32_t score_points(int32_t counter, float m);

// What one FUN_0043f030 call did, for the boxes and the sound.
struct ScoreStep {
    bool busy = true;                    // false = "done" (all six at target; the caller sets 0x516270)
    bool totals = false;                 // the totals were recomputed (not in a pause, not when done)
    bool tick = false;                   // S_SCORETICK queued
    std::array<bool, 3> our_moved{}, their_moved{};
};
// The text of a counter's box: ours divided by d, theirs as is.
std::string score_our_text(const ScoreCount& s, int k);
std::string score_their_text(const ScoreCount& s, int k);

// FUN_0043f030 at time `now_ms` (GetTickCount).
ScoreStep score_step(ScoreCount& s, uint32_t now_ms);
// FUN_0043fb60's first half: every counter = its target - 1 (target > 0).
// The caller then steps until done.
void score_skip_counters(ScoreCount& s);
// The width of a bar for a counter of phase k: counter * 100 / max.
int score_bar_width(int32_t counter, int k);

// FUN_0043fc00.
uint32_t score_rank(uint32_t score);

// A high-score slot (0xDC bytes at 0x515bb8 + i * 0xDC).
struct HighScore {
    bool used = false;   // +0x00
    uint32_t rank = 0;   // +0x04
    std::string name;    // +0x08, 200 bytes
    uint32_t house = 3;  // +0xD0
    uint32_t mission = 0; // +0xD4
    uint32_t score = 0;  // +0xD8
};
using HighScores = std::array<HighScore, 6>;

// FUN_0043fc90's insertion: the slot of the new entry, or -1.
int high_score_insert(HighScores& t, uint32_t score, long side, long mission_number);
// GameSave\hs.dat: the first 5 slots, 0x44C bytes.
constexpr size_t kHighScoreFileSize = 0x44c;
HighScores high_scores_from_file(const std::vector<uint8_t>& d); // FUN_0043eee0
std::vector<uint8_t> high_scores_to_file(const HighScores& t);   // FUN_0043efd0

} // namespace d2k::campaign
