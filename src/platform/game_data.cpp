#include "platform/game_data.hpp"

#include "formats/file_io.hpp"
#include "platform/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace d2k::platform {

namespace fs = std::filesystem;

namespace {

bool iequals(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

// The supported exe: Dune 2000 1.06 US (docs/data-sources.md).
constexpr std::uintmax_t kExe106Size = 999'936;
constexpr const char* kExe106Sha256 = "51082bd7306b5e76b93fc694fc6455ce1704140918f9abe2d42394682b85e8f2";

} // namespace

std::optional<fs::path> GameData::find(const std::string& relative) const {
    fs::path cur = root_;
    std::stringstream ss(relative);
    std::string part;
    while (std::getline(ss, part, '/')) {
        std::error_code ec;
        if (fs::exists(cur / part, ec)) {
            cur /= part;
            continue;
        }
        bool found = false;
        for (const auto& entry : fs::directory_iterator(cur, ec)) {
            if (iequals(entry.path().filename().string(), part)) {
                cur = entry.path();
                found = true;
                break;
            }
        }
        if (!found) {
            return std::nullopt;
        }
    }
    return cur;
}

std::vector<std::string> GameData::validate(const fs::path& root) {
    std::vector<std::string> problems;
    if (!fs::is_directory(root)) {
        problems.push_back("not a directory: " + root.string());
        return problems;
    }
    GameData gd(root);
    for (const char* required : {"DUNE2000.DAT", "Data/DATA.R8", "Data/DATA.R16",
                                 "Data/bin/PALETTE.BIN", "Data/UI_DATA/MENUS.UIB",
                                 "MOVIES/G_INT1_E.VQA", "MUSIC/OPTIONS.AUD",
                                 "MISSIONS/A1V1.MAP"}) {
        if (!gd.find(required)) {
            problems.push_back(std::string("missing ") + required);
        }
    }
    if (auto exe = gd.find("DUNE2000.DAT")) {
        // Tables (territory shapes, spice tiles, ...) are read from the exe
        // at 1.06 US addresses, so any other build must be refused.
        if (fs::file_size(*exe) != kExe106Size ||
            sha256_hex(formats::read_file(*exe)) != kExe106Sha256) {
            problems.push_back("DUNE2000.DAT is not Dune 2000 1.06 (US). "
                               "Apply the official 1.06 patch (d2k106us.exe).");
        }
    }
    return problems;
}

} // namespace d2k::platform
