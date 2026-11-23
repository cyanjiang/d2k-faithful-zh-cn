#include <doctest/doctest.h>

#include <filesystem>

#include "campaign/campaign_table.hpp"
#include "campaign/regions.hpp"
#include "formats/bmp.hpp"
#include "formats/pe_image.hpp"
#include "formats/file_io.hpp"
#include "formats/font.hpp"
#include "formats/string_table.hpp"
#include "formats/tga.hpp"
#include "platform/game_data.hpp"
#include "ui/animation.hpp"
#include "ui/assets.hpp"
#include "ui/credits.hpp"
#include "ui/menu_hooks.hpp"
#include "ui/render.hpp"
#include "ui/screen.hpp"
#include "ui/globe.hpp"
#include "ui/transition.hpp"

using namespace d2k;
namespace fs = std::filesystem;

TEST_CASE("parse_screen follows the original dispatcher rules") {
    std::vector<std::string> lines = {
        "BACKGROUND title", "END_BACKGROUND",
        // Headerless leftover body: ignored line by line.
        "20, 100", "100, 60", "NoText", "END_BUTTON",
        "TEXTBOX_C caption", "1, 2", "3, 4", "Black", "Black", "Black", "Grey", "Key", "END_TEXTBOX",
        "TEXTBOX_L_UL left", "END_TEXTBOX",
        "BUTTON exit 0", "250, 295", "140, 20", "Exit   // button text", "END_BUTTON",
        "button lowercase is not a keyword",
    };
    auto s = ui::parse_screen(lines);
    REQUIRE(s.elements.size() == 4);
    CHECK(s.elements[0].type == ui::ElementType::Background);
    CHECK(s.elements[0].name == "title");
    CHECK(s.elements[1].type == ui::ElementType::TextBox);
    CHECK(s.elements[1].align == ui::TextAlign::Centre); // TEXTBOX_C falls to TEXTBOX
    CHECK(s.elements[2].align == ui::TextAlign::Left);
    CHECK(s.elements[2].underline);
    CHECK(s.elements[3].name == "exit");
    CHECK(s.elements[3].body[2] == "Exit"); // comment stripped
    auto r = ui::parse_pair(s.elements[3].body[0]);
    CHECK(r.x == 250);
    CHECK(r.y == 295);
}

TEST_CASE("MAINMENU hook removes the debug button; without a network it adds Practice") {
    ui::Screen s;
    ui::Element mp;
    mp.type = ui::ElementType::Button;
    mp.name = "startMultiplayer";
    mp.body = {"250, 220", "140, 20"};
    ui::Element dbg = mp;
    dbg.name = "startNewGame";
    s.elements = {mp, dbg};
    ui::Screen with_net = s;
    ui::HookContext none;
    none.network_available = false;
    ui::apply_menu_hooks("MAINMENU", s, none);
    REQUIRE(s.elements.size() == 1);
    CHECK(s.elements[0].name == "practice");
    CHECK(s.elements[0].body[0] == "250, 220");
    ui::apply_menu_hooks("MAINMENU", with_net, {});
    REQUIRE(with_net.elements.size() == 1);
    CHECK(with_net.elements[0].name == "startMultiplayer");
}

TEST_CASE("MULTIPLAYERTYPE hook removes Internet and Modem/Serial; IPXLOBBY gets the address box") {
    ui::Screen s;
    for (const char* n : {"internet", "network", "serialModem", "skirmish", "cancel"}) {
        ui::Element e;
        e.type = ui::ElementType::Button;
        e.name = n;
        s.elements.push_back(e);
    }
    ui::apply_menu_hooks("MULTIPLAYERTYPE", s, {});
    REQUIRE(s.elements.size() == 3);
    CHECK(s.elements[0].name == "network");
    ui::Screen lobby;
    ui::apply_menu_hooks("IPXLOBBY", lobby, {});
    REQUIRE(lobby.elements.size() == 2);
    CHECK(lobby.elements[1].type == ui::ElementType::EditBox);
    CHECK(lobby.elements[1].body.back() == "ProbeAddress");
}

