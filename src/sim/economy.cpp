// Money, power, build lists and production. RE: docs/re/11-sidebar.md.
#include <algorithm>
#include <cstdlib>

#include "sim/world.hpp"

namespace d2k::sim {

namespace {
// Building kinds (record +0x5c) and unit kinds (+0x2d) used here.
constexpr int kKindConstructionYard = 2, kKindRefinery = 4, kKindSilo = 5;
constexpr int kUnitAirstrike = 9, kUnitDeathHand = 10, kUnitSaboteur = 4, kUnitFremen = 12;
constexpr float kPerProgress = 1.0f / 23040.0f; // 0x4c93b8

// MSVC __ftol: truncation toward zero.
int32_t ftol(float v) { return int32_t(v); }

// 1 -> 100, 2 -> 150, 3+ -> 200; then the power cut (FUN_00442a00 / 00442b10).
uint32_t count_factor(int count, int32_t percent) {
    uint32_t f = count <= 0 ? 0 : count == 1 ? 100 : count == 2 ? 150 : 200;
    const uint32_t pc = uint32_t(percent);
    if (pc < 10) f >>= 3;
    else if (pc < 50) f >>= 2;
    else if (pc < 100) f >>= 1;
    return f;
}

// FUN_00442960: production speed, and FUN_004429b0: cost, by level
// (World::factor_level): 0 easy, 1 normal, anything else hard.
uint32_t speed_percent(int level) { return level == 0 ? 125 : level == 1 ? 100 : 75; }
uint32_t cost_percent(int level) { return level == 0 ? 75 : level == 1 ? 100 : 125; }
} // namespace

int World::factor_level(int p) const {
    // FUN_00442960 / FUN_004429b0: single player uses gDifficultyLevel for
    // gSideId and normal for every other side; a network game the side's
    // handicap byte (0x6b922e + p * 0x3c, docs/re/57 section 3.4).
    if (network) return handicap[size_t(p)];
    return p == local_player ? difficulty : 1;
}

void World::init_players(const formats::MissionFile& mis) {
    for (int p = 0; p < 8; ++p) {
        auto& pl = players_[size_t(p)];
        pl.house = mis.house[size_t(p)];
        pl.tech = mis.tech_level[size_t(p)];
        pl.cash = int32_t(mis.credits[size_t(p)]); // FUN_0046bd20: straight into cash
        alliance_[size_t(p)] = mis.alliance[size_t(p)];
        update_power(p);
        update_capacity(p);
        rebuild_lists(p);
        // The set-up (FUN_00448550 0x448a95, after FUN_0046be50 and
        // FUN_0046bd20) copies both list counts into the shown counts, so the
        // first tick says no NEWOPTIONS.
        pl.shown_buildings = pl.building_list.size();
        pl.shown_units = pl.unit_list.size();
    }
    build_upgrade_lists(); // FUN_0046e7a0 at mission start (0x448d6d)
    for (int p = 0; p < 8; ++p) build_starport_list(p);
}

bool World::is_special(int record) const {
    const int k = templates_.unit_records[size_t(record)].kind;
    return k == kUnitAirstrike || k == kUnitDeathHand || k == kUnitSaboteur || k == kUnitFremen;
}

bool World::art_name_is(int record, const char* name) const {
    // The record's art name (Templates block 0x7891b0, 0x1C2 bytes each,
    // docs/re/08), compared without case as the exe's _strcmpi does.
    const auto* b = templates_.block(0x7891b0);
    if (!b || record < 0) return false;
    const size_t at = size_t(record) * 0x1c2;
    auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : int(c); };
    for (size_t i = 0;; ++i) {
        const unsigned char c = at + i < b->bytes.size() && i < 0x1c2 ? b->bytes[at + i] : 0;
        if (lower(c) != lower(static_cast<unsigned char>(name[i]))) return false;
        if (c == 0) return true;
    }
}

int World::unit_cost(int p, int record) const {
    return int(cost_percent(factor_level(p)) * uint32_t(templates_.unit_records[size_t(record)].cost) / 100);
}

int World::building_cost(int p, int record) const {
    return int(cost_percent(factor_level(p)) * uint32_t(templates_.building_records[size_t(record)].cost[0]) / 100);
}

bool World::spend(int p, int32_t amount) {
    auto& pl = players_[size_t(p)];
    if (pl.cash + pl.pending_spice + pl.pending_cash + pl.spice < amount) return false;
    if (amount < pl.pending_spice + pl.spice) {
        pl.pending_spice -= amount;
        return true;
    }
    pl.pending_cash -= amount - (pl.pending_spice + pl.spice);
    pl.pending_spice = -pl.spice;
    return true;
}

