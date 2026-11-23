#include <doctest/doctest.h>

#include <filesystem>
#include <vector>

#include "formats/byte_reader.hpp"
#include "formats/file_io.hpp"
#include "formats/palette.hpp"
#include "formats/r8.hpp"

using namespace d2k::formats;
namespace fs = std::filesystem;

namespace {

void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i)));
}

std::vector<uint8_t> entry(uint8_t type, uint32_t w, uint32_t h, uint32_t pal_handle, uint8_t bpp) {
    std::vector<uint8_t> v{type};
    put32(v, w);
    put32(v, h);
    put32(v, uint32_t(-3)); // offset_x
    put32(v, 5);            // offset_y
    put32(v, 0xDEADBEEF);   // image handle
    put32(v, pal_handle);
    v.insert(v.end(), {bpp, uint8_t(h), uint8_t(w), 0});
    for (uint32_t i = 0; i < w * h * (bpp / 8); ++i) v.push_back(uint8_t(i));
    return v;
}

} // namespace

TEST_CASE("parse_r8: synthetic entries of every kind") {
    std::vector<uint8_t> data;
    auto a = entry(1, 2, 3, 0, 8);
    data.insert(data.end(), a.begin(), a.end());
    data.push_back(0); // empty placeholder
    auto b = entry(1, 1, 1, 0x1234, 8);
    data.insert(data.end(), b.begin(), b.end());
    put32(data, 1);
    put32(data, 0xCAFE);
    for (int i = 0; i < 256; ++i) { data.push_back(uint8_t(i)); data.push_back(0); }
    auto c = entry(2, 1, 1, 0x5678, 8); // type 2: no palette data follows
    data.insert(data.end(), c.begin(), c.end());
    auto d = entry(1, 2, 2, 0, 16);
    data.insert(data.end(), d.begin(), d.end());

    auto images = parse_r8(data);
    REQUIRE(images.size() == 5);
    CHECK(images[0].width == 2);
    CHECK(images[0].height == 3);
    CHECK(images[0].offset_x == -3);
    CHECK(images[0].offset_y == 5);
    CHECK(images[0].pixels.size() == 6);
    CHECK(images[1].empty());
    REQUIRE(images[2].palette.has_value());
    CHECK((*images[2].palette)[255] == 255);
    CHECK_FALSE(images[3].palette.has_value());
    CHECK(images[4].pixels.size() == 8);
}

TEST_CASE("parse_r8: rejects unknown entry type and truncation") {
    std::vector<uint8_t> bad{7};
    CHECK_THROWS_AS(parse_r8(bad), FormatError);
    auto e = entry(1, 4, 4, 0, 8);
    e.pop_back();
    CHECK_THROWS_AS(parse_r8(e), FormatError);
}

TEST_CASE("colour conversions") {
    CHECK(vga6_to_8(63) == 252); // FUN_004a3f50: << 2
    CHECK(vga6_to_8(0) == 0);
    CHECK(rgb565_to_rgb(0xFFFF) == Rgb{255, 255, 255});
    CHECK(rgb555_to_rgb(0x7FFF) == Rgb{255, 255, 255});
    CHECK(rgb555_to_rgb(0x7C00) == Rgb{255, 0, 0});
}

TEST_CASE("golden: every R8/R16 sprite file in 1.06 parses (skips without data)") {
    fs::path dir = fs::path(D2K_GAMEDATA_DIR) / "Data";
    if (!fs::exists(dir)) {
        MESSAGE("skipped: no game data");
        return;
    }
    // Known counts from the 1.06 data (see docs/formats/r8.md).
    struct Expect { const char* name; size_t entries; };
    for (auto [name, entries] : {Expect{"DATA.R8", 6555}, Expect{"DATA.R16", 6555},
                                 Expect{"MOUSE.R8", 265}, Expect{"MOUSE.R16", 265},
                                 Expect{"BLOXBASE.R8", 800}, Expect{"BLOXBASE.R16", 800},
                                 Expect{"BLOXXMAS.R8", 800}}) {
        CAPTURE(name);
        CHECK(parse_r8(read_file(dir / name)).size() == entries);
    }
    auto pal = parse_vga_palette(read_file(dir / "bin" / "PALETTE.BIN"));
    CHECK(pal[0] == Rgb{0, 0, 0});
}
