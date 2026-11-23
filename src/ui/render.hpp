#pragma once
// Draws a parsed menu screen onto a Surface.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ui/assets.hpp"
#include "ui/screen.hpp"
#include "ui/surface.hpp"

namespace d2k::ui {

// Runtime state of an element, set by its commands.
struct ElementState {
    bool checked = false;            // CHECKBOX (+0x794)
    std::string text = "NoText";     // CHECKBOX text key (EnterText; default at 0x4e8c30)
    // VALUEBAR (CValueBar, docs/re/20 section 3): min +0x7a4, max +0x7a8,
    // step +0x7ac, value +0x7b0.
    int vmin = 0, vmax = 0, step = 1, value = 0;
    // While dragged between the ends the thumb sits at the mouse (+0x7bc,
    // FUN_0048c9c0), not at the value; -1 = from the value.
    int drag_thumb_x = -1;
    // TEXTBOX: a text set by code (+0x790), shown as is.
    std::optional<std::string> literal;
    bool enter_default = false;  // EnterActions: Enter runs this button
    // EDITBOX (CEditBox, FUN_0048b190): text +0x77c, cursor +0xa9c, focus +0xaac.
    std::string edit_text;
    int edit_cursor = 0;
    bool edit_focus = false;
    int max_chars = 0;           // MaxCharacters (init); 0 = only the box width limits
    // Edit box character classes refused (element +0xab0, FUN_00484000
    // cases 0xC1..0xC4): 1 NoSpaces, 2 NoNumbers, 4 NoLetters, 8 NoOtherChars.
    uint8_t refused_chars = 0;
    // FUN_0043cb70's test (0x43cc9b): only 0x20..0x7F (a signed byte), and
    // not a refused class: space bit 1, digits bit 2, letters bit 4, the rest
    // bit 8.
    static bool char_allowed(unsigned char c, uint8_t refused) {
        if (c < 0x20 || c >= 0x80) return false;
        const uint8_t bit = c == ' '                                              ? 1
                            : (c >= '0' && c <= '9')                              ? 2
                            : ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ? 4
                                                                                  : 8;
        return (refused & bit) == 0;
    }
    // LISTBOX (CListBox, docs/re/27 section 3): items +0x790 (with the NULL
    // placeholder item 0 on an empty-slot list), white rows +0x798,
    // selection +0x7E8 (1-based, 0 none), first visible +0x79C.
    std::vector<std::string> items;
    std::vector<bool> item_white;
    std::vector<int> item_data;
    // The network pages' rows (FUN_004885f0, docs/re/58 section 2.3): an
    // icon at x + 2 with the text at x + icon width + 4, the row's colour
    // (a colour name instead of the font colour), a text drawn after the
    // item in the same colour, and a mark icon after that. Empty or
    // parallel to `items`.
    struct RowExtra {
        std::string icon, colour, suffix, mark;
    };
    std::vector<RowExtra> item_extra;
    bool no_highlight = false;   // NoHighlight (init): the selection is not drawn
    int selection = 0;
    int first = 0;
    bool empty_slot = false;     // LISTBOXEMPTYSLOT
    int arrow_pressed = 0;       // 1 up, 2 down (shown pressed)
    bool dragging = false;       // +0x7E4
    int grab = 0;                // mouse y - thumb top
};

// The value bar's thumb rectangle (width = bar width / 5).
Rect valuebar_thumb(const Rect& bar, const ElementState& s);
// A click beside the thumb (dir -1 left, +1 right): true when the value changed.
bool valuebar_step(ElementState& s, int dir);
// A drag with the thumb's left edge at x: true when the bar must be redrawn.
bool valuebar_drag(const Rect& bar, ElementState& s, int x);
// FUN_0048c9c0's value for a thumb `pos` pixels into a `travel`-pixel track
// (0 < pos < travel), before the clamps (docs/re/20 section 3).
int valuebar_drag_value(int pos, int travel, int vmin, int vmax, int step);

// List box geometry (FUN_00486440 / FUN_0040b7c0, docs/re/27 section 3.2).
struct ListGeometry {
    int row_h = 14, visible = 1;
    bool bar = false, active = false; // a scroll bar / it can scroll
    Rect rows, up, down, track, thumb;
};
ListGeometry list_geometry(const Rect& box, const ElementState& s, const formats::Font& font);

// FUN_00486f10: a text added to a list box (EnterText) becomes rows
// narrower than `limit` (the box width - 20), split at spaces and at 0xAC
// (forced break), each row trimmed of spaces and 0xAC at both ends.
std::vector<std::string> wrap_list_text(const formats::Font& font, const std::string& text, int limit);

struct RenderState {
    // Current frame per element index (ANIMATION); null or missing -> frame 0.
    const std::vector<int>* anim_frames = nullptr;
    // Per element index; null or missing -> defaults.
    const std::vector<ElementState>* states = nullptr;
    int hovered = -1;      // element index under the mouse
    int pressed = -1;      // element index held down
};

// Whole menu: background, then every element in draw_order().
void render_screen(const Screen& screen, Assets& assets, Surface& out, const RenderState& state);

// The pieces, for drawing only what changed (as the exe does):
// the background image (or `override_image`, a modified copy of it) ...
void render_background(const Screen& screen, Assets& assets, Surface& out,
                       const Surface* override_image);
// ... one element ...
void render_element(const Screen& screen, size_t index, Assets& assets, Surface& out,
                    const RenderState& state);
// ... and the exe's drawing order (by element class, FUN_0043c4b0).
std::vector<size_t> draw_order(const Screen& screen);

// FUN_004384a0 (AddTextBox): a text at least as wide as its box is word-
// wrapped greedily at spaces (0xAC forces a break), and each extra line
// becomes another TEXTBOX one box height lower. Call after parsing.
std::vector<std::string> split_textbox_text(const formats::Font& font, const std::string& text,
                                            int width);
void split_textboxes(Screen& screen, Assets& assets);

// Buttons, group boxes and checkboxes take the size of their image when
// they have one (their constructors set the element size on each image
// load). Rewrites the size line. Call after parsing.
void apply_image_sizes(Screen& screen, Assets& assets);

// Screen-space rectangle of an element (pos + size lines), if it has one.
std::optional<Rect> element_rect(const Element& e);

} // namespace d2k::ui
