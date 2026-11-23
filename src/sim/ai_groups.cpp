// The computer player's military part: groups, building attacks, side and
// target choice, group monitoring, fighting back, rampaging, defend areas
// and BESERK. RE: docs/re/23-ai-teams.md (function names are the exe's
// profiler labels).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

#include "sim/world.hpp"
#include "sim/x87.hpp"

namespace d2k::sim {

namespace {
// AI block fields (doc 23 section 2).
constexpr size_t kSideToAttack = 0x02, kSideCountdown = 0x18, kSquish = 0x1C, kSquishRange = 0x14;
constexpr size_t kDefendOn = 0x1D, kBeserk = 0x1E, kLastX = 0x24, kLastY = 0x28;
constexpr size_t kTimeBetweenBuildingAttacks = 0xF14, kBuildingAttackTimer = 0xF18;
constexpr size_t kAttackBuildingStrength = 0xF3D, kRetargetChance = 0xF47, kGuardBuildings = 0xF56;
constexpr size_t kBaseX = 0xF57, kBaseY = 0xF58, kGuardGroupSize = 0xF5C, kDistanceWeight = 0xF64;
constexpr size_t kAttackBuildingRandom = 0xF6C, kGroupSizeRandom = 0xF6F;
constexpr size_t kSpeedWeight = 0xF70, kHpWeight = 0xFBC, kMorale = 0x1078, kAttackPriority = 0x121C;
constexpr size_t kDefendGroup = 0x1D07, kMoraleModifier = 0x1D1C, kUseIdeal = 0x1D20, kMaxBaseStrength = 0xF4C;
constexpr size_t kBaseGroup = 0x1D3C, kGuardGroup = 0x1D3D, kAreaCount = 0x1D51, kBaseArea = 0x1D52;
constexpr size_t kAreas = 0x1D54; // + a * 0x14: x0, x1, y0, y1, +8 wanted, +0xc period
// Group record fields, at g * 0x2C + offset.
constexpr size_t gSide = 0x2C, gTarget = 0x2E, gTimer = 0x34, gStrength = 0x38, gStart = 0x3C;
constexpr size_t gIdle = 0x40, gAge = 0x44, gJob = 0x48, gSub = 0x49, gFlags = 0x4C, gX = 0x50, gY = 0x51;
constexpr size_t gInUse = 0x52, gMode = 0x53, gDefend = 0x54;
constexpr int kJobBase = 1, kJobGuarding = 2, kJobAttackBuilding = 5, kJobAttackUnit = 6, kJobReturning = 10;
constexpr int kJobRepair = 0xB, kJobDeploying = 0xD, kJobRampaging = 0xE, kJobDefendArea = 0xF;
constexpr size_t kEmergency = 0x1D1D;
constexpr uint32_t kEngaged = 0x100000;
constexpr uint16_t kNone = 0xffff;
size_t G(int g, size_t off) { return size_t(g) * 0x2C + off; }
uint32_t le32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
void set_le32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
// FUN_00421770: the squared distance of the two pixel cells (x / 32, the
// signed divisions rounding toward zero).
uint32_t ai_cell_dist2(const Unit& a, const Unit& b) {
    const int dx = (a.x / 32) / 0x10000 - (b.x / 32) / 0x10000, dy = (a.y / 32) / 0x10000 - (b.y / 32) / 0x10000;
    return uint32_t(dx * dx + dy * dy);
}
} // namespace

uint16_t World::ai_u16(int s, size_t o) const { return uint16_t(ai_[size_t(s)][o] | ai_[size_t(s)][o + 1] << 8); }

uint8_t* World::ai_group_field(int s, int g, size_t off) {
    // The 8 blocks are contiguous at 0x4f4118. Group 0xFF of side 7 lies
    // past them (0x503d40.., other globals): not modelled, the write is
    // dropped (it needs a full group table, docs/re/53 section 11).
    const size_t flat = size_t(s) * 0x1DB8 + G(g, off);
    if (flat + 4 > 8 * size_t(0x1DB8)) return nullptr;
    return &ai_[flat / 0x1DB8][flat % 0x1DB8];
}
void World::set_ai_u16(int s, size_t o, uint16_t v) {
    ai_[size_t(s)][o] = uint8_t(v);
    ai_[size_t(s)][o + 1] = uint8_t(v >> 8);
}

bool World::player_active(int p) const {
    // +0x24250: set by AddPlayer (0x4a45d0), which the mission set-up
    // (0x448828) calls for all 8 sides; only a network game clears it, when
    // a side is removed (FUN_0046e6b0, docs/re/57 section 3.8).
    return p >= 0 && p < 8 && !side_removed_[size_t(p)];
}

void World::base_box(int p, int& x0, int& x1, int& y0, int& y1) const {
    // The player box +0x262e8..+0x262eb, as RebuildLists last left it.
    const auto& bb = players_[size_t(p)].base_box;
    y0 = bb[0];
    y1 = bb[1];
    x0 = bb[2];
    x1 = bb[3];
}

void World::snapshot_base_box(int p) {
    // The box part of FUN_0046be50 "RebuildLists" (docs/re/53 section 1.6):
    // the buildings' cells with a 2-cell margin (the y of a building is its
    // bottom edge); without buildings, or with a box at (0, 0), the units'
    // cells. Signed divisions round toward zero, as the exe's shifts.
    const int W = map_.width(), H = map_.height();
    int x0 = W, y0 = H, x1 = 0, y1 = 0;
    bool any = false;
    for (const auto& b : buildings_) { // the P+0x10 list
        if (b.owner != p || b.removed) continue;
        any = true;
        const auto& r = templates_.building_records[size_t(b.record)];
        const int cx = (b.x / 0x10000) / 32, cy = (b.y / 0x10000) / 32;
        x0 = std::min(x0, cx - 2);
        x1 = std::max(x1, cx + (r.width_px >> 5) + 2);
        y0 = std::min(y0, cy - 2 - (r.height_px >> 5));
        y1 = std::max(y1, cy + 2);
    }
    if (!any) x0 = y0 = 0;
    if (x0 == 0 && y0 == 0) {
        x0 = W; // x1, y1 keep their values
        y0 = H;
        for (const auto& u : units_) { // the P+8 list, aircraft too
            if (u.owner != p || u.remove) continue;
            const int ux = (u.x / 0x10000) / 32, uy = (u.y / 0x10000) / 32;
            x0 = std::min(x0, ux);
            x1 = std::max(x1, ux + 1);
            y0 = std::min(y0, uy);
            y1 = std::max(y1, uy + 1);
        }
    }
    // Stored as bytes. An inverted box (no units, and no buildings or a
    // building box starting exactly at (0, 0)) reaches the callers; each one
    // keeps the exe's arithmetic, except the two divisions by a zero width
    // (FindRandomBuildingPosition, the job 0xF patrol), which skip the try
    // (docs/re/53 section 1.6).
    auto& bb = players_[size_t(p)].base_box;
    bb[0] = uint8_t(y0 < 1 ? 0 : y0);
    bb[1] = uint8_t(std::min(y1, H - 1));
    bb[2] = uint8_t(x0 < 1 ? 0 : x0);
    bb[3] = uint8_t(std::min(x1, W - 1));
}

void World::ai_base_position(int s, int& x, int& y) {
    // FUN_0041e180 "GetBestBasePosition": FUN_0046e3f0's result (with no
    // buildings: the first unit that is not an aircraft, else the box
    // centre). +0x24 / +0x28 (-1 from InitAI 0x41b1f4) remember the first
    // result (0x41e1d9, also a first (0, 0)); a later (0, 0) gives the
    // remembered cell (its low bytes). Then x >= W - 1 -> W - 1, y likewise.
    retreat_destination(s, x, y); // the no-building branch fills the cell too (FUN_0046e3f0)

    x = uint8_t(x); // FUN_0046e3f0 writes bytes
    y = uint8_t(y);
    const bool remembered = ai_u32(s, kLastX) != 0xffffffffu;
    if (x == 0 && y == 0 && remembered) {
        x = ai_u8(s, kLastX);
        y = ai_u8(s, kLastY);
    } else if (!remembered) {
        set_ai_u32(s, kLastX, uint32_t(x));
        set_ai_u32(s, kLastY, uint32_t(y));
    }
    if (x >= map_.width() - 1) x = map_.width() - 1;
    if (y >= map_.height() - 1) y = map_.height() - 1;
}

int World::ai_unit_strength(int s, const Unit& u, int job) const {
    // FUN_0041cb70 "GetUnitStrength": record hit points and speed, weighted.
    if (u.state == 0x11 || u.remove) return 0;
    const auto& r = templates_.unit_records[size_t(u.record)];
    const size_t j = size_t(std::clamp(job, 0, 18));
    return int(double(r.hit_points) * double(ai_f32(s, kHpWeight + 4 * j))) +
           int(double(r.speed >> 16) * double(ai_f32(s, kSpeedWeight + 4 * j)));
}

int World::ai_group_strength(int s, int g) const { return ai_group_strength_job(s, g, ai_u8(s, G(g, gJob))); }

int World::ai_group_strength_job(int s, int g, int job) const {
    int sum = 0;
    for (const auto& u : units_) {
        if (u.owner == s && u.ai_group == g) sum += ai_unit_strength(s, u, job);
    }
    return sum;
}

void World::ai_group_position(int s, int g, int& x, int& y) const {
    // FUN_00420a70 "GetAveragePositionOfGroup": unsigned mean of the cells of
    // the pixel positions, (x / 32) / 65536 (0x420aa9..0x420adc), not the
    // +0x4A / +0x4B cells (docs/re/57a section 7).
    uint32_t sx = 0, sy = 0, n = 0;
    for (const auto& u : units_) {
        if (u.owner != s || u.ai_group != g || u.remove) continue;
        sx += uint32_t((u.x / 32) / 0x10000);
        sy += uint32_t((u.y / 32) / 0x10000);
        ++n;
    }
    x = n ? int(sx / n) : 0;
    y = n ? int(sy / n) : 0;
}

int World::ai_free_group(int s) const {
    for (int g = 1; g < 32; ++g) {
        if (!ai_u8(s, G(g, gInUse))) return g;
    }
    return -1;
}

void World::ai_take_over(int s) {
    // FUN_004235e0 "TakeOver_AI" (docs/re/57a section 4): ActivateAI, then
    // the armed ground units outside the base box go into Rampaging groups
    // of 20 in list order. Each group's timer is one rand(); the last group
    // always draws one (even empty) and is not marked in use (quirk kept).
    ai_activate(s);
    auto free_group = [&] { return std::max(ai_free_group(s), 0); }; // none: group 0
    auto set_up_rampage = [&](int g) {
        set_ai_u8(s, G(g, gJob), kJobRampaging);
        set_ai_u32(s, G(g, gStrength), uint32_t(ai_group_strength_job(s, g, kJobRampaging)));
        set_ai_u8(s, G(g, gSub), 4);
        set_ai_u32(s, G(g, gAge), 0);
        set_ai_u32(s, G(g, gTimer), (rng_.next() & 0x3F) + 1);
        set_ai_u16(s, G(g, gTarget), kNone);
        int x, y;
        ai_group_position(s, g, x, y);
        set_ai_u8(s, G(g, gX), uint8_t(x));
        set_ai_u8(s, G(g, gY), uint8_t(y));
    };
    int x0, x1, y0, y1;
    base_box(s, x0, x1, y0, y1);
    int n = 0;
    int g = free_group();
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& u = units_[i];
        if (u.owner != s || u.remove) continue;
        const bool unarmed = templates_.unit_records[size_t(u.record)].weapon == 0xff;
        const bool in_box = u.cur_x >= x0 && u.cur_x <= x1 && u.cur_y >= y0 && u.cur_y <= y1;
        if (unarmed || (u.flags & 0x400140) || in_box) {
            u.ai_group = 0xff;
            continue;
        }
        u.ai_group = uint8_t(g);
        if (++n >= 20) {
            set_up_rampage(g);
            set_ai_u8(s, G(g, gInUse), 1);
            n = 0;
            g = free_group();
        }
    }
    set_up_rampage(g);
}

void World::ai_clear_selection(int s) {
    for (auto& u : units_) {
        if (u.owner == s) u.selected = false;
    }
}

void World::ai_select_group(int s, int g, bool all) {
    // FUN_0041cce0 "SelectedGroup".
    for (auto& u : units_) {
        if (u.owner != s || u.ai_group != g || u.state == 8 || u.state == 0x11 || u.remove) continue;
        if (!all && (u.flags & kEngaged)) continue;
        u.selected = true;
    }
}

