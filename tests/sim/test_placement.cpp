// Building creation details and the kill / loss counters
// (docs/re/12 section 6, docs/re/60). Golden tests: they skip without data.
#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
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

// One mission, set up as the campaign does (map objects, rules, players).
struct Mission {
    formats::MapFile map;
    formats::MissionFile mis;
    formats::Templates t;
    sim::ExeTables tables;
    sim::Rng rng{1};
    std::unique_ptr<sim::GameMap> gm;
    std::unique_ptr<sim::World> w;

    Mission(platform::GameData& gd, const std::string& name, bool objects = true) {
        map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(
            formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
        auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
        t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
        auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
        gm = std::make_unique<sim::GameMap>(map, attr, tables, rng);
        auto placed = objects ? sim::place_map_objects(map, td, t, mis.house, tables) : std::vector<sim::Object>{};
        w = std::make_unique<sim::World>(*gm, t, tables, speed, placed, rng);
        w->set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w->init_players(mis);
    }

    int unit_record(bool infantry, int kind) const {
        for (size_t i = 0; i < t.unit_records.size(); ++i) {
            const auto& r = t.unit_records[i];
            if (r.infantry == infantry && r.kind == kind) return int(i);
        }
        return -1;
    }

    // An open area (vehicles and infantry, no objects), (size x size) around the result.
    bool open_area(int half, int& cx, int& cy) const {
        const auto& m = w->map();
        for (int y = half + 1; y < m.height() - half - 1; ++y) {
            for (int x = half + 1; x < m.width() - half - 1; ++x) {
                bool ok = true;
                for (int dy = -half; dy <= half && ok; ++dy) {
                    for (int dx = -half; dx <= half && ok; ++dx) {
                        const uint32_t f = m.at(x + dx, y + dy).flags;
                        if (!(f & sim::cell::kVehicles) || !(f & sim::cell::kInfantry) || (f & 0x13f8)) ok = false;
                    }
                }
                if (ok) {
                    cx = x;
                    cy = y;
                    return true;
                }
            }
        }
        return false;
    }
};

} // namespace

TEST_CASE("golden: map-placed buildings get the concrete hit point rule of FUN_004563b0 (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    // Every campaign mission: a map building's hit points follow the
    // concrete under its footprint after its own slabs were laid, and
    // flag 0x8000 (decay) marks the ones not fully on concrete.
    int buildings = 0, reduced = 0;
    std::string example;
    for (const char h : {'A', 'H', 'O'}) {
        for (int n = 1; n <= 9; ++n) {
            for (const char v : {'1', '2', '3'}) {
                const std::string name = std::string(1, h) + std::to_string(n) + "V" + v;
                if (!gd.find("MISSIONS/" + name + ".MAP") || !gd.find("MISSIONS/_" + name + ".MIS")) continue;
                Mission m(gd, name);
                for (const auto& b : m.w->buildings()) {
                    const auto& r = m.t.building_records[size_t(b.record)];
                    int on = 0, total = 0;
                    for (int k = 0; k < 16; ++k) {
                        if (!(r.footprint & (1u << k))) continue;
                        const int x = b.cell_x + (k & 3), y = b.cell_y + (k >> 2);
                        if (x >= m.w->map().width() || y >= m.w->map().height()) continue;
                        ++total;
                        if (m.w->map().at(x, y).flags & 0x800) ++on;
                    }
                    INFO(name << " building " << b.index << " record " << b.record);
                    ++buildings;
                    // FUN_0044dac0 has no bounds test; no 1.06 map building
                    // overhangs the map (place mask inside), and the footprint
                    // lies inside the place mask.
                    CHECK((r.footprint & ~r.place_mask) == 0);
                    for (int k = 0; k < 16; ++k) {
                        if (!(r.place_mask & (1u << k))) continue;
                        CHECK(b.cell_x + (k & 3) < m.w->map().width());
                        CHECK(b.cell_y + (k >> 2) < m.w->map().height());
                    }
                    if (r.flags & 0x400000) {
                        CHECK(b.hit_points == r.hit_points);
                        CHECK((b.flags & 0x8000) == 0);
                        continue;
                    }
                    CHECK(b.hit_points == ((on * r.hit_points) >> 1) / total + r.hit_points / 2);
                    CHECK(((b.flags & 0x8000) != 0) == (on != total));
                    if (on != total) {
                        ++reduced;
                        if (example.empty()) {
                            example = name + " record " + std::to_string(b.record) + " at " +
                                      std::to_string(b.cell_x) + "," + std::to_string(b.cell_y) + ": " +
                                      std::to_string(on) + "/" + std::to_string(total) + " on concrete, hp " +
                                      std::to_string(b.hit_points) + " of " + std::to_string(r.hit_points);
                        }
                    }
                }
            }
        }
    }
    MESSAGE(buildings << " map buildings, " << reduced << " partly off concrete; first: " << example);
    CHECK(buildings > 0);
    CHECK(reduced > 0);
}

