// Combat: KillUnit (FUN_0049cde0) and the Death Hand's anti-air quirk
// (docs/re/18 section 2.2.1, docs/re/41 section 8).
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

    int bullet(const char* name) const {
        for (size_t i = 0; i < t.bullet_name.size(); ++i)
            if (t.bullet_name[i] == name) return int(i);
        return -1;
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
};

} // namespace

TEST_CASE("golden: a delivery carryall cannot be damaged (flag 0x8000000); killed by RemoveSide it leaves its delivery entry active and started (FUN_0049cde0, docs/re/18 2.2.1) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Mission m(gd, "A1V1");
    auto& w = *m.w;
    int veh = -1;
    for (size_t i = 0; i < m.t.unit_records.size() && veh < 0; ++i)
        if (!m.t.unit_records[i].infantry && m.t.unit_records[i].kind == 0) veh = int(i);
    REQUIRE(veh >= 0);
    const int big = m.bullet("DEVASTATOR");
    REQUIRE(big >= 0);
    int cx = 0, cy = 0;
    REQUIRE(m.open_area(2, cx, cy));
    const int s = 1;
    REQUIRE(w.player_active(s));
    const size_t entries0 = w.deliveries(s).size();
    w.queue_delivery(s, cx, cy, {veh}, 1, true);
    REQUIRE(w.deliveries(s).size() == entries0 + 1);
    const size_t e = entries0;
    // The launch (FUN_0046def0) runs for side s on ticks with tick & 7 == s.
    int cid = -1;
    for (int i = 0; i < 20 && cid < 0; ++i) {
        w.tick();
        for (const auto& u : w.units())
            if (u.owner == s && u.delivery == int(e) && (u.flags & 0x40)) cid = u.index;
    }
    REQUIRE(cid >= 0);
    CHECK(w.deliveries(s)[e].started);
    {
        // The launch sets 0x8000b00 (0x46e36b) and nothing clears bit 27, so
        // the damage loop (0x49d806) skips the carryall: no hit.
        auto& c = w.units()[size_t(w.find_unit(cid))];
        CHECK((c.flags & 0x8000000) != 0);
        c.hit_points = 1;
        w.damage_at(c.x >> 16, c.y >> 16, c.altitude, big, 0, -1); // an air hit (height > 6 px)
        CHECK(c.hit_points == 1);
        CHECK(c.state != 0x11);
    }
    // KillUnit through RemoveSide.
    w.remove_side(s);
    {
        const auto& c = w.units()[size_t(w.find_unit(cid))];
        CHECK(c.state == 0x11);
        CHECK(c.hit_points == 0);
    }
    // KillUnit does not touch the delivery table: the entry stays active
    // and started, and the cargo never arrives.
    CHECK(w.deliveries(s)[e].active);
    CHECK(w.deliveries(s)[e].started);
    for (int i = 0; i < 100; ++i) w.tick();
    CHECK(w.find_unit(cid) < 0); // removed after its countdown
    CHECK(w.deliveries(s)[e].active);
    CHECK(w.deliveries(s)[e].started);
    int owned = 0;
    for (const auto& u : w.units()) owned += u.owner == s && u.record == veh && u.state != 0x11 && !u.remove;
    CHECK(owned == 0);
}

TEST_CASE("golden: KillUnit plays S_MANDYING1..9 for infantry at the pixel's cell; no infantry record has a death explosion (FUN_0049cde0) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Mission m(gd, "A1V1");
    auto& w = *m.w;
    int inf = -1;
    for (size_t i = 0; i < m.t.unit_records.size(); ++i) {
        const auto& r = m.t.unit_records[i];
        if (r.infantry) {
            INFO("record " << i);
            CHECK(r.death_explosion < 0); // so the explosion and the scream never both run
            if (inf < 0 && r.kind == 0) inf = int(i);
        }
    }
    REQUIRE(inf >= 0);
    const int big = m.bullet("DEVASTATOR");
    REQUIRE(big >= 0);
    int cx = 0, cy = 0;
    REQUIRE(m.open_area(2, cx, cy));
    const int p = w.create_unit(2, inf, cx, cy, cx, cy);
    REQUIRE(p >= 0);
    const int id = w.units()[size_t(p)].index;
    const int px = w.units()[size_t(p)].x / 65536, py = w.units()[size_t(p)].y / 65536;
    w.sounds.clear();
    w.damage_at(px, py, 0, big, 0, -1);
    const auto& u = w.units()[size_t(w.find_unit(id))];
    CHECK(u.state == 0x11);
    CHECK(u.countdown == 0x32);
    int screams = 0;
    for (const auto& s : w.sounds) {
        if (s.name.rfind("S_MANDYING", 0) != 0) continue;
        ++screams;
        const int n = std::stoi(s.name.substr(10));
        CHECK(n >= 1);
        CHECK(n <= 9);
        CHECK(s.cx == px / 32);
        CHECK(s.cy == py / 32);
    }
    CHECK(screams == 1);
}


