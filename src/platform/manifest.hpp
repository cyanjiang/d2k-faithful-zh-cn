#pragma once
// The Dune 2000 1.06 US files our engine reads (docs/first-run-setup.md),
// with sizes and SHA-256 digests, built from docs/inventory-1.06.csv.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace d2k::platform {

struct ManifestEntry {
    const char* path;   // relative, '/' separated, e.g. "Data/bin/ARMOUR.BIN"
    uint32_t size;
    const char* sha256; // lower-case hex
};

std::span<const ManifestEntry> manifest_106();

// The start-up check: every manifest file present (case-insensitive) with
// its size, and DUNE2000.DAT's hash. Returns the problems, at most `limit`
// missing or wrong files listed by name; empty means usable.
std::vector<std::string> quick_check(const std::filesystem::path& root, size_t limit = 5);

} // namespace d2k::platform
