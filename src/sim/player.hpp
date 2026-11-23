#pragma once
// A player's economy and production state: the parts of the exe's player
// struct (0x798870 + p*0x26990) the sidebar works with.
// RE: docs/re/11-sidebar.md §6-8.

#include <array>
#include <cstdint>
#include <vector>

namespace d2k::sim {

constexpr uint16_t kProgressDone = 0x5a00; // 23040 = finished

struct ProductionSlot {
    int16_t item = -1;       // record (-1 = none)
    uint16_t progress = 0;   // out of kProgressDone
    float cost_acc = 0.0f;   // cost owed so far (float, +0x26524)
    int32_t paid = 0;        // +0x26528
    bool hold = false;       // +0x2652c
    bool placing = false;    // +0x2652d: placement already sent (building slot)
    bool used = false;       // +0x26541: a ready special's target already sent (docs/re/35)
};

// A carryall request slot (player +0x2660c, 0x14 bytes, docs/re/33 section 1.1).
struct CarrySlot {
    uint8_t from_x = 0, from_y = 0, to_x = 0, to_y = 0;
    uint8_t kind = 0;   // 100 own request, 1 DoProtection (never read)
    uint8_t state = 0;  // 0 free, 1 waiting, 2 taken, 3 done, 4 picked up
    int unit = -1;      // unit id (same player)
    int carryall = -1;  // carryall id
    uint32_t age = 0, saved = 0;
};

struct Player {
    int house = 0;  // .MIS house byte (0x8cd4f0)
    int tech = 0;   // mission tech level (0x6b87c8)
    int32_t spice = 0, capacity = 0, cash = 0;       // +0x24254 / +0x24258 / +0x2425c
    int32_t pending_cash = 0, pending_spice = 0;     // +0x24260 / +0x24264
    int32_t power_produced = 0, power_used = 0;      // +0x245a8 / +0x245ac
    int32_t power_percent = 0;                       // +0x245b0
    std::array<uint8_t, 100> upgrade_level{};        // +0x24544, per building name index
    std::array<bool, 100> building_marks{};          // +0x24718, per building record
    std::vector<int> building_list, unit_list;       // +0x260bc / +0x261b4 (records)
    ProductionSlot building_slot;                    // +0x2651c
    std::array<ProductionSlot, 10> unit_slots;       // +0x26530
    // Upgrades (docs/re/28): the list (+0x262c8, building records, -1
    // empty; built once per mission) and the slot +0x265f8 (item = record).
    std::array<int, 8> upgrade_list{-1, -1, -1, -1, -1, -1, -1, -1};
    ProductionSlot upgrade_slot;
    uint16_t upgrade_prev = 0;                       // +0x265fc: previous progress (drawing)
    // Primary building per kind (+0x26098..+0x260ac, object index, -1 none).
    std::array<int, 20> primary{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    size_t shown_buildings = 0, shown_units = 0;     // +0x260b8 / +0x261b0 (NEWOPTIONS)
    bool low_money_said = false;                     // 0x516480
    uint32_t last_money_tick = 0;                    // S_MONEYTICK limiter (15 ticks)
    bool money_ticked = false;                       // unread (S_MONEYTICK is a SoundEvent); kept for the save layout
    int32_t units_lost = 0, units_killed = 0;        // +0x24a08 / +0x24c90 (CASUALTIES)
    int32_t buildings_lost = 0, buildings_killed = 0; // +0x24a0c / +0x24c94 (destroyed, not sold)
    // Per record (docs/re/60): units lost (+0x24a10[unit rec]), units and
    // buildings killed (+0x24c98 / +0x25418, index rec * 8 + side). The
    // exe never reads them; they are kept because its save holds them.
    std::array<int32_t, 60> units_lost_by_record{};
    std::array<int32_t, 60 * 8> units_killed_by_record{};
    std::array<int32_t, 100 * 8> buildings_killed_by_record{};
    int32_t spice_harvested = 0;                     // +0x2477c: +blob value per unload step
    // The Starport (docs/re/34): cart / stock / price per unit record
    // (+0x24268 / +0x24358 / +0x24448), the countdowns (+0x24538 /
    // +0x2453c), the money in the cart or in transit (+0x24540), the
    // purchase posted (+0x262a4), a delivery pending (+0x262a5), the list
    // (+0x262a8, unit records, -1 empty).
    std::array<uint32_t, 60> cart{}, stock{}, price{};
    uint32_t price_timer = 0, stock_timer = 0, starport_money = 0;
    bool purchase_posted = false, delivery_pending = false;
    std::array<int, 8> starport_list{-1, -1, -1, -1, -1, -1, -1, -1};
    // The carryall ferry: 32 request slots, the slot count (+0x2688c), live
    // carryalls (+0x2688d, delivery ones too), owned carryalls (+0x245c0).
    std::array<CarrySlot, 32> carry{};
    uint8_t carry_count = 0, carry_live = 0, carry_owned = 0;
    // The base box (+0x262e8..+0x262eb: y min, y max, x min, x max), a
    // snapshot taken by RebuildLists (docs/re/53 section 1.6).
    std::array<uint8_t, 4> base_box{};

    int32_t total_money() const { return cash + spice; }
};

} // namespace d2k::sim
