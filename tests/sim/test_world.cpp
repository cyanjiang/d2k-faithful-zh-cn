#include <set>
#include <map>
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <numeric>

#include "formats/file_io.hpp"
#include "formats/map.hpp"
#include "formats/pe_image.hpp"
#include "formats/templates.hpp"
#include "platform/game_data.hpp"
#include "platform/sha256.hpp"
#include "sim/game_map.hpp"
#include "sim/objects.hpp"
#include "sim/tick_scheduler.hpp"
#include "sim/world.hpp"
#include "sim/x87.hpp"

using namespace d2k;
namespace fs = std::filesystem;

TEST_CASE("TickScheduler: speed buckets are ticks per real second") {
    // Before the first timer: base 100, cap 60 (FUN_0045a7a0).
    struct Case {
        int speed, limit;
    } cases[] = {{0, 8}, {7, 8}, {8, 10}, {30, 12}, {50, 15}, {75, 20}, {90, 25}, {100, 60}};
    for (auto c : cases) {
        sim::TickScheduler s(c.speed);
        CHECK(s.tick_limit() == c.limit);
    }
    // Default speed, first second: every 5th slot (step 100 / 20).
    sim::TickScheduler s(75);
    for (int i = 0; i < 100; ++i) CHECK(s.tick_due(0) == (i % 5 == 0));

    // A steady 60 Hz loop: after the first timer the base is 60 frames, so
    // each bucket gives its own number of ticks every second (the exe at
    // speed 75 measured 19.98 ticks/s at ~61 frames/s).
    struct Rate {
        int speed, per_second;
    } rates[] = {{0, 8}, {30, 12}, {50, 15}, {75, 20}, {90, 25}, {100, 60}};
    for (auto r : rates) {
        sim::TickScheduler t(r.speed);
        int per_second[20] = {};
        for (uint64_t frame = 0; frame < 60 * 20; ++frame) {
            const uint64_t now = frame * 1000 / 60;
            if (t.tick_due(now)) ++per_second[now / 1000];
        }
        CHECK(t.base() == 60);
        CHECK(t.cap() == 60); // set from the base on the 9th second
        for (int sec = 2; sec < 20; ++sec) CHECK(per_second[sec] == r.per_second);
    }
}

TEST_CASE("golden: A1V1 units can be selected and moved (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    sim::World w(gm, t, tables, speed, placed, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));

    // Pick the first own vehicle and send it 3 cells along a free direction.
    size_t idx = w.units().size();
    for (size_t i = 0; i < w.units().size(); ++i) {
        const auto& u = w.units()[i];
        if (u.owner == 0 && !t.unit_records[size_t(u.record)].infantry && w.selectable(u, 0)) {
            idx = i;
            break;
        }
    }
    REQUIRE(idx < w.units().size());
    w.select_unit(idx, false);
    CHECK(w.any_unit_selected(0));
    const auto start = w.units()[idx];
    int tx = -1, ty = -1;
    for (int dy = -3; dy <= 3 && tx < 0; dy += 3) {
        for (int dx = -3; dx <= 3; dx += 3) {
            const int x = start.cur_x + dx, y = start.cur_y + dy;
            if ((dx || dy) && x >= 0 && y >= 0 && x < gm.width() && y < gm.height() &&
                w.passable(x, y, start, 0)) {
                tx = x;
                ty = y;
                break;
            }
        }
    }
    REQUIRE(tx >= 0);
    w.order_move(0, tx, ty);
    for (int i = 0; i < 2000 && (w.units()[idx].cur_x != tx || w.units()[idx].cur_y != ty ||
                                 w.units()[idx].state == sim::state::kMove);
         ++i) {
        w.tick();
    }
    const auto& u = w.units()[idx];
    CHECK(int(u.cur_x) == tx);
    CHECK(int(u.cur_y) == ty);
    CHECK((u.x >> 16) == tx * 32 + 16);
    CHECK((u.y >> 16) == ty * 32 + 16);

    // Box select over the moved unit only; right-click style deselect.
    CHECK(w.box_select(0, tx * 32 + 4, ty * 32 + 4, tx * 32 + 28, ty * 32 + 28));
    CHECK(w.units()[idx].selected);
    w.deselect_all();
    CHECK_FALSE(w.any_unit_selected(0));
}

TEST_CASE("golden: A1V1 sidebar lists, icons and production (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    // Icons sit right after the explosions in DATA.R16 (60x47 images).
    CHECK(t.art.unit_icons == 4922);
    CHECK(t.art.building_icons == 4962);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    sim::World w(gm, t, tables, speed, placed, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    auto& pl = w.player(0);
    CHECK(pl.cash == 2300);
    // Construction yard power -20: (20 + 200) * 100 / 200.
    CHECK(pl.power_percent == 110);
    REQUIRE(pl.building_list.size() == 2);
    CHECK(t.building_name[t.building_records[size_t(pl.building_list[0])].name_index] == "CONCRETE");
    CHECK(t.building_name[t.building_records[size_t(pl.building_list[1])].name_index] == "WIND_TRAP");
    CHECK(pl.unit_list.empty());

    auto money = [&] { return pl.cash + pl.spice + pl.pending_cash + pl.pending_spice; };
    // Wind trap: rate 54 per tick at 100 %, cost 225 -> done after 427 ticks.
    const int trap = pl.building_list[1];
    w.start_building(0, trap);
    for (int i = 0; i < 426; ++i) w.tick();
    CHECK(pl.building_slot.progress == 426 * 54);
    w.tick();
    CHECK(pl.building_slot.progress == sim::kProgressDone);
    CHECK(money() == 2300 - 225);
    // Ready: a right click cancels with a full refund.
    w.hold_or_cancel_building(0, trap);
    CHECK(pl.building_slot.item == -1);
    CHECK(money() == 2300);

    // Concrete: hold, then cancel; the refund is progress * cost / 0x5a00.
    const int concrete = pl.building_list[0];
    w.start_building(0, concrete);
    for (int i = 0; i < 64; ++i) w.tick();
    pl.building_slot.placing = true; // the right press sets +0x2652d first
    w.hold_or_cancel_building(0, trap); // another icon: no effect (FUN_004560f0)
    CHECK(!pl.building_slot.hold);
    w.hold_or_cancel_building(0, concrete);
    CHECK(pl.building_slot.hold);
    CHECK(!pl.building_slot.placing); // hold clears it again
    const int paid = pl.building_slot.paid;
    const int progress = pl.building_slot.progress;
    w.tick();
    CHECK(pl.building_slot.progress == progress); // on hold: no progress
    w.hold_or_cancel_building(0, concrete);
    CHECK(money() == 2300 - paid + progress * 20 / 0x5a00);
    for (int i = 0; i < 200; ++i) w.tick(); // the drip settles pending money into cash
    CHECK(pl.pending_cash == 0);
    CHECK(pl.pending_spice == 0);
}

TEST_CASE("golden: A1V1 placement, build-up and a unit leaving the barracks (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    sim::World w(gm, t, tables, speed, placed, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    auto& pl = w.player(0);

    // The map's construction yard is pre-placed: concrete under its 0x50
    // cells, so full hit points and no decay flag.
    REQUIRE(!w.buildings().empty());
    const auto& yard = w.buildings()[0];
    const auto& yr = t.building_records[size_t(yard.record)];
    CHECK(yr.kind == 2);
    CHECK(yard.hit_points == yr.hit_points);
    CHECK((yard.flags & 0x8000) == 0);
    CHECK((yard.flags & 0x80) != 0); // primary
    CHECK((gm.at(yard.cell_x, yard.cell_y).flags & 0x800) != 0);
    CHECK((gm.at(yard.cell_x, yard.cell_y).flags & 0x10) != 0);

    // Build a wind trap and place it on the first valid spot.
    const int trap = pl.building_list[1];
    w.start_building(0, trap);
    for (int i = 0; i < 427; ++i) w.tick();
    REQUIRE(pl.building_slot.progress == sim::kProgressDone);
    std::vector<sim::PlaceCell> cells;
    int px = -1, py = -1;
    for (int y = 1; y < gm.height() && px < 0; ++y) {
        for (int x = 0; x < gm.width(); ++x) {
            if (w.placement_check(0, trap, x, y, cells)) {
                px = x;
                py = y;
                break;
            }
        }
    }
    REQUIRE(px >= 0);
    // Not valid far away from the base (the near rule).
    CHECK_FALSE(w.placement_check(0, trap, 0, gm.height() - 3, cells));
    const size_t before = w.buildings().size();
    w.place_building(0, trap, px, py);
    REQUIRE(w.buildings().size() == before + 1);
    CHECK(pl.building_slot.item == -1);
    const auto& wt = w.buildings().back();
    const auto& wr = t.building_records[size_t(wt.record)];
    CHECK((wt.flags & 2) != 0); // building up
    // On bare rock: half the hit points plus the concrete share, and decay.
    CHECK(wt.hit_points == wr.hit_points / 2);
    CHECK((wt.flags & 0x8000) != 0);
    const int frames = t.art.anim1_frames[wr.anim1];
    CHECK(frames > 0);
    for (int i = 0; i < frames * 3; ++i) w.tick();
    CHECK((w.buildings().back().flags & 2) == 0);
    // Wind trap power -200 (at half health: 100) raises the percentage.
    CHECK(pl.power_percent > 110);

    // A barracks (created directly) trains a light infantry, which appears
    // at the spawn cell and walks to the first move cell.
    const int barracks = t.building_record(t.building_index("BARRACKS"), pl.house);
    REQUIRE(barracks >= 0);
    int bx = -1, by = -1;
    for (int y = 1; y < gm.height() && bx < 0; ++y) {
        for (int x = 0; x < gm.width(); ++x) {
            if (w.placement_check(0, barracks, x, y, cells)) {
                bx = x;
                by = y;
                break;
            }
        }
    }
    REQUIRE(bx >= 0);
    const int bi = w.create_building(0, barracks, bx, by, false);
    const int inf = t.unit_record(t.unit_index("LIGHT_INF"), pl.house);
    REQUIRE(inf >= 0);
    const size_t units_before = w.units().size();
    w.start_unit(0, inf);
    int sx, sy, mx, my;
    REQUIRE(w.find_factory(inf, 0, true, sx, sy, mx, my) == bi);
    const auto& br = t.building_records[size_t(barracks)];
    CHECK(sx == bx + br.spawn_x);
    CHECK(my == by + br.exit_y);
    for (int i = 0; i < 2000 && w.units().size() == units_before; ++i) w.tick();
    REQUIRE(w.units().size() == units_before + 1);
    for (int i = 0; i < 400; ++i) w.tick();
    const auto& u = w.units().back();
    CHECK(int(u.cur_x) == mx);
    CHECK(int(u.cur_y) == my);
}

TEST_CASE("golden: A1V1 harvester cycle: spice -> refinery -> credits (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    CHECK(t.explosion_index("EX_HARV") >= 0);
    CHECK(t.art.explosion_frames[size_t(t.explosion_index("EX_HARV"))] == 11);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    sim::World w(gm, t, tables, speed, placed, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    auto& pl = w.player(0);

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
    // The refinery needs the wind trap; it is now in the build list.
    bool listed = false;
    for (int rec : pl.building_list) listed |= t.building_name[t.building_records[size_t(rec)].name_index] == "REFINERY";
    CHECK(listed);
    const int ref = place("REFINERY");
    for (int i = 0; i < 300; ++i) w.tick(); // build-up
    CHECK(pl.capacity == 2000);

    // A harvester on the dock cell (as the delivery sets it down).
    int dx = 0, dy = 0;
    const auto& rb = w.buildings()[size_t(ref)];
    const auto& rr = t.building_records[size_t(rb.record)];
    dx = rb.cell_x + rr.spawn_x;
    dy = rb.cell_y + rr.spawn_y;
    const int harv = t.unit_record(t.unit_index("HARVESTER"), pl.house);
    const int pos = w.create_unit(0, harv, dx, dy, dx, dy);
    REQUIRE(pos >= 0);
    const int id = w.units()[size_t(pos)].index;
    CHECK(w.refinery_of(w.units()[size_t(pos)]) == ref);
    CHECK(w.units()[size_t(pos)].state == 0xc);

    const int32_t before = pl.cash + pl.spice + pl.pending_cash + pl.pending_spice;
    int max_load = 0, docked_frames = 0;
    for (int i = 0; i < 20000; ++i) {
        w.tick();
        const int hi = w.find_unit(id);
        REQUIRE(hi >= 0);
        const auto& u = w.units()[size_t(hi)];
        max_load = std::max<int>(max_load, u.load);
        if (u.frame != 0) ++docked_frames;
        if (max_load == 7 && u.load == 0) break; // a full load unloaded
    }
    CHECK(max_load == 7);
    CHECK(docked_frames > 0);
    CHECK(pl.cash + pl.spice + pl.pending_cash + pl.pending_spice >= before + 700);
}

TEST_CASE("angle12 matches the exe's facing conventions") {
    CHECK(sim::facing8(5, 5, 5, 4) == 0);   // north
    CHECK(sim::facing8(5, 5, 6, 5) == 8);   // east
    CHECK(sim::facing8(5, 5, 5, 6) == 16);  // south
    CHECK(sim::facing8(5, 5, 4, 5) == 24);  // west
    CHECK(sim::facing8(5, 5, 6, 6) == 12);  // south-east
}

TEST_CASE("golden: A1V1 new refinery: the carryall brings a free harvester (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    sim::World w(gm, t, tables, speed, placed, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    auto& pl = w.player(0);
    std::vector<sim::PlaceCell> cells;
    auto place = [&](const char* name) {
        const int rec = t.building_record(t.building_index(name), pl.house);
        for (int y = 1; y < gm.height(); ++y) {
            for (int x = 0; x < gm.width(); ++x) {
                if (w.placement_check(0, rec, x, y, cells)) return w.create_building(0, rec, x, y, false);
            }
        }
        return -1;
    };
    REQUIRE(place("WIND_TRAP") >= 0);
    const int ref = place("REFINERY");
    REQUIRE(ref >= 0);
    REQUIRE(w.deliveries(0).size() == 1);
    CHECK(w.deliveries(0)[0].active);

    auto count_kind = [&](int kind) {
        int n = 0;
        for (const auto& u : w.units()) n += t.unit_records[size_t(u.record)].kind == kind && u.owner == 0;
        return n;
    };
    const int harvesters = count_kind(1);
    bool saw_carryall = false;
    int max_alt = 0, min_alt = 1 << 30;
    int i = 0;
    for (; i < 6000; ++i) {
        w.tick();
        for (const auto& u : w.units()) {
            if (t.unit_records[size_t(u.record)].kind != 2) continue;
            saw_carryall = true;
            max_alt = std::max(max_alt, u.altitude >> 16);
            min_alt = std::min(min_alt, u.altitude >> 16);
        }
        if (saw_carryall && count_kind(2) == 0) break; // the carryall left the map
    }
    CHECK(saw_carryall);
    CHECK(max_alt == 60);
    CHECK(min_alt == 10);
    CHECK(count_kind(2) == 0);
    CHECK(count_kind(1) == harvesters + 1);
    CHECK_FALSE(w.deliveries(0)[0].active);
    MESSAGE("carryall delivery done after " << i << " ticks");
}

TEST_CASE("golden: A1V1 shroud: start reveal, edge shapes, unit reveal (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    CHECK(tables.shroud_shape[0xff] == 1);
    CHECK(tables.shroud_shape[0x7f] == 2);
    CHECK(tables.shroud_shape[0] == 0);
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    REQUIRE(circles.size() == 680);
    sim::World w(gm, t, tables, speed, placed, rng, circles);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    w.reveal_start();
    const auto& yard = w.buildings()[0];
    CHECK(w.open(yard.cell_x + 1, yard.cell_y + 1));
    CHECK(gm.at(yard.cell_x + 1, yard.cell_y + 1).radar_known);
    CHECK(w.shrouded(0, 0));                       // far corner
    CHECK(w.shrouded(gm.width() - 1, gm.height() - 1));
    // Every cell is either open, full shroud, or an edge shape with its
    // draw variant (x & 1) | (y & 1) << 1 in bits 4-5.
    int edges = 0;
    for (int y = 0; y < gm.height(); ++y) {
        for (int x = 0; x < gm.width(); ++x) {
            const uint8_t s = gm.at(x, y).state;
            if ((s & 0xf) >= 2) {
                ++edges;
                CHECK((s >> 4) == ((x & 1) | (y & 1) << 1));
            }
            CHECK((s & 0xc0) == 0);
        }
    }
    CHECK(edges > 0);
    // A unit driving into the dark reveals around it (every 8 ticks).
    size_t ui = w.units().size();
    for (size_t i = 0; i < w.units().size(); ++i) {
        if (w.units()[i].owner == 0 && !t.unit_records[size_t(w.units()[i].record)].infantry) {
            ui = i;
            break;
        }
    }
    REQUIRE(ui < w.units().size());
    const int id = w.units()[ui].index;
    w.select_unit(ui, false);
    int tx = 1, ty = 1;
    for (int y = 1; y < gm.height() && tx == 1; ++y)
        for (int x = 1; x < gm.width(); ++x)
            if (w.shrouded(x, y) && (gm.at(x, y).flags & sim::cell::kVehicles) && !(gm.at(x, y).flags & 0x3f8)) {
                tx = x;
                ty = y;
                break;
            }
    w.order_move(0, tx, ty);
    for (int i = 0; i < 3000; ++i) {
        w.tick();
        const auto& u = w.units()[size_t(w.find_unit(id))];
        if (u.cur_x == tx && u.cur_y == ty && u.steps == 0) break;
    }
    for (int i = 0; i < 8; ++i) w.tick();
    CHECK(w.open(tx, ty));
}

TEST_CASE("golden: A1V1 sandworm sleeps, wakes and eats a vehicle on sand (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::World w(gm, t, tables, speed, placed, rng, {}, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    CHECK(w.vars.worm_appetite == 3);
    CHECK(w.vars.worm_initial_sleep == 50);

    int worm = -1;
    for (size_t i = 0; i < w.units().size(); ++i) {
        if (t.unit_records[size_t(w.units()[i].record)].kind == 5) worm = int(i);
    }
    REQUIRE(worm >= 0);
    auto& wu = w.units()[size_t(worm)];
    CHECK(wu.state == 0x1d);
    CHECK(wu.timer == 50);
    CHECK((wu.flags & 0x200000) != 0);
    const int worm_id = wu.index;
    wu.timer = 0; // wake it now instead of after 5051 ticks

    // Park a trike on a sand cell.
    int trike = -1;
    for (size_t i = 0; i < w.units().size(); ++i) {
        const auto& u = w.units()[i];
        if (u.owner == 0 && t.unit_name[t.unit_records[size_t(u.record)].name_index] == "TRIKE") trike = int(i);
    }
    REQUIRE(trike >= 0);
    const int trike_id = w.units()[size_t(trike)].index;
    int sx = -1, sy = -1;
    const auto& tu = w.units()[size_t(trike)];
    int best = 1 << 30;
    for (int y = 0; y < gm.height(); ++y)
        for (int x = 0; x < gm.width(); ++x) {
            const uint32_t f = gm.at(x, y).flags;
            if (!(f & sim::cell::kSand) || !(f & sim::cell::kVehicles) || (f & 0x3f8)) continue;
            const int d = std::abs(x - tu.cur_x) + std::abs(y - tu.cur_y);
            if (d < best) {
                best = d;
                sx = x;
                sy = y;
            }
        }
    REQUIRE(sx >= 0);
    w.select_unit(size_t(trike), false);
    w.order_move(0, sx, sy);
    int i = 0;
    bool woke = false, surfaced_effects = false;
    for (; i < 40000 && w.find_unit(trike_id) >= 0; ++i) {
        w.tick();
        const int wi = w.find_unit(worm_id);
        REQUIRE(wi >= 0);
        woke |= w.units()[size_t(wi)].state != 0x1d;
        surfaced_effects |= !w.particles.empty();
    }
    CHECK(woke);
    CHECK(surfaced_effects);
    CHECK(w.find_unit(trike_id) < 0); // eaten
    CHECK(w.units()[size_t(w.find_unit(worm_id))].load == 1);
    MESSAGE("trike eaten after " << i << " ticks");
}

TEST_CASE("golden: A1V1 combat: ARMOUR.BIN, bullets, an attack order kills the target (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    CHECK(mis.alliance[0][1] == 1);
    CHECK(mis.alliance[0][0] == 0);
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    // Bullet table spot checks (doc 18 section 1.3).
    REQUIRE(t.bullets.size() == 34);
    CHECK(t.bullet_name[0] == "LMG");
    CHECK(t.bullets[0].damage == 125);
    CHECK(t.bullets[0].range == 80);
    CHECK(t.bullets[3].damage == 180); // HMG
    CHECK(t.art.bullet_first[0] == 3912);
    const auto armour = formats::ArmourTable::parse(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    REQUIRE(armour.warheads.size() == 14);
    CHECK(armour.warheads[5].percent[0] == 100); // W_BULLET vs NONE
    CHECK(armour.warheads[5].percent[3] == 75);  // vs WOOD
    CHECK(armour.warheads[5].radius == 15);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    sim::World w(gm, t, tables, speed, placed, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);

    int trike = -1, enemy = -1;
    for (size_t i = 0; i < w.units().size(); ++i) {
        const auto& u = w.units()[i];
        const auto& name = t.unit_name[t.unit_records[size_t(u.record)].name_index];
        if (u.owner == 0 && name == "TRIKE" && trike < 0) trike = int(i);
        if (u.owner == 1 && enemy < 0 && t.unit_records[size_t(u.record)].kind != 5) enemy = int(i);
    }
    REQUIRE(trike >= 0);
    REQUIRE(enemy >= 0);
    CHECK(w.units()[size_t(enemy)].state == 3); // AI units guard their area
    const int enemy_id = w.units()[size_t(enemy)].index;
    w.select_unit(size_t(trike), false);
    w.order_attack_unit(0, enemy_id);
    // HMG bullets hit in the tick they are fired (speed 351 px/tick), so
    // watch the target's hit points instead.
    const int32_t full = w.units()[size_t(enemy)].hit_points;
    bool damaged = false;
    int i = 0;
    for (; i < 10000 && w.find_unit(enemy_id) >= 0; ++i) {
        w.tick();
        const int e = w.find_unit(enemy_id);
        if (e >= 0 && w.units()[size_t(e)].hit_points < full) damaged = true;
    }
    CHECK(damaged);
    CHECK(w.find_unit(enemy_id) < 0);
    MESSAGE("target destroyed after " << i << " ticks");
}

TEST_CASE("golden: A1V1 mission script: hint message, win at 2500 credits, loss without base (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    REQUIRE(mis.conditions.size() == 16);
    REQUIRE(mis.events.size() == 13);
    CHECK(mis.timer == -1);
    CHECK(mis.events[5].type == 17);       // SHOWMESSAGE
    CHECK(mis.events[5].message == 596);   // M1Intro1
    CHECK(mis.conditions[13].type == 8);   // HARVESTED
    CHECK(mis.conditions[13].c == 2500);
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);

    {
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        sim::World w(gm, t, tables, speed, placed, rng);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.init_players(mis);
        w.set_script(mis);
        for (int i = 0; i < 99; ++i) w.tick();
        CHECK(w.messages.empty());
        w.tick(); // TIMER = 100
        REQUIRE(w.messages.size() == 1);
        CHECK(w.messages[0].text == 596);
        CHECK_FALSE(w.won);
        w.player(0).cash = 2600;
        w.tick();
        CHECK(w.won);
        CHECK_FALSE(w.lost);
    }
    {
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        sim::World w(gm, t, tables, speed, placed, rng);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.init_players(mis);
        w.set_script(mis);
        w.tick();
        CHECK_FALSE(w.lost);
        for (const auto& b : w.buildings()) {
            if (b.owner == 0) w.destroy_building(b.index); // the construction yard (no refinery yet)
        }
        for (int i = 0; i < 40 && !w.lost; ++i) w.tick();
        CHECK(w.lost);
        CHECK_FALSE(w.won);
    }
}

TEST_CASE("golden: A1V1 a crusher crushes enemy infantry in every sub-slot (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    int inf_rec = -1, crusher_rec = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        const auto& r = t.unit_records[i];
        if (r.infantry && r.kind == 0 && inf_rec < 0) inf_rec = int(i);
        if (!r.infantry && r.crusher && r.kind == 0 && crusher_rec < 0) crusher_rec = int(i);
    }
    REQUIRE(inf_rec >= 0);
    REQUIRE(crusher_rec >= 0);
    // Directions the crusher drives through the cell: E, W, S, N, SE.
    const int dirs[5][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}};
    for (auto d : dirs) {
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        sim::World w(gm, t, tables, speed, {}, rng);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.init_players(mis);
        // An open drivable 9x9 area.
        int cx = -1, cy = -1;
        for (int y = 4; y < w.map().height() - 4 && cx < 0; ++y) {
            for (int x = 4; x < w.map().width() - 4 && cx < 0; ++x) {
                bool ok = true;
                for (int dy = -4; dy <= 4; ++dy) {
                    for (int dx = -4; dx <= 4; ++dx) {
                        const uint32_t f = w.map().at(x + dx, y + dy).flags;
                        if (!(f & sim::cell::kVehicles) || !(f & sim::cell::kInfantry) || (f & 0x3f8)) ok = false;
                    }
                }
                if (ok) {
                    cx = x;
                    cy = y;
                }
            }
        }
        REQUIRE(cx >= 0);
        std::vector<int> ids;
        for (int s = 0; s < 5; ++s) {
            const int pos = w.create_unit(1, inf_rec, cx, cy, cx, cy);
            REQUIRE(pos >= 0);
            ids.push_back(w.units()[size_t(pos)].index);
        }
        const int pos = w.create_unit(0, crusher_rec, cx - 3 * d[0], cy - 3 * d[1], cx - 3 * d[0], cy - 3 * d[1]);
        REQUIRE(pos >= 0);
        w.select_unit(size_t(pos), false);
        w.order_move(0, cx + 3 * d[0], cy + 3 * d[1]);
        for (int i = 0; i < 600; ++i) {
            w.tick();
            bool all = true;
            for (int id : ids) all = all && w.find_unit(id) >= 0 && w.units()[size_t(w.find_unit(id))].state == 0x11;
            if (all) break;
        }
        for (int s = 0; s < 5; ++s) {
            const int ui = w.find_unit(ids[size_t(s)]);
            INFO("direction " << d[0] << "," << d[1] << " slot " << s);
            REQUIRE(ui >= 0); // still in its removal countdown (0x32 ticks)
            CHECK(w.units()[size_t(ui)].state == 0x11);
            CHECK(w.units()[size_t(ui)].anim_mode == 13); // the crush animation
        }
        // FUN_0049f8f0's counters (docs/re/60): side 1 loses five soldiers;
        // side 0 gets five kills, indexed by its own side (the exe's quirk).
        CHECK(w.player(1).units_lost == 5);
        CHECK(w.player(1).units_lost_by_record[size_t(inf_rec)] == 5);
        CHECK(w.player(0).units_killed == 5);
        CHECK(w.player(0).units_killed_by_record[size_t(inf_rec) * 8 + 0] == 5);
        CHECK(w.player(0).units_killed_by_record[size_t(inf_rec) * 8 + 1] == 0);
    }
}

TEST_CASE("golden: A1V1 a unit blocked on its route by an idle unit still arrives (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    int veh_rec = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        const auto& r = t.unit_records[i];
        if (!r.infantry && r.kind == 0 && r.weapon != 0xff && veh_rec < 0) veh_rec = int(i);
    }
    REQUIRE(veh_rec >= 0);
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    sim::World w(gm, t, tables, speed, {}, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    int cx = -1, cy = -1;
    for (int y = 4; y < w.map().height() - 4 && cx < 0; ++y) {
        for (int x = 5; x < w.map().width() - 5 && cx < 0; ++x) {
            bool ok = true;
            for (int dy = -3; dy <= 3; ++dy) {
                for (int dx = -5; dx <= 5; ++dx) {
                    const uint32_t f = w.map().at(x + dx, y + dy).flags;
                    if (!(f & sim::cell::kVehicles) || (f & 0x3f8)) ok = false;
                }
            }
            if (ok) {
                cx = x;
                cy = y;
            }
        }
    }
    REQUIRE(cx >= 0);
    const int pos = w.create_unit(0, veh_rec, cx - 4, cy, cx - 4, cy);
    REQUIRE(pos >= 0);
    const int id = w.units()[size_t(pos)].index;
    w.select_unit(size_t(pos), false);
    w.order_move(0, cx + 4, cy);
    w.tick(); // the route is planned through (cx, cy)
    REQUIRE(w.create_unit(0, veh_rec, cx, cy, cx, cy) >= 0); // then an idle unit appears on it
    int i = 0;
    for (; i < 3000; ++i) {
        w.tick();
        const auto& u = w.units()[size_t(w.find_unit(id))];
        if (u.cur_x == cx + 4 && u.cur_y == cy && u.steps == 0) break;
    }
    CHECK(i < 3000);
    MESSAGE("arrived after " << i << " ticks");
}


TEST_CASE("golden: A1V1 repair and sell (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
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
    int yard = -1;
    for (const auto& b : w.buildings()) {
        if (b.owner == 0 && t.building_records[size_t(b.record)].kind == 2) yard = b.index;
    }
    REQUIRE(yard >= 0);
    const int32_t max = t.building_records[size_t(w.buildings()[size_t(yard)].record)].hit_points;
    auto& pl = w.player(0);

    // Repair: 12 hp every 8 ticks while money lasts.
    const_cast<sim::Building&>(w.buildings()[size_t(yard)]).hit_points = max / 2;
    REQUIRE(w.can_repair_building(0, yard));
    const int32_t money = pl.cash + pl.spice;
    w.order_repair_building(0, yard);
    for (int i = 0; i < 80; ++i) w.tick();
    const int32_t hp = w.buildings()[size_t(yard)].hit_points;
    CHECK(hp == max / 2 + 12 * 10);
    CHECK(pl.cash + pl.spice < money);
    w.order_repair_building(0, yard); // a second click stops it
    for (int i = 0; i < 16; ++i) w.tick();
    CHECK(w.buildings()[size_t(yard)].hit_points == hp);

    // Sell: the build-up runs backwards, survivors walk out, half the price
    // goes to pending cash when it is removed.
    const size_t units_before = w.units().size();
    const int32_t cost = w.building_cost(0, w.buildings()[size_t(yard)].record);
    const int32_t pend = pl.pending_cash;
    REQUIRE(w.can_sell_building(0, yard));
    w.order_sell(0, yard);
    int i = 0;
    for (; i < 1000 && !w.buildings()[size_t(yard)].removed; ++i) w.tick();
    CHECK(w.buildings()[size_t(yard)].removed);
    CHECK(w.units().size() > units_before);
    CHECK(pl.pending_cash + pl.cash >= pend + cost / 2 - 1000); // pending cash drips into cash
    MESSAGE("sold after " << i << " ticks, " << (w.units().size() - units_before) << " survivors");
    // FUN_0044df60: the footprint is buildable again.
    {
        const auto& yb = w.buildings()[size_t(yard)];
        const auto& yr = t.building_records[size_t(yb.record)];
        for (int k = 0; k < 16; ++k) {
            if (!(yr.footprint & (1u << k))) continue;
            const uint32_t f = w.map().at(yb.cell_x + (k & 3), yb.cell_y + (k >> 2)).flags;
            CHECK((f & sim::cell::kBuildable));
            CHECK(!(f & 0x10));
        }
    }
}

