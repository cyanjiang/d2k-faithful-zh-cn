// Sell / repair order handlers, primary reassignment, the capture of a dying
// building and of a docked harvester, the free harvester delivery, failed
// spawns and DoMCVs' +0x2E (docs/re/12, docs/re/21, docs/re/30). Golden
// tests: they skip without data.
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

    int building(int kind, const std::string& name = "") const {
        for (size_t i = 0; i < t.building_records.size(); ++i) {
            const auto& r = t.building_records[i];
            if (r.kind == kind && (name.empty() || t.building_name[r.name_index] == name)) return int(i);
        }
        return -1;
    }
    int unit(int kind) const {
        for (size_t i = 0; i < t.unit_records.size(); ++i) {
            if (t.unit_records[i].kind == kind) return int(i);
        }
        return -1;
    }
    // A free, drivable area of (2 half + 1)^2 cells (no objects).
    bool open_area(int half, int& cx, int& cy, int skip = 0) const {
        const auto& m = w->map();
        for (int y = half + 2; y < m.height() - half - 1; ++y) {
            for (int x = half + 1; x < m.width() - half - 1; ++x) {
                bool ok = true;
                for (int dy = -half; dy <= half && ok; ++dy) {
                    for (int dx = -half; dx <= half && ok; ++dx) {
                        const uint32_t f = m.at(x + dx, y + dy).flags;
                        if (!(f & sim::cell::kVehicles) || (f & 0x13f8)) ok = false;
                    }
                }
                if (ok && skip-- <= 0) {
                    cx = x;
                    cy = y;
                    return true;
                }
            }
        }
        return false;
    }
    // Creates a building of side p in the next free 5 x 5 area (top-left
    // at its corner); -1 if none.
    int place(int p, int record, bool preplaced) {
        int cx = 0, cy = 0;
        if (!open_area(2, cx, cy)) return -1;
        return w->create_building(p, record, cx - 2, cy - 2, preplaced);
    }
};