void World::ai_splinter(int s, int from, int to, int strength) {
    // FUN_0041ca60 "CreateSplinterGroup": units of `from` (list order, not
    // in state 8, not harvesters or carryalls) until the size limit; the
    // limit uses the unsigned percentage quirk (doc 23 section 4.1), and the
    // loop test is unsigned too (CMP sum, limit; JAE at 0x41cae5), so a
    // negative limit moves every such unit. GroupSizeRandomPercentage is 20
    // in every 1.06 slot; 0 would be a division fault (DIV at 0x41ca97).
    const int q = ai_u8(s, kGroupSizeRandom);
    int32_t limit = strength;
    if (q > 0) limit = x87::percent_of(int32_t(q - int32_t(rng_.next() % uint32_t(2 * q))), strength);
    const int job = ai_u8(s, G(to, gJob));
    int sum = 0;
    for (auto& u : units_) {
        if (u.owner != s || u.ai_group != from || u.remove || u.state == 8) continue;
        const int kind = templates_.unit_records[size_t(u.record)].kind;
        if (kind == 1 || kind == 2) continue;
        if (uint32_t(sum) >= uint32_t(limit)) break;
        u.ai_group = uint8_t(to);
        sum += ai_unit_strength(s, u, job);
    }
    set_ai_u32(s, G(to, gStrength), uint32_t(sum));
    set_ai_u32(s, G(to, gStart), uint32_t(sum));
    set_ai_u32(s, G(to, gAge), 0);
    set_ai_u8(s, G(to, gInUse), 1);
    set_ai_u8(s, G(to, gSub), 0);
}

bool World::ai_target_alive(int s, int g) const {
    const uint16_t t = ai_u16(s, G(g, gTarget));
    if (t == kNone) return false;
    const int kind = ai_target_kind_[size_t(s)][size_t(g)];
    if (kind == 1) {
        const int i = find_unit(t);
        return i >= 0 && units_[size_t(i)].state != 0x11 && !(units_[size_t(i)].flags & 0x400140);
    }
    if (kind == 2) return t < buildings_.size() && building_alive(t) && !buildings_[t].dying;
    return false;
}

void World::ai_commence(int s, int g) {
    // FUN_0041fc80 "CommenceOperation": the group's order for the units
    // that are selected now.
    const int job = ai_u8(s, G(g, gJob));
    const uint16_t t = ai_u16(s, G(g, gTarget));
    switch (job) {
    case kJobAttackBuilding: case 0x10: case 0x12: {
        const int sub = ai_u8(s, G(g, gSub));
        if (sub > 6 && sub < 9) {
            const auto v1 = ai_u8(s, G(g, gX)); // evaluated at post time
            const auto v2 = ai_u8(s, G(g, gY)); // evaluated at post time
            post_order(s, [=, this] { order_move(s, v1, v2); });
        } else if (t != kNone && ai_target_kind_[size_t(s)][size_t(g)] == 2) {
            post_order(s, [=, this] { order_attack_building(s, t); });
        }
        return;
    }
    case kJobAttackUnit:
        if (!ai_target_alive(s, g)) set_ai_u16(s, G(g, gTarget), kNone);
        if (ai_u16(s, G(g, gTarget)) != kNone) {
            const auto v1 = ai_u16(s, G(g, gTarget)); // evaluated at post time
            post_order(s, [=, this] { order_attack_unit(s, v1); });
        }
        return;
    case kJobReturning: {
        // 0x41fe37..0x41fe5e: GetBestBasePosition into +0x50 / +0x51, then
        // SendGroupToPoint (ClearUnits, SelectedGroup(g, 1), move order).
        int x, y;
        ai_base_position(s, x, y);
        set_ai_u8(s, G(g, gX), uint8_t(x));
        set_ai_u8(s, G(g, gY), uint8_t(y));
        ai_send_group_to_point(s, x, y, g);
        return;
    }
    case kJobRampaging: {
        if (!ai_target_alive(s, g)) set_ai_u16(s, G(g, gTarget), kNone);
        if (ai_u16(s, G(g, gTarget)) == kNone) {
            int x, y;
            ai_group_position(s, g, x, y);
            set_ai_u8(s, G(g, gX), uint8_t(x));
            set_ai_u8(s, G(g, gY), uint8_t(y));
            ai_nearest_hostile(s, g); // 0x41fda8: the result is not tested, the target is
            if (ai_u16(s, G(g, gTarget)) == kNone) return;
        }
        const uint16_t tt = ai_u16(s, G(g, gTarget));
        const int kind = ai_target_kind_[size_t(s)][size_t(g)];
        if (kind == 1) post_order(s, [=, this] { order_attack_unit(s, tt); });
        else if (kind == 2) post_order(s, [=, this] { order_attack_building(s, tt); });
        else set_ai_u16(s, G(g, gTarget), kNone);
        return;
    }
    default:
        return;
    }
}

void World::ai_return_to_base(int s, int g) {
    // FUN_0041e340 "ReturnToBase": the base position, job 10, substate 1,
    // no target; ClearUnits, SelectedGroup(g, 0) (engaged units left out),
    // CommenceOperation(g) (0x41e397..0x41e3a9), whose job 10 case takes
    // the base position again and calls SendGroupToPoint. No ClearUnits
    // at the end.
    int x, y;
    ai_base_position(s, x, y);
    set_ai_u8(s, G(g, gX), uint8_t(x));
    set_ai_u8(s, G(g, gY), uint8_t(y));
    set_ai_u8(s, G(g, gJob), kJobReturning);
    set_ai_u8(s, G(g, gSub), 1);
    set_ai_u16(s, G(g, gTarget), kNone);
    ai_clear_selection(s);
    ai_select_group(s, g, false);
    ai_commence(s, g);
}

bool World::ai_morale_ok(int s, int g) const {
    // FUN_0041e420 "MoraleCheck" (signed).
    const int32_t start = int32_t(ai_u32(s, G(g, gStart))), now = int32_t(ai_u32(s, G(g, gStrength)));
    const int job = std::clamp<int>(ai_u8(s, G(g, gJob)), 0, 18);
    const int32_t m = int32_t(ai_u8(s, kMorale + size_t(job))) + int32_t(ai_u8(s, kMoraleModifier));
    return start - now <= (m * start) / 100;
}

void World::ai_become_rampaging(int s, int g) {
    set_ai_u8(s, G(g, gJob), kJobRampaging);
    set_ai_u32(s, G(g, gTimer), 10000); // FUN_00423820
    set_ai_u8(s, G(g, gSub), 4);
    set_ai_u16(s, G(g, gTarget), kNone);
    set_ai_u32(s, G(g, gAge), 0);
    int x, y;
    ai_group_position(s, g, x, y);
    set_ai_u8(s, G(g, gX), uint8_t(x));
    set_ai_u8(s, G(g, gY), uint8_t(y));
}

int World::ai_recalculate_side(int s) {
    // FUN_0041ce50 "RecalculateSideToAttack": one rand() for every side.
    int bx0, bx1, by0, by1;
    base_box(s, bx0, bx1, by0, by1);
    const int mx = bx0 + (bx1 - bx0) / 2, my = by0 + (by1 - by0) / 2;
    uint32_t best = 0;
    int choice = 0xff;
    for (int side = 0; side < 8; ++side) {
        int64_t sc = 0;
        if (side != s && player_active(side) && alliance_[size_t(s)][size_t(side)] == 1) {
            for (const auto& u : units_) sc += u.owner == side && !u.remove && !(u.flags & 0x44);
            for (const auto& b : buildings_) sc += 3 * (b.owner == side && !b.removed && !(b.flags & 0x4000));
            if (sc != 0) {
                int x0, x1, y0, y1;
                base_box(side, x0, x1, y0, y1);
                const int dx = mx - (x0 + (x1 - x0) / 2), dy = my - (y0 + (y1 - y0) / 2);
                const int d = int(std::sqrt(double(dx * dx + dy * dy)));
                if (d > 0) sc = int64_t(double(sc) / (double(d) * 0.0078125));
            }
        }
        if (sc < 2) sc = 1;
        uint32_t w = (uint32_t(rng_.next() % 40 + 80) * uint32_t(sc)) / 100;
        if (!ai_u8(side, 0x01)) w <<= 2; // sides without an AI are 4x more attractive
        if (w > best) {
            best = w;
            choice = side;
        }
    }
    return choice;
}

int World::ai_best_side(int s) {
    // FUN_0041cdf0 "GetBestSideToAttack": the countdown steps first (when
    // not 0), then a countdown of 0, no side or a side that is not an
    // enemy recalculates and restarts at 25. The side byte is only ever
    // 0..7 or 0xFF (RecalculateSideToAttack, InitAI 0x41b1d9).
    uint32_t c = ai_u32(s, kSideCountdown);
    const int side = ai_u8(s, kSideToAttack);
    const bool not_enemy = side != 0xff && (side >= 8 || alliance_[size_t(s)][size_t(side)] != 1);
    if (c != 0) set_ai_u32(s, kSideCountdown, --c);
    if (c == 0 || side == 0xff || not_enemy) {
        set_ai_u8(s, kSideToAttack, uint8_t(ai_recalculate_side(s)));
        set_ai_u32(s, kSideCountdown, 25);
    }
    return ai_u8(s, kSideToAttack);
}

int World::ai_item_ready(int s, int kind) const {
    // FUN_00422730 "ItemReady": the kind must be in the unit build list; the
    // last slot holding it is ready above 0x59FF.
    const auto& pl = players_[size_t(s)];
    bool listed = false;
    for (const int rec : pl.unit_list) {
        if (rec >= 0 && templates_.unit_records[size_t(rec)].kind == kind) listed = true;
    }
    if (!listed) return -1;
    int slot = -1;
    for (int i = 0; i < 10; ++i) {
        const auto& sl = pl.unit_slots[size_t(i)];
        if (sl.item >= 0 && templates_.unit_records[size_t(sl.item)].kind == kind) slot = i;
    }
    if (slot < 0 || pl.unit_slots[size_t(slot)].progress <= 0x59ff) return -1;
    return slot;
}

int World::ai_best_target(int side, int kind, int& x, int& y) {
    // FUN_00422800 "GetBestTarget": a non-wall building with chance b / 128
    // (one rand() each), else a unit with chance u / 128 (rand() first).
    if (side < 0 || side >= 8) return 0;
    int bc = 5, uc = 5;
    if (kind == 9) bc = 10;
    else if (kind == 10) {
        bc = 15;
        uc = 1;
    }
    for (const auto& b : buildings_) {
        if (b.owner != side || b.removed) continue;
        if (templates_.building_records[size_t(b.record)].kind == 0xe) continue;
        if (int(rng_.next() & 0x7f) < bc) {
            x = b.x >> 16; // the bottom-left corner (quirk: one row below)
            y = b.y >> 16;
            return 2;
        }
    }
    for (const auto& u : units_) {
        if (u.owner != side || u.remove) continue;
        if (int(rng_.next() & 0x7f) >= uc || (u.flags & 0x400140) || templates_.unit_records[size_t(u.record)].kind == 5) continue;
        x = u.x >> 16;
        y = u.y >> 16;
        return 1;
    }
    return 0;
}

