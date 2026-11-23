#pragma once
// ISO 9660 CD images (the Dune 2000 CD as a .iso). Plain 2048-byte sectors,
// or raw 2352-byte sectors (a .bin image, Mode 1 or Mode 2 Form 1), told
// apart by the sector sync pattern. Files are read from disk on demand.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace d2k::formats {

struct IsoFile {
    std::string path; // '/' separated, without the ";1" version, e.g. "SETUP/SETUP.Z"
    uint32_t lba = 0;
    uint32_t size = 0;
};

class IsoImage {
public:
    // Opens the image and lists every file. Throws FormatError /
    // std::runtime_error.
    explicit IsoImage(const std::filesystem::path& path);

    const std::vector<IsoFile>& files() const { return files_; }
    std::string volume_id() const { return volume_id_; }
    // The whole file's bytes.
    std::vector<uint8_t> read(const IsoFile& f);

private:
    void read_sector(uint32_t lba, uint8_t* out);
    void walk(uint32_t lba, uint32_t size, const std::string& prefix, int depth);

    std::ifstream in_;
    uint32_t sector_size_ = 2048; // on disk
    uint32_t data_offset_ = 0;    // of the 2048 user bytes inside a raw sector
    std::string volume_id_;
    std::vector<IsoFile> files_;
};

} // namespace d2k::formats
