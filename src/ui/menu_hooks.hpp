#pragma once
// Per-menu adjustments the original makes in code after loading a screen
// (1.06 FUN_004359a0). RE notes: docs/re/03-ui-scripts.md

#include <string>

#include "ui/screen.hpp"

namespace d2k::ui {

struct HookContext {
    // FUN_00492000 result; false makes MAINMENU swap "Multiplayer Game" for
    // the original's "Practice" button. Our network games are always there.
    bool network_available = true;
};

void apply_menu_hooks(const std::string& menu, Screen& screen, const HookContext& ctx);

} // namespace d2k::ui
