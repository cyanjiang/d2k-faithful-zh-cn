#include <doctest/doctest.h>

#include <filesystem>
#include <vector>

#include "formats/aud.hpp"
#include "formats/byte_reader.hpp"
#include "formats/file_io.hpp"
#include "formats/sound_rs.hpp"

using namespace d2k::formats;
namespace fs = std::filesystem;

TEST_CASE("decode_wav: 8-bit PCM is centred and widened") {
    std::vector<uint8_t> w{'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E',
                           'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                           0x22, 0x56, 0, 0, 0x22, 0x56, 0, 0, 1, 0, 8, 0,
                           'd', 'a', 't', 'a', 3, 0, 0, 0, 0x80, 0xFF, 0x00};
    w.push_back(0); // pad byte for odd-sized chunk
    auto pcm = decode_wav(w);
    CHECK(pcm.sample_rate == 22050);
    REQUIRE(pcm.samples.size() == 3);
    CHECK(pcm.samples[0] == 0);
    CHECK(pcm.samples[1] == 127 * 256);
    CHECK(pcm.samples[2] == -128 * 256);
}

TEST_CASE("decode_aud: rejects unsupported compression") {
    std::vector<uint8_t> a{0x22, 0x56, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1};
    CHECK_THROWS_AS(decode_aud(a), FormatError);
}

TEST_CASE("golden: all AUD files and SOUND.RS decode (skips without data)") {
    fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root)) {
        MESSAGE("skipped: no game data");
        return;
    }
    size_t auds = 0;
    for (const auto& dir : {root / "MUSIC", root / "Data" / "GAMESFX"}) {
        for (const auto& e : fs::directory_iterator(dir)) {
            if (e.path().extension() != ".AUD") continue;
            CAPTURE(e.path().filename().string());
            auto aud = decode_aud(read_file(e.path()));
            CHECK(aud.header.sample_rate == 22050);
            CHECK_FALSE(aud.samples.empty());
            ++auds;
        }
    }
    CHECK(auds == 268);

    auto rs = read_file(root / "Data" / "GAMESFX" / "SOUND.RS");
    auto entries = parse_sound_rs(rs);
    CHECK(entries.size() == 57);
    for (const auto& e : entries) {
        CAPTURE(e.name);
        auto wav = decode_wav(std::span<const uint8_t>(rs).subspan(e.offset, e.size));
        CHECK_FALSE(wav.samples.empty());
    }
}

TEST_CASE("AudStream fills whole chunks, zero-pads, and loops keeping state") {
    // Header (22050 Hz, mono 16-bit, type 99) + two chunks of 2 bytes -> 4 samples.
    std::vector<uint8_t> a{0x22, 0x56, 20, 0, 0, 0, 16, 0, 0, 0, 2, 99};
    for (int i = 0; i < 2; ++i) {
        for (int b : {2, 0, 8, 0, 0xAF, 0xDE, 0, 0, 0x77, 0x77}) a.push_back(uint8_t(b));
    }
    AudStream s(a);
    std::vector<int16_t> buf(12, 123);
    CHECK(s.fill_looping(buf) == 8);
    CHECK(buf[7] != 0);
    CHECK(buf[8] == 0); // zero-filled tail
    CHECK(buf[11] == 0);
    std::vector<int16_t> first(buf.begin(), buf.begin() + 8);
    // Next fill reads nothing, so it rewinds; the decoder state carries on,
    // so the second pass differs from the first.
    CHECK(s.fill_looping(buf) == 8);
    CHECK(std::vector<int16_t>(buf.begin(), buf.begin() + 8) != first);
}

TEST_CASE("golden: OPTIONS.AUD streams in 0x8000-byte buffers (skips without data)") {
    fs::path p = fs::path(D2K_GAMEDATA_DIR) / "MUSIC" / "OPTIONS.AUD";
    if (!fs::exists(p)) return;
    auto bytes = read_file(p);
    auto whole = decode_aud(bytes);
    AudStream s(bytes);
    std::vector<int16_t> buf(0x4000), all;
    size_t n;
    while ((n = s.fill(buf)) > 0) all.insert(all.end(), buf.begin(), buf.begin() + std::ptrdiff_t(n));
    REQUIRE(all.size() == whole.samples.size());
    // Identical except the final sample, which the exe decodes from a stale byte.
    CHECK(std::equal(all.begin(), all.end() - 1, whole.samples.begin()));
}