void World::refund(int p, int32_t amount) { players_[size_t(p)].pending_cash += amount; }

// ---- build lists ------------------------------------------------------------

void World::rebuild_lists(int p) {
    auto& pl = players_[size_t(p)];
    const auto old_buildings = pl.building_list;
    const auto old_units = pl.unit_list;
    pl.building_list.clear();
    pl.unit_list.clear();

    std::array<uint8_t, 256> owned{};
    bool yard = false;
    for (const auto& b : buildings_) {
        if (b.owner != p || b.removed) continue;
        const auto& r = templates_.building_records[size_t(b.record)];
        if (r.tech[0] == 9) yard = true; // the construction yard has tech level 9
        owned[r.name_index] |= r.house_mask;
    }
    snapshot_base_box(p); // +0x262e8..+0x262eb, in the same building pass (docs/re/53 section 1.6)
    if (!yard) pl.building_marks.fill(false);

    auto level = [&](int32_t name) { return name >= 0 && name < 100 ? pl.upgrade_level[size_t(name)] : 0; };
    const auto& brs = templates_.building_records;
    if (yard) {
        for (size_t i = 0; i < brs.size(); ++i) {
            const auto& r = brs[i];
            bool ok = r.prereq1 != -1 && (owned[size_t(r.prereq1 & 0xff)] & r.prereq1_mask);
            if (pl.tech < r.tech[0]) ok = false;
            if (level(r.prereq1) < r.prereq1_level) ok = false;
            if (r.prereq2 != -1) {
                if (!(owned[size_t(r.prereq2 & 0xff)] & r.prereq2_mask)) ok = false;
                if (level(r.prereq2) < r.prereq2_level) ok = false;
            }
            if (ok) pl.building_marks[i] = true;
        }
    }
    // Marked records in record order, as this house's variant, each name
    // once (FUN_0046c450 with the duplicate check).
    for (size_t i = 0; i < brs.size(); ++i) {
        if (!pl.building_marks[i]) continue;
        const int name = brs[i].name_index;
        bool listed = false;
        for (int rec : pl.building_list) listed |= brs[size_t(rec)].name_index == name;
        if (listed) continue;
        const int rec = templates_.building_record(name, pl.house);
        if (rec >= 0) pl.building_list.push_back(rec);
    }

    const auto& urs = templates_.unit_records;
    for (size_t i = 0; i < urs.size(); ++i) {
        const auto& r = urs[i];
        bool ok = false;
        if (r.prereq1 != -1) {
            ok = (owned[size_t(r.prereq1 & 0xff)] & r.prereq1_mask) != 0;
            if (level(r.prereq1) < r.prereq1_level) ok = false;
        }
        if (r.prereq2 != -1 && owned[size_t(r.prereq2 & 0xff)] == 0) ok = false;
        if (pl.tech < r.tech) ok = false;
        // FUN_0046be50: skirmish-only records (+0xAA) only in skirmish /
        // network; a THUMPER (kind 14) also needs gNetWorms (0x46c22d).
        if (r.skirmish_only && campaign) ok = false;
        if (r.kind == 14 && net_worms == 0) ok = false;
        if (!ok) continue;
        // FUN_0046c570(name, 1): the same duplicate check as FUN_0046c450
        // (0xFF when a listed record, +0x261b4[i], has this name, rec+1),
        // then the house variant (the last record of the name with the
        // house bit, else the first of the name).
        const int rec = templates_.unit_record(r.name_index, pl.house);
        if (rec < 0) continue;
        bool listed = false;
        for (int l : pl.unit_list) listed |= urs[size_t(l)].name_index == r.name_index;
        if (!listed) pl.unit_list.push_back(rec);
    }

    // Items that vanished while in production: cancelled with a refund.
    for (int rec : old_buildings) {
        if (std::find(pl.building_list.begin(), pl.building_list.end(), rec) != pl.building_list.end()) continue;
        if (pl.building_slot.item == rec) {
            refund(p, int32_t(uint32_t(pl.building_slot.progress) * uint32_t(building_cost(p, rec)) / kProgressDone));
            pl.building_slot.item = -1;
        }
    }
    for (int rec : old_units) {
        if (std::find(pl.unit_list.begin(), pl.unit_list.end(), rec) != pl.unit_list.end()) continue;
        for (auto& s : pl.unit_slots) {
            if (s.item == rec) {
                refund(p, int32_t(uint32_t(s.progress) * uint32_t(unit_cost(p, rec)) / kProgressDone));
                s.item = -1;
            }
        }
    }
    // FUN_0046be50's end: DEATHPREPPING when the Death Hand enters the local
    // player's unit list (docs/re/41 section 2.3). FUN_0046be50 says no
    // NEWOPTIONS; the production pass FUN_004467e0 does (tick_production).
    if (p == local_player) {
        const auto* ids = templates_.block(0x6d60d8);
        const int dh = ids && ids->bytes.size() > 0x13 ? ids->bytes[0x13] : -1; // 0x6d60eb
        auto has = [&](const std::vector<int>& l) { return std::find(l.begin(), l.end(), dh) != l.end(); };
        if (dh >= 0 && !has(old_units) && has(pl.unit_list)) voice(p, "DEATHPREPPING");
    }
}