void World::ai_special_weapons(int s) {
    // FUN_00422270 "DoSpecialWeapons" (docs/re/35 section 10): every 32
    // ticks, after SpecialWeaponDelay, not in BESERK. The top-level
    // GetBestSideToAttack runs for every side (rand() parity). The cases by
    // ((side + tick) >> 5) & 7: 0 the engineer group, 1 saboteur, 2 Fremen,
    // 3 airstrike, 4 Death Hand.
    if (ai_u8(s, 0x1E)) return;
    if (((tick_count_ + uint32_t(s)) & 0x1f) != 0) return;
    if (tick_count_ < ai_u32(s, 0x1D18)) return;
    const int best = ai_best_side(s);
    auto palace = [&](int kind, size_t off) { // cases 1 / 2: ItemReady -> exit free -> used (no rand())
        const int slot = ai_item_ready(s, kind);
        if (slot < 0) return;
        const auto* ids = templates_.block(0x6d60d8);
        const int rec = ids && ids->bytes.size() > off ? ids->bytes[off] : -1;
        int sx, sy, mx, my;
        if (rec < 0 || find_factory(rec, s, true, sx, sy, mx, my) < 0) return; // FUN_00422bf0
        auto& sl = players_[size_t(s)].unit_slots[size_t(slot)];
        if (sl.used) return;
        post_order(s, [=, this] { order_special(s, kind, 0, 0); });
        sl.used = true; // marked after posting; the order clears the slot 2 ticks later
    };
    switch (((uint32_t(s) + tick_count_) >> 5) & 7) {
    case 0: { // the engineer capture group (docs/re/39 section 4.1)
        const int base = ai_u8(s, kBaseGroup);
        Unit* eng = nullptr;
        for (auto& u : units_) {
            if (u.owner == s && !u.remove && templates_.unit_records[size_t(u.record)].kind == 3 && u.ai_group == base) {
                eng = &u;
                break;
            }
        }
        if (!eng) return;
        const int g = ai_free_group(s);
        if (g <= 0) return;
        eng->ai_group = uint8_t(g);
        int n = int(rng_.next() & 7);
        for (auto& u : units_) {
            if (n <= 0) break;
            if (u.owner != s || u.remove) continue;
            const auto& r = templates_.unit_records[size_t(u.record)];
            if (r.infantry && u.ai_group == base && r.weapon != 0xff) {
                u.ai_group = uint8_t(g);
                --n;
            }
        }
        const uint32_t st = uint32_t(ai_group_strength_job(s, g, 0x10));
        set_ai_u32(s, G(g, gStrength), st);
        set_ai_u32(s, G(g, gStart), st);
        set_ai_u8(s, G(g, gInUse), 1);
        int x, y;
        ai_group_position(s, g, x, y);
        if (best == 0xff) return; // the group stays in use with its old job (quirk)
        const int b = ai_best_building(s, best, x, y, 2.0f);
        if (b >= 0) {
            set_ai_u16(s, G(g, gTarget), uint16_t(b));
            ai_target_kind_[size_t(s)][size_t(g)] = 2;
            set_ai_u8(s, G(g, gSide), uint8_t(best));
            set_ai_u8(s, G(g, gX), uint8_t((buildings_[size_t(b)].x >> 16) / 32));
            set_ai_u8(s, G(g, gY), uint8_t((buildings_[size_t(b)].y >> 16) / 32)); // the bottom edge (quirk)
            set_ai_u8(s, G(g, gSub), 2);
            set_ai_u32(s, G(g, gIdle), 0);
            set_ai_u8(s, G(g, gJob), 0x10);
            set_ai_u32(s, G(g, gStrength), uint32_t(ai_group_strength(s, g)));
            ai_clear_selection(s);
            ai_select_group(s, g, true);
            ai_commence(s, g);
        } else {
            ai_merge_groups(s, g, base);
        }
        return;
    }
    case 1:
        palace(4, 0x14); // saboteur, 0x6d60ec
        return;
    case 2:
        palace(12, 0x15); // Fremen, 0x6d60ed
        return;
    case 4: { // the Death Hand (docs/re/41 section 9)
        const int slot = ai_item_ready(s, 10);
        if (slot < 0) return;
        if (players_[size_t(s)].unit_slots[size_t(slot)].used) return;
        int px = 0, py = 0;
        if (ai_best_target(ai_best_side(s), 10, px, py) == 0) return;
        post_order(s, [=, this] { order_special(s, 10, px / 32, py / 32); });
        players_[size_t(s)].unit_slots[size_t(slot)].used = true; // after posting (docs/re/41 section 9)
        return;
    }
    case 3: {
        const int slot = ai_item_ready(s, 9);
        if (slot < 0) return;
        auto& sl = players_[size_t(s)].unit_slots[size_t(slot)];
        if (sl.used) return;
        int px = 0, py = 0;
        if (ai_best_target(ai_best_side(s), 9, px, py) == 0) return; // the second call
        post_order(s, [=, this] { order_special(s, 9, px / 32, py / 32); });
        sl.used = true;
        return;
    }
    default:
        return;
    }
}

void World::ai_merge_groups(int s, int g, int into) {
    // FUN_00421710: every unit of g joins `into`; g is freed.
    for (auto& u : units_) {
        if (u.owner == s && u.ai_group == g) u.ai_group = uint8_t(into);
    }
    set_ai_u8(s, G(g, gInUse), 0);
}

void World::ai_do_fremen(int s) {
    // FUN_00422a80 (docs/re/39 section 4.3); +0x3C, the timer, age and flags
    // keep the slot's old values (quirk).
    const int g = ai_free_group(s);
    if (g <= 0) return;
    for (auto& u : units_) {
        if (u.owner == s && !u.remove && u.ai_group == 0xff && templates_.unit_records[size_t(u.record)].kind == 12) {
            u.ai_group = uint8_t(g);
        }
    }
    const int side = ai_best_side(s);
    int x, y;
    ai_group_position(s, g, x, y);
    const int b = side < 8 ? ai_best_building(s, side, x, y, 0.25f) : -1; // 0xff: a fatal error in the exe
    if (b < 0) {
        ai_merge_groups(s, g, ai_u8(s, kBaseGroup));
        return;
    }
    set_ai_u8(s, G(g, gJob), kJobAttackBuilding);
    set_ai_u16(s, G(g, gTarget), uint16_t(b));
    ai_target_kind_[size_t(s)][size_t(g)] = 2;
    set_ai_u8(s, G(g, gSide), uint8_t(side));
    set_ai_u8(s, G(g, gX), uint8_t((buildings_[size_t(b)].x >> 16) / 32));
    set_ai_u8(s, G(g, gY), uint8_t((buildings_[size_t(b)].y >> 16) / 32));
    set_ai_u8(s, G(g, gSub), 2);
    set_ai_u32(s, G(g, gIdle), 0);
    set_ai_u32(s, G(g, gStrength), uint32_t(ai_group_strength_job(s, g, kJobAttackBuilding)));
    set_ai_u8(s, G(g, gInUse), 1);
    ai_clear_selection(s);
    ai_select_group(s, g, true);
    ai_commence(s, g); // the Fremen stay selected (quirk: the guard order that follows goes to them)
}

void World::ai_do_saboteur(int s) {
    // FUN_00422900 (docs/re/39 section 4.4): one fully charged saboteur per call.
    const int g = ai_free_group(s);
    if (g <= 0) return;
    Unit* sab = nullptr;
    for (auto& u : units_) {
        if (u.owner == s && !u.remove && u.ai_group == 0xff && templates_.unit_records[size_t(u.record)].kind == 4 &&
            u.load >= 0xa0) {
            sab = &u;
            break;
        }
    }
    if (!sab) return;
    sab->ai_group = uint8_t(g);
    const int side = ai_best_side(s);
    int x, y;
    ai_group_position(s, g, x, y);
    const int b = side < 8 ? ai_best_building(s, side, x, y, 0.25f) : -1;
    if (b < 0) {
        ai_merge_groups(s, g, ai_u8(s, kBaseGroup));
        return;
    }
    set_ai_u8(s, G(g, gJob), 0x12);
    set_ai_u16(s, G(g, gTarget), uint16_t(b));
    ai_target_kind_[size_t(s)][size_t(g)] = 2;
    set_ai_u8(s, G(g, gSide), uint8_t(side));
    set_ai_u8(s, G(g, gX), uint8_t((buildings_[size_t(b)].x >> 16) / 32));
    set_ai_u8(s, G(g, gY), uint8_t((buildings_[size_t(b)].y >> 16) / 32));
    set_ai_u8(s, G(g, gSub), 2);
    set_ai_u32(s, G(g, gIdle), 0);
    set_ai_u32(s, G(g, gStrength), 100); // a constant
    set_ai_u8(s, G(g, gInUse), 1);
    ai_clear_selection(s);
    ai_select_group(s, g, true);
    ai_commence(s, g); // job 0x12 like 5: order 0x15
    ai_clear_selection(s);
}

int World::ai_best_building(int s, int side, int x, int y, float f) {
    // FUN_0041bbb0 "GetBestBuilding": one rand() per building; the
    // priority uses the unsigned percentage quirk (doc 23 section 4.4).
    struct C {
        int index, d;
        float prio;
    };
    std::vector<C> cands;
    const int q = ai_u8(s, kAttackBuildingRandom);
    int min_d = 0x7fffffff, max_d = 0;
    int counted = 0;
    for (const auto& b : buildings_) {
        if (b.owner != side || b.removed || counted >= 199) continue;
        ++counted;
        int32_t off = 0;
        if (q > 0) off = int32_t(q - int32_t(rng_.next() % uint32_t(2 * q)));
        const float pr = b.record < 100 ? ai_f32(s, kAttackPriority + 4 * size_t(b.record)) : 0.0f;
        const float prio = x87::to_float(x87::mul(x87::percent_factor(off), x87::from_float(pr)));
        if (prio == 0.0f) continue;
        const int dx = x - ((b.x >> 16) >> 5), dy = y - ((b.y >> 16) >> 5);
        const int d = int(std::sqrt(double(dx * dx + dy * dy)));
        cands.push_back({b.index, d, prio});
        min_d = std::min(min_d, d);
        max_d = std::max(max_d, d);
    }
    int best = -1;
    float best_score = 0.0f;
    const float w = ai_f32(s, kDistanceWeight);
    for (const auto& c : cands) {
        const float score = float(max_d - c.d) * f * w + c.prio;
        if (score > best_score) {
            best_score = score;
            best = c.index;
        }
    }
    return best;
}

void World::ai_attack_best_building(int s, int g, int side, int x, int y) {
    // FUN_0041fee0 "AttackBestBuilding".
    if (side == 0xff) {
        ai_return_to_base(s, g);
        return;
    }
    // A building: target, the cell of its pixel position (x, y >> 16 >> 5:
    // the row below the footprint), substate 2, idle 0, job 5; ClearUnits,
    // SelectedGroup(g, 1), CommenceOperation. None: job 0xE, timer 10000
    // (FUN_00423820), substate 4, in use, age 0, no target, the strength
    // with job 0xE's weights, and CommenceOperation with the selection as
    // it is (no ClearUnits / SelectedGroup on this path).
    set_ai_u8(s, G(g, gSide), uint8_t(side));
    const int b = ai_best_building(s, side, x, y, 1.0f);
    if (b >= 0) {
        const Building& t = buildings_[size_t(b)];
        set_ai_u16(s, G(g, gTarget), uint16_t(b));
        ai_target_kind_[size_t(s)][size_t(g)] = 2;
        set_ai_u8(s, G(g, gX), uint8_t((t.x / 0x10000) / 32));
        set_ai_u8(s, G(g, gY), uint8_t((t.y / 0x10000) / 32));
        set_ai_u8(s, G(g, gSub), 2);
        set_ai_u32(s, G(g, gIdle), 0);
        set_ai_u8(s, G(g, gJob), kJobAttackBuilding);
        ai_clear_selection(s);
        ai_select_group(s, g, true);
        ai_commence(s, g);
        return;
    }
    set_ai_u8(s, G(g, gJob), kJobRampaging);
    set_ai_u32(s, G(g, gTimer), 10000);
    set_ai_u8(s, G(g, gSub), 4);
    set_ai_u8(s, G(g, gInUse), 1);
    set_ai_u32(s, G(g, gAge), 0);
    set_ai_u16(s, G(g, gTarget), kNone);
    set_ai_u32(s, G(g, gStrength), uint32_t(ai_group_strength_job(s, g, kJobRampaging)));
    ai_commence(s, g);
}

bool World::ai_nearest_hostile(int s, int g) {
    // FUN_00420700 "GetNearestHostileThing" (doc 23 section 6.3): from the
    // group's cell (+0x50 / +0x51), over the sides 0..7 that are enemies,
    // their buildings (not dying, not kind 0xE; the name test against
    // 0x6d60e1 never excludes: that id is 0xFF in 1.06) then their units
    // (not dying, not worms, no flag 0x400144), nearest by the unsigned
    // squared distance of the pixel cells (start 0xFFFFFF, strict <). Every
    // better object writes the target id (+0x2E) and side (+0x2C) at once.
    // It succeeds when a unit was the best at some point of the scan (a
    // later, nearer building keeps the success); then +0x50 / +0x51 become
    // the final best object's cell. Nothing found leaves the target as it was.
    const int gx = ai_u8(s, G(g, gX)), gy = ai_u8(s, G(g, gY));
    uint32_t best = 0xffffff;
    bool found = false;
    int cx = 0, cy = 0;
    for (int sd = 0; sd < 8; ++sd) {
        if (alliance_[size_t(s)][size_t(sd)] != 1) continue;
        for (const auto& b : buildings_) {
            if (b.owner != sd || b.removed || b.dying) continue;
            if (templates_.building_records[size_t(b.record)].kind == 0xe) continue;
            const int bx = (b.x / 0x10000) / 32, by = (b.y / 0x10000) / 32; // by: the row below the footprint
            const int dx = gx - bx, dy = gy - by;
            const uint32_t d = uint32_t(dx * dx + dy * dy);
            if (d < best) {
                best = d;
                set_ai_u16(s, G(g, gTarget), uint16_t(b.index));
                set_ai_u8(s, G(g, gSide), uint8_t(sd));
                ai_target_kind_[size_t(s)][size_t(g)] = 2;
                cx = bx;
                cy = by;
            }
        }
        for (const auto& u : units_) {
            if (u.owner != sd || u.remove || u.state == 0x11 || (u.flags & 0x400144)) continue;
            if (templates_.unit_records[size_t(u.record)].kind == 5) continue;
            const int ux = (u.x / 32) / 0x10000, uy = (u.y / 32) / 0x10000;
            const int dx = gx - ux, dy = gy - uy;
            const uint32_t d = uint32_t(dx * dx + dy * dy);
            if (d < best) {
                best = d;
                found = true;
                set_ai_u16(s, G(g, gTarget), uint16_t(u.index));
                set_ai_u8(s, G(g, gSide), uint8_t(sd));
                ai_target_kind_[size_t(s)][size_t(g)] = 1;
                cx = ux;
                cy = uy;
            }
        }
    }
    if (!found) return false;
    set_ai_u8(s, G(g, gX), uint8_t(cx));
    set_ai_u8(s, G(g, gY), uint8_t(cy));
    return true;
}

