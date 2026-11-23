// The simulation state for saved games: every field the game advances.
// One template walks the state for both directions, so saving and loading
// cannot drift apart. Static data (templates, tables, CIRCLES / ARMOUR /
// VARS) comes from the game files and the mission, which the loader opens
// first; this restores everything on top of that.
#include "sim/world.hpp"

#include <cstring>
#include <map>
#include <string>
#include <type_traits>

namespace d2k::sim {

namespace {

struct Writer {
    std::vector<uint8_t>& out;
    static constexpr bool kLoading = false;
    void bytes(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        out.insert(out.end(), b, b + n);
    }
};

struct Reader {
    std::span<const uint8_t> in;
    size_t pos = 0;
    bool ok = true;
    static constexpr bool kLoading = true;
    void bytes(void* p, size_t n) {
        if (pos + n > in.size()) {
            ok = false;
            std::memset(p, 0, n);
            return;
        }
        std::memcpy(p, in.data() + pos, n);
        pos += n;
    }
};

template <class Ar, class T>
void io(Ar& a, T& v) {
    static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>, "add an io() overload for this type");
    if constexpr (std::is_same_v<T, bool>) {
        uint8_t b = v ? 1 : 0;
        a.bytes(&b, 1);
        v = b != 0;
    } else {
        a.bytes(&v, sizeof v);
    }
}

template <class Ar>
void io(Ar& a, std::string& s) {
    uint32_t n = uint32_t(s.size());
    io(a, n);
    if constexpr (Ar::kLoading) {
        if (n > (1u << 20)) {
            a.ok = false;
            return;
        }
        s.resize(n);
    }
    if (n) a.bytes(s.data(), n);
}

template <class Ar, class T>
void io(Ar& a, std::vector<T>& v) {
    uint32_t n = uint32_t(v.size());
    io(a, n);
    if constexpr (Ar::kLoading) {
        if (n > (1u << 24)) {
            a.ok = false;
            return;
        }
        v.assign(n, T{});
    }
    for (auto& e : v) io(a, e);
}

template <class Ar, class T, size_t N>
void io(Ar& a, std::array<T, N>& v) {
    for (auto& e : v) io(a, e);
}

template <class Ar, class A, class B>
void io(Ar& a, std::pair<A, B>& p) {
    io(a, p.first);
    io(a, p.second);
}

template <class Ar>
void io(Ar& a, std::map<std::string, uint32_t>& m) {
    uint32_t n = uint32_t(m.size());
    io(a, n);
    if constexpr (Ar::kLoading) {
        m.clear();
        for (uint32_t k = 0; k < n && a.ok; ++k) {
            std::string key;
            uint32_t v = 0;
            io(a, key);
            io(a, v);
            m[key] = v;
        }
    } else {
        for (auto& [key, v] : m) {
            std::string k = key;
            io(a, k);
            io(a, v);
        }
    }
}

template <class Ar>
void io(Ar& a, Unit& u) {
    io(a, u.index); io(a, u.owner); io(a, u.house); io(a, u.type); io(a, u.record);
    io(a, u.speed); io(a, u.hit_points); io(a, u.flags); io(a, u.state); io(a, u.next_state);
    io(a, u.selected); io(a, u.turn_counter); io(a, u.turret_counter); io(a, u.frame); io(a, u.anim_mode);
    io(a, u.x); io(a, u.y); io(a, u.step_x); io(a, u.step_y);
    io(a, u.dest_x); io(a, u.dest_y); io(a, u.cur_x); io(a, u.cur_y); io(a, u.to_x); io(a, u.to_y);
    io(a, u.half); io(a, u.steps); io(a, u.slot_next); io(a, u.slot_cur); io(a, u.facing); io(a, u.turret_facing);
    io(a, u.route_x); io(a, u.route_y); io(a, u.route_count); io(a, u.blocked); io(a, u.path_length);
    io(a, u.repath_delay); io(a, u.waiting);
    io(a, u.timer); io(a, u.load); io(a, u.home_x); io(a, u.home_y); io(a, u.refinery);
    io(a, u.altitude); io(a, u.counter_2b); io(a, u.delivery); io(a, u.countdown); io(a, u.remove);
    io(a, u.target_kind); io(a, u.target); io(a, u.attacker); io(a, u.attacker_side); io(a, u.reload);
    io(a, u.prone); io(a, u.hit_age); io(a, u.since_stop); io(a, u.ai_group); io(a, u.delivery_entry);
    io(a, u.repair_pad);
    io(a, u.carry_unit); io(a, u.carry_saved); io(a, u.carry_slot);
    io(a, u.wing_dir); io(a, u.wing_timer); io(a, u.bombs); io(a, u.flash); io(a, u.fade);
    io(a, u.orig_side); io(a, u.smoke); io(a, u.explode_delay);
}

template <class Ar>
void io(Ar& a, Building& b) {
    io(a, b.index); io(a, b.owner); io(a, b.house); io(a, b.type); io(a, b.record);
    io(a, b.cell_x); io(a, b.cell_y); io(a, b.x); io(a, b.y); io(a, b.hit_points); io(a, b.frame);
    io(a, b.selected); io(a, b.flags); io(a, b.buildup); io(a, b.anim); io(a, b.dying); io(a, b.death_timer);
    io(a, b.removed); io(a, b.turret_facing); io(a, b.turn_counter); io(a, b.reload);
    io(a, b.target_kind); io(a, b.target); io(a, b.hit_age); io(a, b.attacker_side); io(a, b.attacker);
    io(a, b.wall_frame);
    io(a, b.state); io(a, b.flash); io(a, b.smoke); io(a, b.smoke_points);
}

template <class Ar>
void io(Ar& a, Bullet& b) {
    io(a, b.type); io(a, b.owner); io(a, b.firer); io(a, b.target); io(a, b.flags);
    io(a, b.speed); io(a, b.dx); io(a, b.dy); io(a, b.vz); io(a, b.ticks); io(a, b.delay);
    io(a, b.x); io(a, b.y); io(a, b.height); io(a, b.tx); io(a, b.ty); io(a, b.frame); io(a, b.rot); io(a, b.last_rot); io(a, b.dead);
}

template <class Ar>
void io(Ar& a, Effect& e) {
    io(a, e.explosion); io(a, e.frame); io(a, e.counter); io(a, e.flags);
    io(a, e.x); io(a, e.y); io(a, e.height); io(a, e.owner); io(a, e.move_x); io(a, e.move_y);
}

template <class Ar>
void io(Ar& a, Particle& p) {
    io(a, p.x); io(a, p.y); io(a, p.vx); io(a, p.vy); io(a, p.life); io(a, p.tile); io(a, p.pixel);
}

template <class Ar>
void io(Ar& a, Delivery& d) {
    io(a, d.x); io(a, d.y); io(a, d.started); io(a, d.active); io(a, d.due); io(a, d.records);
    io(a, d.type); io(a, d.silent); io(a, d.action);
}

template <class Ar>
void io(Ar& a, ProductionSlot& s) {
    io(a, s.item); io(a, s.progress); io(a, s.cost_acc); io(a, s.paid); io(a, s.hold); io(a, s.placing); io(a, s.used);
}

template <class Ar>
void io(Ar& a, Player& p) {
    io(a, p.house); io(a, p.tech); io(a, p.spice); io(a, p.capacity); io(a, p.cash);
    io(a, p.pending_cash); io(a, p.pending_spice); io(a, p.power_produced); io(a, p.power_used);
    io(a, p.power_percent); io(a, p.upgrade_level); io(a, p.building_marks);
    io(a, p.building_list); io(a, p.unit_list); io(a, p.building_slot); io(a, p.unit_slots); io(a, p.primary);
    uint64_t sb = p.shown_buildings, su = p.shown_units;
    io(a, sb);
    io(a, su);
    p.shown_buildings = size_t(sb);
    p.shown_units = size_t(su);
    io(a, p.low_money_said); io(a, p.last_money_tick); io(a, p.money_ticked);
    io(a, p.units_lost); io(a, p.units_killed); io(a, p.buildings_lost); io(a, p.buildings_killed);
    io(a, p.cart); io(a, p.stock); io(a, p.price); io(a, p.price_timer); io(a, p.stock_timer);
    io(a, p.starport_money); io(a, p.purchase_posted); io(a, p.delivery_pending); io(a, p.starport_list);
    for (auto& c : p.carry) {
        io(a, c.from_x); io(a, c.from_y); io(a, c.to_x); io(a, c.to_y); io(a, c.kind); io(a, c.state);
        io(a, c.unit); io(a, c.carryall); io(a, c.age); io(a, c.saved);
    }
    io(a, p.carry_count); io(a, p.carry_live); io(a, p.carry_owned);
    io(a, p.spice_harvested);
    io(a, p.upgrade_list); io(a, p.upgrade_slot); io(a, p.upgrade_prev);
}

template <class Ar>
void io(Ar& a, Vars& v) {
    io(a, v.unload_delay); io(a, v.blob_value); io(a, v.load_delay); io(a, v.refinery_fx_x);
    io(a, v.refinery_fx_y); io(a, v.drive_distance); io(a, v.worm_appetite); io(a, v.worm_initial_sleep);
    io(a, v.worm_fed_sleep); io(a, v.worm_shot_sleep); io(a, v.repair_points); io(a, v.unit_repair_points);
    io(a, v.starport_update_delay); io(a, v.starport_stock_delay); io(a, v.starport_stock_prob);
    io(a, v.starport_variation); io(a, v.starport_frigate_delay); io(a, v.repair_carry_distance);
    io(a, v.number_of_fremen); io(a, v.death_hand_accuracy);
}

template <class Ar>
void io(Ar& a, World::Message& m) {
    io(a, m.text);
    io(a, m.ticks);
    io(a, m.key);
}

template <class Ar>
void io(Ar& a, formats::MissionCondition& c) {
    io(a, c.a); io(a, c.b); io(a, c.c); io(a, c.x); io(a, c.y); io(a, c.ratio);
    io(a, c.side); io(a, c.type); io(a, c.building); io(a, c.unit);
}

template <class Ar>
void io(Ar& a, formats::MissionEvent& e) {
    io(a, e.x); io(a, e.y); io(a, e.value); io(a, e.conditions); io(a, e.type); io(a, e.count);
    io(a, e.side); io(a, e.other); io(a, e.allegiance); io(a, e.action); io(a, e.cond); io(a, e.negate);
    io(a, e.units); io(a, e.message);
}

template <class Ar>
void io(Ar& a, CellObject& o) {
    io(a, o.x); io(a, o.y); io(a, o.type); io(a, o.active); io(a, o.image); io(a, o.regrow); io(a, o.timer);
}

template <class Ar>
void io(Ar& a, Cell& c) {
    io(a, c.tile); io(a, c.flags); io(a, c.state); io(a, c.marks); io(a, c.radar_known);
}

constexpr uint32_t kStateVersion = 21; // 21: object pool slots and free lists, 20: per-record kill / loss counters, 19: turret goals,2: wall frame, 3: upgrades, 4: repair pad, 5: cell objects, 6: message keys, 7: Starport, 8: carryall ferry, 9: homing, effect moves, 10: airstrike, 11: turret orders, 12: stealth, 13: deviator, smoke timers, 14: Death Hand, 15: Devastator self-destruct, 16: leaked pool slots, 17: side colours, 18: AI base boxes

} // namespace