// ---- orders -----------------------------------------------------------------

void World::start_building(int p, int record) {
    auto& pl = players_[size_t(p)];
    auto& s = pl.building_slot;
    if (s.item < 0) {
        s = ProductionSlot{};
        s.item = int16_t(record);
        if (pl.total_money() == 0) {
            s.hold = true;
            voice(p, "LOWMONEY");
        } else {
            voice(p, "BUILDING");
        }
        return;
    }
    // Resumes the current item even when another icon was clicked.
    if (s.hold && pl.total_money() > 0) {
        s.hold = false;
        voice(p, "BUILDING");
    }
}

void World::hold_or_cancel_building(int p, int record) {
    // Order 0x25 (FUN_004560f0): only for the item in production. Hold
    // clears the "placement sent" flag (+0x2652d) again.
    auto& pl = players_[size_t(p)];
    auto& s = pl.building_slot;
    if (s.item < 0 || s.item != record) return;
    if (!s.hold && s.progress < kProgressDone) {
        s.hold = true;
        s.placing = false;
        voice(p, "ONHOLD");
        return;
    }
    refund(p, int32_t(uint32_t(s.progress) * uint32_t(building_cost(p, s.item)) / kProgressDone));
    s = ProductionSlot{};
    voice(p, "CANCELLED");
}

bool World::unit_allowed(int p, int record) const {
    if (is_special(record)) return true;
    const int32_t pre = templates_.unit_records[size_t(record)].prereq1;
    for (const auto& s : players_[size_t(p)].unit_slots) {
        if (s.item >= 0 && templates_.unit_records[size_t(s.item)].prereq1 == pre && !is_special(s.item)) {
            return false;
        }
    }
    return true;
}

bool World::home_view(int side, int& x, int& y, bool select_mcv) {
    // FUN_0046cf10 (docs/re/25 A.8): the primary yard, else the building of
    // the smallest group (strict <), else the last MCV, else the first unit;
    // the camera top-left that centres it: a value < 1 becomes 0, then at
    // most map * 32 - view - 1. (The removed test on the primary is only a
    // guard: reassign_primary clears it when the yard goes.)
    const auto& pl = players_[size_t(side)];
    int bi = pl.primary[2];
    if (bi < 0 || size_t(bi) >= buildings_.size() || buildings_[size_t(bi)].removed) {
        bi = -1;
        int best = 0x7fffffff;
        for (const auto& b : buildings_) {
            if (b.owner != side || b.removed) continue;
            const int g = templates_.building_records[size_t(b.record)].name_index;
            if (g < best) {
                best = g;
                bi = b.index;
            }
        }
    }
    int px, py;
    if (bi >= 0) {
        const auto& b = buildings_[size_t(bi)];
        const auto& r = templates_.building_records[size_t(b.record)];
        // The image's offset_y (runtime image +0xc), not its height.
        const int dy = r.base_element >= 0 && size_t(r.base_element) < element_view_dy.size()
                           ? element_view_dy[size_t(r.base_element)]
                           : 0;
        px = (b.x >> 16) - 240;
        py = (b.y >> 16) - dy / 2 - 190;
    } else {
        // The side's unit list: the first unit, replaced by every later MCV.
        Unit* pick = nullptr;
        for (auto& u : units_) {
            if (u.owner != side || u.remove) continue;
            if (!pick || templates_.unit_records[size_t(u.record)].kind == 6) pick = &u;
        }
        if (!pick) return false; // the exe uses the local player's current camera
        px = (pick->x >> 16) - 240;
        py = (pick->y >> 16) - 190;
        if (select_mcv && templates_.unit_records[size_t(pick->record)].kind == 6) {
            // FUN_004a5750(gSideId): only the local side's units and
            // buildings are deselected; then the MCV (+0x19). No voice.
            for (auto& u : units_) {
                if (u.owner == local_player) u.selected = false;
            }
            for (auto& o : buildings_) {
                if (o.owner == local_player) o.selected = false;
            }
            pick->selected = true;
        }
    }
    const int mx = map_.width() * 32 - 480 - 1, my = map_.height() * 32 - 380 - 1;
    x = std::min(px < 1 ? 0 : px, mx);
    y = std::min(py < 1 ? 0 : py, my);
    return true;
}