TEST_CASE("golden: A1V2 the Harkonnen AI activates and builds units (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V2.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V2.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    REQUIRE(mis.ai[1].size() == 0x1DB8);
    CHECK(mis.ai[1][1] == 1); // slot 1 active (doc 22 section 1.4)
    CHECK(mis.ai[0][1] == 0);
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    w.set_script(mis);
    w.set_ai(mis);
    CHECK(w.ai_u32(1, 0x1D2C) == 1000); // UnitBuildRate
    const uint32_t first_attack = w.ai_u32(1, 0xF18);
    CHECK(first_attack >= 3000);          // 4000 +-25 %
    CHECK(first_attack <= 5000);
    auto count = [&] {
        int n = 0;
        for (const auto& u : w.units()) n += u.owner == 1 && !u.remove;
        return n;
    };
    const int before = count();
    // Map units guard (group 1); the units dropped at 3500 join the base
    // group and the timed building attack sends them (doc 23 section 7).
    for (const auto& u : w.units()) {
        if (u.owner == 1) CHECK(u.ai_group == 1);
    }
    // The pair dropped at 3500 (deploy action ATTACK) rampages as soon as
    // the carryall is done and they have stopped; map units keep guarding.
    int attack_tick = -1;
    for (int i = 0; i < 6000 && attack_tick < 0; ++i) {
        w.tick();
        for (const auto& u : w.units()) {
            if (u.owner == 1 && u.ai_group != 1 && (u.state == 4 || u.state == 5)) attack_tick = i;
        }
    }
    for (int i = 0; i < 3000; ++i) w.tick();
    for (const auto& u : w.units()) {
        if (u.owner == 1 && u.index < 18) CHECK(u.ai_group == 1); // the map units
    }
    MESSAGE("AI units " << before << " -> " << count() << ", dropped pair attacks at tick " << attack_tick);
    CHECK(attack_tick > 3500);
    CHECK(attack_tick < 3900);
}

TEST_CASE("x87: the AI's unsigned percentage quirk (docs/re/23 section 4.1)") {
    const int offs[] = {25, 15, 1, 0, -1, -5, -15, -24};
    const int want[] = {6249, 5749, 5049, 5000, 150, -50, -550, -1000};
    for (int k = 0; k < 8; ++k) CHECK(sim::x87::percent_of(offs[k], 5000) == want[k]);
}


TEST_CASE("golden: A2V1 units attacking a building damage it (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A2V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A2V1.MIS").value()));
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
    // Two trikes and a light infantry next to the Harkonnen barracks, ordered to attack it.
    int target = -1;
    for (const auto& b : w.buildings()) {
        if (b.owner == 1 && target < 0) target = b.index;
    }
    REQUIRE(target >= 0);
    const auto& tb = w.buildings()[size_t(target)];
    MESSAGE("target " << t.building_name[t.building_records[size_t(tb.record)].name_index] << " at " << tb.cell_x << "," << tb.cell_y << " hp " << tb.hit_points);
    int trike = -1, inf = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        const auto& n = t.unit_name[t.unit_records[i].name_index];
        if (n == "TRIKE" && trike < 0) trike = int(i);
        if (n == "LIGHT_INF" && inf < 0) inf = int(i);
    }
    for (int k = 0; k < 3; ++k) {
        const int pos = w.create_unit(0, k == 2 ? inf : trike, tb.cell_x - 2 + k, tb.cell_y - 2, tb.cell_x - 2 + k, tb.cell_y - 2);
        if (pos >= 0) w.select_unit(size_t(pos), true);
    }
    w.order_attack_building(0, target);
    for (int i = 0; i < 600; ++i) w.tick();
    MESSAGE("barracks hp after 600 ticks: " << w.buildings()[size_t(target)].hit_points);
    CHECK(w.buildings()[size_t(target)].hit_points < 3200); // FUN_0049d170 closest cell
}

TEST_CASE("golden: A1V1 a second unit leaves the factory past the first one (skips without data)") {
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
    auto& pl = w.player(0);
    const int rec = t.building_record(t.building_index("LIGHT_FACTORY"), pl.house);
    REQUIRE(rec >= 0);
    std::vector<sim::PlaceCell> cells;
    int fb = -1;
    for (int y = 1; y < gm.height() && fb < 0; ++y)
        for (int x = 0; x < gm.width() && fb < 0; ++x)
            if (w.placement_check(0, rec, x, y, cells)) fb = w.create_building(0, rec, x, y, false);
    REQUIRE(fb >= 0);
    for (int i = 0; i < 300; ++i) w.tick(); // build-up
    int trike = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i)
        if (t.unit_name[t.unit_records[i].name_index] == "TRIKE" && trike < 0) trike = int(i);
    int sx, sy, mx, my;
    REQUIRE(w.find_factory(trike, 0, true, sx, sy, mx, my) >= 0);
    const int a = w.units()[size_t(w.create_unit(0, trike, sx, sy, mx, my))].index;
    for (int i = 0; i < 400; ++i) w.tick();
    const auto& ua = w.units()[size_t(w.find_unit(a))];
    MESSAGE("first trike at " << int(ua.cur_x) << "," << int(ua.cur_y) << " exit " << mx << "," << my << " state " << ua.state);
    REQUIRE(w.find_factory(trike, 0, true, sx, sy, mx, my) >= 0);
    const int b = w.units()[size_t(w.create_unit(0, trike, sx, sy, mx, my))].index;
    for (int i = 0; i < 600; ++i) w.tick();
    const auto& ub = w.units()[size_t(w.find_unit(b))];
    const auto& ua2 = w.units()[size_t(w.find_unit(a))];
    MESSAGE("second trike at " << int(ub.cur_x) << "," << int(ub.cur_y) << " state " << ub.state << "; first now at "
                               << int(ua2.cur_x) << "," << int(ua2.cur_y));
    CHECK((ub.cur_x == mx && ub.cur_y == my)); // reached the exit cell
    CHECK(ub.state == 2);
    CHECK(!(ua2.cur_x == mx && ua2.cur_y == my)); // the first one moved aside
}

TEST_CASE("golden: A1V1 HMG hits raise the crater counter and break concrete (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    sim::World w(gm, t, tables, speed, {}, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    int cx = -1, cy = -1;
    for (int y = 0; y < w.map().height() && cx < 0; ++y)
        for (int x = 0; x < w.map().width() && cx < 0; ++x) {
            const uint32_t f = w.map().at(x, y).flags;
            if ((f & sim::cell::kInfantry) && (f & sim::cell::kBuildable) && !(f & sim::cell::kSpiceLevelMask) && !(f & 0x810)) { cx = x; cy = y; }
        }
    REQUIRE(cx >= 0);
    for (int k = 0; k < 3; ++k) w.damage_at(cx * 32 + 16, cy * 32 + 16, 0, 3, 1, -1); // HMG
    CHECK(w.map().at(cx, cy).marks == 12);
    const_cast<sim::Cell&>(w.map().at(cx, cy)).flags |= 0x800; // concrete
    for (int k = 0; k < 2; ++k) w.damage_at(cx * 32 + 16, cy * 32 + 16, 0, 3, 1, -1);
    CHECK(w.map().at(cx, cy).marks == 20);
    CHECK(!(w.map().at(cx, cy).flags & 0x800));
    const int tile = w.map().at(cx, cy).tile;
    CHECK(((tile >= 552 && tile <= 556) || (tile >= 572 && tile <= 574)));
}

TEST_CASE("golden: A1V1 control groups assign and select (skips without data)") {
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
    for (size_t i = 0; i < w.units().size(); ++i) if (w.units()[i].owner == 0) own.push_back(i);
    REQUIRE(own.size() >= 3);
    w.select_unit(own[0], false);
    w.select_unit(own[1], true);
    w.assign_group(0, 1);           // Ctrl + 1
    w.select_unit(own[2], false);   // something else
    CHECK(w.select_group(0, 1, true)); // 1
    CHECK(w.units()[own[0]].selected);
    CHECK(w.units()[own[1]].selected);
    CHECK(!w.units()[own[2]].selected);
    w.select_unit(own[2], false);
    w.assign_group(0, 1);           // reassign: the old members leave
    CHECK(w.units()[own[0]].ai_group == 0xff);
    CHECK(w.units()[own[2]].ai_group == 1);
    CHECK(!w.select_group(0, 5, true)); // an empty group changes nothing
    CHECK(w.units()[own[2]].selected);
}

TEST_CASE("golden: A2V1 the AI goes BESERK when its base is destroyed (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A2V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A2V1.MIS").value()));
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
    w.set_script(mis);
    w.set_ai(mis);
    for (int i = 0; i < 50; ++i) w.tick();
    for (const auto& b : w.buildings()) if (b.owner == 1) w.destroy_building(b.index);
    int attackers = 0, ungrouped = 0;
    for (int i = 0; i < 1000; ++i) w.tick();
    for (const auto& u : w.units()) {
        if (u.owner != 1 || u.remove) continue;
        if (u.ai_group == 0xff) ++ungrouped;
        if (u.state == 4 || u.state == 5 || u.state == 7) ++attackers;
    }
    MESSAGE("beserk " << int(w.ai_u8(1, 0x1E)) << ", attacking / moving " << attackers << ", ungrouped " << ungrouped);
    CHECK(w.ai_u8(1, 0x1E) == 1);   // BESERK (event 4 on BASEDESTROYED side 1)
    CHECK(ungrouped == 0);          // survivors join a rampaging group
    CHECK(attackers > 0);
}

TEST_CASE("golden: A2V1 a destroyed building throws bouncing debris; a second factory can be primary (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A2V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A2V1.MIS").value()));
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

    // Debris: one bullet per death explosion (2 x occupied cells), flag 0x400.
    int target = -1;
    for (const auto& b : w.buildings()) {
        if (b.owner == 1 && target < 0 && t.building_records[size_t(b.record)].death_explosion >= 0) target = b.index;
    }
    REQUIRE(target >= 0);
    const auto& r = t.building_records[size_t(w.buildings()[size_t(target)].record)];
    int cells = 0;
    for (int k = 0; k < 16; ++k) cells += (r.footprint >> k) & 1;
    w.destroy_building(target);
    int debris = 0;
    for (const auto& b : w.bullets) debris += (b.flags & 0x500) == 0x500;
    MESSAGE("cells " << cells << ", debris " << debris << ", shake " << w.shake_ticks);
    CHECK(debris == 2 * cells);
    int max_height = 0;
    for (int i = 0; i < 200 && !w.bullets.empty(); ++i) {
        w.tick();
        for (const auto& b : w.bullets) max_height = std::max(max_height, int(b.height >> 16));
    }
    MESSAGE("debris peak height " << max_height << " px, left " << w.bullets.size());
    CHECK(max_height > 4); // thrown up, then falls
    CHECK(w.bullets.empty());

    // Primary: one barracks cannot be primary; a second one can, and takes the slot.
    int rec = -1;
    for (size_t i = 0; i < t.building_records.size(); ++i) {
        if (t.building_records[i].kind == 11 && (t.building_records[i].house_mask & 1) && rec < 0) rec = int(i);
    }
    REQUIRE(rec >= 0);
    std::vector<sim::PlaceCell> pc;
    auto place = [&]() {
        for (int y = 0; y < gm.height(); ++y)
            for (int x = 0; x < gm.width(); ++x)
                if (w.placement_check(0, rec, x, y, pc)) return w.create_building(0, rec, x, y, true);
        return -1;
    };
    int first = -1;
    for (const auto& b : w.buildings()) {
        if (b.owner == 0 && b.record == rec) first = b.index;
    }
    if (first < 0) first = place();
    REQUIRE(first >= 0);
    CHECK_FALSE(w.can_be_primary(0, first));
    const int second = place();
    REQUIRE(second >= 0);
    CHECK(w.player(0).primary[11] == first);
    CHECK(w.can_be_primary(0, second));
    w.select_building(size_t(second));
    w.order_primary(0);
    CHECK(w.player(0).primary[11] == second);
    CHECK((w.buildings()[size_t(second)].flags & 0x80) != 0);
    CHECK((w.buildings()[size_t(first)].flags & 0x80) == 0);
}

TEST_CASE("golden: A1V1 infantry entering a cell with a stale enemy owner takes the owner bits (skips without data)") {
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
    int inf = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        if (t.unit_name[t.unit_records[i].name_index] == "LIGHT_INF" && inf < 0) inf = int(i);
    }
    REQUIRE(inf >= 0);
    // An empty walkable cell next to the player's first unit, left with owner bits 1.
    const auto& first = *std::find_if(w.units().begin(), w.units().end(), [](const sim::Unit& u) { return u.owner == 0; });
    int sx = -1, sy = -1;
    for (int y = first.cur_y - 4; y <= first.cur_y + 4 && sx < 0; ++y) {
        for (int x = first.cur_x - 4; x <= first.cur_x + 4 && sx < 0; ++x) {
            if (x < 1 || y < 0 || x >= gm.width() || y >= gm.height()) continue;
            const uint32_t f = gm.at(x, y).flags, g = gm.at(x - 1, y).flags;
            if ((f & sim::cell::kInfantry) && !(f & 0x3f8) && !(f & 0x10) && (g & sim::cell::kInfantry) && !(g & 0x3f8) && !(g & 0x10)) {
                sx = x;
                sy = y;
            }
        }
    }
    REQUIRE(sx >= 0);
    gm.at(sx, sy).flags = (gm.at(sx, sy).flags & ~7u) | 1u; // stale: a side 1 unit passed here
    const int pos = w.create_unit(0, inf, sx - 1, sy, sx - 1, sy);
    REQUIRE(pos >= 0);
    w.select_unit(size_t(pos), false);
    w.order_move(0, sx, sy);
    for (int i = 0; i < 200; ++i) w.tick();
    const uint32_t f = gm.at(sx, sy).flags;
    MESSAGE("cell (" << sx << "," << sy << ") flags " << std::hex << f);
    CHECK((f & 0x3e0) != 0);  // the infantry is in it
    CHECK((f & 7) == 0);      // and the cell is ours again
}

TEST_CASE("golden: A2V2 the AI rebuilds a destroyed starting building on its old cell (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A2V2.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A2V2.MIS").value()));
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
    w.set_script(mis);
    w.set_ai(mis);
    int side = -1;
    for (int s = 0; s < 8; ++s) {
        if (w.ai_u8(s, 0x01) && w.ai_u8(s, 0x1D45)) side = s;
    }
    MESSAGE("rebuilding AI side " << side << ", initial buildings " << (side >= 0 ? int(w.ai_u8(side, 0xF0C)) : 0));
    REQUIRE(side >= 0);
    // A starting building of that side other than the yard (the rebuild needs its build list).
    int victim = -1;
    for (const auto& b : w.buildings()) {
        const auto& r = t.building_records[size_t(b.record)];
        if (b.owner == side && !b.removed && r.kind != 2 && r.kind != 0xe && r.kind != 0xf && victim < 0) victim = b.index;
    }
    REQUIRE(victim >= 0);
    const int rec = w.buildings()[size_t(victim)].record, vx = w.buildings()[size_t(victim)].cell_x,
              vy = w.buildings()[size_t(victim)].cell_y;
    w.player(side).cash += 20000; // enough money above the stash
    w.destroy_building(victim);
    int found = -1, tick_found = -1;
    for (int i = 0; i < 6000 && found < 0; ++i) {
        w.tick();
        for (const auto& b : w.buildings()) {
            if (b.owner == side && !b.removed && b.record == rec && b.index != victim) {
                found = b.index;
                tick_found = i;
            }
        }
    }
    REQUIRE(found >= 0);
    const auto& nb = w.buildings()[size_t(found)];
    MESSAGE("rebuilt " << t.building_name[t.building_records[size_t(rec)].name_index] << " at " << nb.cell_x << ","
                       << nb.cell_y << " (was " << vx << "," << vy << ") after " << tick_found << " ticks");
    CHECK(nb.cell_x == vx);
    CHECK(nb.cell_y == vy);
}

TEST_CASE("golden: A2V1 a saved state loaded into a fresh mission continues identically (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A2V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A2V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto armour = formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value());
    struct Game {
        sim::Rng rng;
        sim::GameMap gm;
        sim::World w;
        Game(uint32_t seed, const formats::MapFile& map, const formats::TileAttributes& attr, const sim::ExeTables& tables,
             const formats::TileData& td, const formats::Templates& t, const sim::SpeedTable& speed,
             const formats::MissionFile& mis, const std::vector<uint8_t>& armour)
            : rng(seed), gm(map, attr, tables, rng),
              w(gm, t, tables, speed, sim::place_map_objects(map, td, t, mis.house, tables), rng) {
            w.set_armour(armour);
            w.init_players(mis);
            w.set_script(mis);
            w.set_ai(mis);
        }
    };
    Game a(7, map, attr, tables, td, t, speed, mis, armour);
    // Some player activity: the player's units attack the nearest enemy building.
    for (int i = 0; i < 1500; ++i) a.w.tick();
    int target = -1;
    for (const auto& b : a.w.buildings()) if (b.owner == 1 && !b.removed && target < 0) target = b.index;
    for (size_t i = 0; i < a.w.units().size(); ++i) {
        if (a.w.units()[i].owner == 0) a.w.select_unit(i, true);
    }
    if (target >= 0) a.w.order_attack_building(0, target);
    for (int i = 0; i < 1500; ++i) a.w.tick();
    const auto saved = a.w.save_state();
    MESSAGE("state " << saved.size() << " bytes at tick " << a.w.tick_count() << ", units " << a.w.units().size());

    Game b(12345, map, attr, tables, td, t, speed, mis, armour); // another seed: everything must come from the save
    REQUIRE(b.w.load_state(saved));
    CHECK(b.w.save_state() == saved); // round trip
    for (int i = 0; i < 3000; ++i) {
        a.w.tick();
        b.w.tick();
    }
    const auto sa = a.w.save_state(), sb = b.w.save_state();
    size_t diff = 0;
    while (diff < std::min(sa.size(), sb.size()) && sa[diff] == sb[diff]) ++diff;
    MESSAGE("after 3000 more ticks: " << sa.size() << " / " << sb.size() << " bytes, first difference at " << diff);
    CHECK(sa == sb);
}

TEST_CASE("golden: A1V1 a vehicle drives round a building to the cell behind it (skips without data)") {
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
    int trike = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        if (t.unit_name[t.unit_records[i].name_index] == "TRIKE" && trike < 0) trike = int(i);
    }
    REQUIRE(trike >= 0);
    // The player's construction yard: start under its middle column, goal above it.
    const sim::Building* yard = nullptr;
    for (const auto& b : w.buildings()) {
        if (b.owner == 0 && t.building_records[size_t(b.record)].kind == 2) yard = &b;
    }
    REQUIRE(yard);
    const auto& r = t.building_records[size_t(yard->record)];
    int rows = 0;
    for (int k = 0; k < 16; ++k) if (r.footprint & (1u << k)) rows = std::max(rows, k / 4 + 1);
    const int x = yard->cell_x + 1, gy = yard->cell_y - 1, sy = yard->cell_y + rows;
    MESSAGE("yard at " << yard->cell_x << "," << yard->cell_y << " rows " << rows << "; from (" << x << "," << sy
                       << ") to (" << x << "," << gy << ")");
    const int pos = w.create_unit(0, trike, x, sy, x, sy);
    REQUIRE(pos >= 0);
    const int id = w.units()[size_t(pos)].index;
    w.deselect_all();
    w.select_unit(size_t(pos), false);
    w.order_move(0, x, gy);
    int arrived = -1;
    for (int i = 0; i < 1500 && arrived < 0; ++i) {
        w.tick();
        const auto& u = w.units()[size_t(w.find_unit(id))];
        if (u.cur_x == x && u.cur_y == gy) arrived = i;
    }
    const auto& u = w.units()[size_t(w.find_unit(id))];
    MESSAGE("arrived after " << arrived << " ticks; now at " << int(u.cur_x) << "," << int(u.cur_y));
    CHECK(arrived >= 0);
}

TEST_CASE("golden: A1V1 walls take their frame from the wall neighbours (skips without data)") {
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
    int wall = -1;
    for (size_t i = 0; i < t.building_records.size(); ++i) {
        if (t.building_records[i].kind == 0xe && (t.building_records[i].house_mask & 1) && wall < 0) wall = int(i);
    }
    REQUIRE(wall >= 0);
    // Three free buildable cells in a row.
    int wx = -1, wy = -1;
    for (int y = 1; y < gm.height() - 1 && wx < 0; ++y) {
        for (int x = 1; x + 3 < gm.width() && wx < 0; ++x) {
            bool ok = true;
            for (int k = 0; k < 3; ++k) {
                const uint32_t f = gm.at(x + k, y).flags;
                ok = ok && (f & 0x8000) && !(f & 0x3f8) && !(f & 0x400);
            }
            if (ok) { wx = x; wy = y; }
        }
    }
    REQUIRE(wx >= 0);
    int ids[3];
    for (int k = 0; k < 3; ++k) ids[k] = w.create_building(0, wall, wx + k, wy, true);
    auto frame = [&](int k) { return int(w.buildings()[size_t(ids[k])].wall_frame); };
    MESSAGE("frames " << frame(0) << " " << frame(1) << " " << frame(2) << "; wall element frames "
            << t.art.building[size_t(t.building_records[size_t(wall)].base_element)].frames);
    CHECK(frame(0) == 1);  // east neighbour (mask 8)
    CHECK(frame(1) == 6);  // west + east (mask 12)
    CHECK(frame(2) == 2);  // west neighbour (mask 4)
    w.destroy_building(ids[1]);
    for (int i = 0; i < 40; ++i) w.tick(); // the death countdown, then removal
    CHECK(frame(0) == 0);
    CHECK(frame(2) == 0);
}

TEST_CASE("golden: A3V1 barracks upgrade unlocks troopers; the Ordos AI upgrades too (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A3V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A3V1.MIS").value()));
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
    w.set_script(mis);
    w.set_ai(mis);
    auto name_of = [&](const char* n) {
        for (size_t i = 0; i < t.building_name.size(); ++i) if (t.building_name[i] == n) return int(i);
        return -1;
    };
    const int barracks = name_of("BARRACKS");
    REQUIRE(barracks >= 0);
    // The list: CY, BARRACKS, AT_HIGH_TECH_FACTORY, LIGHT_FACTORY, HEAVY_FACTORY.
    std::string list;
    for (int r : w.player(0).upgrade_list) list += r < 0 ? std::string(" -") : " " + t.building_name[t.building_records[size_t(r)].name_index];
    MESSAGE("upgrade list:" << list);
    CHECK(!w.can_upgrade(0, barracks)); // no barracks yet
    // A barracks for the player, then the upgrade.
    const int rec = t.building_record(barracks, w.player(0).house);
    std::vector<sim::PlaceCell> pc;
    int bi = -1;
    for (int y = 0; y < gm.height() && bi < 0; ++y)
        for (int x = 0; x < gm.width() && bi < 0; ++x)
            if (w.placement_check(0, rec, x, y, pc)) bi = w.create_building(0, rec, x, y, true);
    REQUIRE(bi >= 0);
    CHECK(w.can_upgrade(0, barracks));
    auto has_trooper = [&] {
        for (int r : w.player(0).unit_list) if (t.unit_name[t.unit_records[size_t(r)].name_index] == "TROOPER") return true;
        return false;
    };
    CHECK(!has_trooper());
    w.player(0).cash += 5000;
    w.start_upgrade(0, barracks);
    CHECK(w.player(0).upgrade_slot.item == rec);
    int done = -1;
    for (int i = 0; i < 1000 && done < 0; ++i) {
        w.tick();
        if (w.player(0).upgrade_level[size_t(barracks)] == 1) done = i;
    }
    MESSAGE("barracks upgraded after " << done << " ticks; Ordos barracks level " << int(w.player(2).upgrade_level[size_t(barracks)]));
    MESSAGE("Ordos slot item " << w.player(2).upgrade_slot.item << " progress " << w.player(2).upgrade_slot.progress
            << " hold " << w.player(2).upgrade_slot.hold << " cash " << w.player(2).cash << " spice " << w.player(2).spice
            << " ai upgrade flag " << int(w.ai_u8(2, 0x05)) << " tech " << w.player(2).tech << " can " << w.can_upgrade(2, barracks));
    CHECK(done >= 400);  // rate 54: 427 ticks
    CHECK(done <= 440);
    CHECK(has_trooper());
    CHECK(w.player(0).upgrade_slot.item == -1);
    // The Ordos AI checks every 64 ticks: its barracks upgrade starts at
    // tick 64 and ends 427 ticks later (if it can pay as it goes).
    for (int i = 0; i < 200; ++i) w.tick();
    CHECK(w.player(2).upgrade_level[size_t(barracks)] == 1); // the Ordos AI upgraded its barracks too
}

TEST_CASE("golden: A3V1 the Ordos escort a hit harvester and sell in a power shortage (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A3V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A3V1.MIS").value()));
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
    w.set_script(mis);
    w.set_ai(mis);
    for (int i = 0; i < 100; ++i) w.tick(); // groups formed
    // A harvester for the Ordos (side 2) near their refinery, then "hit" it.
    int harv = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        if (t.unit_records[i].kind == 1 && (t.unit_records[i].house_mask & 4) && harv < 0) harv = int(i);
    }
    const sim::Building* ref = nullptr;
    for (const auto& b : w.buildings()) if (b.owner == 2 && t.building_records[size_t(b.record)].kind == 4) ref = &b;
    REQUIRE(harv >= 0);
    REQUIRE(ref);
    const int pos = w.create_unit(2, harv, ref->cell_x + 4, ref->cell_y + 4, ref->cell_x + 4, ref->cell_y + 4);
    REQUIRE(pos >= 0);
    const int id = w.units()[size_t(pos)].index;
    auto escort = [&] {
        for (int g = 1; g < 32; ++g) {
            if (w.ai_u8(2, size_t(g) * 0x2C + 0x52) && w.ai_u8(2, size_t(g) * 0x2C + 0x48) == 0xE) return g;
        }
        return -1;
    };
    CHECK(escort() < 0);
    int g = -1;
    for (int i = 0; i < 8 && g < 0; ++i) {
        w.units()[size_t(w.find_unit(id))].hit_age = 0; // hit just now
        w.tick();
        g = escort();
    }
    const auto& hu = w.units()[size_t(w.find_unit(id))];
    MESSAGE("escort group " << g << " at " << int(w.ai_u8(2, size_t(std::max(g, 0)) * 0x2C + 0x50)) << ","
                            << int(w.ai_u8(2, size_t(std::max(g, 0)) * 0x2C + 0x51)) << "; harvester at "
                            << int(hu.cur_x) << "," << int(hu.cur_y) << " flag 8 " << ((hu.flags & 8) != 0));
    CHECK(g > 0);
    CHECK((hu.flags & 8) != 0);

    // Power shortage with < 400 money: no damaged producer, so the Ordos
    // sell their highest sell-priority building (the Outpost, 13).
    for (const auto& b : w.buildings()) {
        if (b.owner == 2 && !b.removed && t.building_name[t.building_records[size_t(b.record)].name_index] == "WIND_TRAP") {
            w.destroy_building(b.index); // (the WIND_TRAP records do not carry kind 3, doc 08)
        }
    }
    w.player(2).cash = 100;
    w.player(2).spice = 0;
    int sold = -1;
    for (int i = 0; i < 80 && sold < 0; ++i) {
        w.tick();
        for (const auto& b : w.buildings()) {
            if (b.owner == 2 && (b.flags & 0x4000000)) sold = b.index;
        }
    }
    MESSAGE("after shortage: power " << w.player(2).power_produced << " / " << w.player(2).power_used << " cash "
            << w.player(2).cash << " spice " << w.player(2).spice << " pending " << w.player(2).pending_cash << " list "
            << w.player(2).building_list.size() << " dontsell " << int(w.ai_u8(2, 7)));
    REQUIRE(sold >= 0);
    const std::string name = t.building_name[t.building_records[size_t(w.buildings()[size_t(sold)].record)].name_index];
    MESSAGE("sold " << name << "; power " << w.player(2).power_produced << " / " << w.player(2).power_used);
    CHECK(name == "OUTPOST");
}

TEST_CASE("golden: A3V1 a map-placed refinery gets its free harvester; the script replaces a lost one (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A3V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A3V1.MIS").value()));
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
    w.set_script(mis);
    w.set_ai(mis);
    auto harvesters = [&] {
        int n = 0;
        for (const auto& u : w.units()) n += u.owner == 2 && !u.remove && t.unit_records[size_t(u.record)].kind == 1;
        return n;
    };
    int first = -1;
    for (int i = 0; i < 600 && first < 0; ++i) {
        w.tick();
        if (harvesters() > 0) first = int(w.tick_count());
    }
    MESSAGE("Ordos free harvester at tick " << first);
    CHECK(first > 0); // the carryall from the map edge, at mission start
    for (int i = 0; i < 2000; ++i) w.tick();
    for (auto& u : w.units()) {
        if (u.owner == 2 && t.unit_records[size_t(u.record)].kind == 1) {
            u.hit_points = 0;
            u.remove = true; // gone (e.g. eaten)
        }
    }
    int again = -1;
    for (int i = 0; i < 2000 && again < 0; ++i) {
        w.tick();
        if (harvesters() > 0) again = i;
    }
    MESSAGE("replacement after " << again << " ticks");
    CHECK(again >= 0); // DEPLOY HARVESTER on TIMER % 500 with the script flag set
}

TEST_CASE("golden: A4V1 a damaged tank repairs at a repair pad and leaves it (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A4V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A4V1.MIS").value()));
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
    int padrec = -1;
    for (size_t i = 0; i < t.building_records.size(); ++i) {
        if (t.building_records[i].kind == 6 && (t.building_records[i].house_mask & 1) && padrec < 0) padrec = int(i);
    }
    REQUIRE(padrec >= 0);
    std::vector<sim::PlaceCell> pc;
    int pad = -1;
    for (int y = 0; y < gm.height() && pad < 0; ++y)
        for (int x = 0; x < gm.width() && pad < 0; ++x)
            if (w.placement_check(0, padrec, x, y, pc)) pad = w.create_building(0, padrec, x, y, true);
    REQUIRE(pad >= 0);
    // The player's first combat tank, damaged.
    int ti = -1;
    for (size_t i = 0; i < w.units().size(); ++i) {
        if (w.units()[i].owner == 0 && t.unit_name[t.unit_records[size_t(w.units()[i].record)].name_index] == "COMBAT_TANK") {
            ti = int(i);
            break;
        }
    }
    REQUIRE(ti >= 0);
    const int id = w.units()[size_t(ti)].index;
    const int max = t.unit_records[size_t(w.units()[size_t(ti)].record)].hit_points;
    w.units()[size_t(ti)].hit_points = max / 4;
    const int cash0 = w.player(0).cash;
    w.deselect_all();
    w.select_unit(size_t(ti), false);
    w.order_repair_units(0, pad);
    auto unit = [&]() -> const sim::Unit& { return w.units()[size_t(w.find_unit(id))]; };
    CHECK(unit().state == 0x18);
    int on_pad = -1, healed = -1, left = -1;
    for (int i = 0; i < 4000 && left < 0; ++i) {
        w.tick();
        const auto& u = unit();
        if (on_pad < 0 && u.state == 0x19) on_pad = i;
        if (healed < 0 && u.hit_points == max) healed = i;
        if (healed >= 0 && u.state != 0x19 && u.state != 0x1a && w.building_at_cell(u.cur_x, u.cur_y) != pad) left = i;
    }
    const auto& u = unit();
    MESSAGE("on pad at " << on_pad << ", healed at " << healed << ", off the pad at " << left << "; hp " << u.hit_points
                         << "/" << max << " paid " << (cash0 - w.player(0).cash - w.player(0).pending_cash)
                         << " state " << u.state);
    CHECK(on_pad >= 0);
    CHECK(healed > on_pad);
    CHECK(left > healed);
    CHECK(w.player(0).cash + w.player(0).pending_cash < cash0);
}

