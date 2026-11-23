#include "formats/string_table.hpp"

#include "formats/byte_reader.hpp"

namespace d2k::formats {

namespace {

// _strcmpi as in the exe's lookup (FUN_0048dc10): ASCII, case-insensitive.
bool same_key(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; };
    for (size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

// u16 length (including the NUL terminator), then the bytes.
std::string read_counted(ByteReader& r) {
    uint16_t len = r.u16();
    return r.fixed_string(len);
}

} // namespace

StringTable StringTable::parse(std::span<const uint8_t> data) {
    ByteReader r(data);
    uint32_t count = r.u32();
    StringTable t;
    for (uint32_t i = 0; i < count; ++i) {
        std::string key = read_counted(r);
        std::string value = read_counted(r);
        t.entries_.emplace_back(std::move(key), std::move(value));
    }
    if (r.remaining() != 0) throw FormatError("string table: trailing bytes");
    return t;
}

std::optional<std::string> StringTable::get(const std::string& key) const {
    for (const auto& [k, v] : entries_) {
        if (same_key(k, key)) return v;
    }
    return std::nullopt;
}

std::vector<std::string> StringTable::get_all(const std::string& key) const {
    std::vector<std::string> out;
    for (const auto& [k, v] : entries_) {
        if (same_key(k, key)) out.push_back(v);
    }
    return out;
}

std::vector<std::string> parse_uil_lines(std::span<const uint8_t> data) {
    ByteReader r(data);
    uint32_t count = r.u32();
    std::vector<std::string> lines;
    lines.reserve(count);
    for (uint32_t i = 0; i < count; ++i) lines.push_back(read_counted(r));
    if (r.remaining() != 0) throw FormatError("UIL: trailing bytes");
    return lines;
}

} // namespace d2k::formats
