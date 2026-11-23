// Leftover simulation points settled from the exe on 2026-10-16: the cash
// drip netting and the storage cut (FUN_0046c7d0 / FUN_0044abe0, docs/re/11),
// the auto-fire inside MoveStep (0x4943f3, docs/re/18 section 3.6) and the
// CASUALTIES ratio on the x87 stack (0x453d5c, docs/re/19 section 2.3).
#include <doctest/doctest.h>

#include <bit>
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

    Mission(platform::GameData& gd, const std::string& name) {
        map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(
            formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
        auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
        t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
        auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
        const auto circles = formats::read_file(gd.find("Data/bin/CIRCLES.BIN").value());
        const auto vars = formats::read_file(gd.find("Data/bin/VARS.BIN").value());
        gm = std::make_unique<sim::GameMap>(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        w = std::make_unique<sim::World>(*gm, t, tables, speed, placed, rng, circles, vars);
        w->set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w->init_players(mis);
    }

    // An open area (vehicles and infantry, no objects), (2 half + 1)^2 around the result.
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

    // An armed vehicle record (not a worm, not an aircraft).
    int armed_vehicle() const {
        for (size_t i = 0; i < t.unit_records.size(); ++i) {
            const auto& r = t.unit_records[i];
            if (!r.infantry && r.kind == 0 && r.weapon != 0xff) return int(i);
        }
        return -1;
    }

    // Side 0 (no AI) with storage: a refinery (kind 4) is placed in an open
    // area when it has none, then one tick computes the capacity.
    int side_with_storage() {
        const int p = 0;
        if (!w->player_active(p) || w->ai_active(p)) return -1;
        if (w->player(p).capacity == 0) {
            int rec = -1;
            for (size_t i = 0; i < t.building_records.size() && rec < 0; ++i)
                if (t.building_records[i].kind == 4) rec = int(i);
            int cx = 0, cy = 0;
            if (rec < 0 || !open_area(3, cx, cy)) return -1;
            if (w->create_building(p, rec, cx - 2, cy - 2, true) < 0) return -1;
            w->tick();
        }
        return w->player(p).capacity > 0 ? p : -1;
    }
};

} // namespace

TEST_CASE("golden: the cash drip nets pending cash against pending spice through the real values (FUN_0046c7d0, docs/re/11) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    {
        Mission m(gd, "A1V1");
        const int p = m.side_with_storage();
        REQUIRE(p >= 0);
        REQUIRE(m.w->player(p).capacity >= 200);
        auto& pl = m.w->player(p);
        // |pending cash| < pending spice (0x46c894): PS += PC, spice -= PC,
        // cash += PC, PC = 0; then the step max(10, 400 / 16) = 25 of spice.
        pl.cash = 1000;
        pl.spice = 0;
        pl.pending_cash = -100;
        pl.pending_spice = 500;
        m.w->tick();
        CHECK(pl.cash == 900);
        CHECK(pl.spice == 125);
        CHECK(pl.pending_cash == 0);
        CHECK(pl.pending_spice == 375);
    }
    {
        Mission m(gd, "A1V1");
        const int p = m.side_with_storage();
        REQUIRE(p >= 0);
        auto& pl = m.w->player(p);
        // |pending cash| >= pending spice: PC += PS, cash -= PS, spice += PS,
        // PS = 0; then the step max(10, 400 / 16) = 25 out of cash.
        pl.cash = 1000;
        pl.spice = 0;
        pl.pending_cash = -500;
        pl.pending_spice = 100;
        m.w->tick();
        CHECK(pl.cash == 875);
        CHECK(pl.spice == 100);
        CHECK(pl.pending_cash == -375);
        CHECK(pl.pending_spice == 0);
    }
}

TEST_CASE("golden: spice over the capacity pays a pending spend first, and incoming pending spice is lost (FUN_0044abe0, docs/re/11) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    {
        Mission m(gd, "A1V1");
        const int p = m.side_with_storage();
        REQUIRE(p >= 0);
        auto& pl = m.w->player(p);
        const int32_t cap = pl.capacity;
        pl.cash = 0;
        pl.pending_cash = 0;
        pl.spice = cap + 500;
        pl.pending_spice = -300;
        m.w->tick();
        // The drip first: step max(10, 300 / 16) = 18, spice cap + 482,
        // pending -282; then the cut: pending += 482, spice = cap.
        CHECK(pl.capacity == cap);
        CHECK(pl.spice == cap);
        CHECK(pl.pending_spice == -282 + 482);
    }
    {
        Mission m(gd, "A1V1");
        const int p = m.side_with_storage();
        REQUIRE(p >= 0);
        auto& pl = m.w->player(p);
        const int32_t cap = pl.capacity;
        pl.cash = 0;
        pl.pending_cash = 0;
        pl.spice = cap + 500;
        pl.pending_spice = 300;
        m.w->tick();
        CHECK(pl.spice == cap);
        CHECK(pl.pending_spice == 0);
    }
}

