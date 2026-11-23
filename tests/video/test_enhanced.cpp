#include <doctest/doctest.h>

#include <filesystem>

#include "platform/settings.hpp"
#include "video/enhanced_video.hpp"

using namespace d2k;

TEST_CASE("enhanced movies: the file is <dir>/<VQA name>.mp4 (docs/enhanced-movies.md)") {
    const std::filesystem::path dir = "movies-enhanced";
    CHECK(video::enhanced_path_for(dir, "gamedata/1.06/MOVIES/A_BR01_E.VQA") == dir / "A_BR01_E.mp4");
    CHECK(video::enhanced_path_for(dir, "T_TITL_E.vqa") == dir / "T_TITL_E.mp4");
}

TEST_CASE("enhanced movies: a missing file plays the original pictures") {
    CHECK(video::EnhancedVideo::open("no-such-folder/NONE.mp4") == nullptr);
}

TEST_CASE("enhanced movies: the option is off by default and saved") {
    const auto file = std::filesystem::temp_directory_path() / "d2k_test_enhanced_settings.ini";
    std::filesystem::remove(file);
    CHECK_FALSE(platform::Settings::load(file).enhanced_movies);
    platform::Settings s;
    s.enhanced_movies = true;
    s.save(file);
    CHECK(platform::Settings::load(file).enhanced_movies);
    std::filesystem::remove(file);
}