template <class Ar>
void World::state_io(Ar& a) {
    uint32_t version = kStateVersion;
    io(a, version);
    if (version < 15 || version > kStateVersion) { // 15 to 19 load with the defaults of the later fields
        if constexpr (Ar::kLoading) a.ok = false;
        return;
    }
    // The map (terrain changes, spice, shroud, occupancy, craters).
    int32_t w = map_.width(), h = map_.height();
    io(a, w);
    io(a, h);
    if (w != map_.width() || h != map_.height()) {
        if constexpr (Ar::kLoading) a.ok = false;
        return;
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) io(a, map_.at(x, y));
    }
    io(a, map_.thick_pattern());
    io(a, map_.objects());
    // The RNG (MSVC rand() state).
    uint32_t seed = rng_.state();
    io(a, seed);
    rng_.seed(seed);

    io(a, units_); io(a, buildings_); io(a, players_); io(a, next_unit_id_);
    if (version >= 16 && version < 21) { // the leaked-slot counts, replaced by the free lists (version 21)
        std::array<int, 8> leaked{};
        io(a, leaked);
    }
    io(a, reveal_counter_); io(a, alliance_); io(a, conditions_); io(a, events_);
    io(a, has_buildings_); io(a, has_units_); io(a, deliveries_); io(a, last_voice_);
    io(a, power_shown_); io(a, radar_said_); io(a, tick_count_);
    io(a, ai_); io(a, ai_target_kind_);
    io(a, effects); io(a, particles); io(a, bullets); io(a, shake_ticks); io(a, shake_y);
    io(a, campaign); io(a, won); io(a, lost); io(a, mission_timer); io(a, messages);
    io(a, vars); io(a, view_x); io(a, view_y); io(a, difficulty); io(a, local_player);
    if (version >= 17) {
        io(a, side_colour); io(a, unit_mask_); io(a, game_over_); io(a, human_alive_); io(a, max_crates);
        io(a, net_worms);
    } else {
        side_colour = {0, 1, 2, 3, 4, 5, 6, 7};
    }
    if (version >= 18) {
        for (auto& p : players_) io(a, p.base_box);
    } else if constexpr (Ar::kLoading) {
        for (int p = 0; p < 8; ++p) snapshot_base_box(p); // the RebuildLists box from the loaded objects
    }
    if (version >= 19) {
        for (auto& u : units_) io(a, u.turret_goal);
    } else if constexpr (Ar::kLoading) {
        for (auto& u : units_) u.turret_goal = u.turret_facing; // older saves: no goal kept, aim where it points
    }
    if (version >= 20) {
        for (auto& p : players_) {
            io(a, p.units_lost_by_record); io(a, p.units_killed_by_record); io(a, p.buildings_killed_by_record);
        }
    } // older saves: the per-record counts start at 0 (docs/re/60; nothing reads them)
    if (version >= 21) {
        for (auto& u : units_) io(a, u.slot);
        for (auto& b : buildings_) io(a, b.slot);
        for (auto& b : bullets) io(a, b.slot);
        for (auto& e : effects) io(a, e.slot);
        for (auto& p : pool_free_) io(a, p);
    } else if constexpr (Ar::kLoading) {
        upgrade_pools(); // older saves: slots handed out afresh, references turned into slots
    }
}

std::vector<uint8_t> World::save_state() {
    std::vector<uint8_t> out;
    Writer wr{out};
    state_io(wr);
    return out;
}

bool World::load_state(std::span<const uint8_t> data) {
    Reader rd{data};
    state_io(rd);
    sounds.clear();
    pending_orders_.clear(); // FUN_0045a6b0: a load frees the order lists (docs/re/49 section 4.4)
    map_.fill_radar();       // FUN_004418b0 0x441db6: the true radar from the loaded flags (docs/re/11 section 4)
    return rd.ok && rd.pos == data.size();
}

} // namespace d2k::sim
