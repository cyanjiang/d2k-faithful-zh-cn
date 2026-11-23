#include "i18n/translation.hpp"

#include <fstream>

namespace d2k::i18n {
namespace {
std::string unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i + 1] == 'n') {
                out.push_back('\xAC'); // the original's forced line break
                ++i;
                continue;
            }
            if (s[i + 1] == '\\') {
                out.push_back('\\');
                ++i;
                continue;
            }
            // "\\xHH" restores a raw high byte (e.g. cp1252 0x92 in a few
            // lobby names). The TSV stays plain ASCII, but the key becomes the
            // exact byte string stored in text.uib.
            if (s[i + 1] == 'x' && i + 3 < s.size()) {
                auto hex = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                const int hi = hex(s[i + 2]);
                const int lo = hex(s[i + 3]);
                if (hi >= 0 && lo >= 0) {
                    out.push_back(char((hi << 4) | lo));
                    i += 3;
                    continue;
                }
            }
        }
        out.push_back(s[i]);
    }
    return out;
}
std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}
} // namespace

Translation Translation::load(const std::filesystem::path& file) {
    Translation t;
    std::ifstream in(file, std::ios::binary);
    if (!in) return t;
    std::string line;
    bool first_line = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first_line) { // editors may save the TSV with a UTF-8 BOM
            first_line = false;
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }
        }
        if (line.empty() || line[0] == '#') continue;
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        // Both columns use the same escaping convention: "\\n" stands for the
        // original game's 0xAC forced line break. text.uib stores that byte
        // literally, while UTF-8 would turn it into two bytes (C2 AC), so the
        // left column must be unescaped back to a single 0xAC to ever match.
        const std::string key = unescape(std::string_view(line).substr(0, tab));
        const std::string val = unescape(std::string_view(line).substr(tab + 1));
        if (key.empty() || val.empty()) continue; // empty value = keep English
        t.exact_.emplace(key, val);
        t.trimmed_.emplace(std::string(trim(key)), val);
    }
    return t;
}

std::string_view Translation::lookup(std::string_view src) const {
    if (const auto it = exact_.find(std::string(src)); it != exact_.end()) return it->second;
    if (const auto it = trimmed_.find(std::string(trim(src))); it != trimmed_.end()) return it->second;
    return src;
}

} // namespace d2k::i18n