void World::order_special(int side, int kind, int x, int y) {
    // Order 0x30 (FUN_00457810 case 0x1d): every ready slot of the kind is
    // cleared (the used byte is not checked); kind 9 launches the airstrike
    // from the side's home view.
    auto& pl = players_[size_t(side)];
    bool cleared = false;
    for (auto& s : pl.unit_slots) {
        if (s.item >= 0 && templates_.unit_records[size_t(s.item)].kind == kind && s.progress == kProgressDone) {
            s = ProductionSlot{};
            cleared = true;
        }
    }
    if (!cleared) return;
    if (kind == kUnitAirstrike) {
        int px = 0, py = 0;
        // HomeView's last fallback (the local player's camera 0x798538 /
        // 0x79853c, different on every machine) cannot be reached from here:
        // a ready slot survived DoIconBuild's factory test (FUN_00442c20,
        // check 0) in the previous tick, buildings leave the side's list only
        // in the model update after this order pass, and a side removed by
        // an earlier order (0x31 / leave) is inactive, so CreateUnit makes no
        // ornithopter (docs/re/35 section 3.4). (0, 0) is never used.
        home_view(side, px, py);
        launch_airstrike(side, (px / 32) & 0xffff, (py / 32) & 0xffff, x, y);
    } else if (kind == kUnitSaboteur || kind == kUnitFremen) {
        // docs/re/39 section 3.2: a free palace exit (checked once, after the
        // slots were cleared: a blocked exit loses the charge), then 1
        // saboteur or NumberOfFremen Fremen on the same cells, no voice.
        const auto* ids = templates_.block(0x6d60d8);
        const size_t off = kind == kUnitSaboteur ? 0x14 : 0x15; // 0x6d60ec / 0x6d60ed
        const int rec = ids && ids->bytes.size() > off ? ids->bytes[off] : -1;
        if (rec < 0) return;
        int sx, sy, mx, my;
        if (find_factory(rec, side, true, sx, sy, mx, my) < 0) return;
        const int n = kind == kUnitSaboteur ? 1 : int(vars.number_of_fremen);
        for (int i = 0; i < n; ++i) create_unit(side, rec, sx, sy, mx, my);
    }
    else if (kind == kUnitDeathHand) {
        // docs/re/41 section 4: the scatter, x first (unsigned rand() %
        // (2A)), clamped; drawn even if the launch then fails.
        const int A = vars.death_hand_accuracy;
        int tx = int(uint16_t(x)) - int(rng_.next() % uint32_t(2 * A)) + A;
        int ty = int(uint16_t(y)) - int(rng_.next() % uint32_t(2 * A)) + A;
        tx = std::clamp(tx, 0, map_.width() - 1);
        ty = std::clamp(ty, 0, map_.height() - 1);
        launch_death_hand(side, tx, ty);
    }
}

void World::auto_start_specials() {
    // FUN_004466e0, every tick: every special in a side's unit build list is
    // (re)started, so the airstrike recharges by itself (docs/re/35 section 2.1).
    for (int p = 0; p < 8; ++p) {
        const auto list = players_[size_t(p)].unit_list; // start_unit does not change it
        for (const int rec : list) {
            if (rec >= 0 && is_special(rec)) start_unit(p, rec); // auto_start_specials: direct (FUN_00455510)
        }
    }
}

void World::start_unit(int p, int record) {
    auto& pl = players_[size_t(p)];
    for (auto& s : pl.unit_slots) {
        if (s.item != record) continue;
        if (!s.hold) return;
        if (pl.total_money() == 0 && unit_cost(p, record) != 0) return;
        s.hold = false;
        // Also for a special resumed by AutoStartSpecials (docs/re/35 section 2.1).
        voice(p, templates_.unit_records[size_t(record)].infantry ? "TRAINING" : "BUILDING");
        return;
    }
    if (!is_special(record) && !unit_allowed(p, record)) return; // specials always pass FUN_004430d0
    for (auto& s : pl.unit_slots) {
        if (s.item >= 0) continue;
        s = ProductionSlot{};
        s.item = int16_t(record);
        if (pl.total_money() == 0) {
            s.hold = true;
            if (!is_special(record)) voice(p, "LOWMONEY");
        } else if (!is_special(record)) {
            voice(p, templates_.unit_records[size_t(record)].infantry ? "TRAINING" : "BUILDING");
        }
        return;
    }
}

