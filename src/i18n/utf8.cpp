#include "i18n/utf8.hpp"

namespace d2k::i18n::utf8 {
namespace {
inline uint8_t byte(std::string_view s, std::size_t i) { return static_cast<uint8_t>(s[i]); }
inline bool cont(uint8_t c) { return c >= 0x80 && c <= 0xBF; }
} // namespace

Decoded decode(std::string_view s, std::size_t& pos) {
    if (pos >= s.size()) return {};
    const uint8_t c0 = byte(s, pos);
    if (c0 < 0x80) {
        ++pos;
        return {c0, false};
    }
    // Two-byte: 0xC2..0xDF (0xC0/0xC1 would be overlong).
    if (c0 >= 0xC2 && c0 <= 0xDF && pos + 1 < s.size() && cont(byte(s, pos + 1))) {
        const uint32_t cp = (uint32_t(c0 & 0x1F) << 6) | uint32_t(byte(s, pos + 1) & 0x3F);
        pos += 2;
        return {cp, true};
    }
    // Three-byte: reject overlong (E0 80..9F) and surrogates (ED A0..BF).
    if (c0 >= 0xE0 && c0 <= 0xEF && pos + 2 < s.size() && cont(byte(s, pos + 1)) && cont(byte(s, pos + 2))) {
        const uint8_t c1 = byte(s, pos + 1);
        if (!(c0 == 0xE0 && c1 < 0xA0) && !(c0 == 0xED && c1 >= 0xA0)) {
            const uint32_t cp = (uint32_t(c0 & 0x0F) << 12) | (uint32_t(c1 & 0x3F) << 6) |
                                uint32_t(byte(s, pos + 2) & 0x3F);
            pos += 3;
            return {cp, true};
        }
    }
    // Four-byte: 0xF0..0xF4, reject overlong and > U+10FFFF.
    if (c0 >= 0xF0 && c0 <= 0xF4 && pos + 3 < s.size() && cont(byte(s, pos + 1)) &&
        cont(byte(s, pos + 2)) && cont(byte(s, pos + 3))) {
        const uint8_t c1 = byte(s, pos + 1);
        if (!(c0 == 0xF0 && c1 < 0x90) && !(c0 == 0xF4 && c1 >= 0x90)) {
            const uint32_t cp = (uint32_t(c0 & 0x07) << 18) | (uint32_t(c1 & 0x3F) << 12) |
                                (uint32_t(byte(s, pos + 2) & 0x3F) << 6) | uint32_t(byte(s, pos + 3) & 0x3F);
            pos += 4;
            return {cp, true};
        }
    }
    // cp1252 byte (accented letters, the 0xAC line break, etc.): keep the
    // original single-byte path so vanilla text renders exactly as before.
    ++pos;
    return {c0, false};
}

std::size_t next_boundary(std::string_view s, std::size_t pos) {
    if (pos >= s.size()) return s.size();
    std::size_t q = pos;
    decode(s, q);
    return q;
}

std::size_t prev_boundary(std::string_view s, std::size_t pos) {
    // Largest character start strictly before `pos`. When `pos` sits inside a
    // multi-byte sequence this is the start of the character containing it.
    if (pos == 0) return 0;
    std::size_t result = 0, p = 0;
    while (p < pos) {
        result = p;
        std::size_t q = p;
        decode(s, q);
        p = q;
    }
    return result;
}

bool is_char_start(std::string_view s, std::size_t pos) {
    if (pos == 0 || pos >= s.size()) return true;
    std::size_t p = 0;
    while (p < pos) {
        std::size_t q = p;
        decode(s, q);
        p = q;
    }
    return p == pos;
}

std::string truncate(const std::string& s, std::size_t max_bytes) {
    if (s.size() <= max_bytes) return s;
    std::size_t p = 0;
    while (p < max_bytes) {
        const std::size_t q = next_boundary(s, p);
        if (q > max_bytes) break;
        p = q;
    }
    return s.substr(0, p);
}

} // namespace d2k::i18n::utf8
