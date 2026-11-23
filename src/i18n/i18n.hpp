#pragma once
// Optional localisation for d2k-faithful. Entirely off by default: with no
// language pack installed (lang = "en" or a load failure) every code path is
// byte-for-byte the original English behaviour.

#include <filesystem>
#include <string>
#include <vector>

#include "i18n/cjk_font.hpp"
#include "i18n/translation.hpp"

namespace d2k::i18n {

class I18n {
public:
    static I18n& instance();

    // Looks for <dir>/i18n/<lang>/strings.tsv and font.ckf in each directory.
    // "en" / "" disables localisation. Missing files are not fatal: status()
    // explains what happened and the game stays in English.
    bool init(const std::vector<std::filesystem::path>& dirs, const std::string& lang);
    void reset();

    bool enabled() const { return enabled_; }
    const std::string& lang() const { return lang_; }
    const std::string& status() const { return status_; }
    const CjkFont* cjk() const { return enabled_ ? &cjk_ : nullptr; }
    const Translation& translation() const { return text_; }

private:
    bool enabled_ = false;
    std::string lang_;
    std::string status_;
    Translation text_;
    CjkFont cjk_;
};

// Translate one already-looked-up string. `key` (the uib key) lets us skip
// resource identifiers such as the mve_* movie names.
std::string localize(const std::string& src, const std::string& key = {});
// nullptr when no language pack is active; surface.cpp then stays on the
// original single-byte font path.
const CjkFont* cjk_font();

} // namespace d2k::i18n
