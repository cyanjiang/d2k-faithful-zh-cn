// RE: docs/re/24-score-and-next-mission.md section 2.
#include "campaign/score.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace d2k::campaign {

namespace {
constexpr int32_t kMax[3] = {150, 100, 50}; // 0x4dedc4 / 0x4dedc8 / 0x4dedcc
} // namespace

void score_targets(ScoreCount& s, const std::array<ScoreSide, 8>& sides, int local) {
    // Sums over every active side (the local player and allies included).
    uint32_t U = 0, B = 0, S = 0;
    for (const auto& side : sides) {
        if (!side.active) continue;
        U += side.units_lost;
        B += side.buildings_lost;
        S += side.spice;
    }
    const ScoreSide& L = sides[size_t(std::clamp(local, 0, 7))];
    // Unsigned products and quotients, as the exe (0x43ed4b..).
    s.our_t[0] = U ? int32_t((U - L.units_lost) * uint32_t(kMax[0]) / U) : 0;
    s.their_t[0] = U ? kMax[0] - s.our_t[0] : 0;
    s.our_t[1] = B ? int32_t((B - L.buildings_lost) * uint32_t(kMax[1]) / B) : 0;
    s.their_t[1] = B ? kMax[1] - s.our_t[1] : 0;
    s.our_t[2] = S ? int32_t(uint32_t(kMax[2]) * L.spice / S) : 0;
    s.their_t[2] = S ? kMax[2] - s.our_t[2] : 0;
    s.our_c = {};
    s.their_c = {};
}

int32_t score_points(int32_t counter, float m) {
    // FILD counter, FMUL float m, __ftol (0x43f1fa..0x43f205). A counter of
    // at most 150 times a 24-bit mantissa fits in 53 bits: the product is
    // exact, and ftol truncates.
    return int32_t(std::trunc(double(counter) * double(m)));
}

std::string score_our_text(const ScoreCount& s, int k) {
    return std::to_string(uint32_t(score_points(s.our_c[size_t(k)], s.m)) / s.d);
}

std::string score_their_text(const ScoreCount& s, int k) {
    return std::to_string(score_points(s.their_c[size_t(k)], s.m));
}

ScoreStep score_step(ScoreCount& s, uint32_t now_ms) {
    ScoreStep r;
    if (s.pausing) {
        // |GetTickCount() - start| < 2000 ms: still pausing.
        const int32_t diff = int32_t(now_ms - s.pause_ms);
        if ((diff < 0 ? -int64_t(diff) : int64_t(diff)) < 2000) return r;
    }
    s.pausing = false;
    s.pause_ms = now_ms;
    auto done = [&](size_t k) { return s.our_c[k] == s.our_t[k] && s.their_c[k] == s.their_t[k]; };
    auto advance = [&](size_t k) {
        r.tick = true; // S_SCORETICK into the voice queue, before the counters move
        if (s.our_c[k] != s.our_t[k]) {
            ++s.our_c[k];
            r.our_moved[k] = true;
        }
        if (s.their_c[k] != s.their_t[k]) {
            ++s.their_c[k];
            r.their_moved[k] = true;
        }
    };
    // The phase is the first one not at its targets. A phase that is
    // already done when reached is passed in the same call, without a pause.
    bool pause = false;
    if (!done(0)) {
        advance(0);
        pause = done(0);
    } else if (!done(1)) {
        advance(1);
        pause = done(1);
    } else if (!done(2)) {
        advance(2); // no pause after the spice
    } else {
        r.busy = false; // 0x516270 = 1; returns before the totals
        return r;
    }
    if (pause) {
        s.pausing = true;
        s.pause_ms = now_ms;
    }
    // The totals (0x43f98f..): theirs the sum of the points; ours each
    // part divided by d (unsigned), then the sum divided by d (signed).
    int32_t theirs = 0;
    uint32_t ours = 0;
    for (size_t k = 0; k < 3; ++k) {
        theirs += score_points(s.their_c[k], s.m);
        ours += uint32_t(score_points(s.our_c[k], s.m)) / s.d;
    }
    r.totals = true;
    s.their_total = theirs;
    s.our_total = int32_t(ours) / int32_t(s.d);
    return r;
}

