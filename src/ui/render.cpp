#include "ui/render.hpp"

#include "i18n/i18n.hpp"
#include "i18n/utf8.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace d2k::ui {

namespace {

// Field layouts below come from the 1.06 UIL data (see docs/formats/ui.md);
// each element follows its draw function in the exe (named per case). All
// UI text is font 0 (FONT.FNT, element +0x775 is only ever 0) in one solid
// colour (FUN_0042dc00 -> FUN_00431e40), centred vertically with
// (h - glyph height) / 2 (the slot 0 glyph's height, 14).

const std::string& field(const Element& e, size_t i) {
    static const std::string empty;
    return i < e.body.size() ? e.body[i] : empty;
}

std::optional<uint16_t> colour(Assets& a, const std::string& name) {
    if (name == "Transparent") return std::nullopt;
    auto c = a.colour(name);
    return c ? std::optional<uint16_t>(to_rgb555(*c)) : std::nullopt;
}

// The height the exe centres with: font 0's slot 0 glyph (0x513b28 -> +4).
int glyph_height(const formats::Font& font) {
    return !font.glyphs.empty() && font.glyphs[0].height > 0 ? int(font.glyphs[0].height) : int(font.height);
}

// The text's left edge. Centred: x + (w - text) / 2 (FUN_0042d5d0,
// FUN_00485610). Text boxes (FUN_0048bb10) keep 5 px from the left or right
// edge.
int text_x(const Rect& r, int w, TextAlign align) {
    return align == TextAlign::Left ? r.x + 5 : align == TextAlign::Right ? r.x + r.w - w - 5 : r.x + (r.w - w) / 2;
}

void draw_text_in(Assets& a, Surface& out, const Rect& r, const std::string& text,
                  const std::string& colour_name, TextAlign align, int y_offset = 0) {
    if (text.empty()) return;
    const auto& font = a.menu_font();
    auto c = a.colour(colour_name).value_or(formats::Rgb{255, 255, 255});
    const int x = text_x(r, text_width(font, text), align);
    const int y = r.y + (r.h - glyph_height(font)) / 2 + y_offset;
    out.draw_text_solid(font, x, y, text, to_rgb555(c));
}

// The exe turns a colour name into a colours.uib index with a
// case-sensitive compare (FUN_0048db90); an unknown name (e.g. MAINMENU's
// "grey") gives index 0, "Transparent" = (0, 0, 0).
bool colour_index0(Assets& a, const std::string& name) {
    return name == "Transparent" || !a.colour(name);
}
uint16_t colour_value(Assets& a, const std::string& name) {
    return to_rgb555(a.colour(name).value_or(formats::Rgb{0, 0, 0}));
}

// FUN_0042d660: fill, then left (top-left colour), bottom, right
// (bottom-right colour) and top (top-left colour), in that order.
void box(Surface& out, const Rect& r, uint16_t fill, uint16_t tl, uint16_t br) {
    out.fill(r, fill);
    out.fill({r.x, r.y, 1, r.h}, tl);
    out.fill({r.x, r.y + r.h - 1, r.w, 1}, br);
    out.fill({r.x + r.w - 1, r.y, 1, r.h}, br);
    out.fill({r.x, r.y, r.w, 1}, tl);
}

void draw_label(Assets& a, Surface& out, const Rect& r, const std::string& text_key,
                const std::string& colour_name, TextAlign align, int y_offset = 0) {
    draw_text_in(a, out, r, a.lookup_text(text_key), colour_name, align, y_offset);
}

bool same_name(const std::string& x, const std::string& y) {
    return x.size() == y.size() && std::equal(x.begin(), x.end(), y.begin(), [](char p, char q) {
               return std::toupper(static_cast<unsigned char>(p)) == std::toupper(static_cast<unsigned char>(q));
           });
}

bool has_multibyte(const std::string& s) {
    std::size_t p = 0;
    while (p < s.size()) {
        if (i18n::utf8::decode(s, p).multibyte) return true;
    }
    return false;
}

// CJK paragraphs contain no spaces. The word-based splitters below would see
// a whole briefing paragraph as one over-wide "word" and emit an empty line.
// Break at character boundaries instead, honouring 0xAC forced breaks and
// preferring spaces when they exist. ASCII-only text never takes this path,
// so vanilla wrapping is byte-for-byte unchanged.
std::vector<std::string> split_cjk_lines(const formats::Font& font,
                                         const std::string& text, int width) {
    constexpr char kBreak = '\xAC';
    std::vector<std::string> lines;
    std::string rest = text;
    while (!rest.empty()) {
        std::size_t fit = 0;
        std::size_t last_space = std::string::npos;
        std::size_t forced = std::string::npos;
        std::size_t p = 0;
        while (p < rest.size()) {
            if (i18n::utf8::is_char_start(rest, p)) {
                if (rest[p] == kBreak || rest[p] == '\n') {
                    forced = p;
                    break;
                }
                if (rest[p] == ' ') last_space = p;
            }
            const std::size_t q = i18n::utf8::next_boundary(rest, p);
            if (text_width(font, rest.substr(0, q)) >= width) break;
            p = q;
            fit = q;
        }
        std::size_t cut;
        if (forced != std::string::npos) cut = i18n::utf8::next_boundary(rest, forced);
        else if (p >= rest.size()) cut = rest.size();
        else if (last_space != std::string::npos && last_space > 0)
            cut = i18n::utf8::next_boundary(rest, last_space);
        else cut = fit > 0 ? fit : i18n::utf8::next_boundary(rest, p);
        if (cut == 0) cut = i18n::utf8::next_boundary(rest, 0); // always make progress
        std::string line = rest.substr(0, cut);
        rest.erase(0, cut);
        std::size_t a = 0;
        while (a < line.size() && (line[a] == ' ' || line[a] == kBreak || line[a] == '\n'))
            a = i18n::utf8::next_boundary(line, a);
        line.erase(0, a);
        while (!line.empty()) {
            const std::size_t last = i18n::utf8::prev_boundary(line, line.size());
            if (last >= line.size()) break;
            const char c = line[last];
            if (!(c == ' ' || c == kBreak || c == '\n')) break;
            line.erase(last);
        }
        lines.push_back(line);
    }
    return lines;
}

} // namespace

