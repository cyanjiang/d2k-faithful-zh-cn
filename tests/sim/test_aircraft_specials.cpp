// Aircraft and the side specials (docs/re/14, 33, 35, 39, 32). Golden tests:
// they skip without data.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "formats/file_io.hpp"
#include "platform/game_data.hpp"
#include "sim/game_setup.hpp"

using namespace d2k;
namespace fs = std::filesystem;

namespace {

bool have_data() { return fs::exists(fs::path(D2K_GAMEDATA_DIR) / "MISSIONS/A7V1.MAP"); }

sim::FileReader reader(const platform::GameData& gd) {
    return [&gd](const std::string& p) -> std::optional<std::vector<uint8_t>> {
        auto f = gd.find(p);
        if (!f) return std::nullopt;
        return formats::read_file(*f);
    };
}

std::unique_ptr<sim::Game> campaign(const platform::GameData& gd, const std::string& name, sim::Rng& rng) {
    sim::GameSetup s;
    s.type = sim::GameSetup::Type::Campaign;
    s.mission = name;
    s.local = name[0] == 'A' ? 0 : name[0] == 'H' ? 1 : 2;
    return sim::build_game(s, rng, reader(gd));
}

} // namespace

TEST_CASE("golden: the 1.06 AI sides that can use the side specials (docs/re/35 section 10, 39 section 6) (skips without data)") {
    // For every campaign mission: the computer sides that own an
    // AT_HIGH_TECH_FACTORY (b11, the airstrike after its upgrade; tech >= 6)
    // or a palace (b19 Atreides, b20 Harkonnen, b21 Ordos) at the start, and
    // the specials already in their unit build list (u21 Fremen, u23
    // saboteur, u24 Death Hand).
    if (!have_data()) return;
    const fs::path root = D2K_GAMEDATA_DIR;
    platform::GameData gd(root);
    std::vector<std::string> found;
    for (const auto& f : fs::directory_iterator(root / "MISSIONS")) {
        if (f.path().extension() != ".MAP") continue;
        const std::string name = f.path().stem().string();
        if (!gd.find("MISSIONS/_" + name + ".MIS")) continue;
        sim::Rng rng(1);
        auto g = campaign(gd, name, rng);
        const auto& w = *g->world;
        const auto& t = g->templates;
        for (int s = 0; s < 8; ++s) {
            if (s == w.local_player) continue;
            std::vector<std::string> items;
            for (const auto& b : w.buildings()) {
                if (b.owner != s) continue;
                const int n = t.building_records[size_t(b.record)].name_index;
                if (n == 11 || n == 19 || n == 20 || n == 21) items.push_back("b" + std::to_string(n));
            }
            for (int r : w.player(s).unit_list) {
                const int k = t.unit_records[size_t(r)].kind;
                if (k == 9 || k == 4 || k == 12 || k == 10) items.push_back("u" + std::to_string(r));
            }
            if (items.empty()) continue;
            CHECK(w.ai_active(s));
            std::sort(items.begin(), items.end());
            std::string line = name + ":" + std::to_string(s) + ":t" + std::to_string(w.player(s).tech);
            for (const auto& i : items) line += "," + i;
            found.push_back(line);
        }
    }
    std::sort(found.begin(), found.end());
    const std::vector<std::string> expected = {
        "A8V1:2:t7,b21,u23", "A9V1:1:t8,b20,u24", "A9V2:1:t8,b20,u24", "H7V1:0:t6,b11",
        "H8V1:0:t7,b11",     "H8V1:2:t7,b21,u23", "H9V1:0:t7,b11,b19,u21", "H9V2:0:t7,b11,b19,u21",
        "O6V2:0:t5,b11",     "O7V1:0:t6,b11",     "O8V1:0:t7,b11,b19,u21", "O8V1:1:t7,b20,u24",
        "O9V1:0:t8,b11,b19,u21", "O9V1:1:t8,b20,u24", "O9V2:0:t8,b11,b19,u21", "O9V2:1:t8,b20,u24",
    };
    CHECK(found == expected);
}

TEST_CASE("golden: +0x2b: a shot (flag 2) resets it in the next update, before any count (0x49808f) (skips without data)") {
    if (!have_data()) return;
    const fs::path root = D2K_GAMEDATA_DIR;
    platform::GameData gd(root);
    sim::Rng rng(1);
    auto g = campaign(gd, "A7V1", rng);
    auto& w = *g->world;
    // A ground unit of the player whose 8-tick slot is the next tick.
    const uint32_t next = w.tick_count() + 1;
    int id = -1;
    for (const auto& u : w.units()) {
        if (u.owner == 0 && !(u.flags & 0x40) && u.state != 0x11 && (uint32_t(u.slot) & 7) == (next & 7)) {
            id = u.index;
            break;
        }
    }
    REQUIRE(id >= 0);
    auto& u = w.units()[size_t(w.find_unit(id))];
    u.flags |= 2;
    u.counter_2b = 50;
    w.tick();
    const auto& v = w.units()[size_t(w.find_unit(id))];
    CHECK(v.counter_2b == 0); // reset, not reset-and-counted
    CHECK(!(v.flags & 2));
}

TEST_CASE("golden: CreateUnit refuses a removed side (+0x24250 == 0, 0x45591b) (skips without data)") {
    if (!have_data()) return;
    const fs::path root = D2K_GAMEDATA_DIR;
    platform::GameData gd(root);
    sim::Rng rng(1);
    auto g = campaign(gd, "A7V1", rng);
    auto& w = *g->world;
    const auto* ids = g->templates.block(0x6d60d8);
    REQUIRE(ids);
    const int orni = ids->bytes[0x12];
    REQUIRE(g->templates.unit_records[size_t(orni)].kind == 9);
    CHECK(w.create_unit(1, orni, 5, 5, 5, 5) >= 0);
    w.remove_side(1);
    CHECK(!w.player_active(1));
    CHECK(w.create_unit(1, orni, 5, 5, 5, 5) < 0);
}
