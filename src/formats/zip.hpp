#pragma once
// ZIP archives (PKWARE APPNOTE), Store and Deflate only. The 1.06 patch
// `d2k106us.exe` is a PE stub with a ZIP appended (docs/first-run-setup.md);
// offsets are found from the end of the file, so a prefix is fine.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::formats {

struct ZipEntry {
    std::string name;      // as stored, '\' or '/' separated
    uint16_t method = 0;   // 0 store, 8 deflate
    uint32_t crc32 = 0;
    uint32_t compressed_size = 0;
    uint32_t size = 0;
    uint64_t header_offset = 0; // the local header, in the whole file
    bool directory = false;
};

class ZipArchive {
public:
    // Parses the central directory of `data` (kept by reference: the caller
    // owns the bytes). Throws FormatError.
    explicit ZipArchive(std::span<const uint8_t> data);

    const std::vector<ZipEntry>& entries() const { return entries_; }
    // Decompresses one entry and checks its size and CRC-32.
    std::vector<uint8_t> extract(const ZipEntry& e) const;

private:
    std::span<const uint8_t> data_;
    std::vector<ZipEntry> entries_;
};

uint32_t crc32(std::span<const uint8_t> data);

} // namespace d2k::formats
