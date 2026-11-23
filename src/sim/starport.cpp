// The Starport: stock, prices, the cart, purchases and the AI's buying
// (docs/re/34-starport.md). The frigate itself flies in aircraft.cpp.

#include "sim/world.hpp"

#include <algorithm>

namespace d2k::sim {

namespace {
constexpr int kKindStarport = 13;
} // namespace

void World::build_starport_list(int p) {
    // FUN_0046e7a0 (docs/re/34 section 1): one record per name with the
    // "starportable" byte (+0x25), the side's version of it; no tech or
    // prerequisite check. Cart, stock and prices start at 0.
    auto& pl = players_[size_t(p)];
    pl.starport_list.fill(-1);
    size_t n = 0;
    std::vector<int> names;
    for (size_t r = 0; r < templates_.unit_records.size(); ++r) {
        const auto& ur = templates_.unit_records[r];
        if (!ur.starportable) continue;
        if (std::find(names.begin(), names.end(), ur.name_index) != names.end()) continue;
        if (n >= pl.starport_list.size()) break; // "Too many starportable units on side"
        names.push_back(ur.name_index);
        pl.starport_list[n++] = templates_.unit_record(ur.name_index, pl.house); // FUN_0046c570
    }
    pl.cart.fill(0);
    pl.stock.fill(0);
    pl.price.fill(0);
}

void World::tick_starport(int p) {
    // FUN_0046ddc0, every 8 ticks per active side, with or without a
    // Starport (docs/re/34 section 2).
    auto& pl = players_[size_t(p)];
    if (pl.price_timer == 0) {
        for (uint32_t c : pl.cart) {
            if (c != 0) return; // no refresh (and no stock growth) while the cart is not empty
        }
        for (int r : pl.starport_list) {
            if (r < 0) continue;
            const uint32_t base = uint32_t(unit_cost(p, r));
            const uint32_t v = base * uint32_t(vars.starport_variation) / 100u;
            const uint32_t roll = rng_.next(); // side.cpp line 0x88a
            pl.price[size_t(r)] = v != 0 ? roll % v - v + base : base; // u32, as the exe (v = 0 would fault)
        }
        pl.price_timer = uint32_t(vars.starport_update_delay) >> 3;
    } else {
        --pl.price_timer;
    }
    if (pl.stock_timer != 0) {
        --pl.stock_timer;
        return;
    }
    for (int r : pl.starport_list) {
        if (r < 0 || pl.stock[size_t(r)] >= 5) continue;
        // `& 100`, not `% 100`: a 1-in-4 chance (line 0x8a3).
        if ((rng_.next() & 100u) <= uint32_t(vars.starport_stock_prob)) ++pl.stock[size_t(r)];
    }
    pl.stock_timer = uint32_t(vars.starport_stock_delay) >> 3;
}

bool World::cart_empty(int p) const {
    // FUN_00443370.
    for (uint32_t c : players_[size_t(p)].cart) {
        if (c != 0) return false;
    }
    return true;
}

void World::starport_add(int p, int record) {
    // Order 0x29 (FUN_00456fd0): at most 6 units per order, 5 of one type,
    // stock > 0; the price is paid at once.
    auto& pl = players_[size_t(p)];
    if (record < 0 || record >= 60) return;
    uint32_t total = 0;
    for (int r : pl.starport_list) {
        if (r >= 0) total += pl.cart[size_t(r)];
    }
    if (total >= 6 || pl.stock[size_t(record)] == 0 || pl.cart[size_t(record)] >= 5) return;
    if (spend(p, int32_t(pl.price[size_t(record)]))) {
        pl.starport_money += pl.price[size_t(record)];
        ++pl.cart[size_t(record)];
        --pl.stock[size_t(record)];
    } else if (p == local_player) {
        voice(p, "LOWMONEY");
    }
}

void World::starport_remove(int p, int record) {
    // Order 0x2a (FUN_004570d0): the refund goes to cash.
    auto& pl = players_[size_t(p)];
    if (record < 0 || record >= 60 || pl.cart[size_t(record)] == 0) return;
    --pl.cart[size_t(record)];
    ++pl.stock[size_t(record)];
    refund(p, int32_t(pl.price[size_t(record)]));
    pl.starport_money -= pl.price[size_t(record)];
}

void World::starport_cancel(int p) {
    // Order 0x2c (FUN_004572f0).
    auto& pl = players_[size_t(p)];
    for (size_t r = 0; r < pl.cart.size(); ++r) {
        pl.stock[r] += pl.cart[r];
        refund(p, int32_t(pl.price[r] * pl.cart[r]));
        pl.cart[r] = 0;
    }
    pl.starport_money = 0;
}

void World::starport_purchase(int p) {
    // Order 0x2b (FUN_00457140): a frigate delivery due in
    // starportFrigateDelay ticks, the units in record order.
    auto& pl = players_[size_t(p)];
    pl.purchase_posted = false;
    if (pl.primary[kKindStarport] < 0) {
        refund(p, int32_t(pl.starport_money)); // FUN_0046eac0; the cart stays (quirk)
        pl.starport_money = 0;
        return;
    }
    auto& table = deliveries_[size_t(p)];
    Delivery* e = nullptr;
    for (auto& d : table) {
        if (!d.active) {
            e = &d;
            break;
        }
    }
    if (!e) {
        if (table.size() >= 10) return; // the exe would overrun into the building slot
        table.emplace_back();
        e = &table.back();
    }
    const int action = e->action; // +0x36 is not written: stale from the entry's last use
    *e = Delivery{};
    e->action = action;
    e->active = true;
    e->due = tick_count_ + uint32_t(vars.starport_frigate_delay);
    e->type = 2;
    for (size_t r = 0; r < pl.cart.size(); ++r) {
        for (uint32_t k = 0; k < pl.cart[r]; ++k) e->records.push_back(int(r));
        pl.cart[r] = 0;
    }
    if (e->records.empty()) {
        e->active = false;
        return;
    }
    pl.delivery_pending = true;
}

void World::starport_lost(int p) {
    // FUN_0046d9a0 when the last Starport goes: the money in transit back,
    // every frigate delivery (purchases and REINFORCE) cancelled.
    auto& pl = players_[size_t(p)];
    pl.delivery_pending = false;
    refund(p, int32_t(pl.starport_money));
    pl.starport_money = 0;
    for (auto& d : deliveries_[size_t(p)]) {
        if (d.type == 2) d.active = false;
    }
}

void World::ai_buy_units(int s) {
    // FUN_00421cf0 "BuyUnitsFromStarport" (docs/re/34 section 7): every 128
    // ticks. The adds are posted first and processed afterwards, each with
    // the full checks of order 0x29.
    if (tick_count_ & 0x7f) return;
    auto& pl = players_[size_t(s)];
    if (pl.primary[kKindStarport] < 0 || pl.delivery_pending) return;
    int types = 0;
    std::vector<int> posted;
    for (int r : pl.starport_list) {
        if (r < 0) continue;
        if (!ai_should_buy(s, r) || types >= 6) continue;
        ++types;
        uint8_t n = uint8_t(pl.stock[size_t(r)]);
        // BUG kept: the kind is compared with the MCV record number (12).
        if (templates_.unit_records[size_t(r)].kind == 12 && n >= 1) n = 1;
        while (uint32_t(pl.spice + pl.cash) - pl.price[size_t(r)] > ai_u32(s, 0x1074) && n != 0) {
            posted.push_back(r); // order 0x29; the u32 difference wraps when broke
            --n;
        }
    }
    if (posted.empty()) return;
    pl.delivery_pending = true;
    for (int r : posted) post_order(s, [=, this] { starport_add(s, r); });
    pl.purchase_posted = true;
    post_order(s, [=, this] { starport_purchase(s); }); // order 0x2b
}

bool World::ai_should_buy(int s, int r) {
    // FUN_00421e50 "ShouldIBuyUnit".
    const auto& pl = players_[size_t(s)];
    const uint32_t cost = uint32_t(unit_cost(s, r));
    float f = ai_f32(s, 0xF68);
    if (pl.unit_list.size() < 4) f = 1.2f;
    if (uint32_t(pl.spice) >= uint32_t(pl.capacity - 200)) f = 5.0f;
    // x87: the integer times the float, exact in a double.
    if (double(cost) * double(f) < double(pl.price[size_t(r)])) return false;
    const auto& ur = templates_.unit_records[size_t(r)];
    if (ur.weapon != 0xff) return true;
    if (ur.kind == 1 && ai_u8(s, 0xF54)) return true;
    const auto* ids = templates_.block(0x6d60d8);
    const int mcv = ids && ids->bytes.size() > 15 ? ids->bytes[15] : 12; // 0x6d60e7
    if (r == mcv && pl.primary[2] < 0 && owned_record(s, mcv) == 0 && pl.cart[size_t(mcv)] == 0 && ai_u8(s, 0x03)) {
        return (rng_.next() & 7) == 5; // AI.cpp line 0x1775
    }
    return false;
}

int World::owned_record(int p, int record) const {
    int n = 0;
    for (const auto& u : units_) n += u.owner == p && u.record == record && !u.remove;
    return n;
}

} // namespace d2k::sim
