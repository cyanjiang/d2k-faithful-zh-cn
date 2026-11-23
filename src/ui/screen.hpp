#pragma once
// A menu screen parsed from a UIL script. The top-level parse mirrors the
// original dispatcher (1.06 FUN_004390e0): each line is prefix-matched
// (case-sensitive) against the keywords in a fixed order; lines matching
// no keyword are skipped, which makes "headerless" leftover blocks inert.
// RE notes: docs/re/03-ui-scripts.md

#include <optional>
#include <string>
#include <vector>

namespace d2k::ui {

enum class ElementType {
    Background, ToggleButton, Button, GroupBox, ListBox, CheckBox, MultiStateBox,
    EditBox, TextBox, ValueBar, Animation, Movie,
};

enum class TextAlign { Left = 0, Right = 1, Centre = 2 };

struct Element {
    ElementType type{};
    std::string header;             // full header line, e.g. "BUTTON exit 0"
    std::string name;               // second token of the header
    std::vector<std::string> body;  // lines up to (excluding) the END_ line, comments stripped
    TextAlign align = TextAlign::Centre; // TEXTBOX variants
    bool underline = false;              // TEXTBOX_*_UL
    // Set on TEXTBOX lines produced by split_textboxes(): the text itself,
    // instead of the text key in the body.
    std::optional<std::string> literal_text;
};

struct Screen {
    std::vector<Element> elements;
};

Screen parse_screen(const std::vector<std::string>& uil_lines);

// Helpers for element bodies.
struct Point { int x = 0, y = 0; };
Point parse_pair(const std::string& line); // "12, 364" or "220 150"
std::string strip_comment(const std::string& line);

} // namespace d2k::ui