void World::hold_or_cancel_unit(int p, int record) {
    auto& pl = players_[size_t(p)];
    for (size_t i = 0; i < 8; ++i) { // slots 8 and 9 are not searched (exe)
        auto& s = pl.unit_slots[i];
        if (s.item != record) continue;
        if (!s.hold && s.progress < kProgressDone) {
            s.hold = true;
            voice(p, "ONHOLD");
        } else {
            refund(p, int32_t(uint32_t(s.progress) * uint32_t(unit_cost(p, record)) / kProgressDone));
            s = ProductionSlot{};
            voice(p, "CANCELLED");
        }
        return;
    }
}

// ---- per tick ---------------------------------------------------------------

void World::update_power(int p) {
    auto& pl = players_[size_t(p)];
    int32_t produced = 0, used = 0;
    bool any = false;
    for (const auto& b : buildings_) {
        if (b.owner != p || b.removed) continue;
        any = true;
        const auto& r = templates_.building_records[size_t(b.record)];
        if (r.power < 0) {
            produced += -r.power * (b.hit_points * 100 / std::max(r.hit_points, 1)) / 100;
        } else {
            used += r.power;
        }
    }
    if (produced != 0) {
        produced += 200;
        used += 200;
    }
    pl.power_produced = produced;
    pl.power_used = used;
    pl.power_percent = !any ? 0 : used != 0 ? produced * 100 / used : 200;
}

void World::power_sounds() {
    // FUN_0044aae0, after the power of all 8 players: the local player's
    // percent against the last value drawn on the power bar (+0x245b4,
    // 0xFFFF until the bar is first drawn). Queued as speech in mode 0
    // (FUN_0046f820(id, 0, 0, 0), 0x44ab3c / 0x44ab85 / 0x44abbf).
    const auto& pl = players_[size_t(local_player)];
    auto has_radar = [&]() { // FUN_00442910(player, kind 1)
        for (const auto& b : buildings_) {
            if (b.owner == local_player && !b.removed && templates_.building_records[size_t(b.record)].kind == 1) {
                return true;
            }
        }
        return false;
    };
    auto say = [&](const char* name) {
        SoundEvent e{name, -1, -1, true};
        e.mode0 = true;
        sounds.push_back(e);
    };
    if (power_shown_ != 0xffff) {
        if (pl.power_percent < 100 && power_shown_ > 99) say("S_POWERDOWN");
        if (pl.power_percent > 99) {
            if (power_shown_ < 100) {
                if (has_radar()) {
                    say("S_RADARONLINE");
                    radar_said_ = true;
                } else {
                    say("S_POWERUP");
                }
            } else if (!has_radar()) {
                radar_said_ = false;
            } else if (!radar_said_) {
                say("S_RADARONLINE"); // a radar finished while powered
                radar_said_ = true;
            }
        }
    }
    // The bar draw (FUN_00425860) then stores the value it drew (capped at 200).
    power_shown_ = std::min<int32_t>(pl.power_percent, 200);
}

void World::update_capacity(int p) {
    auto& pl = players_[size_t(p)];
    int32_t cap = 0;
    for (const auto& b : buildings_) {
        if (b.owner != p || b.removed) continue;
        const int k = templates_.building_records[size_t(b.record)].kind;
        if (k == kKindRefinery) cap += 2000;
        else if (k == kKindSilo) cap += 1500;
    }
    pl.capacity = cap;
    // FUN_0044abe0 (0x44ac5e, unsigned jbe): over the capacity, spice is
    // cut to it; a pending spend (pending spice < 0) is paid from the cut
    // part first, and incoming pending spice is lost.
    if (uint32_t(cap) < uint32_t(pl.spice)) {
        if (pl.pending_spice < 0) pl.pending_spice = (pl.pending_spice - cap) + pl.spice;
        else pl.pending_spice = 0;
        pl.spice = cap;
    }
}

void World::storage_pass() {
    // FUN_0044abe0: per side, the drip FUN_0046c7d0, then the capacity.
    for (int p = 0; p < 8; ++p) {
        drip_money(p);
        update_capacity(p);
    }
}