TEST_CASE("golden: A4V1 a heavy factory builds a combat tank that drives out (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A4V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A4V1.MIS").value()));
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
    auto place = [&](const char* name) {
        int rec = -1;
        for (size_t i = 0; i < t.building_records.size(); ++i)
            if (t.building_name[t.building_records[i].name_index] == name && (t.building_records[i].house_mask & 1) && rec < 0) rec = int(i);
        std::vector<sim::PlaceCell> pc;
        for (int y = 0; y < gm.height(); ++y)
            for (int x = 0; x < gm.width(); ++x)
                if (w.placement_check(0, rec, x, y, pc)) return w.create_building(0, rec, x, y, true);
        return -1;
    };
    REQUIRE(place("WIND_TRAP") >= 0);
    REQUIRE(place("REFINERY") >= 0);
    REQUIRE(place("WIND_TRAP") >= 0);
    REQUIRE(place("HEAVY_FACTORY") >= 0);
    int tank = -1;
    for (int r : w.player(0).unit_list) if (t.unit_name[t.unit_records[size_t(r)].name_index] == "COMBAT_TANK") tank = r;
    REQUIRE(tank >= 0);
    int before = 0;
    for (const auto& u : w.units()) before += u.owner == 0 && u.record == tank;
    w.start_unit(0, tank);
    int after = before, at = -1;
    for (int i = 0; i < 3000 && after == before; ++i) {
        w.tick();
        after = 0;
        for (const auto& u : w.units()) after += u.owner == 0 && u.record == tank && !u.remove;
        if (after > before) at = i;
    }
    MESSAGE("combat tank out after " << at << " ticks");
    CHECK(after == before + 1);
}

TEST_CASE("golden: A4V1 spice blooms are created, grow on a clear cell and burst into spice (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A4V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A4V1.MIS").value()));
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
    int blooms = 0, first = -1;
    for (size_t i = 0; i < gm.objects().size(); ++i) {
        const auto& e = gm.objects()[i];
        if (e.active && e.type == 7) {
            ++blooms;
            if (first < 0) first = int(i);
            CHECK((gm.at(e.x, e.y).flags & 0x1000) != 0);
            CHECK(e.timer >= 2000);
            CHECK(e.timer < 3000);
        }
    }
    MESSAGE("blooms " << blooms);
    CHECK(blooms == 7);
    REQUIRE(first >= 0);
    auto& e = gm.objects()[size_t(first)];
    const int bx = e.x, by = e.y, regrow = e.regrow;
    // Blocked by its own spice: the timer only reloads.
    e.timer = 1;
    w.tick();
    CHECK(e.type == 7);
    // Clear cell: it grows.
    gm.at(bx, by).flags &= ~0x700000u;
    e.timer = 1;
    w.tick();
    CHECK(e.type == 8);
    CHECK(e.image == 5);
    // Ripe and large: it bursts by itself.
    auto spice_cells = [&] {
        int n = 0;
        for (int y = by - 6; y <= by + 6; ++y)
            for (int x = bx - 6; x <= bx + 6; ++x)
                if (x >= 0 && y >= 0 && x < gm.width() && y < gm.height() && (gm.at(x, y).flags & 0x700000)) ++n;
        return n;
    };
    const int before = spice_cells();
    e.type = 10;
    e.image = 7;
    e.timer = 1;
    w.tick();
    const size_t blobs = w.bullets.size();
    CHECK(e.type == 7);               // re-armed
    CHECK(e.regrow == regrow - 1);
    for (int i = 0; i < 200; ++i) w.tick();
    const int after = spice_cells();
    MESSAGE("burst: " << blobs << " blobs, spice cells around " << before << " -> " << after);
    CHECK(blobs > 0);
    CHECK(after > before);
}

TEST_CASE("golden: A4V1 an MCV deploys, an engineer captures a refinery, a saboteur destroys a building (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A4V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A4V1.MIS").value()));
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
    auto unit_of_kind = [&](int kind) {
        for (size_t i = 0; i < t.unit_records.size(); ++i)
            if (t.unit_records[i].kind == kind) return int(i);
        return -1;
    };

    // MCV: row 1 never deploys (row 0 fails the bounds test).
    CHECK_FALSE(w.can_deploy(10, 1));
    const int mcv = unit_of_kind(6);
    REQUIRE(mcv >= 0);
    int mx = -1, my = -1;
    for (int y = 2; y < gm.height() - 1 && mx < 0; ++y)
        for (int x = 1; x < gm.width() - 1 && mx < 0; ++x)
            if (w.can_deploy(x, y)) {
                mx = x;
                my = y;
            }
    REQUIRE(mx >= 0);
    const int mi = w.create_unit(0, mcv, mx, my, mx, my);
    REQUIRE(mi >= 0);
    const int mcv_id = w.units()[size_t(mi)].index;
    const size_t nb = w.buildings().size();
    w.order_deploy(0, mcv_id);
    CHECK(w.units()[size_t(w.find_unit(mcv_id))].state == 0x1e);
    for (int i = 0; i < 300 && w.buildings().size() == nb; ++i) w.tick();
    REQUIRE(w.buildings().size() == nb + 1);
    {
        const auto& yard = w.buildings().back();
        CHECK(yard.owner == 0);
        CHECK(t.building_records[size_t(yard.record)].kind == 2);
        CHECK(yard.cell_x == mx - 1);
        CHECK(yard.cell_y == my - 1);
        CHECK(yard.hit_points == t.building_records[size_t(yard.record)].hit_points);
    }
    CHECK((w.units()[size_t(w.find_unit(mcv_id))].flags & 0x600000) == 0x600000);
    MESSAGE("MCV deployed at " << mx << "," << my);

    // An engineer next to an enemy refinery captures it.
    auto enemy_building = [&](int kind, uint32_t flag) {
        for (const auto& b : w.buildings())
            if (b.owner != 0 && !b.removed && !b.dying && w.enemy(0, b.owner) &&
                (kind < 0 || t.building_records[size_t(b.record)].kind == kind) && (b.flags & flag))
                return b.index;
        return -1;
    };
    auto next_to = [&](int bi, int& ox, int& oy) {
        const auto& b = w.buildings()[size_t(bi)];
        const auto& r = t.building_records[size_t(b.record)];
        for (int k = 0; k < 16; ++k) {
            if (!(r.footprint & (1u << k))) continue;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = b.cell_x + (k & 3) + dx, y = b.cell_y + (k >> 2) + dy;
                    if (x < 0 || y < 0 || x >= gm.width() || y >= gm.height()) continue;
                    if (gm.at(x, y).flags & 0x3f8) continue;
                    ox = x;
                    oy = y;
                    return true;
                }
        }
        return false;
    };
    const int ref = enemy_building(4, 0x400);
    REQUIRE(ref >= 0);
    const int victim = w.buildings()[size_t(ref)].owner;
    w.player(victim).spice = 1000;
    const int cap = w.player(victim).capacity;
    REQUIRE(cap > 0);
    const int32_t pend0 = w.player(0).pending_spice + w.player(0).spice;
    int ex = 0, ey = 0;
    REQUIRE(next_to(ref, ex, ey));
    const int ei = w.create_unit(0, unit_of_kind(3), ex, ey, ex, ey);
    REQUIRE(ei >= 0);
    const int eng_id = w.units()[size_t(ei)].index;
    w.deselect_all();
    w.select_unit(size_t(ei), false);
    w.order_attack_building(0, ref);
    CHECK(w.units()[size_t(w.find_unit(eng_id))].state == 5);
    const size_t nb2 = w.buildings().size();
    int captured = -1;
    for (int i = 0; i < 300 && captured < 0; ++i) {
        w.tick();
        if (w.buildings().size() > nb2) captured = i;
    }
    REQUIRE(captured >= 0);
    const int ni = int(w.buildings().size()) - 1;
    CHECK(w.buildings()[size_t(ni)].owner == 0);
    CHECK(w.buildings()[size_t(ni)].record == w.buildings()[size_t(ref)].record);
    CHECK((w.buildings()[size_t(ni)].flags & 0x2000000) != 0);
    CHECK((w.buildings()[size_t(ni)].flags & 0x80) == 0);
    CHECK((w.buildings()[size_t(ref)].flags & 0x110000) == 0x110000);
    CHECK(w.player(0).pending_spice + w.player(0).spice > pend0); // the share moved (harvesters run too)
    CHECK(w.units()[size_t(w.find_unit(eng_id))].state == 0x11);
    w.tick();
    w.tick();
    CHECK(w.buildings()[size_t(ref)].removed);
    int fx = 0, fy = 0;
    for (int k = 15; k >= 0; --k) {
        if (t.building_records[size_t(w.buildings()[size_t(ni)].record)].footprint & (1u << k)) {
            fx = w.buildings()[size_t(ni)].cell_x + (k & 3);
            fy = w.buildings()[size_t(ni)].cell_y + (k >> 2);
        }
    }
    CHECK(w.building_at_cell(fx, fy) == ni);
    MESSAGE("captured after " << captured << " ticks (capacity " << cap << ")");

    // A saboteur destroys a building with flag 0x200.
    const int tgt = enemy_building(-1, 0x200);
    REQUIRE(tgt >= 0);
    int sx = 0, sy = 0;
    REQUIRE(next_to(tgt, sx, sy));
    const int si = w.create_unit(0, unit_of_kind(4), sx, sy, sx, sy);
    REQUIRE(si >= 0);
    const int sab_id = w.units()[size_t(si)].index;
    w.deselect_all();
    w.select_unit(size_t(si), false);
    w.order_attack_building(0, tgt);
    int gone = -1;
    for (int i = 0; i < 300 && gone < 0; ++i) {
        w.tick();
        if (w.buildings()[size_t(tgt)].dying || w.buildings()[size_t(tgt)].removed) gone = i;
    }
    CHECK(gone >= 0);
    CHECK(w.units()[size_t(w.find_unit(sab_id))].state == 0x11);
    MESSAGE("sabotaged after " << gone << " ticks");
}

TEST_CASE("golden: A5V1 runs with the AI and the script; the neutral smugglers stay out of fights (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A5V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A5V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    w.set_script(mis);
    w.set_ai(mis);
    // Side 5 (the smugglers) starts neutral to the player both ways.
    CHECK(w.alliance(0, 5) == 2);
    CHECK(w.alliance(5, 0) == 2);
    int smuggler_fights = 0, harkonnen_units_max = 0;
    for (int i = 0; i < 6000 && !w.won && !w.lost; ++i) {
        w.tick();
        int hk = 0;
        for (const auto& u : w.units()) {
            if (u.owner == 1 && !u.remove) ++hk;
            if (u.target_kind == 1 && w.alliance(0, 5) == 2) {
                const int ti = w.find_unit(u.target);
                if (ti < 0) continue;
                const int o = w.units()[size_t(ti)].owner;
                if ((u.owner == 5 && o == 0) || (u.owner == 0 && o == 5)) ++smuggler_fights;
            }
        }
        harkonnen_units_max = std::max(harkonnen_units_max, hk);
    }
    MESSAGE("tick " << w.tick_count() << " won " << w.won << " lost " << w.lost << " harkonnen units max "
                    << harkonnen_units_max << " player cash " << w.player(0).cash + w.player(0).spice);
    CHECK(smuggler_fights == 0);
    CHECK_FALSE(w.lost);
}

TEST_CASE("golden: A5V1 captured Starport: the script's frigate brings an MCV, then a purchase arrives (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A5V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A5V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    w.set_script(mis);
    w.set_ai(mis);
    // The Starport list: 8 starportable names, the side's versions.
    for (int r : w.player(0).starport_list) CHECK(r >= 0);
    int port = -1;
    for (const auto& b : w.buildings())
        if (b.owner == 5 && t.building_records[size_t(b.record)].kind == 13) port = b.index;
    REQUIRE(port >= 0);
    // An engineer next to it.
    auto unit_of_kind = [&](int kind) {
        for (size_t i = 0; i < t.unit_records.size(); ++i)
            if (t.unit_records[i].kind == kind) return int(i);
        return -1;
    };
    int ex = -1, ey = -1;
    {
        const auto& b = w.buildings()[size_t(port)];
        const auto& r = t.building_records[size_t(b.record)];
        for (int k = 0; k < 16 && ex < 0; ++k) {
            if (!(r.footprint & (1u << k))) continue;
            for (int dy = -1; dy <= 1 && ex < 0; ++dy)
                for (int dx = -1; dx <= 1 && ex < 0; ++dx) {
                    const int x = b.cell_x + (k & 3) + dx, y = b.cell_y + (k >> 2) + dy;
                    if (x < 0 || y < 0 || x >= gm.width() || y >= gm.height() || (gm.at(x, y).flags & 0x3f8)) continue;
                    ex = x;
                    ey = y;
                }
        }
    }
    REQUIRE(ex >= 0);
    const int ei = w.create_unit(0, unit_of_kind(3), ex, ey, ex, ey);
    REQUIRE(ei >= 0);
    w.deselect_all();
    w.select_unit(size_t(ei), false);
    w.order_attack_building(0, port);
    int captured = -1, frigate_seen = -1, mcv_at = -1;
    const int mcv = unit_of_kind(6);
    for (int i = 0; i < 4000 && mcv_at < 0; ++i) {
        w.tick();
        if (captured < 0 && w.player(0).primary[13] >= 0) captured = i;
        for (const auto& u : w.units()) {
            if (u.owner == 0 && t.unit_records[size_t(u.record)].kind == 8 && frigate_seen < 0) frigate_seen = i;
            if (u.owner == 0 && u.record == mcv && !u.remove && mcv_at < 0) mcv_at = i;
        }
    }
    MESSAGE("captured at " << captured << ", frigate at " << frigate_seen << ", MCV out at " << mcv_at);
    CHECK(captured >= 0);
    CHECK(frigate_seen >= captured);
    REQUIRE(mcv_at > frigate_seen);
    CHECK(w.alliance(0, 5) == 1); // the capture made the smugglers hostile
    // Let the frigate finish, then buy two quads.
    for (int i = 0; i < 1500 && w.player(0).delivery_pending; ++i) w.tick();
    CHECK_FALSE(w.player(0).delivery_pending);
    int quad = -1;
    for (int r : w.player(0).starport_list)
        if (r >= 0 && t.unit_name[size_t(t.unit_records[size_t(r)].name_index)] == "QUAD") quad = r;
    REQUIRE(quad >= 0);
    auto& pl = w.player(0);
    CHECK(pl.price[size_t(quad)] < uint32_t(w.unit_cost(0, quad)));
    CHECK(pl.price[size_t(quad)] >= uint32_t(w.unit_cost(0, quad)) * 3 / 4);
    pl.stock[size_t(quad)] = 2;
    pl.cash += 5000;
    int quads0 = 0;
    for (const auto& u : w.units()) quads0 += u.owner == 0 && u.record == quad && !u.remove;
    w.starport_add(0, quad);
    w.starport_add(0, quad);
    CHECK(pl.cart[size_t(quad)] == 2);
    CHECK(pl.starport_money == 2 * pl.price[size_t(quad)]);
    w.starport_purchase(0);
    CHECK(pl.delivery_pending);
    // Count arrivals (rises in the quad count), not the total: a quad may
    // die in the fighting meanwhile.
    int arrived = -1, prev = quads0, came = 0;
    for (int i = 0; i < 3000 && arrived < 0; ++i) {
        w.tick();
        int q = 0;
        for (const auto& u : w.units()) q += u.owner == 0 && u.record == quad && !u.remove;
        if (q > prev) came += q - prev;
        prev = q;
        if (came >= 2) arrived = i;
    }
    MESSAGE("purchase delivered after " << arrived << " ticks");
    CHECK(arrived >= 1500);
    for (int i = 0; i < 1000 && pl.delivery_pending; ++i) w.tick();
    CHECK(pl.starport_money == 0);
}

TEST_CASE("golden: A5V1 carryalls: the map carryall flies in; a full harvester far away is ferried to its refinery (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A5V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A5V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    // The Harkonnen map carryall starts off the map, in the air, flying in.
    int hk = -1;
    for (const auto& u : w.units())
        if (u.owner == 1 && t.unit_records[size_t(u.record)].kind == 2) hk = u.index;
    REQUIRE(hk >= 0);
    {
        const auto& c = w.units()[size_t(w.find_unit(hk))];
        CHECK((c.flags & 0x40) != 0);
        CHECK(c.state == 7);
        CHECK((c.cur_x >= gm.width() || c.cur_y >= gm.height()));
        CHECK(w.player(1).carry_owned == 1);
    }
    // A player carryall, and a full harvester far from the refinery.
    int carryall_rec = -1, harv_rec = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        if (t.unit_records[i].kind == 2 && carryall_rec < 0) carryall_rec = int(i);
        if (t.unit_records[i].kind == 1 && harv_rec < 0) harv_rec = int(i);
    }
    int ref = -1;
    for (const auto& b : w.buildings())
        if (b.owner == 0 && t.building_records[size_t(b.record)].kind == 4) ref = b.index;
    REQUIRE(ref >= 0);
    const int rx = w.buildings()[size_t(ref)].cell_x, ry = w.buildings()[size_t(ref)].cell_y;
    REQUIRE(w.create_unit(0, carryall_rec, rx, ry, rx, ry) >= 0);
    int hx = -1, hy = -1;
    for (int d = 14; d < 30 && hx < 0; ++d)
        for (int k = 0; k < 4 && hx < 0; ++k) {
            const int x = rx + (k == 0 ? d : k == 1 ? -d : 0), y = ry + (k == 2 ? d : k == 3 ? -d : 0);
            if (x < 1 || y < 1 || x >= gm.width() - 1 || y >= gm.height() - 1) continue;
            const uint32_t f = gm.at(x, y).flags;
            if ((f & 0x2000) && !(f & 0x3f8)) {
                hx = x;
                hy = y;
            }
        }
    REQUIRE(hx >= 0);
    const int hi = w.create_unit(0, harv_rec, hx, hy, hx, hy);
    REQUIRE(hi >= 0);
    const int hid = w.units()[size_t(hi)].index;
    w.units()[size_t(hi)].load = 7;
    int waited = -1, carried = -1, unloading = -1;
    for (int i = 0; i < 4000 && unloading < 0; ++i) {
        w.tick();
        const auto& h = w.units()[size_t(w.find_unit(hid))];
        if (h.state == 0x12 && waited < 0) waited = i;
        if (h.state == 0x13 && carried < 0) carried = i;
        if (h.state == 0xf && unloading < 0) unloading = i;
    }
    MESSAGE("harvester at " << hx << "," << hy << ": waits at " << waited << ", carried at " << carried
                            << ", unloads at " << unloading);
    CHECK(waited >= 0);
    CHECK(carried > waited);
    CHECK(unloading > carried);
}

TEST_CASE("golden: A5V1 missile tanks: homing rockets hit ground units and shoot down an enemy carryall (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A5V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A5V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    // The missile tank: a vehicle whose weapon is anti-air; the carryall.
    int mt = -1, carryall_rec = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        const auto& r = t.unit_records[i];
        if (r.kind == 2 && carryall_rec < 0) carryall_rec = int(i);
        if (mt < 0 && !r.infantry && r.kind == 0 && r.weapon != 0xff && t.bullets[r.weapon].anti_air) mt = int(i);
    }
    REQUIRE(mt >= 0);
    REQUIRE(carryall_rec >= 0);
    CHECK((t.bullets[t.unit_records[size_t(mt)].weapon].flags & 0x20) != 0); // homing
    // An open, empty area away from every base.
    int ox = -1, oy = -1;
    for (int y = 8; y < gm.height() - 8 && ox < 0; y += 2)
        for (int x = 8; x < gm.width() - 8 && ox < 0; x += 2) {
            bool ok = true;
            for (int dy = -2; dy <= 2 && ok; ++dy)
                for (int dx = -1; dx <= 5 && ok; ++dx) {
                    const uint32_t f = gm.at(x + dx, y + dy).flags;
                    ok = (f & 0x2000) && !(f & 0x3f8);
                }
            for (const auto& b : w.buildings())
                if (std::abs(b.cell_x - x) < 12 && std::abs(b.cell_y - y) < 12) ok = false;
            if (ok) {
                ox = x;
                oy = y;
            }
        }
    REQUIRE(ox >= 0);
    const int a = w.create_unit(0, mt, ox, oy, ox, oy);
    REQUIRE(a >= 0);
    const int aid = w.units()[size_t(a)].index;
    // Air: an enemy carryall flies in to the open area; a missile tank waits there.
    const int c = w.create_unit(1, carryall_rec, ox + 2, oy, ox + 2, oy);
    REQUIRE(c >= 0);
    const int cid = w.units()[size_t(c)].index;
    const int32_t chp = w.units()[size_t(c)].hit_points;
    int air_hit = -1;
    for (int i = 0; i < 4000 && air_hit < 0; ++i) {
        w.tick();
        const int k = w.find_unit(cid);
        if (k < 0 || w.units()[size_t(k)].hit_points < chp) air_hit = i;
    }
    MESSAGE("carryall hit at tick " << air_hit);
    CHECK(w.find_unit(aid) >= 0);
    CHECK(air_hit >= 0);
    // Ground: an enemy (Harkonnen, side 1) tank 4 cells away.
    const int e = w.create_unit(1, mt, ox + 4, oy, ox + 4, oy);
    REQUIRE(e >= 0);
    const int eid = w.units()[size_t(e)].index;
    const int32_t hp0 = w.units()[size_t(e)].hit_points;
    bool homing_seen = false;
    int hit_tick = -1;
    for (int i = 0; i < 300 && hit_tick < 0; ++i) {
        w.tick();
        for (const auto& b : w.bullets)
            if (b.flags & 0x20) homing_seen = true;
        const int k = w.find_unit(eid);
        if (k < 0 || w.units()[size_t(k)].hit_points < hp0) hit_tick = i;
    }
    MESSAGE("ground target hit at tick " << hit_tick);
    CHECK(homing_seen);
    CHECK(hit_tick >= 0);
}

TEST_CASE("golden: A6V1 an AI MCV of a side without a yard that cannot deploy where it stands drives to a site and deploys (DealWithMCV) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    struct Case {
        const char* name;
        int side;
    };
    for (const Case c : {Case{"A6V1", 3}}) {
        auto map = formats::parse_map(formats::read_file(gd.find(std::string("MISSIONS/") + c.name + ".MAP").value()));
        auto mis = formats::parse_mission(formats::read_file(gd.find(std::string("MISSIONS/_") + c.name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.init_players(mis);
        w.set_script(mis);
        w.set_ai(mis);
        auto yards = [&] {
            int n = 0;
            for (const auto& b : w.buildings())
                if (b.owner == c.side && !b.removed && t.building_records[size_t(b.record)].kind == 2) ++n;
            return n;
        };
        const int before = yards();
        // An MCV right next to one of the side's turrets (no room there). The
        // side has no yard, so no build list and no rebuild kicks.
        int mcv = -1;
        for (size_t i = 0; i < t.unit_records.size(); ++i)
            if (t.unit_records[i].kind == 6 && (t.unit_records[i].house_mask & (1u << mis.house[size_t(c.side)])) && mcv < 0) mcv = int(i);
        REQUIRE(mcv >= 0);
        int cyx = -1, cyy = -1;
        for (const auto& b : w.buildings())
            if (b.owner == c.side && cyx < 0) {
                cyx = b.cell_x;
                cyy = b.cell_y;
            }
        REQUIRE(cyx >= 0);
        int mx = -1, my = -1;
        for (int d = 0; d < 6 && mx < 0; ++d)
            for (int k = 0; k < 4 && mx < 0; ++k) {
                const int x = cyx + (k == 0 ? 3 + d : k == 1 ? -1 - d : 1), y = cyy + (k == 2 ? 3 + d : k == 3 ? -1 - d : 1);
                if (x < 1 || y < 1 || x >= gm.width() - 1 || y >= gm.height() - 1) continue;
                const uint32_t f = gm.at(x, y).flags;
                if ((f & 0x2000) && !(f & 0x3f8) && !w.can_deploy(x, y)) {
                    mx = x;
                    my = y;
                }
            }
        REQUIRE(mx >= 0);
        REQUIRE(w.create_unit(c.side, mcv, mx, my, mx, my) >= 0);
        int mcvs_seen = 0, deployed = -1;
        for (int i = 0; i < 3000 && deployed < 0; ++i) {
            w.tick();
            int m = 0;
            for (const auto& u : w.units())
                if (u.owner == c.side && t.unit_records[size_t(u.record)].kind == 6) ++m;
            mcvs_seen = std::max(mcvs_seen, m);
            if (yards() > before) deployed = i;
        }
        MESSAGE(std::string(c.name) << " side " << c.side << ": MCVs " << mcvs_seen << ", first yard at tick " << deployed);
        CHECK(mcvs_seen > 0);
        CHECK(deployed >= 0);
    }
}

TEST_CASE("golden: A6V1 / A6V2 run with the AI and the script; the Ordos Starport side gets its frigate reinforcement (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    const auto* ids = t.block(0x6d60d8);
    const int frigate = ids && ids->bytes.size() > 14 ? ids->bytes[14] : -1;
    REQUIRE(frigate >= 0);
    for (const std::string name : {"A6V1", "A6V2"}) {
        auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.init_players(mis);
        w.set_script(mis);
        w.set_ai(mis);
        int frigate_tick = -1, max_units = 0, bullets_max = 0;
        for (int i = 0; i < 9000 && !w.won && !w.lost; ++i) {
            w.tick();
            int n = 0;
            for (const auto& u : w.units()) {
                if (u.owner != 0 && !u.remove) ++n;
                if (u.owner == 1 && u.record == frigate && frigate_tick < 0) frigate_tick = i;
            }
            max_units = std::max(max_units, n);
            bullets_max = std::max(bullets_max, int(w.bullets.size()));
        }
        MESSAGE(name << ": tick " << w.tick_count() << " won " << w.won << " lost " << w.lost << " enemy units max "
                     << max_units << " bullets max " << bullets_max << " side 1 frigate at " << frigate_tick);
        CHECK_FALSE(w.lost);
        CHECK(frigate_tick >= 0); // event type 1 (REINFORCE) for the side with the Starport
    }
}

TEST_CASE("golden: walls have no build-up and are removed at once when sold; the frame count is read by record (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A6V2.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A6V2.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    int wall = -1, windtrap = -1;
    for (size_t i = 0; i < t.building_records.size(); ++i) {
        const auto& r = t.building_records[i];
        if (!(r.house_mask & 1)) continue;
        if (r.kind == 0xe && wall < 0) wall = int(i);
        if (t.building_name[r.name_index] == "WIND_TRAP" && windtrap < 0) windtrap = int(i);
    }
    REQUIRE(wall >= 0);
    REQUIRE(windtrap >= 0);
    CHECK(t.art.anim1_frames[size_t(wall)] == 0);
    CHECK(t.art.anim1_frames[size_t(windtrap)] != 0);
    // Open ground next to the player's yard.
    int cyx = -1, cyy = -1;
    for (const auto& b : w.buildings())
        if (b.owner == 0 && t.building_records[size_t(b.record)].kind == 2) {
            cyx = b.cell_x;
            cyy = b.cell_y;
        }
    REQUIRE(cyx >= 0);
    int wx = -1, wy = -1;
    for (int d = 3; d < 8 && wx < 0; ++d)
        for (int k = 0; k < 4 && wx < 0; ++k) {
            const int x = cyx + (k == 0 ? d : k == 1 ? -d : 0), y = cyy + (k == 2 ? d : k == 3 ? -d : 0);
            if (x < 1 || y < 1 || x >= gm.width() - 1 || y >= gm.height() - 1) continue;
            const uint32_t f = gm.at(x, y).flags;
            if ((f & 0x2000) && !(f & 0x3f8) && !(f & 0x10)) {
                wx = x;
                wy = y;
            }
        }
    REQUIRE(wx >= 0);
    const int bi = w.create_building(0, wall, wx, wy, false);
    REQUIRE(bi >= 0);
    CHECK((w.buildings()[size_t(bi)].flags & 6) == 0); // no build-up (was the yard's MCV animation)
    w.order_sell(0, bi);
    CHECK((w.buildings()[size_t(bi)].flags & 4) == 0); // no reverse animation (was the yard's)
    CHECK((w.buildings()[size_t(bi)].flags & 8) != 0); // removed at once
}

TEST_CASE("golden: infantry crushed mid-step leave no occupancy behind (FUN_0049f8f0 clears both slots) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A6V2.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A6V2.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    int inf = -1, tank = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        const auto& r = t.unit_records[i];
        if (inf < 0 && r.infantry && r.kind == 0 && (r.house_mask & 4)) inf = int(i);
        if (tank < 0 && !r.infantry && r.crusher && r.kind == 0 && (r.house_mask & 1)) tank = int(i);
    }
    REQUIRE(inf >= 0);
    REQUIRE(tank >= 0);
    // A long open row away from the bases.
    int ox = -1, oy = -1;
    for (int y = 6; y < gm.height() - 6 && ox < 0; ++y)
        for (int x = 4; x < gm.width() - 24 && ox < 0; ++x) {
            bool ok = true;
            for (int dy = -1; dy <= 1 && ok; ++dy)
                for (int dx = 0; dx < 20 && ok; ++dx) {
                    const uint32_t f = gm.at(x + dx, y + dy).flags;
                    ok = (f & 0x2000) && !(f & 0x3f8) && !(f & 0x10);
                }
            for (const auto& b : w.buildings())
                if (std::abs(b.cell_x - x - 10) < 16 && std::abs(b.cell_y - y) < 16) ok = false;
            if (ok) {
                ox = x;
                oy = y;
            }
        }
    REQUIRE(ox >= 0);
    int crushed = 0, ghosts = 0;
    for (int round = 0; round < 12; ++round) {
        // Infantry walk one way, the tanks drive the other way through them;
        // the direction flips every round (the tanks stay on the row).
        const int west = ox, east = ox + 19;
        const int from = (round & 1) ? east : west, to = (round & 1) ? west : east;
        for (int k = 0; k < 3; ++k) w.create_unit(2, inf, from + (k % 2) * ((round & 1) ? -1 : 1), oy - 1 + k, from, oy - 1 + k);
        if (round == 0)
            for (int k = 0; k < 3; ++k) w.create_unit(0, tank, east, oy - 1 + k, east, oy - 1 + k);
        for (auto& u : w.units()) u.selected = u.owner == 2 && t.unit_records[size_t(u.record)].infantry;
        w.order_move(2, to, oy);
        for (auto& u : w.units()) u.selected = u.owner == 0 && u.record == tank;
        w.order_move(0, from, oy);
        for (auto& u : w.units()) u.selected = false;
        for (int i = 0; i < 400; ++i) {
            w.tick();
            for (const auto& s : w.sounds)
                if (s.name == "S_CRUSHMAN") ++crushed;
            w.sounds.clear();
        }
        // Every infantry slot bit must belong to a live infantry unit.
        for (int y = oy - 3; y <= oy + 3; ++y)
            for (int x = ox - 3; x < ox + 23; ++x) {
                const uint32_t f = gm.at(x, y).flags;
                for (int s = 0; s < 5; ++s) {
                    if (!(f & (0x20u << s))) continue;
                    bool ok = false;
                    for (const auto& u : w.units())
                        if (t.unit_records[size_t(u.record)].infantry &&
                            ((u.cur_x == x && u.cur_y == y && u.slot_cur == s) || (u.to_x == x && u.to_y == y && u.slot_next == s)))
                            ok = true;
                    if (!ok) ++ghosts;
                }
            }
    }
    MESSAGE("crushed " << crushed << ", stale infantry bits " << ghosts);
    CHECK(crushed > 0);
    CHECK(ghosts == 0);
}

TEST_CASE("golden: the sonic wave damages along its path in the second half of its flight, friends included (docs/re/36) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A6V2.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A6V2.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    int sonic = -1, quad = -1, trooper = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        const auto& n = t.unit_name[t.unit_records[i].name_index];
        if (n == "SONIC_TANK" && sonic < 0) sonic = int(i);
        if (n == "QUAD" && quad < 0 && (t.unit_records[i].house_mask & 4)) quad = int(i);
        if (n == "TROOPER" && trooper < 0 && (t.unit_records[i].house_mask & 1)) trooper = int(i);
    }
    REQUIRE(sonic >= 0);
    REQUIRE(quad >= 0);
    REQUIRE(trooper >= 0);
    CHECK((t.bullets[t.unit_records[size_t(sonic)].weapon].flags & 0x800) != 0);
    int ox = -1, oy = -1;
    for (int y = 6; y < gm.height() - 6 && ox < 0; ++y)
        for (int x = 4; x < gm.width() - 12 && ox < 0; ++x) {
            bool ok = true;
            for (int dy = -1; dy <= 1 && ok; ++dy)
                for (int dx = 0; dx < 8 && ok; ++dx) {
                    const uint32_t f = gm.at(x + dx, y + dy).flags;
                    ok = (f & 0x2000) && !(f & 0x3f8) && !(f & 0x10);
                }
            for (const auto& b : w.buildings())
                if (std::abs(b.cell_x - x - 4) < 14 && std::abs(b.cell_y - y) < 14) ok = false;
            if (ok) {
                ox = x;
                oy = y;
            }
        }
    REQUIRE(ox >= 0);
    // The tank, an enemy quad 4 cells east, a friendly trooper beside the path.
    const int ti = w.create_unit(0, sonic, ox, oy, ox, oy);
    REQUIRE(ti >= 0);
    const int tank = w.units()[size_t(ti)].index;
    const int ei = w.create_unit(2, quad, ox + 4, oy, ox + 4, oy);
    REQUIRE(ei >= 0);
    const int enemy = w.units()[size_t(ei)].index;
    const int fi = w.create_unit(0, trooper, ox + 3, oy + 1, ox + 3, oy + 1);
    REQUIRE(fi >= 0);
    const int friend_id = w.units()[size_t(fi)].index;
    auto hp = [&](int id) {
        const int k = w.find_unit(id);
        return k >= 0 ? w.units()[size_t(k)].hit_points : -1;
    };
    const int32_t tank0 = hp(tank), friend0 = hp(friend_id);
    int32_t last = hp(enemy);
    int hits = 0, fired = -1;
    for (int i = 0; i < 120; ++i) {
        w.tick();
        for (const auto& b : w.bullets)
            if ((b.flags & 0x800) && fired < 0) fired = i;
        const int32_t h = hp(enemy);
        if (h >= 0 && h < last) ++hits;
        if (h >= 0) last = h;
        if (fired >= 0 && i > fired + 30) break;
    }
    MESSAGE("fired at " << fired << ", enemy hits " << hits << ", enemy hp " << last << ", friend " << friend0 << " -> "
                        << hp(friend_id) << ", tank " << tank0 << " -> " << hp(tank));
    CHECK(fired >= 0);
    CHECK(hits >= 8);                 // repeated hits from one wave
    CHECK(hp(friend_id) < friend0);   // no side check
    CHECK(hp(tank) == tank0);         // out of its own wave's reach
}