std::vector<std::string> split_textbox_text(const formats::Font& font, const std::string& text,
                                            int width) {
    if (text_width(font, text) < width) return {text};
    if (has_multibyte(text)) return split_cjk_lines(font, text, width);
    constexpr char kBreak = '\xAC'; // forced line break in text.uib
    // 0xAC is also a legal UTF-8 continuation byte. In localised (UTF-8)
    // text it must only count as a separator when it starts a character.
    auto is_sep = [](const std::string& s, std::size_t i) {
        if (i >= s.size() || !i18n::utf8::is_char_start(s, i)) return false;
        return s[i] == ' ' || s[i] == kBreak || s[i] == '\n';
    };
    auto step = [](const std::string& s, std::size_t i) { return i18n::utf8::next_boundary(s, i); };
    auto back = [](const std::string& s, std::size_t i) { return i18n::utf8::prev_boundary(s, i); };
    std::vector<std::string> lines;
    std::string rest = text;
    size_t accepted = 0; // prefix of `rest` that fits
    for (;;) {
        size_t scan = accepted;
        while (scan < rest.size() && !is_sep(rest, scan)) scan = step(rest, scan);
        bool forced = false;
        if (scan < rest.size() && rest[scan] == ' ') {
            scan = step(rest, scan);
        } else if (scan < rest.size() && rest[scan] == kBreak) {
            forced = true;
            const size_t with_break = step(rest, scan);
            scan = accepted;
            if (text_width(font, rest.substr(0, with_break)) < width) accepted = scan = with_break;
        }
        bool emit = forced || accepted == scan || text_width(font, rest.substr(0, scan)) >= width;
        if (!emit) {
            accepted = scan;
            continue;
        }
        bool last = accepted == scan && !forced;
        std::string line = rest.substr(0, accepted);
        rest.erase(0, accepted);
        size_t b = 0;
        while (b < line.size() && is_sep(line, b)) b = step(line, b);
        line.erase(0, b);
        while (!line.empty()) {
            const size_t last_ch = back(line, line.size());
            if (!is_sep(line, last_ch)) break;
            line.erase(last_ch);
        }
        if (accepted == 0 && !last) {
            // A word wider than the box: the exe would loop. No 1.06 text
            // hits this (test "every TEXTBOX text splits into lines narrower
            // than its box"); we keep the rest on one line.
            lines.push_back(rest);
            break;
        }
        lines.push_back(line);
        accepted = 0;
        if (last) break;
    }
    return lines;
}

