#pragma once
// The CREDITS menu's scrolling text: CreateCredits (FUN_0043d2a0) and
// CreditsDraw (FUN_0043d520). RE: docs/re/42-credits.md.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "formats/string_table.hpp"

namespace d2k::ui {

class Credits {
public:
    // The text box and spacing (read-only data at 0x4deda8..0x4dedc0).
    static constexpr int kLineHeight = 10, kLineGap = 5, kRow = kLineHeight + kLineGap;
    static constexpr int kLeft = 0, kTop = 50, kRight = 640, kBottom = 350;
    static constexpr int kFade = 30;          // the fade band at each edge
    static constexpr uint64_t kStepMs = 10;   // one pixel per update, at most once per > 10 ms
    static constexpr uint64_t kPauseMs = 2000; // blank pause before the loop restarts

    // 21 empty rows, then the text.uib values from CreditsStart to CreditsEnd
    // (both included; the first case-insensitive key match, entry 0 if missing).
    // An empty value keeps no text: it is skipped but still takes its row.
    Credits(const formats::StringTable& text, uint64_t now_ms);

    struct Line {
        const std::string* text;
        int y;     // glyph top
        int alpha; // 32 = solid; 1..30 inside the fade bands
    };
    // The rows to draw at the current offset (nothing once the end is reached).
    std::vector<Line> visible() const;
    // The scroll step after drawing (FUN_0043d520): +1 px when more than
    // 10 ms passed since the last step; at the end, back to 0 after 2 s.
    void step(uint64_t now_ms);

    size_t rows() const { return rows_.size(); }
    int length() const { return length_; }
    int offset() const { return offset_; }

private:
    std::vector<std::optional<std::string>> rows_;
    int length_ = 0;  // 0x516220: rows x 15 px
    int offset_ = 0;  // 0x51621c
    uint64_t last_step_ = 0, end_time_ = 0;
};

} // namespace d2k::ui
