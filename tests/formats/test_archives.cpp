// Archive readers for the first-run setup (docs/first-run-setup.md): CRC-32,
// DEFLATE, ZIP, PKWARE DCL, InstallShield 3 and ISO 9660.
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "formats/byte_reader.hpp"
#include "formats/file_io.hpp"
#include "formats/inflate.hpp"
#include "formats/installshield3.hpp"
#include "formats/iso9660.hpp"
#include "formats/zip.hpp"
#include "platform/sha256.hpp"

using namespace d2k;
namespace fs = std::filesystem;

namespace {
std::string str(const std::vector<uint8_t>& v) { return std::string(v.begin(), v.end()); }
} // namespace

TEST_CASE("crc32 check value") {
    const std::string s = "123456789";
    CHECK(formats::crc32(std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size())) == 0xCBF43926u);
}

TEST_CASE("inflate: stored and fixed-code blocks (vectors from zlib)") {
    // zlib raw deflate, level 0 and Z_FIXED.
    const std::vector<uint8_t> stored{0x01, 0x09, 0x00, 0xf6, 0xff, 0x44, 0x75, 0x6e, 0x65, 0x20, 0x32, 0x30, 0x30, 0x30};
    CHECK(str(formats::inflate_raw(stored)) == "Dune 2000");
    const std::vector<uint8_t> fixed{0x4b, 0x4c, 0x4a, 0x4e, 0x44, 0x45, 0x00};
    CHECK(str(formats::inflate_raw(fixed)) == "abcabcabcabcabcabc");
    const std::vector<uint8_t> bad{0x07}; // block type 3
    CHECK_THROWS_AS(formats::inflate_raw(bad), formats::FormatError);
}

TEST_CASE("DCL explode: the blast.c example") {
    // PKWARE DCL, binary literals, 4-bit dictionary (zlib contrib/blast).
    const std::vector<uint8_t> in{0x00, 0x04, 0x82, 0x24, 0x25, 0x8f, 0x80, 0x7f};
    CHECK(str(formats::dcl_explode(in)) == "AIAIAIAIAIAIA");
}

TEST_CASE("golden: the 1.06 patch is a ZIP after a PE stub; every entry extracts with its CRC, and DUNE2000.DAT is the 1.06 exe (skips without d2k106us.exe)") {
    const fs::path patch = fs::path(D2K_REPO_DIR) / "d2k106us.exe";
    if (!fs::exists(patch)) return;
    const auto data = formats::read_file(patch);
    formats::ZipArchive zip(data);
    int files = 0, deflated = 0;
    bool exe = false;
    for (const auto& e : zip.entries()) {
        if (e.directory) continue;
        const auto bytes = zip.extract(e); // throws on a CRC mismatch
        ++files;
        deflated += e.method == 8;
        if (e.name == "DUNE2000.DAT") {
            exe = platform::sha256_hex(bytes) == "51082bd7306b5e76b93fc694fc6455ce1704140918f9abe2d42394682b85e8f2";
        }
    }
    CHECK(files == 341);
    CHECK(deflated > 300);
    CHECK(exe);
}

TEST_CASE("golden: the CD image: volume DUNE2000, SETUP/SETUP.Z with 1165 InstallShield 3 entries that explode to their sizes (skips without DUNE2000.iso)") {
    const fs::path iso_path = fs::path(D2K_REPO_DIR) / "DUNE2000.iso";
    if (!fs::exists(iso_path)) return;
    formats::IsoImage iso(iso_path);
    CHECK(iso.volume_id() == "DUNE2000");
    const formats::IsoFile* setup = nullptr;
    int movies = 0;
    for (const auto& f : iso.files()) {
        if (f.path == "SETUP/SETUP.Z") setup = &f;
        movies += f.path.rfind("MOVIES/", 0) == 0;
    }
    REQUIRE(setup != nullptr);
    CHECK(setup->size == 28109143u);
    CHECK(movies > 0);
    const auto z = iso.read(*setup);
    const auto index = formats::is3_index(z);
    CHECK(index.size() == 1165);
    size_t total = 0;
    for (const auto& e : index) total += formats::is3_extract(z, e).size(); // throws on a bad stream or size
    CHECK(total > 0);
}
