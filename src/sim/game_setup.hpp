#pragma once
// The game set-up (FUN_00448550's start, docs/re/52 section 2, docs/re/57
// section 3.4) in one place, for the game screen, the tests and network
// games (docs/design/multiplayer.md section 4.1). File access comes from
// the caller, so sim/ still does not depend on platform/.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "formats/map.hpp"
#include "formats/r8.hpp"
#include "formats/templates.hpp"
#include "sim/exe_tables.hpp"
#include "sim/game_map.hpp"
#include "sim/world.hpp"

namespace d2k::sim {

// Reads a game file by its path in the data folder ("Data/bin/VARS.BIN");
// nothing when it does not exist.
using FileReader = std::function<std::optional<std::vector<uint8_t>>(const std::string&)>;

struct GameSetup {
    enum class Type { Campaign, Skirmish, Network };
    Type type = Type::Campaign;
    std::string mission;  // campaign: "A1V1"; skirmish / network: the map, "2PLAY1"
    int local = 0;        // gSideId: the side this machine plays
    int difficulty = 1;   // gDifficultyLevel (campaign, skirmish)
    // FUN_00466140's thick-spice pattern (0x790510), rolled once by the
    // caller before any seed (docs/re/26 section 4, docs/re/57 section 1.9).
    std::array<uint8_t, 64> thick{};
    // The unit-reveal counter 0x54804c as the previous games of this run left
    // it (the exe never resets it; docs/re/16 section 3.4). Only its value
    // mod 8 matters: it decides which units the start reveal handles first.
    uint32_t reveal_counter = 0;
    // Skirmish and network (the gNet* settings, docs/re/51, 57 section 3.2):
    int ai_players = 0, unit_count = 0, tech = 0, worms = 0, credits = 0;
    bool crates = false;
    int humans = 1;                       // network: humans are sides 0 .. humans - 1
    std::array<uint8_t, 8> house{};       // humans' houses (skirmish: side 0 only)
    std::array<uint8_t, 8> colour{};      // humans' colours (skirmish: side 0 only)
    std::array<uint8_t, 8> handicap{};    // network (0x6b922e): 0 easy, 1 normal, 2 hard
    std::array<std::string, 8> names{};   // network players' names
    uint32_t latency = 40;                // network: the start latency (FUN_0045a7a0: 40 LAN)
};

// Everything one game needs; the World refers to the members, so keep the
// Game alive while the World is used.
struct Game {
    ExeTables tables;
    formats::Templates templates;
    formats::MapFile map;
    formats::MissionFile mis;           // after the skirmish / network overrides
    formats::TileAttributes attributes; // the mission's TILEATR
    std::unique_ptr<GameMap> game_map;
    std::unique_ptr<World> world;
    std::vector<formats::R8Image> data_images; // DATA.R16, for the sprite bank
    // A hash of every game file build_game read (paths and contents): network
    // games compare it at the start sync, so different data (a modded rules
    // file, another map file) shows as out of sync at once.
    uint64_t data_hash = 0xcbf29ce484222325ull;
};

// Builds the map and the World, sets up the sides and starts the AI and
// the script, drawing from `rng` in the exe's order. The caller seeds
// `rng` first (campaign: clock; skirmish: not; network: the host's seed).
std::unique_ptr<Game> build_game(const GameSetup& setup, Rng& rng, const FileReader& read);

} // namespace d2k::sim
