// Economy and map features: the Starport quirks (docs/re/34), terrain
// damage (docs/re/26), crate cells (docs/re/54) and the shroud (docs/re/16).
// Golden tests: they skip without data.
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

bool have_data() { return fs::exists(fs::path(D2K_GAMEDATA_DIR) / "Data/maps/2PLAY1.MAP"); }

sim::FileReader reader(const platform::GameData& gd) {
    return [&gd](const std::string& p) -> std::optional<std::vector<uint8_t>> {
        auto f = gd.find(p);
        if (!f) return std::nullopt;
        return formats::read_file(*f);
    };
}

// A skirmish with no computer players, no units, no worms and no crates:
// nothing moves money but the test.
std::unique_ptr<sim::Game> quiet_skirmish(const platform::GameData& gd, const std::string& map, sim::Rng& rng,
                                          uint32_t reveal_counter = 0) {
    sim::GameSetup s;
    s.type = sim::GameSetup::Type::Skirmish;
    s.mission = map;
    s.tech = 7;
    s.credits = 5000;
    s.reveal_counter = reveal_counter;
    return sim::build_game(s, rng, reader(gd));
}

int64_t money(const sim::Player& p) {
    return int64_t(p.cash) + p.spice + p.pending_cash + p.pending_spice;
}

} // namespace

TEST_CASE("golden: losing the last Starport with a full cart refunds it twice (docs/re/34 section 8 quirk 3) (skips without data)") {
    // FUN_0046d9a0 refunds the money in transit (+0x24540, which holds the
    // cart), then the tab update (0x4494c2) posts order 0x2c, which refunds
    // price x cart again (FUN_004572f0) and puts the stock back.
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    sim::Rng rng(1);
    auto g = quiet_skirmish(gd, "2PLAY1", rng);
    auto& w = *g->world;
    const auto& map = *g->game_map;
    // A free buildable 3 x 3 spot for a Starport (record 46).
    int sx = -1, sy = -1;
    for (int y = 1; y + 3 < map.height() && sx < 0; ++y) {
        for (int x = 1; x + 3 < map.width() && sx < 0; ++x) {
            bool ok = true;
            for (int dy = 0; dy < 3 && ok; ++dy) {
                for (int dx = 0; dx < 3 && ok; ++dx) {
                    const uint32_t f = map.at(x + dx, y + dy).flags;
                    ok = (f & sim::cell::kBuildable) && !(f & 0x3f8) && !(f & 0x10);
                }
            }
            if (ok) {
                sx = x;
                sy = y;
            }
        }
    }
    REQUIRE(sx >= 0);
    const int port = w.create_building(0, 46, sx, sy, true);
    REQUIRE(port >= 0);
    auto& pl = w.player(0);
    REQUIRE(pl.primary[13] >= 0);
    const int rec = pl.starport_list[0];
    REQUIRE(rec >= 0);
    pl.stock[size_t(rec)] = 2;
    pl.price[size_t(rec)] = 300;
    const int64_t m0 = money(pl);
    w.starport_add(0, rec);
    w.starport_add(0, rec);
    REQUIRE(pl.cart[size_t(rec)] == 2);
    CHECK(money(pl) == m0 - 600);
    CHECK(pl.starport_money == 600);
    // The Starport is destroyed; its removal runs FUN_0046d9a0.
    w.destroy_building(port);
    for (int i = 0; i < 600 && pl.primary[13] >= 0; ++i) w.tick();
    REQUIRE(pl.primary[13] < 0);
    CHECK(pl.starport_money == 0);
    CHECK(pl.cart[size_t(rec)] == 2); // FUN_0046d9a0 leaves the cart
    CHECK(money(pl) == m0);           // first refund
    // The local player's sidebar then posts 0x2c (no Starport, page 2).
    w.post(sim::make_order(sim::order::kStarportCancel, 0));
    for (int i = 0; i < 20 && pl.cart[size_t(rec)] != 0; ++i) w.tick();
    CHECK(pl.cart[size_t(rec)] == 0);
    CHECK(pl.stock[size_t(rec)] == 2);
    CHECK(money(pl) == m0 + 600); // the second refund: 600 credits gained
}

