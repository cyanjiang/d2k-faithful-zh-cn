#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "formats/font.hpp"
#include "i18n/i18n.hpp"
#include "i18n/utf8.hpp"
#include "ui/render.hpp"
#include "ui/surface.hpp"

namespace {
d2k::formats::Font stub_font() {
    d2k::formats::Font f;
    f.header[1] = 5;    // space width
    f.header[2] = 0x21; // first character
    f.header[3] = 1;    // spacing
    f.first_char = 0x21;
    f.height = 14;
    f.glyphs.resize(1);
    f.glyphs[0].width = 2;
    f.glyphs[0].height = 2;
    f.glyphs[0].pixels = {1, 0, 0, 1};
    return f;
}
std::filesystem::path pack_dir() { return std::filesystem::path(D2K_REPO_DIR) / "i18n" / "zh-cn"; }
std::filesystem::path repo_dir() { return std::filesystem::path(D2K_REPO_DIR); }
} // namespace

TEST_CASE("i18n: utf8 keeps cp1252 bytes intact") {
    using namespace d2k::i18n::utf8;
    std::size_t p = 0;
    auto d = decode("A", p);
    CHECK(d.cp == 'A');
    CHECK_FALSE(d.multibyte);
    CHECK(p == 1);

    p = 0;
    d = decode("\xE6\xB2\x99", p); // 沙
    CHECK(d.cp == 0x6C99);
    CHECK(d.multibyte);
    CHECK(p == 3);

    p = 0;
    d = decode(std::string(1, char(0xAC)), p); // the exe's line-break mark
    CHECK(d.cp == 0xAC);
    CHECK_FALSE(d.multibyte);

    p = 0;
    d = decode(std::string(1, char(0xE9)), p); // é in Windows-1252
    CHECK(d.cp == 0xE9);
    CHECK_FALSE(d.multibyte);

    const std::string jing = "\xE4\xBA\xAC"; // 京 ends in 0xAC
    CHECK_FALSE(is_char_start(jing, 2));
    CHECK(is_char_start(jing, 0));
    CHECK(truncate("\xE6\xB2\x99\xE4\xB8\x98" "2000", 4) == "\xE6\xB2\x99");
    CHECK(next_boundary("\xE6\xB2\x99!", 0) == 3);
    CHECK(prev_boundary("\xE6\xB2\x99", 2) == 0);
}

TEST_CASE("i18n: English-only is the untouched original path") {
    auto& i = d2k::i18n::I18n::instance();
    i.reset();
    CHECK_FALSE(i.enabled());
    CHECK(d2k::i18n::cjk_font() == nullptr);
    CHECK(d2k::i18n::localize("OK") == "OK");
    const auto f = stub_font();
    CHECK(d2k::ui::text_width(f, "!!") == 6);
    CHECK(d2k::ui::text_width(f, std::string(1, char(0xAC))) == 5);
}

TEST_CASE("i18n: translation, resources and glyph drawing") {
    auto& i = d2k::i18n::I18n::instance();
    REQUIRE(i.init({repo_dir()}, "zh-cn"));
    CHECK(i.enabled());
    CHECK(d2k::i18n::localize("OK") == "确定");
    CHECK(d2k::i18n::localize("Atreides") == "亚崔迪");
    CHECK(d2k::i18n::localize("unknown text") == "unknown text");
    CHECK(d2k::i18n::localize("g_int1_e", "mve_intro1") == "g_int1_e");
    CHECK(d2k::i18n::localize("but1_up") == "but1_up");
    // hardcoded strings outside text.uib (first-run setup, lobby room names)
    CHECK(d2k::i18n::localize("Install") == "安装");
    CHECK(d2k::i18n::localize("DUNE 2000  -  GAME DATA SETUP") == "《沙丘 2000》 - 游戏数据安装");
    CHECK(d2k::i18n::localize("'s Game") == "的游戏");

    const auto t = d2k::i18n::Translation::load(pack_dir() / "strings.tsv");
    CHECK(t.lookup("The Emperor has granted you Arrakis.").find(char(0xAC)) != std::string::npos);

    const auto f = stub_font();
    CHECK(d2k::ui::text_width(f, "确") == 14);
    CHECK(d2k::ui::text_width(f, "!确!") == 20);
    CHECK(d2k::ui::text_width(f, "!!") == 6); // ASCII unchanged with the pack on

    d2k::ui::Surface s(64, 32);
    CHECK(s.draw_text_solid(f, 0, 0, "确", 0x7fff) == 14);
    int lit = 0;
    for (const uint16_t px : s.pixels())
        if (px != 0) ++lit;
    CHECK(lit > 0);
    i.reset();
}

TEST_CASE("i18n: TSV escapes restore the exact text.uib bytes") {
    const auto path = std::filesystem::temp_directory_path() / "d2k_i18n_escape_test.tsv";
    {
        std::ofstream out(path, std::ios::binary);
        out << "Line one\\nLine two\t第一段\\n第二段\n";
        out << "Tiiopa\\x92il\t提奥帕伊尔\n";
    }
    const auto t = d2k::i18n::Translation::load(path);
    std::string key = "Line one";
    key += char(0xAC); // the runtime sees a single forced-break byte
    key += "Line two";
    const std::string want = "第一段" + std::string(1, char(0xAC)) + "第二段";
    CHECK(std::string(t.lookup(key)) == want);
    std::string cp1252 = "Tiiopa";
    cp1252 += char(0x92);
    cp1252 += "il";
    CHECK(std::string(t.lookup(cp1252)) == "提奥帕伊尔");
    std::filesystem::remove(path);
}

TEST_CASE("i18n: CJK paragraphs wrap instead of vanishing") {
    auto& i = d2k::i18n::I18n::instance();
    REQUIRE(i.init({repo_dir()}, "zh-cn"));
    const auto f = stub_font();
    const std::string text = "这是一段很长的中文任务简报文本没有任何空格用于验证自动折行是否正确工作并且需要足够长";
    const auto lines = d2k::ui::split_textbox_text(f, text, 120);
    REQUIRE(lines.size() > 1);
    std::string joined;
    for (const auto& line : lines) {
        CHECK(d2k::ui::text_width(f, line) < 120);
        joined += line;
    }
    CHECK(joined == text); // no dropped or duplicated characters
    const std::string with_break = "第一段" + std::string(1, char(0xAC)) + "第二段";
    const auto forced = d2k::ui::split_textbox_text(f, with_break + with_break + with_break, 120);
    CHECK(forced.size() >= 3);
    i.reset();
}

TEST_CASE("i18n: CKF font validity") {
    const auto f = d2k::i18n::CjkFont::load(pack_dir() / "font.ckf");
    CHECK(f.valid());
    CHECK(f.cell_w() == 16);
    CHECK(f.find(0x786E) != nullptr); // 确
    CHECK(f.find(0x6C99) != nullptr); // 沙
    CHECK(f.find(0x1F600) == nullptr);
}