TEST_CASE("golden: fonts, colours, UI graphics and every menu script (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root)) {
        MESSAGE("skipped: no game data");
        return;
    }
    auto fontcol = formats::parse_font(formats::read_file(root / "Data" / "FONTCOL.FNT"));
    CHECK(fontcol.height == 17);
    CHECK(fontcol.glyphs.size() == 157);
    CHECK(fontcol.first_char == '!');
    auto font = formats::parse_font(formats::read_file(root / "Data" / "FONT.FNT"));
    CHECK(font.height == 14);
    CHECK(font.glyphs.size() == 159);

    auto colours = formats::ColourTable::parse(formats::read_file(root / "Data" / "UI_DATA" / "colours.uib"));
    CHECK(colours.size() == 51);
    CHECK(colours.get("Blue") == formats::Rgb{0, 0, 255});

    auto title = formats::parse_tga16(formats::read_file(root / "Data" / "UI_GFX" / "Title.tga"));
    CHECK(title.width == 640);
    CHECK(title.height == 400);

    platform::GameData gd(root);
    ui::Assets assets(gd);
    size_t screens = 0;
    for (const auto& e : fs::directory_iterator(root / "Data" / "UI_DATA")) {
        auto ext = e.path().extension().string();
        if (ext != ".uil" && ext != ".UIL") continue;
        CAPTURE(e.path().filename().string());
        CHECK_NOTHROW(ui::parse_screen(formats::parse_uil_lines(formats::read_file(e.path()))));
        ++screens;
    }
    CHECK(screens == 302);
    CHECK(assets.menu_script("MAINMENU").has_value());
    CHECK(assets.image("but1_up") != nullptr);
}

TEST_CASE("AnimationPlayer follows CAnimation timing and modes") {
    ui::AnimationPlayer p;
    p.frames = 3;
    p.period_ms = 100;
    p.update(500);
    CHECK(p.frame == 0); // starts stopped

    p.set_mode(ui::AnimMode::ForwardOnce, 500);
    p.update(550);
    CHECK(p.frame == 0); // period not yet elapsed
    p.update(600);
    CHECK(p.frame == 1);
    p.update(700);
    CHECK(p.frame == 2);
    p.update(800);
    CHECK(p.mode == ui::AnimMode::Stopped);
    CHECK(p.frame == 2); // hold the last frame

    p.set_mode(ui::AnimMode::ForwardOnce, 800);
    CHECK(p.frame == 0); // a mode change rewinds
    p.set_mode(ui::AnimMode::LoopBackward, 800);
    CHECK(p.frame == 2);
    p.update(900);
    CHECK(p.frame == 1);
    p.update(1000);
    p.update(1100);
    CHECK(p.frame == 2); // wraps

    CHECK(ui::anim_mode_from_name("AnimLoopForwardCycle") == ui::AnimMode::LoopForward);
    CHECK(ui::anim_mode_from_name("AnimStopped") == ui::AnimMode::Stopped);
}

TEST_CASE("Transition pixel steps follow the exe arithmetic") {
    ui::Surface old_img(4, 2), new_img(4, 2), out(4, 2);
    std::fill(old_img.pixels().begin(), old_img.pixels().end(), uint16_t(0x7fff));
    std::fill(new_img.pixels().begin(), new_img.pixels().end(), uint16_t(0x0000));
    ui::tween_blend(old_img, new_img, out, {0, 0, 4, 2}, 16);
    CHECK(out.pixels()[0] == uint16_t((15 << 10) | (15 << 5) | 15)); // (31*16)>>5

    // Fade-out step 0: checkerboard, first pixel of row 0 untouched.
    ui::Surface s(4, 2);
    std::fill(s.pixels().begin(), s.pixels().end(), uint16_t(0x7fff));
    ui::fade_out_step(s, {0, 0, 4, 2}, 0);
    CHECK(s.pixels()[0] == 0x7fff);
    CHECK(s.pixels()[1] == uint16_t((15 << 10) | (15 << 5) | 15));
    CHECK(s.pixels()[4] == uint16_t((15 << 10) | (15 << 5) | 15)); // parity flips per row
    CHECK(s.pixels()[5] == 0x7fff);

    // Fade-in step 9 shifts by 0 on its half.
    ui::Surface black(4, 2), white(4, 2);
    std::fill(white.pixels().begin(), white.pixels().end(), uint16_t(0x7fff));
    ui::fade_in_step(white, black, {0, 0, 4, 2}, 9);
    CHECK(black.pixels()[0] == 0x7fff);
    CHECK(black.pixels()[1] == 0);
}