void apply_image_sizes(Screen& screen, Assets& a) {
    for (auto& e : screen.elements) {
        if (e.body.size() < 2) continue;
        // Image fields in the order each constructor loads them; every load
        // sets the element's size, so the last image present wins.
        std::vector<size_t> fields;
        switch (e.type) {
        case ElementType::Button: fields = {9, 10}; break;   // FUN_00485000: up, down
        case ElementType::GroupBox: fields = {4}; break;     // FUN_004860e0
        case ElementType::CheckBox: fields = {5, 6}; break;  // FUN_0048ab10: checked, unchecked
        case ElementType::ValueBar: fields = {8}; break;     // the region image sets the size
        default: continue;
        }
        for (size_t f : fields) {
            if (f >= e.body.size()) continue;
            if (const auto* img = a.image(e.body[f])) {
                e.body[1] = std::to_string(img->width) + ", " + std::to_string(img->height);
            }
        }
    }
}

void split_textboxes(Screen& screen, Assets& a) {
    std::vector<Element> out;
    out.reserve(screen.elements.size());
    for (auto& e : screen.elements) {
        auto rect = element_rect(e);
        if (e.type != ElementType::TextBox || !rect || e.body.size() < 7) {
            out.push_back(std::move(e));
            continue;
        }
        std::string text = a.lookup_text(e.body[6]);
        // Font 0: +0x775 is only ever set to 0 (FUN_00482830).
        if (text_width(a.menu_font(), text) < rect->w) {
            out.push_back(std::move(e));
            continue;
        }
        auto lines = split_textbox_text(a.menu_font(), text, rect->w);
        for (size_t k = 0; k < lines.size(); ++k) {
            Element line = e;
            line.literal_text = lines[k];
            line.body[0] = std::to_string(rect->x) + ", " + std::to_string(rect->y + int(k) * rect->h);
            out.push_back(std::move(line));
        }
    }
    screen.elements = std::move(out);
}

std::vector<std::string> wrap_list_text(const formats::Font& font, const std::string& text, int limit) {
    // FUN_00486f10 (docs/re/20 section 2.5). `fit` is the length of the
    // words that fit so far, `pos` the scan position in the remainder.
    std::vector<std::string> rows;
    auto width = [&](const std::string& s) { return text_width(font, s); };
    if (width(text) < limit) { // short: one row, as is (0xAC kept)
        rows.push_back(text);
        return rows;
    }
    if (has_multibyte(text)) return split_cjk_lines(font, text, limit);
    constexpr char kBreak = '\xAC';
    // Scan by UTF-8 character so a localised string is never split mid-glyph,
    // and so a 0xAC continuation byte is not mistaken for a line break.
    auto sep_at = [](const std::string& s, std::size_t i) {
        if (i >= s.size() || !i18n::utf8::is_char_start(s, i)) return false;
        return s[i] == ' ' || s[i] == kBreak || s[i] == '\n';
    };
    auto step = [](const std::string& s, std::size_t i) { return i18n::utf8::next_boundary(s, i); };
    auto back = [](const std::string& s, std::size_t i) { return i18n::utf8::prev_boundary(s, i); };
    auto emit = [&](std::string line) {
        size_t a = 0;
        while (a < line.size() && sep_at(line, a)) a = step(line, a);
        line.erase(0, a);
        while (!line.empty()) {
            const size_t last_ch = back(line, line.size());
            if (!sep_at(line, last_ch)) break;
            line.erase(last_ch);
        }
        rows.push_back(line);
    };
    size_t start = 0, pos = 0, fit = 0;
    bool done = false;
    while (!done) {
        const std::string s = text.substr(start);
        if (s.find(' ') != std::string::npos || width(s) < limit) {
            bool brk = false;
            while (pos < s.size() && !sep_at(s, pos)) pos = step(s, pos);
            if (pos < s.size() && s[pos] == ' ') {
                pos = step(s, pos);
                // The first word of a row that is too wide is cut to fit.
                if (fit == 0) {
                    while (pos > 0 && width(s.substr(0, pos)) > limit) pos = back(s, pos);
                }
            } else if (pos < s.size() && s[pos] == kBreak) {
                brk = true;
                const size_t after = step(s, pos);
                if (width(s.substr(0, after)) < limit) fit = after;
                pos = fit;
            }
            if (brk || width(s.substr(0, pos)) >= limit) {
                if (fit == pos && !brk) done = true;
            } else if (fit == pos) {
                done = true; // the end: nothing more to scan
            } else {
                fit = pos;
                continue;
            }
            if (fit == 0 && !done && !brk) fit = pos; // a first word exactly `limit` wide: the exe would loop forever
            emit(s.substr(0, fit));
            start += fit;
            fit = pos = 0;
        } else {
            // No space left and too wide: the longest prefix that fits.
            size_t n = s.size();
            while (n > 0 && width(s.substr(0, n)) >= limit) n = back(s, n);
            rows.push_back(s.substr(0, n));
            start += n > 0 ? n : 1; // no 1.06 text reaches this (a word wider than a list)
            fit = pos = 0;
        }
    }
    return rows;
}