TEST_CASE("golden: Death Hand anti-air quirk: AA locks on only when another aircraft flags the missile's launch cell (docs/re/41 section 8) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    // The missile never sets 0x800000 and its cell bytes stay at the cell it
    // was created on; the air pass of FUN_004a4900 only looks at flagged
    // cells and matches aircraft by those bytes.
    auto run = [&](bool flag_cell, int& locked_tick, int& fired_other) {
        Mission m(gd, "A1V1");
        auto& w = *m.w;
        const auto* ids = m.t.block(0x6d60d8);
        REQUIRE(ids);
        const int dh = ids->bytes[0x13];
        REQUIRE(m.t.unit_records[size_t(dh)].kind == 10);
        int aa = -1;
        for (size_t i = 0; i < m.t.unit_records.size() && aa < 0; ++i) {
            const auto& r = m.t.unit_records[i];
            if (!r.infantry && r.kind == 0 && r.weapon != 0xff && m.t.bullets[r.weapon].anti_air) aa = int(i);
        }
        REQUIRE(aa >= 0);
        int cx = 0, cy = 0;
        REQUIRE(m.open_area(2, cx, cy));
        const int p = w.create_unit(1, dh, cx, cy, cx, cy);
        REQUIRE(p >= 0);
        const int mid = w.units()[size_t(p)].index;
        CHECK((w.units()[size_t(p)].flags & 0x20040) == 0x20040);
        const int a = w.create_unit(0, aa, cx + 2, cy, cx + 2, cy);
        REQUIRE(a >= 0);
        const int aid = w.units()[size_t(a)].index;
        locked_tick = -1;
        fired_other = 0;
        for (int i = 0; i < 40 && locked_tick < 0; ++i) {
            if (flag_cell) m.gm->at(cx, cy).flags |= 0x800000; // another aircraft over that cell
            w.tick();
            for (const auto& b : w.bullets) {
                if (b.target == mid) locked_tick = i;
                else if (b.firer == aid) ++fired_other;
            }
            const int k = w.find_unit(mid);
            REQUIRE(k >= 0);
            CHECK(int(w.units()[size_t(k)].cur_x) == cx); // the cell bytes stay
            CHECK(int(w.units()[size_t(k)].cur_y) == cy);
        }
    };
    int with_flag = -1, without_flag = -1, other1 = 0, other2 = 0;
    run(true, with_flag, other1);
    run(false, without_flag, other2);
    MESSAGE("AA rocket at the Death Hand: tick " << with_flag << " with the cell flag, " << without_flag << " without");
    CHECK(with_flag >= 0);
    CHECK(without_flag < 0);
    CHECK(other2 == 0);
}

