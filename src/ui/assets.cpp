#include "ui/assets.hpp"

#include "i18n/i18n.hpp"
#include <algorithm>
#include <cctype>

#include "formats/byte_reader.hpp"
#include "formats/file_io.hpp"

namespace d2k::ui {

namespace {

std::vector<uint8_t> must_read(const platform::GameData& gd, const std::string& rel) {
    auto p = gd.find(rel);
    if (!p) throw std::runtime_error("missing game file: " + rel);
    return formats::read_file(*p);
}

// menus.uib: u32 count, then (counted menu name, counted "file.uid",
// u32 entering transition, u32 leaving transition). docs/re/04-menu-flow.md
std::map<std::string, MenuInfo> parse_menus(std::span<const uint8_t> data) {
    formats::ByteReader r(data);
    std::map<std::string, MenuInfo> out;
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) {
        std::string name = r.fixed_string(r.u16());
        std::string file = r.fixed_string(r.u16());
        MenuInfo info;
        info.script = file.substr(0, file.rfind('.'));
        info.enter = int(r.u32());
        info.leave = int(r.u32());
        out.emplace(name, info); // first match wins, as FUN_0048dd10
    }
    return out;
}

} // namespace

Assets::Assets(const platform::GameData& gd) : gd_(gd) {
    text_ = formats::StringTable::parse(must_read(gd, "Data/UI_DATA/text.uib"));
    samples_ = formats::StringTable::parse(must_read(gd, "Data/UI_DATA/samples.uib"));
    colours_ = formats::ColourTable::parse(must_read(gd, "Data/UI_DATA/colours.uib"));
    fontcol_ = formats::parse_font(must_read(gd, "Data/FONTCOL.FNT"));
    fontcol_colours_ = formats::parse_font_palette(must_read(gd, "Data/FONTCOL.FPL"));
    font_ = formats::parse_font(must_read(gd, "Data/FONT.FNT"));
    // bin\FONT.BIN, the character map every font shares (FUN_0042e450).
    const auto map = must_read(gd, "Data/bin/FONT.BIN");
    font_.set_char_map(map);
    fontcol_.set_char_map(map);
    menus_ = parse_menus(must_read(gd, "Data/UI_DATA/menus.uib"));
}

const UiImage* Assets::image(const std::string& name) {
    if (name.empty()) return nullptr;
    std::string upper = name;
    for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
    if (upper == "NONE") return nullptr; // __strcmpi against "NONE"
    auto& cache = main_loop_ ? images_main_loop_ : images_;
    auto it = cache.find(name);
    if (it == cache.end()) {
        std::optional<UiImage> img;
        // FUN_00491330: with gGameState 2 the exe loads <name>.bmp directly
        // (the 16-bit branch needs 0x4dfb08 != 2), so the in-game pages
        // draw the 8-bit versions of images that exist in both forms
        // (but1_up, box2, Loadgame, RBut4_up, the arrows, ...). Every
        // image the in-game pages name has a .bmp in 1.06; without one we
        // fall back to the front-end order (the exe would fail to load it).
        auto bmp_first = main_loop_ ? gd_.find("Data/UI_GFX/" + name + ".bmp") : std::nullopt;
        if (bmp_first) {
            img = UiImage::from(formats::parse_bmp8(formats::read_file(*bmp_first)));
        } else if (auto tga = gd_.find("Data/UI_GFX/" + name + ".tga")) {
            img = UiImage::from(formats::parse_tga16(formats::read_file(*tga)));
        } else if (auto bmp = gd_.find("Data/UI_GFX/" + name + ".bmp")) {
            img = UiImage::from(formats::parse_bmp8(formats::read_file(*bmp)));
        } else if (auto err = gd_.find("Data/UI_GFX/error.bmp")) {
            img = UiImage::from(formats::parse_bmp8(formats::read_file(*err)));
        }
        it = cache.emplace(name, std::move(img)).first;
    }
    return it->second ? &*it->second : nullptr;
}

std::string Assets::lookup_text(const std::string& key) const {
    if (key == "NoText") return {};
    std::string value;
    if (auto v = text_.get(key)) value = *v;
    else value = text_.entries().empty() ? std::string() : text_.entries()[0].second;
    // The first-run setup validates every 1.06 file against its SHA-256, so
    // localisation must never edit text.uib. Translations live in an external
    // pack; a miss simply leaves the original English text in place.
    return i18n::localize(value, key);
}

std::optional<std::filesystem::path> Assets::menu_script(const std::string& menu) const {
    auto it = menus_.find(menu);
    if (it == menus_.end()) return std::nullopt;
    return gd_.find("Data/UI_DATA/" + it->second.script + ".uil");
}

std::optional<MenuInfo> Assets::menu_info(const std::string& menu) const {
    auto it = menus_.find(menu);
    if (it == menus_.end()) return std::nullopt;
    return it->second;
}

} // namespace d2k::ui
