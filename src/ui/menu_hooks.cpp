#include "ui/menu_hooks.hpp"

#include <algorithm>

namespace d2k::ui {

namespace {

Element* find(Screen& s, const std::string& name) {
    for (auto& e : s.elements) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

void remove(Screen& s, const std::string& name) {
    std::erase_if(s.elements, [&](const Element& e) { return e.name == name; });
}

} // namespace

void apply_menu_hooks(const std::string& menu, Screen& screen, const HookContext& ctx) {
    if (menu == "MAINMENU") {
        // Removed unless the debug flag 0x007984d0 is set; nothing ever sets
        // it in 1.06, so retail players never see this button.
        remove(screen, "startNewGame");

        if (!ctx.network_available) {
            // Replace startMultiplayer with "Practice" at the same rect
            // (fallback rect 250,220 140x20), colours/gfx as in the exe.
            std::string pos = "250, 220", size = "140, 20";
            if (const Element* mp = find(screen, "startMultiplayer"); mp && mp->body.size() >= 2) {
                pos = mp->body[0];
                size = mp->body[1];
            }
            remove(screen, "startMultiplayer");
            Element e;
            e.type = ElementType::Button;
            e.header = "BUTTON practice";
            e.name = "practice";
            e.body = {pos, size, "Practice", "LightRed", "VDarkRed", "Red", "DarkRed",
                      "ButtonUpColour", "ButtonDownColour", "but4_up", "but4_dn",
                      // Command 0xB0 = SetValueS2To, "gNetNetworkType" "0"
                      // (strings at 0x004dee9c / 0x004dee98).
                      "SetValueS2To gNetNetworkType 0", "SetValueECTo gGameType GameTypeSkirmish",
                      "JumpToMenu SKIRMISH"}; // 0xB6, 0xE0 (no CD check; docs/re/51 section 1)
            screen.elements.push_back(std::move(e));
        }
    } else if (menu == "MULTIPLAYERTYPE") {
        // FUN_004359a0 deletes the buttons of services that are not
        // available and leaves the gap (docs/re/58 section 2.2). We have no
        // Westwood Online and no modem or serial link (decided 2026-10-15).
        remove(screen, "internet");
        remove(screen, "serialModem");
    } else if (menu == "IPXLOBBY") {
        // Ours (decided 2026-10-15, docs/re/58 section 7.2): one address box
        // above the game list for joining by IP; Enter asks that address
        // for its game, which then shows in gameList like a LAN game. The
        // label is our only string (text.uib has no "address" text).
        // The original page has no such box; the spot is our choice.
        Element label;
        label.type = ElementType::TextBox;
        label.header = "TEXTBOX_L addressLabel";
        label.name = "addressLabel";
        label.align = TextAlign::Left;
        label.body = {"32, 81", "66, 15", "Transparent", "Transparent", "Transparent", "JNormalText", "NoText"};
        label.literal_text = "Address"; // in the style of "Games" / "Players"
        screen.elements.push_back(std::move(label));
        Element box;
        box.type = ElementType::EditBox;
        box.header = "EDITBOX address";
        box.name = "address";
        box.body = {"100, 78", "217, 20", "Transparent", "JBoxOutLine", "JHighLightText", "ProbeAddress"};
        screen.elements.push_back(std::move(box));
    } else if (menu == "GLOBE") {
        // Debug "prev"/"next" mission buttons, removed unless 0x007984d0 is set.
        remove(screen, "prev");
        remove(screen, "next");
    }
}

} // namespace d2k::ui
