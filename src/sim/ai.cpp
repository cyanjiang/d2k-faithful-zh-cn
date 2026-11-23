// The computer player: activation, unit production, harvester reactions
// and building repair flags. The AI block is kept as the exe's raw 0x1DB8
// bytes per player slot so the documented offsets apply directly.
// RE: docs/re/22-ai-core.md (core), docs/re/23-ai-teams.md (groups).
#include <algorithm>
#include <cmath>
#include <cstring>

#include "sim/world.hpp"
#include "sim/x87.hpp"

namespace d2k::sim {

namespace ai {
// Settings and runtime fields (doc 22 sections 1.2 / 1.3).
constexpr size_t kActive = 0x01, kPractice = 0x03, kStorageFull = 0x08, kMaySpend = 0x09, kRandomRange = 0x0A;
constexpr size_t kRepairCounter = 0x10, kBeserk = 0x1E;
constexpr size_t kMaxRepairUnits = 0x1D08, kUnitRepairTicks = 0x1D10, kRepairGuarding = 0x1D15;
constexpr size_t kUnitRepairValue = 0x1AE8; // f32[60] by unit record
constexpr size_t kFirstAttackBuildingDelay = 0xF10, kBuildingAttackTimer = 0xF18;
constexpr size_t kFirstAttackUnitDelay = 0xF1C, kUnitAttackTimer = 0xF24;
constexpr size_t kEmergencyBuildRate = 0xF28, kHarvestersWanted = 0xF54;
constexpr size_t kTimingRandom = 0xF6E, kMoraleFactor = 0xF68;
constexpr size_t kCashStash = 0x1054; // u32[9 jobs]
constexpr size_t kBuildingRepairValue = 0x13AC; // f32[100]
constexpr size_t kUnitBuildPriority = 0x19F8;   // f32[60]
constexpr size_t kHarvestersPerRefinery = 0x1D04;
constexpr size_t kBuildingRepairTicks = 0x1D0C, kEmergency = 0x1D1D;
constexpr size_t kUnitOrderCounter = 0x1D28, kUnitBuildRate = 0x1D2C;
constexpr int kJobBuildUnits = 6;
} // namespace ai

uint8_t World::ai_u8(int s, size_t o) const { return ai_[size_t(s)][o]; }
void World::set_ai_u8(int s, size_t o, uint8_t v) { ai_[size_t(s)][o] = v; }
uint32_t World::ai_u32(int s, size_t o) const {
    uint32_t v;
    std::memcpy(&v, &ai_[size_t(s)][o], 4);
    return v;
}
void World::set_ai_u32(int s, size_t o, uint32_t v) { std::memcpy(&ai_[size_t(s)][o], &v, 4); }
float World::ai_f32(int s, size_t o) const {
    float v;
    std::memcpy(&v, &ai_[size_t(s)][o], 4);
    return v;
}

void World::set_ai(const formats::MissionFile& mis) {
    // The binary .MIS fills 0x4f4118 directly (file +0x58, 8 x 0x1DB8).
    for (size_t s = 0; s < 8; ++s) {
        std::fill(ai_[s].begin(), ai_[s].end(), uint8_t(0));
        if (s < mis.ai.size() && mis.ai[s].size() == ai_[s].size()) {
            std::copy(mis.ai[s].begin(), mis.ai[s].end(), ai_[s].begin());
        }
    }
    // The mission set-up runs RebuildLists for every player (0x448a89) after
    // the map objects and the skirmish start units exist and right before
    // the ActivateAI loop (0x448b06); our init_players runs before the
    // skirmish start, so the base box (all ActivateAI reads of it) is
    // taken again here.
    for (int p = 0; p < 8; ++p) snapshot_base_box(p);
    for (int s = 0; s < 8; ++s) {
        if (ai_u8(s, ai::kActive)) ai_activate(s);
    }
}

void World::ai_activate(int s) {
    // FUN_0041b250 "ActivateAI" (doc 22 section 2.1); the rand() calls in order.
    const uint32_t r1 = rng_.next();
    const float morale = float(int(r1 % 30 + 60)) * 0.01f;
    std::memcpy(&ai_[size_t(s)][ai::kMoraleFactor], &morale, 4);
    // +0x1D46 = FUN_004a6070(6, side): the first building record (0..99) of
    // kind 6 (repair pad) whose house mask has bit `side` (the side number,
    // quirk); its name group, else 0xFF (docs/re/53 section 1.4).
    {
        uint8_t g = 0xff;
        for (size_t r = 0; r < templates_.building_records.size() && r < 100; ++r) {
            const auto& br = templates_.building_records[r];
            if (br.kind == 6 && (br.house_mask & (1u << s))) {
                g = br.name_index;
                break;
            }
        }
        set_ai_u8(s, 0x1D46, g);
    }
    const int t = ai_u8(s, ai::kTimingRandom);
    if (t > 0) {
        const uint32_t r2 = rng_.next();
        const float f2 = float(int(t - int(r2 % uint32_t(2 * t)) + 100)) * 0.01f;
        set_ai_u32(s, ai::kBuildingAttackTimer,
                   uint32_t(int32_t(f2 * float(int32_t(ai_u32(s, ai::kFirstAttackBuildingDelay))))));
        const uint32_t r3 = rng_.next();
        const float f3 = float(int(t - int(r3 % uint32_t(2 * t)) + 100)) * 0.01f;
        set_ai_u32(s, ai::kUnitAttackTimer,
                   uint32_t(int64_t(double(int64_t(ai_u32(s, ai::kFirstAttackUnitDelay))) * double(f3))));
    }
    if (difficulty == 2) { // hard: UnitBuildRate x 0.75 (double constant 0x4be3c0)
        set_ai_u32(s, ai::kUnitBuildRate, uint32_t(int64_t(double(ai_u32(s, ai::kUnitBuildRate)) * 0.75)));
    }
    ai_initial_buildings(s); // FUN_0041e920 (doc 22 section 3.4)
    ai_activate_groups(s);
}

int World::owned_units(int p, int record) const {
    // +0x24628[rec]: CreateUnit adds 1 (0x455af6); only the free after the
    // death countdown takes it off (FUN_00458e90, 0x458f6d). So a dying unit
    // (state 0x11) still counts, and so does a delivery carryall.
    int n = 0;
    for (const auto& u : units_) n += u.owner == p && u.record == record && !u.remove;
    return n;
}

int World::owned_kind(int p, int kind) const {
    // The side's kind counters, taken off at the same free (0x458f33 /
    // 0x458f47): harvesters +0x245b8 (kind 1), carryalls +0x245c0 (kind 2)
    // without the delivery flag 0x800 (the delivery code undoes its +1,
    // FUN_0046def0, docs/re/13 section 5.2). Dying units count.
    int n = 0;
    for (const auto& u : units_) {
        if (u.owner != p || u.remove || templates_.unit_records[size_t(u.record)].kind != kind) continue;
        if (kind == 2 && (u.flags & 0x800)) continue;
        ++n;
    }
    return n;
}

int World::owned_building_kind(int p, int kind) const {
    // The exe reads the name-group counter P+0x245C1[g] (kind 4 = group 7,
    // records 20..22 in 1.06): +1 in CreateBuilding (0x456680), -1 in
    // FUN_0044dee0 when the building leaves the map (0x49bdf9) or at once
    // when it is captured (0x46d78c): the old object (flag 0x10000) no
    // longer counts.
    int n = 0;
    for (const auto& b : buildings_) {
        n += b.owner == p && !b.removed && !(b.flags & 0x10000) &&
             templates_.building_records[size_t(b.record)].kind == kind;
    }
    return n;
}

bool World::unit_slot_free(int p, int record) const {
    // FUN_004430d0(side, rec, 1): one unit per factory group in production,
    // except for unit kinds 9, 10, 4 and 0xc.
    auto special = [&](int r) {
        const int k = templates_.unit_records[size_t(r)].kind;
        return k == 9 || k == 10 || k == 4 || k == 0xc;
    };
    if (special(record)) return true;
    const int group = templates_.unit_records[size_t(record)].prereq1;
    for (const auto& s : players_[size_t(p)].unit_slots) {
        if (s.item < 0) continue;
        if (templates_.unit_records[size_t(s.item)].prereq1 == group && !special(s.item)) return false;
    }
    return true;
}

bool World::ai_can_build_unit(int s, int record) const {
    // FUN_0041b850 "CanBuildUnit(AI)".
    if (!unit_slot_free(s, record)) return false;
    const auto& pl = players_[size_t(s)];
    if (uint32_t(pl.cash + pl.spice) < uint32_t(unit_cost(s, record))) return false;
    const auto& list = pl.unit_list;
    return std::find(list.begin(), list.end(), record) != list.end();
}

int World::ai_best_unit(int s) {
    // FUN_0041b910 "FindBestUnitToBuild" (doc 22 section 3.2).
    const auto& pl = players_[size_t(s)];
    const auto* ids = templates_.block(0x6d60d8);
    const int harvester = ids && ids->bytes.size() > 0x10 ? ids->bytes[0x10] : -1; // 0x6d60e8: the HARVESTER unit record (rules loader 0x4688eb)
    const bool want_harv = harvester >= 0 &&
                           owned_kind(s, 1) < owned_building_kind(s, 4) * int(ai_u8(s, ai::kHarvestersPerRefinery));
    set_ai_u8(s, ai::kHarvestersWanted, want_harv ? 1 : 0);
    if (want_harv && ai_can_build_unit(s, harvester)) return harvester;
    // Practice (+0x03) without a primary yard and without an MCV: an MCV on
    // rand() & 7 == 1 (line 0x24F), drawn only when the three hold; it skips
    // ShouldIBuildUnit (docs/re/53 section 4). The MCV counter +0x24628 counts
    // a dying MCV (verified, see owned_units).
    const int mcv = ids && ids->bytes.size() > 0xf ? ids->bytes[0xf] : -1; // 0x6d60e7
    if (ai_u8(s, ai::kPractice) && pl.primary[2] < 0 && mcv >= 0 && owned_record(s, mcv) == 0 &&
        (rng_.next() & 7) == 1) {
        return mcv;
    }
    std::vector<int> cand;
    for (int r = 0; r < 60 && r < int(templates_.unit_records.size()); ++r) {
        if (std::find(pl.unit_list.begin(), pl.unit_list.end(), r) != pl.unit_list.end()) cand.push_back(r);
    }
    float P = 0;
    uint32_t N = 0;
    for (int r : cand) {
        P += ai_f32(s, ai::kUnitBuildPriority + 4 * size_t(r));
        N += uint32_t(owned_units(s, r));
    }
    int pick = -1;
    float best = 100000.0f;
    // +0x0A is 20 in InitAI and in every .MIS slot (doc 22 section 1.2); the
    // max only keeps edited data from the exe's division by zero.
    const uint32_t range = std::max<uint32_t>(ai_u8(s, ai::kRandomRange), 1);
    for (int r : cand) {
        const float pr = ai_f32(s, ai::kUnitBuildPriority + 4 * size_t(r));
        // doc 22 section 3.2 (0x41ba8e..0x41bb33; the x87 runs at 53 bits,
        // so double is exact): the zero test is on the unrounded share, and
        // an unordered one (P = 0: 0 / 0) also scores 100000, without a rand().
        const double share_x = double(pr) / double(P);
        double score;
        if (share_x == 0.0 || std::isnan(share_x)) {
            score = 100000.0;
        } else {
            const float share = float(share_x);
            const float own = float(double(owned_units(s, r)) / double(N)); // N = 0: 0 / 0 = NaN
            score = double(own) / double(share) * double(int(rng_.next() % range) + 100) * double(0.01f);
        }
        // FCOM + TEST AH,1: taken when less or unordered, so a NaN score
        // (N = 0) wins, and every later candidate wins against a NaN best.
        if (!(score >= double(best))) {
            best = float(score);
            pick = r;
        }
    }
    if (pick < 0) return -1;
    // ShouldIBuildUnit (FUN_00420350): money above the stash + the price,
    // unless storage is full or in an emergency.
    const uint32_t money = uint32_t(pl.cash + pl.spice);
    if (!ai_u8(s, ai::kStorageFull) && !ai_u8(s, ai::kEmergency) &&
        money <= ai_u32(s, ai::kCashStash + 4 * ai::kJobBuildUnits) + uint32_t(unit_cost(s, pick))) {
        return -1;
    }
    // With a harvester wanted the pick must also pass CanBuildUnit here;
    // DoBuildUnits checks it again for every pick.
    if (!ai_u8(s, ai::kHarvestersWanted)) return pick;
    return ai_can_build_unit(s, pick) ? pick : -1;
}

void World::ai_build_units(int s) {
    // FUN_0041bfe0 "DoBuildUnits": one order every UnitBuildRate ticks.
    const uint32_t rate = ai_u8(s, ai::kEmergency) ? ai_u32(s, ai::kEmergencyBuildRate) : ai_u32(s, ai::kUnitBuildRate);
    const uint32_t c = ai_u32(s, ai::kUnitOrderCounter) + 1;
    set_ai_u32(s, ai::kUnitOrderCounter, c);
    if (c < rate) return;
    set_ai_u32(s, ai::kUnitOrderCounter, 0);
    const int rec = ai_best_unit(s);
    if (rec < 0 || !ai_can_build_unit(s, rec)) return; // CanBuildUnit for every pick (the practice MCV too)
    int sx, sy, mx, my;
    if (find_factory(rec, s, true, sx, sy, mx, my) < 0) return;
    post_order(s, [=, this] { start_unit(s, rec); }); // order 0x27
}

void World::ai_repairs(int s) {
    // FUN_0041bf00 "DoRepairs": flag damaged buildings for repair (doc 21).
    const uint32_t ticks = std::max<uint32_t>(ai_u32(s, ai::kBuildingRepairTicks), 1);
    const auto& pl = players_[size_t(s)];
    for (auto& b : buildings_) {
        if (b.owner != s || b.removed || b.dying) continue;
        if (uint32_t(b.slot) % ticks != tick_count_ % ticks) continue; // +0x8c, the pool slot
        const auto& r = templates_.building_records[size_t(b.record)];
        if (!(r.flags & 0x200) || b.record >= 100) continue;
        const float v = ai_f32(s, ai::kBuildingRepairValue + 4 * size_t(b.record));
        if (b.hit_points < int32_t(int64_t(r.hit_points) * double(v)) && pl.cash + pl.spice > 10) b.flags |= 1;
    }
}

bool World::ai_can_spend(int s, int job, uint32_t amount) const {
    // FUN_0041e8c0 "CanSpendCash": the money above CashStash[job] (u32,
    // 0 when not positive) must exceed the amount.
    const auto& pl = players_[size_t(s)];
    uint32_t m = uint32_t(pl.cash) + (uint32_t(pl.spice) - ai_u32(s, ai::kCashStash + 4 * size_t(job)));
    if (int32_t(m) < 1) m = 0;
    return amount < m;
}

void World::ai_repair_units(int s, int base, int guard) {
    // FUN_0041e650 "DoRepairUnits" (docs/re/53 section 2): every
    // UnitRepairTicks calls, one Repair group (job 0xB) at a time, damaged
    // quiet units of the base and guarding groups go to the nearest pad.
    // No rand().
    const uint32_t c = ai_u32(s, ai::kRepairCounter) + 1;
    set_ai_u32(s, ai::kRepairCounter, c);
    if (c < ai_u32(s, ai::kUnitRepairTicks)) return;
    set_ai_u32(s, ai::kRepairCounter, 0);
    for (int g = 0; g < 32; ++g) {
        if (ai_u8(s, size_t(g) * 0x2C + 0x52) && ai_u8(s, size_t(g) * 0x2C + 0x48) == 0xB) return;
    }
    int n = 0, t = 0;
    for (auto& u : units_) {
        if (u.owner != s || u.remove) continue;
        if (u.ai_group != base && u.ai_group != guard) continue;
        if (u.hit_age <= 12 || u.counter_2b <= 12) continue; // hit or fired recently
        const auto& r = templates_.unit_records[size_t(u.record)];
        // x87: FILD qword (u32 hit points), FMUL dword, ftol.
        const float v = ai_f32(s, ai::kUnitRepairValue + 4 * size_t(u.record));
        const int32_t lim =
            int32_t(x87::trunc_i64(x87::mul(x87::from_u64(uint32_t(r.hit_points)), x87::from_float(v))));
        if (!(u.hit_points < lim)) continue;
        if (!(u.state == state::kIdle || (ai_u8(s, ai::kRepairGuarding) && u.state == 3))) continue;
        if (n >= ai_u8(s, ai::kMaxRepairUnits) || r.infantry) continue;
        // FindFirstFreeGroup: the group is marked in use only after the
        // loop, so every call returns the same group.
        t = std::max(ai_free_group(s), 0);
        if (t == 0) continue;
        const uint32_t price = uint32_t(unit_cost(s, u.record));
        if (ai_can_spend(s, 2, (price / uint32_t(r.hit_points)) >> 1)) { // CashStash[RepairUnits]
            ++n;
            u.ai_group = uint8_t(t);
        }
    }
    if (n == 0 || t == 0) return;
    const size_t G = size_t(t) * 0x2C;
    set_ai_u8(s, G + 0x52, 1);
    set_ai_u8(s, G + 0x48, 0xB);
    set_ai_u32(s, G + 0x38, uint32_t(ai_group_strength_job(s, t, 0xB)));
    set_ai_u16(s, G + 0x2E, 0xffff);
    ai_clear_selection(s);
    ai_select_group(s, t, true);
    post_order(s, [=, this] { order_repair_units(s, -1); }); // FUN_004595e0(side, 0xffff): the nearest pad
}

void World::ai_auto_beserk(int s) {
    // FUN_00423850 (docs/re/53 section 3): a practice side that has lost its
    // means goes BESERK, checked when tick & 7 == side. No rand().
    if (int(tick_count_ & 7) != s || ai_u8(s, ai::kBeserk) != 0 || !ai_u8(s, ai::kPractice)) return;
    const auto& pl = players_[size_t(s)];
    const auto* ids = templates_.block(0x6d60d8);
    const int refinery = ids && ids->bytes.size() > 7 ? ids->bytes[7] : -1; // the name group 0x6d60df
    const int mcv = ids && ids->bytes.size() > 0xf ? ids->bytes[0xf] : -1;  // the record 0x6d60e7
    const bool no_refinery = !owns_name(s, refinery);                        // P+0x245c1[group] == 0
    const bool no_harvester = owned_kind(s, 1) == 0;                         // P+0x245b8
    // The MCV counter +0x24628 counts a dying MCV (verified, see owned_units).
    const bool no_yard_or_mcv = pl.primary[2] < 0 && (mcv < 0 || owned_record(s, mcv) == 0);
    bool no_factory = true; // Starport, light, heavy, barracks and the three palaces
    for (const int k : {13, 7, 8, 11, 17, 18, 19}) no_factory = no_factory && pl.primary[size_t(k)] < 0;
    const bool broke = uint32_t(pl.spice + pl.cash) < 400;
    if (no_refinery && no_yard_or_mcv && broke) set_ai_u8(s, ai::kBeserk, 1);
    if (no_factory && no_yard_or_mcv) set_ai_u8(s, ai::kBeserk, 1);
    if (broke && (no_harvester || no_refinery)) set_ai_u8(s, ai::kBeserk, 1);
}

void World::ai_monitor_harvesters(int s) {
    // FUN_00421fa0 "MonitorHarvesters", when (tick + side) & 0x1f == 0.
    if (((tick_count_ + uint32_t(s)) & 0x1f) != 0) return;
    for (auto& u : units_) {
        if (u.owner != s || u.remove) continue;
        const auto& r = templates_.unit_records[size_t(u.record)];
        if (r.weapon == 0xff && r.crusher && u.hit_age < 10 && u.attacker >= 0) {
            // Run over the infantry that hit it: the attacker's slot holds a
            // unit (+0x90 == 1, dying ones too) of an infantry record, and the
            // crusher takes orders (FUN_0049f4a0) and is not dying. Then
            // ClearUnits, select it, and a posted move to the attacker's
            // pixel cell (FUN_00459450); the selection is left (0x422088..).
            const int a = find_unit(u.attacker);
            if (a >= 0 && templates_.unit_records[size_t(units_[size_t(a)].record)].infantry && takes_orders(u) &&
                u.state != 0x11) {
                const Unit& v = units_[size_t(a)];
                const int x = uint8_t((v.x / 32) / 0x10000), y = uint8_t((v.y / 32) / 0x10000);
                ai_clear_selection(s);
                u.selected = true;
                post_order(s, [=, this] { order_move(s, x, y); });
            }
        }
        if (r.kind == 1 && !(u.flags & 0x100)) {
            // Two separate tests (FUN_00421fa0): idle -> 0xB, then hit and
            // below max >> 2 (unsigned shift, signed compare) -> 0xD.
            if (u.state == state::kIdle) set_state(u, 0xb);
            if (u.hit_age < 10 && u.hit_points < int32_t(uint32_t(r.hit_points) >> 2) && u.state != 0xd) {
                set_state(u, 0xd);
            }
        }
    }
}

void World::tick_ai() {
    // FUN_0041b610 "DoAI" for each active slot, every tick (doc 22 section 2.2,
    // doc 23 section 1.1, docs/re/53 section 0).
    for (int s = 0; s < 8; ++s) {
        if (!ai_u8(s, ai::kActive)) continue;
        // FUN_0041cc20 "GetSelected": the side's unit the player is viewing
        // (the first selected one, list order) is put aside, so the AI's
        // own selection-based orders skip it; a second one stays selected
        // (the exe only logs "More than one enemy unit selected").
        int viewed = -1;
        for (auto& u : units_) {
            if (u.owner != s || u.remove || !u.selected) continue;
            if (viewed < 0) {
                viewed = u.index;
                u.selected = false;
            }
        }
        ai_mcvs(s);
        ai_group_new_units(s, ai_u8(s, 0x1D3C), false);
        ai_repairs(s);
        ai_monitor_harvesters(s);
        ai_build_units(s);
        ai_overrides(s);          // after DoBuildBuildings (empty in 1.06)
        // DoAttackApproachers (+0xF56) is dead: ActivateAI clears the flag.
        // DoRepairUnits: RepairUnits set, a repair pad group, and one owned.
        const int pad = ai_u8(s, 0x1D46);
        if (ai_u8(s, 0x1D14) && pad != 0xff && owns_name(s, pad)) {
            ai_repair_units(s, ai_u8(s, 0x1D3C), ai_u8(s, 0x1D3D));
        }
        ai_attack_buildings(s);
        ai_protection(s);
        ai_special_weapons(s);    // after DoProtection, before DoDefence (docs/re/35 section 10)
        ai_defence(s);
        ai_monitor_groups(s, 0);
        ai_monitor_groups(s, 0x10);
        ai_monitor_buildings(s);
        ai_defend_checks(s);
        ai_build_carryalls(s);
        if (ai_u8(s, 0x1D45)) ai_rebuild(s); // RebuildBuildings flag
        if (ai_u8(s, 0x05)) ai_upgrade_buildings(s); // UpgradeBuildings flag
        if (ai_u8(s, 0x04)) ai_buy_units(s);         // BuyUnits flag
        ai_auto_beserk(s);                           // FUN_00423850
        ai_beserk(s);
        ai_clear_selection(s);
        // FUN_0041cc90 "ResetSelected": the viewed unit is selected again
        // unless it is dying.
        if (viewed >= 0) {
            const int k = find_unit(viewed);
            if (k >= 0 && units_[size_t(k)].state != 0x11) units_[size_t(k)].selected = true;
        }
    }
}

} // namespace d2k::sim
