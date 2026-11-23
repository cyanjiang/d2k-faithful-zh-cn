#pragma once
// Minimal reader for the original executable (DUNE2000.DAT, a Win32 PE), so
// small data tables the game keeps only in its exe (e.g. the territory
// shapes) are read from the player's own copy instead of being shipped.

#include <cstdint>
#include <span>
#include <vector>

namespace d2k::formats {

class PeImage {
public:
    explicit PeImage(std::vector<uint8_t> file);

    // Bytes at a virtual address (as in Ghidra / docs/re), or throws
    // FormatError if [va, va + size) is not inside a section's file data.
    std::span<const uint8_t> at(uint32_t va, size_t size) const;

private:
    struct Section {
        uint32_t va, size, file_offset;
    };
    std::vector<uint8_t> data_;
    uint32_t image_base_ = 0;
    std::vector<Section> sections_;
};

} // namespace d2k::formats