void World::ai_group_new_units(int s, int group, bool force) {
    // FUN_0041c3e0 "GroupNewUnits" (doc 23 section 3.4).
    if (!force && (tick_count_ & 0x1f) != uint32_t(s)) return;
    if (ai_free_group(s) < 0) return;
    ai_clear_selection(s);
    bool any = false;
    int busy = 0;
    const Unit* last = nullptr; // the last unit of the side's list (iVar10)
    const bool beserk = ai_u8(s, kBeserk) == 1 || ai_u8(s, kBeserk) == 2;
    const int free_group = ai_free_group(s); // FUN_0041c3a0
    // After the loop (0x41c75a..): when no unit was busy (BESERK: when none
    // joined), the first group (1..31) whose delivery is complete (flag 1)
    // starts by its mode: 0 guard, 1 rampage, 2 return to base; then
    // SelectedGroup(g, 0) and CommenceOperation. No ClearUnits around it
    // (docs/re/39 section 4.2): the units selected so far stay selected.
    auto start_delivered_group = [&](const Unit* last_unit) {
        for (int g = 1; g < 32; ++g) {
            const uint32_t fl = ai_u32(s, G(g, gFlags));
            if (!(fl & 1)) continue;
            set_ai_u32(s, G(g, gFlags), fl & ~1u);
            const int mode = ai_u8(s, G(g, gMode));
            if (mode == 0) {
                // FUN_0041e490 SetGroupToGuard: per unit of the group (list
                // order) FUN_0041fea0: ClearUnits, then a live one is
                // selected alone and gets its own guard order (FUN_00459980).
                std::vector<int> ids;
                for (const auto& u : units_) {
                    if (u.owner == s && !u.remove && u.ai_group == g) ids.push_back(u.index);
                }
                for (int id : ids) {
                    ai_clear_selection(s);
                    const int ui = find_unit(id);
                    if (ui < 0 || units_[size_t(ui)].state == 0x11) continue;
                    units_[size_t(ui)].selected = true;
                    post_order(s, [=, this] { order_guard(s); });
                }
            } else if (mode == 1 && last_unit) {
                // The cell of the last unit's pixel position (0x41c7b9).
                set_ai_u8(s, G(g, gX), uint8_t(last_unit->x / 32 / 65536));
                set_ai_u8(s, G(g, gY), uint8_t(last_unit->y / 32 / 65536));
                set_ai_u16(s, G(g, gTarget), kNone);
                set_ai_u8(s, G(g, gSide), 0);
                set_ai_u32(s, G(g, gTimer), 10000); // FUN_00423820
                set_ai_u8(s, G(g, gJob), kJobRampaging);
                set_ai_u8(s, G(g, gSub), 4);
                set_ai_u32(s, G(g, gAge), 0);
            } else if (mode == 2) {
                ai_return_to_base(s, g);
            }
            ai_select_group(s, g, false);
            ai_commence(s, g);
            break;
        }
    };
    if (beserk) {
        // BESERK: every ungrouped unit between steps joins the free group;
        // units in state 3 go to state 2 (selected). With BESERK = 1 the
        // group then rampages (FUN_0041c3e0, 0x41c6f0..).
        int joined = 0;
        for (auto& u : units_) {
            if (u.owner != s || u.remove) continue;
            last = &u;
            if (u.ai_group != 0xff && !ai_u8(s, G(u.ai_group, gInUse))) u.ai_group = 0xff;
            if ((u.flags & 0x840) || u.ai_group != 0xff || u.steps != 0) continue;
            u.ai_group = uint8_t(free_group);
            if (u.state == 3) {
                // SetState(2)'s result is not tested (0x41c4eb): SelectUnit
                // (FUN_0041cd50) selects it unless it is dying (0x41c4fd).
                set_state(u, state::kIdle);
                if (u.state != 0x11) u.selected = true;
            }
            ++joined;
        }
        set_ai_u32(s, G(group, gStrength), uint32_t(ai_group_strength(s, group)));
        if (joined == 0) start_delivered_group(last); // the same counter as the busy count
        if (ai_u8(s, kBeserk) == 1 && joined) {
            set_ai_u8(s, G(free_group, gJob), kJobRampaging);
            set_ai_u32(s, G(free_group, gTimer), 10000); // FUN_00423820
            set_ai_u8(s, G(free_group, gSub), 4);
            set_ai_u8(s, G(free_group, gInUse), 1);
            set_ai_u32(s, G(free_group, gAge), 0);
            set_ai_u16(s, G(free_group, gTarget), kNone);
            set_ai_u32(s, G(free_group, gStrength), uint32_t(ai_group_strength(s, free_group)));
            ai_commence(s, free_group);
        }
        return; // no ClearUnits: DoAI ends with one (FUN_0041b610)
    }
    int n_sab = 0, n_fremen = 0;
    for (auto& u : units_) {
        if (u.owner != s || u.remove) continue;
        last = &u;
        if (u.ai_group != 0xff && !ai_u8(s, G(u.ai_group, gInUse))) u.ai_group = 0xff;
        if (u.ai_group != 0xff || (u.flags & 0x840)) continue;
        const int kind = templates_.unit_records[size_t(u.record)].kind;
        if (kind == 1 || kind == 2 || kind == 6) continue;
        if (kind == 4) { // counted before the busy test (docs/re/39 section 4.2)
            ++n_sab;
            continue;
        }
        if (kind == 0xc) {
            ++n_fremen;
            continue;
        }
        if (u.state == 8 || u.state == 0x14 || u.state == 9 || u.steps != 0) { // busy
            ++busy;
            continue;
        }
        if (u.flags & 0x1080) {
            // Deviated (0x1000) or just returned (0x80): an own one-unit
            // rampage group; a Devastator then self-destructs on rand() & 1
            // (docs/re/40 section 5.1). No free group: retried next time.
            const int g = ai_free_group(s);
            if (g > 0) {
                u.ai_group = uint8_t(g);
                set_ai_u8(s, G(g, gJob), kJobRampaging);
                set_ai_u32(s, G(g, gTimer), 2);
                set_ai_u32(s, G(g, gStrength), uint32_t(ai_group_strength_job(s, g, kJobRampaging)));
                set_ai_u8(s, G(g, gInUse), 1);
                set_ai_u8(s, G(g, gSub), 4);
                set_ai_u32(s, G(g, gAge), 0);
                set_ai_u16(s, G(g, gTarget), kNone);
                u.flags &= ~0x80u;
                if (kind == 7 && (rng_.next() & 1)) {
                    // FUN_0045a3f0 with +0x19 set around it: the self-destruct
                    // order (order 0x2F, docs/re/37 section 1.6).
                    post_order(s, [=, this] { order_deploy(s, u.index); });
                }
            }
            continue;
        }
        if (u.flags & 0x40000) {
            // Delivered: the delivery's group (a frigate's goes to the base
            // group); harvesters clear the group's flags.
            u.flags &= ~0x40000u;
            const auto& table = deliveries_[size_t(s)];
            const bool frigate = u.delivery_entry < table.size() && table[u.delivery_entry].type == 2;
            if (frigate) {
                u.ai_group = ai_u8(s, kBaseGroup);
            } else {
                const int g = ai_group_for_delivery(s, u.delivery_entry);
                u.ai_group = uint8_t(g);
                if (uint8_t* f = ai_group_field(s, g, gFlags)) set_le32(f, le32(f) | 4);
            }
            if (kind == 1) {
                if (uint8_t* f = ai_group_field(s, u.ai_group, gFlags)) set_le32(f, 0);
            }
            continue;
        }
        u.ai_group = uint8_t(group);
        if (kind != 2 && kind != 1 && u.state != 0x11) {
            u.selected = true;
            any = true;
        }
    }
    set_ai_u32(s, G(group, gStrength), uint32_t(ai_group_strength(s, group)));
    if (busy == 0) start_delivered_group(last);
    // DoFremen leaves its Fremen selected and DoSaboteur ends with ClearUnits
    // (0x422a67 / 0x422a6e), so the guard order below goes to whatever is
    // selected now: the new base units and a started group, or the Fremen
    // after a successful DoFremen, or nobody after DoSaboteur acted
    // (docs/re/39 section 4.2).
    if (!force && ai_u8(s, kBeserk) == 0) {
        if (n_fremen > 4) ai_do_fremen(s);
        if (n_sab > 0) ai_do_saboteur(s);
    }
    if (any) post_order(s, [=, this] { order_guard(s); }); // FUN_00459980: order 0x18 for the selected units
    // No ClearUnits here: DoAI ends with one (FUN_0041b610).
}

int World::ai_group_for_delivery(int s, int entry) {
    // FUN_00420480 "FindGroupForDelivery": a Deploying group of this entry,
    // else the first free group (set up as Deploying).
    int pick = -1;
    for (int g = 1; g < 32; ++g) {
        if (!ai_u8(s, G(g, gInUse)) && pick < 0) pick = g;
        if (ai_u8(s, G(g, gInUse)) == 1 && ai_u8(s, G(g, gJob)) == kJobDeploying && ai_u8(s, G(g, gSub)) == entry) {
            pick = g;
        }
    }
    if (pick < 0) {
        // No free group (all 31 in use, none Deploying this entry): the exe
        // keeps 0xFF and indexes the record with MOVSX (0x4204c6), i.e. at
        // the block - 0x2C, so "in use" is byte +0x26 (byte 2 of LastX) and
        // the set-up writes into the side's own header: +0x1D (DefendOn) =
        // entry, +0x1C (Squish) = 0xD, +0x0C = 100, u16 +0x02 = 0xFFFF
        // (side to attack, and EnablePractice +0x03). It returns 0xFF.
        if (!ai_u8(s, 0x26)) {
            set_ai_u8(s, 0x26, 1);
            set_ai_u8(s, 0x1D, uint8_t(entry));
            set_ai_u8(s, 0x1C, 0xD);
            set_ai_u32(s, 0x0C, 100);
            set_ai_u8(s, 0x02, 0xff);
            set_ai_u8(s, 0x03, 0xff);
        }
        return 0xff;
    }
    if (!ai_u8(s, G(pick, gInUse))) {
        set_ai_u8(s, G(pick, gInUse), 1);
        set_ai_u8(s, G(pick, gSub), uint8_t(entry));
        set_ai_u8(s, G(pick, gJob), kJobDeploying);
        set_ai_u32(s, G(pick, gStrength), 100);
        set_ai_u16(s, G(pick, gTarget), kNone);
    }
    return pick;
}

void World::ai_delivery_done(int s, int entry) {
    if (s < 0 || s >= 8 || !ai_u8(s, 0x01)) return;
    const int g = ai_group_for_delivery(s, entry);
    if (uint8_t* f = ai_group_field(s, g, gFlags)) set_le32(f, le32(f) | 1);
    const auto& table = deliveries_[size_t(s)];
    if (uint8_t* m = ai_group_field(s, g, gMode)) *m = uint8_t(size_t(entry) < table.size() ? table[size_t(entry)].action : 0);
}

void World::ai_attack_buildings(int s) {
    // FUN_0041c920 "DoAttackBuildings".
    const int32_t t = int32_t(ai_u32(s, kBuildingAttackTimer));
    set_ai_u32(s, kBuildingAttackTimer, uint32_t(t - 1));
    if (t >= 1) return;
    const int p = ai_u8(s, 0xF6E); // TimingRandomPercentage
    int32_t next = int32_t(ai_u32(s, kTimeBetweenBuildingAttacks));
    if (p > 0) {
        next = x87::percent_of(int32_t(p - int32_t(rng_.next() % uint32_t(2 * p))),
                               int64_t(ai_u32(s, kTimeBetweenBuildingAttacks)));
    }
    set_ai_u32(s, kBuildingAttackTimer, uint32_t(next));
    const int g = ai_free_group(s);
    if (g < 0) return;
    const int base = ai_u8(s, kBaseGroup);
    // GetPercentage (FUN_0041e5d0).
    const int32_t pct = ai_u8(s, kAttackBuildingStrength);
    const int32_t strength = ai_u8(s, kUseIdeal) ? int32_t(ai_u32(s, kMaxBaseStrength)) * pct / 100
                                                 : int32_t(ai_u32(s, G(base, gStrength))) * pct / 100;
    ai_splinter(s, base, g, strength);
    set_ai_u8(s, G(g, gJob), kJobAttackBuilding);
    int x, y;
    ai_group_position(s, g, x, y);
    ai_attack_best_building(s, g, ai_best_side(s), x, y);
    ai_group_position(s, g, x, y);
    ai_attack_best_building(s, g, ai_u8(s, G(g, gSide)), x, y);
}

