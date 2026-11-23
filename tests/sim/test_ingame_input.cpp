// In-game input rules that live in the World (docs/re/25 part A): the
// control-group selection and the other sides' deselection. Golden test:
// skips without data.
#include <doctest/doctest.h>

#include <filesystem>
#include <vector>

#include "formats/file_io.hpp"
#include "formats/map.hpp"
#include "formats/pe_image.hpp"
#include "formats/templates.hpp"
#include "platform/game_data.hpp"
#include "sim/game_map.hpp"
#include "sim/objects.hpp"
#include "sim/world.hpp"

using namespace d2k;
namespace fs = std::filesystem;

TEST_CASE("golden: a group key selects the own side only; the others go only with the voice (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    std::vector<size_t> own;
    int other = -1;
    for (size_t i = 0; i < w.units().size(); ++i) {
        const auto& u = w.units()[i];
        if (u.owner == 0) own.push_back(i);
        else if (other < 0 && !(u.flags & 0x140) && u.state != 8 && u.state != 0x11) other = int(i);
    }
    REQUIRE(!own.empty());
    REQUIRE(other >= 0);
    w.select_unit(own[0], false);
    w.assign_group(0, 1);
    // An enemy unit clicked to view it (FUN_004a5a50(owner, idx, 0)).
    w.view_unit(size_t(other));
    CHECK(w.units()[size_t(other)].selected);
    CHECK(!w.units()[own[0]].selected);
    // FUN_0046cb40 touches the own side only (held repeats, Alt + digit).
    CHECK(w.select_group(0, 1, true));
    CHECK(w.units()[own[0]].selected);
    CHECK(w.units()[size_t(other)].selected);
    // The plain digit's voice block (0x4462f7): FUN_0046cb00 / FUN_0046cb20
    // for every other side.
    w.deselect_other_sides(0);
    CHECK(w.units()[own[0]].selected);
    CHECK(!w.units()[size_t(other)].selected);
}