TEST_CASE("golden: a purchase with an empty cart leaves delivery_pending set: the AI lock-out (docs/re/34 section 8 quirk 6) (skips without data)") {
    // BuyUnitsFromStarport sets +0x262a5 itself when it posts (0x421e32);
    // FUN_00457140 with nothing in the cart deactivates the entry and returns
    // before its own write (0x4572dc). Only a frigate's last drop (0x49860a)
    // and the loss of the last Starport (0x46dc82) clear it again.
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    sim::Rng rng(1);
    auto g = quiet_skirmish(gd, "2PLAY1", rng);
    auto& w = *g->world;
    auto& pl = w.player(0);
    pl.primary[13] = 0; // as if a Starport were primary; the purchase only tests != -1
    pl.delivery_pending = true;
    w.starport_purchase(0);
    CHECK(pl.delivery_pending);
    for (const auto& d : w.deliveries(0)) CHECK_FALSE(d.active);
}

TEST_CASE("golden: no 1.06 weapon saturates the crater counter past a concrete break (docs/re/26 section 2) (skips without data)") {
    // inc = (ARMOUR[warhead][0] x damage / 100) / 40; the 0xFF quirk needs
    // inc >= 0xEF from a counter <= 0x10.
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    const auto t = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    const auto a = formats::ArmourTable::parse(formats::read_file(gd.find("Data/bin/ARMOUR.BIN").value()));
    uint32_t max_inc = 0;
    std::string line;
    for (size_t b = 0; b < t.bullets.size(); ++b) {
        const auto& br = t.bullets[b];
        if (br.warhead >= a.warheads.size()) continue;
        const uint32_t inc = (uint32_t(a.warheads[br.warhead].percent[0]) * uint32_t(br.damage) / 100u) / 40u;
        const std::string name = b < t.bullet_name.size() ? t.bullet_name[b] : std::to_string(b);
        line += name + "=" + std::to_string(inc) + " ";
        if (name == "WORM") continue; // the worm's weapon is never fired (docs/re/17 section 6.4)
        max_inc = std::max(max_inc, inc);
    }
    MESSAGE("crater increments: " << line);
    CHECK(max_inc == 225); // CRATE (10000, W_FIRE 90 %): the largest
    CHECK(max_inc < 0xef);
}

TEST_CASE("golden: every skirmish map has free cells for a crate at the start (docs/re/54 section 1.2) (skips without data)") {
    // AddRandomCrate loops until it finds (flags & 0x73F8) == 0x6000.
    if (!have_data()) return;
    const fs::path root = D2K_GAMEDATA_DIR;
    platform::GameData gd(root);
    int fewest = 1 << 30;
    std::string worst;
    int maps = 0;
    for (const auto& f : fs::directory_iterator(root / "Data/maps")) {
        if (f.path().extension() != ".MAP") continue;
        const std::string name = f.path().stem().string();
        sim::Rng rng(1);
        auto g = quiet_skirmish(gd, name, rng);
        const auto& map = *g->game_map;
        int n = 0;
        for (int y = 0; y < map.height(); ++y) {
            for (int x = 0; x < map.width(); ++x) n += (map.at(x, y).flags & 0x73f8) == 0x6000;
        }
        ++maps;
        if (n < fewest) {
            fewest = n;
            worst = name;
        }
    }
    MESSAGE(maps << " maps; fewest crate cells: " << fewest << " on " << worst);
    CHECK(maps > 0);
    CHECK(fewest > 100);
}

TEST_CASE("golden: shroud helpers: the reveal counter carries over, open map, the radar rebuild after a load (docs/re/16) (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    sim::Rng rng(1);
    auto g = quiet_skirmish(gd, "2PLAY1", rng, 5);
    auto& w = *g->world;
    // The start reveal (FUN_0049f4d0) runs 8 unit sweeps from the run's counter.
    CHECK(w.reveal_counter() == 13);
    const auto& map = *g->game_map;
    int edge = 0, open = 0;
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            const uint8_t s = map.at(x, y).state;
            edge += (s & 0xf) != 1 && s != 0;
            open += s == 0;
        }
    }
    REQUIRE(open > 0);
    REQUIRE(edge > 0); // edge cells exist and show their radar pixel now
    w.rebuild_shown_radar();
    int known = 0, wrong = 0;
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            known += map.at(x, y).radar_known;
            wrong += map.at(x, y).radar_known != (map.at(x, y).state == 0);
        }
    }
    CHECK(known == open);
    CHECK(wrong == 0);
    w.open_map();
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) REQUIRE(map.at(x, y).state == 0);
    }
}
