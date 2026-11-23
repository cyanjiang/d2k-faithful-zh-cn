// The per-side object pool and the references that read a slot
// (docs/re/50). Golden tests: they skip without data.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
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

namespace {

struct Pool {
    formats::MapFile map;
    formats::MissionFile mis;
    formats::Templates t;
    sim::ExeTables tables;
    sim::Rng rng{1};
    std::vector<uint8_t> vars;
    std::vector<sim::Object> placed;
    sim::SpeedTable speed{};
    std::unique_ptr<sim::GameMap> gm;
    std::unique_ptr<sim::World> w;

    Pool(platform::GameData& gd, const std::string& name) {
        map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(
            formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
        auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
        t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
        speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
        vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
        gm = std::make_unique<sim::GameMap>(map, attr, tables, rng);
        placed = sim::place_map_objects(map, td, t, mis.house, tables);
        w = std::make_unique<sim::World>(*gm, t, tables, speed, placed, rng, std::span<const uint8_t>{}, vars);
        w->set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w->init_players(mis);
    }
    // A free drivable cell from (x0, y0) on, scanning rows.
    bool free_cell(int& x, int& y, int x0, int y0) const {
        for (int cy = y0; cy < gm->height() - 1; ++cy) {
            for (int cx = (cy == y0 ? x0 : 1); cx < gm->width() - 1; ++cx) {
                const uint32_t f = gm->at(cx, cy).flags;
                if ((f & sim::cell::kVehicles) && !(f & 0x3f8)) {
                    x = cx;
                    y = cy;
                    return true;
                }
            }
        }
        return false;
    }
    int unit(int side, const char* name, int x, int y) {
        const int rec = t.unit_record(t.unit_index(name), w->player(side).house);
        REQUIRE(rec >= 0);
        const int pos = w->create_unit(side, rec, x, y, x, y);
        REQUIRE(pos >= 0);
        return w->units()[size_t(pos)].index;
    }
    sim::Unit& by_id(int id) {
        const int i = w->find_unit(id);
        REQUIRE(i >= 0);
        return w->units()[size_t(i)];
    }
    // A building of the side's house at the first spot that passes the checks.
    int building(int side, const char* name, int from_y = 1) {
        const int rec = t.building_record(t.building_index(name), w->player(side).house);
        REQUIRE(rec >= 0);
        std::vector<sim::PlaceCell> cells;
        for (int y = from_y; y < gm->height(); ++y) {
            for (int x = 0; x < gm->width(); ++x) {
                if (w->placement_check(side, rec, x, y, cells)) return w->create_building(side, rec, x, y, false);
            }
        }
        FAIL("no spot for " << name);
        return -1;
    }
    int bullet_id(size_t k) const {
        const auto* ids = t.block(0x6d60d8);
        REQUIRE(ids);
        REQUIRE(ids->bytes.size() > k);
        return ids->bytes[k];
    }
    // Kills a unit with the Devastator blast (5000) at its own pixel.
    void kill(int id) {
        const auto& u = by_id(id);
        w->damage_at(u.x >> 16, u.y >> 16, 0, bullet_id(0x33), (u.owner + 1) & 7, -1);
        REQUIRE(by_id(id).state == 0x11);
    }
    // Drops the effects and bullets without giving their slots back, so the
    // only slot a test frees is the one it watches.
    void quiet() {
        w->effects.clear();
        w->bullets.clear();
    }
};

} // namespace

TEST_CASE("golden: object pool: a fresh side hands out slots 999, 998, ... in creation order (0x46ba3d) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Pool p(gd, "A1V1");
    auto& w = *p.w;
    for (int s = 0; s < 8; ++s) {
        // The map objects of the side, in the map's creation order.
        std::vector<int> slots;
        size_t ui = 0, bi = 0;
        for (const auto& o : p.placed) {
            if (o.owner != s) {
                if (o.building) ++bi;
                else ++ui;
                continue;
            }
            if (o.building) slots.push_back(w.buildings()[bi++].slot);
            else slots.push_back(w.units()[ui++].slot);
        }
        for (size_t k = 0; k < slots.size(); ++k) CHECK(slots[k] == 999 - int(k));
        const auto& free = w.pool_free_slots(s);
        REQUIRE(free.size() == 1000 - slots.size());
        if (!free.empty()) CHECK(free.back() == 999 - int(slots.size()));
    }
}

TEST_CASE("golden: object pool: a dead unit's slot comes back when its countdown ends and the side's next object takes it (FUN_0046bdb0, LIFO) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Pool p(gd, "A1V1");
    auto& w = *p.w;
    int x = 0, y = 0;
    REQUIRE(p.free_cell(x, y, 1, 1));
    const int a = p.unit(0, "TRIKE", x, y);
    int bx = 0, by = 0;
    REQUIRE(p.free_cell(bx, by, 1, y + 10)); // out of the blast
    const int b = p.unit(0, "TRIKE", bx, by);
    const uint16_t sa = p.by_id(a).slot, sb = p.by_id(b).slot;
    CHECK(sb == sa - 1);
    p.kill(a);
    p.quiet();
    const size_t before = w.pool_free_slots(0).size();
    int ticks = 0;
    while (w.find_unit(a) >= 0 && ticks < 100) {
        w.tick();
        ++ticks;
        p.quiet();
    }
    // A vehicle stays countdown (10) + 1 updates in state 0x11.
    CHECK(ticks == 11);
    REQUIRE(w.pool_free_slots(0).size() == before + 1);
    CHECK(w.pool_free_slots(0).back() == sa);
    // The next object of side 0 gets the freed slot, not a fresh one.
    const int c = p.unit(0, "TRIKE", x, y);
    CHECK(p.by_id(c).slot == sa);
    // Other sides' pools are untouched.
    const int d = p.unit(1, "TRIKE", x, y);
    CHECK(p.by_id(d).slot != sa);
}

TEST_CASE("golden: object pool: a destroyed building's slot returns in its side's building pass of the update its +0x3b timer ends (31 updates) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Pool p(gd, "A1V1");
    auto& w = *p.w;
    int victim = -1;
    for (const auto& b : w.buildings()) {
        if (b.owner == 0 && !b.removed) victim = b.index;
    }
    REQUIRE(victim >= 0);
    const uint16_t slot = w.buildings()[size_t(victim)].slot;
    w.destroy_building(victim);
    p.quiet();
    auto freed = [&] {
        const auto& f = w.pool_free_slots(0);
        return std::find(f.begin(), f.end(), slot) != f.end();
    };
    int ticks = 0;
    while (!w.buildings()[size_t(victim)].removed && ticks < 100) {
        CHECK_FALSE(freed());
        w.tick();
        ++ticks;
        p.quiet();
    }
    CHECK(ticks == 31); // +0x3b = 30, decremented while non-zero, removed on the next update
    // Freed in that update, after the survivors (0x19) took their slots.
    REQUIRE(!w.pool_free_slots(0).empty());
    CHECK(w.pool_free_slots(0).back() == slot);
}

TEST_CASE("golden: a building's first attacker is a pool slot; DoDefence then follows whichever unit holds it (0x49d65e, 0x41c2e8) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Pool p(gd, "A1V1");
    auto& w = *p.w;
    int target = -1;
    for (const auto& b : w.buildings()) {
        if (b.owner == 0 && !b.removed && target < 0) target = b.index;
    }
    REQUIRE(target >= 0);
    int x = 0, y = 0;
    REQUIRE(p.free_cell(x, y, 1, 1));
    const int a = p.unit(1, "LIGHT_INF", x, y);
    const uint16_t sa = p.by_id(a).slot;
    // A small hit by the soldier's gun: the first attacker is kept as (side, slot).
    const auto& tb = w.buildings()[size_t(target)];
    const int gun = p.t.unit_records[size_t(p.by_id(a).record)].weapon;
    w.damage_at(tb.cell_x * 32 + 16, tb.cell_y * 32 + 16, 0, gun, 1, a);
    REQUIRE(w.buildings()[size_t(target)].attacker_side == 1);
    CHECK(w.buildings()[size_t(target)].attacker == sa);
    REQUIRE(w.unit_in_slot(1, sa) != nullptr);
    CHECK(w.unit_in_slot(1, sa)->index == a);
    // The soldier dies and its slot is freed; the next side-1 unit takes it.
    p.kill(a);
    p.quiet();
    while (w.find_unit(a) >= 0) {
        w.tick();
        p.quiet();
    }
    CHECK(w.unit_in_slot(1, sa) == nullptr);
    const int b = p.unit(1, "LIGHT_INF", x, y);
    CHECK(p.by_id(b).slot == sa);
    // The building still names slot sa: it now reads as the new unit.
    CHECK(w.buildings()[size_t(target)].attacker == sa);
    REQUIRE(w.unit_in_slot(1, sa) != nullptr);
    CHECK(w.unit_in_slot(1, sa)->index == b);
    // A building firer is recorded too (any firer index, 0x49d65e).
    int fx = 0, fy = 0;
    REQUIRE(p.free_cell(fx, fy, x + 4, y + 4));
    const int frec = p.t.building_record(p.t.building_index("WIND_TRAP"), w.player(1).house);
    REQUIRE(frec >= 0);
    const int firer = w.create_building(1, frec, fx, fy, true);
    REQUIRE(firer >= 0);
    int t2 = -1;
    for (const auto& ob : w.buildings()) {
        if (ob.owner == 0 && !ob.removed && ob.index != target && ob.attacker < 0 && t2 < 0) t2 = ob.index;
    }
    if (t2 < 0) {
        int tx = 0, ty = 0;
        REQUIRE(p.free_cell(tx, ty, 1, fy + 4));
        t2 = w.create_building(0, p.t.building_record(p.t.building_index("WIND_TRAP"), w.player(0).house), tx, ty, true);
    }
    REQUIRE(t2 >= 0);
    const auto& tb2 = w.buildings()[size_t(t2)];
    w.damage_at(tb2.cell_x * 32 + 16, tb2.cell_y * 32 + 16, 0, gun, 1, -1000 - firer);
    CHECK(w.buildings()[size_t(t2)].attacker == w.buildings()[size_t(firer)].slot);
    CHECK(w.unit_in_slot(1, w.buildings()[size_t(t2)].attacker) == nullptr); // a building's slot: no defence
}
TEST_CASE("golden: a harvester's refinery (+0x58) is a slot: a new refinery in the freed slot is used instead of the nearest search (0x4997d2) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Pool p(gd, "A1V1");
    auto& w = *p.w;
    p.building(0, "WIND_TRAP");
    const int r1 = p.building(0, "REFINERY");
    REQUIRE(r1 >= 0);
    const uint16_t s1 = w.buildings()[size_t(r1)].slot;
    int x = 0, y = 0;
    REQUIRE(p.free_cell(x, y, 1, 1));
    const int h = p.unit(0, "HARVESTER", x, y);
    CHECK(p.by_id(h).refinery == s1);
    CHECK(w.refinery_of(p.by_id(h)) == r1);
    // While it dies the refinery still counts (no state test).
    w.destroy_building(r1);
    p.quiet();
    CHECK(w.refinery_of(p.by_id(h)) == r1);
    while (!w.buildings()[size_t(r1)].removed) {
        w.tick();
        p.quiet();
    }
    CHECK(w.refinery_of(p.by_id(h)) == -1);
    // A second refinery takes the freed slot (LIFO): the harvester's old
    // +0x58 now names it.
    const int r2 = p.building(0, "REFINERY");
    REQUIRE(r2 >= 0);
    CHECK(w.buildings()[size_t(r2)].slot == s1);
    CHECK(p.by_id(h).refinery == s1);
    CHECK(w.refinery_of(p.by_id(h)) == r2);
}

TEST_CASE("golden: the unit head drops a dying attacker (0x497ce5) and the bullet head a dying firer (0x49be92) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Pool p(gd, "A1V1");
    auto& w = *p.w;
    int x = 0, y = 0, x2 = 0, y2 = 0;
    REQUIRE(p.free_cell(x, y, 1, 1));
    REQUIRE(p.free_cell(x2, y2, 1, y + 8));
    const int a = p.unit(0, "TRIKE", x, y);
    const int v = p.unit(1, "HARVESTER", x2, y2);
    const int gun = p.t.unit_records[size_t(p.by_id(a).record)].weapon;
    w.damage_at(p.by_id(v).x >> 16, p.by_id(v).y >> 16, 0, gun, 0, a);
    REQUIRE(p.by_id(v).state != 0x11);
    CHECK(p.by_id(v).attacker == a);
    CHECK(p.by_id(v).attacker_side == 0);
    // A bullet of side 0 fired by the trike, still in flight.
    sim::Bullet bl;
    bl.type = gun;
    bl.owner = 0;
    bl.firer = a;
    bl.x = (x * 32 + 16) << 16;
    bl.y = (y * 32 + 16) << 16;
    bl.ticks = 50;
    bl.slot = uint16_t(w.pool_free_slots(0).back()); // not popped: cleared by quiet() below anyway
    w.tick();
    CHECK(p.by_id(v).attacker == a); // alive: kept
    w.bullets.push_back(bl);
    p.kill(a);
    w.effects.clear();
    w.tick();
    CHECK(p.by_id(v).attacker == -1);
    REQUIRE(w.bullets.size() == 1);
    CHECK(w.bullets[0].firer == -1);
    w.bullets.clear();
}

TEST_CASE("golden: the free lists and slots survive a save and load (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Pool p(gd, "A1V1");
    auto& w = *p.w;
    int x = 0, y = 0;
    REQUIRE(p.free_cell(x, y, 1, 1));
    const int a = p.unit(0, "TRIKE", x, y);
    p.unit(0, "TRIKE", x, y);
    p.kill(a);
    for (int i = 0; i < 40; ++i) w.tick();
    const auto saved = w.save_state();
    Pool q(gd, "A1V1");
    REQUIRE(q.w->load_state(saved));
    for (int s = 0; s < 8; ++s) CHECK(q.w->pool_free_slots(s) == w.pool_free_slots(s));
    REQUIRE(q.w->units().size() == w.units().size());
    for (size_t i = 0; i < w.units().size(); ++i) CHECK(q.w->units()[i].slot == w.units()[i].slot);
    for (int i = 0; i < 200; ++i) {
        w.tick();
        q.w->tick();
    }
    CHECK(q.w->save_state() == w.save_state());
}

TEST_CASE("golden: an order record keeps the target's (side, slot): a target freed before the order runs is replaced by the unit now in its slot (record +0x20 / +0x22, ProcessOrder case 1) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Pool p(gd, "A1V1");
    auto& w = *p.w;
    int x = 0, y = 0, x2 = 0, y2 = 0;
    REQUIRE(p.free_cell(x, y, 1, 1));
    REQUIRE(p.free_cell(x2, y2, 1, y + 8));
    const int a = p.unit(0, "TRIKE", x, y);
    const int t = p.unit(1, "LIGHT_INF", x2, y2);
    const uint16_t st = p.by_id(t).slot;
    p.kill(t);
    p.quiet();
    // Run to the update before the one that frees it (+0x2f == 0).
    while (p.by_id(t).countdown != 0) {
        w.tick();
        p.quiet();
    }
    for (auto& u : w.units()) u.selected = u.index == a;
    w.post(sim::make_order(sim::order::kAttackUnit, 0, t)); // a click on the dying soldier
    w.tick(); // its countdown ends: the slot is freed
    p.quiet();
    REQUIRE(w.find_unit(t) < 0);
    const int n = p.unit(1, "LIGHT_INF", x2, y2); // the side's next object takes the slot
    REQUIRE(p.by_id(n).slot == st);
    w.tick(); // the order runs (2 ticks after the post)
    const auto& au = p.by_id(a);
    CHECK(au.target_kind == 1);
    CHECK(au.target == n);
}
TEST_CASE("Facing8 (FUN_0045e020): 0 is north and the steps run clockwise, multiples of 4") {
    // FUN_0045e020(x1, y1, x2, y2) = FUN_0049e160(x1 - x2, y2 - y1) -> ((a + 0x40) & 0xfff) >> 7 & ~3.
    const int dx[8] = {0, 1, 1, 1, 0, -1, -1, -1}, dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    for (int k = 0; k < 8; ++k) CHECK(sim::facing8(10, 10, 10 + dx[k], 10 + dy[k]) == 4 * k);
    CHECK(sim::facing8(10, 10, 12, 9) == 4);
    CHECK(sim::facing8(10, 10, 11, 8) == 0);
    CHECK(sim::facing8(10, 10, 13, 11) == 8);
    CHECK(sim::facing8(10, 10, 8, 15) == 16);
}