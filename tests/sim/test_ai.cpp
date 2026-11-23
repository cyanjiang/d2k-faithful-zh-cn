// The computer player's exe routines, checked one by one against the 1.06
// exe (docs/re/22, 23, 29, 53, 57a; settled 2026-10-16). Golden tests skip
// without the game data.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
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
#include "sim/x87.hpp"

using namespace d2k;
namespace fs = std::filesystem;

namespace d2k::sim {
// Reaches the private exe routines of World (friend in world.hpp).
struct WorldTestAccess {
    World& w;
    Player& player(int p) { return w.players_[size_t(p)]; }
    std::vector<Building>& buildings() { return w.buildings_; }
    std::vector<Unit>& units() { return w.units_; }
    void set_tick(uint32_t t) { w.tick_count_ = t; }
    void set_u8(int s, size_t o, uint8_t v) { w.set_ai_u8(s, o, v); }
    void set_u32(int s, size_t o, uint32_t v) { w.set_ai_u32(s, o, v); }
    void set_f32(int s, size_t o, float v) {
        uint32_t b;
        std::memcpy(&b, &v, 4);
        w.set_ai_u32(s, o, b);
    }
    int best_unit(int s) { return w.ai_best_unit(s); }
    void overrides(int s) { w.ai_overrides(s); }
    void group_position(int s, int g, int& x, int& y) { w.ai_group_position(s, g, x, y); }
    int best_side(int s) { return w.ai_best_side(s); }
    void base_position(int s, int& x, int& y) { w.ai_base_position(s, x, y); }
    int group_for_delivery(int s, int entry) { return w.ai_group_for_delivery(s, entry); }
    void splinter(int s, int from, int to, int strength) { w.ai_splinter(s, from, to, strength); }
    bool random_position(int s, int rec, int& x, int& y, int tries) { return w.ai_random_position(s, rec, x, y, tries); }
    uint32_t can_place(int s, int rec, int x, int y) { return w.ai_can_place_building(s, rec, x, y); }
    int unit_strength(int s, const Unit& u, int job) { return w.ai_unit_strength(s, u, job); }
    bool nearest_hostile(int s, int g) { return w.ai_nearest_hostile(s, g); }
    uint8_t& alliance(int a, int b) { return w.alliance_[size_t(a)][size_t(b)]; }
};
} // namespace d2k::sim

namespace {

constexpr size_t G(int g, size_t off) { return size_t(g) * 0x2C + off; }

// One campaign mission as the game sets it up (objects, rules, players, AI).
struct Mission {
    formats::MapFile map;
    formats::MissionFile mis;
    formats::Templates t;
    sim::ExeTables tables;
    sim::Rng rng{1};
    std::unique_ptr<sim::GameMap> gm;
    std::unique_ptr<sim::World> w;