void World::drip_money(int p) {
    auto& pl = players_[size_t(p)];
    // FUN_0046c7d0 netting: pending cash and pending spice of opposite signs
    // settle the smaller one through the real values (the total stays).
    auto settle_cash = [&] { // 0x46c894: the pending cash goes through
        pl.pending_spice += pl.pending_cash;
        pl.spice -= pl.pending_cash;
        pl.cash += pl.pending_cash;
        pl.pending_cash = 0;
    };
    auto settle_spice = [&] { // the pending spice goes through
        pl.cash -= pl.pending_spice;
        pl.spice += pl.pending_spice;
        pl.pending_spice = 0;
    };
    if (pl.pending_cash < 0 && pl.pending_spice > 0) {
        if (std::abs(pl.pending_cash) < pl.pending_spice) {
            settle_cash();
        } else {
            pl.pending_cash += pl.pending_spice;
            settle_spice();
        }
    } else if (pl.pending_cash > 0 && pl.pending_spice < 0) {
        if (pl.pending_cash < std::abs(pl.pending_spice)) {
            settle_cash();
        } else {
            pl.pending_cash += pl.pending_spice;
            settle_spice();
        }
    }
    const int32_t step = std::max<int32_t>(10, (std::abs(pl.pending_cash) + std::abs(pl.pending_spice)) / 16);
    auto move = [&](int32_t& pending, int32_t& value) {
        if (pending == 0) return;
        const int32_t d = std::clamp(pending, -step, step);
        value += d;
        pending -= d;
        if (tick_count_ - pl.last_money_tick >= 15) { // S_MONEYTICK, at most once per 15 ticks
            pl.last_money_tick = tick_count_;
            pl.money_ticked = true; // read by nothing now; kept so saves and the state hash stay the same
            // FUN_0046f820(S_MONEYTICK, mode 0, limit 15, prio 0) for the
            // local side only (0x46c8fe / 0x46c914), queued here in the
            // tick's sound order (docs/re/59 section 3.4).
            if (p == local_player) {
                SoundEvent e;
                e.name = "S_MONEYTICK";
                e.mode0 = true;
                sounds.push_back(e);
            }
        }
    };
    move(pl.pending_cash, pl.cash);
    move(pl.pending_spice, pl.spice);
}

int World::factory_factor(int p, int prereq) const {
    // Units: buildings whose name is the prerequisite; one alias counts too
    // (0x6d60dc byte 2 -> byte 1).
    const auto* alias = templates_.block(0x6d60d8);
    int other = prereq;
    if (alias && alias->bytes.size() > 6 && prereq == alias->bytes[6]) other = alias->bytes[5];
    int count = 0;
    for (const auto& b : buildings_) {
        if (b.owner != p || b.removed) continue;
        const int name = templates_.building_records[size_t(b.record)].name_index;
        if (name == prereq || name == other) ++count;
    }
    return int(count_factor(count, players_[size_t(p)].power_percent));
}

// ---- upgrades (docs/re/28-upgrades.md) -------------------------------------

void World::build_upgrade_lists() {
    // FUN_0046e7a0: record order, one entry per name with an upgrade
    // (tech[1] != 0xFF), mapped to the player's house record.
    for (auto& pl : players_) {
        pl.upgrade_list.fill(-1);
        std::array<bool, 256> seen{};
        int n = 0;
        for (const auto& r : templates_.building_records) {
            if (r.tech[1] == 0xff || seen[r.name_index] || n >= 8) continue;
            seen[r.name_index] = true;
            pl.upgrade_list[size_t(n++)] = templates_.building_record(r.name_index, pl.house);
        }
        pl.upgrade_slot = ProductionSlot{};
        pl.upgrade_prev = 0;
    }
}

bool World::owns_name(int p, int name) const {
    for (const auto& b : buildings_) {
        if (b.owner == p && !b.removed && templates_.building_records[size_t(b.record)].name_index == name) return true;
    }
    return false;
}

bool World::can_upgrade(int p, int name) const {
    // FUN_004431d0: owns the name, level <= 2, tech[level + 1] <= the
    // mission tech of the side.
    if (name < 0 || name >= 100 || !owns_name(p, name)) return false;
    const auto& pl = players_[size_t(p)];
    const int level = pl.upgrade_level[size_t(name)];
    if (level > 2) return false;
    const int rec = templates_.building_record(name, pl.house); // FUN_0046c450
    return rec >= 0 && templates_.building_records[size_t(rec)].tech[size_t(level + 1)] <= pl.tech;
}

int World::upgrade_cost(int p, int record, int level) const {
    // FUN_00442be0: cost[level] x the cost percentage (FUN_004429b0).
    const auto& r = templates_.building_records[size_t(record)];
    return int(cost_percent(factor_level(p)) *uint32_t(r.cost[size_t(std::clamp(level, 0, 3))]) / 100);
}

