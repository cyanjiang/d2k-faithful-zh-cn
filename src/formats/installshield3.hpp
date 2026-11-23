#pragma once
// InstallShield 3 archives (`SETUP\SETUP.Z` on the Dune 2000 CD) and their
// PKWARE DCL "implode" compression. Layout: docs/formats/installshield3.md
// (the C++ form of tools/is3extract.py).

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::formats {

struct Is3Entry {
    std::string dir;  // e.g. "DUNE\Data\GAMESFX"
    std::string name; // e.g. "SOUND.RS"
    uint32_t size = 0;
    uint32_t compressed_size = 0;
    uint32_t offset = 0;
};

// The file list of a whole archive. Throws FormatError.
std::vector<Is3Entry> is3_index(std::span<const uint8_t> archive);

// One entry, decompressed and checked against its stated size.
std::vector<uint8_t> is3_extract(std::span<const uint8_t> archive, const Is3Entry& e);

// PKWARE Data Compression Library "explode" (binary or ASCII literals, 4-6
// bit dictionary). Throws FormatError.
std::vector<uint8_t> dcl_explode(std::span<const uint8_t> in, size_t size_hint = 0);

} // namespace d2k::formats