TEST_CASE("golden: A7V1 airstrike: order 0x30 launches 3 ornithopters that bomb the target and leave (docs/re/35) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A7V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A7V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    const auto* ids = t.block(0x6d60d8);
    REQUIRE(ids);
    const int orni = ids->bytes[0x12];
    CHECK(t.unit_records[size_t(orni)].kind == 9);
    // A ready airstrike in the player's slot 0, aimed at a Harkonnen building.
    auto& slot = w.player(0).unit_slots[0];
    slot = sim::ProductionSlot{};
    slot.item = int16_t(orni);
    slot.progress = sim::kProgressDone;
    int tb = -1;
    for (const auto& b : w.buildings())
        if (b.owner == 1 && !b.removed && t.building_records[size_t(b.record)].kind != 0xe) {
            tb = b.index;
            break;
        }
    REQUIRE(tb >= 0);
    const int tx = w.buildings()[size_t(tb)].cell_x, ty = w.buildings()[size_t(tb)].cell_y;
    const int32_t hp0 = w.buildings()[size_t(tb)].hit_points;
    w.order_special(0, 9, tx, ty);
    CHECK(w.player(0).unit_slots[0].item == -1); // the ready slot is cleared
    std::vector<int> ids3;
    for (const auto& u : w.units())
        if (u.owner == 0 && u.record == orni) {
            ids3.push_back(u.index);
            CHECK((u.flags & 0x10840) == 0x10840);
            CHECK(u.bombs == 12);
            CHECK(u.state == 7);
            CHECK((u.x < 0 || u.y < 0 || (u.x >> 21) >= gm.width() || (u.y >> 21) >= gm.height())); // past the edge
        }
    REQUIRE(ids3.size() == 3);
    int bombs = 0, gone = -1, shot_down = 0;
    std::map<int, int> left;
    for (const int id : ids3) left[id] = 12;
    for (int i = 0; i < 4000 && gone < 0; ++i) {
        w.tick();
        int alive = 0;
        for (const int id : ids3) {
            const int k = w.find_unit(id);
            if (k < 0) continue;
            ++alive;
            const auto& u = w.units()[size_t(k)];
            bombs += left[id] - u.bombs;
            left[id] = u.bombs;
            if (u.hit_points <= 0 && u.bombs > 0 && left[id] >= 0) {
                ++shot_down;
                left[id] = -100;
            }
        }
        if (alive == 0) gone = i;
    }
    const int32_t hp1 = w.buildings()[size_t(tb)].removed ? 0 : w.buildings()[size_t(tb)].hit_points;
    MESSAGE("bombs dropped " << bombs << " (shot down " << shot_down << "), ornithopters gone at tick " << gone << ", target hp " << hp0 << " -> " << hp1);
    CHECK(shot_down == 0);
    CHECK(bombs == 36);
    CHECK(bombs > 0);
    CHECK(gone > 0);
    CHECK(hp1 < hp0);
}

TEST_CASE("golden: a viewed enemy unit stays selected through its AI's ticks; a box with only an own aircraft clears the selection (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A7V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A7V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    w.set_script(mis);
    w.set_ai(mis);
    // An idle Harkonnen (side 1, AI active) ground unit.
    int ei = -1;
    for (size_t i = 0; i < w.units().size(); ++i) {
        const auto& u = w.units()[i];
        if (u.owner == 1 && !(u.flags & 0x140) && t.unit_records[size_t(u.record)].kind != 5) {
            ei = int(i);
            break;
        }
    }
    REQUIRE(ei >= 0);
    const int eid = w.units()[size_t(ei)].index;
    w.view_unit(size_t(ei));
    for (int i = 0; i < 40; ++i) w.tick();
    const int k = w.find_unit(eid);
    REQUIRE(k >= 0);
    CHECK(w.units()[size_t(k)].selected);   // GetSelected / ResetSelected around DoAI
    CHECK_FALSE(w.any_unit_selected(0));     // not one of ours: no orders go to it
    // A box around only an own carryall: the selection is cleared, nothing selected.
    int carryall = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i)
        if (t.unit_records[i].kind == 2 && (t.unit_records[i].house_mask & 1)) {
            carryall = int(i);
            break;
        }
    REQUIRE(carryall >= 0);
    const int ci = w.create_unit(0, carryall, 20, 20, 20, 20);
    REQUIRE(ci >= 0);
    const auto& c = w.units()[size_t(ci)];
    const int cx = c.x >> 16, cy = c.y >> 16;
    CHECK(w.box_select(0, cx - 8, cy - 8, cx + 8, cy + 8));
    const int k2 = w.find_unit(eid);
    REQUIRE(k2 >= 0);
    CHECK_FALSE(w.units()[size_t(k2)].selected);
    CHECK_FALSE(w.any_unit_selected(0));
}

TEST_CASE("golden: turret orders 0x1E / 0x1F put a selected turret in states 4 / 5 and it fires; Stop returns it to state 2 keeping the target (docs/re/38) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A7V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A7V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    int turret = -1, wall = -1, tank = -1;
    for (size_t i = 0; i < t.building_records.size(); ++i) {
        const auto& r = t.building_records[i];
        if (!(r.house_mask & 1)) continue;
        if (t.building_name[r.name_index] == "MEDIUM_GUN_TURRET" && turret < 0) turret = int(i);
        if (r.kind == 0xe && wall < 0) wall = int(i);
    }
    for (size_t i = 0; i < t.unit_records.size(); ++i)
        if (t.unit_name[t.unit_records[i].name_index] == "COMBAT_TANK" && (t.unit_records[i].house_mask & 1) && tank < 0) tank = int(i);
    REQUIRE(turret >= 0);
    REQUIRE(wall >= 0);
    REQUIRE(tank >= 0);
    // Open ground away from every base.
    int ox = -1, oy = -1;
    for (int y = 6; y < gm.height() - 6 && ox < 0; ++y)
        for (int x = 6; x < gm.width() - 6 && ox < 0; ++x) {
            bool ok = true;
            for (int dy = -3; dy <= 3 && ok; ++dy)
                for (int dx = -3; dx <= 3 && ok; ++dx) {
                    const uint32_t f = gm.at(x + dx, y + dy).flags;
                    ok = (f & 0x2000) && !(f & 0x3f8) && !(f & 0x10);
                }
            for (const auto& b : w.buildings())
                if (std::abs(b.cell_x - x) < 14 && std::abs(b.cell_y - y) < 14) ok = false;
            for (const auto& u : w.units())
                if (std::abs(int(u.cur_x) - x) < 10 && std::abs(int(u.cur_y) - y) < 10) ok = false;
            if (ok) {
                ox = x;
                oy = y;
            }
        }
    REQUIRE(ox >= 0);
    const int ti = w.create_building(0, turret, ox, oy, true);
    REQUIRE(ti >= 0);
    const int wi = w.create_building(0, wall, ox + 2, oy, true);
    REQUIRE(wi >= 0);
    const int ui = w.create_unit(0, tank, ox, oy + 2, ox, oy + 2);
    REQUIRE(ui >= 0);
    const int uid = w.units()[size_t(ui)].index;
    const int32_t tank_hp = w.units()[size_t(ui)].hit_points;
    REQUIRE(w.selected_turret(0) < 0);
    w.select_building_checked(size_t(ti));
    REQUIRE(w.selected_turret(0) == w.buildings()[size_t(ti)].index);
    // Order 0x1E on an own unit (Ctrl): state 4, it fires at it.
    w.order_turret_attack_unit(0, uid);
    CHECK(w.buildings()[size_t(ti)].state == 4);
    for (int i = 0; i < 120; ++i) w.tick();
    const int uk = w.find_unit(uid);
    CHECK((uk < 0 || w.units()[size_t(uk)].hit_points < tank_hp));
    // Stop: state 2, the target is kept.
    w.order_stop(0);
    CHECK(w.buildings()[size_t(ti)].state == 2);
    CHECK(w.buildings()[size_t(ti)].target_kind == 1);
    // Order 0x1F on an own wall: state 5, it fires at it.
    const int32_t wall_hp = w.buildings()[size_t(wi)].hit_points;
    w.order_turret_attack_building(0, wi);
    CHECK(w.buildings()[size_t(ti)].state == 5);
    for (int i = 0; i < 200; ++i) w.tick();
    CHECK((w.buildings()[size_t(wi)].removed || w.buildings()[size_t(wi)].hit_points < wall_hp));
    // The turret itself is never its own target.
    w.order_stop(0);
    w.order_turret_attack_building(0, ti);
    CHECK(w.buildings()[size_t(ti)].state == 2);
}

TEST_CASE("golden: a Fremen (kind 12) cloaks after its first update and uncloaks next to foreign infantry (docs/re/32) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A8V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A8V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    int fremen = -1, inf = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        const auto& r = t.unit_records[i];
        if (r.kind == 12 && fremen < 0) fremen = int(i);
        if (inf < 0 && r.infantry && r.kind == 0 && (r.house_mask & 2) && r.weapon != 0xff) inf = int(i);
    }
    REQUIRE(fremen >= 0);
    REQUIRE(inf >= 0);
    int ox = -1, oy = -1;
    for (int y = 6; y < gm.height() - 6 && ox < 0; ++y)
        for (int x = 6; x < gm.width() - 6 && ox < 0; ++x) {
            bool ok = true;
            for (int dy = -4; dy <= 4 && ok; ++dy)
                for (int dx = -4; dx <= 4 && ok; ++dx) {
                    const uint32_t f = gm.at(x + dx, y + dy).flags;
                    ok = (f & 0x2000) && !(f & 0x3f8) && !(f & 0x10);
                }
            for (const auto& b : w.buildings())
                if (std::abs(b.cell_x - x) < 14 && std::abs(b.cell_y - y) < 14) ok = false;
            for (const auto& u : w.units())
                if (std::abs(int(u.cur_x) - x) < 10 && std::abs(int(u.cur_y) - y) < 10) ok = false;
            if (ok) {
                ox = x;
                oy = y;
            }
        }
    REQUIRE(ox >= 0);
    const int fi = w.create_unit(0, fremen, ox, oy, ox, oy);
    REQUIRE(fi >= 0);
    const int fid = w.units()[size_t(fi)].index;
    CHECK((w.units()[size_t(fi)].flags & 0x10) != 0); // can cloak
    auto fr = [&]() -> const sim::Unit& { return w.units()[size_t(w.find_unit(fid))]; };
    w.tick();
    CHECK((fr().flags & 4) != 0); // cloaked on its first update, the fade-out starts
    CHECK(fr().fade == 22);
    for (int i = 0; i < 30; ++i) w.tick();
    CHECK((fr().flags & 4) != 0);
    CHECK(fr().fade == 0);
    // A Harkonnen (enemy) trooper two cells away does not see it and does not fire.
    const int ei = w.create_unit(1, inf, ox + 2, oy, ox + 2, oy);
    REQUIRE(ei >= 0);
    const int32_t hp = fr().hit_points;
    for (int i = 0; i < 60; ++i) w.tick();
    CHECK(fr().hit_points == hp);
    CHECK((fr().flags & 4) != 0);
    // Next to it: revealed (fade-in), then fired at.
    {
        auto& e = w.units()[size_t(w.find_unit(w.units()[size_t(ei)].index))];
        w.deselect_all();
        e.selected = true;
        w.order_move(1, ox + 1, oy);
    }
    bool revealed = false;
    for (int i = 0; i < 200 && !revealed; ++i) {
        w.tick();
        revealed = !(fr().flags & 4);
    }
    CHECK(revealed);
}

TEST_CASE("golden: A8V1 palace specials: the Fremen call-in brings 2 cloaking Fremen; the Ordos AI charges and calls in a saboteur (docs/re/39) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A8V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A8V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    w.set_script(mis);
    w.set_ai(mis);
    CHECK(w.vars.number_of_fremen == 2);
    const auto* ids = t.block(0x6d60d8);
    REQUIRE(ids);
    const int fremen = ids->bytes[0x15], saboteur = ids->bytes[0x14];
    CHECK(t.unit_records[size_t(fremen)].kind == 12);
    CHECK(t.unit_records[size_t(saboteur)].kind == 4);
    // The player's palace on open ground, and a ready Fremen slot.
    int palace = -1;
    for (size_t i = 0; i < t.building_records.size(); ++i)
        if (t.building_name[t.building_records[i].name_index] == "ATREIDES_PALACE" && (t.building_records[i].house_mask & 1)) palace = int(i);
    REQUIRE(palace >= 0);
    int ox = -1, oy = -1;
    for (int y = 6; y < gm.height() - 8 && ox < 0; ++y)
        for (int x = 6; x < gm.width() - 8 && ox < 0; ++x) {
            bool ok = true;
            for (int dy = -1; dy <= 4 && ok; ++dy)
                for (int dx = -1; dx <= 4 && ok; ++dx) {
                    const uint32_t f = gm.at(x + dx, y + dy).flags;
                    ok = (f & 0x2000) && !(f & 0x3f8) && !(f & 0x10);
                }
            for (const auto& b : w.buildings())
                if (std::abs(b.cell_x - x) < 14 && std::abs(b.cell_y - y) < 14) ok = false;
            for (const auto& u : w.units())
                if (std::abs(int(u.cur_x) - x) < 8 && std::abs(int(u.cur_y) - y) < 8) ok = false;
            if (ok) {
                ox = x;
                oy = y;
            }
        }
    REQUIRE(ox >= 0);
    REQUIRE(w.create_building(0, palace, ox, oy, true) >= 0);
    auto& slot = w.player(0).unit_slots[0];
    slot = sim::ProductionSlot{};
    slot.item = int16_t(fremen);
    slot.progress = sim::kProgressDone;
    w.order_special(0, 12, 0, 0);
    int n = 0;
    for (const auto& u : w.units())
        if (u.owner == 0 && u.record == fremen) {
            ++n;
            CHECK((u.flags & 0x10) != 0);
        }
    CHECK(n == 2);
    // The Ordos (side 2) AI: AutoStart charges its palace's saboteur; the
    // AI calls it in (case 1) once ready.
    int sab_tick = -1;
    for (int i = 0; i < 9000 && sab_tick < 0; ++i) {
        w.tick();
        for (const auto& u : w.units())
            if (u.owner == 2 && u.record == saboteur) sab_tick = i;
    }
    MESSAGE("Ordos saboteur at tick " << sab_tick);
    CHECK(sab_tick > 0);
}

TEST_CASE("golden: A8V1 the Deviator converts a vehicle to the attacker for about 1920 ticks, then it reverts (docs/re/40) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A8V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A8V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    int dev = -1, tank = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        const auto& n = t.unit_name[t.unit_records[i].name_index];
        if (n == "DEVIATOR" && dev < 0) dev = int(i);
        if (n == "COMBAT_TANK" && (t.unit_records[i].house_mask & 1) && tank < 0) tank = int(i);
    }
    REQUIRE(dev >= 0);
    REQUIRE(tank >= 0);
    CHECK((t.bullets[t.unit_records[size_t(dev)].weapon].flags & 0x40) != 0);
    int ox = -1, oy = -1;
    for (int y = 6; y < gm.height() - 6 && ox < 0; ++y)
        for (int x = 6; x < gm.width() - 10 && ox < 0; ++x) {
            bool ok = true;
            for (int dy = -2; dy <= 2 && ok; ++dy)
                for (int dx = -2; dx <= 6 && ok; ++dx) {
                    const uint32_t f = gm.at(x + dx, y + dy).flags;
                    ok = (f & 0x2000) && !(f & 0x3f8) && !(f & 0x10);
                }
            for (const auto& b : w.buildings())
                if (std::abs(b.cell_x - x) < 16 && std::abs(b.cell_y - y) < 16) ok = false;
            for (const auto& u : w.units())
                if (std::abs(int(u.cur_x) - x) < 12 && std::abs(int(u.cur_y) - y) < 12) ok = false;
            if (ok) {
                ox = x;
                oy = y;
            }
        }
    REQUIRE(ox >= 0);
    const int di = w.create_unit(2, dev, ox, oy, ox, oy);
    REQUIRE(di >= 0);
    const int did = w.units()[size_t(di)].index;
    const int ti = w.create_unit(0, tank, ox + 4, oy, ox + 4, oy);
    REQUIRE(ti >= 0);
    int conv_tick = -1, conv_id = -1;
    for (int i = 0; i < 600 && conv_tick < 0; ++i) {
        w.tick();
        for (const auto& u : w.units())
            if (u.record == tank && u.owner == 2 && (u.flags & 0x1000)) {
                conv_tick = i;
                conv_id = u.index;
                CHECK(u.orig_side == 0);
                CHECK(u.delivery_entry <= 0xf0);
            }
    }
    MESSAGE("converted at tick " << conv_tick);
    REQUIRE(conv_tick >= 0);
    // Keep the Deviator out of it, then wait for the revert.
    for (auto& u : w.units())
        if (u.index == did) u.remove = true;
    int back_tick = -1;
    for (int i = 0; i < 3000 && back_tick < 0; ++i) {
        w.tick();
        for (const auto& u : w.units())
            if (u.record == tank && u.owner == 0 && !u.remove && u.state != 0x11 && (u.flags & 0x80)) back_tick = i;
    }
    MESSAGE("reverted " << back_tick << " ticks later");
    CHECK(back_tick > 1800);
    CHECK(back_tick < 2100);
}

TEST_CASE("golden: A9V1 Death Hand: launched from the Harkonnen palace, hidden 21 updates, bursts into 40 bomblets near the target (docs/re/41) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A9V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A9V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    CHECK(w.vars.death_hand_accuracy == 3);
    const auto* ids = t.block(0x6d60d8);
    REQUIRE(ids);
    const int dh = ids->bytes[0x13];
    CHECK(t.unit_records[size_t(dh)].kind == 10);
    REQUIRE(w.player(1).primary[17] >= 0); // the Harkonnen palace
    // The player's construction yard as the target.
    int tb = -1;
    for (const auto& b : w.buildings())
        if (b.owner == 0 && t.building_records[size_t(b.record)].kind == 2) tb = b.index;
    REQUIRE(tb >= 0);
    const int32_t hp0 = w.buildings()[size_t(tb)].hit_points;
    auto& slot = w.player(1).unit_slots[0];
    slot = sim::ProductionSlot{};
    slot.item = int16_t(dh);
    slot.progress = sim::kProgressDone;
    w.order_special(1, 10, w.buildings()[size_t(tb)].cell_x + 1, w.buildings()[size_t(tb)].cell_y + 1);
    int mid = -1;
    for (const auto& u : w.units())
        if (u.record == dh) mid = u.index;
    REQUIRE(mid >= 0);
    {
        const auto& m = w.units()[size_t(w.find_unit(mid))];
        CHECK((m.flags & 0x220040) == 0x220040); // aircraft, Death Hand, hidden
        CHECK(m.altitude == 0x3c0000);
    }
    int visible = -1, burst = -1, gone = -1, bomblets = 0;
    const int bomb = t.unit_records[size_t(dh)].weapon;
    for (int i = 0; i < 2000 && gone < 0; ++i) {
        w.tick();
        const int k = w.find_unit(mid);
        if (k < 0) {
            gone = i;
            break;
        }
        const auto& m = w.units()[size_t(k)];
        if (visible < 0 && !(m.flags & 0x200000)) visible = i;
        if (burst < 0 && (m.flags & 0x8000)) {
            burst = i;
            for (const auto& b : w.bullets)
                if (b.type == bomb) ++bomblets;
        }
    }
    for (int i = 0; i < 80; ++i) w.tick(); // the bomblets land
    const int32_t hp1 = w.buildings()[size_t(tb)].removed ? 0 : w.buildings()[size_t(tb)].hit_points;
    MESSAGE("visible at " << visible << ", burst at " << burst << " with " << bomblets << " bomblets, gone at " << gone
                          << ", yard hp " << hp0 << " -> " << hp1);
    CHECK(visible == 20); // updated 21 times hidden (0..20), the flag clears on the 21st
    CHECK(burst > visible);
    CHECK(bomblets == 40);
    // 10 steps in state 0x11, removed in the 11th update: the zero test
    // comes before the decrement (0x499e31 ja 0x49a09a, docs/re/14 3.3).
    CHECK(gone == burst + 11);
    CHECK(hp1 < hp0);
}

TEST_CASE("golden: H1V1 / H1V2 the local player is slot 1 (gSideId): no AI on it, the difficulty and the HARVESTED goal follow it (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    for (const std::string name : {"H1V1", "H1V2"}) {
        auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        auto run = [&](bool to_goal) {
            sim::Rng rng(1);
            sim::GameMap gm(map, attr, tables, rng);
            auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
            sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
            w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
            w.local_player = 1;
            w.difficulty = 0; // easy: 75 % costs for the local player only
            w.init_players(mis);
            w.set_script(mis);
            w.set_ai(mis);
            CHECK(w.player(1).house == 1);
            CHECK(w.ai_u8(1, 1) == 0); // the player's slot has no AI
            bool yard = false;
            for (const auto& b : w.buildings()) yard = yard || (b.owner == 1 && t.building_records[size_t(b.record)].kind == 2);
            CHECK(yard);
            CHECK_FALSE(w.player(1).building_list.empty());
            const int rec = w.player(1).building_list[0];
            const int base = int(t.building_records[size_t(rec)].cost[0]);
            CHECK(w.building_cost(1, rec) == base * 75 / 100);
            CHECK(w.building_cost(0, rec) == base); // another slot: normal
            if (to_goal) w.player(1).cash = 2500; // HARVESTED reads the local player
            for (int i = 0; i < 12000 && !w.won && !w.lost; ++i) w.tick();
            const std::string label = name + (to_goal ? " at the goal" : " idle");
            MESSAGE(label << ": tick " << w.tick_count() << " won " << w.won
                         << " lost " << w.lost);
            return std::pair{w.won, w.lost};
        };
        const auto idle = run(false);
        CHECK_FALSE(idle.first);
        CHECK_FALSE(idle.second);
        const auto goal = run(true);
        CHECK(goal.first);
    }
}

TEST_CASE("golden: H2 / H3 (both versions) as slot 1: the Atreides base destroyed sets their AI BESERK, then base and units gone wins (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    for (const std::string name : {"H2V1", "H2V2", "H3V1", "H3V2"}) {
        auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.local_player = 1;
        w.init_players(mis);
        w.set_script(mis);
        w.set_ai(mis);
        CHECK(w.ai_u8(0, 1) == 1); // Atreides: AI
        CHECK(w.ai_u8(1, 1) == 0); // the player
        for (int i = 0; i < 200; ++i) w.tick();
        // The Atreides base goes: their buildings marked destroyed.
        for (size_t i = 0; i < w.buildings().size(); ++i)
            if (w.buildings()[i].owner == 0 && !w.buildings()[i].removed) w.destroy_building(int(i));
        int beserk_tick = -1;
        for (int i = 0; i < 400 && beserk_tick < 0; ++i) {
            w.tick();
            if (w.ai_u8(0, 0x1e) == 1) beserk_tick = i;
        }
        CHECK(beserk_tick >= 0);
        CHECK_FALSE(w.won); // their units are left
        for (auto& u : w.units())
            if (u.owner == 0) u.remove = true;
        int won_tick = -1;
        for (int i = 0; i < 600 && won_tick < 0 && !w.lost; ++i) {
            w.tick();
            if (w.won) won_tick = i;
        }
        MESSAGE(name << ": BESERK " << beserk_tick << " ticks after the base, won " << won_tick
                     << " ticks after the units");
        CHECK(won_tick >= 0);
        CHECK_FALSE(w.lost);
    }
}

TEST_CASE("golden: H4V1 as slot 1: Atreides plus two allied Fremen AI sides; BESERK per base, the win needs the Atreides and Fremen side 4 gone (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/H4V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_H4V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.local_player = 1;
    w.init_players(mis);
    w.set_script(mis);
    w.set_ai(mis);
    CHECK(w.player(3).house == 4);
    CHECK(w.player(4).house == 4);
    CHECK(w.alliance(1, 3) == 1); // the Fremen are the player's enemies
    CHECK(w.alliance(0, 3) == 0); // and the Atreides' allies
    for (int i = 0; i < 200; ++i) w.tick();
    // Scouting (12, 32) (REVEALED) brings Fremen side 4's FREMEN by
    // carryall: enemy units with the stealth ability (flag 0x10).
    w.reveal_circle(12, 32, 1);
    int fremen = 0;
    for (int i = 0; i < 3000 && fremen == 0; ++i) {
        w.tick();
        for (const auto& u : w.units())
            if (u.owner == 4 && !u.remove && u.state != 0x11 &&
                t.unit_name[t.unit_records[size_t(u.record)].name_index] == "FREMEN") {
                ++fremen;
                CHECK((u.flags & 0x10) != 0);
            }
    }
    CHECK(fremen > 0);
    auto destroy_base = [&](int side) {
        for (size_t i = 0; i < w.buildings().size(); ++i)
            if (w.buildings()[i].owner == side && !w.buildings()[i].removed) w.destroy_building(int(i));
    };
    auto remove_units = [&](int side) {
        for (auto& u : w.units())
            if (u.owner == side) u.remove = true;
    };
    auto run_until = [&](int n, auto done) {
        for (int i = 0; i < n; ++i) {
            w.tick();
            if (done()) return i;
        }
        return -1;
    };
    destroy_base(0);
    const int b0 = run_until(400, [&] { return w.ai_u8(0, 0x1e) == 1; });
    CHECK(b0 >= 0);
    CHECK(w.ai_u8(3, 0x1e) == 0);
    destroy_base(4);
    destroy_base(3);
    const int b34 = run_until(400, [&] { return w.ai_u8(3, 0x1e) == 1 && w.ai_u8(4, 0x1e) == 1; });
    CHECK(b34 >= 0);
    remove_units(0);
    CHECK(run_until(200, [&] { return w.won; }) < 0); // Fremen side 4 still has units
    remove_units(4);
    const int won = run_until(600, [&] { return w.won || w.lost; });
    MESSAGE("BESERK: Atreides " << b0 << ", Fremen " << b34 << "; won " << won << " ticks after side 4's units");
    CHECK(won >= 0);
    CHECK(w.won);
    CHECK_FALSE(w.lost);
}

