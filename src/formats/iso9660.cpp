#include "formats/iso9660.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include "formats/byte_reader.hpp"

namespace d2k::formats {

namespace {

constexpr uint32_t kUser = 2048;

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

} // namespace

IsoImage::IsoImage(const std::filesystem::path& path) : in_(path, std::ios::binary) {
    if (!in_) throw std::runtime_error("cannot open " + path.string());
    // A raw image starts with the 12-byte sync pattern 00 FF x10 00.
    std::array<uint8_t, 16> head{};
    in_.read(reinterpret_cast<char*>(head.data()), head.size());
    static constexpr std::array<uint8_t, 12> kSync{0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
    if (in_ && std::equal(kSync.begin(), kSync.end(), head.begin())) {
        sector_size_ = 2352;
        data_offset_ = head[15] == 2 ? 24 : 16; // Mode 2 Form 1 has an 8-byte subheader
    }
    // Volume descriptors from sector 16; the primary one has type 1.
    std::array<uint8_t, kUser> s{};
    for (uint32_t lba = 16; lba < 32; ++lba) {
        read_sector(lba, s.data());
        if (std::memcmp(s.data() + 1, "CD001", 5) != 0) throw FormatError("not an ISO 9660 image");
        if (s[0] == 255) break;
        if (s[0] != 1) continue;
        volume_id_.assign(reinterpret_cast<const char*>(s.data() + 40), 32);
        while (!volume_id_.empty() && volume_id_.back() == ' ') volume_id_.pop_back();
        const uint8_t* root = s.data() + 156; // the root directory record
        walk(le32(root + 2), le32(root + 10), "", 0);
        return;
    }
    throw FormatError("ISO 9660: no primary volume descriptor");
}

void IsoImage::read_sector(uint32_t lba, uint8_t* out) {
    in_.clear();
    in_.seekg(std::streamoff(uint64_t(lba) * sector_size_ + data_offset_));
    in_.read(reinterpret_cast<char*>(out), kUser);
    if (!in_) throw FormatError("ISO 9660: read past the end of the image");
}

void IsoImage::walk(uint32_t lba, uint32_t size, const std::string& prefix, int depth) {
    if (depth > 16) throw FormatError("ISO 9660: directories nested too deep");
    std::vector<uint8_t> dir(((size + kUser - 1) / kUser) * kUser);
    for (uint32_t i = 0; i < dir.size() / kUser; ++i) read_sector(lba + i, dir.data() + size_t(i) * kUser);
    size_t p = 0;
    while (p < size) {
        const uint8_t len = dir[p];
        if (len == 0) { // records do not cross sectors: go to the next one
            p = (p / kUser + 1) * kUser;
            continue;
        }
        if (len < 34 || p + len > dir.size()) throw FormatError("ISO 9660: bad directory record");
        const uint8_t* r = dir.data() + p;
        const uint8_t name_len = r[32];
        const bool is_dir = (r[25] & 2) != 0;
        std::string name(reinterpret_cast<const char*>(r + 33), name_len);
        p += len;
        if (name_len == 1 && (name[0] == 0 || name[0] == 1)) continue; // "." and ".."
        if (const auto semi = name.find(';'); semi != std::string::npos) name.resize(semi);
        if (!name.empty() && name.back() == '.') name.pop_back(); // "README." for no extension
        const std::string full = prefix.empty() ? name : prefix + "/" + name;
        if (is_dir) {
            walk(le32(r + 2), le32(r + 10), full, depth + 1);
        } else {
            files_.push_back({full, le32(r + 2), le32(r + 10)});
        }
    }
}

std::vector<uint8_t> IsoImage::read(const IsoFile& f) {
    std::vector<uint8_t> out(f.size);
    if (sector_size_ == kUser) { // contiguous: one read
        in_.clear();
        in_.seekg(std::streamoff(uint64_t(f.lba) * kUser));
        in_.read(reinterpret_cast<char*>(out.data()), std::streamsize(f.size));
        if (!in_) throw FormatError("ISO 9660: read past the end of the image");
        return out;
    }
    std::array<uint8_t, kUser> s{};
    for (uint32_t done = 0, i = 0; done < f.size; ++i) {
        read_sector(f.lba + i, s.data());
        const uint32_t n = std::min(kUser, f.size - done);
        std::memcpy(out.data() + done, s.data(), n);
        done += n;
    }
    return out;
}

} // namespace d2k::formats