bool World::ai_recently_in_action(const Unit& u) const {
    // FUN_0041e2c0: hit recently, or fired within 3 x (rec+0x0e >> 4) x 8 ticks.
    if (u.state == 0x11) return false;
    const auto& r = templates_.unit_records[size_t(u.record)];
    return u.hit_age <= 11 || u.counter_2b < uint32_t((r.reload >> 4) * 3);
}

bool World::ai_hostile(int s, int side) const {
    // The alliance table 0x798830[s * 8 + side] != 0. The exe also indexes
    // it with side 0xFF (a unit never hit, CreateUnit 0x455a04, or one that
    // gave up a chase, 0x41f791): byte 0x79892f + 8 s, inside side 0's
    // object pool (slot i at side + 0x30 + i * 0x94): slot 0 +0x8F for
    // s = 0, slot 1 +0x03 + 8 (s - 1) for s >= 1. The side reset zeroes
    // +0x00..+0x7B of every slot (0x46ba92), nothing writes +0x8E / +0x8F,
    // and the LIFO free list hands out slot 999 first (head at 0x46ba61),
    // so slots 1 and 0 are only taken while side 0 holds 999 / 1000 pool
    // objects at once. Below that the byte is 0, as here (docs/re/53
    // section 11; the 999-object case is not modelled).
    return side >= 0 && side < 8 && alliance_[size_t(s)][size_t(side)] != 0;
}

bool World::ai_object_is_unit_with(int kind, int id, uint32_t flags) const {
    // FUN_004218a0 (flags 0x400140) / the worm test FUN_00421860 (flags 0):
    // the object is a unit, and has one of the flags (or is a worm).
    if (kind != 1) return false;
    const int i = find_unit(id);
    if (i < 0) return false;
    const Unit& v = units_[size_t(i)];
    if (flags == 0) return templates_.unit_records[size_t(v.record)].kind == 5;
    return (v.flags & flags) != 0;
}

void World::ai_flee_cell(int s, int& x, int& y) const {
    // FUN_0049e290, the same routine as the harvester's and the drop's
    // free cell (one port: World::free_cell_near, docs/re/13 section 5.3).
    free_cell_near(s, x, y);
}

bool World::ai_most_appropriate_target(int s, const Unit& u, int& kind, int& id) const {
    // FUN_00420140 "FindMostAppropriateTarget": over the other units of the
    // group that are in action (list order), within 0xFFF squared cells and
    // not aiming at a cloaked unit: their attacker if hostile, not a worm
    // and not 0x400140; else their target under the same tests, with the
    // attacker's side in the alliance test (quirk). The last match wins.
    kind = 0;
    id = -1;
    for (const auto& v : units_) {
        if (v.owner != s || v.remove || &v == &u || v.ai_group != u.ai_group || !ai_recently_in_action(v)) continue;
        const uint32_t d = ai_cell_dist2(u, v);
        if (ai_object_is_unit_with(v.target_kind, v.target, 4)) continue; // aiming at a cloaked unit
        if (d >= 0xfff) continue;
        const int akind = v.attacker == -1 ? 0 : v.attacker <= -1000 ? 2 : 1;
        const int aid = akind == 2 ? -1000 - v.attacker : v.attacker;
        if (akind != 0 && ai_hostile(s, v.attacker_side) && !ai_object_is_unit_with(akind, aid, 0) &&
            !ai_object_is_unit_with(akind, aid, 0x400140)) {
            kind = akind;
            id = aid;
        } else if (v.target_kind != 0 && ai_hostile(s, v.attacker_side) &&
                   !ai_object_is_unit_with(v.target_kind, v.target, 0) &&
                   !ai_object_is_unit_with(v.target_kind, v.target, 0x400140)) {
            kind = v.target_kind;
            id = v.target;
        }
    }
    return kind != 0;
}

bool World::ai_stop_start(int s, int g, int n, bool retarget) {
    // FUN_0041f670 "DoStopStartStuff" (docs/re/53 section 7.2): engaged units
    // give up far chases; units that are hit fight back (or flee, unarmed);
    // quiet units retarget, pick up nearby cell objects or rejoin the
    // group's operation. The selection is left as it is at the end.
    bool reacted = false;
    int released = 0;
    ai_clear_selection(s);
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& u = units_[i];
        if (u.owner != s || u.remove || u.ai_group != g) continue;
        const auto& r = templates_.unit_records[size_t(u.record)];
        bool engaged = false;
        if (u.flags & kEngaged) {
            if (u.target_kind == 0) {
                u.flags &= ~kEngaged;
            } else if (u.target_kind == 1 && find_unit(u.target) >= 0) {
                // The give-up distance: squared cells against the longer
                // weapon range in cells (the test mixes d^2 and R, kept).
                const uint32_t d = ai_cell_dist2(u, units_[size_t(find_unit(u.target))]);
                uint32_t R = uint32_t(bullet_range(r.weapon)) >> 5;
                if (r.weapon2 != 0xff) R = std::max(R, uint32_t(bullet_range(r.weapon2)) >> 5);
                if (R * R < d && uint32_t(vars.chase_give_up) < d - R) {
                    u.attacker = -1;
                    u.attacker_side = -1;
                    u.flags &= ~kEngaged;
                    if (u.state != 0x11) {
                        u.selected = true;
                        ai_commence(s, g);
                        u.selected = false;
                    }
                }
            }
            engaged = (u.flags & kEngaged) != 0;
        }
        if (!engaged) {
            // FUN_0041e310 "UnitRecentlyHit": not dying and hit within 12.
            if (u.state != 0x11 && u.hit_age < 12) {
                if (ai_hostile(s, u.attacker_side) && u.state != 3 && u.attacker != -1 && !(u.flags & kEngaged)) {
                    if (r.weapon == 0xff) {
                        ai_clear_selection(s);
                        if (u.state != 0x11) u.selected = true; // SelectUnit FUN_0041cd50
                        if (u.state == state::kIdle) {
                            int x = (u.x / 32) / 0x10000, y = (u.y / 32) / 0x10000;
                            ai_flee_cell(s, x, y); // FUN_0049e290
                            post_order(s, [=, this] { order_move(s, x, y); });
                            reacted = true;
                            continue;
                        }
                    } else {
                        const int akind = u.attacker <= -1000 ? 2 : 1;
                        const int aid = akind == 2 ? -1000 - u.attacker : u.attacker;
                        if (!ai_object_is_unit_with(akind, aid, 0x400140)) {
                            ai_clear_selection(s);
                            if (u.state != 0x11) u.selected = true; // SelectUnit FUN_0041cd50
                            ai_attack_object(s, u, akind, aid);
                            u.flags |= kEngaged;
                        }
                    }
                    reacted = true;
                }
                continue; // a recently hit unit never reaches the idle part
            }
        }
        if (u.since_stop > 10 && u.counter_2b > 10) {
            bool done = false;
            int tkind = u.target_kind, tid = u.target;
            if (!(u.flags & kEngaged) || ai_recently_in_action(u) || u.since_stop < 11) {
                if (retarget && (rng_.next() % uint32_t(n)) != uint32_t(u.slot) % uint32_t(n)) { // line 0x103D
                    ai_most_appropriate_target(s, u, tkind, tid);
                    if (tkind != 0 && !ai_object_is_unit_with(tkind, tid, 0x400140)) {
                        ai_clear_selection(s);
                        if (u.state != 0x11) u.selected = true; // SelectUnit FUN_0041cd50
                        ai_attack_object(s, u, tkind, tid);
                        u.flags |= kEngaged;
                        done = true;
                    }
                }
            } else {
                ++released;
                u.flags &= ~kEngaged;
                ai_clear_selection(s);
                if (u.state != 0x11) u.selected = true; // SelectUnit FUN_0041cd50
                done = true;
            }
            if (tkind == 0 && ((tick_count_ >> 5) & 7) == (uint32_t(u.slot) & 7)) {
                // Cell objects within 5 cells: FUN_0044e9d0 picks them up
                // (crates vanish, ripe blooms re-arm) and the unit is sent to
                // those of types < 7 or > 11 (the last one wins; doc 31 6.3).
                const int x0 = std::max(u.cur_x - 5, 0), x1 = std::min(u.cur_x + 6, map_.width() - 1);
                const int y0 = std::max(u.cur_y - 5, 0), y1 = std::min(u.cur_y + 6, map_.height() - 1);
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        if (!(map_.at(x, y).flags & 0x1000)) continue;
                        const int t = pick_up_cell_object(x, y);
                        if (t < 7 || t > 11) {
                            ai_clear_selection(s);
                            if (u.state != 0x11) u.selected = true; // SelectUnit FUN_0041cd50
                            post_order(s, [=, this] { order_move(s, x, y); });
                        }
                    }
                }
            }
            if (!done) {
                if (u.state != 0x11) u.selected = true; // SelectUnit FUN_0041cd50
                ++released;
            }
        }
    }
    if (released) ai_commence(s, g);
    return reacted;
}

void World::ai_attack_object(int s, Unit& u, int kind, int id) {
    // FUN_00420010 "AttackObject": the unit's target (+0x26 / +0x0C) is set
    // first. A unit: none when it is an aircraft (flag 0x40, the attacker is
    // cleared); crushers drive onto it within SquishRange squared cells when
    // SquishInfantry is set; else the attack-unit order. A building: the
    // attack-building order and the engaged flag.
    u.target_kind = kind;
    u.target = id;
    if (kind == 2) {
        post_order(s, [=, this] { order_attack_building(s, id); });
        u.flags |= kEngaged;
        return;
    }
    const int ti = kind == 1 ? find_unit(id) : -1;
    if (ti < 0) return; // a freed object: no order
    const Unit& target = units_[size_t(ti)];
    if (target.flags & 0x40) {
        u.attacker = -1;
        return;
    }
    if (ai_u8(s, kSquish)) {
        const uint32_t d = ai_cell_dist2(u, target);
        if (d < ai_u32(s, kSquishRange) && templates_.unit_records[size_t(u.record)].crusher) {
            const int x = (target.x / 32) / 0x10000, y = (target.y / 32) / 0x10000;
            post_order(s, [=, this] { order_move(s, x, y); });
            return;
        }
    }
    post_order(s, [=, this] { order_attack_unit(s, id); });
}

int World::ai_percentage(int s, int g, int pct) const {
    // FUN_0041e5d0 "GetPercentage": MaxBaseStrength (unsigned) with
    // UseIdealStrength, else the group's strength (signed).
    if (ai_u8(s, kUseIdeal)) return int(ai_u32(s, kMaxBaseStrength) * uint32_t(pct) / 100u);
    return int32_t(ai_u32(s, G(g, gStrength))) * pct / 100;
}

int World::ai_defend_group_id(int s, int a) const {
    // FUN_00422230 "GetDefendGroupId": the last in-use group with job 0xF
    // and this defend area, else 0xFF.
    int id = 0xff;
    for (int g = 0; g < 32; ++g) {
        if (ai_u8(s, G(g, gInUse)) && ai_u8(s, G(g, gJob)) == kJobDefendArea && ai_u8(s, G(g, gDefend)) == a) id = g;
    }
    return id;
}

void World::ai_send_group_to_point(int s, int x, int y, int g) {
    // FUN_0041e3c0 "SendGroupToPoint".
    ai_clear_selection(s);
    ai_select_group(s, g, true);
    set_ai_u8(s, G(g, gX), uint8_t(x));
    set_ai_u8(s, G(g, gY), uint8_t(y));
    const uint8_t mx = uint8_t(x), my = uint8_t(y);
    post_order(s, [=, this] { order_move(s, mx, my); }); // FUN_00459450
}

bool World::ai_send_some_units_to_attack_building(int s, int g, int b, int pct) {
    // FUN_00423440 "SendSomeUnitsToAttackBuilding": each unit of the group
    // (list order) with rand() % 100 <= pct (line 0x1C0E) is selected.
    ai_clear_selection(s);
    int n = 0;
    for (auto& u : units_) {
        if (u.owner != s || u.remove || u.ai_group != g) continue;
        if (rng_.next() % 100 <= uint32_t(pct)) {
            ++n;
            if (u.state != 0x11) u.selected = true; // SelectUnit FUN_0041cd50
        }
    }
    if (n == 0) return false;
    post_order(s, [=, this] { order_attack_building(s, b); }); // FUN_00459800
    return true;
}