TEST_CASE("golden: H5V1 as slot 1: the SP_OUTPOST intel, the Emperor frigate once it is lost, BESERK for both Ordos sides and the win (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    const auto* ids = t.block(0x6d60d8);
    REQUIRE(ids);
    const int frigate = ids->bytes[0x0e];
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/H5V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_H5V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.local_player = 1;
    w.init_players(mis);
    w.set_script(mis);
    w.set_ai(mis);
    int outpost = -1;
    for (const auto& b : w.buildings())
        if (b.owner == 1 && b.record == 60) outpost = b.index;
    REQUIRE(outpost >= 0); // the player starts with the SP_OUTPOST (record 60)
    // Nobody plays: keep the player's base standing (else the AI wins
    // before the 8000-tick timer).
    auto run_until = [&](int n, auto done) {
        for (int i = 0; i < n; ++i) {
            w.tick();
            for (auto& b : w.buildings())
                if (b.owner == 1 && !b.removed) b.hit_points = t.building_records[size_t(b.record)].hit_points;
            if (done()) return i;
        }
        return -1;
    };
    // While the player holds it, no Emperor frigate even after tick 8000.
    auto frigate_for = [&](int side) {
        for (const auto& u : w.units())
            if (u.owner == side && u.record == frigate && !u.remove) return true;
        return false;
    };
    CHECK(run_until(8100, [&] { return frigate_for(3) || w.lost; }) < 0);
    // The outpost lost: the Starport frigate comes for the Emperor.
    for (size_t i = 0; i < w.buildings().size(); ++i)
        if (w.buildings()[i].index == outpost) w.destroy_building(int(i));
    bool starport = false;
    for (const auto& b : w.buildings()) starport = starport || (b.owner == 3 && !b.removed && b.record == 47);
    CHECK(starport);
    bool msg620 = false;
    const int fr = run_until(8500, [&] { // TIMER % 8000: the next is at tick 16000
        for (const auto& m : w.messages) msg620 = msg620 || m.text == 620;
        return frigate_for(3);
    });
    MESSAGE("Emperor frigate " << fr << " ticks after the outpost, message 620 " << msg620 << ", tick " << w.tick_count());
    CHECK(fr >= 0);
    auto destroy_base = [&](int side) {
        for (size_t i = 0; i < w.buildings().size(); ++i)
            if (w.buildings()[i].owner == side && !w.buildings()[i].removed) w.destroy_building(int(i));
    };
    auto remove_units = [&](int side) {
        for (auto& u : w.units())
            if (u.owner == side) u.remove = true;
    };
    destroy_base(2);
    destroy_base(4);
    const int bz = run_until(400, [&] { return w.ai_u8(2, 0x1e) == 1 && w.ai_u8(4, 0x1e) == 1; });
    CHECK(bz >= 0);
    remove_units(2);
    remove_units(4);
    CHECK(run_until(200, [&] { return w.won; }) < 0); // the Emperor's units are left
    remove_units(3);
    const int won = run_until(600, [&] { return w.won || w.lost; });
    MESSAGE("BESERK " << bz << "; won " << won << " ticks after the Emperor's units");
    CHECK(w.won);
    CHECK_FALSE(w.lost);
}

TEST_CASE("golden: H6V1 / H6V2 as slot 1: the neutral Smugglers go BESERK without their refinery; destroying the Ordos Starport wins (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    for (const std::string name : {"H6V1", "H6V2"}) {
        auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.local_player = 1;
        w.init_players(mis);
        w.set_script(mis);
        w.set_ai(mis);
        const std::string label = name;
        CHECK(w.player(5).house == 5);
        CHECK(w.alliance(1, 5) == 2); // the Smugglers are neutral
        CHECK(w.alliance(5, 1) == 2);
        for (int i = 0; i < 200; ++i) w.tick();
        auto run_until = [&](int n, auto done) {
            for (int i = 0; i < n; ++i) {
                w.tick();
                if (done()) return i;
            }
            return -1;
        };
        auto destroy = [&](int side, auto keep) {
            for (size_t i = 0; i < w.buildings().size(); ++i) {
                const auto& b = w.buildings()[i];
                if (b.owner == side && !b.removed && !keep(b.record)) w.destroy_building(int(i));
            }
        };
        // The Smugglers' refinery (record 22) gone: BESERK for side 5 only.
        CHECK(w.ai_u8(5, 0x1e) == 0);
        destroy(5, [](int r) { return r != 22; });
        const int b5 = run_until(100, [&] { return w.ai_u8(5, 0x1e) == 1; });
        CHECK(b5 >= 0);
        CHECK(w.ai_u8(2, 0x1e) == 0);
        // Both Ordos bases down except the Starport (record 48): no win yet.
        destroy(4, [](int) { return false; });
        destroy(2, [](int r) { return r == 48; });
        CHECK(run_until(200, [&] { return w.won || w.lost; }) < 0);
        CHECK(w.ai_u8(2, 0x1e) == 0); // side 2 still has a building
        // The Starport gone: the win, and BESERK for both Ordos sides.
        destroy(2, [](int) { return false; });
        const int won = run_until(100, [&] { return w.won || w.lost; });
        CHECK(w.won);
        CHECK_FALSE(w.lost);
        // BESERK waits for the base-destroyed flags (16-tick defeat check).
        const int bz = run_until(100, [&] { return w.ai_u8(2, 0x1e) == 1 && w.ai_u8(4, 0x1e) == 1; });
        MESSAGE(label << ": Smugglers BESERK " << b5 << ", won " << won << " ticks after the Starport, Ordos BESERK " << bz << " later");
        CHECK(bz >= 0);
    }
}

TEST_CASE("golden: H7V1 as slot 1: no base, the engineer must capture a Construction Yard; the Sonic Tank / Sardaukar reinforcements; BESERK and the win (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/H7V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_H7V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto unit_name = [&](int rec) { return t.unit_name[t.unit_records[size_t(rec)].name_index]; };
    // capture = false: the engineer dies first; true: it takes side 4's yard.
    for (const bool capture : {false, true}) {
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.local_player = 1;
        w.init_players(mis);
        w.set_script(mis);
        w.set_ai(mis);
        const std::string label = capture ? "captured" : "engineer lost";
        REQUIRE(unit_name(2) == "ENGINEER"); // the UNITEXISTS condition's record
        for (const auto& b : w.buildings()) CHECK(b.owner != 1); // no base at the start
        for (int i = 0; i < 100; ++i) w.tick();
        CHECK_FALSE(w.lost);
        if (capture) {
            int yard = -1;
            for (size_t i = 0; i < w.buildings().size(); ++i)
                if (w.buildings()[i].owner == 4 && !w.buildings()[i].removed && w.buildings()[i].record == 0) yard = int(i);
            REQUIRE(yard >= 0);
            CHECK(w.capture_building(4, 1, yard));
            w.tick();
        }
        for (auto& u : w.units())
            if (u.owner == 1 && u.record == 2) u.remove = true;
        auto run_until = [&](int n, auto done) {
            for (int i = 0; i < n; ++i) {
                w.tick();
                // Nobody plays: keep the player's base standing.
                for (auto& b : w.buildings())
                    if (b.owner == 1 && !b.removed) b.hit_points = t.building_records[size_t(b.record)].hit_points;
                if (done()) return i;
            }
            return -1;
        };
        const int lost = run_until(200, [&] { return w.lost; });
        MESSAGE(label << ": lost " << lost);
        if (!capture) {
            CHECK(lost >= 0);
            continue;
        }
        CHECK(lost < 0);
        // While side 0 has its AT_HIGH_TECH_FACTORY (record 32), on TIMER % 6500
        // a Sonic Tank for the Atreides and Sardaukar for the Emperor arrive.
        auto count = [&](int side, const char* n) {
            int c = 0;
            for (const auto& u : w.units())
                if (u.owner == side && !u.remove && u.state != 0x11 && unit_name(u.record) == n) ++c;
            return c;
        };
        const int sonic0 = count(0, "SONIC_TANK"), sard0 = count(3, "SARDAUKAR");
        run_until(int(6500 - w.tick_count()), [] { return false; });
        const int got = run_until(2000, [&] { return count(0, "SONIC_TANK") > sonic0 && count(3, "SARDAUKAR") > sard0; });
        MESSAGE("Sonic Tank / Sardaukar " << got << " ticks after 6500");
        CHECK(got >= 0);
        auto destroy_base = [&](int side) {
            for (size_t i = 0; i < w.buildings().size(); ++i)
                if (w.buildings()[i].owner == side && !w.buildings()[i].removed) w.destroy_building(int(i));
        };
        auto remove_units = [&](int side) {
            for (auto& u : w.units())
                if (u.owner == side) u.remove = true;
        };
        // BESERK for the three Atreides sides needs all their bases gone.
        destroy_base(0);
        destroy_base(4);
        CHECK(run_until(200, [&] { return w.ai_u8(0, 0x1e) == 1; }) < 0);
        destroy_base(5);
        const int bz = run_until(200, [&] { return w.ai_u8(0, 0x1e) == 1 && w.ai_u8(4, 0x1e) == 1 && w.ai_u8(5, 0x1e) == 1; });
        CHECK(bz >= 0);
        CHECK(w.ai_u8(3, 0x1e) == 0);
        for (int s : {0, 4, 5}) remove_units(s);
        CHECK(run_until(200, [&] { return w.won; }) < 0); // the Emperor is left
        destroy_base(3);
        CHECK(run_until(200, [&] { return w.ai_u8(3, 0x1e) == 1; }) >= 0);
        remove_units(3);
        const int won = run_until(600, [&] { return w.won || w.lost; });
        MESSAGE("BESERK " << bz << "; won " << won << " ticks after the Emperor's units");
        CHECK(w.won);
        CHECK_FALSE(w.lost);
    }
}

TEST_CASE("golden: Devastator self-destruct: order 0x2F, a sticky 101-tick countdown with sparks, then the 5000 plasma blast that spares no one (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/H7V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_H7V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    // The special indices the state reads (block 0x6d60d8).
    const auto* ids = t.block(0x6d60d8);
    REQUIRE(ids);
    CHECK(t.bullet_name[ids->bytes[0x33]] == "DEVASTATOR");
    CHECK(t.explosion_name[ids->bytes[0x51]] == "EX_DEVSPARK1");
    CHECK(t.explosion_name[ids->bytes[0x53]] == "EX_DEVSPARK3");
    CHECK(t.explosion_name[ids->bytes[0x54]] == "EX_DEVDEATH");
    auto record = [&](const char* n) {
        for (size_t i = 0; i < t.unit_records.size(); ++i)
            if (t.unit_name[t.unit_records[i].name_index] == n) return int(i);
        return -1;
    };
    const int dev = record("DEVASTATOR"), inf = record("LIGHT_INF"), tank = record("COMBAT_TANK");
    REQUIRE(dev == 17);
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    sim::World w(gm, t, tables, speed, {}, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.local_player = 1;
    w.init_players(mis);
    CHECK(w.vars.devastator_explode_delay == 100); // VARS.BIN +0x43
    int cx = -1, cy = -1;
    for (int y = 6; y < w.map().height() - 6 && cx < 0; ++y) {
        for (int x = 6; x < w.map().width() - 6 && cx < 0; ++x) {
            bool ok = true;
            for (int dy = -5; dy <= 5; ++dy)
                for (int dx = -5; dx <= 5; ++dx) {
                    const uint32_t f = w.map().at(x + dx, y + dy).flags;
                    if (!(f & sim::cell::kVehicles) || (f & 0x3f8)) ok = false;
                }
            if (ok) {
                cx = x;
                cy = y;
            }
        }
    }
    REQUIRE(cx >= 0);
    const int dp = w.create_unit(1, dev, cx, cy, cx, cy);
    REQUIRE(dp >= 0);
    const int dev_id = w.units()[size_t(dp)].index;
    // An own infantryman (no friendly-fire check), an enemy tank next to
    // it, and a tank out of the 96 px radius.
    const int own = w.units()[size_t(w.create_unit(1, inf, cx + 1, cy, cx + 1, cy))].index;
    const int near = w.units()[size_t(w.create_unit(0, tank, cx, cy + 1, cx, cy + 1))].index;
    const int far = w.units()[size_t(w.create_unit(0, tank, cx + 5, cy + 5, cx + 5, cy + 5))].index;
    auto find = [&](int id) -> const sim::Unit& {
        for (const auto& u : w.units())
            if (u.index == id) return u;
        FAIL("unit gone");
        return w.units().front();
    };
    w.tick();
    int hp = find(dev_id).hit_points; // the enemy tank shoots it meanwhile
    const int lost = w.player(1).units_lost;
    w.order_deploy(1, dev_id);
    CHECK(find(dev_id).state == 0x1f);
    // Sticky: a move order is refused, the countdown cannot be cancelled.
    for (auto& u : w.units()) u.selected = u.index == dev_id;
    w.order_move(1, cx - 4, cy);
    CHECK(find(dev_id).state == 0x1f);
    int ticks = 0;
    size_t effects = 0;
    while (find(dev_id).state == 0x1f && ticks < 300) {
        w.tick();
        ++ticks;
        if (find(dev_id).state == 0x1f) {
            effects = std::max(effects, w.effects.size());
            hp = find(dev_id).hit_points;
        }
    }
    MESSAGE("detonated after " << ticks << " ticks");
    CHECK(ticks == 101);
    CHECK(effects > 0); // the sparks
    const auto& d = find(dev_id);
    CHECK(d.state == 0x11);
    CHECK(d.hit_points == hp); // not a KillUnit: hp kept from the last countdown tick
    CHECK(w.player(1).units_lost == lost + 1 + (find(own).state == 0x11 ? 1 : 0));
    CHECK(find(own).state == 0x11);
    CHECK(find(near).state == 0x11);
    CHECK(find(far).state != 0x11);
}

TEST_CASE("golden: Grenadier death: GRENDEATH from the cell corner in the 5th state-0x11 update, own units hit, chains; removal after countdown + 1 updates (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/H7V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_H7V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    // Bullet 0x21 (pushed as an immediate at 0x499e1b) is GRENDEATH: 150, W_HE.
    REQUIRE(t.bullets.size() > 0x21);
    CHECK(t.bullet_name[0x21] == "GRENDEATH");
    CHECK(t.bullets[0x21].damage == 150);
    CHECK(t.bullets[0x21].warhead == 13); // W_HE, radius 64
    auto record = [&](const char* n) {
        for (size_t i = 0; i < t.unit_records.size(); ++i)
            if (t.unit_name[t.unit_records[i].name_index] == n) return int(i);
        return -1;
    };
    const int gren = record("GRENADIER"), inf = record("LIGHT_INF");
    REQUIRE(gren == 27);
    int lmg = -1;
    for (size_t k = 0; k < t.bullet_name.size(); ++k)
        if (t.bullet_name[k] == "M_LMG") lmg = int(k); // radius 15: kills the Grenadier only
    REQUIRE(lmg >= 0);
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    sim::World w(gm, t, tables, speed, {}, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.local_player = 1;
    w.init_players(mis);
    int cx = -1, cy = -1;
    for (int y = 6; y < w.map().height() - 6 && cx < 0; ++y) {
        for (int x = 6; x < w.map().width() - 6 && cx < 0; ++x) {
            bool ok = true;
            for (int dy = -5; dy <= 5; ++dy)
                for (int dx = -5; dx <= 5; ++dx) {
                    const uint32_t f = w.map().at(x + dx, y + dy).flags;
                    if (!(f & sim::cell::kInfantry) || (f & 0x3f8)) ok = false;
                }
            if (ok) {
                cx = x;
                cy = y;
            }
        }
    }
    REQUIRE(cx >= 0);
    auto make = [&](int rec, int x, int y) { return w.units()[size_t(w.create_unit(1, rec, x, y, x, y))].index; };
    auto find = [&](int id) -> sim::Unit* {
        for (auto& u : w.units())
            if (u.index == id) return &u;
        return nullptr;
    };
    // g1 dies first; g2 one cell east (≈ 50 px from g1's cell corner) dies
    // in g1's blast and blasts itself; `last` two cells east is out of g1's
    // 64 px but inside g2's; `far` is out of both.
    const int g1 = make(gren, cx, cy), g2 = make(gren, cx + 1, cy);
    const int last = make(inf, cx + 2, cy), far = make(inf, cx - 3, cy);
    auto dist = [&](int id, int bx, int by) {
        const sim::Unit* u = find(id);
        const double dx = (u->x >> 16) - bx, dy = (u->y >> 16) - by;
        return std::sqrt(dx * dx + dy * dy);
    };
    REQUIRE(dist(g2, cx * 32, cy * 32) < 64);
    REQUIRE(dist(last, cx * 32, cy * 32) >= 64);
    REQUIRE(dist(last, (cx + 1) * 32, cy * 32) < 64);
    REQUIRE(dist(far, cx * 32, cy * 32) >= 64);
    find(g2)->hit_points = 1;
    find(last)->hit_points = 1;
    const int far_hp = find(far)->hit_points;
    const sim::Unit* u1 = find(g1);
    find(g1)->hit_points = 1;
    w.damage_at(u1->x >> 16, u1->y >> 16, 0, lmg, 0, -1);
    REQUIRE(find(g1)->state == 0x11);
    CHECK(find(g1)->countdown == 0x32);
    CHECK(find(g2)->state != 0x11);
    for (int k = 1; k <= 4; ++k) {
        w.tick();
        CHECK(find(g2)->state != 0x11); // +0x2f 0x32 .. 0x2f: no blast yet
    }
    w.tick(); // the 5th update sees 0x2e
    CHECK(find(g2)->state == 0x11);
    CHECK(find(last)->state != 0x11);
    // g2 comes after g1 in the unit list, so its first state-0x11 update
    // was in that same tick: its 5th is 4 ticks later.
    for (int k = 0; k < 3; ++k) {
        w.tick();
        CHECK(find(last)->state != 0x11);
    }
    w.tick();
    CHECK(find(last)->state == 0x11);
    w.tick();
    CHECK(find(far)->hit_points == far_hp);
    // Removal: g1 has had 10 updates (countdown 0x28); the zero test
    // comes before the decrement, so it goes in its 51st update.
    CHECK(find(g1)->countdown == 0x32 - 10);
    for (int k = 10; k < 50; ++k) w.tick();
    REQUIRE(find(g1));
    CHECK(find(g1)->countdown == 0);
    w.tick();
    CHECK(find(g1) == nullptr);
}

TEST_CASE("golden: the radar image is event-driven: start colours, buildings, spice eaten to 0; other cell changes wait for a load (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    namespace rc = sim::radar_colour;
    // FUN_00462140 at the start: the colour by flags.
    auto by_flags = [&](int x, int y) {
        const uint32_t f = gm.at(x, y).flags;
        if (f & 0x10) return uint8_t(f & 7);
        if (f & 0x700000) return rc::kSpice;
        if (f & 0x10000) return rc::kSand;
        if (f & 0x2000) return rc::kDrivable;
        return rc::kOther;
    };
    for (int y = 0; y < gm.height(); ++y)
        for (int x = 0; x < gm.width(); ++x) REQUIRE(gm.at(x, y).radar == by_flags(x, y));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    sim::World w(gm, t, tables, speed, placed, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    auto& pl = w.player(0);
    // Map buildings: their footprint in the owner colour (0x44de4f).
    for (const auto& b : w.buildings()) {
        const auto& r = t.building_records[size_t(b.record)];
        for (int k = 0; k < 16; ++k)
            if (r.footprint & (1u << k)) CHECK(gm.at(b.cell_x + k % 4, b.cell_y + k / 4).radar == b.owner);
    }
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
    const int wind = place("WIND_TRAP");
    const auto& wb = w.buildings()[size_t(wind)];
    const auto& wr = t.building_records[size_t(wb.record)];
    const int wx = wb.cell_x, wy = wb.cell_y;
    for (int k = 0; k < 16; ++k)
        if (wr.footprint & (1u << k)) CHECK(gm.at(wx + k % 4, wy + k / 4).radar == 0);
    const int ref = place("REFINERY");
    for (int i = 0; i < 300; ++i) w.tick();
    // A sand cell given spice behind the radar's back keeps its pixel.
    int sx = -1, sy = -1;
    for (int y = 0; y < gm.height() && sx < 0; ++y)
        for (int x = 0; x < gm.width() && sx < 0; ++x)
            if (gm.at(x, y).radar == rc::kSand && !(gm.at(x, y).flags & 0x700000) && (gm.at(x, y).flags & 0x10000)) {
                sx = x;
                sy = y;
            }
    REQUIRE(sx >= 0);
    // The wind trap destroyed: its cells become RGB(60, 50, 50) (0x44e0a9).
    w.destroy_building(wind);
    for (int i = 0; i < 40; ++i) w.tick();
    for (int k = 0; k < 16; ++k)
        if (wr.footprint & (1u << k)) CHECK(gm.at(wx + k % 4, wy + k / 4).radar == rc::kDrivable);
    // A harvester: every cell eaten to 0 turns sand, the others stay spice.
    std::vector<uint32_t> level0(size_t(gm.width() * gm.height()));
    for (int y = 0; y < gm.height(); ++y)
        for (int x = 0; x < gm.width(); ++x) level0[size_t(y * gm.width() + x)] = gm.at(x, y).flags & 0x700000;
    const auto& rb = w.buildings()[size_t(ref)];
    const auto& rr = t.building_records[size_t(rb.record)];
    const int dx = rb.cell_x + rr.spawn_x, dy = rb.cell_y + rr.spawn_y;
    const int pos = w.create_unit(0, t.unit_record(t.unit_index("HARVESTER"), pl.house), dx, dy, dx, dy);
    REQUIRE(pos >= 0);
    for (int i = 0; i < 6000; ++i) w.tick();
    int emptied = 0, eaten = 0;
    for (int y = 0; y < gm.height(); ++y) {
        for (int x = 0; x < gm.width(); ++x) {
            const uint32_t before = level0[size_t(y * gm.width() + x)], now = gm.at(x, y).flags & 0x700000;
            if (before != 0 && now == 0) {
                ++emptied;
                CHECK(gm.at(x, y).radar == rc::kSand);
            } else if (before != 0 && now != before) {
                ++eaten;
                CHECK(gm.at(x, y).radar == rc::kSpice);
            }
        }
    }
    MESSAGE(emptied << " cells emptied, " << eaten << " eaten down");
    CHECK(emptied > 0);
    gm.at(sx, sy).flags |= 0x200000;
    w.tick();
    CHECK(gm.at(sx, sy).radar == rc::kSand); // not recomputed from the flags
    // A load re-colours every cell from its flags (FUN_004418b0 0x441db6).
    const auto saved = w.save_state();
    REQUIRE(w.load_state(saved));
    CHECK(gm.at(sx, sy).radar == rc::kSpice);
    for (int k = 0; k < 16; ++k)
        if (wr.footprint & (1u << k)) CHECK(gm.at(wx + k % 4, wy + k / 4).radar == by_flags(wx + k % 4, wy + k / 4));
}

TEST_CASE("golden: H8V1 as slot 1: capturing the Mercenary Heavy Factory turns the Mercenaries; the Ordos Palace makes peace with the Atreides; the win needs the Ordos only (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/H8V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_H8V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.local_player = 1;
    w.init_players(mis);
    w.set_script(mis);
    w.set_ai(mis);
    auto find_building = [&](int side, int record) {
        for (size_t i = 0; i < w.buildings().size(); ++i)
            if (w.buildings()[i].owner == side && !w.buildings()[i].removed && w.buildings()[i].record == record) return int(i);
        return -1;
    };
    for (int i = 0; i < 100; ++i) w.tick();
    // The Mercenaries (side 6) start allied with the Atreides and Ordos.
    CHECK(w.alliance(6, 0) == 0);
    CHECK(w.alliance(6, 2) == 0);
    CHECK(w.alliance(6, 1) == 1);
    // Their Heavy Factory (record 44) captured: they join the player.
    const int hf = find_building(6, 44);
    REQUIRE(hf >= 0);
    CHECK(w.capture_building(6, 1, hf));
    for (int i = 0; i < 50; ++i) w.tick();
    CHECK(w.alliance(6, 1) == 0);
    CHECK(w.alliance(1, 6) == 0);
    CHECK(w.alliance(6, 0) == 1);
    CHECK(w.alliance(0, 6) == 1);
    CHECK(w.alliance(6, 2) == 1);
    CHECK(w.alliance(2, 6) == 1);
    // The Ordos Palace (record 58) gone: the Atreides make peace with the
    // player (friendly to it, neutral from the player) and the Mercenaries.
    const int pal = find_building(2, 58);
    REQUIRE(pal >= 0);
    w.destroy_building(pal);
    for (int i = 0; i < 200; ++i) w.tick();
    CHECK(w.alliance(0, 1) == 0);
    CHECK(w.alliance(1, 0) == 2);
    CHECK(w.alliance(0, 6) == 0);
    CHECK(w.alliance(6, 0) == 0);
    // Then on TIMER % 1000 the Atreides turn neutral to the player.
    const uint32_t next = (w.tick_count() / 1000 + 1) * 1000;
    while (w.tick_count() < next + 10) w.tick();
    CHECK(w.alliance(0, 1) == 2);
    // The win needs the Ordos only.
    for (size_t i = 0; i < w.buildings().size(); ++i)
        if (w.buildings()[i].owner == 2 && !w.buildings()[i].removed) w.destroy_building(int(i));
    for (int i = 0; i < 200; ++i) w.tick();
    CHECK(w.ai_u8(2, 0x1e) == 1);
    CHECK_FALSE(w.won);
    for (auto& u : w.units())
        if (u.owner == 2) u.remove = true;
    int won = -1;
    for (int i = 0; i < 600 && won < 0 && !w.lost; ++i) {
        w.tick();
        if (w.won) won = i;
    }
    MESSAGE("won " << won << " ticks after the Ordos units");
    CHECK(w.won);
    CHECK_FALSE(w.lost);
}

TEST_CASE("golden: H9V1 / H9V2 as slot 1: BESERK per group (Atreides, Emperor), the win needs both groups gone; H9V2's neutral Smugglers (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    struct Case {
        const char* name;
        std::vector<int> atreides, emperor;
    };
    for (const Case& c : {Case{"H9V1", {0, 4, 5}, {2, 3}}, Case{"H9V2", {0, 4}, {3, 6}}}) {
        const std::string name = c.name;
        auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.local_player = 1;
        w.init_players(mis);
        w.set_script(mis);
        w.set_ai(mis);
        for (int i = 0; i < 100; ++i) w.tick();
        auto run_until = [&](int n, auto done) {
            for (int i = 0; i < n; ++i) {
                w.tick();
                if (done()) return i;
            }
            return -1;
        };
        auto destroy = [&](int side, int only_record) {
            for (size_t i = 0; i < w.buildings().size(); ++i) {
                const auto& b = w.buildings()[i];
                if (b.owner == side && !b.removed && (only_record < 0 || b.record == only_record)) w.destroy_building(int(i));
            }
        };
        auto remove_units = [&](int side) {
            for (auto& u : w.units())
                if (u.owner == side) u.remove = true;
        };
        auto beserk = [&](const std::vector<int>& sides) {
            for (int s : sides)
                if (w.ai_u8(s, 0x1e) != 1) return false;
            return true;
        };
        if (name == "H9V2") {
            // The Smugglers are neutral; their refinery (record 22) gone: BESERK.
            CHECK(w.alliance(1, 5) == 2);
            CHECK(w.alliance(5, 0) == 2);
            destroy(5, 22);
            CHECK(run_until(100, [&] { return w.ai_u8(5, 0x1e) == 1; }) >= 0);
        }
        for (int s : c.atreides) destroy(s, -1);
        const int ba = run_until(200, [&] { return beserk(c.atreides); });
        CHECK(ba >= 0);
        CHECK(w.ai_u8(c.emperor[0], 0x1e) == 0);
        for (int s : c.atreides) remove_units(s);
        CHECK(run_until(200, [&] { return w.won || w.lost; }) < 0); // the Emperor is left
        for (int s : c.emperor) destroy(s, -1);
        const int be = run_until(200, [&] { return beserk(c.emperor); });
        CHECK(be >= 0);
        for (int s : c.emperor) remove_units(s);
        const int won = run_until(600, [&] { return w.won || w.lost; });
        MESSAGE(name << ": BESERK Atreides " << ba << ", Emperor " << be << "; won " << won);
        CHECK(w.won);
        CHECK_FALSE(w.lost);
    }
}

TEST_CASE("golden: O1V1 / O1V2 the local player is slot 2 (gSideId): no AI on it, the difficulty and the HARVESTED goal follow it (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    for (const std::string name : {"O1V1", "O1V2"}) {
        auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        auto run = [&](bool to_goal) {
            sim::Rng rng(1);
            sim::GameMap gm(map, attr, tables, rng);
            auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
            sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
            w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
            w.local_player = 2;
            w.difficulty = 0; // easy: 75 % costs for the local player only
            w.init_players(mis);
            w.set_script(mis);
            w.set_ai(mis);
            CHECK(w.player(2).house == 2);
            CHECK(w.ai_u8(2, 1) == 0); // the player's slot has no AI
            bool yard = false;
            for (const auto& b : w.buildings()) yard = yard || (b.owner == 2 && t.building_records[size_t(b.record)].kind == 2);
            CHECK(yard);
            CHECK_FALSE(w.player(2).building_list.empty());
            const int rec = w.player(2).building_list[0];
            const int base = int(t.building_records[size_t(rec)].cost[0]);
            CHECK(w.building_cost(2, rec) == base * 75 / 100);
            CHECK(w.building_cost(0, rec) == base); // another slot: normal
            if (to_goal) w.player(2).cash = 2500; // HARVESTED reads the local player
            for (int i = 0; i < 12000 && !w.won && !w.lost; ++i) w.tick();
            const std::string label = name + (to_goal ? " at the goal" : " idle");
            MESSAGE(label << ": tick " << w.tick_count() << " won " << w.won
                         << " lost " << w.lost);
            return std::pair{w.won, w.lost};
        };
        const auto idle = run(false);
        CHECK_FALSE(idle.first);
        CHECK_FALSE(idle.second);
        const auto goal = run(true);
        CHECK(goal.first);
    }
}

TEST_CASE("golden: O2V1 / O2V2 as slot 2: the Harkonnen base destroyed sets their AI BESERK, then base and units gone wins; O2V1 Ordos reinforcement at 8000 (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    for (const std::string name : {"O2V1", "O2V2"}) {
        auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.local_player = 2;
        w.init_players(mis);
        w.set_script(mis);
        w.set_ai(mis);
        CHECK(w.ai_u8(1, 1) == 1); // Harkonnen: AI
        CHECK(w.ai_u8(2, 1) == 0); // the player
        auto count = [&](int owner) {
            int n = 0;
            for (const auto& u : w.units()) n += u.owner == owner && !u.remove;
            return n;
        };
        if (name == "O2V1") {
            // Event 8: DEPLOY (a carryall) LIGHT_INF LIGHT_INF RAIDER for side 2 on INTERVAL 8000.
            for (int i = 0; i < 7990; ++i) w.tick();
            std::set<int> seen;
            for (const auto& u : w.units()) seen.insert(u.index);
            std::map<std::string, int> dropped;
            // Dropped units start in state 7 at the drop cell; survivors of a
            // destroyed building (the AI may raze the idle player's yard) start
            // in state 8 and are not counted.
            for (int i = 0; i < 600; ++i) {
                w.tick();
                for (const auto& u : w.units()) {
                    if (u.owner != 2 || !seen.insert(u.index).second) continue;
                    if (u.state != 7) continue; // the carryall flies in state 7 too
                    ++dropped[t.unit_name[t.unit_records[size_t(u.record)].name_index]];
                }
            }
            MESSAGE("O2V1 after tick 8000: CARRYALL " << dropped["CARRYALL"] << ", LIGHT_INF "
                                                      << dropped["LIGHT_INF"] << ", RAIDER " << dropped["RAIDER"]);
            CHECK(dropped["LIGHT_INF"] == 2);
            CHECK(dropped["RAIDER"] == 1);
        } else {
            for (int i = 0; i < 200; ++i) w.tick();
        }
        if (w.lost) { // the idle player can be overrun first: nothing more to check
            MESSAGE(name << ": the idle player lost first");
            continue;
        }
        for (size_t i = 0; i < w.buildings().size(); ++i)
            if (w.buildings()[i].owner == 1 && !w.buildings()[i].removed) w.destroy_building(int(i));
        int beserk_tick = -1;
        for (int i = 0; i < 400 && beserk_tick < 0; ++i) {
            w.tick();
            if (w.ai_u8(1, 0x1e) == 1) beserk_tick = i;
        }
        CHECK(beserk_tick >= 0);
        CHECK_FALSE(w.won); // their units are left
        for (auto& u : w.units())
            if (u.owner == 1) u.remove = true;
        int won_tick = -1;
        for (int i = 0; i < 600 && won_tick < 0 && !w.lost; ++i) {
            w.tick();
            if (w.won) won_tick = i;
        }
        MESSAGE(name << ": BESERK " << beserk_tick << " ticks after the base, won " << won_tick
                     << " ticks after the units");
        CHECK(won_tick >= 0);
        CHECK_FALSE(w.lost);
    }
}