ListGeometry list_geometry(const Rect& box, const ElementState& s, const formats::Font& font) {
    ListGeometry g;
    // Row height +0x779: font 0's first glyph height (FUN_00482830), 14.
    g.row_h = glyph_height(font);
    g.visible = std::max(1, box.h / g.row_h);
    g.bar = box.h >= 36;
    const int count = int(s.items.size());
    g.active = g.bar && count > g.visible;
    g.rows = {box.x, box.y, g.bar ? box.w - 16 : box.w, box.h};
    if (g.bar) {
        g.up = {box.x + box.w - 16, box.y, 16, 16};
        g.down = {box.x + box.w - 16, box.y + box.h - 16, 16, 16};
        g.track = {box.x + box.w - 16, box.y + 16, 16, box.h - 32};
        int th = count > 0 ? g.track.h * g.visible / count : g.track.h;
        th = std::clamp(th, 5, std::max(5, g.track.h));
        int top = g.track.y + (count > 0 ? s.first * g.track.h / count : 0);
        top = std::min(top, g.track.y + g.track.h - th);
        g.thumb = {g.track.x, top, 16, th};
    }
    return g;
}

int valuebar_drag_value(int pos, int travel, int vmin, int vmax, int step) {
    // The x87 sequence at 0x48cb13 (53-bit precision, each step rounded to
    // double): ((((pos / travel) * 100.0f) * n) * step) * -0.01f, n =
    // (max - min) / step as unsigned integers; then min minus that, and
    // __ftol (truncation). Unsigned: a value % step != 0 is rounded down.
    if (travel <= 0) return vmin;
    const double ratio = double(pos) / double(travel);
    const uint32_t n = step > 0 ? uint32_t(vmax - vmin) / uint32_t(step) : 0;
    double t = ratio * double(100.0f);
    t = t * double(n);
    t = t * double(step);
    t = t * double(-0.01f);
    const double v = double(uint32_t(vmin)) - t;
    uint32_t u = uint32_t(int64_t(v));
    if (step > 0 && u % uint32_t(step) != 0) u = (u / uint32_t(step)) * uint32_t(step);
    return int(u);
}

Rect valuebar_thumb(const Rect& bar, const ElementState& s) {
    // Thumb x = bar x + ((w - w/5) * (value - min)) / (max - min), integer division.
    const int tw = bar.w / 5;
    if (s.drag_thumb_x >= 0) return {s.drag_thumb_x, bar.y, tw, bar.h};
    const int span = s.vmax - s.vmin;
    const int x = bar.x + (span > 0 ? ((bar.w - tw) * (s.value - s.vmin)) / span : 0);
    return {x, bar.y, tw, bar.h};
}

namespace {
// The value clamps of the value-bar code: [min, max], then the copies
// +0x7b4 / +0x7b8 (the same values); unsigned compares.
uint32_t valuebar_clamp(uint32_t v, const ElementState& s) {
    if (v < uint32_t(s.vmin)) v = uint32_t(s.vmin);
    if (uint32_t(s.vmax) < v) v = uint32_t(s.vmax);
    return v;
}
} // namespace

bool valuebar_step(ElementState& s, int dir) {
    // FUN_0048c640 / FUN_0048c450: value -/+ step in unsigned arithmetic,
    // clamped. Quirk: below a step from 0 (min 0) the subtraction wraps and
    // the clamp makes it the maximum.
    const uint32_t step = uint32_t(s.step);
    const uint32_t v = valuebar_clamp(dir < 0 ? uint32_t(s.value) - step : uint32_t(s.value) + step, s);
    if (uint32_t(s.value) == v) return false;
    s.value = int(v);
    s.drag_thumb_x = -1; // the thumb is recomputed from the new value
    return true;
}

bool valuebar_drag(const Rect& bar, ElementState& s, int x) {
    // FUN_0048c9c0 with the thumb's new left edge x (mouse - grab offset).
    const int t = bar.w / 5;
    if (x <= bar.x || x >= bar.x + bar.w - t) {
        // At or past an end: the minimum or the maximum; the thumb is
        // recomputed only when the value changes (else it stays where it was).
        const int v = std::clamp(x <= bar.x ? std::min(s.vmin, s.vmax) : s.vmax, s.vmin, s.vmax);
        if (s.value == v) return false;
        s.value = v;
        s.drag_thumb_x = -1;
        return true;
    }
    // Between: the thumb follows the mouse (0x48cb1d), the value is the x87
    // expression of valuebar_drag_value, clamped; redrawn on every move.
    s.drag_thumb_x = x;
    s.value = std::clamp(valuebar_drag_value(x - bar.x, bar.w - t, s.vmin, s.vmax, s.step), s.vmin, s.vmax);
    return true;
}

