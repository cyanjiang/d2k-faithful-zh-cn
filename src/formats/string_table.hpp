#pragma once
// Key/value string tables in UI_DATA (text.uib; also used for movie names:
// "mve_intro1" -> "g_int1_e"). The table is an ordered list and keys may
// repeat on purpose: e.g. the credits are 348 consecutive "CreditEntry"
// rows. Format: docs/formats/ui.md

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace d2k::formats {

class StringTable {
public:
    using Entry = std::pair<std::string, std::string>;

    static StringTable parse(std::span<const uint8_t> data);

    // First value stored under `key`, ignoring case: the exe's FUN_0048dc10
    // takes the first _strcmpi match (lo2mis2.uil asks for "B_raider",
    // the table has "B_Raider"). Returns nothing on a miss; the exe then
    // uses entry 0 (ui::Assets::lookup_text). Keys do miss in 1.06, e.g.
    // CGameSes.uil's "MultiPlayer" and loadmis.uil's "AMission4V2".
    std::optional<std::string> get(const std::string& key) const;

    // Every value stored under `key`, in file order.
    std::vector<std::string> get_all(const std::string& key) const;

    const std::vector<Entry>& entries() const { return entries_; }
    size_t size() const { return entries_.size(); }

private:
    std::vector<Entry> entries_;
};

// UIL menu scripts: u32 line count, then lines (u16 length incl. NUL, text).
std::vector<std::string> parse_uil_lines(std::span<const uint8_t> data);

} // namespace d2k::formats
