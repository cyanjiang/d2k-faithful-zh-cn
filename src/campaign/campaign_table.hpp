#pragma once
// The campaign table (Data\UI_DATA\campaign.uib): 3 houses x 10 records of
// 0x104 bytes, a raw dump of the exe's table at 0x795708 (filled from the
// text Campaign.txt by FUN_00477790). Format: docs/formats/campaign.md

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace d2k::campaign {

constexpr int kHouses = 3;     // 0 Atreides, 1 Harkonnen, 2 Ordos (gSideId)
constexpr int kRecords = 10;   // record 0: start, 1..9: missions
constexpr int kTerritories = 27;

struct CampaignRecord {
    // Territories (1..27, 0 = none) the player can pick for this mission,
    // and the mission file base names (e.g. "a1v1"; empty when none).
    std::array<uint8_t, 2> territory{};
    std::array<std::string, 2> mission;
    // Arrow for each choice: direction (North..NorthWest = 0..7; not used
    // by the 1.06 globe code) and position (0,0 = territory centre).
    std::array<int, 2> arrow_direction{};
    std::array<int, 2> arrow_x{}, arrow_y{};
    // Territories each house (index = house) holds after this record.
    std::array<std::array<uint8_t, 3>, kHouses> gained{};
    // +0x30 (Campaign.txt column 17 times a constant): the score multiplier m;
    // its only reader is FUN_0043f030 (0x43f10d), docs/re/24 section 2.3.
    float value = 0;
    // Order in which the globe reveals each house's new territories, and
    // the samples.uib speech played for each ("NONE" = silent).
    std::array<int, 3> reveal_house{};
    std::array<std::string, 3> reveal_speech;
    std::string track;   // +0xF4, column 25 (e.g. "atregain"): the music that starts the mission (docs/re/15)
    // +0x100, column 24: the mission's enemy house; its only reader is
    // FUN_0043d820 (0x43d88a), which picks the score's "their" bar images
    // (docs/re/24 section 2.2).
    int track_house = 0;
};

struct CampaignTable {
    std::array<std::array<CampaignRecord, kRecords>, kHouses> records;

    static CampaignTable parse(std::span<const uint8_t> data);
    const CampaignRecord& at(int house, int record) const { return records.at(house).at(record); }
};

} // namespace d2k::campaign
