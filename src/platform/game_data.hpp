#pragma once
// Locating and validating the player's original Dune 2000 data folder.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace d2k::platform {

class GameData {
public:
    // Checks that `root` looks like a Dune 2000 1.06 folder (as produced by
    // tools/import_game.py). Returns the list of problems; empty means OK.
    static std::vector<std::string> validate(const std::filesystem::path& root);

    explicit GameData(std::filesystem::path root) : root_(std::move(root)) {}

    const std::filesystem::path& root() const { return root_; }

    // Resolves a path like "Data/UI_DATA/MENUS.UIB" case-insensitively,
    // since the original game ran on a case-insensitive filesystem.
    std::optional<std::filesystem::path> find(const std::string& relative) const;

private:
    std::filesystem::path root_;
};

} // namespace d2k::platform
