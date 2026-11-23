#include "ui/screen.hpp"

#include <cstdio>
#include <cstring>

namespace d2k::ui {

namespace {

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool starts(const std::string& line, const char* kw) {
    return std::strncmp(line.c_str(), kw, std::strlen(kw)) == 0;
}

struct Keyword {
    const char* text;
    ElementType type;
    const char* end; // terminator line prefix
    TextAlign align = TextAlign::Centre;
    bool underline = false;
};

// Order matters: identical to the exe's strncmp chain.
const Keyword kKeywords[] = {
    {"BACKGROUND", ElementType::Background, "END_BACKGROUND"},
    {"TOGGLEBUTTON", ElementType::ToggleButton, "END_TOGGLEBUTTON"},
    {"BUTTON", ElementType::Button, "END_BUTTON"},
    {"GROUPBOX", ElementType::GroupBox, "END_GROUPBOX"},
    {"LISTBOX_MULTI", ElementType::ListBox, "END_LISTBOX"},
    {"LISTBOXEMPTYSLOT", ElementType::ListBox, "END_LISTBOX"},
    {"LISTBOX_CROPPED", ElementType::ListBox, "END_LISTBOX"},
    {"LISTBOX", ElementType::ListBox, "END_LISTBOX"},
    {"CHECKBOX", ElementType::CheckBox, "END_CHECKBOX"},
    {"MULTISTATEBOX", ElementType::MultiStateBox, "END_MULTISTATEBOX"},
    {"EDITBOXFOCUS", ElementType::EditBox, "END_EDITBOX"},
    {"EDITBOX", ElementType::EditBox, "END_EDITBOX"},
    {"TEXTBOX_L_UL", ElementType::TextBox, "END_TEXTBOX", TextAlign::Left, true},
    {"TEXTBOX_R_UL", ElementType::TextBox, "END_TEXTBOX", TextAlign::Right, true},
    {"TEXTBOX_UL", ElementType::TextBox, "END_TEXTBOX", TextAlign::Centre, true},
    {"TEXTBOX_L", ElementType::TextBox, "END_TEXTBOX", TextAlign::Left},
    {"TEXTBOX_R", ElementType::TextBox, "END_TEXTBOX", TextAlign::Right},
    {"TEXTBOX", ElementType::TextBox, "END_TEXTBOX"}, // also TEXTBOX_C
    {"VALUEBAR_NOVAL", ElementType::ValueBar, "END_VALUEBAR"},
    {"VALUEBAR", ElementType::ValueBar, "END_VALUEBAR"},
    {"ANIMATION", ElementType::Animation, "END_ANIMATION"},
    {"MOVIE", ElementType::Movie, "END_MOVIE"},
};

} // namespace

std::string strip_comment(const std::string& line) {
    auto c = line.find("//");
    return trim(c == std::string::npos ? line : line.substr(0, c));
}

Point parse_pair(const std::string& line) {
    Point p;
    std::sscanf(line.c_str(), "%d%*[ ,\t]%d", &p.x, &p.y);
    return p;
}

Screen parse_screen(const std::vector<std::string>& lines) {
    Screen s;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        const Keyword* kw = nullptr;
        for (const auto& k : kKeywords) {
            if (starts(line, k.text)) {
                kw = &k;
                break;
            }
        }
        if (!kw) continue; // unknown lines are ignored by the original too
        Element e;
        e.type = kw->type;
        e.header = strip_comment(line);
        e.align = kw->align;
        e.underline = kw->underline;
        if (auto sp = e.header.find(' '); sp != std::string::npos) {
            std::string rest = e.header.substr(sp + 1);
            e.name = rest.substr(0, rest.find(' '));
        }
        // Each element parser reads a fixed number of lines, then hands every
        // line up to its END_ line to the command parser; we collect them all.
        // Same result: every 1.06 block has at least the fixed lines (e.g.
        // BUTTON 11, LISTBOX 16, CHECKBOX 7), so no END_ is taken as a field.
        for (++i; i < lines.size() && !starts(lines[i], kw->end); ++i) {
            e.body.push_back(strip_comment(lines[i]));
        }
        s.elements.push_back(std::move(e));
    }
    return s;
}

} // namespace d2k::ui
