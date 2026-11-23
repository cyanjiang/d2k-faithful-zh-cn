// Formats and drawing settled against the exe (fonts, text lookup,
// territory regions, text box splitting). docs/formats/fonts.md, ui.md,
// campaign.md.
#include <doctest/doctest.h>

#include <filesystem>

#include "campaign/regions.hpp"
#include "formats/file_io.hpp"
#include "formats/font.hpp"
#include "formats/string_table.hpp"
#include "platform/game_data.hpp"
#include "ui/assets.hpp"
#include "ui/render.hpp"
#include "ui/screen.hpp"

using namespace d2k;
namespace fs = std::filesystem;

TEST_CASE("Region::contains matches GDI PtInRegion: integer point, left/top in, right/bottom out") {
    // Expected values read from gdi32 (CreatePolygonRgn ALTERNATE + PtInRegion).
    campaign::Region sq;
    sq.polygon = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    CHECK(sq.contains(0, 0));
    CHECK(sq.contains(9, 9));
    CHECK(sq.contains(5, 0));
    CHECK(sq.contains(0, 9));
    CHECK_FALSE(sq.contains(10, 5));
    CHECK_FALSE(sq.contains(5, 10));
    campaign::Region tri;
    tri.polygon = {{5, 0}, {10, 10}, {0, 10}};
    CHECK_FALSE(tri.contains(5, 0));
    CHECK(tri.contains(5, 1));
    CHECK_FALSE(tri.contains(4, 1));
    CHECK_FALSE(tri.contains(6, 1));
    CHECK(tri.contains(9, 9));
    CHECK_FALSE(tri.contains(0, 9));
}

TEST_CASE("Font::glyph goes through the character map; 0x20 means a space") {
    formats::Font f;
    f.header[1] = 4;
    f.first_char = 'A';
    f.glyphs.push_back(formats::Glyph{1, 1, {1}});
    f.glyphs.push_back(formats::Glyph{2, 1, {1, 1}});
    CHECK(f.glyph('A') == &f.glyphs[0]);
    std::array<uint8_t, 256> map{};
    for (size_t i = 0; i < map.size(); ++i) map[i] = uint8_t(i);
    map[0xC0] = 'B'; // an accented letter drawn with another glyph
    map['A'] = 0x20; // mapped to a space
    f.set_char_map(map);
    CHECK(f.glyph(0xC0) == &f.glyphs[1]);
    CHECK(f.glyph('A') == nullptr);
    CHECK(ui::text_width(f, "A") == 4);
}

TEST_CASE("golden: font headers, FONT.BIN map and the FONTCOL handle (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data")) return;
    auto font = formats::parse_font(formats::read_file(root / "Data" / "FONT.FNT"));
    CHECK(font.header[0] == 1); // the loaded flag
    CHECK(font.space_width() == 5);
    CHECK(font.first_char == 0x21);
    CHECK(font.spacing() == 1);
    CHECK(font.glyphs.at(0).height == 14); // the row height / centring height
    auto fontcol = formats::parse_font(formats::read_file(root / "Data" / "FONTCOL.FNT"));
    CHECK(fontcol.spacing() == 0);
    // Every FONTCOL glyph index stays inside the 16-entry colour handle.
    for (const auto& g : fontcol.glyphs) {
        for (uint8_t v : g.pixels) CHECK(v < 15);
    }
    const auto pal = formats::parse_font_palette(formats::read_file(root / "Data" / "FONTCOL.FPL"));
    CHECK(pal[1] == 0);      // black edge
    CHECK(pal[2] == 0x7c00); // red

    platform::GameData gd(root);
    ui::Assets assets(gd);
    // bin\FONT.BIN maps 0x92 (one credits name) and 0xAC to a space.
    CHECK(assets.menu_font().glyph(0x92) == nullptr);
    CHECK(assets.menu_font().glyph(0xAC) == nullptr);
    CHECK(assets.menu_font().glyph('A') != nullptr);
    // A key missing from text.uib gives entry 0's text ("NoText" -> "").
    CHECK(assets.text().entries().at(0).first == "NoText");
    CHECK(assets.lookup_text("MultiPlayer").empty());
    CHECK(assets.lookup_text("B_raider") == assets.lookup_text("B_Raider"));
}