TEST_CASE("Transition: TWEEN takes 8 blended frames, FADE 20 timed steps") {
    ui::Surface screen(4, 2), next(4, 2);
    std::fill(next.pixels().begin(), next.pixels().end(), uint16_t(0x7fff));

    ui::Transition t;
    t.select(ui::Transition::Tween, {0, 0, 4, 2}, true);
    t.start(screen, next);
    int frames = 0;
    while (t.step(screen, 0)) ++frames;
    CHECK(frames == 8);
    CHECK(screen.pixels()[0] == 0x7fff);

    ui::Surface screen2(4, 2);
    t.select(ui::Transition::FadeIn, {0, 0, 4, 2}, true);
    t.start(screen2, next);
    int steps = 0;
    uint64_t now = 1000;
    while (t.step(screen2, now)) {
        CHECK(t.step(screen2, now + 5)); // gated at 12 ms
        now += 12;
        ++steps;
    }
    CHECK(steps == 19); // the 20th step finishes and returns false
    CHECK(screen2.pixels()[0] == 0x7fff);

    ui::Transition skip;
    skip.select(ui::Transition::Skip, {0, 0, 4, 2}, true);
    skip.start(screen2, next);
    CHECK_FALSE(skip.owns_screen());
    CHECK_FALSE(skip.step(screen2, 0));
}

TEST_CASE("golden: every UI BMP parses; BMP-only images load as 8-bit (skips without data)") {
    fs::path dir = fs::path(D2K_GAMEDATA_DIR) / "Data" / "UI_GFX";
    if (!fs::exists(dir)) return;
    int n = 0;
    for (const auto& f : fs::directory_iterator(dir)) {
        auto ext = f.path().extension().string();
        if (ext != ".BMP" && ext != ".bmp") continue;
        auto img = formats::parse_bmp8(formats::read_file(f.path()));
        CHECK(img.indices.size() == size_t(img.width) * img.height);
        ++n;
    }
    CHECK(n == 275);

    platform::GameData gd(D2K_GAMEDATA_DIR);
    ui::Assets assets(gd);
    const auto* box = assets.image("box4"); // only BOX4.BMP exists
    REQUIRE(box != nullptr);
    CHECK(box->paletted);
    CHECK(box->width == 240);
    CHECK(assets.image("None") == nullptr);
    CHECK_FALSE(assets.image("title")->paletted); // TITLE.TGA wins over the BMP
}

TEST_CASE("golden: long TEXTBOX text wraps like AddTextBox (skips without data)") {
    if (!fs::exists(fs::path(D2K_GAMEDATA_DIR) / "Data")) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    ui::Assets assets(gd);
    const auto& font = assets.menu_font();
    std::string text = "There are no saved games to choose from.";
    auto lines = ui::split_textbox_text(font, text, 200);
    REQUIRE(lines.size() >= 2);
    std::string joined;
    for (const auto& l : lines) {
        CHECK(ui::text_width(font, l) < 200);
        CHECK(l.front() != ' ');
        CHECK(l.back() != ' ');
        joined += (joined.empty() ? "" : " ") + l;
    }
    CHECK(joined == text);
    // A forced break (0xAC) ends a line even when more would fit.
    auto forced = ui::split_textbox_text(font, std::string("Hello\xAC") + "world and some more text here", 150);
    CHECK(forced.at(0) == "Hello");
    // Text that fits stays a single unchanged line.
    CHECK(ui::split_textbox_text(font, "Ok", 200) == std::vector<std::string>{"Ok"});
}

TEST_CASE("golden: campaign map reveal follows CreateGlobe / FUN_0042c9b0 (skips without data)") {
    if (!fs::exists(fs::path(D2K_GAMEDATA_DIR) / "Data")) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    ui::Assets assets(gd);
    auto table = campaign::CampaignTable::parse(formats::read_file(gd.find("Data/UI_DATA/campaign.uib").value()));
    auto regions = campaign::load_regions(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));

    const auto& a1 = table.at(0, 1);
    CHECK(a1.mission[0] == "a1v1");
    CHECK(a1.territory[0] == 7);
    CHECK(a1.reveal_speech[0] == "C_A1a");
    CHECK(assets.sample_file("C_A1a") == std::optional<std::string>("ai_map1a"));
    CHECK(regions[0].polygon.size() == 5);

    // Atreides mission 1: the 9 starting territories are revealed house by
    // house (one speech line each), 17 updates per territory after a
    // 9-update fade of the whole map.
    ui::Surface screen(640, 400), background(640, 400);
    ui::Globe globe(assets, table, regions, 0, 1);
    std::vector<int> speech_frames;
    std::vector<ui::Globe::Arrow> arrows;
    int frame = 0;
    for (; frame < 1000 && arrows.empty(); ++frame) {
        auto f = globe.update(screen, background);
        if (f.speech) speech_frames.push_back(frame);
        arrows = f.arrows;
    }
    CHECK(speech_frames == std::vector<int>{9, 60, 111});
    CHECK(frame == 163);
    REQUIRE(arrows.size() == 2);
    CHECK(arrows[0].x == 121);
    CHECK(arrows[0].y == 199);
    CHECK(arrows[0].gfx == "ATpoint");
    // Clicking inside territory 7 picks the first mission.
    CHECK(globe.click(150, 230) == std::optional<std::string>("a1v1"));
    CHECK_FALSE(globe.click(600, 100).has_value());
}

