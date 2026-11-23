// The first-run import (docs/first-run-setup.md).
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>

#include "platform/manifest.hpp"
#include "platform/setup_sources.hpp"

using namespace d2k;
namespace fs = std::filesystem;
namespace setup = platform::setup;

TEST_CASE("the built-in 1.06 manifest: 1305 files, DUNE2000.DAT and the Data, MISSIONS, MOVIES, MUSIC trees") {
    const auto m = platform::manifest_106();
    CHECK(m.size() == 1305);
    bool exe = false;
    for (const auto& e : m) {
        const std::string p = e.path;
        exe = exe || p == "DUNE2000.DAT";
        CHECK((p == "DUNE2000.DAT" || p.rfind("Data/", 0) == 0 || p.rfind("MISSIONS/", 0) == 0 ||
               p.rfind("MOVIES/", 0) == 0 || p.rfind("MUSIC/", 0) == 0));
        CHECK(std::string(e.sha256).size() == 64);
    }
    CHECK(exe);
}

TEST_CASE("golden: our repo's game folder passes the start-up check (skips without data)") {
    const fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "DUNE2000.DAT")) return;
    CHECK(platform::quick_check(root).empty());
    CHECK_FALSE(platform::quick_check(root / "nothing-here").empty());
}

TEST_CASE("golden: the CD image alone lacks the 1.06 patch; with the patch everything is found and installs into a folder that passes the check (skips without DUNE2000.iso and d2k106us.exe)") {
    const fs::path repo(D2K_REPO_DIR);
    if (!fs::exists(repo / "DUNE2000.iso") || !fs::exists(repo / "d2k106us.exe")) return;
    const auto t0 = std::chrono::steady_clock::now();
    setup::Collector c;
    const size_t from_cd = c.add(setup::open_cd_image(repo / "DUNE2000.iso"));
    CHECK(from_cd > 1000);
    CHECK_FALSE(c.complete());
    const auto missing = c.missing_summary();
    REQUIRE_FALSE(missing.empty());
    CHECK(missing[0] == "the 1.06 patch (d2k106us.exe)");
    const size_t from_patch = c.add(setup::open_patch(repo / "d2k106us.exe"));
    CHECK(from_patch > 0);
    CHECK(c.complete());
    CHECK(c.missing_summary().empty());
    CHECK(c.supplied_by(0) + c.supplied_by(1) == c.needed());
    const fs::path dest = fs::temp_directory_path() / "d2k-setup-test" / "1.06";
    std::error_code ec;
    fs::remove_all(dest.parent_path(), ec);
    fs::create_directories(dest.parent_path(), ec);
    REQUIRE(c.install(dest));
    CHECK_FALSE(fs::exists(fs::path(dest) += ".tmp"));
    CHECK(platform::quick_check(dest).empty());
    MESSAGE("CD " << from_cd << " + patch " << from_patch << " files, scan and install in "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s");
    fs::remove_all(dest.parent_path(), ec);
}

TEST_CASE("golden: an installed 1.06 folder supplies everything by itself (skips without data)") {
    const fs::path root(D2K_GAMEDATA_DIR);
    if (!fs::exists(root / "DUNE2000.DAT")) return;
    setup::Collector c;
    c.add(setup::open_folder(root));
    CHECK(c.complete());
}

TEST_CASE("setup sources refuse files that are not what they claim, with a plain message") {
    const fs::path here = fs::temp_directory_path() / "d2k-setup-bad.txt";
    {
        std::FILE* f = std::fopen(here.string().c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fputs("not an image", f);
        std::fclose(f);
    }
    CHECK_THROWS_AS(setup::open_cd_image(here), std::runtime_error);
    CHECK_THROWS_AS(setup::open_patch(here), std::runtime_error);
    CHECK_THROWS_AS(setup::open_folder(here), std::runtime_error);
    std::error_code ec;
    fs::remove(here, ec);
}
