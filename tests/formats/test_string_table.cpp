#include <doctest/doctest.h>

#include <filesystem>
#include <vector>

#include "formats/byte_reader.hpp"
#include "formats/file_io.hpp"
#include "formats/string_table.hpp"

using namespace d2k::formats;
namespace fs = std::filesystem;

TEST_CASE("StringTable: counted key/value pairs") {
    std::vector<uint8_t> d{2, 0, 0, 0,
                           3, 0, 'a', 'b', 0, 2, 0, 'x', 0,
                           1, 0, 0, 1, 0, 0};
    auto t = StringTable::parse(d);
    CHECK(t.size() == 2);
    CHECK(t.get("ab") == "x");
    CHECK(t.get("") == "");
    CHECK_FALSE(t.get("missing").has_value());
    d.push_back(0);
    CHECK_THROWS_AS(StringTable::parse(d), FormatError);
}

TEST_CASE("golden: text.uib movie keys and all UIL scripts (skips without data)") {
    fs::path ui = fs::path(D2K_GAMEDATA_DIR) / "Data" / "UI_DATA";
    if (!fs::exists(ui)) {
        MESSAGE("skipped: no game data");
        return;
    }
    auto text = StringTable::parse(read_file(ui / "text.uib"));
    CHECK(text.size() == 1322);
    CHECK(text.get("mve_intro1") == "g_int1_e");
    CHECK(text.get("mve_fast") == "t_titl_e");
    // Keys ignore case (_strcmpi in FUN_0048dc10): lo2mis2.uil asks for B_raider.
    REQUIRE(text.get("B_Raider").has_value());
    CHECK(text.get("B_raider") == text.get("B_Raider"));
    auto credits = text.get_all("CreditEntry");
    REQUIRE(credits.size() == 348);
    CHECK(credits.front() == "DUNE 2000");
    CHECK(credits.back() == "");
    size_t uils = 0;
    for (const auto& e : fs::directory_iterator(ui)) {
        auto ext = e.path().extension().string();
        if (ext != ".uil" && ext != ".UIL") continue;
        CAPTURE(e.path().filename().string());
        CHECK_NOTHROW(parse_uil_lines(read_file(e.path())));
        ++uils;
    }
    CHECK(uils == 302);
}
