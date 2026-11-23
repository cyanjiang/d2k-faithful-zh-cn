// The skirmish start: AI houses, start points, MCVs, starting units and
// worms (FUN_00469790 from 0x469d6e; docs/re/52 sections 2.4 to 2.6).
#include <algorithm>
#include <cmath>

#include "sim/world.hpp"

namespace d2k::sim {

std::vector<std::pair<int, int>> skirmish_starts(const formats::MapFile& map) {
    // Map special 0x17, row by row with x inner (the map scan order).
    std::vector<std::pair<int, int>> out;
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            if (map.at(x, y).special == 0x17 && out.size() < 8) out.emplace_back(x, y);
        }
    }
    return out;
}

void World::skirmish_start(const SkirmishSetup& k) {
    // Humans are sides 0 .. h - 1 (one in a skirmish, 0x7984c0), the AI the
    // next sides (0x469d6e: h = max(humans, 1)).
    const int humans = std::clamp(k.humans, 1, 8);
    const int players = std::min(8, humans + std::clamp(k.ai_players, 0, 7));
    // AI houses: rand() % 3 for the AI sides in order (rand line 0x1608).
    for (int s = humans; s < players; ++s) players_[size_t(s)].house = int(rng_.next() % 3);
    // The MCV type: the last unit record with kind 6.
    int mcv = -1;
    for (const auto& r : templates_.unit_records) {
        if (r.kind == 6) mcv = r.name_index;
    }
    const int count = int(k.starts.size());
    std::array<bool, 8> used{};
    int worms = 0; // shared by all the passes
    for (int p = 0; p < players && count > 0; ++p) {
        int start = 0;
        if (p == 0) {
            start = int(rng_.next() % uint32_t(count)); // rand line 0x1628
        } else {
            // The unused start farthest from the used ones: the smallest
            // ftol(sqrt(dx^2 + dy^2)) to a used start, largest wins, strict >
            // (the lowest index on ties).
            int best = -1;
            for (int s = 0; s < count; ++s) {
                if (used[size_t(s)]) continue;
                int d = 0x7fffffff;
                for (int u = 0; u < count; ++u) {
                    if (!used[size_t(u)]) continue;
                    const int dx = k.starts[size_t(s)].first - k.starts[size_t(u)].first;
                    const int dy = k.starts[size_t(s)].second - k.starts[size_t(u)].second;
                    d = std::min(d, int(std::sqrt(float(dx * dx + dy * dy))));
                }
                if (best < 0 || d > best) {
                    best = d;
                    start = s;
                }
            }
        }
        used[size_t(start)] = true;
        const int sx = k.starts[size_t(start)].first, sy = k.starts[size_t(start)].second;
        const int house = players_[size_t(p)].house;
        if (mcv >= 0) {
            int x = sx, y = sy;
            spiral_free_cell(x, y, 6); // FUN_004a60e0; unchanged when none is free
            const int rec = templates_.unit_record(mcv, house);
            if (rec >= 0) create_unit(p, rec, x, y, x, y);
        }
        // Starting units: TECHPOS[tech][c] = {type A, H, O, dx, dy}.
        for (int c = 0; c < k.unit_count; ++c) {
            const size_t e = size_t(c + k.tech * 10) * 5;
            if (e + 4 >= k.techpos.size()) break;
            if (k.techpos[e] == 0xff || house > 2) continue; // a house above 2 is a fatal error in the exe
            int x = std::clamp(sx + int(int8_t(k.techpos[e + 3])), 0, map_.width() - 1);
            int y = std::clamp(sy + int(int8_t(k.techpos[e + 4])), 0, map_.height() - 1);
            const int rec = templates_.unit_record(k.techpos[e + size_t(house)], house);
            if (rec < 0) continue;
            spiral_free_cell(x, y, 6);
            create_unit(p, rec, x, y, x, y);
        }
        // Worms: the map's kind-5 objects, while the shared count is within gNetWorms.
        for (const auto& o : k.map_units) {
            if (o.building || templates_.unit_records[size_t(o.record)].kind != 5) continue;
            if (++worms <= k.worms) add_map_unit(o);
        }
    }
}

} // namespace d2k::sim