TEST_CASE("Surface::draw_text_alpha blends each 5-bit field as FUN_00432060") {
    // One 1x1 glyph 'A' with a nonzero pixel.
    formats::Font font;
    font.first_char = 'A';
    font.glyphs.push_back(formats::Glyph{1, 1, {1}});
    ui::Surface s(4, 1);
    s.pixels()[0] = 0x7fff;                             // white
    s.draw_text_alpha(font, 0, 0, "A", 0x0000, 8);      // black at 8/32
    // Each field: (31 * 24 + 0 * 8) >> 5 = 23.
    CHECK(s.pixels()[0] == uint16_t(23 << 10 | 23 << 5 | 23));
    s.pixels()[1] = 0x0000;
    s.draw_text_alpha(font, 1, 0, "A", 0x7c00, 32);     // full alpha: the colour
    CHECK(s.pixels()[1] == 0x7c00);
}

TEST_CASE("golden: CREDITS rows, scroll, fade and loop as FUN_0043d2a0 / FUN_0043d520 (skips without data)") {
    fs::path ui = fs::path(D2K_GAMEDATA_DIR) / "Data" / "UI_DATA";
    if (!fs::exists(ui)) return;
    auto text = formats::StringTable::parse(formats::read_file(ui / "text.uib"));
    uint64_t t = 1000;
    ui::Credits c(text, t);
    // 21 empty rows, then CreditsStart (972) .. CreditsEnd (1321): 371 rows.
    CHECK(c.rows() == 371);
    CHECK(c.length() == 5565);
    CHECK(c.visible().empty()); // offset 0: only empty rows in the box
    // At most one pixel per step, only after more than 10 ms.
    c.step(t + 10);
    CHECK(c.offset() == 0);
    for (int i = 0; i < 31; ++i) c.step(t += 11);
    CHECK(c.offset() == 31);
    auto lines = c.visible();
    REQUIRE(lines.size() == 1);
    CHECK(*lines[0].text == "DUNE 2000");
    CHECK(lines[0].y == 349);
    CHECK(lines[0].alpha == 1); // d = 1, 1 * 32 / 30
    // Mid-box rows are solid.
    for (int i = 31; i < 200; ++i) c.step(t += 11);
    bool solid = false;
    for (const auto& l : c.visible()) solid |= l.alpha == 32 && l.y >= 80 && l.y <= 320;
    CHECK(solid);
    // The end: nothing drawn, 2 s pause, then from the top again.
    while (c.offset() < c.length()) c.step(t += 11);
    CHECK(c.visible().empty());
    c.step(t + 2000);
    CHECK(c.offset() == c.length());
    c.step(t + 2001);
    CHECK(c.offset() == 0);
}

TEST_CASE("edit boxes: NoSpaces / NoNumbers / NoLetters / NoOtherChars (FUN_0043cb70 0x43cc9b)") {
    using ui::ElementState;
    for (unsigned char c : {'A', 'z', '5', ' '}) CHECK(ElementState::char_allowed(c, 8)); // NoOtherChars
    for (unsigned char c : {'!', '.', ':', '@', '[', '_', '`', '{', '~'}) CHECK(!ElementState::char_allowed(c, 8));
    CHECK(!ElementState::char_allowed(' ', 1));
    CHECK(!ElementState::char_allowed('7', 2));
    CHECK(!ElementState::char_allowed('q', 4));
    CHECK(ElementState::char_allowed('.', 7)); // the other classes leave punctuation alone
    CHECK(!ElementState::char_allowed(0x1f, 0));
    CHECK(!ElementState::char_allowed(0xe9, 0)); // a signed byte: nothing above 0x7F
}
