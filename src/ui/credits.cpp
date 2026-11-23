#include "ui/credits.hpp"

#include <algorithm>
#include <cstdlib>

namespace d2k::ui {

namespace {

// FUN_0048dc10: the index of the first key that matches ignoring case, 0 if none.
size_t key_index(const formats::StringTable& text, const std::string& key) {
    const auto& entries = text.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& k = entries[i].first;
        if (k.size() != key.size()) continue;
        bool same = true;
        for (size_t j = 0; j < k.size() && same; ++j) {
            auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; };
            same = lower(k[j]) == lower(key[j]);
        }
        if (same) return i;
    }
    return 0;
}

} // namespace

Credits::Credits(const formats::StringTable& text, uint64_t now_ms) {
    // FUN_0043d2a0: (box height / row) + 1 empty rows, so the text enters at the bottom.
    const int blank = (kBottom - kTop) / kRow + 1;
    rows_.assign(size_t(blank), std::optional<std::string>{});
    const size_t start = key_index(text, "CreditsStart"), end = key_index(text, "CreditsEnd");
    const auto& entries = text.entries();
    for (size_t i = start; i <= end && i < entries.size(); ++i) {
        const auto& v = entries[i].second;
        rows_.push_back(v.empty() ? std::optional<std::string>{} : std::optional<std::string>{v});
    }
    length_ = kRow * int(rows_.size());
    last_step_ = now_ms; // 0x516234 = GetTickCount()
}

std::vector<Credits::Line> Credits::visible() const {
    std::vector<Line> out;
    if (offset_ >= length_) return out;
    const int first = offset_ / kRow;
    const int last = std::min(int(rows_.size()) - 1, first + (kBottom - kTop) / kRow);
    int y = kTop - offset_ % kRow;
    for (int r = first; r <= last; ++r, y += kRow) {
        const auto& t = rows_[size_t(r)];
        if (!t || y <= kTop || y >= kBottom) continue;
        int alpha = 32;
        int d = -1;
        if (y < kTop + kFade) d = std::abs(y - kTop);
        else if (y > kBottom - kFade) d = std::abs(kBottom - y);
        if (d >= 0) alpha = std::min(d, kFade) * 32 / kFade;
        out.push_back({&*t, y, alpha});
    }
    return out;
}

void Credits::step(uint64_t now_ms) {
    if (offset_ < length_) {
        const uint64_t dt = now_ms > last_step_ ? now_ms - last_step_ : last_step_ - now_ms;
        if (dt > kStepMs) {
            last_step_ = now_ms;
            ++offset_;
        }
        if (offset_ >= length_) {
            end_time_ = now_ms;
        }
        return;
    }
    const uint64_t dt = now_ms > end_time_ ? now_ms - end_time_ : end_time_ - now_ms;
    if (dt > kPauseMs) {
        offset_ = 0;
    }
}

} // namespace d2k::ui
