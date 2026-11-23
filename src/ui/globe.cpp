#include "ui/globe.hpp"

#include <algorithm>
#include <cstdlib>

#include "ui/transition.hpp"

namespace d2k::ui {

namespace {
constexpr uint8_t kThreshold = 32;   // 0x4de608
constexpr uint8_t kStep = 4;         // 0x4de60c (halved while revealing a territory)
constexpr int kNobody = 3;
constexpr const char* kOwnerLetter = "aho";
constexpr const char* kPointGfx[] = {"ATpoint", "HKpoint", "ORpoint"};

void blit_whole(Surface& dst, const UiImage& img, int x, int y) {
    dst.blit(img, {0, 0, int(img.width), int(img.height)}, x, y, BlitMode::KeyedShadow);
}
} // namespace

Globe::Globe(Assets& assets, const campaign::CampaignTable& table,
             const std::array<campaign::Region, 27>& regions, int side, int mission)
    : assets_(assets), current_(table.at(side, std::clamp(mission, 0, 9))), regions_(regions),
      side_(side), map_(640, 400) {
    owner_.fill(kNobody);
    auto apply = [&](const campaign::CampaignRecord& r, bool mark) {
        for (int house = 0; house < campaign::kHouses; ++house) {
            for (uint8_t t : r.gained[size_t(house)]) {
                if (t == 0) continue;
                owner_[t - 1] = house;
                if (mark) pending_[t - 1] = true;
            }
        }
        // Both territories offered in that record go to the player's house.
        for (uint8_t t : r.territory) {
            if (t == 0) continue;
            owner_[t - 1] = side_;
            if (mark) pending_[t - 1] = true;
        }
    };
    for (int m = 0; m < mission - 1; ++m) apply(table.at(side, m), false);
    if (mission >= 1) apply(table.at(side, mission - 1), true);

    for (int i = 0; i < 27; ++i) {
        if (owner_[i] == kNobody) continue;
        image_[i] = assets_.image("Regions/" + std::string(1, kOwnerLetter[owner_[i]]) +
                                  std::to_string(i + 1));
    }
    // FUN_0042c370: territories not waiting to be revealed go onto dunemap.
    if (const auto* dunemap = assets_.image("dunemap")) blit_whole(map_, *dunemap, 0, 0);
    for (int i = 0; i < 27; ++i) {
        if (image_[i] && !pending_[i]) blit_whole(map_, *image_[i], regions_[i].x, regions_[i].y);
    }
}

Rect Globe::region_rect(int i) const {
    // Image rect clipped to the map's size.
    int x = regions_[i].x, y = regions_[i].y;
    int r = std::min(x + int(image_[i]->width), map_.width());
    int b = std::min(y + int(image_[i]->height), map_.height());
    return {x, y, r - x, b - y};
}

std::optional<int> Globe::pick_next(Frame& frame) {
    // Houses in the record's reveal order; within a house, lowest index first.
    for (int slot = 0; slot < 3; ++slot) {
        for (int i = 0; i < 27; ++i) {
            if (!pending_[i] || !image_[i] || owner_[i] != current_.reveal_house[size_t(slot)]) continue;
            if (!spoken_[size_t(slot)]) {
                frame.wait_for_speech = true;
                spoken_[size_t(slot)] = true;
                const std::string& s = current_.reveal_speech[size_t(slot)];
                if (s != "NONE") frame.speech = s;
            }
            return i;
        }
    }
    frame.wait_for_speech = true;
    return std::nullopt;
}

Globe::Frame Globe::update(Surface& screen, Surface& background) {
    Frame frame;
    // FUN_0042c9b0.
    if (counter_ > kThreshold) {
        counter_ = 0;
        if (initial_) {
            initial_ = false;
            revealing_ = true;
            if (auto i = pick_next(frame)) {
                background.pixels() = map_.pixels(); // map copied over the whole background
                last_ = *i + 1;
                pending_[*i] = false;
                blit_whole(map_, *image_[*i], regions_[*i].x, regions_[*i].y);
                dirty_ = region_rect(*i);
            }
        } else if (revealing_) {
            if (last_ > 0) {
                const int p = last_ - 1; // settle the previous territory on the background
                blit_whole(background, *image_[p], regions_[p].x, regions_[p].y);
            }
            if (auto i = pick_next(frame)) {
                last_ = *i + 1;
                pending_[*i] = false;
                blit_whole(map_, *image_[*i], regions_[*i].x, regions_[*i].y);
                dirty_ = region_rect(*i);
            } else {
                done_ = true;
                revealing_ = false;
            }
        }
    }
    if (initial_) {
        tween_blend(background, map_, screen, {0, 0, 640, 400}, counter_);
        counter_ = uint8_t(counter_ + kStep);
    } else if (revealing_) {
        tween_blend(background, map_, screen, dirty_, counter_);
        counter_ = uint8_t(counter_ + (kStep >> 1));
    }

    // FUN_0042c400: once revealed, arrows on the selectable territories.
    if (arrows_pending_ && done_) {
        arrows_pending_ = false;
        frame.finished = true;
        for (int k = 0; k < 2; ++k) {
            uint8_t t = current_.territory[size_t(k)];
            if (t == 0) continue;
            Arrow a;
            a.name = k == 0 ? "region1anim" : "region2anim";
            a.gfx = kPointGfx[side_];
            if (current_.arrow_x[size_t(k)] == 0 && current_.arrow_y[size_t(k)] == 0) {
                a.x = regions_[t - 1].label.x;
                a.y = regions_[t - 1].label.y;
            } else {
                a.x = current_.arrow_x[size_t(k)];
                a.y = current_.arrow_y[size_t(k)];
            }
            frame.arrows.push_back(a);
        }
    }
    return frame;
}

bool Globe::hover(int x, int y) const { return click(x, y).has_value(); }

std::optional<std::string> Globe::click(int x, int y) const {
    // Offsets 0x513a10 / 0x513a14 are 0 in 1.06.
    for (int k = 0; k < 2; ++k) {
        uint8_t t = current_.territory[size_t(k)];
        if (t != 0 && regions_[t - 1].contains(x, y)) return current_.mission[size_t(k)];
    }
    return std::nullopt;
}

} // namespace d2k::ui
