#pragma once
// Bounds-checked little-endian reader over an in-memory buffer.
// All original Dune 2000 data is little-endian (x86, 1998).

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace d2k::formats {

class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> data) : data_(data) {}

    size_t pos() const { return pos_; }
    size_t size() const { return data_.size(); }
    size_t remaining() const { return data_.size() - pos_; }
    void seek(size_t pos);
    void skip(size_t n) { seek(pos_ + n); }

    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    int16_t i16() { return static_cast<int16_t>(u16()); }
    int32_t i32() { return static_cast<int32_t>(u32()); }

    std::span<const uint8_t> bytes(size_t n);
    // Fixed-size field; stops at the first NUL.
    std::string fixed_string(size_t n);

private:
    void need(size_t n) const;

    std::span<const uint8_t> data_;
    size_t pos_ = 0;
};

} // namespace d2k::formats
