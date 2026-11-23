#include "formats/zip.hpp"

#include <array>

#include "formats/byte_reader.hpp"
#include "formats/inflate.hpp"

namespace d2k::formats {

uint32_t crc32(std::span<const uint8_t> data) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : data) c = table[(c ^ b) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

ZipArchive::ZipArchive(std::span<const uint8_t> data) : data_(data) {
    // End of central directory: the last "PK\5\6" within the final 64 KiB + 22.
    if (data.size() < 22) throw FormatError("zip: too small");
    size_t eocd = size_t(-1);
    const size_t lowest = data.size() > 0x10000 + 22 ? data.size() - 0x10000 - 22 : 0;
    for (size_t p = data.size() - 22 + 1; p-- > lowest;) {
        if (data[p] == 'P' && data[p + 1] == 'K' && data[p + 2] == 5 && data[p + 3] == 6) {
            eocd = p;
            break;
        }
    }
    if (eocd == size_t(-1)) throw FormatError("zip: no end of central directory");
    ByteReader r(data);
    r.seek(eocd + 10);
    const uint16_t count = r.u16();
    const uint32_t cd_size = r.u32();
    const uint32_t cd_offset = r.u32();
    if (cd_size > eocd) throw FormatError("zip: bad central directory size");
    // A prefix (the SFX stub) shifts every stored offset by the same amount.
    const uint64_t cd_pos = eocd - cd_size;
    const int64_t shift = int64_t(cd_pos) - int64_t(cd_offset);
    r.seek(size_t(cd_pos));
    for (uint16_t i = 0; i < count; ++i) {
        if (r.u32() != 0x02014b50) throw FormatError("zip: bad central directory entry");
        r.skip(4); // versions
        const uint16_t flags = r.u16();
        ZipEntry e;
        e.method = r.u16();
        r.skip(4); // time, date
        e.crc32 = r.u32();
        e.compressed_size = r.u32();
        e.size = r.u32();
        const uint16_t name_len = r.u16(), extra_len = r.u16(), comment_len = r.u16();
        r.skip(8); // disk, internal and external attributes
        e.header_offset = uint64_t(int64_t(r.u32()) + shift);
        const auto name = r.bytes(name_len);
        e.name.assign(name.begin(), name.end());
        r.skip(size_t(extra_len) + comment_len);
        if (flags & 1) throw FormatError("zip: encrypted entry " + e.name);
        e.directory = !e.name.empty() && (e.name.back() == '/' || e.name.back() == '\\');
        entries_.push_back(std::move(e));
    }
}

std::vector<uint8_t> ZipArchive::extract(const ZipEntry& e) const {
    ByteReader r(data_);
    r.seek(size_t(e.header_offset));
    if (r.u32() != 0x04034b50) throw FormatError("zip: bad local header for " + e.name);
    r.skip(22);
    const uint16_t name_len = r.u16(), extra_len = r.u16();
    r.skip(size_t(name_len) + extra_len);
    const auto packed = r.bytes(e.compressed_size);
    std::vector<uint8_t> out;
    if (e.method == 0) {
        out.assign(packed.begin(), packed.end());
    } else if (e.method == 8) {
        out = inflate_raw(packed, e.size);
    } else {
        throw FormatError("zip: unsupported method for " + e.name);
    }
    if (out.size() != e.size || crc32(out) != e.crc32) throw FormatError("zip: CRC mismatch in " + e.name);
    return out;
}

} // namespace d2k::formats
