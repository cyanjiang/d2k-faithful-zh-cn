#pragma once
// UI resources loaded from the original data: strings, colours, fonts, and
// UI_GFX images (TGA or 8-bit BMP, cached by name).

#include <map>
#include <string>

#include "formats/font.hpp"
#include "formats/string_table.hpp"
#include "formats/tga.hpp"
#include "platform/game_data.hpp"
#include "ui/surface.hpp"

namespace d2k::ui {

// A menus.uib entry. Transition values: entering FADE 0 / TWEEN 2 / SKIP 3,
// leaving FADE 1 / TWEEN 2 / SKIP 3 (docs/re/04-menu-flow.md).
struct MenuInfo {
    std::string script; // UI_DATA script base name
    int enter = 3;
    int leave = 3;
};

class Assets {
public:
    explicit Assets(const platform::GameData& gd);

    const formats::StringTable& text() const { return text_; }
    const formats::ColourTable& colours() const { return colours_; }
    // Font 0 (FUN_00470710 loads "font" = FONT.FNT): every UI element's
    // font (+0x775 is only ever set to 0, FUN_00482830), drawn in one solid
    // colour by FUN_0042dc00 -> FUN_00431e40.
    const formats::Font& menu_font() const { return font_; }
    const formats::Font& small_font() const { return font_; }
    // Font 1 (FUN_00448550 loads "FontCol" into slot 0x5162b0 = 1): in-game
    // texts drawn through a 16-colour handle (FUN_0042dd90 -> FUN_00431f50).
    const formats::Font& fontcol_font() const { return fontcol_; }
    // The "FontCol" colour handle (FUN_0042e1c0, 16-bit mode): the first 16
    // FONTCOL.FPL entries converted by FUN_0042f3d0. In 1.06: index 1
    // black, the others red.
    const std::array<uint16_t, 16>& fontcol_colours() const { return fontcol_colours_; }

    // UI_GFX\<name>: the .tga if present, else the 8-bit .bmp, else
    // error.bmp (FUN_00491330, 16-bit mode). nullptr for "NONE" (any case).
    // While gGameState (0x4dfb08) is 2 (GameStateMainLoop: the in-game
    // menus) FUN_00491330 skips the .tga and loads <name>.bmp.
    const UiImage* image(const std::string& name);
    // The gGameState test of FUN_00491330: the menu runner sets it to the
    // state the menu was opened in (the exe loads the images then).
    void set_main_loop(bool on) { main_loop_ = on; }
    bool main_loop() const { return main_loop_; }

    // Text for a string key. A key missing from text.uib gives entry 0's
    // text, as FUN_0048dc10 returns index 0 on a miss ("NoText" = "" in 1.06).
    std::string lookup_text(const std::string& key) const;

    std::optional<formats::Rgb> colour(const std::string& name) const { return colours_.get(name); }

    // UI_DATA\<script>.uil for a menu name, via menus.uib.
    std::optional<std::filesystem::path> menu_script(const std::string& menu) const;
    std::optional<MenuInfo> menu_info(const std::string& menu) const;

    // samples.uib: sample name (e.g. "C_A1a") -> sound file base name.
    std::optional<std::string> sample_file(const std::string& name) const { return samples_.get(name); }
    // samples.uib entry by index (the exe's sample id, FUN_0048dd90).
    std::optional<std::string> sample_name(size_t i) const {
        if (i >= samples_.size()) return std::nullopt;
        return samples_.entries()[i].first;
    }

private:
    const platform::GameData& gd_;
    formats::StringTable text_, samples_;
    formats::ColourTable colours_;
    formats::Font fontcol_, font_;
    std::array<uint16_t, 16> fontcol_colours_{};
    std::map<std::string, MenuInfo> menus_;
    std::map<std::string, std::optional<UiImage>> images_;
    std::map<std::string, std::optional<UiImage>> images_main_loop_; // loaded with gGameState 2
    bool main_loop_ = false;
};

} // namespace d2k::ui