    Mission(platform::GameData& gd, const std::string& name, bool ai = true) {
        map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
        mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
        auto attr = formats::TileAttributes::parse(
            formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
        tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
        auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
        t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
        auto speed = sim::parse_speed_table(formats::read_file(gd.find("Data/bin/SPEED.BIN").value()));
        gm = std::make_unique<sim::GameMap>(map, attr, tables, rng);
        auto placed = sim::place_map_objects(map, td, t, mis.house, tables);
        w = std::make_unique<sim::World>(*gm, t, tables, speed, placed, rng);
        w->set_armour(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
        w->init_players(mis);
        w->set_script(mis);
        if (ai) w->set_ai(mis);
    }
};

std::vector<std::string> campaign_missions(const fs::path& root) {
    std::vector<std::string> out;
    for (const auto& e : fs::directory_iterator(root / "MISSIONS")) {
        const std::string n = e.path().filename().string();
        if (n.size() > 5 && n[0] == '_' && e.path().extension() == ".MIS") {
            const std::string base = n.substr(1, n.size() - 5);
            if (fs::exists(root / "MISSIONS" / (base + ".MAP"))) out.push_back(base);
        }
    }
    return out;
}

} // namespace

TEST_CASE("x87: arithmetic rounds at 53 bits (the CRT's _PC_53), loads are exact (docs/re/22 section 3.2)") {
    using namespace sim::x87;
    const uint64_t p53 = uint64_t(1) << 53;
    CHECK(trunc_i64(from_u64(p53 + 1)) == int64_t(p53 + 1));             // FILD: exact
    CHECK(trunc_i64(add(from_u64(p53), from_u64(1))) == int64_t(p53));  // tie to even at 53 bits
    CHECK(trunc_i64(add(from_u64(p53), from_u64(3))) == int64_t(p53 + 4));
    CHECK(trunc_i64(mul(from_u64(p53 + 1), from_u64(3))) == int64_t(3 * p53 + 4));
    // The percentage table of doc 23 section 4.1 is the same at 53 bits.
    const int offs[] = {25, 15, 1, 0, -1, -5, -15, -24};
    const int want[] = {6249, 5749, 5049, 5000, 150, -50, -550, -1000};
    for (int k = 0; k < 8; ++k) CHECK(percent_of(offs[k], 5000) == want[k]);
}

TEST_CASE("golden: AI routine details from the exe (docs/re/22, 23, 29, 53, 57a) (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    const int s = 1; // A1V2: the Harkonnen AI

    SUBCASE("FindBestUnitToBuild: with none owned (N = 0) the last candidate wins (0x41bb1e)") {
        Mission m(gd, "A1V2");
        sim::WorldTestAccess a{*m.w};
        std::vector<int> cand;
        for (int r = 0; r < 60 && cand.size() < 3; ++r) {
            bool owned = false;
            for (const auto& u : m.w->units()) owned = owned || (u.owner == s && u.record == r);
            if (!owned && m.t.unit_records[size_t(r)].kind != 1 && m.t.unit_records[size_t(r)].kind != 6 &&
                m.t.unit_records[size_t(r)].hit_points > 0) {
                cand.push_back(r);
            }
        }
        REQUIRE(cand.size() == 3);
        a.player(s).unit_list = cand;
        a.set_u8(s, 0x08, 1); // storage full: skip the money test
        for (int r = 0; r < 60; ++r) a.set_f32(s, 0x19F8 + 4 * size_t(r), 0.0f);
        a.set_f32(s, 0x19F8 + 4 * size_t(cand[0]), 1.0f);
        a.set_f32(s, 0x19F8 + 4 * size_t(cand[1]), 1.0f);
        sim::Rng before = m.rng;
        // cand[2] has priority 0, but a NaN best takes every later candidate.
        CHECK(a.best_unit(s) == cand[2]);
        before.next();
        before.next(); // one rand() per candidate with a share
        CHECK(m.rng.state() == before.state());
        // All priorities 0: P = 0, every share is 0 / 0, no rand(), no pick.
        a.set_f32(s, 0x19F8 + 4 * size_t(cand[0]), 0.0f);
        a.set_f32(s, 0x19F8 + 4 * size_t(cand[1]), 0.0f);
        const uint32_t st = m.rng.state();
        CHECK(a.best_unit(s) == -1);
        CHECK(m.rng.state() == st);
    }

    SUBCASE("DoOverrides rule (b): hp = max / 10 qualifies (exact 53-bit product, 0x422dc6)") {
        Mission m(gd, "A3V1"); // side 2: an AI with a base
        const int s = 2;
        sim::WorldTestAccess a{*m.w};
        int rb = -1, rc = -1;
        const auto& br = m.t.building_records;
        for (int r = 0; r < int(br.size()) && rb < 0; ++r) {
            if (br[size_t(r)].hit_points >= 100 && br[size_t(r)].hit_points % 10 == 0 && br[size_t(r)].power >= 0 &&
                !br[size_t(r)].needs_power && br[size_t(r)].sell_priority < 200) {
                for (int q = 0; q < int(br.size()); ++q) {
                    if (br[size_t(q)].hit_points > 0 && !br[size_t(q)].needs_power &&
                        br[size_t(q)].sell_priority > br[size_t(r)].sell_priority) {
                        rb = r;
                        rc = q;
                        break;
                    }
                }
            }
        }
        REQUIRE(rb >= 0);
        auto& bs = a.buildings();
        int ib = -1, ic = -1;
        for (auto& b : bs) {
            if (b.removed || b.owner != s) continue;
            if (ib < 0) ib = b.index;
            else if (ic < 0) ic = b.index;
            else b.owner = 7; // out of the way
        }
        REQUIRE(ic >= 0);
        for (int hp_extra : {0, 1}) {
            for (int i : {ib, ic}) {
                bs[size_t(i)].flags &= ~0x400000Eu;
                bs[size_t(i)].hit_age = 0xff;
            }
            bs[size_t(ib)].record = rb;
            bs[size_t(ic)].record = rc;
            bs[size_t(ic)].hit_points = br[size_t(rc)].hit_points;
            bs[size_t(ib)].hit_points = br[size_t(rb)].hit_points / 10 + hp_extra;
            bs[size_t(ib)].hit_age = 0;
            auto& pl = a.player(s);
            pl.power_produced = 0;
            pl.power_used = 100;
            pl.spice = 0;
            pl.cash = 100; // below 400, not 0 (0 returns early)
            pl.capacity = 1000;
            a.set_u8(s, 0x07, 0);
            a.set_tick(uint32_t(s));
            a.overrides(s);
            const bool sold_b = (bs[size_t(ib)].flags & 0x4000000) != 0;
            const bool sold_c = (bs[size_t(ic)].flags & 0x4000000) != 0;
            CHECK(sold_b == (hp_extra == 0));
            CHECK(sold_c == (hp_extra == 1));
        }
    }

    SUBCASE("GetAveragePositionOfGroup: the cells of the pixel positions (0x420aa9)") {
        Mission m(gd, "A1V2");
        sim::WorldTestAccess a{*m.w};
        sim::Unit* one = nullptr;
        for (auto& u : a.units()) {
            if (u.owner == s && !one) one = &u;
            else if (u.owner == s && u.ai_group == 9) u.ai_group = 1;
        }
        REQUIRE(one);
        one->ai_group = 9;
        one->x = (11 * 32 + 16) << 16;
        one->y = (12 * 32 + 16) << 16;
        one->cur_x = 10;
        one->cur_y = 10;
        int x = 0, y = 0;
        a.group_position(s, 9, x, y);
        CHECK(x == 11);
        CHECK(y == 12);
    }

    SUBCASE("GetBestSideToAttack: the countdown steps first, 1 -> recalculate (FUN_0041cdf0)") {
        Mission m(gd, "A1V2");
        sim::WorldTestAccess a{*m.w};
        int enemy = -1;
        for (int k = 0; k < 8; ++k) {
            if (k != s && m.w->alliance(s, k) == 1) enemy = k;
        }
        REQUIRE(enemy >= 0);
        a.set_u8(s, 0x02, uint8_t(enemy));
        a.set_u32(s, 0x18, 2);
        a.best_side(s);
        CHECK(m.w->ai_u32(s, 0x18) == 1);
        a.best_side(s);
        CHECK(m.w->ai_u32(s, 0x18) == 25); // recalculated when the step reaches 0
    }

    SUBCASE("GetBestBasePosition remembers its first result in +0x24 / +0x28 (0x41e1d9)") {
        Mission m(gd, "A3V1");
        const int s = 2;
        sim::WorldTestAccess a{*m.w};
        a.set_u32(s, 0x24, 0xffffffffu);
        a.set_u32(s, 0x28, 0xffffffffu);
        int x = 0, y = 0;
        a.base_position(s, x, y);
        CHECK((x != 0 || y != 0));
        CHECK(m.w->ai_u32(s, 0x24) == uint32_t(x));
        CHECK(m.w->ai_u32(s, 0x28) == uint32_t(y));
    }

    SUBCASE("FindGroupForDelivery without a free group: 0xFF and the header writes (0x4204c6)") {
        Mission m(gd, "A1V2");
        sim::WorldTestAccess a{*m.w};
        for (int g = 1; g < 32; ++g) {
            a.set_u8(s, G(g, 0x52), 1);
            a.set_u8(s, G(g, 0x48), 2);
        }
        a.set_u32(s, 0x24, 30); // a remembered x: byte +0x26 is 0
        a.set_u8(s, 0x1D, 0);
        CHECK(a.group_for_delivery(s, 3) == 0xff);
        CHECK(m.w->ai_u8(s, 0x26) == 1);
        CHECK(m.w->ai_u8(s, 0x1D) == 3);
        CHECK(m.w->ai_u8(s, 0x1C) == 0xD);
        CHECK(m.w->ai_u32(s, 0x0C) == 100);
        CHECK(m.w->ai_u8(s, 0x02) == 0xff);
        CHECK(m.w->ai_u8(s, 0x03) == 0xff);
        // Byte +0x26 is now 1: a second call writes nothing.
        a.set_u8(s, 0x1D, 0);
        CHECK(a.group_for_delivery(s, 4) == 0xff);
        CHECK(m.w->ai_u8(s, 0x1D) == 0);
    }

    SUBCASE("CreateSplinterGroup: a negative limit moves every eligible unit (JAE at 0x41cae5)") {
        Mission m(gd, "A1V2");
        sim::WorldTestAccess a{*m.w};
        int eligible = 0;
        for (auto& u : a.units()) {
            if (u.owner != s) continue;
            u.ai_group = 2;
            const int k = m.t.unit_records[size_t(u.record)].kind;
            eligible += u.state != 8 && k != 1 && k != 2;
        }
        REQUIRE(eligible > 1);
        // Find a GroupSizeRandomPercentage whose next roll gives a negative limit.
        const int strength = 1000;
        int q = 0;
        for (int c = 2; c < 256 && q == 0; ++c) {
            sim::Rng peek = m.rng;
            const uint32_t r = peek.next();
            if (sim::x87::percent_of(int32_t(c - int32_t(r % uint32_t(2 * c))), strength) < 0) q = c;
        }
        REQUIRE(q > 0);
        a.set_u8(s, 0xF6F, uint8_t(q));
        a.set_u8(s, G(5, 0x48), 0);
        a.splinter(s, 2, 5, strength);
        int moved = 0;
        for (const auto& u : m.w->units()) moved += u.owner == s && u.ai_group == 5;
        CHECK(moved == eligible);
    }

    SUBCASE("FindRandomBuildingPosition: an inverted base box tries (rand + x0) & 0xFF (0x420f8b)") {
        Mission m(gd, "A3V1");
        const int s = 2;
        sim::WorldTestAccess a{*m.w};
        bool any = false;
        for (const auto& b : m.w->buildings()) any = any || (b.owner == s && !b.removed);
        REQUIRE(any);
        int rec = -1;
        for (const auto& b : m.w->buildings()) {
            if (b.owner == s && !b.removed) rec = b.record;
        }
        auto& bb = a.player(s).base_box; // y min, y max, x min, x max
        const int W = m.w->map().width(), H = m.w->map().height();
        bb = {uint8_t(H), uint8_t(10), uint8_t(W), uint8_t(12)};
        const int x0 = W - 4, x1 = 14, y0 = H - 4, y1 = 12;
        sim::Rng peek = m.rng;
        int ex = -1, ey = -1;
        uint32_t best = 0;
        for (int k = 0; k < 8; ++k) {
            const uint32_t rx = peek.next(), ry = peek.next();
            const int x = uint8_t(rx % uint32_t(x1 - x0) + uint32_t(x0));
            const int y = uint8_t(ry % uint32_t(y1 - y0) + uint32_t(y0));
            const uint32_t v = a.can_place(s, rec, x, y);
            if (best < v) {
                best = v;
                ex = x;
                ey = y;
            }
        }
        int x = -1, y = -1;
        CHECK(a.random_position(s, rec, x, y, 8) == (best != 0));
        if (best != 0) {
            CHECK(x == ex);
            CHECK(y == ey);
        }
        CHECK(m.rng.state() == peek.state());
    }

    SUBCASE("GetNearestHostileThing: buildings alone fail and keep the group cell (FUN_00420700)") {
        Mission m(gd, "A1V2");
        sim::WorldTestAccess a{*m.w};
        // Only enemy buildings: units of the enemies go to a neutral side.
        for (auto& u : a.units()) {
            if (u.owner != s && m.w->alliance(s, u.owner) == 1) u.flags |= 0x400000; // skipped (0x400144)
        }
        const int g = 6;
        a.set_u8(s, G(g, 0x50), 20);
        a.set_u8(s, G(g, 0x51), 21);
        a.set_u8(s, G(g, 0x2E), 0xff);
        a.set_u8(s, G(g, 0x2F), 0xff);
        CHECK_FALSE(a.nearest_hostile(s, g));
        CHECK(m.w->ai_u8(s, G(g, 0x50)) == 20);
        CHECK(m.w->ai_u8(s, G(g, 0x51)) == 21);
    }
}

TEST_CASE("golden: the AI data guards of docs/re/53 section 11 hold for every 1.06 mission (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "MISSIONS")) return;
    platform::GameData gd(root);
    int missions = 0, sides = 0, free_pct = 0, empty = 0;
    for (const auto& name : campaign_missions(root)) {
        Mission m(gd, name);
        ++missions;
        for (int s = 0; s < 8; ++s) {
            const auto& blk = m.mis.ai[size_t(s)];
            if (blk.size() != 0x1DB8 || !blk[1]) continue;
            ++sides;
            // FreePercentage's group 0xFF reads its job from the next block.
            if (blk[0xF0D] != 0) {
                ++free_pct;
                REQUIRE(s < 7);
                CHECK(m.mis.ai[size_t(s + 1)][0xE64] == 0);
                int count = 0;
                for (const auto& b : m.w->buildings()) count += b.owner == s + 1;
                CHECK(count < 139);
            }
            // Never a zero defend period, ProtectTicks or group size roll.
            const int n = std::min<int>(m.w->ai_u8(s, 0x1D51), 5);
            for (int a = 0; a < n; ++a) CHECK(m.w->ai_u32(s, 0x1D54 + size_t(a) * 0x14 + 0xC) != 0);
            CHECK(m.w->ai_u32(s, 0x1D34) != 0);
            CHECK(m.w->ai_u8(s, 0xF6F) != 0);
            CHECK(m.w->ai_u8(s, 0x0A) != 0);
            CHECK(m.w->ai_u8(s, 0x0B) == 0);
            // The base box at activation is flat or inverted only for a side
            // that starts with no building and no unit (W..0, H..0).
            const auto& bb = m.w->player(s).base_box;
            int objects = 0;
            for (const auto& b : m.w->buildings()) objects += b.owner == s && !b.removed;
            for (const auto& u : m.w->units()) objects += u.owner == s && !u.remove;
            if (objects == 0) {
                ++empty;
                MESSAGE(name << " side " << s << " starts empty; defend " << int(blk[0x1D]));
                CHECK(int(bb[0]) == m.w->map().height());
                CHECK(int(bb[2]) == m.w->map().width());
                CHECK(bb[1] == 0);
                CHECK(bb[3] == 0);
            } else {
                CHECK(bb[1] > bb[0]);
                CHECK(bb[3] > bb[2]);
            }
        }
    }
    MESSAGE(missions << " missions, " << sides << " AI sides, " << free_pct << " with FreePercentage");
    CHECK(free_pct == 3);
    CHECK(empty == 4); // A5V1 side 6, A7V1 side 3, H6V2 side 3, O9V1 side 4
}
