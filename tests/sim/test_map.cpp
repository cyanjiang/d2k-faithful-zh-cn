#include <doctest/doctest.h>

#include <cctype>
#include <filesystem>
#include <string>

#include "formats/file_io.hpp"
#include "formats/map.hpp"
#include "formats/pe_image.hpp"
#include "formats/templates.hpp"
#include "platform/game_data.hpp"
#include "sim/game_map.hpp"
#include "sim/rng.hpp"

using namespace d2k;
namespace fs = std::filesystem;

TEST_CASE("Rng is MSVC rand()") {
    sim::Rng r(1);
    CHECK(r.next() == 41); // rand() after srand(1)
    CHECK(r.next() == 18467);
    CHECK(r.next() == 6334);
}

TEST_CASE("golden: every mission map and .MIS parses; A1V1 spice is smoothed (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    int maps = 0, missions = 0;
    for (const auto& f : fs::directory_iterator(root / "MISSIONS")) {
        auto ext = f.path().extension().string();
        if (ext == ".MAP") {
            auto m = formats::parse_map(formats::read_file(f.path()));
            CHECK(m.cells.size() == size_t(m.width) * m.height);
            ++maps;
        } else if (ext == ".MIS") {
            // _TESTH / _TESTO are 74,210-byte leftovers in an older layout
            // that no menu reaches.
            if (f.path().stem().string().rfind("_TEST", 0) == 0) continue;
            auto mi = formats::parse_mission(formats::read_file(f.path()));
            std::string ts = mi.tileset;
            for (char& ch : ts) ch = char(std::toupper(static_cast<unsigned char>(ch)));
            CHECK(ts.rfind("BLOX", 0) == 0);
            ++missions;
        }
    }
    CHECK(maps > 0);
    CHECK(missions > 0);

    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    CHECK(mis.tileset == "BLOXTREE");
    CHECK(mis.credits[0] == 2300);
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    CHECK(tables.spice_edge[255] == 0x319); // surrounded thin spice = the plain tile
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    int thin = 0, thick = 0;
    for (int y = 0; y < gm.height(); ++y) {
        for (int x = 0; x < gm.width(); ++x) {
            int level = gm.spice_level(x, y);
            if (level == 2) {
                ++thin;
                CHECK(gm.at(x, y).tile >= 0x2ee);
                CHECK(gm.at(x, y).tile <= 0x31f);
            } else if (level == 4) {
                ++thick;
            }
        }
    }
    CHECK(thin == 93);  // 92 special-1 cells + 1 bloom spot (0x2d), also level 2
    CHECK(thick == 33); // special 2 cells
}

TEST_CASE("golden: Templates.bin and TILEDATA.BIN (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data")) return;
    platform::GameData gd(root);
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    CHECK(t.unit_name[0] == "LIGHT_INF");
    CHECK(t.unit_index("SANDWORM") == 22);
    CHECK(t.building_name[0] == "CONSTRUCTION_YARD");
    CHECK(t.building_index("SP_OUTPOST") == 23);
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    formats::MapObject o;
    REQUIRE(td.object(8, o)); // A1V1: the Atreides construction yard
    CHECK(o.building);
    CHECK(o.owner == 0);
    CHECK(o.type == 0);
    REQUIRE(td.object(187, o)); // A1V1: Atreides trikes
    CHECK_FALSE(o.building);
    CHECK(t.unit_name[o.type] == "TRIKE");
}
