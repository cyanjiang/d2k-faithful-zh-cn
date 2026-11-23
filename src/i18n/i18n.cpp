#include "i18n/i18n.hpp"

#include <algorithm>
#include <cctype>

namespace d2k::i18n {

I18n& I18n::instance() {
    static I18n i;
    return i;
}

void I18n::reset() {
    enabled_ = false;
    lang_.clear();
    status_.clear();
    text_ = Translation();
    cjk_ = CjkFont();
}

namespace {
// A released build keeps the pack next to the exe; a source build has it at
// <repo>/i18n while the exe lives in <repo>/build/bin. Walk up a few levels
// from every root so both layouts work without an extra command-line flag.
bool find_pack(std::filesystem::path start, const std::string& lang,
               std::filesystem::path& out) {
    std::error_code ec;
    const auto abs = std::filesystem::absolute(start, ec);
    if (!ec) start = abs;
    for (int up = 0; up <= 3; ++up) {
        const auto candidate = start / "i18n" / lang;
        std::error_code e2;
        if (std::filesystem::is_regular_file(candidate / "strings.tsv", e2)) {
            out = candidate;
            return true;
        }
        const auto parent = start.parent_path();
        if (parent.empty() || parent == start) break;
        start = parent;
    }
    return false;
}
} // namespace

bool I18n::init(const std::vector<std::filesystem::path>& dirs, const std::string& lang) {
    reset();
    lang_ = lang;
    if (lang.empty() || lang == "en" || lang == "en-us") {
        status_ = "localisation disabled (English)";
        return false;
    }
    std::vector<std::filesystem::path> search = dirs;
    std::error_code cwd_ec;
    const auto cwd = std::filesystem::current_path(cwd_ec);
    if (!cwd_ec) search.push_back(cwd);
    std::filesystem::path pack;
    for (const auto& dir : search) {
        if (find_pack(dir, lang, pack)) break;
    }
    if (pack.empty()) {
        status_ = "no i18n/" + lang + "/strings.tsv found (searched " +
                  std::to_string(search.size()) + " roots, up to 3 parents each); staying in English";
        return false;
    }
    text_ = Translation::load(pack / "strings.tsv");
    if (!text_.valid()) {
        status_ = "i18n/" + lang + "/strings.tsv has no translations; staying in English";
        return false;
    }
    const auto font = pack / "font.ckf";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(font, ec)) {
        status_ = "missing " + font.string() + "; staying in English";
        return false;
    }
    try {
        cjk_ = CjkFont::load(font);
    } catch (const std::exception& e) {
        status_ = std::string("bad font.ckf: ") + e.what() + "; staying in English";
        return false;
    }
    if (!cjk_.valid()) {
        status_ = "empty font.ckf; staying in English";
        return false;
    }
    enabled_ = true;
    status_ = "i18n/" + lang + " [" + pack.string() + "]: " +
              std::to_string(text_.size()) + " strings, " +
              std::to_string(cjk_.size()) + " glyphs";
    return true;
}

namespace {
bool starts_with_movie_key(const std::string& key) {
    if (key.size() < 4) return false;
    const auto p = key.substr(0, 4);
    return (p[0] == 'm' || p[0] == 'M') && (p[1] == 'v' || p[1] == 'V') &&
           (p[2] == 'e' || p[2] == 'E') && p[3] == '_';
}
// Movie base names and UI_GFX names (g_int1_e, but1_up, ...) are data, not
// display text. Underscored all-lowercase tokens without spaces are skipped
// even if someone puts them in the translation file by accident.
bool looks_like_resource(const std::string& s) {
    if (s.empty() || s.size() > 48) return false;
    bool underscore = false;
    for (const char c : s) {
        if (c == '_') { underscore = true; continue; }
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-') continue;
        return false; // spaces, punctuation, non-ASCII: display text
    }
    return underscore;
}
} // namespace

std::string localize(const std::string& src, const std::string& key) {
    auto& i = I18n::instance();
    if (!i.enabled() || src.empty()) return src;
    if (starts_with_movie_key(key) || looks_like_resource(src)) return src;
    return std::string(i.translation().lookup(src));
}

const CjkFont* cjk_font() { return I18n::instance().cjk(); }

} // namespace d2k::i18n
