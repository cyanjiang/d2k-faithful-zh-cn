// Standalone self-test for the optional localisation layer.
// Builds without SDL3 / doctest, so it can be run on a bare MSVC install:
//   cl /std:c++20 /utf-8 /EHsc /Isrc \
//      src/i18n/*.cpp src/formats/font.cpp src/formats/byte_reader.cpp \
//      src/ui/surface.cpp tools/i18n_selftest.cpp
// Usage: i18n_selftest <repo-root>   (contains i18n/zh-cn/strings.tsv + font.ckf)

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "formats/font.hpp"
#include "i18n/i18n.hpp"
#include "i18n/utf8.hpp"
#include "ui/surface.hpp"

namespace {
int failures = 0, checks = 0;
void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}
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
} // namespace

int main(int argc, char** argv) {
    using namespace d2k;
    const std::filesystem::path root = argc > 1 ? argv[1] : std::filesystem::current_path();
    const auto pack = root / "i18n" / "zh-cn";

    // 1. UTF-8 decoding, with the original cp1252 bytes preserved.
    {
        std::size_t p = 0;
        auto d = i18n::utf8::decode("A", p);
        check(d.cp == 'A' && !d.multibyte && p == 1, "ASCII decodes as a single byte");
        p = 0;
        d = i18n::utf8::decode("\xE6\xB2\x99", p); // 沙
        check(d.cp == 0x6C99 && d.multibyte && p == 3, "three-byte UTF-8 decodes");
        p = 0;
        d = i18n::utf8::decode(std::string(1, char(0xAC)), p);
        check(d.cp == 0xAC && !d.multibyte && p == 1, "0xAC line break stays a cp1252 byte");
        p = 0;
        d = i18n::utf8::decode(std::string(1, char(0xE9)), p); // é in cp1252
        check(d.cp == 0xE9 && !d.multibyte && p == 1, "bare 0xE9 does not become UTF-8");
        const std::string jing = "\xE4\xBA\xAC"; // 京, last byte is 0xAC
        check(!i18n::utf8::is_char_start(jing, 2), "0xAC inside UTF-8 is not a char start");
        check(i18n::utf8::is_char_start(jing, 0), "first byte is a char start");
        check(i18n::utf8::truncate("\xE6\xB2\x99\xE4\xB8\x98" "2000", 4) == "\xE6\xB2\x99",
              "truncate never cuts a multi-byte character");
        check(i18n::utf8::next_boundary("\xE6\xB2\x99!", 0) == 3, "next_boundary");
        check(i18n::utf8::prev_boundary("\xE6\xB2\x99!", 3) == 0, "prev_boundary on a boundary");
        check(i18n::utf8::prev_boundary("\xE6\xB2\x99", 2) == 0, "prev_boundary inside a glyph");
    }

    // 2. Translation table and font loading.
    {
        auto t = i18n::Translation::load(pack / "strings.tsv");
        check(t.valid(), "strings.tsv loads");
        check(t.lookup("OK") == "确定", "OK translates");
        check(t.lookup("Atreides") == "亚崔迪", "house name translates");
        check(t.lookup("not in the table") == "not in the table", "miss falls back to source");
        const auto br = t.lookup("The Emperor has granted you Arrakis.");
        check(br.find(char(0xAC)) != std::string::npos, "\\n becomes the 0xAC line break");

        auto f = i18n::CjkFont::load(pack / "font.ckf");
        check(f.valid() && f.cell_w() == 16 && f.cell_h() == 16, "font.ckf loads");
        check(f.find(0x786E) != nullptr, "glyph 确 exists");
        check(f.find(0x6C99) != nullptr, "glyph 沙 exists");
        check(f.find(0x1F600) == nullptr, "unknown glyph returns null");
    }

    // 3. English-only behaviour is unchanged.
    {
        i18n::I18n::instance().reset();
        check(!i18n::I18n::instance().enabled(), "disabled by default");
        check(i18n::cjk_font() == nullptr, "no sidecar font in English mode");
        check(i18n::localize("OK") == "OK", "no translation when disabled");
        const auto f = stub_font();
        check(d2k::ui::text_width(f, "!!") == 6, "ASCII width is the original 2*(2+1)");
        check(d2k::ui::text_width(f, std::string(1, char(0xAC))) == 5,
              "0xAC still maps to the space width");
    }

    // 4. Language pack enabled: translation, resource protection, drawing.
    {
        const bool ok = i18n::I18n::instance().init({root}, "zh-cn");
        check(ok, "i18n/zh-cn initialises");
        check(i18n::I18n::instance().enabled(), "pack enabled");
        std::printf("status: %s\n", i18n::I18n::instance().status().c_str());
        check(i18n::localize("OK") == "确定", "OK is localised");
        check(i18n::localize("g_int1_e", "mve_intro1") == "g_int1_e", "movie name is not translated");
        check(i18n::localize("but1_up") == "but1_up", "UI image name is not translated");
        check(i18n::localize("unknown text") == "unknown text", "untranslated text stays English");

        const auto f = stub_font();
        check(d2k::ui::text_width(f, "确") == 14, "CJK glyph advance");
        check(d2k::ui::text_width(f, "!确!") == 20, "mixed ASCII/UTF-8 width");
        check(d2k::ui::text_width(f, "!!") == 6, "ASCII width still identical with pack on");

        ui::Surface s(64, 32);
        const int w = s.draw_text_solid(f, 0, 0, "确", 0x7fff);
        check(w == 14, "draw returns the same advance as text_width");
        int lit = 0;
        for (const uint16_t px : s.pixels())
            if (px != 0) ++lit;
        check(lit > 0, "Chinese glyph actually writes pixels");
        std::printf("drawn pixels for 确: %d\n", lit);

        // 0xAC inside 京 must not be treated as a line-break byte by the
        // boundary helper (render.cpp uses is_char_start for separators).
        check(!i18n::utf8::is_char_start("\xE4\xBA\xAC", 2), "line-break byte inside glyph");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
