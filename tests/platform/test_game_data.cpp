#include <doctest/doctest.h>

#include <filesystem>

#include "platform/game_data.hpp"
#include "platform/sha256.hpp"

using d2k::platform::GameData;

TEST_CASE("GameData: missing folder is reported") {
    CHECK_FALSE(GameData::validate("this/does/not/exist").empty());
}

TEST_CASE("GameData: real 1.06 folder validates (skips without data)") {
    if (!std::filesystem::exists(D2K_GAMEDATA_DIR)) {
        MESSAGE("skipped: no game data at " D2K_GAMEDATA_DIR);
        return;
    }
    auto problems = GameData::validate(D2K_GAMEDATA_DIR);
    for (const auto& p : problems) {
        MESSAGE(p);
    }
    CHECK(problems.empty());

    GameData gd(D2K_GAMEDATA_DIR);
    CHECK(gd.find("data/ui_data/menus.uib").has_value()); // case-insensitive
}

TEST_CASE("sha256_hex matches FIPS test vectors") {
    std::string abc = "abc";
    CHECK(d2k::platform::sha256_hex({reinterpret_cast<const uint8_t*>(abc.data()), abc.size()}) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(d2k::platform::sha256_hex({}) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}