void World::ai_enemy_base_waypoint(int& x, int& y, int tx, int ty) {
    // FUN_00420b20 "GetEnemyBaseWaypoint" (docs/re/53 section 5.4): about 11
    // cells from the target on the group's side, turned two 32-step facings;
    // byte arithmetic (a wrap below 0 lands above 0xC0). The safety check
    // FUN_00420c60 always passes, so the loop runs once.
    const int a = (angle12(x - tx, ty - y) + 0x40) & 0xfff;
    rng_.next(); // line 0x1394, the value is unused
    const int d = ((a >> 7) - 2) & 0x1f;
    const int32_t dx = flight_dx(d, 0x160), dy = flight_dy(d, 0x160);
    uint8_t nx = uint8_t(tx - dx / 0x10000), ny = uint8_t(ty - dy / 0x10000);
    if (nx > 0xc0) nx = 0;
    else if (nx > map_.width() - 1) nx = uint8_t(map_.width() - 1);
    if (ny > 0xc0) ny = 0;
    else if (ny > map_.height() - 1) ny = uint8_t(map_.height() - 1);
    x = nx;
    y = ny;
}

bool World::ai_group_full_health(int s, int g) const {
    // FUN_0041e860 "GroupAtFullHealth" (an empty group is).
    for (const auto& u : units_) {
        if (u.owner == s && !u.remove && u.ai_group == g &&
            u.hit_points != templates_.unit_records[size_t(u.record)].hit_points) {
            return false;
        }
    }
    return true;
}

void World::ai_monitor_groups(int s, int off) {
    // FUN_0041d070 "MonitorGroups" (doc 23 section 4.6, docs/re/53 section 5).
    const int i = int((tick_count_ + uint32_t(off)) & 0x1f);
    const int base = ai_u8(s, kBaseGroup), defend = ai_u8(s, kDefendGroup);
    if (i == 0) {
        std::array<int, 32> count{};
        for (const auto& u : units_) {
            if (u.owner == s && !u.remove && u.ai_group < 32) ++count[u.ai_group];
        }
        for (int g = 0; g < 32; ++g) {
            if (count[size_t(g)] == 0 && ai_u8(s, G(g, gInUse)) && g != base && g != defend) {
                set_ai_u8(s, G(g, gInUse), 0);
            }
        }
        // Groups above their job's maximum (0x4be350) are halved: the counts
        // are those from before the freeing (not updated after a split);
        // the old record is copied over the splinter's, so both halves go
        // on with the same operation, with the selection as it is.
        static constexpr int kMaxUnits[19] = {200, 400, 60, 30, 30, 20, 30, 30, 60, 60,
                                              40, 20, 60, 60, 20, 500, 20, 10, 10};
        for (int g = 0; g < 32; ++g) {
            const int job = ai_u8(s, G(g, gJob));
            if (job > 18 || count[size_t(g)] <= kMaxUnits[job]) continue;
            const int t = ai_free_group(s);
            if (t <= 0) continue;
            ai_splinter(s, g, t, ai_percentage(s, g, 50)); // rand() line 0x645
            for (size_t k = 0; k < 0x2C; ++k) set_ai_u8(s, G(t, 0x2C) + k, ai_u8(s, G(g, 0x2C) + k));
            ai_target_kind_[size_t(s)][size_t(t)] = ai_target_kind_[size_t(s)][size_t(g)];
            ai_commence(s, t);
        }
    }
    for (int g = 0; g < 32; ++g) { // target checks (FUN_004237a0)
        if (!ai_u8(s, G(g, gInUse)) || ai_u16(s, G(g, gTarget)) == kNone) continue;
        const int side = ai_u8(s, G(g, gSide));
        if (side >= 8 || alliance_[size_t(s)][size_t(side)] != 1 || !ai_target_alive(s, g)) {
            set_ai_u8(s, G(g, gSub), 4);
            set_ai_u16(s, G(g, gTarget), kNone);
        }
    }
    if (i == 0) return;
    if (ai_u8(s, kBeserk) == 1 && i == base) ai_become_rampaging(s, i);
    const int g = i;
    if (ai_u32(s, G(g, gStrength)) == 0 && g != base && ai_u8(s, G(g, gInUse)) && g != defend) {
        set_ai_u8(s, G(g, gInUse), 0);
        return;
    }
    set_ai_u32(s, G(g, gAge), ai_u32(s, G(g, gAge)) + 1);
    set_ai_u32(s, G(g, gStrength), uint32_t(ai_group_strength(s, g)));
    const int job = ai_u8(s, G(g, gJob));
    const uint32_t age = ai_u32(s, G(g, gAge));
    auto quiet_pass = [&](auto on_third) {
        bool in_action = false, exists = false;
        for (const auto& u : units_) {
            if (u.owner != s || u.ai_group != g || u.remove) continue;
            exists = true;
            in_action = in_action || ai_recently_in_action(u);
        }
        if (in_action && ai_u8(s, G(g, gSub)) == 2) set_ai_u8(s, G(g, gSub), 3);
        if (in_action || !exists) {
            set_ai_u32(s, G(g, gIdle), 0);
            return;
        }
        const uint32_t idle = ai_u32(s, G(g, gIdle)) + 1;
        set_ai_u32(s, G(g, gIdle), idle);
        if (idle > 2) on_third();
    };
    if (job == 0x12) { // the saboteur group (docs/re/39 section 4.5)
        Unit* u = nullptr;
        for (auto& v : units_) {
            if (v.owner == s && v.ai_group == g && !v.remove) {
                u = &v;
                break;
            }
        }
        if (u) {
            const auto& r = templates_.unit_records[size_t(u->record)];
            const bool near = target_unit_in_range(u->x >> 16, u->y >> 16, s, 0xc0, 9, r.weapon2, false, true) >= 0;
            if ((u->hit_age < 10 || cell_dist2(*u, ai_u8(s, G(g, gX)), ai_u8(s, G(g, gY))) < 0x40 || near) &&
                u->load == 0xa0 && !(u->flags & 0x10)) {
                u->flags |= 0x10; // stealth on, no sound
            }
        }
    }
    switch (job) {
    case 0x10: // the engineer group and the saboteur group run the job-5 code
    case 0x12:
    case kJobAttackBuilding: {
        // The rand() is drawn for jobs 0x10 / 0x12 too; only job 5 turns rampaging.
        if (age > 10 && ai_stop_start(s, g, 8, true) && (rng_.next() & 0x3f) != 0 && job == kJobAttackBuilding) {
            ai_become_rampaging(s, g);
        }
        if (!ai_morale_ok(s, g)) ai_return_to_base(s, g);
        const int sub = ai_u8(s, G(g, gSub));
        auto stable = [&] {
            for (const auto& u : units_) {
                if (u.owner == s && u.ai_group == g && !u.remove && u.since_stop < 10) return false;
            }
            return true;
        };
        if (sub == 7) {
            if (stable() && ai_u32(s, G(g, gAge)) > 10) {
                int x, y;
                ai_group_position(s, g, x, y);
                if (ai_u8(s, kRetargetChance) < rng_.next() % 100) { // line 0x8C6
                    ai_attack_best_building(s, g, ai_best_side(s), x, y);
                    set_ai_u32(s, G(g, gAge), 0);
                    set_ai_u8(s, G(g, gSub), 8);
                    // First to a point short of the target (FUN_00420b20), then
                    // substate 8 attacks; from the group's average position.
                    ai_enemy_base_waypoint(x, y, ai_u8(s, G(g, gX)), ai_u8(s, G(g, gY)));
                    ai_send_group_to_point(s, x, y, g);
                    return;
                }
                ai_attack_best_building(s, g, ai_u8(s, G(g, gSide)), x, y);
            }
        } else if (sub == 8) {
            if (stable() && ai_u32(s, G(g, gAge)) > 10) {
                int x, y;
                ai_group_position(s, g, x, y);
                ai_attack_best_building(s, g, ai_u8(s, G(g, gSide)), x, y);
            }
        } else if (sub == 4) {
            quiet_pass([&] {
                // Group flag 8 (the defend-area hand-off, FUN_00422230) is
                // never set in 1.06 (docs/re/53 section 5.2).
                int x, y;
                ai_group_position(s, g, x, y);
                ai_attack_best_building(s, g, ai_u8(s, G(g, gSide)), x, y);
            });
        }
        return;
    }
    case kJobReturning:
        if (age > 10) ai_stop_start(s, g, 8, true);
        {
            // Units inside the side's base box leave the group.
            int x0, x1, y0, y1;
            base_box(s, x0, x1, y0, y1);
            for (auto& u : units_) {
                if (u.owner == s && u.ai_group == g && u.cur_x >= x0 && u.cur_x <= x1 && u.cur_y >= y0 &&
                    u.cur_y <= y1) {
                    u.ai_group = 0xff;
                }
            }
        }
        return;
    case kJobRepair:
        // Back to base once every unit is whole (no DoStopStartStuff).
        if (ai_group_full_health(s, g)) ai_return_to_base(s, g);
        return;
    case kJobRampaging:
        if (int32_t(ai_u32(s, G(g, gTimer))) == 0) ai_return_to_base(s, g);
        if (age > 10) ai_stop_start(s, g, 8, true);
        if (ai_u8(s, G(g, gSub)) == 4 && ai_nearest_hostile(s, g)) {
            // 0x41d7e9..0x41d88b: on success ClearUnits, select the group,
            // the timer steps down, then by the target slot's type: a unit
            // -> substate 2 and order 0x14, a building -> substate 2 and
            // order 0x15, else no target. The selection is left as it is.
            ai_clear_selection(s);
            ai_select_group(s, g, false);
            if (int32_t(ai_u32(s, G(g, gTimer))) > 0) {
                set_ai_u32(s, G(g, gTimer), ai_u32(s, G(g, gTimer)) - 1);
            }
            const uint16_t v1 = ai_u16(s, G(g, gTarget));
            const int kind = ai_target_kind_[size_t(s)][size_t(g)]; // the slot type (+0x90) of the target
            if (kind == 1) {
                set_ai_u8(s, G(g, gSub), 2);
                post_order(s, [=, this] { order_attack_unit(s, v1); });
            } else if (kind == 2) {
                set_ai_u8(s, G(g, gSub), 2);
                post_order(s, [=, this] { order_attack_building(s, v1); });
            } else {
                set_ai_u16(s, G(g, gTarget), kNone);
            }
        }
        return;
    case kJobDefendArea:
        ai_defend_area(s, g);
        return;
    default:
        ai_stop_start(s, g, 8, false);
        return;
    }
}