TEST_CASE("golden: O3V1 / O3V2 as slot 2: ADDUNITS for the Harkonnen at tick 5, the upgrade hint once for the barracks or light factory, their harvester replaced, BESERK and the win (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    for (const std::string name : {"O3V1", "O3V2"}) {
        auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.local_player = 2;
        w.init_players(mis);
        w.set_script(mis);
        w.set_ai(mis);
        CHECK(w.ai_u8(1, 1) == 1); // Harkonnen: AI
        CHECK(w.ai_u8(2, 1) == 0); // the player
        auto unit_name = [&](const sim::Unit& u) { return t.unit_name[t.unit_records[size_t(u.record)].name_index]; };
        auto count = [&](int owner, const char* n) {
            int c = 0;
            for (const auto& u : w.units()) c += u.owner == owner && !u.remove && u.state != 0x11 && unit_name(u) == n;
            return c;
        };

        // Event 0: ADDUNITS on TIMER == 5, straight onto the map (no carryall).
        const int li = count(1, "LIGHT_INF"), quad = count(1, "QUAD"), trike = count(1, "TRIKE"),
                  trooper = count(1, "TROOPER");
        for (int i = 0; i < 10; ++i) w.tick();
        CHECK(count(1, "LIGHT_INF") == li + 2);
        CHECK(count(1, "QUAD") == quad + 2);
        CHECK(count(1, "TRIKE") == trike + 2);
        CHECK(count(1, "TROOPER") == trooper + 2);

        // Events 7-10: message 245 (A3V1UpgradeMessage) once, for the Ordos
        // BARRACKS (record 15) or LIGHT_FACTORY (record 37); flag 11 / 22 blocks the second.
        auto shown = [&] {
            int c = 0;
            for (const auto& m : w.messages) c += m.text == 245;
            return c;
        };
        auto place = [&](int rec) {
            std::vector<sim::PlaceCell> cells;
            for (int y = 1; y < gm.height(); ++y)
                for (int x = 0; x < gm.width(); ++x)
                    if (w.placement_check(2, rec, x, y, cells)) return w.create_building(2, rec, x, y, false);
            return -1;
        };
        CHECK(shown() == 0);
        REQUIRE(place(15) >= 0);
        for (int i = 0; i < 10; ++i) w.tick();
        CHECK(shown() == 1);
        for (int i = 0; i < 300; ++i) w.tick(); // the line times out (200)
        CHECK(shown() == 0);
        // O3V2 starts with a lone yard on cramped rock: no legal spot for the
        // light factory yet, so the second building is checked in O3V1 only.
        if (place(37) >= 0) {
            for (int i = 0; i < 10; ++i) w.tick();
            CHECK(shown() == 0);
        } else {
            CHECK(name == "O3V2");
        }

        // Events 4-6: the Harkonnen harvester is replaced by carryall on TIMER % 500.
        for (auto& u : w.units())
            if (u.owner == 1 && unit_name(u) == "HARVESTER") u.remove = true;
        int back = -1;
        for (int i = 0; i < 2000 && back < 0 && !w.lost; ++i) {
            w.tick();
            if (count(1, "HARVESTER") > 0) back = i;
        }
        MESSAGE(name << ": Harkonnen harvester back after " << back << " ticks");
        if (w.lost) {
            MESSAGE(name << ": the idle player lost first");
            continue;
        }
        CHECK(back >= 0);

        for (size_t i = 0; i < w.buildings().size(); ++i)
            if (w.buildings()[i].owner == 1 && !w.buildings()[i].removed) w.destroy_building(int(i));
        int beserk_tick = -1;
        for (int i = 0; i < 400 && beserk_tick < 0; ++i) {
            w.tick();
            if (w.ai_u8(1, 0x1e) == 1) beserk_tick = i;
        }
        CHECK(beserk_tick >= 0);
        CHECK_FALSE(w.won);
        for (auto& u : w.units())
            if (u.owner == 1) u.remove = true;
        int won_tick = -1;
        for (int i = 0; i < 600 && won_tick < 0 && !w.lost; ++i) {
            w.tick();
            if (w.won) won_tick = i;
        }
        MESSAGE(name << ": BESERK " << beserk_tick << " ticks after the base, won " << won_tick
                     << " ticks after the units");
        CHECK(won_tick >= 0);
        CHECK_FALSE(w.lost);
    }
}

TEST_CASE("golden: O4V1 as slot 2: ADDUNITS at tick 1, the large force at 10000, Harkonnen BESERK without a win; capturing the SP_OUTPOST wins, losing it loses (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/O4V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_O4V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    for (const bool capture : {true, false}) {
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.local_player = 2;
        w.init_players(mis);
        w.set_script(mis);
        w.set_ai(mis);
        CHECK(w.ai_u8(1, 1) == 1);
        CHECK(w.ai_u8(3, 1) == 1);
        CHECK(w.ai_u8(5, 1) == 1);
        CHECK(w.ai_u8(2, 1) == 0);
        auto find_building = [&](int side, int record) {
            for (size_t i = 0; i < w.buildings().size(); ++i)
                if (w.buildings()[i].owner == side && !w.buildings()[i].removed && w.buildings()[i].record == record) return int(i);
            return -1;
        };
        auto count = [&](int owner) {
            int c = 0;
            for (const auto& u : w.units()) c += u.owner == owner && !u.remove && u.state != 0x11;
            return c;
        };
        // Events 0 / 1: ADDUNITS on TIMER == 1 (4 Harkonnen units, 7 for side 5).
        const int h = count(1), s5 = count(5);
        for (int i = 0; i < 5; ++i) w.tick();
        CHECK(count(1) - h >= 4); // the AI may add a carryall
        CHECK(count(5) - s5 >= 7);
        CHECK(count(5) - s5 <= 8);

        if (!capture) {
            // Event 6: the SP_OUTPOST (record 61) held by nobody: LOSE.
            const int op = find_building(5, 61);
            REQUIRE(op >= 0);
            w.destroy_building(op);
            for (int i = 0; i < 300 && !w.lost; ++i) w.tick();
            CHECK(w.lost);
            CHECK_FALSE(w.won);
            continue;
        }

        // Events 13-17 on TIMER == 10000: three Harkonnen drops, four
        // LIGHT_INF for the player, "WARNING: Large force approaching!".
        for (int i = 5; i < 9995; ++i) w.tick();
        bool warned = false;
        for (int i = 0; i < 20; ++i) {
            w.tick();
            for (const auto& m : w.messages) warned |= m.text == 621;
        }
        CHECK(warned);
        if (w.lost) {
            MESSAGE("O4V1: the idle player lost first");
            continue;
        }
        // Events 9-11: the Harkonnen base (side 1) gone: sides 1 and 3 BESERK. No win.
        for (size_t i = 0; i < w.buildings().size(); ++i)
            if (w.buildings()[i].owner == 1 && !w.buildings()[i].removed) w.destroy_building(int(i));
        int beserk = -1;
        for (int i = 0; i < 400 && beserk < 0; ++i) {
            w.tick();
            if (w.ai_u8(1, 0x1e) == 1 && w.ai_u8(3, 0x1e) == 1) beserk = i;
        }
        CHECK(beserk >= 0);
        CHECK_FALSE(w.won);
        // Event 12: the player owns the SP_OUTPOST: WIN.
        const int op = find_building(5, 61);
        REQUIRE(op >= 0);
        CHECK(w.capture_building(5, 2, op));
        int won_tick = -1;
        for (int i = 0; i < 300 && won_tick < 0 && !w.lost; ++i) {
            w.tick();
            if (w.won) won_tick = i;
        }
        MESSAGE("O4V1: BESERK " << beserk << " ticks after the base, won " << won_tick << " ticks after the capture");
        CHECK(won_tick >= 0);
        CHECK_FALSE(w.lost);
    }
}

TEST_CASE("golden: O5V1 as slot 2: capture the Atreides STAR_PORT for a frigate MCV; losing the engineers or the Starport loses; BESERK for the four Atreides sides; HARVESTED 15000 wins (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/O5V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_O5V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    enum Run { kCapture, kEngineers, kStarport };
    for (const Run run : {kCapture, kEngineers, kStarport}) {
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.local_player = 2;
        w.init_players(mis);
        w.set_script(mis);
        w.set_ai(mis);
        for (int s : {0, 3, 4, 5}) CHECK(w.ai_u8(s, 1) == 1);
        CHECK(w.ai_u8(2, 1) == 0);
        auto unit_name = [&](const sim::Unit& u) { return t.unit_name[t.unit_records[size_t(u.record)].name_index]; };
        auto count = [&](int owner, const char* n) {
            int c = 0;
            for (const auto& u : w.units())
                c += u.owner == owner && !u.remove && u.state != 0x11 && (n == nullptr || unit_name(u) == n);
            return c;
        };
        auto find_building = [&](int side, int record) {
            for (size_t i = 0; i < w.buildings().size(); ++i)
                if (w.buildings()[i].owner == side && !w.buildings()[i].removed && w.buildings()[i].record == record) return int(i);
            return -1;
        };
        CHECK(count(2, "ENGINEER") == 3);
        CHECK(count(2, "MCV") == 0);
        // Events 0-2: ADDUNITS on TIMER == 1: 9 for side 0, 7 each for sides 4 and 3.
        const int a0 = count(0, nullptr), a3 = count(3, nullptr), a4 = count(4, nullptr);
        for (int i = 0; i < 5; ++i) w.tick();
        CHECK(count(0, nullptr) - a0 >= 9);
        CHECK(count(3, nullptr) - a3 >= 7);
        CHECK(count(4, nullptr) - a4 >= 7);
        // Event 13: "Capture the Atreides Starport to receive an MCV." on TIMER == 100.
        bool intro = false;
        for (int i = 0; i < 110; ++i) {
            w.tick();
            for (const auto& m : w.messages) intro |= m.text == 353;
        }
        CHECK(intro);
        const int sp = find_building(5, 46);
        REQUIRE(sp >= 0);

        if (run == kEngineers) { // event 12: no ENGINEER and the Starport never taken
            for (auto& u : w.units())
                if (u.owner == 2 && unit_name(u) == "ENGINEER") u.remove = true;
            for (int i = 0; i < 300 && !w.lost; ++i) w.tick();
            CHECK(w.lost);
            continue;
        }
        if (run == kStarport) { // event 10: the Starport gone and no MCV
            w.destroy_building(sp);
            for (int i = 0; i < 300 && !w.lost; ++i) w.tick();
            CHECK(w.lost);
            continue;
        }

        // Events 7-9: the player owns the Starport: a frigate brings an MCV (REINFORCE).
        CHECK(w.capture_building(5, 2, sp));
        int mcv = -1;
        for (int i = 0; i < 3000 && mcv < 0 && !w.lost; ++i) {
            w.tick();
            if (count(2, "MCV") > 0) mcv = i;
        }
        MESSAGE("O5V1: the MCV is out " << mcv << " ticks after the capture");
        CHECK(mcv >= 0);
        CHECK_FALSE(w.lost);
        // The engineers may now all go: flag 27 (Starport taken) keeps event 12 off.
        for (auto& u : w.units())
            if (u.owner == 2 && unit_name(u) == "ENGINEER") u.remove = true;
        for (int i = 0; i < 100; ++i) w.tick();
        CHECK_FALSE(w.lost);

        // Events 14-17: all four Atreides bases gone: BESERK for each.
        for (size_t i = 0; i < w.buildings().size(); ++i) {
            const int o = w.buildings()[i].owner;
            if ((o == 0 || o == 3 || o == 4 || o == 5) && !w.buildings()[i].removed) w.destroy_building(int(i));
        }
        int beserk = -1;
        for (int i = 0; i < 400 && beserk < 0; ++i) {
            w.tick();
            if (w.ai_u8(0, 0x1e) == 1 && w.ai_u8(3, 0x1e) == 1 && w.ai_u8(4, 0x1e) == 1 && w.ai_u8(5, 0x1e) == 1)
                beserk = i;
        }
        CHECK(beserk >= 0);
        CHECK_FALSE(w.won);
        // Event 20: HARVESTED 15000 (the local player's credits) wins.
        w.player(2).cash = 15000;
        int won_tick = -1;
        for (int i = 0; i < 300 && won_tick < 0 && !w.lost; ++i) {
            w.tick();
            if (w.won) won_tick = i;
        }
        MESSAGE("O5V1: BESERK " << beserk << " ticks after the bases, won " << won_tick << " ticks after the goal");
        CHECK(won_tick >= 0);
        CHECK_FALSE(w.lost);
    }
}

TEST_CASE("golden: O6V1 / O6V2 as slot 2: the 30:00 clock, frigates to the player's Starport after 45000, losing the Starport loses, BESERK per side, the win needs both houses gone (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    for (const std::string name : {"O6V1", "O6V2"}) {
        auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        // The frigate's load (event 18 / event 34).
        const std::map<std::string, int> load = name == "O6V1"
            ? std::map<std::string, int>{{"DEVIATOR", 2}, {"MISSILE_TANK", 3}, {"SIEGE_TANK", 2}, {"COMBAT_TANK", 1}}
            : std::map<std::string, int>{{"COMBAT_TANK", 2}, {"MISSILE_TANK", 2}, {"SIEGE_TANK", 1}, {"DEVIATOR", 2}};
        const int first_drop = name == "O6V1" ? 5000 : 3000; // the first "Enemy reinforcements have arrived."
        for (const bool lose : {false, true}) {
            sim::Rng rng(1);
            sim::GameMap gm(map, attr, tables, rng);
            auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
            sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
            w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
            w.local_player = 2;
            w.init_players(mis);
            w.set_script(mis);
            w.set_ai(mis);
            CHECK(w.mission_timer == 45000); // 30:00 on the clock, display only
            CHECK(w.ai_u8(0, 1) == 1);
            CHECK(w.ai_u8(1, 1) == 1);
            CHECK(w.ai_u8(2, 1) == 0);
            auto unit_name = [&](const sim::Unit& u) { return t.unit_name[t.unit_records[size_t(u.record)].name_index]; };
            auto count = [&](int owner) {
                int c = 0;
                for (const auto& u : w.units()) c += u.owner == owner && !u.remove && u.state != 0x11;
                return c;
            };
            auto find_building = [&](int side, int record) {
                for (size_t i = 0; i < w.buildings().size(); ++i)
                    if (w.buildings()[i].owner == side && !w.buildings()[i].removed && w.buildings()[i].record == record) return int(i);
                return -1;
            };
            auto shown = [&](uint32_t text) {
                for (const auto& m : w.messages)
                    if (m.text == text) return true;
                return false;
            };
            // ADDUNITS on TIMER == 1 for both enemies.
            const int a0 = count(0), a1 = count(1);
            for (int i = 0; i < 5; ++i) w.tick();
            CHECK(count(0) - a0 >= (name == "O6V1" ? 5 : 6));
            CHECK(count(1) - a1 >= (name == "O6V1" ? 6 : 7));

            if (lose) {
                // The first enemy drop comes with its warning line.
                bool warned = false;
                for (int i = 5; i < first_drop + 10; ++i) {
                    w.tick();
                    warned |= shown(623);
                }
                CHECK(warned);
                // LOSE when the player no longer owns its STAR_PORT (record 48).
                const int sp = find_building(2, 48);
                if (sp < 0) { // already overrun while idle
                    CHECK(w.lost);
                    continue;
                }
                CHECK_FALSE(w.lost);
                w.destroy_building(sp);
                for (int i = 0; i < 300 && !w.lost; ++i) w.tick();
                CHECK(w.lost);
                continue;
            }

            // Skip to just before 45000: every TIMER counter equals the tick count.
            for (auto& c : w.script_conditions())
                if (c.type == 3) c.b = 44990;
            REQUIRE(find_building(2, 48) >= 0);
            std::set<int> seen;
            for (const auto& u : w.units()) seen.insert(u.index);
            std::map<std::string, int> got;
            bool deliveries = false;
            for (int i = 0; i < 2000; ++i) {
                w.tick();
                deliveries |= shown(622);
                for (const auto& u : w.units())
                    if (u.owner == 2 && seen.insert(u.index).second) ++got[unit_name(u)];
            }
            CHECK(deliveries); // "Deliveries beginning to arrive. Massive reinforcements expected!"
            for (const auto& [n, k] : load) {
                INFO(name << " " << n);
                CHECK(got[n] == k);
            }
            CHECK_FALSE(w.lost);

            // BESERK per house when its base goes; WIN once both are annihilated.
            for (size_t i = 0; i < w.buildings().size(); ++i) {
                const int o = w.buildings()[i].owner;
                if ((o == 0 || o == 1) && !w.buildings()[i].removed) w.destroy_building(int(i));
            }
            int beserk = -1;
            for (int i = 0; i < 400 && beserk < 0; ++i) {
                w.tick();
                if (w.ai_u8(0, 0x1e) == 1 && w.ai_u8(1, 0x1e) == 1) beserk = i;
            }
            CHECK(beserk >= 0);
            CHECK_FALSE(w.won);
            for (auto& u : w.units())
                if (u.owner == 0 || u.owner == 1) u.remove = true;
            int won_tick = -1;
            for (int i = 0; i < 600 && won_tick < 0 && !w.lost; ++i) {
                w.tick();
                if (w.won) won_tick = i;
            }
            MESSAGE(name << ": BESERK " << beserk << " ticks after the bases, won " << won_tick << " ticks after the units");
            CHECK(won_tick >= 0);
            CHECK_FALSE(w.lost);
        }
    }
}

TEST_CASE("golden: O7V1 as slot 2: allied Mercenary frigates; their CASUALTIES warn, then end the alliance and the frigates; BESERK per group; the win needs the Atreides and the Emperor gone (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/O7V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_O7V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.local_player = 2;
    w.init_players(mis);
    w.set_script(mis);
    w.set_ai(mis);
    for (int s : {0, 1, 3, 4, 6}) CHECK(w.ai_u8(s, 1) == 1);
    CHECK(w.ai_u8(2, 1) == 0);
    CHECK(w.alliance(6, 2) == 0); // the Mercenaries start as the player's allies
    CHECK(w.alliance(2, 6) == 0);
    auto unit_name = [&](const sim::Unit& u) { return t.unit_name[t.unit_records[size_t(u.record)].name_index]; };
    auto shown = [&](uint32_t text) {
        for (const auto& m : w.messages)
            if (m.text == text) return true;
        return false;
    };
    // Mercenary units that appear after now, by name.
    auto watch = [&](int ticks) {
        std::set<int> seen;
        for (const auto& u : w.units()) seen.insert(u.index);
        std::map<std::string, int> got;
        for (int i = 0; i < ticks; ++i) {
            w.tick();
            for (const auto& u : w.units())
                if (u.owner == 6 && seen.insert(u.index).second) ++got[unit_name(u)];
        }
        return got;
    };
    auto skip_to = [&](uint32_t tick) {
        for (auto& c : w.script_conditions())
            if (c.type == 3) c.b = tick;
    };
    for (int i = 0; i < 5; ++i) w.tick();

    // Event 31: REINFORCE side 6 on TIMER % 5000 while it owns its STAR_PORT (record 48).
    skip_to(4990);
    auto got = watch(2000);
    MESSAGE("O7V1 Mercenary units after 5000: TROOPER " << got["TROOPER"] << ", QUAD " << got["QUAD"] << ", TRIKE "
                                                        << got["TRIKE"] << ", FRIGATE " << got["FRIGATE"]);
    CHECK(got["FRIGATE"] == 1);
    CHECK(got["TROOPER"] >= 4); // the AI may train more
    CHECK(got["QUAD"] >= 2);
    CHECK(got["TRIKE"] >= 2);

    // Events 25-26: CASUALTIES side 6, more than 20 lost and lost / killed > 2.2: the warning.
    auto& merc = w.player(6);
    merc.units_lost = 21;
    merc.units_killed = 9; // 2.33
    for (int i = 0; i < 5; ++i) w.tick();
    CHECK(shown(624)); // "Mercenary Leader: We're losing too many men!"
    CHECK(w.alliance(6, 2) == 0);
    // Events 27-30: more than 25 lost and > 3.5: they withdraw, both ways neutral.
    merc.units_lost = 26;
    merc.units_killed = 7; // 3.71
    for (int i = 0; i < 5; ++i) w.tick();
    CHECK(shown(625));
    CHECK(w.alliance(6, 2) == 2);
    CHECK(w.alliance(2, 6) == 2);
    // Flag 24 now blocks their frigates.
    skip_to(9990);
    got = watch(2000);
    CHECK(got["FRIGATE"] == 0);
    CHECK_FALSE(w.lost);

    // The Emperor (side 3) base gone: BESERK side 3. The three Atreides bases: BESERK 0, 4, 1.
    for (size_t i = 0; i < w.buildings().size(); ++i)
        if (w.buildings()[i].owner == 3 && !w.buildings()[i].removed) w.destroy_building(int(i));
    for (int i = 0; i < 400 && w.ai_u8(3, 0x1e) != 1; ++i) w.tick();
    CHECK(w.ai_u8(3, 0x1e) == 1);
    CHECK(w.ai_u8(0, 0x1e) != 1);
    for (size_t i = 0; i < w.buildings().size(); ++i) {
        const int o = w.buildings()[i].owner;
        if ((o == 0 || o == 1 || o == 4) && !w.buildings()[i].removed) w.destroy_building(int(i));
    }
    int beserk = -1;
    for (int i = 0; i < 400 && beserk < 0; ++i) {
        w.tick();
        if (w.ai_u8(0, 0x1e) == 1 && w.ai_u8(1, 0x1e) == 1 && w.ai_u8(4, 0x1e) == 1) beserk = i;
    }
    CHECK(beserk >= 0);
    CHECK_FALSE(w.won);
    // The win: the Atreides (0, 1, 4) and the Emperor (3) annihilated; the Mercenaries do not count.
    for (auto& u : w.units())
        if (u.owner == 0 || u.owner == 1 || u.owner == 3 || u.owner == 4) u.remove = true;
    int won_tick = -1;
    for (int i = 0; i < 600 && won_tick < 0 && !w.lost; ++i) {
        w.tick();
        if (w.won) won_tick = i;
    }
    MESSAGE("O7V1: Atreides BESERK " << beserk << " ticks after their bases, won " << won_tick << " ticks after the units");
    CHECK(won_tick >= 0);
    CHECK_FALSE(w.lost);
}

TEST_CASE("golden: O8V1 as slot 2: Mercenary and Emperor frigates; the Mercenary CASUALTIES warning and withdrawal; BESERK per house; the win needs the Atreides, Harkonnen and Emperor gone (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/O8V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_O8V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.local_player = 2;
    w.init_players(mis);
    w.set_script(mis);
    w.set_ai(mis);
    for (int s : {0, 1, 3, 6}) CHECK(w.ai_u8(s, 1) == 1);
    CHECK(w.ai_u8(2, 1) == 0);
    CHECK(w.alliance(6, 2) == 0);
    CHECK(w.alliance(2, 6) == 0);
    auto unit_name = [&](const sim::Unit& u) { return t.unit_name[t.unit_records[size_t(u.record)].name_index]; };
    auto shown = [&](uint32_t text) {
        for (const auto& m : w.messages)
            if (m.text == text) return true;
        return false;
    };
    // New units of `side` over `ticks`, by name.
    auto watch = [&](int side, int ticks) {
        std::set<int> seen;
        for (const auto& u : w.units()) seen.insert(u.index);
        std::map<std::string, int> got;
        for (int i = 0; i < ticks; ++i) {
            w.tick();
            for (const auto& u : w.units())
                if (u.owner == side && seen.insert(u.index).second) ++got[unit_name(u)];
        }
        return got;
    };
    auto skip_to = [&](uint32_t tick) {
        for (auto& c : w.script_conditions())
            if (c.type == 3) c.b = tick;
    };
    for (int i = 0; i < 5; ++i) w.tick();

    // Event 28: Mercenary frigate on TIMER % 7500 while they own their STAR_PORT (48).
    skip_to(7490);
    auto merc = watch(6, 1200);
    CHECK(merc["FRIGATE"] == 1);
    CHECK(merc["TROOPER"] >= 6);
    CHECK(merc["SIEGE_TANK"] >= 2);
    // Event 30: Emperor frigate (3 SARDAUKAR) on TIMER % 9000 while side 3 owns its STAR_PORT (47).
    skip_to(8990);
    auto emp = watch(3, 1200);
    MESSAGE("O8V1 frigates: Mercenary TROOPER " << merc["TROOPER"] << " SIEGE_TANK " << merc["SIEGE_TANK"]
                                                << "; Emperor SARDAUKAR " << emp["SARDAUKAR"]);
    CHECK(emp["FRIGATE"] == 1);
    CHECK(emp["SARDAUKAR"] >= 3);

    // Events 22-27: CASUALTIES side 6. Killed = 1 makes the ratio the loss count.
    const auto& warn = mis.conditions[19];
    const auto& quit = mis.conditions[20];
    MESSAGE("O8V1 CASUALTIES: warn > " << warn.c << " ratio " << warn.ratio << ", withdraw > " << quit.c << " ratio "
                                       << quit.ratio << "; allegiance " << int(mis.events[25].allegiance) << " / "
                                       << int(mis.events[26].allegiance));
    auto& m = w.player(6);
    m.units_lost = int32_t(warn.c) + 1;
    m.units_killed = 1;
    REQUIRE(float(m.units_lost) > warn.ratio);
    for (int i = 0; i < 5; ++i) w.tick();
    CHECK(shown(624));
    CHECK_FALSE(shown(625));
    CHECK(w.alliance(6, 2) == 0);
    m.units_lost = int32_t(quit.c) + 1;
    REQUIRE(float(m.units_lost) > quit.ratio);
    for (int i = 0; i < 5; ++i) w.tick();
    CHECK(shown(625));
    CHECK(w.alliance(6, 2) == mis.events[25].allegiance);
    CHECK(w.alliance(2, 6) == mis.events[26].allegiance);
    CHECK(w.alliance(6, 2) == 2);
    skip_to(14990); // the next Mercenary frigate time (15000 % 7500), now blocked by flag 28
    merc = watch(6, 1200);
    CHECK(merc["FRIGATE"] == 0);
    CHECK_FALSE(w.lost);

    // BESERK per house as its base goes; the win needs all three annihilated.
    const std::pair<int, uint32_t> houses[] = {{0, 602}, {1, 600}, {3, 606}};
    for (const auto& [side, text] : houses) {
        for (size_t i = 0; i < w.buildings().size(); ++i)
            if (w.buildings()[i].owner == side && !w.buildings()[i].removed) w.destroy_building(int(i));
        for (int i = 0; i < 400 && w.ai_u8(side, 0x1e) != 1; ++i) w.tick();
        CHECK(w.ai_u8(side, 0x1e) == 1);
        for (auto& u : w.units())
            if (u.owner == side) u.remove = true;
        bool annihilated = false;
        for (int i = 0; i < 100 && !annihilated; ++i) {
            w.tick();
            annihilated = shown(text);
        }
        INFO("side " << side);
        CHECK(annihilated);
        if (side != 3) CHECK_FALSE(w.won);
    }
    for (int i = 0; i < 300 && !w.won && !w.lost; ++i) w.tick();
    CHECK(w.won);
    CHECK_FALSE(w.lost);
}

