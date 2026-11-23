#include "formats/pe_image.hpp"

#include <algorithm>

#include "formats/byte_reader.hpp"

namespace d2k::formats {

PeImage::PeImage(std::vector<uint8_t> file) : data_(std::move(file)) {
    ByteReader r(data_);
    r.seek(0x3c);
    uint32_t pe = r.u32();
    r.seek(pe);
    if (r.u32() != 0x00004550) throw FormatError("PE: bad signature");
    r.skip(2); // machine
    uint16_t sections = r.u16();
    r.skip(12);
    uint16_t optional_size = r.u16();
    r.skip(2);
    size_t optional = r.pos();
    r.seek(optional + 28);
    image_base_ = r.u32();
    r.seek(optional + optional_size);
    for (uint16_t i = 0; i < sections; ++i) {
        size_t s = r.pos();
        r.skip(8); // name
        uint32_t virtual_size = r.u32();
        uint32_t va = r.u32();
        uint32_t raw_size = r.u32();
        uint32_t raw_offset = r.u32();
        sections_.push_back({va, std::min(virtual_size, raw_size), raw_offset});
        r.seek(s + 40);
    }
}

std::span<const uint8_t> PeImage::at(uint32_t va, size_t size) const {
    uint32_t rva = va - image_base_;
    for (const auto& s : sections_) {
        if (rva >= s.va && rva + size <= uint64_t(s.va) + s.size) {
            size_t off = s.file_offset + (rva - s.va);
            if (off + size <= data_.size()) return std::span(data_).subspan(off, size);
        }
    }
    throw FormatError("PE: address outside the file's sections");
}

} // namespace d2k::formats