void World::ai_defend_area(int s, int g) {
    // MonitorGroups job 0xF (docs/re/53 section 5.3): nothing before the
    // first build list (+0x19EC); a quiet group patrols its area half the
    // time, else the area grown by 4 cells is scanned for enemies; then
    // the base area follows the base box and sets the emergency flag.
    if (!ai_u8(s, 0x19EC)) return;
    const int a = ai_u8(s, G(g, gDefend));
    const size_t A = kAreas + size_t(a) * 0x14; // +0 x0, +1 x1, +2 y0, +3 y1
    const int W = map_.width(), H = map_.height();
    bool seen = false;
    ai_stop_start(s, g, 8, true);
    bool in_action = false;
    for (const auto& u : units_) in_action = in_action || (u.owner == s && !u.remove && u.ai_group == g && ai_recently_in_action(u));
    if (!in_action && (rng_.next() & 1) == 0 && ai_u8(s, A + 3) != ai_u8(s, A + 2)) { // line 0xA4B
        int bx0, bx1, by0, by1;
        base_box(s, bx0, bx1, by0, by1);
        for (auto& u : units_) {
            if (u.owner != s || u.remove) continue;
            const bool out = u.cur_x > bx1 || u.cur_x < bx0 || u.cur_y > by1 || u.cur_y < by0 ||
                             (map_.at(u.cur_x, u.cur_y).flags & 0x10000); // the player's box, or sand
            if (u.ai_group != g || (u.state != 2 && u.state != 3)) continue;
            if (!((rng_.next() & 0x7f) == 0 || u.since_stop > 200 || out)) continue; // line 0xA57
            const uint32_t ax0 = ai_u8(s, A + 0), ay0 = ai_u8(s, A + 2);
            const uint32_t mx = ai_u8(s, A + 1) - ax0, my = ai_u8(s, A + 3) - ay0; // u32 (wraps when inverted)
            const uint32_t rx = rng_.next(); // line 0xA60
            const uint32_t ry = rng_.next(); // line 0xA61
            // Unsigned DIV at 0x41dc33 / 0x41dc6c, the cell kept as an int. A
            // zero width (my == 0 is excluded by the y test above) is a
            // division fault in the exe (a crash); an inverted area gives
            // x = rand + x0, and the exe reads the cell flags past the map's
            // rows (0x547df0 row table, 0x517df4 cells), i.e. unrelated
            // memory. Both tries are skipped here: a crash fix, and a known
            // deviation that cannot be reproduced (the inverted base area
            // of the four AI sides that start empty, docs/re/53 section 1.6).
            if (mx == 0 || my == 0) continue;
            const int x = int(rx % mx + ax0), y = int(ry % my + ay0);
            if (x < 0 || y < 0 || x >= W || y >= H) continue;
            const uint32_t c = map_.at(x, y).flags;
            // The rebuild rectangle test (+0x0B; no writer of 1 found) keeps
            // the exe's operators.
            const bool rect_ok = ai_u8(s, 0x0B) == 0 ||
                                 ((x < int32_t(ai_u32(s, 0xF2C)) || x > int32_t(ai_u32(s, 0xF34)) ||
                                   y < int32_t(ai_u32(s, 0xF30))) &&
                                  y > int32_t(ai_u32(s, 0xF38)));
            if ((c & 0x103f8) == 0 && (c & 0x2000) && rect_ok) {
                ai_clear_selection(s);
                if (u.state != 0x11) u.selected = true;
                post_order(s, [=, this] { order_move(s, x, y); }); // FUN_00459450
            }
        }
    } else {
        const int x0 = std::max(int(ai_u8(s, A + 0)) - 4, 0), x1 = std::min(int(ai_u8(s, A + 1)) + 4, W - 1);
        const int y0 = std::max(int(ai_u8(s, A + 2)) - 4, 0), y1 = std::min(int(ai_u8(s, A + 3)) + 4, H - 1);
        // The exe computes a per-pass row start ((tick >> 5) + pass) %
        // (y1 - y0) that it never uses. It faults only for y1 == y0, i.e.
        // an area inverted by exactly 8 rows; not computed here (a crash fix).
        for (int pass = 0; pass < 4; ++pass) {
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const uint32_t c = map_.at(x, y).flags;
                    if (alliance_[size_t(s)][c & 7] != 1) continue;
                    if ((c & 0x3e8) == 0) {
                        if (!(c & 0x10)) continue;
                        const int bi = building_at_cell(x, y); // FUN_004a5640
                        if (bi < 0) continue;
                        const Building& b = buildings_[size_t(bi)];
                        if ((b.flags & 0x4000) || templates_.building_records[size_t(b.record)].kind == 0xe) continue;
                        ai_send_some_units_to_attack_building(s, g, bi, 40);
                        return; // the whole MonitorGroups pass
                    }
                    bool inf;
                    const int vi = unit_in_cell(x, y, inf); // FUN_004a5270(x * 32, y * 32, .., 0)
                    if (vi < 0 || (units_[size_t(vi)].flags & 4)) continue; // cloaked
                    seen = true;
                    const int kind = templates_.unit_records[size_t(units_[size_t(vi)].record)].kind;
                    const int k = kind == 4 ? 6 : kind == 3 ? 4 : kind == 0x10 ? 6 : 2;
                    for (int j = 0; j < k; ++j) {
                        if (ai_send_nearest_unit(s, g, x, y) && ai_u8(s, kDefendOn) &&
                            a == int(int8_t(ai_u8(s, kBaseArea))) && (tick_count_ & 0xf) == 0) {
                            set_ai_u32(s, kBuildingAttackTimer, ai_u32(s, kBuildingAttackTimer) + 1);
                        }
                    }
                }
            }
        }
    }
    if (!ai_u8(s, kDefendOn) || a != int(int8_t(ai_u8(s, kBaseArea)))) return;
    set_ai_u8(s, kEmergency, seen ? 1 : 0);
    if ((tick_count_ & 0xe0) != 0) return;
    if (ai_u8(s, kDefendGroup) == 0xff) set_ai_u8(s, kDefendGroup, uint8_t(g));
    int bx0, bx1, by0, by1;
    base_box(s, bx0, bx1, by0, by1);
    set_ai_u8(s, A + 2, uint8_t(by0));
    set_ai_u8(s, A + 3, uint8_t(by1));
    set_ai_u8(s, A + 0, uint8_t(bx0));
    set_ai_u8(s, A + 1, uint8_t(bx1));
    bool buildings = false;
    for (const auto& b : buildings_) buildings = buildings || (b.owner == s && !b.removed);
    uint32_t wanted = 0;
    if (buildings) {
        // x87: FILD dword (signed), FSQRT, FILD qword (u32), FMULP, ftol.
        // Same in double: sqrt(area) * size is either exact (a square area)
        // or at least 1 / (2k) from any integer k (area * size^2 - k^2 is a
        // nonzero integer), far above the rounding error while k < 3e7; area
        // <= 255^2 and every mission's size <= 1000 (test "GuardGroupSize").
        const int32_t area = (int32_t(ai_u8(s, A + 3)) - int32_t(ai_u8(s, A + 2))) *
                             (int32_t(ai_u8(s, A + 1)) - int32_t(ai_u8(s, A + 0)));
        if (area >= 0) wanted = uint32_t(int64_t(std::sqrt(double(area)) * double(ai_u32(s, kGuardGroupSize))));
        // A negative area: FSQRT gives a NaN and ftol 0x80000000_00000000 (low dword 0).
    }
    set_ai_u32(s, A + 8, wanted);
}

void World::ai_defend_checks(int s) {
    // FUN_004214a0 "DoDefendChecks" (docs/re/53 section 6): per defend area
    // every period ticks, a new defend group of 20 % of the wanted strength
    // from the base group, or a top-up of the area's group; both through
    // CreateSplinterGroup (rand() line 0x645). +0x1D07 is set by the
    // area's MonitorGroups pass, not here.
    if (ai_u8(s, kBeserk) != 0) return;
    const int n = std::min<int>(ai_u8(s, kAreaCount), 5); // 5 records fit in the block
    const int base = ai_u8(s, kBaseGroup);
    for (int a = 0; a < n; ++a) {
        const size_t A = kAreas + size_t(a) * 0x14;
        const uint32_t period = ai_u32(s, A + 0xC);
        // A zero period would be a division fault in the exe. The 1.06
        // missions' areas carry 1000 (A4V1, H4V1) and ActivateAI's base area
        // gets 100 (0x41b579); no other code of the binary-mission path
        // writes it (docs/re/53 section 11, golden test).
        if (period == 0 || tick_count_ % period != 0) continue;
        const int32_t wanted = int32_t(ai_u32(s, A + 8));
        const int ax0 = ai_u8(s, A + 0), ax1 = ai_u8(s, A + 1), ay0 = ai_u8(s, A + 2), ay1 = ai_u8(s, A + 3);
        const int cx = uint8_t(ax0 + (ax1 - ax0) / 2), cy = uint8_t(ay0 + (ay1 - ay0) / 2);
        const int g = ai_defend_group_id(s, a);
        if (g == 0xff) {
            const int t = ai_free_group(s);
            if (t <= 0) continue;
            ai_splinter(s, base, t, wanted * 20 / 100); // the strength uses t's old job
            if (ai_u32(s, G(t, gStrength)) == 0) {
                set_ai_u8(s, G(t, gInUse), 0);
                continue;
            }
            ai_clear_selection(s);
            ai_select_group(s, t, false);
            ai_send_group_to_point(s, cx, cy, t);
            set_ai_u8(s, G(t, gJob), kJobDefendArea);
            set_ai_u8(s, G(t, gDefend), uint8_t(a));
            set_ai_u16(s, G(t, gTarget), kNone);
        } else {
            const int32_t now = int32_t(ai_u32(s, G(g, gStrength)));
            if (now >= wanted) continue;
            const int32_t need = std::min(wanted - now, wanted * 20 / 100);
            const int t = ai_free_group(s);
            if (t <= 0) continue;
            ai_splinter(s, base, t, need);
            if (ai_u32(s, G(t, gStrength)) != 0) {
                ai_select_group(s, t, false);
                ai_send_group_to_point(s, cx, cy, t);
                ai_merge_groups(s, t, g); // FUN_00421710
            }
            set_ai_u8(s, G(t, gInUse), 0);
        }
    }
}

void World::ai_defence(int s) {
    // FUN_0041c270 "DoDefence": each own building, in its DefendTicks slot,
    // hit within DefendTicks x 8 ticks: +0x08 = 1 and, with a defend area,
    // the defend group's nearest idle unit goes to the attacking unit.
    const uint32_t ticks = std::max<uint32_t>(ai_u32(s, 0x1D38), 1);
    for (const auto& b : buildings_) {
        if (b.owner != s || b.removed) continue;
        if (uint32_t(b.slot) % ticks != tick_count_ % ticks || b.hit_age >= ticks) continue;
        set_ai_u8(s, 0x08, 1);
        if (!ai_u8(s, kDefendOn) || b.attacker < 0) continue;
        // 0x41c2e8: the first attacker's slot (+0x50 / +0x52), never
        // cleared: kind 1 (a unit) and not an aircraft, no state test. After
        // that unit is gone, whatever unit of that side holds the slot now
        // is the one defended against (docs/re/50 section 6).
        const Unit* a = unit_in_slot(b.attacker_side, b.attacker);
        if (!a || (a->flags & 0x40)) continue;
        ai_send_nearest_unit(s, ai_u8(s, kDefendGroup), a->cur_x, a->cur_y);
    }
}

bool World::ai_send_nearest_unit(int s, int g, int x, int y) {
    // FUN_00423360 "SendNearestUnit": idle (state 2) units of group g,
    // nearest first; infantry (record +0x11) win over vehicles. The metric
    // FUN_00421800 is the squared distance from the unit's pixel cell
    // ((x / 32) / 65536) to (x, y). ClearUnits, select the pick, order the
    // move; the selection is left as it is (0x423360).
    Unit* best = nullptr;
    uint32_t best_d = 0xfffff;
    bool best_inf = false;
    for (auto& u : units_) {
        if (u.owner != s || u.remove || u.ai_group != g) continue;
        const bool inf = templates_.unit_records[size_t(u.record)].infantry;
        const int dx = (u.x / 32) / 0x10000 - x, dy = (u.y / 32) / 0x10000 - y;
        const uint32_t d = uint32_t(dx * dx + dy * dy);
        if ((d < best_d && u.state == state::kIdle && inf == best_inf) || (inf && !best_inf)) {
            best = &u;
            best_d = d;
            best_inf = inf;
        }
    }
    if (!best) return false;
    ai_clear_selection(s);
    if (best->state != 0x11) best->selected = true; // SelectUnit FUN_0041cd50 skips a dying unit
    post_order(s, [=, this] { order_move(s, x, y); });
    return true;
}

void World::ai_beserk(int s) {
    // The BESERK tail of DoAI (doc 22 section 3.9, doc 23 section 6.1).
    const int b = ai_u8(s, kBeserk);
    if (b != 1 && b != 2) return;
    for (auto& bl : buildings_) {
        if (bl.owner != s || bl.removed || (bl.flags & 0x4000006)) continue;
        if ((rng_.next() & 0x1f) != 0) continue; // line 0x1C0, drawn for walls too (docs/re/53 section 7.1)
        if (templates_.building_records[size_t(bl.record)].kind == 0xe) continue;
        post_order(s, [=, this] { order_sell(s, bl.index); });
        bl.flags |= 0x4000000;
    }
    // The group of this tick turns rampaging: job, timer, substate, target,
    // side 0 and its position; the age and the in-use byte stay.
    const int g = int(tick_count_ & 0x1f);
    if (ai_u8(s, G(g, gJob)) != kJobRampaging && ai_u8(s, G(g, gInUse)) == 1) {
        set_ai_u8(s, G(g, gJob), kJobRampaging);
        set_ai_u32(s, G(g, gTimer), 10000); // FUN_00423820
        set_ai_u8(s, G(g, gSub), 4);
        set_ai_u16(s, G(g, gTarget), kNone);
        set_ai_u8(s, G(g, gSide), 0);
        int x, y;
        ai_group_position(s, g, x, y);
        set_ai_u8(s, G(g, gX), uint8_t(x));
        set_ai_u8(s, G(g, gY), uint8_t(y));
    }
}

