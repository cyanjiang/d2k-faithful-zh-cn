#include "formats/byte_reader.hpp"

#include <algorithm>

namespace d2k::formats {

void ByteReader::need(size_t n) const {
    if (n > remaining()) {
        throw FormatError("read of " + std::to_string(n) + " bytes at offset " +
                          std::to_string(pos_) + " overruns buffer of " +
                          std::to_string(data_.size()));
    }
}

void ByteReader::seek(size_t pos) {
    if (pos > data_.size()) {
        throw FormatError("seek to " + std::to_string(pos) + " past end " +
                          std::to_string(data_.size()));
    }
    pos_ = pos;
}

uint8_t ByteReader::u8() {
    need(1);
    return data_[pos_++];
}

uint16_t ByteReader::u16() {
    need(2);
    uint16_t v = static_cast<uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
    pos_ += 2;
    return v;
}

uint32_t ByteReader::u32() {
    need(4);
    uint32_t v = uint32_t(data_[pos_]) | uint32_t(data_[pos_ + 1]) << 8 |
                 uint32_t(data_[pos_ + 2]) << 16 | uint32_t(data_[pos_ + 3]) << 24;
    pos_ += 4;
    return v;
}

std::span<const uint8_t> ByteReader::bytes(size_t n) {
    need(n);
    auto s = data_.subspan(pos_, n);
    pos_ += n;
    return s;
}

std::string ByteReader::fixed_string(size_t n) {
    auto s = bytes(n);
    auto end = std::find(s.begin(), s.end(), uint8_t{0});
    return std::string(s.begin(), end);
}

} // namespace d2k::formats
