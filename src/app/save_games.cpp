#include "app/save_games.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/user_dirs.hpp"

namespace d2k::app {

namespace {
constexpr uint32_t kHeaderSize = 0xD8, kVersion = 0x28F;
constexpr char kOurs[4] = {'D', '2', 'K', 'F'};
constexpr uint32_t kOurVersion = 1;

std::vector<uint8_t> read_all(const std::filesystem::path& p) {
    std::vector<uint8_t> d;
    if (std::FILE* f = std::fopen(p.string().c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n > 0) {
            d.resize(size_t(n));
            if (std::fread(d.data(), 1, d.size(), f) != d.size()) d.clear();
        }
        std::fclose(f);
    }
    return d;
}

uint32_t u32(const std::vector<uint8_t>& d, size_t o) {
    return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}
} // namespace

std::filesystem::path save_path(int game_type, int n) {
    return game_save_dir() / ((game_type == 1 ? "P" : "S") + std::to_string(n) + ".SAV");
}

std::optional<SaveGame> read_save_header(const std::filesystem::path& p) {
    std::vector<uint8_t> d;
    if (std::FILE* f = std::fopen(p.string().c_str(), "rb")) {
        d.resize(kHeaderSize);
        if (std::fread(d.data(), 1, d.size(), f) != d.size()) d.clear();
        std::fclose(f);
    }
    if (d.size() != kHeaderSize) return std::nullopt;
    SaveGame g;
    g.full = d[4] != 0;
    const char* desc = reinterpret_cast<const char*>(&d[0x0D]);
    g.description.assign(desc, strnlen(desc, kHeaderSize - 0x0D));
    return g;
}

std::optional<SaveGame> read_save(const std::filesystem::path& p, bool full) {
    const auto d = read_all(p);
    // FUN_004418b0 checks the size field and the version.
    if (d.size() < kHeaderSize + 32 || u32(d, 0) != kHeaderSize || u32(d, 8) != kVersion) return std::nullopt;
    auto g = read_save_header(p);
    if (!g) return std::nullopt;
    size_t o = kHeaderSize;
    auto& c = g->common;
    c.mission = d[o];
    c.side = d[o + 1];
    c.game_type = u32(d, o + 2);
    c.difficulty = uint16_t(d[o + 6] | d[o + 7] << 8);
    const char* m = reinterpret_cast<const char*>(&d[o + 8]);
    c.music.assign(m, strnlen(m, 10));
    c.scroll_speed = u32(d, o + 18);
    c.game_speed = uint16_t(d[o + 22] | d[o + 23] << 8);
    c.sfx_volume = u32(d, o + 24);
    c.cd_volume = u32(d, o + 28);
    o += 32;
    if (!full) return g;
    if (d.size() < o + 16 || std::memcmp(&d[o], kOurs, 4) != 0 || u32(d, o + 4) != kOurVersion) return std::nullopt;
    o += 8;
    const uint32_t ml = u32(d, o);
    o += 4;
    if (d.size() < o + ml + 4) return std::nullopt;
    g->mission.assign(reinterpret_cast<const char*>(&d[o]), ml);
    o += ml;
    const uint32_t sl = u32(d, o);
    o += 4;
    if (d.size() < o + sl) return std::nullopt;
    g->state.assign(d.begin() + std::ptrdiff_t(o), d.begin() + std::ptrdiff_t(o + sl));
    return g;
}

bool write_save(const std::filesystem::path& p, const SaveGame& g) {
    std::vector<uint8_t> d(kHeaderSize, 0);
    auto put32 = [&](size_t o, uint32_t v) {
        for (int b = 0; b < 4; ++b) d[o + size_t(b)] = uint8_t(v >> (8 * b));
    };
    put32(0, kHeaderSize);
    d[4] = g.full ? 1 : 0;
    put32(8, kVersion);
    d[0x0C] = g.full ? 8 : 0; // the player-block count 0x798628 (informational here)
    std::memcpy(&d[0x0D], g.description.data(), std::min<size_t>(g.description.size(), kHeaderSize - 0x0D - 1));
    const auto& c = g.common;
    const size_t o = d.size();
    d.resize(o + 32, 0);
    d[o] = c.mission;
    d[o + 1] = c.side;
    put32(o + 2, c.game_type);
    d[o + 6] = uint8_t(c.difficulty);
    d[o + 7] = uint8_t(c.difficulty >> 8);
    std::memcpy(&d[o + 8], c.music.data(), std::min<size_t>(c.music.size(), 9));
    put32(o + 18, c.scroll_speed);
    d[o + 22] = uint8_t(c.game_speed);
    d[o + 23] = uint8_t(c.game_speed >> 8);
    put32(o + 24, c.sfx_volume);
    put32(o + 28, c.cd_volume);
    if (g.full) {
        auto app32 = [&](uint32_t v) {
            for (int b = 0; b < 4; ++b) d.push_back(uint8_t(v >> (8 * b)));
        };
        d.insert(d.end(), kOurs, kOurs + 4);
        app32(kOurVersion);
        app32(uint32_t(g.mission.size()));
        d.insert(d.end(), g.mission.begin(), g.mission.end());
        app32(uint32_t(g.state.size()));
        d.insert(d.end(), g.state.begin(), g.state.end());
    }
    std::FILE* f = std::fopen(p.string().c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(d.data(), 1, d.size(), f) == d.size();
    std::fclose(f);
    return ok;
}

} // namespace d2k::app
