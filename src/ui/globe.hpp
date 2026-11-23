#pragma once
// The campaign map ("GLOBE" menu), after 1.06 CreateGlobe (FUN_0042bc60)
// and its per-frame update (FUN_0042c400 / FUN_0042c9b0).
// RE notes: docs/re/05-campaign-map.md

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "campaign/campaign_table.hpp"
#include "campaign/regions.hpp"
#include "ui/assets.hpp"
#include "ui/surface.hpp"

namespace d2k::ui {

class Globe {
public:
    // Builds territory ownership from the campaign records before `mission`
    // (records 0 .. mission-2 applied directly, record mission-1's changes
    // marked to be revealed) and draws the already-known ones onto the map.
    Globe(Assets& assets, const campaign::CampaignTable& table,
          const std::array<campaign::Region, 27>& regions, int side, int mission);

    struct Arrow {
        std::string name;  // "region1anim" / "region2anim"
        int x = 0, y = 0;  // 72 x 70 animation
        std::string gfx;   // ATpoint / HKpoint / ORpoint, 14 fps, looping
    };
    struct Frame {
        std::optional<std::string> speech;  // samples.uib name to play now
        bool wait_for_speech = false;       // exe busy-waits for the previous one first
        std::vector<Arrow> arrows;          // create these ANIMATION elements now
        bool finished = false;              // reveal over: redraw the whole menu
    };

    // One UI update. `background` is the menu's background image (the exe
    // modifies it in place); the cross-fades are written to `screen`.
    Frame update(Surface& screen, Surface& background);

    // FUN_0042d020: over a selectable territory (cursor changes).
    bool hover(int x, int y) const;
    // FUN_0042cf10: the mission file base name (e.g. "a1v1") clicked, if any.
    std::optional<std::string> click(int x, int y) const;

    // Whether the reveal is running (the exe hides the cursor meanwhile).
    bool revealing() const { return initial_ || revealing_; }

private:
    std::optional<int> pick_next(Frame& frame); // FUN_0042c6b0
    Rect region_rect(int index) const;

    Assets& assets_;
    const campaign::CampaignRecord& current_;
    const std::array<campaign::Region, 27>& regions_;
    int side_;

    std::array<int, 27> owner_{};           // 0..2, 3 = nobody
    std::array<bool, 27> pending_{};        // changed in the last mission: still to reveal
    std::array<const UiImage*, 27> image_{};
    Surface map_;                           // dunemap with the revealed territories
    std::array<bool, 3> spoken_{};          // per reveal slot (0x513a04..0c)

    bool initial_ = true;     // 0x5139e8: whole-map fade-in
    bool revealing_ = false;  // 0x5139ec: one territory at a time
    bool done_ = false;       // 0x5139f0
    bool arrows_pending_ = true; // 0x4de604
    uint8_t counter_ = 0;     // 0x5139f8 (fade alpha)
    int last_ = 0;            // 0x5139fc: index + 1 of the last revealed territory
    Rect dirty_{};            // 0x5139d8..e4
};

} // namespace d2k::ui