TEST_CASE("golden: a new building converts other sides' units on its footprint, leaves its own, writes and clears skirt tiles (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Mission m(gd, "A4V1", false);
    auto& w = *m.w;
    const int veh = m.unit_record(false, 0), inf = m.unit_record(true, 0);
    REQUIRE(veh >= 0);
    REQUIRE(inf >= 0);
    int yard = -1;
    for (size_t i = 0; i < m.t.building_records.size(); ++i) {
        const auto& r = m.t.building_records[i];
        if (r.kind == 2 && (r.house_mask & (1u << w.player(0).house)) && (r.flags & 0x200000)) {
            yard = int(i);
            break;
        }
    }
    REQUIRE(yard >= 0);
    const auto& yr = m.t.building_records[size_t(yard)];
    REQUIRE((yr.width_px >> 5) == 3);
    REQUIRE((yr.height_px >> 5) == 3);
    int cx = 0, cy = 0;
    REQUIRE(m.open_area(3, cx, cy));
    // The yard occupies the top two rows of its 3x3 place mask (footprint
    // 0x77): the units stand on (1, 1), (2, 1) and (0, 0).
    const int bx = cx - 1, by = cy - 1;
    REQUIRE(yr.footprint == 0x77);

    // Side 1: a vehicle on (1, 1), a soldier on (2, 1); side 0: a vehicle on (0, 0).
    const int pv = w.create_unit(1, veh, cx, cy, cx, cy);
    REQUIRE(pv >= 0);
    const int enemy_veh = w.units()[size_t(pv)].index;
    const int pi = w.create_unit(1, inf, bx + 2, by + 1, bx + 2, by + 1);
    REQUIRE(pi >= 0);
    const int enemy_inf = w.units()[size_t(pi)].index;
    const int po = w.create_unit(0, veh, bx, by, bx, by);
    REQUIRE(po >= 0);
    const int own = w.units()[size_t(po)].index;
    const auto lost0 = w.player(1).units_lost;
    const auto killed0 = w.player(0).units_killed;
    const size_t units0 = w.units().size();

    const int bi = w.create_building(0, yard, bx, by, false);
    REQUIRE(bi >= 0);
    // The enemy vehicle: removed, a copy for side 0 in the same cell (state 7, flag 0x2000000).
    const auto& old_v = w.units()[size_t(w.find_unit(enemy_veh))];
    CHECK((old_v.flags & 0x600000) == 0x600000);
    CHECK(old_v.state == 0x11);
    int copies = 0;
    for (size_t i = units0; i < w.units().size(); ++i) {
        const auto& c = w.units()[i];
        if (c.owner != 0 || c.record != veh) continue;
        ++copies;
        CHECK(int(c.cur_x) == cx);
        CHECK(int(c.cur_y) == cy);
        CHECK(c.state == sim::state::kMove);
        CHECK((c.flags & 0x2000000) != 0);
    }
    CHECK(copies == 1);
    // The soldier: removed, not converted.
    const auto& old_i = w.units()[size_t(w.find_unit(enemy_inf))];
    CHECK((old_i.flags & 0x600000) == 0x600000);
    CHECK(old_i.state == 0x11);
    // Own units stay as they were.
    const auto& o = w.units()[size_t(w.find_unit(own))];
    CHECK((o.flags & 0x400000) == 0);
    CHECK(o.owner == 0);
    // Losses for side 1 (total and per record), no kill for the placer.
    CHECK(w.player(1).units_lost == lost0 + 2);
    CHECK(w.player(1).units_lost_by_record[size_t(veh)] == 1);
    CHECK(w.player(1).units_lost_by_record[size_t(inf)] == 1);
    CHECK(w.player(0).units_killed == killed0);
    // The footprint cells belong to side 0 now.
    for (int k = 0; k < 16; ++k) {
        if (!(yr.footprint & (1u << k))) continue;
        CHECK((w.map().at(bx + (k & 3), by + (k >> 2)).flags & 0x17) == 0x10);
    }

    // Skirt (FUN_0044f1b0): the two bottom rows, 0x263 + col + 0x14 * row,
    // 0x283 + ... on concrete (a construction yard lays its own).
    for (int k = 0; k < 2; ++k) {
        for (int c = 0; c < 3; ++c) {
            const auto& cell = w.map().at(bx + c, by + 1 + k);
            const int base = (cell.flags & 0x800) ? 0x283 : 0x263;
            INFO("skirt cell " << c << "," << k);
            CHECK(int(cell.tile) == base + c + 0x14 * k);
        }
    }

    // Removal (FUN_0044f2f0): concrete cells get 0x28b, the others a rubble tile.
    w.destroy_building(bi);
    for (int i = 0; i < 40 && !w.buildings()[size_t(bi)].removed; ++i) w.tick();
    REQUIRE(w.buildings()[size_t(bi)].removed);
    for (int k = 0; k < 2; ++k) {
        for (int c = 0; c < 3; ++c) {
            const auto& cell = w.map().at(bx + c, by + 1 + k);
            INFO("cleared skirt cell " << c << "," << k);
            if (cell.flags & 0x800) {
                CHECK(int(cell.tile) == 0x28b);
            } else {
                const int tl = cell.tile;
                CHECK(((tl >= 552 && tl <= 556) || (tl >= 572 && tl <= 574)));
            }
        }
    }
}