void World::start_upgrade(int p, int name) {
    // Order 0x2D (FUN_00457350), item = name index.
    if (!can_upgrade(p, name)) return;
    auto& pl = players_[size_t(p)];
    auto& s = pl.upgrade_slot;
    if (s.item < 0) {
        s = ProductionSlot{};
        s.item = int16_t(templates_.building_record(name, pl.house));
        pl.upgrade_prev = 0;
        if (pl.spice + pl.cash == 0) {
            s.hold = true;
            voice(p, "LOWMONEY");
            return;
        }
        voice(p, "BUILDUPGRADE");
        return;
    }
    // A held upgrade resumes, whichever available icon was clicked.
    if (!s.hold || pl.spice + pl.cash == 0) return;
    s.hold = false;
    voice(p, "BUILDUPGRADE");
}

void World::hold_or_cancel_upgrade(int p, int record) {
    // Order 0x2E (FUN_00457490), item = record: hold, or cancel when held.
    auto& pl = players_[size_t(p)];
    auto& s = pl.upgrade_slot;
    if (s.item < 0 || s.item != record) return;
    if (!s.hold) {
        s.hold = true;
        voice(p, "ONHOLD");
        return;
    }
    // 1.06 quirk (0x4574c2): the level is read by the record number, not
    // the name index. TODO: a fixes.ini option.
    const int level = record < 100 ? pl.upgrade_level[size_t(record)] : 0;
    refund(p, int32_t(uint32_t(s.progress) * uint32_t(upgrade_cost(p, record, level + 1)) / kProgressDone));
    s.item = -1;
    voice(p, "CANCELLED");
}

void World::tick_upgrade(int p) {
    // FUN_004467e0 0x446b3b: step rate[level + 1] (no factory, power or
    // speed factor), cost cost[level + 1] x difficulty, the building
    // slot's float payment; stalls when unpaid. Done: level + 1, slot
    // empty, build lists rebuilt.
    auto& pl = players_[size_t(p)];
    auto& s = pl.upgrade_slot;
    if (s.item < 0) return;
    const int name = templates_.building_records[size_t(s.item)].name_index;
    if (!owns_name(p, name)) { // every building of the name lost: silent refund
        refund(p, int32_t(uint32_t(s.progress) *
                          uint32_t(upgrade_cost(p, s.item, pl.upgrade_level[size_t(name)] + 1)) / kProgressDone));
        s.item = -1;
        return;
    }
    if (s.progress >= kProgressDone || s.hold) return;
    const int level = pl.upgrade_level[size_t(name)];
    const int rec = templates_.building_record(name, pl.house);
    const uint32_t step = uint32_t(templates_.building_records[size_t(rec)].build_rate[size_t(std::min(level + 1, 3))]);
    const int32_t cost = upgrade_cost(p, rec, level + 1);
    const float inc = float(step) * kPerProgress * float(cost);
    int32_t due = ftol(inc + s.cost_acc) - s.paid;
    if (step + s.progress >= kProgressDone) due = cost - s.paid;
    if (!spend(p, due)) return;
    pl.upgrade_prev = s.progress;
    s.cost_acc += inc;
    s.paid += due;
    s.progress = uint16_t(s.progress + step); // not clamped
    if (s.progress >= kProgressDone) {
        ++pl.upgrade_level[size_t(name)];
        s.item = -1;
        rebuild_lists(p);
    }
}

void World::ai_upgrade_buildings(int s) {
    // FUN_00421c40: every 64 ticks with an empty slot, the first list entry
    // that can be upgraded gets order 0x2D. No money check.
    if ((tick_count_ & 0x3f) != 0 || players_[size_t(s)].upgrade_slot.item >= 0) return;
    for (int rec : players_[size_t(s)].upgrade_list) {
        if (rec < 0) continue;
        const int name = templates_.building_records[size_t(rec)].name_index;
        if (can_upgrade(s, name)) {
            post_order(s, [=, this] { start_upgrade(s, name); });
            return;
        }
    }
}

