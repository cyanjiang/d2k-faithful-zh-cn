#pragma once
// The first-run import (docs/first-run-setup.md): sources (a CD image, a CD
// or install folder, the 1.06 patch), hash-driven matching against the 1.06
// manifest, and the copy into the game's data folder.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace d2k::platform::setup {

enum class SourceKind { CdImage, Folder, Patch };

struct SourceItem {
    std::string path; // inside the source, for messages
    uint64_t size = 0;
    std::function<std::vector<uint8_t>()> read;
};

struct Source {
    SourceKind kind = SourceKind::Folder;
    std::filesystem::path location;
    std::string label; // e.g. "DUNE2000.iso (CD image)"
    std::vector<SourceItem> items;
};

// Opens a source and lists what it holds; nested archives (the CD's
// SETUP\SETUP.Z, a patch .exe inside a folder) are listed as items too.
// Throws std::runtime_error with a message for the player.
Source open_cd_image(const std::filesystem::path& iso);
Source open_folder(const std::filesystem::path& dir);
Source open_patch(const std::filesystem::path& exe);

// Progress: bytes done, bytes total. Returning false cancels.
using Progress = std::function<bool(uint64_t, uint64_t)>;

class Collector {
public:
    Collector();

    // Hashes the source's items that could fill a missing manifest file and
    // keeps the matches. Returns how many files it newly supplies (0 also
    // when cancelled). The source is kept even if it supplies nothing.
    size_t add(Source source, const Progress& progress = {});

    size_t found() const;
    size_t needed() const;
    bool complete() const { return found() == needed(); }
    const std::vector<Source>& sources() const { return sources_; }
    size_t supplied_by(size_t source) const;
    // The missing files in plain words (empty when complete), e.g.
    // "the 1.06 patch (d2k106us.exe)", "the movies (add the CD or CD image)".
    std::vector<std::string> missing_summary(size_t examples = 3) const;

    // Writes every file into `dest` through `dest` + ".tmp" and renames it
    // when all are written and checked. Throws std::runtime_error; a cancel
    // removes the temporary folder and returns false.
    bool install(const std::filesystem::path& dest, const Progress& progress = {}) const;

private:
    struct Match {
        int source = -1, item = -1;
    };
    std::vector<Source> sources_;
    std::vector<Match> match_; // per manifest entry
};

} // namespace d2k::platform::setup