TEST_CASE("golden: MoveStep auto-fires in every stepping state, not at the destination (0x4943f3, docs/re/18 section 3.6) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    // A unit in state 6 (attack ground) waiting for a route (+0x78 != 0)
    // fires at an enemy next to it on its 8-tick slot.
    {
        Mission m(gd, "A1V1");
        auto& w = *m.w;
        const int rec = m.armed_vehicle();
        REQUIRE(rec >= 0);
        int cx = 0, cy = 0;
        REQUIRE(m.open_area(2, cx, cy));
        int enemy_side = -1;
        for (int q = 1; q < 8 && enemy_side < 0; ++q)
            if (w.player_active(q) && m.mis.alliance[0][size_t(q)] == 1) enemy_side = q;
        REQUIRE(enemy_side >= 0);
        const int a = w.create_unit(0, rec, cx, cy, cx, cy);
        REQUIRE(a >= 0);
        const int aid = w.units()[size_t(a)].index;
        const int b = w.create_unit(enemy_side, rec, cx + 1, cy, cx + 1, cy);
        REQUIRE(b >= 0);
        const int bid = w.units()[size_t(b)].index;
        {
            auto& u = w.units()[size_t(w.find_unit(aid))];
            u.state = 6;
            u.dest_x = uint8_t(cx < w.map().width() / 2 ? w.map().width() - 2 : 1);
            u.dest_y = uint8_t(cy);
            u.route_count = 0;
            u.repath_delay = 200;
            u.target_kind = 0;
        }
        bool aimed = false;
        for (int i = 0; i < 9 && !aimed; ++i) {
            w.tick();
            const auto& u = w.units()[size_t(w.find_unit(aid))];
            aimed = u.target_kind == 1 && u.target == bid;
        }
        CHECK(aimed);
    }
    // A unit in state 7 at its destination: MoveStep returns 0 before the
    // auto-fire, so nothing is aimed this tick even on its slot.
    {
        Mission m(gd, "A1V1");
        auto& w = *m.w;
        const int rec = m.armed_vehicle();
        int cx = 0, cy = 0;
        REQUIRE(m.open_area(2, cx, cy));
        int enemy_side = -1;
        for (int q = 1; q < 8 && enemy_side < 0; ++q)
            if (w.player_active(q) && m.mis.alliance[0][size_t(q)] == 1) enemy_side = q;
        REQUIRE(enemy_side >= 0);
        const int a = w.create_unit(0, rec, cx, cy, cx, cy);
        REQUIRE(a >= 0);
        const int aid = w.units()[size_t(a)].index;
        const uint32_t slot = w.units()[size_t(a)].slot;
        while ((slot & 7) != ((w.tick_count() + 1) & 7)) w.tick();
        const int b = w.create_unit(enemy_side, rec, cx + 1, cy, cx + 1, cy);
        REQUIRE(b >= 0);
        {
            auto& u = w.units()[size_t(w.find_unit(aid))];
            u.state = 7;
            u.dest_x = u.cur_x;
            u.dest_y = u.cur_y;
            u.steps = 0;
            u.repath_delay = 200;
            u.target_kind = 0;
        }
        w.tick();
        const auto& u = w.units()[size_t(w.find_unit(aid))];
        CHECK(u.target_kind == 0);
        CHECK(u.state == 2);
    }
}

TEST_CASE("golden: CASUALTIES compares the exact x87 product n * 0.01f with the float ratio (0x453d5c, docs/re/19 section 2.3) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Mission m(gd, "A1V1");
    auto& w = *m.w;
    formats::MissionFile s;
    formats::MissionCondition c;
    c.type = 4; // CASUALTIES
    c.side = 0;
    c.c = 0;
    // float32(5 * 0.01f) = 0x3d4ccccc rounds down: the exact product
    // 0.0499999988... is above it, the float32 product equals it.
    c.ratio = std::bit_cast<float>(0x3d4cccccu);
    s.conditions.push_back(c);
    formats::MissionEvent e;
    e.type = 10; // WIN
    e.conditions = 1;
    e.cond[0] = 0;
    s.events.push_back(e);
    w.set_script(s);
    w.player(0).units_lost = 5;
    w.player(0).units_killed = 100; // 5 * 100 / 100 = 5
    w.tick();
    CHECK(w.won);
}
