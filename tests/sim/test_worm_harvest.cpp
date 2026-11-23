// Sandworm, harvesting and spice-bloom checks against the 1.06 data
// (docs/re/17, docs/re/13, docs/re/31). Golden tests: they skip without data.
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>
#include <string>

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

namespace {

struct Setup {
    formats::MapFile map;
    formats::MissionFile mis;
    formats::Templates t;
    sim::ExeTables tables;
    sim::Rng rng{1};
    std::vector<uint8_t> vars;
    std::unique_ptr<sim::GameMap> gm;
    std::unique_ptr<sim::World> w;

    Setup(platform::GameData& gd, const std::string& name) {
        map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(
            formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
        auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
        t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
        auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
        vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
        gm = std::make_unique<sim::GameMap>(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        w = std::make_unique<sim::World>(*gm, t, tables, speed, placed, rng, std::span<const uint8_t>{}, vars);
        w->set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w->init_players(mis);
    }
};

} // namespace

TEST_CASE("golden: every campaign mission: worms of side 7 only, no event adds a worm, spice lies on sand (docs/re/17 sections 4.3, 5.1, 6) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    int missions = 0, worms = 0, spice_cells = 0;
    for (const char h : {'A', 'H', 'O'}) {
        for (int n = 1; n <= 9; ++n) {
            for (const char v : {'1', '2', '3'}) {
                const std::string name = std::string(1, h) + std::to_string(n) + "V" + v;
                if (!gd.find("MISSIONS/" + name + ".MAP") || !gd.find("MISSIONS/_" + name + ".MIS")) continue;
                Setup s(gd, name);
                INFO(name);
                ++missions;
                int here = 0;
                for (const auto& u : s.w->units()) {
                    if (s.t.unit_records[size_t(u.record)].kind != 5) continue;
                    ++here;
                    // Side 7 is never a campaign player (0, 1 or 2), so the
                    // box select (own units only, FUN_004a5ae0) never sees one.
                    CHECK(u.owner == 7);
                }
                // FUN_00469790 aborts a mission without a worm.
                CHECK(here > 0);
                worms += here;
                // DEPLOY (0), REINFORCE (1) and ADDUNITS (18) are the only
                // events that create units; none lists a worm record.
                for (const auto& e : s.mis.events) {
                    if (e.type != 0 && e.type != 1 && e.type != 18) continue;
                    for (int k = 0; k < e.count && k < int(e.units.size()); ++k) {
                        const int rec = e.units[size_t(k)];
                        if (rec < int(s.t.unit_records.size())) CHECK(s.t.unit_records[size_t(rec)].kind != 5);
                    }
                }
                // Every spice cell carries the sand bit, so worms reach spice
                // (docs/re/17 section 5.1).
                for (int y = 0; y < s.gm->height(); ++y) {
                    for (int x = 0; x < s.gm->width(); ++x) {
                        const uint32_t f = s.gm->at(x, y).flags;
                        if (!(f & sim::cell::kSpiceLevelMask)) continue;
                        ++spice_cells;
                        CHECK((f & sim::cell::kSand) != 0);
                    }
                }
            }
        }
    }
    MESSAGE(missions << " missions, " << worms << " worms, " << spice_cells << " spice cells");
    CHECK(missions > 0);
}

TEST_CASE("golden: the harvester effects EX_HARV, EX_HARVDUST0-7 and EX_REF have no sound (docs/re/13 section 8.2) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    std::vector<std::string> names = {"EX_HARV", "EX_REF"};
    for (int i = 0; i < 8; ++i) names.push_back("EX_HARVDUST" + std::to_string(i));
    for (const auto& n : names) {
        INFO(n);
        const int e = t.explosion_index(n);
        REQUIRE(e >= 0);
        CHECK(t.explosion_sound[size_t(e)] < 0);
    }
}

TEST_CASE("golden: A1V1 a harvester heading home moves an idle friendly unit off the dock cell (FUN_00497b90 0x499900, docs/re/13 section 1.3) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Setup s(gd, "A1V1");
    auto& w = *s.w;
    auto& t = s.t;
    auto& gm = *s.gm;
    const auto& pl = w.player(0);
    auto place = [&](const char* name) {
        const int rec = t.building_record(t.building_index(name), pl.house);
        REQUIRE(rec >= 0);
        std::vector<sim::PlaceCell> cells;
        for (int y = 1; y < gm.height(); ++y) {
            for (int x = 0; x < gm.width(); ++x) {
                if (w.placement_check(0, rec, x, y, cells)) return w.create_building(0, rec, x, y, false);
            }
        }
        FAIL("no spot for " << name);
        return -1;
    };
    place("WIND_TRAP");
    const int ref = place("REFINERY");
    for (int i = 0; i < 300; ++i) w.tick(); // build-up
    const auto& rb = w.buildings()[size_t(ref)];
    const auto& rr = t.building_records[size_t(rb.record)];
    const int dx = rb.cell_x + rr.spawn_x, dy = rb.cell_y + rr.spawn_y;

    // A free drivable cell 4 to 8 cells from the dock for the harvester.
    int hx = -1, hy = -1;
    for (int y = 0; y < gm.height() && hx < 0; ++y) {
        for (int x = 0; x < gm.width(); ++x) {
            const int d = std::abs(x - dx) + std::abs(y - dy);
            const uint32_t f = gm.at(x, y).flags;
            if (d >= 4 && d <= 8 && (f & sim::cell::kVehicles) && !(f & 0x13f8)) {
                hx = x;
                hy = y;
                break;
            }
        }
    }
    REQUIRE(hx >= 0);

    const int trike = t.unit_record(t.unit_index("TRIKE"), pl.house);
    REQUIRE(trike >= 0);
    const int tp = w.create_unit(0, trike, dx, dy, dx, dy);
    REQUIRE(tp >= 0);
    const int trike_id = w.units()[size_t(tp)].index;
    const int trike_state = w.units()[size_t(tp)].state;
    REQUIRE((trike_state == 2 || trike_state == 3));

    const int harv = t.unit_record(t.unit_index("HARVESTER"), pl.house);
    const int hp = w.create_unit(0, harv, hx, hy, hx, hy);
    REQUIRE(hp >= 0);
    const int harv_id = w.units()[size_t(hp)].index;
    {
        auto& h = w.units()[size_t(hp)];
        h.load = 7;
        h.refinery = w.building_slot(ref);
        h.state = 0xd;
    }
    // One tick: the harvester, still cells away, tells the trike to move
    // aside (state 9); the trike, earlier in the unit list, acts next tick.
    w.tick();
    CHECK(w.units()[size_t(w.find_unit(trike_id))].state == 9);
    // The trike leaves the dock cell and the harvester docks there.
    bool docked = false, trike_left = false;
    for (int i = 0; i < 3000 && !docked; ++i) {
        w.tick();
        const auto& tu = w.units()[size_t(w.find_unit(trike_id))];
        trike_left |= tu.cur_x != dx || tu.cur_y != dy;
        const auto& h = w.units()[size_t(w.find_unit(harv_id))];
        docked = h.state == 0xe || h.state == 0xf;
    }
    CHECK(trike_left);
    CHECK(docked);
}

TEST_CASE("golden: A1V1 the dock cell blocker rule leaves an enemy unit alone (alliance != 0) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Setup s(gd, "A1V1");
    auto& w = *s.w;
    auto& t = s.t;
    auto& gm = *s.gm;
    int enemy = -1;
    for (int p = 1; p < 8 && enemy < 0; ++p) {
        if (s.mis.alliance[0][size_t(p)] != 0 && w.player_active(p)) enemy = p;
    }
    if (enemy < 0) return;
    const auto& pl = w.player(0);
    const int rec = t.building_record(t.building_index("WIND_TRAP"), pl.house);
    std::vector<sim::PlaceCell> cells;
    int ref = -1;
    for (int pass = 0; pass < 2; ++pass) {
        const int r = pass == 0 ? rec : t.building_record(t.building_index("REFINERY"), pl.house);
        bool done = false;
        for (int y = 1; y < gm.height() && !done; ++y) {
            for (int x = 0; x < gm.width() && !done; ++x) {
                if (w.placement_check(0, r, x, y, cells)) {
                    const int b = w.create_building(0, r, x, y, false);
                    if (pass == 1) ref = b;
                    done = true;
                }
            }
        }
        REQUIRE(done);
    }
    for (int i = 0; i < 300; ++i) w.tick();
    const auto& rb = w.buildings()[size_t(ref)];
    const auto& rr = t.building_records[size_t(rb.record)];
    const int dx = rb.cell_x + rr.spawn_x, dy = rb.cell_y + rr.spawn_y;
    const int trike = t.unit_record(t.unit_index("TRIKE"), w.player(enemy).house);
    REQUIRE(trike >= 0);
    const int tp = w.create_unit(enemy, trike, dx, dy, dx, dy);
    REQUIRE(tp >= 0);
    const int trike_id = w.units()[size_t(tp)].index;
    const int harv = t.unit_record(t.unit_index("HARVESTER"), pl.house);
    int hp = -1;
    for (int d = 4; d < 12 && hp < 0; ++d) {
        if (dy + d < gm.height() && (gm.at(dx, dy + d).flags & sim::cell::kVehicles) && !(gm.at(dx, dy + d).flags & 0x13f8))
            hp = w.create_unit(0, harv, dx, dy + d, dx, dy + d);
    }
    if (hp < 0) return;
    {
        auto& h = w.units()[size_t(hp)];
        h.load = 7;
        h.refinery = w.building_slot(ref);
        h.state = 0xd;
    }
    w.tick();
    const int ti = w.find_unit(trike_id);
    if (ti >= 0) CHECK(w.units()[size_t(ti)].state != 9);
}
