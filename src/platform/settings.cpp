#include "platform/settings.hpp"

#include <cstdlib>
#include <fstream>
#include <string>

namespace d2k::platform {

Settings Settings::load(const std::filesystem::path& file) {
    Settings s;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "play_full_intro") s.play_full_intro = value == "1";
        if (key == "aspect" && (value == "crt" || value == "square")) s.aspect = value;
        if (key == "lang") s.lang = value.substr(0, 32);
        if (key == "game_speed") {
            // FUN_00478c20 clamps values above 100 to 100.
            const int v = std::atoi(value.c_str());
            s.game_speed = v < 0 ? 0 : v > 100 ? 100 : v;
        }
        auto clamp = [&](int lo, int hi) {
            const int v = std::atoi(value.c_str());
            return v < lo ? lo : v > hi ? hi : v;
        };
        if (key == "scroll_speed") s.scroll_speed = clamp(4, 40);
        if (key == "music_volume") s.music_volume = clamp(0, 100);
        if (key == "sfx_volume") s.sfx_volume = clamp(0, 100);
        if (key == "mouse_grab") s.mouse_grab = value != "0";
        if (key == "fullscreen") s.fullscreen = value == "1";
        if (key == "display_width") s.display_width = clamp(320, 8192);
        if (key == "display_height") s.display_height = clamp(200, 8192);
        if (key == "enhanced_movies") s.enhanced_movies = value == "1";
        // FUN_00478c20's checks: an out-of-range value takes the fallback.
        auto check = [&](int lo, int hi, int bad) {
            const int v = std::atoi(value.c_str());
            return v < lo || v > hi ? bad : v;
        };
        if (key == "net_unit_count") s.net_unit_count = check(0, 10, 0);
        if (key == "net_tech_level") s.net_tech_level = check(1, 7, 1);
        if (key == "net_credits") s.net_credits = check(2500, 7000, 2500);
        if (key == "net_ai_players") s.net_ai_players = check(0, 5, 1);
        if (key == "net_crates") s.net_crates = check(0, 1, 0);
        if (key == "net_worms") s.net_worms = check(0, 3, 0);
        if (key == "net_side") s.net_side = check(0, 2, 0);
        if (key == "net_colour") s.net_colour = check(0, 7, 0);
        if (key == "net_name") s.net_name = value.substr(0, 99);
        if (key == "net_port") s.net_port = clamp(1024, 65535);
        if (key == "net_addresses") s.net_addresses = value.substr(0, 400);
    }
    return s;
}

void Settings::save(const std::filesystem::path& file) const {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    out << "play_full_intro=" << (play_full_intro ? 1 : 0) << "\n";
    out << "aspect=" << aspect << "\n";
    out << "lang=" << lang << "\n";
    out << "game_speed=" << game_speed << "\n";
    out << "scroll_speed=" << scroll_speed << "\n";
    out << "music_volume=" << music_volume << "\n";
    out << "sfx_volume=" << sfx_volume << "\n";
    out << "mouse_grab=" << (mouse_grab ? 1 : 0) << "\n";
    out << "fullscreen=" << (fullscreen ? 1 : 0) << "\n";
    out << "display_width=" << display_width << "\n";
    out << "display_height=" << display_height << "\n";
    out << "enhanced_movies=" << (enhanced_movies ? 1 : 0) << "\n";
    out << "net_unit_count=" << net_unit_count << "\n";
    out << "net_tech_level=" << net_tech_level << "\n";
    out << "net_credits=" << net_credits << "\n";
    out << "net_ai_players=" << net_ai_players << "\n";
    out << "net_crates=" << net_crates << "\n";
    out << "net_worms=" << net_worms << "\n";
    out << "net_side=" << net_side << "\n";
    out << "net_colour=" << net_colour << "\n";
    out << "net_name=" << net_name << "\n";
    out << "net_port=" << net_port << "\n";
    out << "net_addresses=" << net_addresses << "\n";
}

} // namespace d2k::platform
