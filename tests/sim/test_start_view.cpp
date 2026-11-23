// The start camera (HomeView FUN_0046cf10 at 0x448a45, docs/re/25 A.8).
// Golden: skips without data.
#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "formats/file_io.hpp"
#include "platform/game_data.hpp"
#include "sim/game_setup.hpp"

using namespace d2k;
namespace fs = std::filesystem;

namespace {

bool have_data() { return fs::exists(fs::path(D2K_GAMEDATA_DIR) / "MISSIONS/A1V1.MAP"); }

std::unique_ptr<sim::Game> campaign(const platform::GameData& gd, const std::string& name, sim::Rng& rng) {
    sim::GameSetup s;
    s.type = sim::GameSetup::Type::Campaign;
    s.mission = name;
    s.local = name[0] == 'A' ? 0 : name[0] == 'H' ? 1 : 2;
    return sim::build_game(s, rng, [&gd](const std::string& p) -> std::optional<std::vector<uint8_t>> {
        auto f = gd.find(p);
        if (!f) return std::nullopt;
        return formats::read_file(*f);
    });
}

struct View {
    int x, y;
};

View start_view(const platform::GameData& gd, const std::string& name) {
    sim::Rng rng(1);
    auto g = campaign(gd, name, rng);
    View v{-1, -1};
    REQUIRE(g->world->home_view(g->world->local_player, v.x, v.y));
    return v;
}

} // namespace

TEST_CASE("golden: the A1V1 start camera is the original's (48, 138) (skips without data)") {
    // Measured on a capture of the original (cnc-ddraw 640x400, ~8 s after
    // Start, mouse over the yard): the yard's 3x3 cell frame starts at screen
    // (240, 170); the playfield is at (0, 20), the yard (cell 9,9) at map
    // pixel (288, 288), so the camera is (288 - 240, 288 + 20 - 170) =
    // (48, 138). HomeView: building x 288 - 480 / 2 = 48; building y
    // (cy * 32 + height 96 = 384) - offset_y 113 / 2 - 380 / 2 = 138.
    if (!have_data()) return;
    platform::GameData gd{fs::path(D2K_GAMEDATA_DIR)};
    const View v = start_view(gd, "A1V1");
    CHECK(v.x == 48);
    CHECK(v.y == 138);
}

TEST_CASE("golden: HomeView start cameras of other first missions (skips without data)") {
    // The same formula (no capture of these): H1V1 yard cell 5,12 (x 160 -
    // 240 < 1 -> 0; 480 - 117 / 2 - 190 = 232), O1V1 cell 7,11 (0; 448 - 56 -
    // 190 = 202), A2V1 cell 19,8 (608 - 240 = 368; 352 - 56 - 190 = 106).
    if (!have_data()) return;
    platform::GameData gd{fs::path(D2K_GAMEDATA_DIR)};
    const View h = start_view(gd, "H1V1");
    CHECK(h.x == 0);
    CHECK(h.y == 232);
    const View o = start_view(gd, "O1V1");
    CHECK(o.x == 0);
    CHECK(o.y == 202);
    const View a = start_view(gd, "A2V1");
    CHECK(a.x == 368);
    CHECK(a.y == 106);
}