TEST_CASE("golden: O9V1 / O9V2 as slot 2: Mercenary and Emperor frigates; the Mercenary withdrawal also befriends the enemy houses (one way); BESERK and the annihilated line per house; the win needs the Atreides, Harkonnen and Emperor gone (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    for (const std::string name : {"O9V1", "O9V2"}) {
        const bool v2 = name == "O9V2";
        auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.local_player = 2;
        w.init_players(mis);
        w.set_script(mis);
        w.set_ai(mis);
        for (int s : {0, 1, 3, 4, 6}) CHECK(w.ai_u8(s, 1) == 1);
        CHECK(w.ai_u8(2, 1) == 0);
        CHECK(w.alliance(6, 2) == 0);
        CHECK(w.alliance(2, 6) == 0);
        auto unit_name = [&](const sim::Unit& u) { return t.unit_name[t.unit_records[size_t(u.record)].name_index]; };
        auto shown = [&](uint32_t text) {
            for (const auto& m : w.messages)
                if (m.text == text) return true;
            return false;
        };
        auto skip_to = [&](uint32_t tick) {
            for (auto& c : w.script_conditions())
                if (c.type == 3) c.b = tick;
        };
        for (int i = 0; i < 5; ++i) w.tick();

        // Frigates for the Mercenaries (6, STAR_PORT 48) and the Emperor (3, STAR_PORT 47)
        // on the same TIMER % 5021 (V1) / 5000 (V2).
        skip_to(v2 ? 4990 : 5011);
        std::set<int> seen;
        for (const auto& u : w.units()) seen.insert(u.index);
        std::map<std::string, int> merc, emp;
        for (int i = 0; i < 1500; ++i) {
            w.tick();
            for (const auto& u : w.units()) {
                if (!seen.insert(u.index).second) continue;
                if (u.owner == 6) ++merc[unit_name(u)];
                if (u.owner == 3) ++emp[unit_name(u)];
            }
        }
        MESSAGE(name << " frigates: Mercenary TROOPER " << merc["TROOPER"] << " SIEGE_TANK " << merc["SIEGE_TANK"]
                     << "; Emperor SARDAUKAR " << emp["SARDAUKAR"]);
        CHECK(merc["FRIGATE"] == 1);
        CHECK(merc["TROOPER"] >= (v2 ? 5 : 6));
        CHECK(merc["SIEGE_TANK"] >= 2);
        CHECK(emp["FRIGATE"] == 1);
        CHECK(emp["SARDAUKAR"] >= (v2 ? 5 : 6));

        // CASUALTIES side 6: the warning, then the withdrawal (killed = 1: ratio = losses).
        std::vector<const formats::MissionCondition*> cas;
        for (const auto& c : mis.conditions)
            if (c.type == 4) cas.push_back(&c);
        REQUIRE(cas.size() == 2);
        auto& m = w.player(6);
        m.units_killed = 1;
        m.units_lost = int32_t(cas[0]->c) + 1;
        REQUIRE(float(m.units_lost) > cas[0]->ratio);
        for (int i = 0; i < 5; ++i) w.tick();
        CHECK(shown(624));
        CHECK_FALSE(shown(625));
        m.units_lost = int32_t(cas[1]->c) + 1;
        REQUIRE(float(m.units_lost) > cas[1]->ratio);
        for (int i = 0; i < 5; ++i) w.tick();
        CHECK(shown(625));
        CHECK(w.alliance(6, 2) == 2);
        CHECK(w.alliance(2, 6) == 2);
        // ... and they befriend the enemy houses, who still treat them as enemies.
        for (int s : {0, 1, 3}) {
            INFO("side " << s);
            CHECK(w.alliance(6, s) == 0);
            CHECK(w.alliance(s, 6) == 1);
        }
        CHECK(w.alliance(6, 4) == (v2 ? 0 : 1));
        CHECK_FALSE(w.lost);

        // BESERK and the annihilated line per house. O9V2's Emperor line also
        // needs side 4's base gone (no BESERK for side 4 in the script).
        const std::vector<std::pair<std::vector<int>, uint32_t>> houses = {
            {{0}, 602}, {{1}, 600}, {v2 ? std::vector<int>{3, 4} : std::vector<int>{3}, 606}};
        for (const auto& [sides, text] : houses) {
            for (size_t i = 0; i < w.buildings().size(); ++i)
                if (std::find(sides.begin(), sides.end(), w.buildings()[i].owner) != sides.end() &&
                    !w.buildings()[i].removed)
                    w.destroy_building(int(i));
            for (int i = 0; i < 400 && w.ai_u8(sides[0], 0x1e) != 1; ++i) w.tick();
            INFO(name << " side " << sides[0]);
            CHECK(w.ai_u8(sides[0], 0x1e) == 1);
            for (auto& u : w.units())
                if (u.owner == sides[0]) u.remove = true;
            bool annihilated = false;
            for (int i = 0; i < 100 && !annihilated; ++i) {
                w.tick();
                annihilated = shown(text);
            }
            CHECK(annihilated);
            if (text != 606) CHECK_FALSE(w.won);
        }
        for (int i = 0; i < 300 && !w.won && !w.lost; ++i) w.tick();
        CHECK(w.won); // the Mercenaries (604) do not count
        CHECK_FALSE(w.lost);
    }
}

TEST_CASE("golden: O8V1 shroud: allied (Mercenary) units reveal for the player, enemy units do not (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/O8V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_O8V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    sim::World w(gm, t, tables, speed, placed, rng, circles, {}, 2);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.local_player = 2;
    w.init_players(mis);
    REQUIRE(w.alliance(6, 2) == 0);
    REQUIRE(w.alliance(0, 2) == 1);
    w.reveal_start(); // FUN_0049f4d0: 8 sweeps of FUN_0044cff0, then the local player's buildings
    int merc = 0, enemy_hidden = 0;
    for (const auto& u : w.units()) {
        if (t.unit_records[size_t(u.record)].view == 0 || (u.flags & 0x100)) continue;
        if (u.owner == 6) {
            ++merc;
            CHECK_FALSE(w.shrouded(u.to_x, u.to_y));
        }
        if (u.owner == 0) enemy_hidden += w.shrouded(u.to_x, u.to_y);
    }
    // The Atreides (side 0) base far away stays hidden: only the local
    // player's pre-placed buildings reveal (the World knows slot 2 from the start).
    int atreides_buildings_open = 0;
    for (const auto& b : w.buildings())
        if (b.owner == 0) atreides_buildings_open += !w.shrouded((b.x >> 16) >> 5, ((b.y >> 16) >> 5) - 1);
    CHECK(atreides_buildings_open == 0);
    MESSAGE("O8V1: " << merc << " Mercenary units revealed, " << enemy_hidden << " Atreides units still under shroud");
    CHECK(merc > 0);
    CHECK(enemy_hidden > 0);
}

TEST_CASE("golden: O8V1 a selected turret ordered at an allied unit / building (orders 0x1E / 0x1F) shoots it (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/O8V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_O8V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars, 2);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    REQUIRE(w.alliance(2, 6) == 0);
    // A Mercenary unit, and an Ordos MEDIUM_GUN_TURRET (record 25) next to it.
    int merc = -1;
    for (size_t i = 0; i < w.units().size(); ++i)
        if (w.units()[i].owner == 6 && !t.unit_records[size_t(w.units()[i].record)].infantry) merc = int(i);
    if (merc < 0)
        for (size_t i = 0; i < w.units().size(); ++i)
            if (w.units()[i].owner == 6) merc = int(i);
    REQUIRE(merc >= 0);
    const int mx = w.units()[size_t(merc)].cur_x, my = w.units()[size_t(merc)].cur_y;
    const int tu = w.create_building(2, 25, mx + 2, my, true);
    REQUIRE(tu >= 0);
    w.select_building(size_t(tu));
    REQUIRE(w.selected_turret(2) == tu);
    const int merc_id = w.units()[size_t(merc)].index;
    const int hp0 = w.units()[size_t(merc)].hit_points;
    w.order_turret_attack_unit(2, merc_id);
    int hp = hp0;
    for (int i = 0; i < 300; ++i) {
        w.tick();
        const int mi = w.find_unit(merc_id);
        hp = mi < 0 ? 0 : w.units()[size_t(mi)].hit_points;
        if (hp < hp0) break;
    }
    MESSAGE("O8V1 turret vs allied unit: hp " << hp0 << " -> " << hp);
    CHECK(hp < hp0);
    // An allied (Mercenary) building next to the turret: order 0x1F.
    const int mb = w.create_building(6, 12, mx + 2, my + 3, true); // WIND_TRAP (Ordos art)
    REQUIRE(mb >= 0);
    const int bhp0 = w.buildings()[size_t(mb)].hit_points;
    w.order_turret_attack_building(2, mb);
    int bhp = bhp0;
    for (int i = 0; i < 300 && bhp == bhp0; ++i) {
        w.tick();
        bhp = w.buildings()[size_t(mb)].hit_points;
    }
    MESSAGE("O8V1 turret vs allied building: hp " << bhp0 << " -> " << bhp);
    CHECK(bhp < bhp0);
}

TEST_CASE("golden: A1V1 infantry go prone under fire, stand up after +0x2a reaches 40, and do idle moves when never hit (docs/re/45) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto setup = [&](sim::Rng& rng, sim::GameMap& gm, std::unique_ptr<sim::World>& w) {
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        w = std::make_unique<sim::World>(gm, t, tables, speed, placed, rng);
        w->init_players(mis);
        for (size_t i = 0; i < w->units().size(); ++i) {
            const auto& u = w->units()[i];
            if (u.owner == 0 && t.unit_records[size_t(u.record)].infantry && u.state == 2) return int(i);
        }
        return -1;
    };
    {
        // Hit every tick for 60 ticks: +0x2a stays 0, every wrap rolls 1 in 5.
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        std::unique_ptr<sim::World> w;
        const int ui = setup(rng, gm, w);
        REQUIRE(ui >= 0);
        const int id = w->units()[size_t(ui)].index;
        auto unit = [&]() -> sim::Unit& { return w->units()[size_t(w->find_unit(id))]; };
        bool went_prone = false;
        for (int i = 0; i < 60; ++i) {
            unit().flags |= 1; // hit this tick
            w->tick();
            went_prone |= unit().prone;
        }
        CHECK(went_prone);
        CHECK(unit().prone);
        CHECK(unit().anim_mode == 4);
        CHECK(unit().frame == 13); // prone stand
        // No more hits: up again once +0x2a reaches 40 (40 eight-tick slots).
        int up = -1;
        for (int i = 0; i < 400 && up < 0; ++i) {
            w->tick();
            if (!unit().prone) up = i;
        }
        MESSAGE("stood up " << up << " ticks after the last hit");
        CHECK(up >= 300);
        CHECK(up <= 330);
        CHECK(unit().anim_mode == 0);
    }
    {
        // Never hit (+0x2a = 255): facing 16 plays idle animation 14 / 15 sooner or later;
        // facing 0 first turns toward 8.
        sim::Rng rng(2);
        sim::GameMap gm(map, attr, tables, rng);
        std::unique_ptr<sim::World> w;
        const int ui = setup(rng, gm, w);
        REQUIRE(ui >= 0);
        const int id = w->units()[size_t(ui)].index;
        auto unit = [&]() -> sim::Unit& { return w->units()[size_t(w->find_unit(id))]; };
        REQUIRE(unit().hit_age == 0xff);
        unit().facing = 16;
        int idle = -1;
        for (int i = 0; i < 20000 && idle < 0; ++i) {
            w->tick();
            if (unit().anim_mode == 14 || unit().anim_mode == 15) idle = i;
        }
        MESSAGE("idle animation after " << idle << " ticks");
        CHECK(idle >= 0);
        CHECK(unit().facing == 16);
        // Facing 0: only turns, one step per success, toward 8.
        unit().anim_mode = 0;
        unit().frame = 0;
        unit().facing = 0;
        bool idle_anim = false;
        for (int i = 0; i < 20000 && unit().facing == 0; ++i) {
            w->tick();
            idle_anim |= unit().anim_mode == 14 || unit().anim_mode == 15;
        }
        CHECK(unit().facing == 1);
        CHECK_FALSE(idle_anim);
    }
}

TEST_CASE("golden: A1V1 damage smoke: BUILEXP points, a damaged vehicle puffs EX_SMOKE3, a damaged yard burns at its points (docs/re/43) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    w.set_smoke_points(formats::read_file(gd.find("Data/bin/BUILEXP.BIN").value()));
    // Row 0 (Atreides CONSTRUCTION_YARD): 5 points, the first a flame at (40, 19) every 8.
    REQUIRE(w.smoke_points().size() == 100);
    const auto& cy = w.smoke_points()[0];
    CHECK(cy.n == 5);
    CHECK(cy.x[0] == 40);
    CHECK(cy.y[0] == 19);
    CHECK(cy.period[0] == 8);
    CHECK(w.smoke_points()[3].n == 0);
    const auto* ids = t.block(0x6d60d8);
    REQUIRE(ids);
    const int smoke3 = ids->bytes[59];

    // A vehicle below half hp with a timer puffs EX_SMOKE3 within a few px.
    int vi = -1;
    for (size_t i = 0; i < w.units().size(); ++i)
        if (w.units()[i].owner == 0 && !t.unit_records[size_t(w.units()[i].record)].infantry) vi = int(i);
    REQUIRE(vi >= 0);
    const int vid = w.units()[size_t(vi)].index;
    auto veh = [&]() -> sim::Unit& { return w.units()[size_t(w.find_unit(vid))]; };
    veh().hit_points = t.unit_records[size_t(veh().record)].hit_points / 3;
    veh().smoke = 100;
    int puffs = 0;
    for (int i = 0; i < 40; ++i) {
        const size_t before = w.effects.size();
        w.tick();
        for (size_t k = before; k < w.effects.size(); ++k) {
            const auto& e = w.effects[k];
            if (e.explosion != smoke3) continue;
            const int dx = e.x - veh().x / 65536, dy = e.y - veh().y / 65536;
            if (dx >= -4 && dx <= 3 && dy >= -2 && dy <= 1) ++puffs;
        }
    }
    MESSAGE("vehicle puffs in 40 ticks: " << puffs);
    CHECK(puffs >= 5); // about one every 4 ticks
    CHECK(veh().smoke == 90); // -1 every 4th tick
    // Repaired above half: the timer is cleared.
    veh().hit_points = t.unit_records[size_t(veh().record)].hit_points;
    w.tick();
    CHECK(veh().smoke == 0);

    // The yard below half with point 0 on: its type at (x + 40, y + 19 - frame height).
    int yard = -1;
    for (size_t i = 0; i < w.buildings().size(); ++i)
        if (w.buildings()[i].owner == 0 && w.buildings()[i].record == 0) yard = int(i);
    REQUIRE(yard >= 0);
    auto& b = w.buildings()[size_t(yard)];
    b.hit_points = t.building_records[0].hit_points / 3;
    b.smoke = 200;
    b.smoke_points = 1;
    int flames = 0;
    for (int i = 0; i < 40; ++i) {
        const size_t before = w.effects.size();
        w.tick();
        for (size_t k = before; k < w.effects.size(); ++k)
            flames += w.effects[k].explosion == cy.type[0] && w.effects[k].x == (b.x >> 16) + 40;
    }
    MESSAGE("yard point-0 effects in 40 ticks: " << flames);
    CHECK(flames >= 3); // every 8 ticks
}

TEST_CASE("golden: A1V1 order queue: a posted order runs 2 ticks later with the units selected at post time; a load clears it (docs/re/49) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    std::vector<int> mine;
    for (const auto& u : w.units())
        if (u.owner == 0 && u.state == 2) mine.push_back(u.index);
    REQUIRE(mine.size() >= 2);
    auto unit = [&](int id) -> sim::Unit& { return w.units()[size_t(w.find_unit(id))]; };
    const int a = mine[0], b = mine[1];
    w.deselect_all();
    w.select_unit(size_t(w.find_unit(a)), false);
    const int dx = unit(a).cur_x, dy = unit(a).cur_y + 3;
    w.post_order(0, [&w, dx, dy] { w.order_move(0, dx, dy); });
    CHECK(w.pending_orders() == 1);
    // The player changes the selection before the order runs: no effect.
    w.deselect_all();
    w.select_unit(size_t(w.find_unit(b)), false);
    w.tick(); // T + 1: not yet
    CHECK(unit(a).state == 2);
    CHECK(w.pending_orders() == 1);
    w.tick(); // T + 2: runs, at the start of the unit update
    CHECK(w.pending_orders() == 0);
    CHECK(unit(a).dest_x == dx);
    CHECK(unit(a).dest_y == dy);
    CHECK(unit(b).state == 2);         // not part of the order
    CHECK(unit(b).selected);           // the player's selection is kept
    CHECK_FALSE(unit(a).selected);
    // A load frees the pending orders.
    const auto saved = w.save_state();
    w.post_order(0, [&w, dx, dy] { w.order_move(0, dx, dy); });
    CHECK(w.pending_orders() == 1);
    CHECK(w.load_state(saved));
    CHECK(w.pending_orders() == 0);
}

TEST_CASE("golden: A1V1 object pool: 1000 objects per side, then ReachedUnitLimit for any side (docs/re/50) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    // Side 1 (not the local player) fills its pool with one unit record.
    int record = -1, x = 0, y = 0;
    for (const auto& u : w.units())
        if (u.owner == 0) { record = u.record; x = u.cur_x; y = u.cur_y; break; }
    REQUIRE(record >= 0);
    auto side_objects = [&](int s) {
        int n = 0;
        for (const auto& u : w.units()) n += u.owner == s;
        for (const auto& b : w.buildings()) n += b.owner == s && !b.removed;
        for (const auto& b : w.bullets) n += b.owner == s && !b.dead;
        for (const auto& e : w.effects) n += e.owner == s;
        return n;
    };
    w.messages.clear();
    int made = 0;
    while (made < 2000 && w.create_unit(1, record, x, y, x, y) >= 0) ++made;
    CHECK(side_objects(1) == sim::World::kPoolSize);
    REQUIRE(w.messages.size() == 1);
    CHECK(w.messages[0].key == "ReachedUnitLimit"); // shown although side 1 is not local
    // Another side's pool is separate.
    CHECK(w.create_unit(0, record, x, y, x, y) >= 0);
    // A save round trip keeps the state loadable.
    const auto saved = w.save_state();
    CHECK(w.load_state(saved));
    CHECK(w.create_unit(1, record, x, y, x, y) < 0);
}

namespace {
// A skirmish set up as the game screen does it (docs/re/51, 52): _PRAC.MIS
// with the skirmish overrides, no map objects but worms, then the starts.
struct Skirmish {
    platform::GameData gd;
    sim::ExeTables tables;
    formats::Templates t;
    formats::MapFile map;
    formats::MissionFile mis;
    std::unique_ptr<sim::GameMap> gm;
    std::unique_ptr<sim::World> w;
    sim::Rng rng{7};
    Skirmish(const fs::path& root, const std::string& name, int ai, int units, int tech, int side = 0, int worms = 0)
        : gd(root),
          tables(sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())))),
          t(formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()))),
          map(formats::parse_map(formats::read_file(gd.find("Data/maps/" + name + ".MAP").value()))),
          mis(formats::parse_mission(formats::read_file(gd.find("Data/maps/_PRAC.MIS").value()))) {
        for (int i = 0; i < 8; ++i) mis.house[size_t(i)] = uint8_t(i);
        mis.house[0] = uint8_t(side);
        for (int i = 0; i < 8; ++i) {
            mis.tech_level[size_t(i)] = uint8_t(tech);
            mis.credits[size_t(i)] = i == 0 ? 4000 : 10500;
        }
        const auto tmpl = mis.ai[0];
        for (size_t s = 0; s < 8; ++s) {
            if (s > 0) mis.ai[s] = tmpl;
            mis.ai[s][0] = uint8_t(s);
            mis.ai[s][1] = uint8_t(s >= 1 && int(s) <= ai ? 1 : 0);
        }
        auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
        auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
        auto attr = formats::TileAttributes::parse(
            formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        gm = std::make_unique<sim::GameMap>(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
        const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
        w = std::make_unique<sim::World>(*gm, t, tables, speed, std::vector<sim::Object>{}, rng, circles, vars, 0);
        w->set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value())); // without it every hit does 0
        w->campaign = false;
        w->difficulty = 1;
        w->init_players(mis);
        sim::SkirmishSetup k;
        k.ai_players = ai;
        k.unit_count = units;
        k.tech = tech;
        k.worms = worms;
        w->net_worms = worms;
        k.techpos = formats::read_file(gd.find("Data/bin/TECHPOS.BIN").value());
        k.starts = sim::skirmish_starts(map);
        k.map_units = placed;
        w->skirmish_start(k);
        w->set_ai(mis);
        w->set_script(mis);
        w->reveal_start();
    }
};
} // namespace

TEST_CASE("golden: skirmish 2PLAY1 start: 8 starts, one MCV and the TECHPOS units per side, AI houses rolled (docs/re/52) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish s(root, "2PLAY1", 3, 6, 7);
    CHECK(sim::skirmish_starts(s.map).size() == 8);
    std::array<int, 8> units{}, mcvs{};
    for (const auto& u : s.w->units()) {
        ++units[size_t(u.owner)];
        if (s.t.unit_records[size_t(u.record)].kind == 6) ++mcvs[size_t(u.owner)];
    }
    for (int p = 0; p < 4; ++p) {
        CHECK(units[size_t(p)] == 7);
        CHECK(mcvs[size_t(p)] == 1);
        CHECK(s.w->player(p).house <= 2);
    }
    for (int p = 4; p < 8; ++p) CHECK(units[size_t(p)] == 0);
    CHECK(s.w->buildings().empty());
    // Every side starts at a different start point: the MCVs are apart.
    for (const auto& a : s.w->units()) {
        for (const auto& b : s.w->units()) {
            if (a.owner < b.owner && s.t.unit_records[size_t(a.record)].kind == 6 &&
                s.t.unit_records[size_t(b.record)].kind == 6) {
                CHECK(std::abs(int(a.cur_x) - int(b.cur_x)) + std::abs(int(a.cur_y) - int(b.cur_y)) > 10);
            }
        }
    }
}

TEST_CASE("golden: skirmish 2PLAY1, 1 AI: the AI deploys its MCV and builds a base; the default check ends the game (docs/re/52, 53) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish s(root, "2PLAY1", 1, 6, 7);
    int built = 0;
    for (int i = 0; i < 6000 && !s.w->won && !s.w->lost; ++i) {
        s.w->tick();
        built = 0;
        for (const auto& b : s.w->buildings()) built += b.owner == 1 && !b.removed;
    }
    CHECK(built >= 3); // a yard and more within 5 minutes
    CHECK_FALSE(s.w->won);
    CHECK_FALSE(s.w->lost);
    // Side 1 loses everything (its buildings, then every unit, survivors
    // included): the local side wins.
    for (const auto& b : s.w->buildings()) {
        if (b.owner == 1 && !b.removed) s.w->destroy_building(b.index);
    }
    for (int i = 0; i < 2000 && !s.w->won; ++i) {
        for (auto& u : s.w->units()) {
            if (u.owner == 1) u.remove = true;
        }
        s.w->tick();
    }
    CHECK(s.w->won);
    CHECK_FALSE(s.w->lost);
}

TEST_CASE("golden: skirmish crates: one crate on the map with crates on; the cash crate pays 1000 into the pending cash (docs/re/54) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish s(root, "2PLAY1", 1, 6, 7);
    auto crates = [&] {
        int n = 0;
        for (const auto& e : s.gm->objects()) n += e.active == 1 && e.type < 6;
        return n;
    };
    CHECK(crates() == 0);
    s.w->tick();
    CHECK(crates() == 0); // max_crates 0: crates off
    s.w->max_crates = 1;
    s.w->tick();
    CHECK(crates() == 1);
    for (int i = 0; i < 20; ++i) s.w->tick();
    CHECK(crates() == 1); // topped up to the maximum only
    // A cash crate in front of a side-0 vehicle.
    sim::Unit* v = nullptr;
    for (auto& u : s.w->units()) {
        if (u.owner == 0 && !s.t.unit_records[size_t(u.record)].infantry &&
            s.t.unit_records[size_t(u.record)].kind != 6) {
            v = &u;
            break;
        }
    }
    REQUIRE(v != nullptr);
    int cx = -1, cy = -1;
    for (int dy = -1; dy <= 1 && cx < 0; ++dy) {
        for (int dx = -1; dx <= 1 && cx < 0; ++dx) {
            const int x = v->cur_x + dx, y = v->cur_y + dy;
            if ((dx || dy) && (s.gm->at(x, y).flags & 0x73f8) == 0x6000) {
                cx = x;
                cy = y;
            }
        }
    }
    REQUIRE(cx >= 0);
    s.w->max_crates = 0;
    REQUIRE(s.gm->add_object(cx, cy, 5000, 0, 0, 0));
    const auto& pl = s.w->player(0);
    const int32_t before = pl.cash + pl.pending_cash;
    const int id = v->index;
    s.w->deselect_all();
    s.w->select_unit(size_t(s.w->find_unit(id)), false);
    s.w->order_move(0, cx, cy);
    for (int i = 0; i < 100 && (s.gm->at(cx, cy).flags & 0x1000); ++i) s.w->tick();
    CHECK((s.gm->at(cx, cy).flags & 0x1000) == 0);
    CHECK(pl.cash + pl.pending_cash - before == 1000);
}

TEST_CASE("golden: skirmish thumper: order 0x2F starts state 0x20, one thump every 72 ticks (docs/re/54 section 4.4) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish s(root, "2PLAY1", 1, 0, 7);
    int rec = -1;
    for (size_t r = 0; r < s.t.unit_records.size(); ++r) {
        if (s.t.unit_records[r].kind == 0xe) rec = int(r);
    }
    REQUIRE(rec >= 0);
    int sx = -1, sy = -1;
    for (int y = 2; y < s.gm->height() - 2 && sx < 0; ++y) {
        for (int x = 2; x < s.gm->width() - 2 && sx < 0; ++x) {
            if ((s.gm->at(x, y).flags & 0x173f8) == 0x16000) {
                sx = x;
                sy = y;
            }
        }
    }
    REQUIRE(sx >= 0);
    const int i = s.w->create_unit(0, rec, sx, sy, sx, sy);
    REQUIRE(i >= 0);
    const int id = s.w->units()[size_t(i)].index;
    s.w->order_deploy(0, id);
    auto th = [&]() -> sim::Unit& { return s.w->units()[size_t(s.w->find_unit(id))]; };
    CHECK(th().state == 0x20);
    std::vector<int> thumps; // ticks where frame 7 starts
    int last = -1;
    for (int t = 0; t < 400; ++t) {
        s.w->tick();
        const int f = th().frame;
        if (f == 7 && last != 7) thumps.push_back(t);
        last = f;
    }
    REQUIRE(thumps.size() >= 4);
    for (size_t k = 2; k < thumps.size(); ++k) CHECK(thumps[k] - thumps[k - 1] == 72);
    // Any other order ends it (0x20 is not sticky).
    s.w->deselect_all();
    s.w->select_unit(size_t(s.w->find_unit(id)), false);
    s.w->order_move(0, sx + 1, sy);
    CHECK(th().state != 0x20);
}

TEST_CASE("golden: skirmish save: a loaded skirmish continues identically (docs/re/51 section 7) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish a(root, "2PLAY1", 2, 6, 7);
    a.w->max_crates = 1;
    a.w->net_worms = 2;
    a.w->side_colour = {3, 5, 0, 1, 4, 5, 6, 7};
    for (int i = 0; i < 1500; ++i) a.w->tick();
    const auto saved = a.w->save_state();
    // A second game, as the game screen builds one before load_saved.
    Skirmish b(root, "2PLAY1", 1, 3, 4);
    REQUIRE(b.w->load_state(saved));
    CHECK(b.w->save_state() == saved); // round trip
    CHECK(b.w->max_crates == 1);
    CHECK(b.w->net_worms == 2);
    CHECK(b.w->side_colour[1] == 5);
    CHECK_FALSE(b.w->campaign);
    for (int i = 0; i < 1500; ++i) {
        a.w->tick();
        b.w->tick();
    }
    CHECK(a.w->save_state() == b.w->save_state());
}

TEST_CASE("golden: skirmish finishing places: unused sides on the first tick in side order, a beaten side the next place (docs/re/56 section 2) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish s(root, "2PLAY1", 2, 6, 7);
    s.w->tick();
    for (int p = 0; p < 3; ++p) CHECK(s.w->place_[size_t(p)] == 0xff);
    for (int p = 3; p < 8; ++p) CHECK(s.w->place_[size_t(p)] == p - 3);
    // Side 2 loses everything: the next place (5).
    for (int i = 0; i < 64 && s.w->place_[2] == 0xff; ++i) {
        for (auto& u : s.w->units()) {
            if (u.owner == 2) u.remove = true;
        }
        s.w->tick();
    }
    CHECK(s.w->place_[2] == 5);
    CHECK(s.w->place_[1] == 0xff);
}

namespace {
// The side's MCV (the first), or nullptr.
sim::Unit* skirmish_mcv(Skirmish& s, int side) {
    for (auto& u : s.w->units()) {
        if (u.owner == side && !u.remove && s.t.unit_records[size_t(u.record)].kind == 6) return &u;
    }
    return nullptr;
}
// A 4 x 4 block of buildable, empty cells at least 3 cells from (x, y).
bool skirmish_free_block(Skirmish& s, int x, int y, int& bx, int& by) {
    for (int r = 3; r < 20; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                const int cx = x + dx, cy = y + dy;
                if (cx < 1 || cy < 1 || cx + 4 >= s.gm->width() || cy + 4 >= s.gm->height()) continue;
                bool ok = true;
                for (int k = 0; k < 16 && ok; ++k) ok = (s.gm->at(cx + (k & 3), cy + (k >> 2)).flags & 0x83f8) == 0x8000;
                if (ok) {
                    bx = cx;
                    by = cy;
                    return true;
                }
            }
        }
    }
    return false;
}
} // namespace

TEST_CASE("golden: skirmish AI base box: the units' box before a building, the buildings' box with 2-cell margins after (docs/re/53 section 1.6) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish s(root, "2PLAY1", 1, 6, 7);
    int x0 = s.gm->width(), y0 = s.gm->height(), x1 = 0, y1 = 0;
    for (const auto& u : s.w->units()) {
        if (u.owner != 1) continue;
        const int ux = (u.x / 0x10000) / 32, uy = (u.y / 0x10000) / 32;
        x0 = std::min(x0, ux);
        x1 = std::max(x1, ux + 1);
        y0 = std::min(y0, uy);
        y1 = std::max(y1, uy + 1);
    }
    const auto& bb = s.w->player(1).base_box;
    CHECK(bb[0] == y0);
    CHECK(bb[1] == y1);
    CHECK(bb[2] == x0);
    CHECK(bb[3] == x1);
    // The base area of ActivateAI is that box.
    CHECK(s.w->ai_u8(1, 0x1D52) == 0);
    CHECK(s.w->ai_u8(1, 0x1D54) == x0);
    CHECK(s.w->ai_u8(1, 0x1D55) == x1);
    CHECK(s.w->ai_u8(1, 0x1D56) == y0);
    CHECK(s.w->ai_u8(1, 0x1D57) == y1);
    // A building: its cells with 2-cell margins, the y from its bottom edge.
    sim::Unit* mcv = skirmish_mcv(s, 1);
    REQUIRE(mcv != nullptr);
    int bx, by;
    REQUIRE(skirmish_free_block(s, mcv->cur_x, mcv->cur_y, bx, by));
    const auto* ids = s.t.block(0x6d60d8);
    const int pad = s.t.building_record(17, s.w->player(1).house); // REPAIR_PAD
    REQUIRE(pad >= 0);
    REQUIRE(ids != nullptr);
    const int b = s.w->create_building(1, pad, bx, by, true);
    REQUIRE(b >= 0);
    const auto& bl = s.w->buildings()[size_t(b)];
    const auto& r = s.t.building_records[size_t(pad)];
    const int cx = (bl.x / 0x10000) / 32, cy = (bl.y / 0x10000) / 32;
    CHECK(bb[2] == std::max(cx - 2, 0));
    CHECK(bb[3] == cx + (r.width_px >> 5) + 2);
    CHECK(bb[0] == std::max(cy - 2 - (r.height_px >> 5), 0));
    CHECK(bb[1] == cy + 2);
    // The box is saved (state version 18).
    const auto saved = s.w->save_state();
    REQUIRE(s.w->load_state(saved));
    CHECK(s.w->player(1).base_box[3] == cx + (r.width_px >> 5) + 2);
}

TEST_CASE("golden: skirmish AI auto-BESERK: a practice side without yard, MCV and factories goes BESERK on its tick & 7 slot (docs/re/53 section 3) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish s(root, "2PLAY1", 1, 6, 7);
    for (int i = 0; i < 8; ++i) s.w->tick();
    CHECK(s.w->ai_u8(1, 0x1E) == 0); // the MCV is there
    for (auto& u : s.w->units()) {
        if (u.owner == 1 && s.t.unit_records[size_t(u.record)].kind == 6) u.remove = true;
    }
    int at = -1;
    for (int i = 0; i < 16 && at < 0; ++i) {
        s.w->tick();
        if (s.w->ai_u8(1, 0x1E) == 1) at = int(s.w->tick_count());
    }
    REQUIRE(at >= 0);
    CHECK((at & 7) == 1); // set in DoAI of a tick with tick & 7 == side
}

