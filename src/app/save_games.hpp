#pragma once
// Saved games: GameSave\S<n>.SAV (campaign) / P<n>.SAV (skirmish), slot 0
// the newest. The header (0xD8 bytes) and the common block are the
// original's (docs/re/27 section 2); an in-mission save then carries our
// own simulation state instead of the exe's memory dumps.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace d2k::app {

struct SaveCommon {             // FUN_004414d0 after the header (32 bytes)
    uint8_t mission = 1;        // gMissionNumber 0x4de610
    uint8_t side = 0;           // gSideId 0x798544
    uint32_t game_type = 0;     // gGameType 0x797e34
    uint16_t difficulty = 1;    // 0x4e8bf0
    std::string music;          // 0x797a98, 10 bytes
    uint32_t scroll_speed = 20; // 0x4eb030
    uint16_t game_speed = 75;   // 0x4e3b18
    uint32_t sfx_volume = 100, cd_volume = 100; // 0x4e5fc4 / 0x4e5fc8
};

struct SaveGame {
    bool full = false;          // header +4: saved during a mission
    std::string description;    // header +0x0D
    SaveCommon common;
    std::string mission;        // ours: the mission file (e.g. "A2V1")
    std::vector<uint8_t> state; // ours: GameScreen::save_blob()
};

std::filesystem::path save_path(int game_type, int n);
// The header only (GetGameSaves); nullopt if the file cannot be read.
std::optional<SaveGame> read_save_header(const std::filesystem::path& p);
// Header + common block, and for full saves our state (nullopt if it is
// not one of ours, e.g. an original exe save).
std::optional<SaveGame> read_save(const std::filesystem::path& p, bool full);
bool write_save(const std::filesystem::path& p, const SaveGame& g);

} // namespace d2k::app