TEST_CASE("golden: kill and loss counters per record (FUN_0049d2e0, docs/re/60) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Mission m(gd, "A1V1", false);
    auto& w = *m.w;
    const int veh = m.unit_record(false, 0);
    REQUIRE(veh >= 0);
    int cx = 0, cy = 0;
    REQUIRE(m.open_area(4, cx, cy));
    int big = -1, spice_expl = -1;
    for (size_t i = 0; i < m.t.bullet_name.size(); ++i) {
        if (m.t.bullet_name[i] == "DEVASTATOR") big = int(i);
    }
    const auto* ids = m.t.block(0x6d60d8);
    REQUIRE(ids != nullptr);
    spice_expl = ids->bytes[0x32];
    REQUIRE(big >= 0);

    // A side-2 vehicle killed by side 0's hit: side 0 gets the kill at
    // [rec * 8 + 2], side 2 the loss at [rec].
    int p = w.create_unit(2, veh, cx, cy, cx, cy);
    REQUIRE(p >= 0);
    int id = w.units()[size_t(p)].index;
    w.damage_at(cx * 32 + 16, cy * 32 + 16, 0, big, 0, -1);
    CHECK(w.units()[size_t(w.find_unit(id))].state == 0x11);
    CHECK(w.player(0).units_killed == 1);
    CHECK(w.player(0).units_killed_by_record[size_t(veh) * 8 + 2] == 1);
    CHECK(w.player(2).units_lost == 1);
    CHECK(w.player(2).units_lost_by_record[size_t(veh)] == 1);

    // A spice explosion: the loss counts, no kill, no attacker recorded.
    p = w.create_unit(2, veh, cx + 3, cy, cx + 3, cy);
    REQUIRE(p >= 0);
    id = w.units()[size_t(p)].index;
    w.units()[size_t(p)].hit_points = 1;
    w.damage_at((cx + 3) * 32 + 16, cy * 32 + 16, 0, spice_expl, 0, -1);
    const auto& se = w.units()[size_t(w.find_unit(id))];
    CHECK(se.state == 0x11);
    CHECK(se.attacker_side == -1);
    CHECK(w.player(0).units_killed == 1);
    CHECK(w.player(2).units_lost == 2);
    CHECK(w.player(2).units_lost_by_record[size_t(veh)] == 2);

    // A building destroyed by a hit with no firer object still counts as
    // the attacker's kill at [rec * 8 + owner].
    int wall = -1;
    for (size_t i = 0; i < m.t.building_records.size(); ++i) {
        if (m.t.building_records[i].kind == 0xe) {
            wall = int(i);
            break;
        }
    }
    REQUIRE(wall >= 0);
    const int bi = w.create_building(2, wall, cx, cy + 3, true);
    REQUIRE(bi >= 0);
    w.damage_at(cx * 32 + 16, (cy + 3) * 32 + 16, 0, big, 0, -1);
    CHECK(w.buildings()[size_t(bi)].dying);
    CHECK(w.player(0).buildings_killed == 1);
    CHECK(w.player(0).buildings_killed_by_record[size_t(wall) * 8 + 2] == 1);
    CHECK(w.player(2).buildings_lost == 1);

    // RemoveSide kills through KillUnit, which counts nothing.
    p = w.create_unit(2, veh, cx - 3, cy, cx - 3, cy);
    REQUIRE(p >= 0);
    id = w.units()[size_t(p)].index;
    w.remove_side(2);
    CHECK(w.units()[size_t(w.find_unit(id))].state == 0x11);
    CHECK(w.player(2).units_lost == 2);
    CHECK(w.player(2).buildings_lost == 1);

    // The counters survive a save and load.
    const auto saved = w.save_state();
    Mission m2(gd, "A1V1", false);
    REQUIRE(m2.w->load_state(saved));
    CHECK(m2.w->player(0).units_killed_by_record[size_t(veh) * 8 + 2] == 1);
    CHECK(m2.w->player(2).units_lost_by_record[size_t(veh)] == 2);
    CHECK(m2.w->player(0).buildings_killed_by_record[size_t(wall) * 8 + 2] == 1);
}