std::optional<Rect> element_rect(const Element& e) {
    if (e.type == ElementType::Background || e.body.size() < 2) return std::nullopt;
    Point p = parse_pair(e.body[0]), s = parse_pair(e.body[1]);
    return Rect{p.x, p.y, s.x, s.y};
}

void render_background(const Screen& screen, Assets& a, Surface& out, const Surface* override_image) {
    // FUN_0043c4b0: the menu's background image (+0x54), opaque copy at 0,0.
    if (override_image) {
        for (int y = 0; y < std::min(out.height(), override_image->height()); ++y) {
            std::copy_n(override_image->pixels().begin() + y * override_image->width(),
                        std::min(out.width(), override_image->width()),
                        out.pixels().begin() + y * out.width());
        }
        return;
    }
    for (const auto& e : screen.elements) {
        if (e.type != ElementType::Background) continue;
        if (const auto* img = a.image(e.name)) {
            out.blit(*img, {0, 0, int(img->width), int(img->height)}, 0, 0, BlitMode::Opaque);
        }
        return; // one background per menu
    }
}

std::vector<size_t> draw_order(const Screen& screen) {
    // FUN_0043c4b0 draws by element class, not script order.
    static const ElementType kOrder[] = {
        ElementType::GroupBox, ElementType::Button,        ElementType::ToggleButton,
        ElementType::ListBox,  ElementType::CheckBox,      ElementType::MultiStateBox,
        ElementType::EditBox,  ElementType::TextBox,       ElementType::ValueBar,
        ElementType::Animation, ElementType::Movie};
    std::vector<size_t> out;
    for (ElementType t : kOrder) {
        for (size_t i = 0; i < screen.elements.size(); ++i) {
            if (screen.elements[i].type == t) out.push_back(i);
        }
    }
    return out;
}

void render_screen(const Screen& screen, Assets& a, Surface& out, const RenderState& st) {
    render_background(screen, a, out, nullptr);
    for (size_t i : draw_order(screen)) render_element(screen, i, a, out, st);
}

