#pragma once
// Little-endian message writing and bounds-checked reading for the
// network messages (lockstep and lobby).

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::net {

struct Writer {
    std::vector<uint8_t> v;
    Writer& u8(uint32_t x) {
        v.push_back(uint8_t(x));
        return *this;
    }
    Writer& u16(uint32_t x) {
        v.push_back(uint8_t(x));
        v.push_back(uint8_t(x >> 8));
        return *this;
    }
    Writer& u32(uint32_t x) {
        for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i)));
        return *this;
    }
    Writer& str(const std::string& s) { // u8 length, at most 255 bytes
        const size_t n = s.size() < 255 ? s.size() : 255;
        u8(uint32_t(n));
        v.insert(v.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
        return *this;
    }
    Writer& bytes(std::span<const uint8_t> b) {
        v.insert(v.end(), b.begin(), b.end());
        return *this;
    }
};

// `ok` turns false on a short message; reads past the end give 0 / "".
struct Reader {
    std::span<const uint8_t> d;
    size_t p = 0;
    bool ok = true;
    uint32_t get(int n) {
        if (p + size_t(n) > d.size()) {
            ok = false;
            return 0;
        }
        uint32_t x = 0;
        for (int i = 0; i < n; ++i) x |= uint32_t(d[p + size_t(i)]) << (8 * i);
        p += size_t(n);
        return x;
    }
    uint32_t u8() { return get(1); }
    uint32_t u16() { return get(2); }
    uint32_t u32() { return get(4); }
    std::string str() {
        const size_t n = get(1);
        if (p + n > d.size()) {
            ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(d.data() + p), n);
        p += n;
        return s;
    }
};

} // namespace d2k::net