void score_skip_counters(ScoreCount& s) {
    for (size_t k = 0; k < 3; ++k) {
        if (s.our_t[k] > 0) s.our_c[k] = s.our_t[k] - 1;
        if (s.their_t[k] > 0) s.their_c[k] = s.their_t[k] - 1;
    }
}

int score_bar_width(int32_t counter, int k) {
    // Ours: (u32)(counter * 100) / max; theirs the same through a 64-bit
    // product masked to 32 bits: equal for counters 0..150.
    return int(uint32_t(counter * 100) / uint32_t(kMax[std::clamp(k, 0, 2)]));
}

uint32_t score_rank(uint32_t score) {
    // FUN_0043fc00: the first threshold reached, from the top.
    static const uint32_t kFrom[] = {1350, 1300, 1200, 1100, 950, 800, 650, 500, 350, 300, 250};
    for (size_t i = 0; i < std::size(kFrom); ++i) {
        if (score >= kFrom[i]) return uint32_t(11 - i);
    }
    return 0;
}

int high_score_insert(HighScores& t, uint32_t score, long side, long mission_number) {
    // The first slot that is unused or has a lower score (unsigned) takes
    // the entry; the slots below move down by one (memmove), slot 6 drops.
    for (int k = 0; k < 6; ++k) {
        if (t[size_t(k)].used && t[size_t(k)].score >= score) continue;
        for (int j = 5; j > k; --j) t[size_t(j)] = t[size_t(j - 1)];
        HighScore& h = t[size_t(k)];
        // Written: used, rank (at most 11), name "", house, the low byte
        // of the mission (0 -> 1), score. The other bytes keep the old
        // slot's values, which are 0 in every reachable case.
        h.used = true;
        h.rank = std::min<uint32_t>(score_rank(score), 11);
        h.name.clear();
        h.house = side >= 0 && side <= 2 ? uint32_t(side) : 3;
        const uint8_t m = uint8_t(mission_number - 1);
        h.mission = (h.mission & ~0xffu) | (m == 0 ? 1u : m);
        h.score = score;
        return k;
    }
    return -1;
}

HighScores high_scores_from_file(const std::vector<uint8_t>& d) {
    // Without the file every slot is unused, empty, house 3, mission and
    // score 0. With it the 5 first slots come from the file and slot 6 is
    // reset the same way. (A short file is treated as missing; the exe
    // would keep the rest of its previous table.)
    HighScores t{};
    if (d.size() < kHighScoreFileSize) return t;
    auto u32 = [&](size_t o) {
        return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
    };
    for (size_t k = 0; k < 5; ++k) {
        const size_t o = k * 0xdc;
        HighScore& h = t[k];
        h.used = d[o] != 0;
        h.rank = u32(o + 4);
        const char* name = reinterpret_cast<const char*>(&d[o + 8]);
        h.name.assign(name, strnlen(name, 200));
        h.house = u32(o + 0xd0);
        h.mission = u32(o + 0xd4);
        h.score = u32(o + 0xd8);
    }
    return t;
}

std::vector<uint8_t> high_scores_to_file(const HighScores& t) {
    std::vector<uint8_t> d(kHighScoreFileSize, 0);
    auto put = [&](size_t o, uint32_t v) {
        for (size_t b = 0; b < 4; ++b) d[o + b] = uint8_t(v >> (8 * b));
    };
    for (size_t k = 0; k < 5; ++k) {
        const size_t o = k * 0xdc;
        const HighScore& h = t[k];
        d[o] = h.used ? 1 : 0;
        put(o + 4, h.rank);
        std::copy_n(h.name.begin(), std::min<size_t>(h.name.size(), 199), d.begin() + std::ptrdiff_t(o + 8));
        put(o + 0xd0, h.house);
        put(o + 0xd4, h.mission);
        put(o + 0xd8, h.score);
    }
    return d;
}

} // namespace d2k::campaign