void render_element(const Screen& screen, size_t i, Assets& a, Surface& out, const RenderState& st) {
    {
        const Element& e = screen.elements[i];
        auto rect = element_rect(e);
        switch (e.type) {
        case ElementType::Animation: {
            if (!rect) break;
            std::istringstream ss(field(e, 2));
            std::string gfx;
            ss >> gfx;
            const auto* img = a.image(gfx);
            if (!img || rect->w <= 0) break;
            int frame = 0;
            if (st.anim_frames && i < st.anim_frames->size()) frame = (*st.anim_frames)[i];
            // FUN_0048d650: restores the saved background, then the frame (1,1).
            out.blit(*img, {frame * rect->w, 0, rect->w, rect->h}, rect->x, rect->y,
                     BlitMode::KeyedShadow);
            break;
        }
        case ElementType::GroupBox: {
            // FUN_004863a0: the image (1,1) at the box position, sized by the
            // image; without one FUN_0042d660 with the fill colour and the
            // border colour on all four sides ("Transparent" fills black).
            if (!rect) break;
            if (const auto* img = a.image(field(e, 4))) {
                out.blit(*img, {0, 0, int(img->width), int(img->height)}, rect->x, rect->y,
                         BlitMode::KeyedShadow);
                break;
            }
            const uint16_t border = colour_value(a, field(e, 3));
            box(out, *rect, colour_value(a, field(e, 2)), border, border);
            break;
        }
        case ElementType::Button: {
            // FUN_00485610: up/down image (1,1) with a centred label, one
            // pixel lower while pressed if the button has commands (+0x774)
            // and the two image names differ (__strcmpi).
            if (!rect) break;
            bool down = st.pressed == int(i) && st.hovered == int(i);
            if (const auto* img = a.image(field(e, down ? 10 : 9))) {
                out.blit(*img, {0, 0, int(img->width), int(img->height)}, rect->x, rect->y,
                         BlitMode::KeyedShadow);
                int dy = down && e.body.size() > 11 && !same_name(field(e, 9), field(e, 10)) ? 1 : 0;
                draw_label(a, out, *rect, field(e, 2), field(e, down ? 8 : 7), TextAlign::Centre, dy);
                break;
            }
            // FUN_0042d5d0: FUN_0042d660 with the up / down colour, highlight
            // top-left and lowlight bottom-right (swapped while pressed), then
            // the centred label.
            const uint16_t hi = colour_value(a, field(e, 3)), lo = colour_value(a, field(e, 4));
            box(out, *rect, colour_value(a, field(e, down ? 6 : 5)), down ? lo : hi, down ? hi : lo);
            draw_label(a, out, *rect, field(e, 2), field(e, down ? 8 : 7), TextAlign::Centre);
            break;
        }
        case ElementType::CheckBox: {
            // FUN_0048adc0. Body: pos, size, border, on, off colours, checked
            // gfx, unchecked gfx. Images are drawn opaque (0,0); without one
            // the box is filled. Text: font 0, centred, border colour when
            // unchecked, on colour when checked.
            if (!rect) break;
            static const ElementState kDefault;
            const ElementState& es = st.states && i < st.states->size() ? (*st.states)[i] : kDefault;
            if (const auto* img = a.image(field(e, es.checked ? 5 : 6))) {
                out.blit(*img, {0, 0, int(img->width), int(img->height)}, rect->x, rect->y,
                         BlitMode::Opaque);
            } else if (es.checked) {
                if (auto fill = colour(a, field(e, 3))) out.fill(*rect, *fill);
                if (auto border = colour(a, field(e, 2))) out.frame(*rect, *border, *border);
            } else if (auto off = colour(a, field(e, 4))) {
                out.fill(*rect, *off);
                out.frame(*rect, *off, *off);
            }
            draw_label(a, out, *rect, es.text, field(e, es.checked ? 3 : 2), TextAlign::Centre);
            break;
        }
        case ElementType::ListBox: {
            // FUN_004885f0 (docs/re/27 section 3.3). Body: pos, size, back,
            // font, frame, highlight colours, 4 button colours, region gfx,
            // slider gfx, up / up pressed / down / down pressed gfx.
            if (!rect) break;
            static const ElementState kDefault;
            const ElementState& es = st.states && i < st.states->size() ? (*st.states)[i] : kDefault;
            const auto& font = a.menu_font();
            const ListGeometry g = list_geometry(*rect, es, font);
            const auto back = colour(a, field(e, 2));
            if (back) out.fill(*rect, *back);
            const auto font_c = a.colour(field(e, 3)).value_or(formats::Rgb{255, 255, 255});
            const auto hi = colour(a, field(e, 5));
            auto highlight = [&](int row) {
                const Rect r{g.rows.x, g.rows.y + row * g.row_h, g.rows.w, g.row_h};
                if (hi) out.fill(r, *hi);
                // FUN_0042d660: a 1-px border in the back colour's RGB; the line
                // functions draw it as is, so Transparent (and an unknown name,
                // colour index 0) gives a black border (RGB 0, 0, 0).
                const uint16_t edge = back ? *back : uint16_t(0);
                out.frame(r, edge, edge);
            };
            int row = 0;
            if (es.empty_slot) {
                if (es.selection == 0) highlight(0);
                out.draw_text_solid(font, g.rows.x + 5, g.rows.y, a.lookup_text("EmptySlot"), to_rgb555(font_c));
                row = 1;
            }
            for (; row < g.visible; ++row) {
                const int item = es.first + row;
                if (item >= int(es.items.size())) break;
                if (es.selection == item + 1 && !es.no_highlight) highlight(row);
                auto c = item < int(es.item_white.size()) && es.item_white[size_t(item)]
                             ? formats::Rgb{255, 255, 255}
                             : font_c;
                const int top = g.rows.y + row * g.row_h;
                int x = g.rows.x + 2;
                const ElementState::RowExtra* ex =
                    item < int(es.item_extra.size()) ? &es.item_extra[size_t(item)] : nullptr;
                // Icons are drawn at the row top (FUN_00430770 (1,1) at
                // local_2e8, the row's y in FUN_004885f0).
                auto icon = [&](const std::string& name, int at) -> int {
                    const auto* img = name.empty() ? nullptr : a.image(name);
                    if (!img) return 0;
                    out.blit(*img, {0, 0, int(img->width), int(img->height)}, at, top, BlitMode::KeyedShadow);
                    return int(img->width);
                };
                if (ex) {
                    if (const int w = icon(ex->icon, x); w > 0) x = g.rows.x + w + 4;
                    if (!ex->colour.empty()) c = a.colour(ex->colour).value_or(c);
                }
                // Localise at draw time: the stored item stays the English
                // source so reverse lookups (map name -> map_N key) still work.
                const std::string shown = i18n::localize(es.items[size_t(item)]);
                // At the row top, no clipping (FUN_004885f0 -> FUN_0042dc00).
                out.draw_text_solid(font, x, top, shown, to_rgb555(c));
                if (ex) {
                    x += text_width(font, shown);
                    if (!ex->suffix.empty()) {
                        out.draw_text_solid(font, x, top, ex->suffix, to_rgb555(c));
                        x += text_width(font, ex->suffix);
                    }
                    icon(ex->mark, x);
                }
            }
            // FUN_0042d140 (frame) hands the frame colour's RGB to the line
            // functions, which write it as is: a Transparent (index 0) frame
            // is black, as the original LOAD page shows (docs/re/27 3.3).
            const uint16_t frame_c = colour_value(a, field(e, 4));
            if (g.bar) {
                out.frame(g.track, frame_c, frame_c);
                if (const auto* img = a.image(field(e, 11))) {
                    const Rect t = g.active ? g.thumb : Rect{g.track.x, g.track.y, 16, g.thumb.h};
                    out.blit(*img, {0, 0, std::min(int(img->width), t.w), std::min(int(img->height), t.h)}, t.x, t.y,
                             BlitMode::KeyedShadow);
                }
                if (const auto* img = a.image(field(e, es.arrow_pressed == 1 ? 13 : 12))) {
                    out.blit(*img, {0, 0, int(img->width), int(img->height)}, g.up.x, g.up.y, BlitMode::KeyedShadow);
                }
                if (const auto* img = a.image(field(e, es.arrow_pressed == 2 ? 15 : 14))) {
                    out.blit(*img, {0, 0, int(img->width), int(img->height)}, g.down.x, g.down.y,
                             BlitMode::KeyedShadow);
                }
            }
            // LAB_0048aa70: the frame around the whole box, with or without a
            // scroll bar.
            out.frame(*rect, frame_c, frame_c);
            break;
        }
        case ElementType::EditBox: {
            // FUN_0048b560. Body: pos, size, fill (+0xaa4), border (+0xaa0),
            // text colour (+0xaa8). Text at x + 5, centred vertically; with
            // focus a 2-px underline cursor (no blink) under the character at
            // the cursor (its width + spacing, or the space width at the end
            // or on a space), at text x + width(prefix), font height - 3 / - 4
            // below the centred top.
            if (!rect) break;
            static const ElementState kDefault;
            const ElementState& es = st.states && i < st.states->size() ? (*st.states)[i] : kDefault;
            if (auto fill = colour(a, field(e, 2))) out.fill(*rect, *fill);
            if (auto border = colour(a, field(e, 3))) out.frame(*rect, *border, *border);
            const auto& font = a.menu_font();
            const auto c = a.colour(field(e, 4)).value_or(formats::Rgb{255, 255, 255});
            const int top = rect->y + (rect->h - glyph_height(font)) / 2;
            if (!es.edit_text.empty()) out.draw_text_solid(font, rect->x + 5, top, es.edit_text, to_rgb555(c));
            if (es.edit_focus) {
                const size_t k = size_t(std::clamp(es.edit_cursor, 0, int(es.edit_text.size())));
                const int cx = rect->x + 5 + text_width(font, i18n::utf8::truncate(es.edit_text, k));
                int w = int(font.space_width());
                if (k < es.edit_text.size() && es.edit_text[k] != ' ') {
                    if (const auto* g = font.glyph(static_cast<unsigned char>(es.edit_text[k]))) {
                        w = int(g->width) + int(font.spacing());
                    }
                }
                const uint16_t px = to_rgb555(c);
                out.fill({cx, top + int(font.height) - 3, w, 1}, px);
                out.fill({cx, top + int(font.height) - 4, w, 1}, px);
            }
            break;
        }
        case ElementType::TextBox:
            // FUN_0048bb10: fill colour index 0 ("Transparent" or an unknown
            // name) keeps the grabbed background; any other draws FUN_0042d660
            // (fill field 2, top-left field 4, bottom-right field 3). Text in
            // field 5.
            if (!rect) break;
            if (!colour_index0(a, field(e, 2))) {
                box(out, *rect, colour_value(a, field(e, 2)), colour_value(a, field(e, 4)),
                    colour_value(a, field(e, 3)));
            }
            if (e.literal_text) {
                draw_text_in(a, out, *rect, *e.literal_text, field(e, 5), e.align);
            } else {
                static const ElementState kNoState;
                const ElementState& es = st.states && i < st.states->size() ? (*st.states)[i] : kNoState;
                if (es.literal) {
                    draw_text_in(a, out, *rect, *es.literal, field(e, 5), e.align);
                } else {
                    // EnterText (init) replaces the script's text key.
                    draw_label(a, out, *rect, es.text != "NoText" ? es.text : field(e, 6), field(e, 5), e.align);
                }
            }
            if (e.underline && colour_index0(a, field(e, 2))) {
                // FUN_0042d450: a 1 px line of the text's width in the frame
                // colour (+0x780), at top + centring + glyph height (doc 56
                // 1.5); only with the grabbed background (+0x860).
                static const ElementState kNoState2;
                const ElementState& es = st.states && i < st.states->size() ? (*st.states)[i] : kNoState2;
                const std::string text = e.literal_text ? *e.literal_text
                                         : es.literal  ? *es.literal
                                                       : a.lookup_text(es.text != "NoText" ? es.text : field(e, 6));
                const auto& font = a.menu_font();
                const int w = text_width(font, text);
                const int x = text_x(*rect, w, e.align);
                const int gh = glyph_height(font);
                if (w > 0) out.fill({x, rect->y + (rect->h - gh) / 2 + gh, w, 1}, colour_value(a, field(e, 3)));
            }
            break;
        case ElementType::ValueBar: {
            // FUN_0048cbf0: the region image at (x, y), the thumb image at the
            // thumb rect; without images, filled rects in the script colours
            // (background +2, slider +3). VALUEBAR (not _NOVAL) also prints the
            // value (below).
            if (!rect) break;
            static const ElementState kDefault;
            const ElementState& es = st.states && i < st.states->size() ? (*st.states)[i] : kDefault;
            const Rect thumb = valuebar_thumb(*rect, es);
            if (const auto* img = a.image(field(e, 8))) {
                out.blit(*img, {0, 0, int(img->width), int(img->height)}, rect->x, rect->y, BlitMode::KeyedShadow);
            } else if (auto bg = colour(a, field(e, 2))) {
                out.fill(*rect, *bg);
            }
            if (const auto* img = a.image(field(e, 9))) {
                out.blit(*img, {0, 0, std::min(int(img->width), thumb.w), std::min(int(img->height), thumb.h)},
                         thumb.x, thumb.y, BlitMode::KeyedShadow);
            } else if (auto fg = colour(a, field(e, 3))) {
                out.fill(thumb, *fg);
            }
            // VALUEBAR (not _NOVAL) with max != 0: itoa(value) in font 0 and
            // the number font colour at (x + w + 4, y) (docs/re/55 section 2.2);
            // the glyphs have no origin offset (FUN_0042ef90 sets +8 / +0xC to 0).
            if (e.header.rfind("VALUEBAR ", 0) == 0 && es.vmax != 0) {
                const auto c = a.colour(field(e, 6)).value_or(formats::Rgb{255, 255, 255});
                out.draw_text_solid(a.menu_font(), rect->x + rect->w + 4, rect->y, std::to_string(es.value),
                                    to_rgb555(c));
            }
            break;
        }
        case ElementType::MultiStateBox: {
            // FUN_0048b0e0 (docs/re/55 section 1.3). Body: pos, size, border,
            // count, one colour per state, one image per state. The state's
            // image opaque; without one, the state's colour with a frame in
            // the border colour. No text, no hover or pressed look.
            if (!rect) break;
            static const ElementState kDefault;
            const ElementState& es = st.states && i < st.states->size() ? (*st.states)[i] : kDefault;
            auto token = [&](size_t f) {
                std::istringstream ts(field(e, f));
                std::string t;
                for (int k = 0; k <= es.value && (ts >> t); ++k) {
                    if (k == es.value) return t;
                }
                return std::string("Transparent");
            };
            const std::string gfx = token(5);
            if (const auto* img = gfx != "NONE" ? a.image(gfx) : nullptr) {
                out.blit(*img, {0, 0, int(img->width), int(img->height)}, rect->x, rect->y, BlitMode::Opaque);
            } else {
                if (auto fill = colour(a, token(4))) out.fill(*rect, *fill);
                if (auto border = colour(a, field(e, 2))) out.frame(*rect, *border, *border);
            }
            break;
        }
        default:
            break; // other element types are not drawn yet
        }
    }
}

} // namespace d2k::ui