TEST_CASE("golden: MAINMENU version box: unknown fill 'grey' is colour index 0, so no box; text 5 px from the right (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data")) return;
    platform::GameData gd(root);
    ui::Assets assets(gd);
    auto path = assets.menu_script("MAINMENU");
    REQUIRE(path.has_value());
    auto screen = ui::parse_screen(formats::parse_uil_lines(formats::read_file(*path)));
    size_t idx = screen.elements.size();
    for (size_t i = 0; i < screen.elements.size(); ++i) {
        if (screen.elements[i].name == "versionnumber") idx = i;
    }
    REQUIRE(idx < screen.elements.size());
    ui::Surface s(640, 400);
    s.clear(0x1234);
    ui::render_element(screen, idx, assets, s, ui::RenderState{});
    auto px = [&](int x, int y) { return s.pixels()[size_t(y * 640 + x)]; };
    CHECK(px(535, 385) == 0x1234); // no fill, no border (FUN_0048bb10, index 0)
    CHECK(px(634, 399) == 0x1234);
    // The original (reference screenshot): "V 1.06" in black, 'V' from
    // x 597 at y 387, the '6' ending at x 628.
    CHECK(px(597, 387) == 0);
    CHECK(px(628, 388) == 0);
    for (int y = 385; y < 400; ++y) CHECK(px(629, y) == 0x1234);
}

TEST_CASE("golden: PLAY_ATREIDES Yes / No: Transparent colours fill the button rect black, as the original (skips without data)") {
    // FUN_00485610 without an image -> FUN_0042d5d0 -> FUN_0042d660: the
    // fill writes colour entry 0 = (0, 0, 0) unconditionally. The original
    // capture (orig_8.png) shows AACCT's faint stars blacked out exactly
    // inside 239..283 x 340..359 and kept one pixel outside.
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data")) return;
    platform::GameData gd(root);
    ui::Assets assets(gd);
    auto path = assets.menu_script("PLAY_ATREIDES");
    REQUIRE(path.has_value());
    auto screen = ui::parse_screen(formats::parse_uil_lines(formats::read_file(*path)));
    ui::Surface bg(640, 400), s(640, 400);
    ui::render_background(screen, assets, bg, nullptr);
    ui::render_screen(screen, assets, s, ui::RenderState{});
    auto at = [](const ui::Surface& x, int px, int py) { return x.pixels()[size_t(py * 640 + px)]; };
    // Background stars inside the rects: black in the original.
    REQUIRE(at(bg, 259, 341) != 0);
    CHECK(at(s, 259, 341) == 0);
    REQUIRE(at(bg, 385, 342) != 0);
    CHECK(at(s, 385, 342) == 0);
    // Stars just outside: unchanged in the original.
    for (auto [px, py] : {std::pair{266, 339}, std::pair{241, 360}, std::pair{238, 347}}) {
        REQUIRE(at(bg, px, py) != 0);
        CHECK(at(s, px, py) == at(bg, px, py));
    }
    // "Yes" in the font-up colour: the 'Y' starts at (252, 345) in the original.
    CHECK(at(s, 252, 345) == ui::to_rgb555(*assets.colour("Grey")));
}

TEST_CASE("golden: every TEXTBOX text splits into lines narrower than its box (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "Data")) return;
    platform::GameData gd(root);
    ui::Assets assets(gd);
    size_t boxes = 0, split = 0;
    for (const auto& de : fs::directory_iterator(root / "Data" / "UI_DATA")) {
        auto ext = de.path().extension().string();
        if (ext != ".uil" && ext != ".UIL") continue;
        auto screen = ui::parse_screen(formats::parse_uil_lines(formats::read_file(de.path())));
        for (const auto& e : screen.elements) {
            auto rect = ui::element_rect(e);
            if (e.type != ui::ElementType::TextBox || !rect || e.body.size() < 7) continue;
            ++boxes;
            const std::string text = assets.lookup_text(e.body[6]);
            const auto lines = ui::split_textbox_text(assets.menu_font(), text, rect->w);
            if (lines.size() > 1) {
                ++split;
                for (const auto& l : lines) {
                    CAPTURE(de.path().filename().string());
                    CAPTURE(l);
                    CHECK(ui::text_width(assets.menu_font(), l) < rect->w);
                }
            }
        }
    }
    MESSAGE(boxes << " text boxes, " << split << " split");
    CHECK(split > 0);
}
