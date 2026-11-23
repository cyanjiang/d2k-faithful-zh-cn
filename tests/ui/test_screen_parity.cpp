// Front-end and in-game pages drawn offscreen and compared pixel for pixel
// with captures of the original (1.06 under cnc-ddraw, 640x400, 2026-10-16):
// HOUSESELECTION, GLOBE before A1, LOAD over GLOBE, the A1 briefing page
// (PRECAP) and INGAME. Compared at 5 bits per channel; left out: the mouse
// cursor, the globe's two looping arrows, the in-game view behind INGAME
// and the capture's bottom 6x6 corners (Windows 11 rounds the window
// corners). After the two fixes below every compared pixel matched.
//
// The captures are not in the repository. Each test keeps a hash of our
// render over exactly the pixels that matched (so any change to these
// pages shows up and needs a new comparison), plus the facts behind the
// fixes:
// - the in-game pages draw the .bmp version of their images
//   (FUN_00491330 skips the .tga while gGameState is 2);
// - a list box's frame and its scroll track's frame are drawn in the frame
//   colour's RGB even when it is Transparent (black, FUN_0042d140).
#include <doctest/doctest.h>

#include <filesystem>

#include "campaign/campaign_table.hpp"
#include "campaign/regions.hpp"
#include "formats/file_io.hpp"
#include "formats/pe_image.hpp"
#include "platform/game_data.hpp"
#include "ui/assets.hpp"
#include "ui/globe.hpp"
#include "ui/menu_hooks.hpp"
#include "ui/render.hpp"
#include "ui/screen.hpp"

using namespace d2k;
namespace fs = std::filesystem;

namespace {

// A menu as the runner opens it (MenuRunner::open_menu): parsed, the exe's
// per-menu hooks, image sizes and split text boxes.
struct Page {
    ui::Screen screen;
    std::vector<ui::ElementState> states;
    int find(const std::string& name) const {
        for (size_t i = 0; i < screen.elements.size(); ++i) {
            if (screen.elements[i].name == name) return int(i);
        }
        return -1;
    }
};

Page open_page(ui::Assets& assets, const std::string& name, const fs::path& script) {
    Page p;
    p.screen = ui::parse_screen(formats::parse_uil_lines(formats::read_file(script)));
    ui::apply_menu_hooks(name, p.screen, ui::HookContext{});
    ui::apply_image_sizes(p.screen, assets);
    ui::split_textboxes(p.screen, assets);
    p.states.assign(p.screen.elements.size(), {});
    return p;
}

Page open_page(ui::Assets& assets, const std::string& name) {
    auto path = assets.menu_script(name);
    REQUIRE(path.has_value());
    return open_page(assets, name, *path);
}

// FUN_0043c4b0: the background (or the menu's modified copy), then every
// element in the exe's class order.
void draw_page(const Page& p, ui::Assets& assets, ui::Surface& out, const ui::Surface* bg = nullptr) {
    ui::render_background(p.screen, assets, out, bg);
    ui::RenderState st;
    st.states = &p.states;
    for (size_t i : ui::draw_order(p.screen)) ui::render_element(p.screen, i, assets, out, st);
}

uint16_t at(const ui::Surface& s, int x, int y) { return s.pixels()[size_t(y) * size_t(s.width()) + size_t(x)]; }

// The capture's rounded window corners (not the game's pixels).
const ui::Rect kCorners[] = {{0, 394, 6, 6}, {634, 394, 6, 6}};

// FNV-1a over the pixels of `area` outside `skip` (and the corners): the
// pixels that matched the original.
uint32_t matched_hash(const ui::Surface& s, const ui::Rect& area, std::initializer_list<ui::Rect> skip,
                      uint16_t also_skip = 0xFFFF) {
    uint32_t h = 2166136261u;
    for (int y = area.y; y < area.y + area.h; ++y) {
        for (int x = area.x; x < area.x + area.w; ++x) {
            bool out = false;
            for (const auto& r : skip) out = out || r.contains(x, y);
            for (const auto& r : kCorners) out = out || r.contains(x, y);
            const uint16_t v = at(s, x, y);
            if (out || v == also_skip) continue;
            h = (h ^ uint32_t(v & 0xFF)) * 16777619u;
            h = (h ^ uint32_t(v >> 8)) * 16777619u;
        }
    }
    return h;
}

uint16_t rgb555(int r5, int g5, int b5) { return uint16_t((r5 << 10) | (g5 << 5) | b5); }

} // namespace

TEST_CASE("parity: HOUSESELECTION matches the original outside the cursor (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data")) return;
    platform::GameData gd(root);
    ui::Assets assets(gd);
    Page p = open_page(assets, "HOUSESELECTION");
    ui::Surface s(640, 400);
    draw_page(p, assets, s);
    CHECK(matched_hash(s, {0, 0, 640, 400}, {{320, 205, 14, 14}}) == 754050259u);
}

