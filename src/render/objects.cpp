#include "render/objects.hpp"

#include <algorithm>

#include "render/terrain.hpp"

namespace d2k::render {

namespace {
const ui::Rect kPlayfield{kPlayfieldX, kPlayfieldY, kPlayfieldWidth, kPlayfieldHeight};

// One draw-list item (FUN_0042aa90): up to three images (body, turret,
// overlay) drawn in that order at their own positions.
struct Part {
    size_t entry;
    int x, y;
    bool no_flash = false; // the repair wrench is never grey
    bool additive = false; // FUN_004349a0: the active overlay of record flag 0x800000
};
struct Draw {
    int sort_y;
    std::vector<Part> parts;
    int colour;
    // Stealth (units): shimmer pattern entry (0 = normal blit), the faded
    // level (-1 = no shimmer) and the dissolve mask entry (0 = none).
    size_t shimmer = 0;
    int level = -1;
    size_t dissolve = 0;
    // The target flash (docs/re/44 section 3): grey before any other blit choice.
    bool flash = false;
    size_t object = 0; // index into draw_objects' `objects` (for after_item)
};
} // namespace

void draw_objects(const std::vector<sim::Object>& objects, const formats::Templates& templates,
                  const SpriteBank& sprites, ui::Surface& screen, int scroll_x, int scroll_y, int local,
                  uint32_t tick, int passes, const std::function<void(size_t)>& after_item) {
    const auto& art = templates.art;
    auto screen_x = [&](const sim::Object& o) { return (o.x >> 16) + kPlayfieldX - scroll_x; };
    auto screen_y = [&](const sim::Object& o) { return (o.y >> 16) + kPlayfieldY - scroll_y; };

    // Pass 1: building foundations. The damaged image replaces the base when
    // the element has no frames and hit points are below half.
    for (const auto& o : objects) {
        if (!(passes & 1)) break;
        if (!o.building) continue;
        const auto& r = templates.building_records[size_t(o.record)];
        if (r.base_element < 0 || size_t(r.base_element) >= art.building.size()) continue;
        const auto& e = art.building[size_t(r.base_element)];
        // While building up, the base shows from frame +0x89 on (FUN_00427d50).
        if ((o.flags & 6) && o.buildup / 3 < r.buildup_base) continue;
        size_t entry = size_t(e.base);
        if (e.frames == 0 && o.hit_points < r.hit_points / 2) entry = size_t(e.base + 1 + e.frames);
        sprites.draw(screen, entry, screen_x(o), screen_y(o), o.colour >= 0 ? o.colour : o.owner, kPlayfield);
    }

    // Pass 2: building bodies (+ turret tops) and units.
    if (!(passes & 2)) return;
    std::vector<Draw> list;
    for (const auto& o : objects) {
        const int sx = screen_x(o), sy = screen_y(o);
        if (o.building) {
            // FUN_00428e30: body, turret top and overlay of one draw-list item.
            const auto& r = templates.building_records[size_t(o.record)];
            long body = -1, turret = -1, overlay = -1;
            bool additive = false;
            if (r.base_element >= 0 && size_t(r.base_element) < art.building.size()) {
                const auto& e = art.building[size_t(r.base_element)];
                if (e.frames > 0) body = e.base + 1 + std::min(o.frame, e.frames - 1);
                // FUN_00428e30: below half hit points (and not building up or
                // selling) the body comes from the damaged table 0x7907a8
                // [element][+0x20], the images after the normal frames. +0x20
                // is 0 except on walls / turrets, so animated buildings show
                // their first damaged image (their animations are overlays).
                if (e.frames > 0 && o.hit_points < r.hit_points / 2 && !(o.flags & 6)) {
                    const long dmg = e.base + 1 + e.frames + std::min(o.damage_frame, e.frames - 1);
                    if (size_t(dmg) < sprites.size() && sprites.entry(size_t(dmg)).width > 0) body = dmg;
                }
            }
            if (r.second_element >= 0 && size_t(r.second_element) < art.building.size()) {
                const auto& e = art.building[size_t(r.second_element)];
                const int rot = templates.building[size_t(o.record)][size_t(0x64 + (o.facing & 31))];
                if (e.frames > 0) turret = e.base + 1 + rot % e.frames;
            }
            if (o.flags & 6) { // build-up (animation 1, 3 ticks per frame)
                const int f = o.buildup / 3;
                const long a1 = art.anim1_first[r.anim1 % 100] + f;
                if (body < 0 || f < r.buildup_base) {
                    body = a1;
                    turret = -1;
                } else {
                    overlay = a1;
                }
            } else if ((o.flags & 0x30) && (o.flags & 0x40) && o.hit_points >= r.hit_points / 2) {
                // Active animation (animation 2). Records with flag 0x800000
                // (the light, heavy and high tech factories in 1.06) give the
                // item draw flag 4 (0x429135), and the overlay is
                // then drawn by FUN_004349a0 (0x429b7a): every pixel, each 5-bit
                // channel min(screen + sprite, 31) through the table 0x6d8da8
                // that FUN_00466140 fills with min(i + j, 31).
                const size_t ai = r.anim2 % 100;
                const int rate = ai < templates.building_records.size()
                                     ? std::max<int>(templates.building_records[ai].anim_rate, 1)
                                     : 1;
                if (art.anim2_frames[ai] > 0) overlay = art.anim2_first[ai] + o.anim / rate;
                additive = (r.flags & 0x800000) != 0;
            }
            // With a turret top, body and turret move by record +0x60/+0x61.
            const int tx = turret >= 0 ? r.spawn_x : 0, ty = turret >= 0 ? r.spawn_y : 0;
            // FUN_0042aa90: the sort key is the pixel y the item is drawn at
            // (0x42902f), 32 px less for a building whose record has flag
            // 0x200000.
            Draw d{(o.y >> 16) - ((r.flags & 0x200000) ? 32 : 0), {}, o.colour >= 0 ? o.colour : o.owner};
            d.flash = (o.flash & 2) != 0;
            d.object = size_t(&o - objects.data());
            if (body >= 0) d.parts.push_back({size_t(body), sx + tx, sy + ty});
            if (turret >= 0) d.parts.push_back({size_t(turret), sx + tx, sy + ty});
            if (overlay >= 0) d.parts.push_back({size_t(overlay), sx, sy, false, additive});
            if (o.repair_blink && sprites.size() > 3) {
                // The repair wrench, fixed DATA.R16 entry 3, centred on the
                // drawn body image at the item position (0x429c81: x = item x +
                // w / 2 - x_off - icon w / 2, y = item y + h / 2 - y_off -
                // icon h / 2, the icon's own offsets not applied; the item
                // position has no turret offset), docs/re/21 section 2.3.
                long base = body;
                if (base < 0 && r.base_element >= 0 && size_t(r.base_element) < art.building.size()) {
                    base = art.building[size_t(r.base_element)].base;
                }
                if (base >= 0 && size_t(base) < sprites.size()) {
                    const auto& img = sprites.entry(size_t(base));
                    const auto& icon = sprites.entry(3);
                    const int cxp = sx + (int(img.width) / 2 - img.offset_x) - int(icon.width) / 2;
                    const int cyp = sy + int(img.height) / 2 - img.offset_y - int(icon.height) / 2;
                    d.parts.push_back({3, cxp + icon.offset_x, cyp + icon.offset_y, true});
                }
            }
            // Kept without images too: the exe's item still draws its
            // bracket from the base image (after_item).
            list.push_back(std::move(d));
        } else {
            const auto& r = templates.unit_records[size_t(o.record)];
            // FUN_00428e30: in the death / crush modes (+0x20 > 8) the
            // rotation slot is the mode - 9 (which death animation), not
            // the facing.
            const int rot_index = o.anim_mode > 8 ? o.anim_mode - 9 : r.facing_to_rotation[size_t(o.facing & 31)];
            // FUN_00428e30 (0x429332): a unit's key is its drawn pixel y,
            // y - altitude.
            Draw d{(o.y >> 16) - (o.altitude >> 16), {}, o.colour >= 0 ? o.colour : o.owner};
            d.flash = (o.flash & 2) != 0;
            d.object = size_t(&o - objects.data());
            if ((o.flags & 4) || o.fade != 0) {
                // FUN_00428e30: own units shimmer; others shimmer faded and
                // dissolve during the 22-tick fades, else are not drawn.
                // Shimmer patterns DATA.R16 178..193, dissolve masks 194..205.
                const bool cloaked = (o.flags & 4) != 0;
                const int half = o.fade >> 1;
                d.shimmer = size_t(178 + ((uint32_t(o.index) + tick) & 0xf));
                if (o.owner == local) d.level = 0;
                else if (o.fade != 0 && !o.infantry_pass) d.level = cloaked ? 11 - half : half;
                // The infantry pass (FUN_0042af00, docs/re/32 section 1.7): no
                // faded shimmer for other sides and the dissolve index reversed.
                if (o.fade != 0) d.dissolve = size_t(194 + ((cloaked != o.infantry_pass) ? 11 - half : half));
            }
            if (r.kind == 1 && o.frame != 0) { // docked harvester: explosion EX_HARV frame (doc 13 section 8.2)
                const int harv = templates.explosion_index("EX_HARV");
                if (harv >= 0) {
                    d.parts.push_back({size_t(art.explosion_first[size_t(harv)] + o.frame), sx, sy});
                    list.push_back(std::move(d));
                    continue;
                }
            }
            // FUN_00428e30: the body by facing (+0x54) and frame; the turret
            // (record +0x18) by the turret facing (+0x55), frame 0.
            for (int element : {r.base_element, r.turret_element}) {
                if (element < 0 || size_t(element) >= art.unit.size()) continue;
                const auto& e = art.unit[size_t(element)];
                if (e.rotations <= 0) continue;
                const bool turret = element == r.turret_element;
                const int frame = turret ? 0 : std::min(o.frame, e.frames - 1);
                const int rot = turret ? r.facing_to_rotation[size_t(o.turret_facing & 31)] : rot_index;
                d.parts.push_back({size_t(e.first + frame * e.rotations + rot % e.rotations), sx, sy});
            }
            if (!d.parts.empty()) list.push_back(std::move(d));
        }
    }
    // FUN_0042aa90 inserts each item before the first one whose key is >=
    // its own, so equal keys end up in reverse add order.
    std::vector<Draw> sorted;
    sorted.reserve(list.size());
    for (const auto& d : list) {
        auto it = std::find_if(sorted.begin(), sorted.end(), [&](const Draw& o) { return o.sort_y >= d.sort_y; });
        sorted.insert(it, d);
    }
    for (const auto& d : sorted) {
        for (const auto& part : d.parts) {
            if (d.flash && !part.no_flash) { // tested first: also over stealth (docs/re/44 section 3.1)
                sprites.draw_grey(screen, part.entry, part.x, part.y, d.colour, kPlayfield);
                continue;
            }
            if (d.shimmer == 0) {
                if (part.additive) {
                    sprites.draw_blit(screen, part.entry, part.x, part.y, d.colour, Blit::Additive, kPlayfield);
                } else {
                    sprites.draw(screen, part.entry, part.x, part.y, d.colour, kPlayfield);
                }
                continue;
            }
            if (d.level >= 0) sprites.draw_shimmer(screen, part.entry, part.x, part.y, d.shimmer, d.level, kPlayfield);
            if (d.dissolve != 0) sprites.draw_dissolve(screen, part.entry, part.x, part.y, d.colour, d.dissolve, kPlayfield);
        }
        if (after_item) after_item(d.object);
    }
}

} // namespace d2k::render
