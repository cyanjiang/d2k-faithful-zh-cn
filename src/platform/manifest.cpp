#include "platform/manifest.hpp"

#include <algorithm>
#include <string_view>

#include "formats/file_io.hpp"
#include "platform/game_data.hpp"
#include "platform/sha256.hpp"

namespace d2k::platform {

namespace {
constexpr ManifestEntry kManifest[] = {
#include "manifest_106.inc"
};
} // namespace

std::span<const ManifestEntry> manifest_106() { return kManifest; }

std::vector<std::string> quick_check(const std::filesystem::path& root, size_t limit) {
    std::vector<std::string> problems;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        problems.push_back("no game data folder: " + root.string());
        return problems;
    }
    GameData gd(root);
    size_t bad = 0;
    for (const auto& e : manifest_106()) {
        const auto p = gd.find(e.path);
        const bool ok = p && std::filesystem::file_size(*p, ec) == e.size && !ec;
        if (ok) continue;
        if (bad++ < limit) problems.push_back(std::string(p ? "wrong size: " : "missing: ") + e.path);
    }
    if (bad > limit) problems.push_back("... " + std::to_string(bad - limit) + " more");
    if (bad == 0) {
        // The exe tables are read at 1.06 addresses: its content must match.
        const auto& exe = *std::find_if(manifest_106().begin(), manifest_106().end(),
                                        [](const ManifestEntry& e) { return std::string_view(e.path) == "DUNE2000.DAT"; });
        if (sha256_hex(formats::read_file(*gd.find(exe.path))) != exe.sha256) {
            problems.push_back("DUNE2000.DAT is not the 1.06 (US) game");
        }
    }
    return problems;
}

} // namespace d2k::platform
