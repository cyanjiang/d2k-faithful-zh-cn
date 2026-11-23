#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <vector>

#include "formats/byte_reader.hpp"
#include "formats/file_io.hpp"
#include "formats/ima_adpcm.hpp"
#include "formats/lcw.hpp"
#include "formats/vqa.hpp"

using namespace d2k::formats;
namespace fs = std::filesystem;

TEST_CASE("lcw: literals, fill, short and long copies, end marker") {
    const std::vector<uint8_t> src{
        0x83, 'a', 'b', 'c',  // 3 literals
        0x00, 0x03,           // short copy: 3 bytes from 3 back -> "abc"
        0xFE, 0x04, 0x00, 'z', // fill 4 x 'z'
        0xC1, 0x01, 0x00,     // long copy: 4 bytes from absolute 1 -> "bcab"
        0xFF, 0x02, 0x00, 0x00, 0x00, // very long copy: 2 bytes from 0 -> "ab"
        0x80};                // end
    std::array<uint8_t, 64> dst{};
    size_t n = lcw_decompress(src, dst);
    CHECK(std::string(dst.begin(), dst.begin() + std::ptrdiff_t(n)) == "abcabczzzzbcabab");
}

TEST_CASE("lcw: relative mode addresses back from the output position") {
    const std::vector<uint8_t> src{0x00, 0x82, 'x', 'y', 0xC0, 0x02, 0x00, 0x80};
    std::array<uint8_t, 16> dst{};
    size_t n = lcw_decompress(src, dst);
    CHECK(std::string(dst.begin(), dst.begin() + std::ptrdiff_t(n)) == "xyxyx");
}

TEST_CASE("lcw: rejects overflow and bad references") {
    std::array<uint8_t, 2> small{};
    CHECK_THROWS_AS(lcw_decompress(std::vector<uint8_t>{0x83, 1, 2, 3}, small), FormatError);
    std::array<uint8_t, 16> dst{};
    CHECK_THROWS_AS(lcw_decompress(std::vector<uint8_t>{0x81, 1, 0x00, 0x05}, dst), FormatError);
}

TEST_CASE("ima adpcm: known first steps") {
    ImaAdpcmState s;
    std::vector<int16_t> out;
    ima_adpcm_decode(std::vector<uint8_t>{0x07}, s, out); // nibbles 7, then 0
    REQUIRE(out.size() == 2);
    CHECK(out[0] == 11); // 7>>3 + 7>>2 + 7>>1 + 7 = 0+1+3+7
    CHECK(s.step_index == 7); // 8 after nibble 7, then -1 after nibble 0
}

TEST_CASE("golden: every 1.06 movie decodes cleanly (skips without data)") {
    fs::path dir = fs::path(D2K_GAMEDATA_DIR) / "MOVIES";
    if (!fs::exists(dir)) {
        MESSAGE("skipped: no game data");
        return;
    }
    size_t movies = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() != ".VQA") continue;
        CAPTURE(e.path().filename().string());
        VqaDecoder vqa(read_file(e.path()));
        while (vqa.next_frame()) {
            vqa.take_audio();
        }
        CHECK(vqa.frame_index() + 1 == vqa.header().num_frames);
        CHECK(vqa.stats().sn2j_mismatches == 0);
        CHECK(vqa.stats().vptr_leftover_frames == 0);
        CHECK(vqa.stats().codebook_trailing_bytes <= 2);
        ++movies;
    }
    CHECK(movies == 43);
}
