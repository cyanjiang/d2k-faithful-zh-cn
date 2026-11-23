// The mission script: conditions and events from the .MIS, evaluated once
// per tick in the campaign. RE: docs/re/19-mission-script.md.
#include <algorithm>

#include "sim/world.hpp"

namespace d2k::sim {

namespace {
namespace ct {
constexpr int kBuildingExists = 0, kUnitExists = 1, kInterval = 2, kTimer = 3, kCasualties = 4,
              kBaseDestroyed = 5, kUnitsDestroyed = 6, kRevealed = 7, kHarvested = 8, kFlag = 9;
} // namespace ct
namespace et {
constexpr int kDeploy = 0, kReinforce = 1, kAllegiance = 2, kBeserk = 4, kSetBuildRate = 6,
              kSetAttackBuildingRate = 7, kSetCash = 8, kSetTech = 9, kWin = 10, kLose = 11, kReveal = 14,
              kShowTimer = 15, kHideTimer = 16, kShowMessage = 17, kAddUnits = 18, kSetFlag = 19;
} // namespace et
} // namespace

void World::set_script(const formats::MissionFile& mis) {
    conditions_ = mis.conditions;
    events_ = mis.events;
    mission_timer = mis.timer;
    won = lost = false;
    defeat_check_all(); // FUN_0044f770 at set-up
}

void World::defeat_check(int side, int mode) {
    // FUN_00455290. Mode 0: any building but walls. Mode 1: any unit but
    // aircraft and worms (dying ones count), and the mask 0x6b8258 (docs/re/52
    // section 5.1): 0 = can fight (an armed unit, an MCV or buildings);
    // bit 0 = no infantry, bit 1 = no vehicle.
    if (mode == 0) {
        bool any = false;
        for (const auto& b : buildings_) {
            if (b.owner == side && !b.removed && templates_.building_records[size_t(b.record)].kind != 14) any = true;
        }
        has_buildings_[size_t(side)] = any;
        return;
    }
    bool any = false;
    const auto* ids = templates_.block(0x6d60d8);
    const int mcv = ids && ids->bytes.size() > 15 ? ids->bytes[15] : -1; // 0x6d60e7
    uint8_t m = 3;
    for (const auto& u : units_) {
        if (u.owner != side || (u.flags & 0x40)) continue;
        const auto& r = templates_.unit_records[size_t(u.record)];
        if (r.kind == 5) continue;
        any = true;
        const bool armed = r.weapon != 0xff;
        if (has_buildings_[size_t(side)]) {
            m = 0;
        } else if (!r.infantry) {
            m &= uint8_t(~2u);
            if (armed || u.record == mcv) m = 0;
        } else {
            m &= uint8_t(~1u);
            if (armed) m = 0;
        }
    }
    has_units_[size_t(side)] = any;
    unit_mask_[size_t(side)] = m;
}

void World::assign_places() {
    // FUN_0044c520 (docs/re/56 section 2): every side with no building and
    // no unit gets the next finishing place, once (FUN_0044c4f0).
    for (int s = 0; s < 8; ++s) {
        if (!has_buildings_[size_t(s)] && !has_units_[size_t(s)] && place_[size_t(s)] == 0xff) {
            place_[size_t(s)] = next_place_++;
        }
    }
}

void World::default_end_check() {
    // FUN_00453b90's skirmish / network branch (docs/re/52 section 5.2).
    human_alive_ = false;
    bool fight = false, enemies = false;
    uint8_t acc = 0xff;
    auto alive = [&](int s) { return has_units_[size_t(s)] || has_buildings_[size_t(s)]; };
    for (int i = 0; i < 8; ++i) {
        if (unit_mask_[size_t(i)] == 0 || has_buildings_[size_t(i)]) {
            fight = true;
        } else if (has_units_[size_t(i)]) {
            acc &= uint8_t(~unit_mask_[size_t(i)]);
        }
        if (!alive(i)) continue;
        for (int j = 0; j < 8; ++j) {
            if (alive(j) && (alliance_[size_t(i)][size_t(j)] != 0 || alliance_[size_t(j)][size_t(i)] != 0)) enemies = true;
        }
        if (ai_u8(i, 0x01) == 0) human_alive_ = true; // no active AI (block +0x01)
    }
    if (!enemies) game_over_ = true;
    if (!alive(local_player)) {
        if (!won) lost = true;
    } else if (game_over_ && !lost) {
        won = true;
    }
    // The stalemate tie-break: nobody can fight, crates are off. The local
    // side wins when it is the side this tick's defeat check looked at.
    if (!fight && max_crates == 0 && !game_over_ && (acc == 0xfd || acc == 0xfe)) {
        game_over_ = true;
        if (local_player == int((tick_count_ & 0xf) >> 1)) won = true;
        else lost = true;
    }
}

void World::defeat_check_all() {
    for (int s = 0; s < 8; ++s) {
        defeat_check(s, 0);
        defeat_check(s, 1);
    }
}

bool World::evaluate(formats::MissionCondition& c) {
    const int side = c.side & 7;
    switch (c.type) {
    case ct::kBuildingExists:
        for (const auto& b : buildings_) {
            if (b.owner == side && !b.removed && b.record == c.building) return true;
        }
        return false;
    case ct::kUnitExists:
        for (const auto& u : units_) {
            if (u.owner == side && u.record == c.unit) return true;
        }
        return false;
    case ct::kInterval:
        if (c.b == 0) {
            if (c.c == 0) return false;
            --c.c;
            c.b = c.a;
            return true;
        }
        --c.b;
        return false;
    case ct::kTimer:
        ++c.b;
        switch (c.unit) {
        case 0: return c.b > c.a;
        case 1: return c.b < c.a;
        case 2: return c.b == c.a;
        default: return c.a != 0 && c.b % c.a == 0;
        }
    case ct::kCasualties: {
        const auto& pl = players_[size_t(side)];
        const int32_t lost_n = pl.units_lost, kills = pl.units_killed;
        // 0x453d5c: fild of the unsigned quotient, fmul by the float32 constant
        // 0.01f at 0x4c93e4, fcomp with the float ratio, all on the x87
        // stack. An integer below 2^29 times a 24-bit mantissa is exact in a
        // double as in the x87 register, so this matches the exe bit for bit
        // (the product is never rounded to float32).
        const uint32_t n = uint32_t(lost_n), k = uint32_t(kills);
        const double ratio = k == 0 ? double(n) : double(n * 100u / k) * double(0.01f);
        return n > c.c && ratio > double(c.ratio);
    }
    case ct::kBaseDestroyed:
        return !has_buildings_[size_t(side)];
    case ct::kUnitsDestroyed:
        return !has_units_[size_t(side)];
    case ct::kRevealed:
        if (c.c != 0 && c.x >= 0 && c.y >= 0 && c.x < map_.width() && c.y < map_.height() &&
            map_.at(c.x, c.y).state == 0) {
            --c.c;
            return true;
        }
        return false;
    case ct::kHarvested: { // the local player's spice + cash; the side is ignored
        const auto& pl = players_[size_t(local_player)];
        return uint32_t(pl.spice + pl.cash) >= c.c;
    }
    case ct::kFlag:
        return c.c != 0;
    default:
        return false; // a fatal error in the exe
    }
}

void World::tick_script() {
    // FUN_00453b90: one defeat check (side (tick & 0xf) >> 1, mode tick & 1).
    defeat_check(int((tick_count_ & 0xf) >> 1), int(tick_count_ & 1));
    if (!campaign) {
        default_end_check();
        uncloak_last_units(int(tick_count_ & 7)); // FUN_00455480
        return;
    }
    std::array<bool, 48> result{};
    for (size_t i = 0; i < conditions_.size(); ++i) result[i] = evaluate(conditions_[i]);
    for (const auto& e : events_) {
        bool run = true;
        for (int k = 0; k < e.conditions && k < 14; ++k) {
            if (result[e.cond[size_t(k)] % 48] == (e.negate[size_t(k)] != 0)) {
                run = false;
                break;
            }
        }
        if (run) run_event(e);
    }
    uncloak_last_units(int(tick_count_ & 7)); // FUN_00455480
}

void World::run_event(const formats::MissionEvent& e) {
    const int side = e.side & 7;
    auto records = [&] {
        std::vector<int> r;
        for (int k = 0; k < e.count && k < 20; ++k) r.push_back(e.units[size_t(k)]);
        return r;
    };
    switch (e.type) {
    case et::kDeploy:
        queue_delivery(side, int(e.x & 0xff), int(e.y & 0xff), records(), 1, e.action == 4, e.action);
        return;
    case et::kReinforce:
        if (players_[size_t(side)].primary[13] < 0) return; // needs a Starport
        queue_delivery(side, int(e.x & 0xff), int(e.y & 0xff), records(), 2, e.action == 4, e.action);
        return; // the frigate launch: tick_deliveries (docs/re/34)
    case et::kAllegiance:
        alliance_[size_t(side)][size_t(e.other & 7)] = e.allegiance;
        if (e.allegiance != 1) { // FUN_0046eb00: drop targets on that side
            for (auto& u : units_) {
                if (u.owner != side || u.target_kind == 0) continue;
                const int t = u.target_kind == 1 ? find_unit(u.target) : -1;
                const int towner = u.target_kind == 1 ? (t >= 0 ? units_[size_t(t)].owner : -1)
                                                      : (building_alive(u.target) ? buildings_[size_t(u.target)].owner : -1);
                if (towner == (e.other & 7)) u.target_kind = 0;
            }
            for (auto& b : buildings_) {
                if (b.owner == side && b.target_kind == 1) {
                    const int t = find_unit(b.target);
                    if (t >= 0 && units_[size_t(t)].owner == (e.other & 7)) b.target_kind = 0;
                }
            }
        }
        return;
    case et::kBeserk: // the side's AI goes BESERK (+0x1e = 1)
        set_ai_u8(side, 0x1E, 1);
        return;
    case et::kSetBuildRate: // UnitBuildRate (+0x1d2c)
        set_ai_u32(side, 0x1D2C, e.value);
        return;
    case et::kSetAttackBuildingRate: // TimeBetweenBuildingAttacks (+0xf14)
        set_ai_u32(side, 0xF14, e.value);
        return;
    case et::kSetCash:
        return; // 0x6b87d0[side] is read only at set-up and by AI activation
    case et::kSetTech:
        players_[size_t(side)].tech = int(uint8_t(e.value));
        return;
    case et::kWin:
        if (!lost) won = true;
        return;
    case et::kLose:
        if (!won) lost = true;
        return;
    case et::kReveal:
        reveal_circle(int(e.x), int(e.y), e.count);
        return;
    case et::kShowTimer:
        mission_timer = int32_t(e.value);
        return;
    case et::kHideTimer:
        mission_timer = -1;
        return;
    case et::kShowMessage: {
        SoundEvent s; // FUN_0046f820 mode 0 (0x454415)
        s.name = "S_CHATMSG";
        s.voice = true;
        s.mode0 = true;
        sounds.push_back(s);
        messages.insert(messages.begin(), Message{e.message, 201, {}});
        if (messages.size() > 5) messages.resize(5);
        return;
    }
    case et::kAddUnits:
        for (int k = 0; k < e.count && k < 20; ++k) {
            int x = int(e.x), y = int(e.y);
            spiral_free_cell(x, y, 12); // the result is ignored
            create_unit(side, e.units[size_t(k)], x, y, x, y);
        }
        return;
    case et::kSetFlag:
        if (e.side < conditions_.size()) conditions_[e.side].c = e.value;
        return;
    default:
        return;
    }
}

} // namespace d2k::sim