TEST_CASE("golden: skirmish AI practice MCV: without yard and MCV, a heavy factory builds a new MCV (docs/re/53 section 4) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish s(root, "2PLAY1", 1, 0, 3); // tech 3: the MCV (tech 3) but no siege tank
    sim::Unit* mcv = skirmish_mcv(s, 1);
    REQUIRE(mcv != nullptr);
    int bx, by;
    REQUIRE(skirmish_free_block(s, mcv->cur_x, mcv->cur_y, bx, by));
    const auto* ids = s.t.block(0x6d60d8);
    REQUIRE(ids != nullptr);
    const int heavy = s.t.building_record(ids->bytes[4], s.w->player(1).house); // 0x6d60dc HEAVY_FACTORY
    REQUIRE(heavy >= 0);
    REQUIRE(s.w->create_building(1, heavy, bx, by, true) >= 0);
    // The MCV also needs a repair pad and an upgraded heavy factory (record
    // prerequisites); the pad's placement rebuilds the lists.
    s.w->player(1).upgrade_level[ids->bytes[4]] = 3;
    REQUIRE(skirmish_free_block(s, mcv->cur_x, mcv->cur_y, bx, by));
    REQUIRE(s.w->create_building(1, s.t.building_record(17, s.w->player(1).house), bx, by, true) >= 0);
    const int mrec = ids->bytes[0xf];
    mcv->remove = true;
    s.w->tick();
    const auto& list = s.w->player(1).unit_list;
    REQUIRE(std::find(list.begin(), list.end(), mrec) != list.end());
    // Below CashStash[BuildUnits] + a price nothing else is picked (and with
    // room for spice, as spice == capacity counts as storage full); the MCV
    // skips ShouldIBuildUnit and only needs its price (CanBuildUnit).
    s.w->player(1).cash = 2500;
    s.w->player(1).spice = 0;
    s.w->player(1).capacity = 1000;
    bool ordered = false;
    for (int i = 0; i < 3000 && !ordered; ++i) {
        s.w->tick();
        for (const auto& sl : s.w->player(1).unit_slots) ordered = ordered || sl.item == mrec;
        for (const auto& u : s.w->units()) ordered = ordered || (u.owner == 1 && u.record == mrec);
    }
    CHECK(ordered);
    CHECK(s.w->ai_u8(1, 0x1E) == 0); // a factory: no BESERK
}

TEST_CASE("golden: skirmish AI DoRepairUnits: damaged base units with a repair pad form a Repair group (job 0xB) and head for the pad; whole again, they return (docs/re/53 section 2) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish s(root, "2PLAY1", 1, 0, 7);
    sim::Unit* mcv = skirmish_mcv(s, 1);
    REQUIRE(mcv != nullptr);
    const int mx = mcv->cur_x, my = mcv->cur_y;
    int bx, by;
    REQUIRE(skirmish_free_block(s, mx, my, bx, by));
    const int house = s.w->player(1).house;
    const int pad = s.t.building_record(17, house);
    REQUIRE(pad >= 0);
    REQUIRE(s.w->create_building(1, pad, bx, by, true) >= 0);
    CHECK(s.w->ai_u8(1, 0x1D46) == 17);
    // Two armed vehicles of the side, badly damaged.
    int tank = -1;
    for (size_t r = 0; r < s.t.unit_records.size() && tank < 0; ++r) {
        const auto& ur = s.t.unit_records[r];
        if (ur.weapon != 0xff && !ur.infantry && ur.kind == 0 && ur.speed_class != 3 &&
            s.t.unit_record(ur.name_index, house) == int(r)) {
            tank = int(r);
        }
    }
    REQUIRE(tank >= 0);
    std::vector<int> ids;
    for (int k = 0; k < 2; ++k) {
        const int i = s.w->create_unit(1, tank, mx + 2 + k, my, mx + 2 + k, my);
        REQUIRE(i >= 0);
        auto& u = s.w->units()[size_t(i)];
        u.hit_points = s.t.unit_records[size_t(tank)].hit_points / 5;
        ids.push_back(u.index);
    }
    auto unit = [&](int id) -> sim::Unit& { return s.w->units()[size_t(s.w->find_unit(id))]; };
    auto repair_group = [&] {
        for (int g = 0; g < 32; ++g) {
            if (s.w->ai_u8(1, size_t(g) * 0x2C + 0x52) && s.w->ai_u8(1, size_t(g) * 0x2C + 0x48) == 0xB) return g;
        }
        return -1;
    };
    int g = -1;
    for (int i = 0; i < 400 && g < 0; ++i) {
        s.w->tick();
        g = repair_group();
    }
    REQUIRE(g > 0);
    for (int id : ids) CHECK(unit(id).ai_group == g);
    for (int i = 0; i < 3; ++i) s.w->tick(); // the order runs 2 ticks after it is posted
    for (int id : ids) CHECK(unit(id).state == 0x18);
    // Whole again: on the group's MonitorGroups turn, ReturnToBase (job 10).
    for (int id : ids) unit(id).hit_points = s.t.unit_records[size_t(tank)].hit_points;
    bool returning = false;
    for (int i = 0; i < 40 && !returning; ++i) {
        s.w->tick();
        returning = s.w->ai_u8(1, size_t(g) * 0x2C + 0x48) == 10;
    }
    CHECK(returning);
}

TEST_CASE("golden: skirmish AI: 1 AI on 2PLAY1 runs 12000 ticks with the base defence, repairs and splits; the base area follows the box (docs/re/53) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish s(root, "2PLAY1", 1, 6, 7);
    bool defend = false;
    for (int i = 0; i < 12000 && !s.w->won && !s.w->lost; ++i) {
        s.w->tick();
        for (int g = 0; g < 32; ++g) {
            defend = defend || (s.w->ai_u8(1, size_t(g) * 0x2C + 0x52) && s.w->ai_u8(1, size_t(g) * 0x2C + 0x48) == 0xF);
        }
    }
    CHECK(defend); // DoDefendChecks made the base area's group
    CHECK(s.w->ai_u8(1, 0x19EC) == 1);
    // The base area was refreshed from the box ((tick & 0xE0) == 0 passes).
    const auto& bb = s.w->player(1).base_box;
    CHECK(s.w->ai_u8(1, 0x1D54) <= bb[3]);
    CHECK(s.w->ai_u32(1, 0x1D5C) > 1000u); // sqrt(area) x GuardGroupSize (300)
    // Deterministic: a second run from the same seed matches.
    Skirmish t(root, "2PLAY1", 1, 6, 7);
    for (int i = 0; i < 12000 && !t.w->won && !t.w->lost; ++i) t.w->tick();
    CHECK(t.w->save_state() == s.w->save_state());
}

TEST_CASE("golden: skirmish thumper draws the worm after its initial sleep (5051 ticks) and keeps it at the thumper's cell (docs/re/17 section 2, docs/re/54 section 4.5) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    // No AI: its vehicles on sand would draw the worm too (FUN_0044d630).
    Skirmish s(root, "2PLAY1", 0, 0, 7, 0, 1);
    int rec = -1;
    for (size_t r = 0; r < s.t.unit_records.size(); ++r) {
        if (s.t.unit_records[r].kind == 0xe) rec = int(r);
    }
    REQUIRE(rec >= 0);
    auto worm = [&]() -> const sim::Unit* {
        for (auto& u : s.w->units()) {
            if (s.t.unit_records[size_t(u.record)].kind == 5) return &u;
        }
        return nullptr;
    };
    REQUIRE(worm() != nullptr);
    const int wx = worm()->cur_x, wy = worm()->cur_y;
    // An open sand cell about 20 cells from the worm.
    int sx = -1, sy = -1, bd = 1 << 30;
    for (int y = 2; y < s.gm->height() - 2; ++y) {
        for (int x = 2; x < s.gm->width() - 2; ++x) {
            if ((s.gm->at(x, y).flags & 0x173f8) != 0x16000) continue;
            const int d = std::abs(std::abs(x - wx) + std::abs(y - wy) - 20);
            if (d < bd) {
                bd = d;
                sx = x;
                sy = y;
            }
        }
    }
    REQUIRE(sx >= 0);
    const int i = s.w->create_unit(0, rec, sx, sy, sx, sy);
    REQUIRE(i >= 0);
    s.w->order_deploy(0, s.w->units()[size_t(i)].index);
    // Asleep (state 0x1d) until 50 x 101 ticks have passed.
    for (int t = 0; t < 5000; ++t) s.w->tick();
    CHECK(worm()->state == 0x1d);
    CHECK(worm()->cur_x == wx);
    for (int t = 0; t < 3000; ++t) s.w->tick();
    CHECK(worm()->cur_x == sx);
    CHECK(worm()->cur_y == sy);
    CHECK(worm()->dest_x == sx);
    CHECK(worm()->dest_y == sy);
}

namespace {
int turret_record(const formats::Templates& t, const std::string& name) {
    for (size_t r = 0; r < t.unit_records.size(); ++r) {
        if (t.unit_name[t.unit_records[r].name_index] == name && t.unit_records[r].has_turret) return int(r);
    }
    return -1;
}
// An open sand cell with free neighbours, scanning from (x0, y0).
bool open_cell(const sim::GameMap& gm, int x0, int y0, int& sx, int& sy) {
    for (int y = y0; y < gm.height() - 3; ++y) {
        for (int x = x0; x < gm.width() - 6; ++x) {
            bool ok = true;
            for (int dy = -2; dy <= 2 && ok; ++dy) {
                for (int dx = -2; dx <= 5 && ok; ++dx) ok = (gm.at(x + dx, y + dy).flags & 0x173f8) == 0x16000;
            }
            if (ok) {
                sx = x;
                sy = y;
                return true;
            }
        }
    }
    return false;
}
} // namespace

TEST_CASE("golden: a moving turreted unit turns its turret toward the destination cell (0x4948d7, FUN_0045e020) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish s(root, "2PLAY1", 1, 0, 7);
    const int rec = turret_record(s.t, "COMBAT_TANK");
    REQUIRE(rec >= 0);
    int sx, sy;
    REQUIRE(open_cell(*s.gm, 4, 4, sx, sy));
    const int i = s.w->create_unit(0, rec, sx, sy, sx, sy);
    REQUIRE(i >= 0);
    const int id = s.w->units()[size_t(i)].index;
    auto u = [&]() -> sim::Unit& { return s.w->units()[size_t(s.w->find_unit(id))]; };
    // Destination three cells east: the turret starts pointing west (24).
    u().turret_facing = 24;
    u().turret_goal = 24;
    s.w->deselect_all();
    s.w->select_unit(size_t(s.w->find_unit(id)), false);
    s.w->order_move(0, sx + 3, sy);
    bool aimed = false;
    for (int t = 0; t < 300 && !aimed; ++t) {
        s.w->tick();
        if (u().cur_x != u().dest_x || u().cur_y != u().dest_y) aimed = u().turret_facing == 8;
    }
    CHECK(aimed); // east = 8, reached while still moving
}

TEST_CASE("golden: the SIEGE_TANK turret is locked to the hull (record flag 0x8000, FUN_00493f40): it follows the body while moving, and to fire the hull turns (FUN_00495b90) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish s(root, "2PLAY1", 1, 0, 7);
    const int rec = turret_record(s.t, "SIEGE_TANK");
    REQUIRE(rec >= 0);
    CHECK((s.t.unit_records[size_t(rec)].initial_flags & 0x8000) != 0);
    int sx, sy;
    REQUIRE(open_cell(*s.gm, 4, 4, sx, sy));
    const int i = s.w->create_unit(0, rec, sx, sy, sx, sy);
    REQUIRE(i >= 0);
    const int id = s.w->units()[size_t(i)].index;
    auto u = [&]() -> sim::Unit& { return s.w->units()[size_t(s.w->find_unit(id))]; };
    s.w->deselect_all();
    s.w->select_unit(size_t(s.w->find_unit(id)), false);
    s.w->order_move(0, sx + 3, sy);
    for (int t = 0; t < 300; ++t) {
        s.w->tick();
        if (u().steps > 0) CHECK(u().turret_facing == u().facing);
    }
    // A target three cells west: the hull turns round and the tank fires.
    const int tank = turret_record(s.t, "COMBAT_TANK");
    const int e = s.w->create_unit(1, tank, u().cur_x - 3, u().cur_y, u().cur_x - 3, u().cur_y);
    REQUIRE(e >= 0);
    const int eid = s.w->units()[size_t(e)].index;
    const int hp0 = s.w->units()[size_t(e)].hit_points;
    bool hit = false;
    for (int t = 0; t < 400 && !hit; ++t) {
        s.w->tick();
        const int ei = s.w->find_unit(eid);
        hit = ei < 0 || s.w->units()[size_t(ei)].hit_points < hp0;
    }
    CHECK(hit);
    CHECK(u().turret_facing == u().facing);
}

TEST_CASE("golden: the idle turret wander (FUN_00494210): an area-guard unit at its guard cell turns its turret toward the goal +0x56 (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish s(root, "2PLAY1", 1, 0, 7);
    const int rec = turret_record(s.t, "COMBAT_TANK");
    REQUIRE(rec >= 0);
    int sx, sy;
    REQUIRE(open_cell(*s.gm, 4, 4, sx, sy));
    const int i = s.w->create_unit(0, rec, sx, sy, sx, sy);
    REQUIRE(i >= 0);
    const int id = s.w->units()[size_t(i)].index;
    auto u = [&]() -> sim::Unit& { return s.w->units()[size_t(s.w->find_unit(id))]; };
    u().state = 3;
    u().home_x = uint8_t(sx);
    u().home_y = uint8_t(sy);
    u().turret_facing = 0;
    u().turret_goal = 5;
    for (int t = 0; t < 400; ++t) s.w->tick();
    CHECK(u().turret_facing == 5);
    CHECK(u().facing == 0); // only the turret
}

TEST_CASE("golden: DoUnitFacings (FUN_00420c80): at activation an AI side with a primary yard turns every unit away from its base centre (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
    const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/O6V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_O6V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
    sim::World w(gm, t, tables, speed, placed, rng, circles, vars);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.local_player = 2;
    w.init_players(mis);
    w.set_script(mis);
    w.set_ai(mis);
    int checked = 0;
    for (int s = 0; s < 8; ++s) {
        if (!w.ai_u8(s, 1) || w.player(s).primary[2] < 0) continue;
        int tx, ty;
        REQUIRE(w.retreat_destination(s, tx, ty));
        for (const auto& u : w.units()) {
            if (u.owner != s || u.remove) continue;
            const int32_t dx = tx * 0x200000 - u.x, dy = u.y - ty * 0x200000;
            const int f = sim::angle12(dx / 65536, dy / 65536) / 128;
            CHECK(int(u.facing) == f);
            CHECK(int(u.turret_facing) == f);
            CHECK(int(u.turret_goal) == f);
            ++checked;
        }
    }
    MESSAGE("O6V1: " << checked << " AI units turned away from their bases");
    CHECK(checked > 0);
}

TEST_CASE("golden: every mission's GuardGroupSize (+0xF5C) is below 131072, so the defend-area strength ftol(sqrt(area) * size) is the same in double as on the x87 (docs/re/53 section 11) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    uint32_t biggest = 0;
    int files = 0;
    std::vector<fs::path> mis;
    for (auto& e : fs::directory_iterator(root / "MISSIONS")) mis.push_back(e.path());
    if (fs::exists(root / "Data/maps/_PRAC.MIS")) mis.push_back(root / "Data/maps/_PRAC.MIS");
    for (const auto& p : mis) {
        std::string ext = p.extension().string();
        for (auto& c : ext) c = char(std::toupper(static_cast<unsigned char>(c)));
        if (ext != ".MIS") continue;
        formats::MissionFile m;
        try {
            m = formats::parse_mission(formats::read_file(p));
        } catch (const std::exception&) {
            continue; // not a mission file of the 1.06 size
        }
        ++files;
        for (const auto& block : m.ai) {
            if (block.size() < 0xF60) continue;
            uint32_t v;
            std::memcpy(&v, block.data() + 0xF5C, 4);
            biggest = std::max(biggest, v);
        }
    }
    MESSAGE(files << " mission files, the largest GuardGroupSize " << biggest);
    CHECK(files > 0);
    CHECK(biggest < 131072u);
}

TEST_CASE("golden: determinism fingerprint: 4PLAY1 with 3 AIs for 6000 ticks; the state hash is printed so builds on different compilers can be compared (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/4PLAY1.MAP")) return;
    Skirmish s(root, "4PLAY1", 3, 6, 7, 0, 2);
    s.w->max_crates = 1;
    for (int i = 0; i < 6000; ++i) s.w->tick();
    const std::string h = platform::sha256_hex(s.w->save_state());
    MESSAGE("determinism fingerprint " << h);
    // The same on MSVC and GCC (2026-10-14). A deliberate simulation change
    // updates it; an unexpected change here means the simulation moved.
    // 2026-10-16: skirt tiles (and their rand() at removal) and the saved
    // per-record counters (docs/re/12 step 15, docs/re/60); without those
    // two the hash was still the old 2a18e00f...
    // 2026-10-16 (AI sweep, docs/re/53 section 11): the unsigned splinter
    // limit, the unit pick with none owned, FindGroupForDelivery, the
    // remembered base cell, GetNearestHostileThing / SendNearestUnit /
    // group-position pixel cells and the ClearUnits points; was 0c0d217c...
    // 2026-10-16: the tick runs side by side as FUN_00458e90 (units,
    // buildings, bullets, explosions of each side in turn; docs/re/43 section 5).
    // Then the head of the unit update in the exe order for every unit
    // (docs/re/18 section 3.7a). Then the placement / sell / capture close:
    // unit +0x2E starts at 0 (DoMCVs), the free harvester delivery has
    // action 4, sell / repair handlers and the primary pick by name as the
    // exe (docs/re/12, 21, 30).
    // Then the object pool (docs/re/50: per-side LIFO slots as staggers, slot
    // references, order records by (side, slot)) and the movement / order
    // handler fixes (docs/re/09, 49); was e696ac8e...
    CHECK(h == "18a6ff5ea76dc1ad68dffad70422a60fc40b8ffee1e7d739b885a850ee641f93");
}

TEST_CASE("golden: an order record runs exactly as the closure it replaces (docs/design/multiplayer.md section 3) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data/maps/2PLAY1.MAP")) return;
    Skirmish a(root, "2PLAY1", 1, 6, 7), b(root, "2PLAY1", 1, 6, 7), c(root, "2PLAY1", 1, 6, 7);
    for (auto* w : {a.w.get(), b.w.get(), c.w.get()}) {
        for (auto& u : w->units()) u.selected = u.owner == 0;
        for (int i = 0; i < 20; ++i) w->tick();
    }
    int x = 0, y = 0;
    for (const auto& u : a.w->units()) {
        if (u.owner == 0) {
            x = std::min(u.cur_x + 6, a.w->map().width() - 1);
            y = u.cur_y;
            break;
        }
    }
    a.w->post_order(0, [&] { a.w->order_move(0, x, y); });
    const sim::Order posted = b.w->post(sim::make_order(sim::order::kMove, 0, x, y));
    CHECK(posted.tick == b.w->tick_count() + sim::World::kOrderLatency);
    CHECK(!posted.ids.empty());
    for (auto& u : b.w->units()) u.selected = false; // the record keeps the post-time selection
    for (auto& u : a.w->units()) u.selected = false;
    for (auto& u : c.w->units()) u.selected = false;
    for (int i = 0; i < 300; ++i) {
        a.w->tick();
        b.w->tick();
        c.w->tick(); // no order: the order must have changed something
    }
    CHECK(platform::sha256_hex(a.w->save_state()) == platform::sha256_hex(b.w->save_state()));
    CHECK(platform::sha256_hex(a.w->save_state()) != platform::sha256_hex(c.w->save_state()));
}


TEST_CASE("golden: A1V1 click hit test is cell based (FUN_004a5270 / FUN_004a5640) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    int inf_rec = -1, veh_rec = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        const auto& r = t.unit_records[i];
        if (r.infantry && r.kind == 0 && inf_rec < 0) inf_rec = int(i);
        if (!r.infantry && r.kind == 0 && veh_rec < 0) veh_rec = int(i);
    }
    REQUIRE(inf_rec >= 0);
    REQUIRE(veh_rec >= 0);
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    sim::World w(gm, t, tables, speed, {}, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    REQUIRE(w.alliance(0, 1) != 0);
    // An open area of 8 x 3 drivable cells.
    int cx = -1, cy = -1;
    for (int y = 2; y < w.map().height() - 4 && cx < 0; ++y) {
        for (int x = 2; x < w.map().width() - 10 && cx < 0; ++x) {
            bool ok = true;
            for (int dy = 0; dy < 3; ++dy) {
                for (int dx = 0; dx < 8; ++dx) {
                    const uint32_t f = w.map().at(x + dx, y + dy).flags;
                    if (!(f & sim::cell::kVehicles) || !(f & sim::cell::kInfantry) || (f & 0x3f8)) ok = false;
                }
            }
            if (ok) {
                cx = x;
                cy = y;
            }
        }
    }
    REQUIRE(cx >= 0);
    const int v = w.create_unit(0, veh_rec, cx, cy, cx, cy);
    REQUIRE(v >= 0);
    const int vid = w.units()[size_t(v)].index;
    const int i1 = w.create_unit(0, inf_rec, cx + 3, cy, cx + 3, cy);
    const int i2 = w.create_unit(0, inf_rec, cx + 3, cy, cx + 3, cy);
    REQUIRE(i1 >= 0);
    REQUIRE(i2 >= 0);
    const int id1 = w.units()[size_t(i1)].index, id2 = w.units()[size_t(i2)].index;
    const int e = w.create_unit(1, veh_rec, cx + 6, cy, cx + 6, cy);
    REQUIRE(e >= 0);
    const int eid = w.units()[size_t(e)].index;
    auto at = [&](int id) { return w.find_unit(id); };

    // Any pixel of the vehicle's cell hits it, corners included; the next
    // cell does not, even 1 px from the vehicle's centre row.
    CHECK(w.click_unit(0, cx * 32, cy * 32) == at(vid));
    CHECK(w.click_unit(0, cx * 32 + 31, cy * 32 + 31) == at(vid));
    CHECK(w.click_unit(0, cx * 32 + 32, cy * 32 + 16) == -1);
    CHECK(w.click_unit(0, cx * 32 + 16, cy * 32 - 1) == -1);
    // Infantry: the one nearest to the pixel (sub-cell positions differ).
    const auto& u1 = w.units()[size_t(at(id1))];
    const auto& u2 = w.units()[size_t(at(id2))];
    REQUIRE((u1.x != u2.x || u1.y != u2.y));
    CHECK(w.click_unit(0, u1.x >> 16, u1.y >> 16) == at(id1));
    CHECK(w.click_unit(0, u2.x >> 16, u2.y >> 16) == at(id2));
    bool inf = false;
    CHECK(w.unit_at_pixel(u2.x >> 16, u2.y >> 16, false, &inf) == at(id2));
    CHECK(inf);
    // The cell-corner form (unit_in_cell) takes the one nearest to the corner.
    {
        const int d1 = std::abs((u1.x >> 16) - (cx + 3) * 32) + std::abs((u1.y >> 16) - cy * 32);
        const int d2 = std::abs((u2.x >> 16) - (cx + 3) * 32) + std::abs((u2.y >> 16) - cy * 32);
        CHECK(w.unit_at_cell(cx + 3, cy) == (d2 < d1 ? at(id2) : at(id1)));
    }
    // An enemy vehicle: hit from either side's view.
    CHECK(w.click_unit(0, (cx + 6) * 32 + 16, cy * 32 + 16) == at(eid));
    // Cloaked: none for a side that is not allied, still hit for its owner;
    // the raw lookup without skip_cloaked finds it.
    w.units()[size_t(at(eid))].flags |= 4;
    CHECK(w.click_unit(0, (cx + 6) * 32 + 16, cy * 32 + 16) == -1);
    CHECK(w.click_unit(1, (cx + 6) * 32 + 16, cy * 32 + 16) == at(eid));
    CHECK(w.unit_at_pixel((cx + 6) * 32 + 16, cy * 32 + 16, false) == at(eid));
    CHECK(w.unit_at_pixel((cx + 6) * 32 + 16, cy * 32 + 16, true) == -1);
    w.units()[size_t(at(eid))].flags &= ~4u;
    // Airborne / carried units (0x140) are never hit.
    w.units()[size_t(at(vid))].flags |= 0x40;
    CHECK(w.click_unit(0, cx * 32 + 16, cy * 32 + 16) == -1);
    w.units()[size_t(at(vid))].flags &= ~0x40u;
    // Stale owner bits: the cell says side 1, so side 0's vehicle there is
    // not found (only the cell owner's units are searched).
    auto& cell = const_cast<sim::Cell&>(w.map().at(cx, cy));
    const uint32_t keep = cell.flags;
    cell.flags = (cell.flags & ~7u) | 1;
    CHECK(w.click_unit(0, cx * 32 + 16, cy * 32 + 16) == -1);
    cell.flags = keep;
    CHECK(w.click_unit(0, cx * 32 + 16, cy * 32 + 16) == at(vid));
    // Empty cells and pixels off the map.
    CHECK(w.click_unit(0, (cx + 7) * 32 + 16, (cy + 2) * 32 + 16) == -1);
    CHECK(w.click_unit(0, w.map().width() * 32 + 5, 5) == -1);
}

TEST_CASE("golden: A1V1 buildings under the mouse and HomeView (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/A1V1.MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_A1V1.MIS").value()));
    auto attr = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
    int mcv_rec = -1, veh_rec = -1;
    for (size_t i = 0; i < t.unit_records.size(); ++i) {
        const auto& r = t.unit_records[i];
        if (!r.infantry && r.kind == 6 && mcv_rec < 0) mcv_rec = int(i);
        if (!r.infantry && r.kind == 0 && veh_rec < 0) veh_rec = int(i);
    }
    REQUIRE(mcv_rec >= 0);
    REQUIRE(veh_rec >= 0);
    const int mx = map.width * 32 - 480 - 1, my = map.height * 32 - 380 - 1;
    auto expect = [&](int px, int py, int& x, int& y) {
        x = std::min(px < 1 ? 0 : px, mx);
        y = std::min(py < 1 ? 0 : py, my);
    };

    {
        // The map's buildings: every footprint cell finds its building, and
        // no unit is hit there.
        sim::Rng rng(1);
        sim::GameMap gm(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        sim::World w(gm, t, tables, speed, placed, rng);
        w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w.init_players(mis);
        int own = -1;
        for (const auto& b : w.buildings()) {
            const auto& r = t.building_records[size_t(b.record)];
            for (int k = 0; k < 16; ++k) {
                if (!(r.footprint & (1u << k))) continue;
                const int fx = b.cell_x + (k & 3), fy = b.cell_y + (k >> 2);
                CHECK(w.building_at_cell(fx, fy) == b.index);
                CHECK(w.click_unit(0, fx * 32 + 16, fy * 32 + 16) == -1);
            }
            if (b.owner == 0 && own < 0) own = b.index;
        }
        REQUIRE(own >= 0);
        // HomeView on a building: x - 240, y - offset_y / 2 - 190 (the
        // element's offset_y, set here by hand).
        w.element_view_dy.assign(t.art.building.size(), 41);
        int pick = w.player(0).primary[2];
        if (pick < 0) {
            int best = 0x7fffffff;
            for (const auto& b : w.buildings()) {
                const int g = t.building_records[size_t(b.record)].name_index;
                if (b.owner == 0 && g < best) {
                    best = g;
                    pick = b.index;
                }
            }
        }
        REQUIRE(pick >= 0);
        const auto& pb = w.buildings()[size_t(pick)];
        int x = -1, y = -1, ex = 0, ey = 0;
        REQUIRE(w.home_view(0, x, y));
        expect((pb.x >> 16) - 240, (pb.y >> 16) - 20 - 190, ex, ey);
        CHECK(x == ex);
        CHECK(y == ey);
    }

    // No buildings: the first unit, replaced by every later MCV; with H
    // (select_mcv) the MCV becomes the local side's only selection, other
    // sides' selected units stay.
    sim::Rng rng(1);
    sim::GameMap gm(map, attr, tables, rng);
    sim::World w(gm, t, tables, speed, {}, rng);
    w.set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    w.init_players(mis);
    int x = -1, y = -1, ex = 0, ey = 0;
    CHECK(!w.home_view(0, x, y, true)); // nothing: the camera stays
    std::vector<std::pair<int, int>> free_cells;
    for (int cy = 2; cy < w.map().height() - 2 && free_cells.size() < 4; ++cy) {
        for (int cx = 2; cx < w.map().width() - 2 && free_cells.size() < 4; cx += 3) {
            const uint32_t f = w.map().at(cx, cy).flags;
            if ((f & sim::cell::kVehicles) && !(f & 0x3f8)) free_cells.emplace_back(cx, cy);
        }
    }
    REQUIRE(free_cells.size() == 4);
    const int a = w.create_unit(0, veh_rec, free_cells[0].first, free_cells[0].second, free_cells[0].first, free_cells[0].second);
    REQUIRE(a >= 0);
    const int aid = w.units()[size_t(a)].index;
    REQUIRE(w.home_view(0, x, y, true));
    expect((w.units()[size_t(a)].x >> 16) - 240, (w.units()[size_t(a)].y >> 16) - 190, ex, ey);
    CHECK(x == ex);
    CHECK(y == ey);
    CHECK(!w.units()[size_t(a)].selected); // not an MCV: no selection change
    const int m1 = w.create_unit(0, mcv_rec, free_cells[1].first, free_cells[1].second, free_cells[1].first, free_cells[1].second);
    const int m2 = w.create_unit(0, mcv_rec, free_cells[2].first, free_cells[2].second, free_cells[2].first, free_cells[2].second);
    const int en = w.create_unit(1, veh_rec, free_cells[3].first, free_cells[3].second, free_cells[3].first, free_cells[3].second);
    REQUIRE(m1 >= 0);
    REQUIRE(m2 >= 0);
    REQUIRE(en >= 0);
    const int m1id = w.units()[size_t(m1)].index, m2id = w.units()[size_t(m2)].index, enid = w.units()[size_t(en)].index;
    w.units()[size_t(w.find_unit(aid))].selected = true;
    w.units()[size_t(w.find_unit(enid))].selected = true;
    REQUIRE(w.home_view(0, x, y, false));
    const auto& last = w.units()[size_t(w.find_unit(m2id))];
    expect((last.x >> 16) - 240, (last.y >> 16) - 190, ex, ey);
    CHECK(x == ex); // the last MCV
    CHECK(y == ey);
    CHECK(!last.selected); // select_mcv off (mission start, airstrike)
    REQUIRE(w.home_view(0, x, y, true));
    CHECK(w.units()[size_t(w.find_unit(m2id))].selected);
    CHECK(!w.units()[size_t(w.find_unit(m1id))].selected);
    CHECK(!w.units()[size_t(w.find_unit(aid))].selected);
    CHECK(w.units()[size_t(w.find_unit(enid))].selected); // FUN_004a5750(gSideId): own side only
}