bool has_sound(const sim::World& w, const std::string& suffix) {
    for (const auto& s : w.sounds) {
        if (s.name.size() >= suffix.size() && s.name.compare(s.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("golden: the sell and repair order handlers (FUN_00456ee0 / FUN_00456fb0) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Mission m(gd, "A1V1", false);
    auto& w = *m.w;
    w.local_player = 0;
    const int light = m.building(7);
    const int wall = m.building(0xe);
    REQUIRE(light >= 0);
    REQUIRE(wall >= 0);
    const int a = m.place(0, light, true);
    const int b = m.place(0, light, true);
    const int c = m.place(0, light, true);
    const int wl = m.place(0, wall, true);
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);
    REQUIRE(c >= 0);
    REQUIRE(wl >= 0);
    const int hp = m.t.building_records[size_t(light)].hit_points;
    for (int i : {a, b, c}) { // full hit points, no concrete decay
        w.buildings()[size_t(i)].hit_points = hp;
        w.buildings()[size_t(i)].flags &= ~0x8000u;
    }

    // Repair: the click needs own, record flag 0x200 and hp below the
    // record's.
    CHECK_FALSE(w.can_repair_building(0, b)); // full hit points
    CHECK_FALSE(w.can_repair_building(1, b)); // not the side's
    CHECK_FALSE(w.can_repair_building(0, wl)); // walls lack 0x200
    // The handler only toggles flag 1 (no hp test); DoRepairs clears it at
    // full hit points.
    w.order_repair_building(0, b);
    CHECK((w.buildings()[size_t(b)].flags & 1) != 0);
    w.order_repair_building(0, b);
    CHECK((w.buildings()[size_t(b)].flags & 1) == 0);
    w.buildings()[size_t(b)].hit_points = hp - 100;
    CHECK(w.can_repair_building(0, b));
    w.player(0).cash = 5000;
    w.order_repair_building(0, b);
    for (int i = 0; i < 80; ++i) w.tick();
    CHECK(w.buildings()[size_t(b)].hit_points == hp);
    CHECK((w.buildings()[size_t(b)].flags & 1) == 0);

    // Selling: flag 4, the counter at frames * 3 - 1, the building deselected
    // (+0x35 = 0), the SELL voice.
    w.buildings()[size_t(a)].selected = true;
    w.sounds.clear();
    w.order_sell(0, a);
    const auto& ba = w.buildings()[size_t(a)];
    CHECK((ba.flags & 4) != 0);
    CHECK_FALSE(ba.selected);
    const int n = int(ba.buildup);
    CHECK(n > 1);
    CHECK(has_sound(w, "_SELL"));
    // The handler does not test flags & 6: a second order restarts the countdown.
    w.tick();
    CHECK(int(w.buildings()[size_t(a)].buildup) == n - 1);
    w.order_sell(0, a);
    CHECK(int(w.buildings()[size_t(a)].buildup) == n);
    // The click does test it.
    CHECK_FALSE(w.can_sell_building(0, a));
    CHECK(w.can_sell_building(0, b));
    // Being entered (0x1000000) or dying (state 0x11): refused.
    w.buildings()[size_t(b)].flags |= 0x1000000;
    w.order_sell(0, b);
    CHECK((w.buildings()[size_t(b)].flags & 4) == 0);
    w.destroy_building(c);
    REQUIRE(w.buildings()[size_t(c)].dying);
    w.order_sell(0, c);
    CHECK((w.buildings()[size_t(c)].flags & 4) == 0);
    // Another side's index: not this side's building.
    w.buildings()[size_t(b)].flags &= ~0x1000000u;
    w.order_sell(1, b);
    CHECK((w.buildings()[size_t(b)].flags & 4) == 0);
    // A wall (kind 0xe): no build-up frames, removed at once (flag 8), no voice.
    w.sounds.clear();
    w.order_sell(0, wl);
    CHECK((w.buildings()[size_t(wl)].flags & 8) != 0);
    CHECK_FALSE(has_sound(w, "_SELL"));
    // The repair click also accepts a dying own building (FUN_004a5640
    // finds it until its cells are cleared; hp 0 is below the record's).
    CHECK(w.can_repair_building(0, c));
}

TEST_CASE("golden: a removed primary goes to the next building of the same name (FUN_0046e740) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Mission m(gd, "A1V1", false);
    auto& w = *m.w;
    const int barracks = m.building(11, "BARRACKS");
    const int sietch = m.building(11, "SIETCH");
    REQUIRE(barracks >= 0);
    REQUIRE(sietch >= 0);
    const int a = m.place(0, barracks, true);
    const int s = m.place(0, sietch, true);
    const int b = m.place(0, barracks, true);
    REQUIRE(a >= 0);
    REQUIRE(s >= 0);
    REQUIRE(b >= 0);
    REQUIRE(w.player(0).primary[11] == a);
    // Both kind 11; the Sietch comes first in the list but has another name.
    w.destroy_building(a);
    for (int i = 0; i < 60 && !w.buildings()[size_t(a)].removed; ++i) w.tick();
    REQUIRE(w.buildings()[size_t(a)].removed);
    CHECK(w.player(0).primary[11] == b);
    CHECK((w.buildings()[size_t(b)].flags & 0x80) != 0);
    CHECK((w.buildings()[size_t(s)].flags & 0x80) == 0);
}

TEST_CASE("golden: capturing a dying building keeps it dying; a docked harvester is converted by the footprint pass (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Mission m(gd, "A1V1", false);
    auto& w = *m.w;
    const int refinery = m.building(4);
    const int harvester = m.unit(1);
    REQUIRE(refinery >= 0);
    REQUIRE(harvester >= 0);

    // A refinery destroyed during the walk-in: FUN_004a4740 still finds it,
    // the capture runs, and FUN_0049e120(new, 2) is refused for the copied
    // state 0x11, so the captured copy is removed when its countdown ends.
    const int r1 = m.place(1, refinery, true);
    REQUIRE(r1 >= 0);
    w.destroy_building(r1);
    REQUIRE(w.buildings()[size_t(r1)].dying);
    const size_t nb = w.buildings().size();
    REQUIRE(w.capture_building(1, 0, r1));
    REQUIRE(w.buildings().size() == nb + 1);
    const int n1 = int(nb);
    CHECK(w.buildings()[size_t(n1)].owner == 0);
    CHECK(w.buildings()[size_t(n1)].dying);
    for (int i = 0; i < 60 && !w.buildings()[size_t(n1)].removed; ++i) w.tick();
    CHECK(w.buildings()[size_t(n1)].removed);

    // A harvester docked (state 0xf) on a refinery that is captured: the
    // dock cell (2, 1) is not in the refinery footprint (0x36), so
    // CreateBuilding's footprint pass never sees it; the explicit loop of
    // FUN_0046d6d0 converts it with keepState 1 (state 0xf kept, +0x58 = the
    // new building), one loss for the victim.
    const int r2 = m.place(1, refinery, true);
    REQUIRE(r2 >= 0);
    // The dock cell: top-left + record +0x60 / +0x61 (doc 13 section 4.1).
    const auto& rr = m.t.building_records[size_t(refinery)];
    const int dx = w.buildings()[size_t(r2)].cell_x + rr.spawn_x, dy = w.buildings()[size_t(r2)].cell_y + rr.spawn_y;
    const int hi = w.create_unit(1, harvester, dx, dy, dx, dy);
    REQUIRE(hi >= 0);
    const int hid = w.units()[size_t(hi)].index;
    w.units()[size_t(hi)].state = 0xf;
    w.units()[size_t(hi)].refinery = w.building_slot(r2);
    REQUIRE((w.map().at(dx, dy).flags & 8) != 0);
    const auto lost0 = w.player(1).units_lost;
    const size_t nu = w.units().size();
    REQUIRE(w.capture_building(1, 0, r2));
    CHECK(w.units()[size_t(w.find_unit(hid))].state == 0x11);
    CHECK(w.player(1).units_lost == lost0 + 1);
    int copies = 0;
    for (size_t i = nu; i < w.units().size(); ++i) {
        const auto& u = w.units()[i];
        if (u.owner != 0 || u.record != harvester) continue;
        ++copies;
        CHECK(u.state == 0xf); // ConvertUnit(.., 1)
        CHECK(w.refinery_of(u) == int(w.buildings().size()) - 1);
        CHECK((u.flags & 0x2000000) != 0);
    }
    CHECK(copies == 1);
}

TEST_CASE("golden: refinery free harvester entry, failed spawn, voice houses, DoMCVs +0x2E (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Mission m(gd, "A1V1", false);
    auto& w = *m.w;
    w.local_player = 0;

    // FUN_004563b0: the free harvester delivery has +0x35 = 1 (carryall)
    // and +0x36 = 4 at (cx + 2, cy + 1).
    const int refinery = m.building(4);
    const size_t d0 = w.deliveries(0).size();
    const int r = m.place(0, refinery, false);
    REQUIRE(r >= 0);
    const int rx = w.buildings()[size_t(r)].cell_x, ry = w.buildings()[size_t(r)].cell_y;
    REQUIRE(w.deliveries(0).size() == d0 + 1);
    const auto& e = w.deliveries(0).back();
    CHECK(e.type == 1);
    CHECK(e.action == 4);
    CHECK(e.silent);
    CHECK(e.x == rx + 2);
    CHECK(e.y == ry + 1);

    // FUN_0046f900: a local house other than 0..2 plays no house voice.
    const int house = w.player(0).house;
    w.sounds.clear();
    w.player(0).house = decltype(w.player(0).house)(3);
    w.voice(0, "PRIMARY");
    CHECK(w.sounds.empty());
    w.player(0).house = house;
    w.voice(0, "PRIMARY");
    CHECK(w.sounds.size() == 1);

    // A failed creation at a ready slot (pool full): FUN_004467e0 clears the
    // slot and plays *_UNITREADY anyway (the result of FUN_00455870 is not
    // tested), so the unit is lost.
    const int heavy = m.building(8);
    REQUIRE(heavy >= 0);
    int tank = -1; // a vehicle from the heavy factory for this house
    for (size_t i = 0; i < m.t.unit_records.size() && tank < 0; ++i) {
        const auto& ur = m.t.unit_records[i];
        if (!ur.infantry && ur.prereq1 == m.t.building_records[size_t(heavy)].name_index &&
            (ur.prereq1_mask & m.t.building_records[size_t(heavy)].house_mask)) {
            tank = int(i);
        }
    }
    REQUIRE(tank >= 0);
    const int f = m.place(0, heavy, true);
    REQUIRE(f >= 0);
    int ux = 0, uy = 0;
    REQUIRE(m.open_area(1, ux, uy));
    int filler = 0;
    while (w.create_unit(0, tank, ux, uy, ux, uy) >= 0 && filler < 5000) ++filler;
    REQUIRE(filler < 5000);
    auto& slot = w.player(0).unit_slots[0];
    slot = sim::ProductionSlot{};
    slot.item = tank;
    slot.progress = sim::kProgressDone;
    w.sounds.clear();
    const size_t units0 = w.units().size();
    w.tick();
    CHECK(w.player(0).unit_slots[0].item < 0);
    CHECK(w.units().size() <= units0);
    CHECK(has_sound(w, "_UNITREADY"));
}

TEST_CASE("golden: DoMCVs deploys an MCV of a side with a build list only when +0x2E is set (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    // The first campaign mission with an active AI side that has a build list.
    std::unique_ptr<Mission> mp;
    int s = -1;
    for (const char* name : {"A2V1", "A3V1", "A4V1", "A5V1", "A6V1", "H2V1", "H3V1", "O2V1", "O3V1"}) {
        if (!gd.find(std::string("MISSIONS/") + name + ".MAP")) continue;
        mp = std::make_unique<Mission>(gd, name);
        mp->w->set_ai(mp->mis);
        mp->w->tick(); // the build lists
        for (int p = 0; p < 8 && s < 0; ++p) {
            if (p != mp->w->local_player && mp->w->ai_active(p) && !mp->w->player(p).building_list.empty()) s = p;
        }
        if (s >= 0) {
            MESSAGE("DoMCVs test in " << std::string(name) << ", side " << s);
            break;
        }
    }
    REQUIRE(s >= 0);
    Mission& m = *mp;
    auto& w = *m.w;
    const int mcv = m.unit(6);
    REQUIRE(mcv >= 0);
    // A deployable site (CanDeploy) with no units around, for the test MCV.
    int cx = -1, cy = -1;
    for (int y = 3; y < w.map().height() - 3 && cx < 0; ++y) {
        for (int x = 3; x < w.map().width() - 3 && cx < 0; ++x) {
            if (!w.can_deploy(x, y) || (w.map().at(x, y).flags & 0x3f8)) continue;
            cx = x;
            cy = y;
        }
    }
    REQUIRE(cx >= 0);
    const int ui = w.create_unit(s, mcv, cx, cy, cx, cy);
    REQUIRE(ui >= 0);
    const int id = w.units()[size_t(ui)].index;
    CHECK(w.units()[size_t(ui)].delivery_entry == 0); // never written by CreateUnit; slots start zeroed
    bool deployed = false;
    for (int i = 0; i < 130; ++i) {
        w.tick();
        const int j = w.find_unit(id);
        if (j < 0 || w.units()[size_t(j)].state == 0x1e || w.units()[size_t(j)].state == 0x11) deployed = true;
    }
    CHECK_FALSE(deployed);
    const int j = w.find_unit(id);
    REQUIRE(j >= 0);
    w.units()[size_t(j)].delivery_entry = 1; // as a delivery from entry 1 (0x49e687)
    for (int i = 0; i < 130 && !deployed; ++i) {
        w.tick();
        const int q = w.find_unit(id);
        if (q < 0 || w.units()[size_t(q)].state == 0x1e || w.units()[size_t(q)].state == 0x11) deployed = true;
    }
    CHECK(deployed);
}