TEST_CASE("parity: GLOBE before A1 and LOAD over it match the original (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data")) return;
    platform::GameData gd(root);
    ui::Assets assets(gd);
    auto table = campaign::CampaignTable::parse(formats::read_file(gd.find("Data/UI_DATA/campaign.uib").value()));
    auto regions = campaign::load_regions(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    // MenuRunner::open_menu / update_globe: the globe edits a copy of the
    // background; once the reveal is over the menu redraws over it.
    Page p = open_page(assets, "GLOBE");
    ui::Surface bg(640, 400);
    const auto* map = assets.image("DuneMap2");
    REQUIRE(map != nullptr);
    bg.blit(*map, {0, 0, int(map->width), int(map->height)}, 0, 0, ui::BlitMode::Opaque);
    ui::Globe globe(assets, table, regions, 0, 1);
    ui::Surface s(640, 400);
    draw_page(p, assets, s, &bg);
    std::vector<ui::Globe::Arrow> arrows;
    for (int i = 0; i < 100000 && arrows.empty(); ++i) {
        auto f = globe.update(s, bg);
        if (f.finished) arrows = f.arrows;
    }
    REQUIRE(arrows.size() == 2);
    // The looping ATpoint arrows (72 x 70) are left out of the comparison.
    CHECK(arrows[0].x == 121);
    CHECK(arrows[0].y == 199);
    CHECK(arrows[1].x == 140);
    CHECK(arrows[1].y == 265);
    draw_page(p, assets, s, &bg);
    const ui::Rect a1{121, 199, 72, 70}, a2{140, 265, 72, 70};
    CHECK(matched_hash(s, {0, 0, 640, 400}, {{320, 200, 14, 14}, a1, a2}) == 3410175067u);

    // LOAD from the map (gGameState 1: the .tga images), three campaign
    // saves: "test3" saved between missions (font colour), "test2" and
    // "test" during a mission (white), no selection, inactive scroll bar.
    Page load = open_page(assets, "LOAD");
    const int list = load.find("loadListBox");
    REQUIRE(list >= 0);
    auto& ls = load.states[size_t(list)];
    ls.items = {"test3", "test2", "test"};
    ls.item_white = {false, true, true};
    ls.item_data = {0, 1, 2};
    draw_page(load, assets, s);
    CHECK(matched_hash(s, {0, 0, 640, 400}, {{223, 360, 14, 14}, a1, a2}) == 1593410683u);
    // The list's frame (192, 75, 256 x 225) and the track's frame (432, 91,
    // 16 x 193) are black in the original: Transparent = RGB (0, 0, 0),
    // written by FUN_0042d140 without a transparency test.
    for (auto [x, y] : {std::pair{300, 75}, std::pair{192, 295}, std::pair{300, 299}, std::pair{447, 150},
                        std::pair{432, 95}, std::pair{447, 80}}) {
        CAPTURE(x);
        CAPTURE(y);
        CHECK(at(s, x, y) == 0);
    }
    // Row colours: 't' of "test3" in JHighLightText, 't' of "test2" white
    // (the capture's pixels at 5 bits).
    CHECK(at(s, 194, 79) == ui::to_rgb555(*assets.colour("JHighLightText")));
    CHECK(at(s, 194, 93) == rgb555(31, 31, 31));
}

TEST_CASE("parity: the A1 briefing page (PRECAP) matches the original (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data")) return;
    platform::GameData gd(root);
    ui::Assets assets(gd);
    // PRECAP with gSideId 0, gMissionNumber 1 opens LA1MIS (FUN_004359a0).
    Page p = open_page(assets, "PRECAP", root / "Data" / "UI_DATA" / "LA1MIS.uil");
    ui::Surface s(640, 400);
    draw_page(p, assets, s);
    CHECK(matched_hash(s, {0, 0, 640, 400}, {{320, 200, 13, 14}}) == 855841578u);
}

TEST_CASE("parity: INGAME draws the .bmp images, as the original (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data")) return;
    platform::GameData gd(root);
    ui::Assets assets(gd);
    constexpr uint16_t kView = 0x1234; // stands for the game view
    // In a mission gGameState is 2: FUN_00491330 loads but1_up.bmp, box2.bmp.
    assets.set_main_loop(true);
    Page p = open_page(assets, "INGAME");
    ui::Surface s(640, 400);
    s.clear(kView);
    draw_page(p, assets, s);
    // box2.bmp is 205 x 250; only its keyed pixels show the game.
    CHECK(matched_hash(s, {200, 65, 205, 250}, {{320, 200, 14, 14}}, kView) == 2367103952u);
    // "Load Mission" (but1_up at 233, 110): the capture has the BMP's
    // palette colours (10, 4, 0) and (12, 5, 0), where the TGA has (11, 4, 0)
    // and (11, 5, 0).
    CHECK(at(s, 240, 115) == rgb555(10, 4, 0));
    CHECK(at(s, 250, 120) == rgb555(12, 5, 0));

    // The same page opened in the front end would draw the TGA.
    assets.set_main_loop(false);
    Page front = open_page(assets, "INGAME");
    ui::Surface f(640, 400);
    draw_page(front, assets, f);
    CHECK(at(f, 240, 115) == rgb555(11, 4, 0));
    CHECK(at(f, 250, 120) == rgb555(11, 5, 0));
}