void World::tick_production(int p) {
    auto& pl = players_[size_t(p)];
    const uint32_t speed = speed_percent(factor_level(p));

    auto advance = [&](ProductionSlot& s, uint32_t rate, uint32_t f, int32_t cost) {
        const uint32_t step = rate * (speed * f / 100) / 100;
        const float inc = float(step) * kPerProgress * float(cost);
        int32_t due = ftol(inc + s.cost_acc) - s.paid;
        if (step + s.progress >= kProgressDone) due = cost - s.paid;
        if (!spend(p, due)) return false; // no money: the slot waits
        s.paid += due;
        s.cost_acc += inc;
        s.progress = uint16_t(s.progress + step);
        if (s.progress >= kProgressDone) {
            s.progress = kProgressDone;
            return true;
        }
        return false;
    };

    auto& bs = pl.building_slot;
    if (bs.item >= 0 && bs.progress < kProgressDone && !bs.hold) {
        int yards = 0;
        for (const auto& b : buildings_) {
            if (b.owner == p && !b.removed && templates_.building_records[size_t(b.record)].kind == kKindConstructionYard) ++yards;
        }
        const auto& r = templates_.building_records[size_t(bs.item)];
        if (advance(bs, uint32_t(r.build_rate[0]), count_factor(yards, pl.power_percent),
                    building_cost(p, bs.item))) {
            voice(p, "BUILDREADY");
        }
    }

    // Unit slots (docs/re/12 section 8.2).
    for (auto& s : pl.unit_slots) {
        if (s.item < 0) continue;
        int sx, sy, mx, my;
        const int factory = find_factory(s.item, p, false, sx, sy, mx, my);
        if (factory < 0) { // no factory at all: cleared, no refund
            s = ProductionSlot{};
            continue;
        }
        if (s.hold) {
            buildings_[size_t(factory)].flags &= ~0x10u;
            continue;
        }
        const auto& r = templates_.unit_records[size_t(s.item)];
        const bool special = is_special(s.item); // spawn_ready clears s.item
        if (s.progress < kProgressDone) {
            if (r.kind != kUnitDeathHand) buildings_[size_t(factory)].flags |= 0x10; // working
            uint32_t f;
            if (r.kind == kUnitDeathHand) f = pl.power_percent >= 100 ? 100 : 0;
            else if (special) f = 100;
            else f = uint32_t(factory_factor(p, r.prereq1));
            if (advance(s, uint32_t(r.build_rate), f, unit_cost(p, s.item))) {
                if (r.kind == kUnitDeathHand) voice(p, "DEATHREADY");
                else if (!special) spawn_ready(p, s);
                const char* ready = r.kind == kUnitAirstrike ? "AirStrikeReady"
                                    : r.kind == kUnitSaboteur ? "SaboteurReady"
                                    : r.kind == kUnitFremen   ? "FremenReady"
                                                              : nullptr;
                if (ready && p == local_player) { // FUN_0048dc10(key, 1, -1), no voice
                    Message m;
                    m.key = ready;
                    messages.insert(messages.begin(), m);
                    if (messages.size() > 5) messages.resize(5);
                }
                // Only normal units clear the factory's working flag; a ready
                // special keeps it (docs/re/39 section 2.1).
                if (!special) buildings_[size_t(factory)].flags &= ~0x10u;
            }
        } else if (!special) {
            spawn_ready(p, s); // waiting for a free exit: retried every tick
        }
    }
    tick_upgrade(p); // after the building and unit slots (0x446b3b)

    if (p != local_player) return;
    // End of FUN_004467e0, local player.
    if (pl.unit_list.size() > pl.shown_units || pl.building_list.size() > pl.shown_buildings) {
        voice(p, "NEWOPTIONS");
    }
    pl.shown_units = pl.unit_list.size();
    pl.shown_buildings = pl.building_list.size();
    if (pl.power_percent < 100 && pl.power_produced != 0) voice(p, "LOWPOWER", 2000);
    const auto* ids = templates_.block(0x6d60d8);
    if (ids && ids->bytes.size() > 7 && pl.capacity < pl.spice + 100) {
        bool refinery = false; // owns the group 0x6d60dc byte 3
        for (const auto& b : buildings_) {
            refinery |= b.owner == p && !b.removed && templates_.building_records[size_t(b.record)].name_index == ids->bytes[7];
        }
        if (refinery) voice(p, "SILOSNEEDED", 2000);
    }
    const int32_t money = pl.cash + pl.spice;
    if (money == 0) {
        if (!pl.low_money_said) {
            pl.low_money_said = true;
            voice(p, "LOWMONEY", 2000);
        }
    } else if (money > 99) {
        pl.low_money_said = false;
    }
}

void World::tick_economy() {
    // The end of FUN_0044aca0, then the tick block (FUN_00448550):
    // power FUN_0044aae0 (every side, then the local player's power
    // sounds), storage FUN_0044abe0 (drip and capacity, every side), the
    // unit reveal FUN_0044cff0 (0x449aeb) and the production FUN_004467e0
    // (0x449af0, every side).
    for (int p = 0; p < 8; ++p) update_power(p);
    power_sounds();
    storage_pass();
    reveal_units();
    for (int p = 0; p < 8; ++p) tick_production(p);
}

} // namespace d2k::sim
