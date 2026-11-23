#pragma once
// UTF-8 helpers for the optional localisation layer.
//
// The 1.06 string tables are single-byte Windows-1252, and 0xAC is the
// original's forced line-break mark. A decoder that simply rejected invalid
// UTF-8 would turn those bytes into replacement characters and break vanilla
// text. So decode() returns `multibyte=false` for any byte that is not part of
// a well-formed UTF-8 sequence, and callers keep feeding it to the original
// Font::glyph(unsigned char) unchanged. English-only play is bit-identical.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace d2k::i18n::utf8 {

struct Decoded {
    uint32_t cp = 0;
    bool multibyte = false;
};

// Reads one character at `pos` and advances it. Never advances past the end.
// Invalid / cp1252 bytes are returned as themselves with multibyte=false.
Decoded decode(std::string_view s, std::size_t& pos);

// Byte index just after the character starting at `pos`.
std::size_t next_boundary(std::string_view s, std::size_t pos);
// Byte index of the character containing (or immediately before) `pos`.
std::size_t prev_boundary(std::string_view s, std::size_t pos);
// True when `pos` starts a character, not the middle of a UTF-8 sequence.
bool is_char_start(std::string_view s, std::size_t pos);
// Longest prefix of `s` no longer than `max_bytes` that ends on a boundary.
std::string truncate(const std::string& s, std::size_t max_bytes);

} // namespace d2k::i18n::utf8