void World::ai_protection(int s) {
    // FUN_0041c070 "DoProtection" (docs/re/29 section 1): a protected unit
    // (ProtectUnit[record]) hit within the last ProtectTicks x 8 ticks
    // gets an escort: a Rampaging group split from the Base group at its
    // cell, which MonitorGroups then sends at the nearest hostile.
    const uint32_t T = ai_u32(s, 0x1D34); // ProtectTicks
    if (T == 0) return;                    // the exe would divide by zero
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& u = units_[i];
        if (u.owner != s || u.remove) continue;
        if (uint32_t(u.slot) % T != tick_count_ % T) continue; // +0x8c: the side's pool slot (docs/re/50)
        if (!ai_u8(s, 0x1CC8 + size_t(u.record % 60))) continue;
        int protecting = 0; // NumberOfGroupsDoingTask(3): no 1.06 code writes job 3
        for (int g = 0; g < 32; ++g) protecting += ai_u8(s, G(g, gInUse)) && ai_u8(s, G(g, gJob)) == 3;
        if (protecting >= ai_u8(s, 0xF43)) return;
        if (u.hit_age >= T || (u.flags & 8) || u.state == 0x12) continue;
        u.flags |= 8;
        const uint32_t r = rng_.next(); // line 0x40B
        bool lifted = false;
        if (r % 100 <= ai_u8(s, 0x1D1F)) {
            // FindCarryallSlot, then the base position as the destination and
            // a kind-1 request. The unit is never put in 0x12, so no carryall
            // ever lifts it directly (docs/re/33 section 0).
            const int slot = find_carry_slot(s, u);
            if (slot != 0xff) {
                int bx = map_.width() / 2, by = map_.height() / 2;
                ai_base_position(s, bx, by); // FUN_0041e180 overwrites both
                u.dest_x = uint8_t(bx);
                u.dest_y = uint8_t(by);
                if (add_to_queue(s, u, slot, 1, u.state)) lifted = true;
            }
        }
        if (lifted && !ai_u8(s, 0x1D44)) continue;
        const int from = ai_u8(s, kBaseGroup);
        const int pct = ai_u8(s, 0xF3E); // ProtectStrength
        // GetPercentage (FUN_0041e5d0).
        const int32_t strength = ai_u8(s, 0x1D20) ? int32_t(ai_u32(s, 0xF4C) * uint32_t(pct) / 100u)
                                                  : int32_t(int32_t(ai_u32(s, G(from, gStrength))) * pct / 100);
        const int g = ai_free_group(s);
        if (g <= 0) continue;
        ai_splinter(s, from, g, strength); // rand() line 0x645; the strength uses g's old job
        set_ai_u8(s, G(g, gJob), uint8_t(kJobRampaging));
        set_ai_u32(s, G(g, gTimer), 10000);
        set_ai_u8(s, G(g, gSub), 4);
        set_ai_u16(s, G(g, gTarget), kNone);
        const Unit& pu = units_[i];
        set_ai_u8(s, G(g, gX), uint8_t((pu.x >> 16) >> 5));
        set_ai_u8(s, G(g, gY), uint8_t((pu.y >> 16) >> 5));
    }
}

void World::ai_overrides(int s) {
    // FUN_00422c20 "DoOverrides" (docs/re/29 section 2): storage full,
    // and in a power shortage repair the producers or sell one building.
    if (ai_u8(s, 0x07)) return; // DontSell: the whole routine
    if ((tick_count_ & 7) != uint32_t(s)) return;
    set_ai_u8(s, 0x08, 0);
    set_ai_u8(s, 0x09, 0);
    auto& pl = players_[size_t(s)];
    if (pl.spice == pl.capacity) {
        set_ai_u8(s, 0x09, 1);
        set_ai_u8(s, 0x08, 1);
    }
    if (pl.building_list.empty()) return;
    const int refineries = owned_building_kind(s, 4);
    const int harvesters = owned_kind(s, 1);
    if (uint32_t(pl.spice) + uint32_t(pl.cash) == 0) {
        set_ai_u8(s, 0x06, 1);
        if (refineries != 0 || harvesters == 0) return;
    }
    set_ai_u8(s, 0x06, 0);
    if (uint32_t(pl.power_produced) >= uint32_t(pl.power_used)) return;
    bool any = false;
    for (auto& b : buildings_) {
        if (b.owner != s || b.removed) continue;
        const auto& r = templates_.building_records[size_t(b.record)];
        if (r.power < 0 && b.hit_points < r.hit_points) {
            b.flags |= 1; // repair (an OR, not the order's toggle)
            any = true;
        }
    }
    set_ai_u8(s, 0x09, 1);
    if (any || uint32_t(pl.spice) + uint32_t(pl.cash) >= 400) return;
    Building* pick = nullptr;
    for (auto& b : buildings_) { // (a) needs power to fire (large gun turrets)
        if (b.owner == s && !b.removed && templates_.building_records[size_t(b.record)].needs_power) {
            pick = &b;
            break;
        }
    }
    if (!pick) { // (b) below 10 % and hit recently
        // FILD hp, FILD qword max, FMUL 0.1f, FCOMPP (0x422daf..0x422dd3): the
        // product is exact at the exe's 53 bits, so hp = max / 10 qualifies
        // (a float32 product rounds to max / 10 and would not; docs/re/29).
        for (auto& b : buildings_) {
            if (b.owner != s || b.removed) continue;
            const auto& r = templates_.building_records[size_t(b.record)];
            if (double(b.hit_points) < double(uint32_t(r.hit_points)) * double(0.1f) && b.hit_age < 10) {
                pick = &b;
                break;
            }
        }
    }
    if (!pick) { // (c) the highest sell priority, the first on a tie
        int best = 0;
        for (auto& b : buildings_) {
            if (b.owner != s || b.removed) continue;
            const int v = templates_.building_records[size_t(b.record)].sell_priority;
            if (v > best) {
                best = v;
                pick = &b;
            }
        }
    }
    if (!pick) return;
    if (!(pick->flags & 0x400000E) && !pick->removed) {
        const int index = pick->index;
        order_sell(s, index); // the sell handler FUN_00456ee0, called directly
        buildings_[size_t(index)].flags |= 0x4000000;
    }
}

void World::ai_build_carryalls(int s) {
    // FUN_004203d0 (docs/re/29 section 3): every tick.
    const auto& pl = players_[size_t(s)];
    if (uint32_t(pl.cash + pl.spice) < ai_u32(s, 0x1064)) return; // CashStash[BuildCarryalls]
    if (owned_kind(s, 2) >= owned_building_kind(s, 4) * int(ai_u8(s, 0x1D05))) return;
    const auto* ids = templates_.block(0x6d60d8);
    if (!ids || ids->bytes.empty()) return;
    const int rec = templates_.unit_record(ids->bytes[0], pl.house); // FUN_0046c570
    if (rec < 0 || !ai_can_build_unit(s, rec)) return;
    if (owned_kind(s, 1) == 0) return; // ShouldIBuildCarryall: owns a harvester
    post_order(s, [=, this] { start_unit(s, rec); }); // order 0x27
}

void World::ai_unit_facings(int s) {
    // FUN_00420c80 "DoUnitFacings" (docs/re/53 section 11): with a primary
    // construction yard (+0x26098 != 0xFFFF), every unit of the side (its
    // object list, dying and carried ones included) gets body, turret and
    // turret-goal facing pointing AWAY from the base centre (FUN_0046e3f0,
    // cell units), outward like sentries:
    // FUN_0049e160((tx * 0x200000 - x) / 65536, (y - ty * 0x200000) / 65536) / 128,
    // each division truncated toward zero. With FUN_0049e160's convention
    // (dx = from.x - to.x, dy = to.y - from.y; see facing8) that is the
    // direction from the base corner to the unit. Units the mission script
    // adds after activation are not turned (they keep CreateUnit's 0).
    if (players_[size_t(s)].primary[2] < 0) return;
    int tx, ty;
    if (!retreat_destination(s, tx, ty)) return;
    for (auto& u : units_) {
        if (u.owner != s || u.remove) continue;
        const int32_t dx = int32_t(uint32_t(tx & 0xff) * 0x200000u) - u.x;
        const int32_t dy = u.y - int32_t(uint32_t(ty & 0xff) * 0x200000u);
        const int f = angle12(dx / 65536, dy / 65536) / 128;
        u.facing = uint8_t(f);
        u.turret_facing = uint8_t(f);
        u.turret_goal = uint8_t(f);
    }
}

void World::ai_activate_groups(int s) {
    // The group part of ActivateAI (doc 23 sections 3.1 and 5.4).
    for (size_t o = 0x2C; o < 0x2C + 32 * 0x2C; ++o) ai_[size_t(s)][o] = 0;
    for (int g = 0; g < 32; ++g) {
        set_ai_u16(s, G(g, gTarget), kNone);
        ai_target_kind_[size_t(s)][size_t(g)] = 0;
    }
    for (auto& u : units_) {
        if (u.owner == s) u.ai_group = 0xff;
    }
    set_ai_u8(s, G(0, gInUse), 1);
    set_ai_u8(s, kGuardGroup, 1);
    set_ai_u8(s, G(1, gInUse), 1);
    set_ai_u8(s, G(1, gJob), kJobGuarding);
    set_ai_u8(s, kBaseGroup, 2);
    set_ai_u8(s, G(2, gInUse), 1);
    set_ai_u8(s, G(2, gJob), kJobBase);
    if (ai_u8(s, 0x03)) set_ai_u8(s, 0x19EC, 0); // practice (docs/re/53 section 1.4); DoMCVs sets it back
    set_ai_u8(s, kGuardBuildings, 0); // cleared at activation (0x41b4db)
    set_ai_u8(s, kDefendGroup, 0xff);
    // Not in skirmish / network (0x41b2f6, docs/re/53 section 1.4): there the
    // starting units join the base group through the periodic GroupNewUnits.
    if (campaign) ai_group_new_units(s, 1, true); // every map unit guards (group 1)
    ai_unit_facings(s); // FUN_00420c80 (0x41b464), campaign and skirmish
    // The guarding group's strength with job 1's weights (FUN_0041c340(guard, 1)).
    const uint32_t gs = uint32_t(ai_group_strength_job(s, 1, kJobBase));
    set_ai_u32(s, G(1, gStrength), gs);
    set_ai_u32(s, G(1, gStart), gs);
    int x, y;
    ai_base_position(s, x, y);
    set_ai_u8(s, kBaseX, uint8_t(x));
    set_ai_u8(s, kBaseY, uint8_t(y));
    if (ai_u32(s, kGuardGroupSize) == 0) set_ai_u8(s, kDefendOn, 0);
    if (!ai_u8(s, kDefendOn)) set_ai_u8(s, kBaseArea, 0xff);
    if (ai_u8(s, kDefendOn) && ai_u8(s, kAreaCount) < 5) { // the exe has no limit (5 records fit the block)
        // The base's defend area: the side's base box, period 100, wanted 1000.
        const int a = ai_u8(s, kAreaCount);
        const size_t A = kAreas + size_t(a) * 0x14;
        int x0, x1, y0, y1;
        base_box(s, x0, x1, y0, y1);
        set_ai_u8(s, A + 0, uint8_t(x0));
        set_ai_u8(s, A + 1, uint8_t(x1));
        set_ai_u8(s, A + 2, uint8_t(y0));
        set_ai_u8(s, A + 3, uint8_t(y1));
        set_ai_u32(s, A + 8, 1000);
        set_ai_u32(s, A + 0xC, 100);
        set_ai_u32(s, A + 0x10, 0xffffffff);
        set_ai_u8(s, kBaseArea, uint8_t(a));
        set_ai_u8(s, kAreaCount, uint8_t(a + 1));
    }
    // FreePercentage (+0xF0D): that share of the guarding group's strength
    // leaves every group (CreateSplinterGroup to 0xFF, rand() line 0x645).
    // 100 for A4V1 side 4 and H4V1 sides 3 and 4, 0 elsewhere (and in
    // _PRAC.MIS). The exe's group 0xFF record lies past this side's block:
    // it reads the job at block(s + 1) + 0xE64 and writes block(s + 1)
    // +0xE54..+0xE6E, i.e. initial-building entries 138 and 139 of the next
    // side, which only entries below its count (< 139) are ever read. The
    // job byte is 0 in those three missions and nothing writes it, so job
    // 0's weights apply (docs/re/53 section 11, golden test).
    const int32_t fp = int32_t(ai_u32(s, G(1, gStrength))) * int32_t(ai_u8(s, 0xF0D)) / 100;
    if (fp != 0) {
        const int q = ai_u8(s, kGroupSizeRandom);
        int32_t limit = fp;
        const uint32_t r = rng_.next();
        if (q > 0) limit = x87::percent_of(int32_t(q - int32_t(r % uint32_t(2 * q))), fp);
        int sum = 0;
        for (auto& u : units_) {
            if (u.owner != s || u.ai_group != 1 || u.remove || u.state == 8) continue;
            if (uint32_t(sum) >= uint32_t(limit)) break; // unsigned, as CreateSplinterGroup
            const int kind = templates_.unit_records[size_t(u.record)].kind;
            if (kind == 1 || kind == 2) continue;
            u.ai_group = 0xff;
            sum += ai_unit_strength(s, u, 0);
        }
    }
    ai_clear_selection(s);
}

} // namespace d2k::sim