TEST_CASE("golden: which 1.06 campaign missions give an AI a Death Hand (docs/re/41 section 9) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    std::string listed, possible;
    for (const char h : {'A', 'H', 'O'}) {
        for (int n = 1; n <= 9; ++n) {
            for (const char v : {'1', '2', '3'}) {
                const std::string name = std::string(1, h) + std::to_string(n) + "V" + v;
                if (!gd.find("MISSIONS/" + name + ".MAP") || !gd.find("MISSIONS/_" + name + ".MIS")) continue;
                Mission m(gd, name);
                auto& w = *m.w;
                w.set_ai(m.mis);
                const auto* ids = m.t.block(0x6d60d8);
                REQUIRE(ids);
                const int dh = ids->bytes[0x13];
                const auto& dr = m.t.unit_records[size_t(dh)];
                for (int s = 0; s < 8; ++s) {
                    if (!w.player_active(s) || !w.ai_u8(s, 1)) continue;
                    const auto& pl = w.player(s);
                    bool in_list = false;
                    for (int rec : pl.unit_list) in_list |= rec == dh;
                    // Could it ever list record 24: its house, the tech level,
                    // and a palace (owned, or a yard and the palace's tech).
                    const bool house = (dr.house_mask >> pl.house) & 1;
                    bool palace = false, yard = false, palace_tech = false;
                    for (const auto& b : w.buildings()) {
                        if (b.owner != s || b.removed) continue;
                        const auto& br = m.t.building_records[size_t(b.record)];
                        if (br.tech[0] == 9) yard = true;
                        if (br.name_index == dr.prereq1 && ((br.house_mask >> pl.house) & 1)) palace = true;
                    }
                    for (const auto& br : m.t.building_records)
                        if (br.name_index == dr.prereq1 && ((br.house_mask >> pl.house) & 1) && br.tech[0] <= pl.tech) palace_tech = true;
                    const bool can = house && pl.tech >= dr.tech && (palace || (yard && palace_tech));
                    if (in_list) listed += " " + name + "/" + std::to_string(s);
                    if (can) possible += " " + name + "/" + std::to_string(s);
                    if (in_list) CHECK(can);
                }
            }
        }
    }
    MESSAGE("AI sides listing the Death Hand at the start:" << listed);
    CHECK(listed == " A9V1/1 A9V2/1 O8V1/1 O9V1/1 O9V2/1");
    MESSAGE("AI sides that can get it (house, tech, palace or yard):" << possible);
}
TEST_CASE("golden: the tick runs side by side: units, buildings, bullets, explosions of side 0, then side 1 (FUN_00458e90, docs/re/43 section 5) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Mission m(gd, "A1V1");
    auto& w = *m.w;
    int veh = -1;
    for (size_t i = 0; i < m.t.unit_records.size() && veh < 0; ++i)
        if (!m.t.unit_records[i].infantry && m.t.unit_records[i].kind == 0) veh = int(i);
    REQUIRE(veh >= 0);
    const int big = m.bullet("DEVASTATOR");
    REQUIRE(big >= 0);
    REQUIRE((m.t.bullets[size_t(big)].flags & 0x920) == 0); // a plain shot
    int cx = 0, cy = 0;
    REQUIRE(m.open_area(2, cx, cy));
    const int a = w.create_unit(1, veh, cx - 2, cy, cx - 2, cy); // hit by side 0's bullet
    REQUIRE(a >= 0);
    const int b = w.create_unit(0, veh, cx + 2, cy, cx + 2, cy); // hit by side 1's bullet
    REQUIRE(b >= 0);
    const int ida = w.units()[size_t(a)].index, idb = w.units()[size_t(b)].index;
    auto shoot = [&](int side, int target) {
        auto& u = w.units()[size_t(w.find_unit(target))];
        u.hit_points = 1;
        sim::Bullet s;
        s.type = big;
        s.owner = side;
        s.flags = m.t.bullets[size_t(big)].flags;
        s.speed = m.t.bullets[size_t(big)].speed;
        s.tx = u.x >> 16;
        s.ty = u.y >> 16;
        s.x = u.x;
        s.y = u.y;
        s.height = 6 << 16;
        s.ticks = 0; // impact on its first update
        w.bullets.push_back(s);
    };
    shoot(0, ida);
    shoot(1, idb);
    w.tick();
    const auto& ua = w.units()[size_t(w.find_unit(ida))];
    const auto& ub = w.units()[size_t(w.find_unit(idb))];
    CHECK(ua.state == 0x11);
    CHECK(ub.state == 0x11);
    // Side 0's bullet killed the side-1 unit before side 1's units ran, so
    // it already had one state-0x11 update; side 1's bullet came after side
    // 0's units, so that victim has none yet.
    CHECK(int(ua.countdown) == 9);
    CHECK(int(ub.countdown) == 10);
}
TEST_CASE("golden: the head of the unit update runs for aircraft too (+0x2a, flag 1), and a dying attacker is dropped (FUN_00497b90) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Mission m(gd, "A1V1");
    auto& w = *m.w;
    int veh = -1, carry = -1;
    for (size_t i = 0; i < m.t.unit_records.size(); ++i) {
        const auto& r = m.t.unit_records[i];
        if (veh < 0 && !r.infantry && r.kind == 0) veh = int(i);
        if (carry < 0 && r.kind == 2) carry = int(i);
    }
    REQUIRE(veh >= 0);
    REQUIRE(carry >= 0);
    const int lmg = m.bullet("LMG");
    REQUIRE(lmg >= 0);
    int cx = 0, cy = 0;
    REQUIRE(m.open_area(2, cx, cy));
    // An aircraft hit: flag 1 set by the damage, then cleared with +0x2a = 0
    // by its own next update, as for ground units.
    const int c = w.create_unit(2, carry, cx, cy, cx, cy);
    REQUIRE(c >= 0);
    const int cid = w.units()[size_t(c)].index;
    {
        auto& u = w.units()[size_t(c)];
        REQUIRE((u.flags & 0x40) != 0);
        u.hit_age = 50;
        w.damage_at(u.x >> 16, u.y >> 16, u.altitude, lmg, 1, -1);
        CHECK((u.flags & 1) != 0);
    }
    w.tick();
    {
        const auto& u = w.units()[size_t(w.find_unit(cid))];
        CHECK((u.flags & 1) == 0);
        CHECK(int(u.hit_age) == 0);
    }
    // The attacker check: a dying attacker is dropped, its side is kept.
    const int a = w.create_unit(0, veh, cx - 2, cy, cx - 2, cy);
    const int b = w.create_unit(1, veh, cx + 2, cy, cx + 2, cy);
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);
    const int aid = w.units()[size_t(a)].index, bid = w.units()[size_t(b)].index;
    {
        auto& ua = w.units()[size_t(w.find_unit(aid))];
        ua.attacker = bid;
        ua.attacker_side = 1;
        auto& ub = w.units()[size_t(w.find_unit(bid))];
        ub.hit_points = 1;
        w.damage_at(ub.x >> 16, ub.y >> 16, 0, lmg, 0, -1);
        REQUIRE(ub.state == 0x11);
    }
    w.tick();
    const auto& ua = w.units()[size_t(w.find_unit(aid))];
    CHECK(ua.attacker == -1);
    CHECK(ua.attacker_side == 1);
}
TEST_CASE("golden: a harvester's refinery index is looked up in its own side (FUN_004a4740): a converted harvester re-picks among its new side's refineries (docs/re/40 section 7) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Mission m(gd, "A1V1");
    auto& w = *m.w;
    int harv = -1, ref = -1;
    for (size_t i = 0; i < m.t.unit_records.size() && harv < 0; ++i)
        if (m.t.unit_records[i].kind == 1) harv = int(i);
    for (size_t i = 0; i < m.t.building_records.size() && ref < 0; ++i)
        if (m.t.building_records[i].kind == 4 && (m.t.building_records[i].house_mask & (1u << w.player(1).house))) ref = int(i);
    REQUIRE(harv >= 0);
    REQUIRE(ref >= 0);
    // A side-1 refinery and a side-0 one, and a side-0 harvester whose +0x58
    // still names the side-1 refinery, as a deviated harvester keeps its old
    // side's index.
    int ref0 = -1;
    for (size_t i = 0; i < m.t.building_records.size() && ref0 < 0; ++i)
        if (m.t.building_records[i].kind == 4 && (m.t.building_records[i].house_mask & (1u << w.player(0).house))) ref0 = int(i);
    REQUIRE(ref0 >= 0);
    auto free_area = [&](int avoid_x, int avoid_y, int& ox, int& oy) {
        for (int y = 3; y < w.map().height() - 6; ++y)
            for (int x = 3; x < w.map().width() - 6; ++x) {
                if (avoid_x >= 0 && std::abs(x - avoid_x) < 6 && std::abs(y - avoid_y) < 6) continue;
                bool ok = true;
                for (int dy = 0; dy < 5 && ok; ++dy)
                    for (int dx = 0; dx < 5 && ok; ++dx) {
                        const uint32_t f = w.map().at(x + dx, y + dy).flags;
                        ok = (f & sim::cell::kVehicles) && !(f & 0x13f8);
                    }
                if (ok) {
                    ox = x;
                    oy = y;
                    return true;
                }
            }
        return false;
    };
    int ax = 0, ay = 0, bx = 0, by = 0;
    REQUIRE(free_area(-1, -1, ax, ay));
    REQUIRE(free_area(ax, ay, bx, by));
    const int foreign = w.create_building(1, ref, ax, ay, true);
    const int own = w.create_building(0, ref0, bx, by, true);
    REQUIRE(foreign >= 0);
    REQUIRE(own >= 0);
    const int h = w.create_unit(0, harv, bx + 4, by + 4, bx + 4, by + 4);
    REQUIRE(h >= 0);
    const int hid = w.units()[size_t(h)].index;
    {
        auto& u = w.units()[size_t(h)];
        u.refinery = w.building_slot(foreign); // a slot number of side 1's pool
        u.load = 3;
        u.state = 0xd; // to the refinery
    }
    w.tick();
    const auto& u = w.units()[size_t(w.find_unit(hid))];
    CHECK(w.refinery_of(u) == own);
}

TEST_CASE("golden: explosion flags in Templates.bin: flag 8 only on EX_DEVIATE / EX_GREENEXP, no 0x40 / 0x80 tint (docs/re/47 section A) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    Mission m(gd, "A1V1");
    for (size_t e = 0; e < m.t.explosion_flags.size(); ++e) {
        INFO("explosion " << e << " " << (e < m.t.explosion_name.size() ? m.t.explosion_name[e] : std::string()));
        CHECK((m.t.explosion_flags[e] & 0xc0) == 0);
        if (m.t.explosion_flags[e] & 8) {
            CHECK((m.t.explosion_name[e] == "EX_DEVIATE" || m.t.explosion_name[e] == "EX_GREENEXP"));
        }
    }
}