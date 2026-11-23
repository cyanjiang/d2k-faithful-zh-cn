#pragma once
// Optional translation table: English source text -> localised UTF-8 text.
// Kept outside the game data on purpose: the first-run setup validates all
// 1.06 files against docs/inventory-1.06.csv, so text.uib must never change.

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace d2k::i18n {

class Translation {
public:
    // Tab-separated UTF-8: "source<TAB>translation". Blank lines and lines
    // starting with '#' are ignored. "\\n" in the value becomes the game's
    // 0xAC forced line break; "\\" becomes a literal backslash.
    static Translation load(const std::filesystem::path& file);
    // Returns `src` itself when there is no entry (English fallback).
    std::string_view lookup(std::string_view src) const;
    bool contains(std::string_view src) const { return exact_.find(std::string(src)) != exact_.end(); }
    std::size_t size() const { return exact_.size(); }
    bool valid() const { return !exact_.empty(); }

private:
    std::unordered_map<std::string, std::string> exact_;
    std::unordered_map<std::string, std::string> trimmed_; // tolerant fallback
};

} // namespace d2k::i18n
