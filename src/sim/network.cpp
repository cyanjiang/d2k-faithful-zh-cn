// The order queue of a network game (docs/re/57 sections 1.2 to 1.5 and
// 3.8; docs/design/multiplayer.md section 4): one list per side, human
// orders run by sequence number, leave and latency records between them,
// the latency floor.
#include <algorithm>

#include "sim/world.hpp"

namespace d2k::sim {

namespace {
bool is_system(uint8_t type) { return type == order::kLeave || type == order::kLatency; }
} // namespace

void World::start_network(int humans, int ai_players, uint32_t latency) {
    // FUN_0045a7a0 (latency by game type) and FUN_004481b0 (the per-player
    // records): humans are sides 0 .. humans - 1, the AI the next sides.
    network = true;
    latency_ = latency;
    floor_ = 0;
    last_latency_tick_ = 0;
    latency_pending_ = false;
    human_ = {};
    left_ = {};
    posted_seq_ = {};
    next_seq_ = {1, 1, 1, 1, 1, 1, 1, 1};
    last_run_seq_ = {};
    model_oos_ = latency_oos_ = false;
    const int h = std::clamp(humans, 1, 8);
    for (int s = 0; s < h; ++s) human_[size_t(s)] = true;
    // FUN_0045d230 runs sides 0 .. humans + AI players - 1 (0x4e3b0c).
    net_sides_ = std::min(8, h + std::clamp(ai_players, 0, 7));
}

Order World::post_system(Order o) {
    // FUN_0045bf00 for types 9 and 0x11: numbered from the side's counter;
    // the tick is the poster's (now + latency for a leave, now + max(new,
    // old) latency for a latency record, doc 57 sections 1.5 and 3.8).
    o.seq = ++posted_seq_[size_t(o.side & 7)];
    mark_leaving(o);
    PendingOrder p;
    p.rec = o;
    pending_orders_.push_back(std::move(p));
    return o;
}

void World::mark_leaving(const Order& o) {
    // FUN_0045c740 (0x45cae0..): a leave record seen in the misc list marks
    // the leaving player as left at once (+0x3A = 1), so the stall rule no
    // longer waits for it; the record itself runs at its tick.
    if (o.type == order::kLeave) left_[size_t(o.b & 7)] = true;
}

void World::receive(const Order& o) {
    // GetNetworkOrders (FUN_0045c0e0, message 1000 / 0x3EF): types 9 and
    // above 0x12 only from an active human; types above 0x34 dropped; the
    // tick is raised to the latency floor.
    if (o.type > 0x34) return;
    if ((o.type == order::kLeave || o.type > 0x12 || o.type == order::kLatency) && !human_[size_t(o.side & 7)]) return;
    PendingOrder p;
    p.rec = o;
    p.rec.tick = std::max(o.tick, floor_);
    mark_leaving(p.rec);
    pending_orders_.push_back(std::move(p));
}

void World::raise_to_floor() {
    // FUN_0045a540: the lists of active human sides and the leave / latency
    // records never run before the floor (AI lists are not touched).
    for (auto& p : pending_orders_) {
        if (p.done) continue;
        const bool system = !p.run && is_system(p.rec.type);
        if ((system || (!p.run && human_[size_t(p.rec.side & 7)])) && p.rec.tick < floor_) p.rec.tick = floor_;
    }
}

void World::run_orders_network() {
    // FUN_0045d230: floor, system records, then every side that is an
    // active human or has an AI, in side order.
    raise_to_floor();
    run_system_orders();
    for (int s = 0; s < net_sides_; ++s) {
        if (human_[size_t(s)] || ai_active(s)) run_side_orders(s);
    }
    pending_orders_.erase(std::remove_if(pending_orders_.begin(), pending_orders_.end(),
                                         [](const PendingOrder& p) { return p.done; }),
                          pending_orders_.end());
}

void World::run_side_orders(int s) {
    // FUN_0045d120: an AI side takes the head of its list (by tick, then
    // sequence, then arrival); a human side the record with its next
    // sequence number. It runs when its tick is now; a passed tick is the
    // "Model Out Of Sync" error and the list stays blocked.
    const bool ai = ai_active(s);
    for (;;) {
        size_t best = pending_orders_.size();
        for (size_t i = 0; i < pending_orders_.size(); ++i) {
            const auto& p = pending_orders_[i];
            if (p.done || (p.rec.side & 7) != s || (!p.run && is_system(p.rec.type))) continue;
            if (ai) {
                if (best == pending_orders_.size() || p.rec.tick < pending_orders_[best].rec.tick ||
                    (p.rec.tick == pending_orders_[best].rec.tick && p.rec.seq < pending_orders_[best].rec.seq)) {
                    best = i;
                }
            } else if (p.rec.seq == next_seq_[size_t(s)]) {
                best = i;
                break;
            }
        }
        if (best == pending_orders_.size()) return;
        if (pending_orders_[best].rec.tick != tick_count_) {
            if (pending_orders_[best].rec.tick < tick_count_) model_oos_ = true;
            return;
        }
        if (!ai) ++next_seq_[size_t(s)];
        pending_orders_[best].done = true;
        const Order rec = pending_orders_[best].rec; // the handler may post (the vector may grow)
        const auto run = pending_orders_[best].run;
        if (run) with_selection(rec, run);
        else execute(rec);
        if (!ai) {
            last_run_seq_[size_t(s)] = rec.seq;
            run_system_orders();
        }
    }
}

void World::run_system_orders() {
    // FUN_0045cda0: the leave (9) and latency (0x11) records whose tick is
    // now and whose sequence is their side's next one.
    uint32_t new_latency = 0;
    for (size_t i = 0; i < pending_orders_.size(); ++i) {
        if (pending_orders_[i].done || pending_orders_[i].run) continue;
        const Order r = pending_orders_[i].rec;
        const size_t s = size_t(r.side & 7);
        if (r.type == order::kLeave) {
            if (r.tick != tick_count_ || r.seq != next_seq_[s]) continue;
            pending_orders_[i].done = true;
            if (!human_[s]) continue;
            // +0x1A the leaving side, +0x14 the kind: 0 the AI takes over,
            // 0xFF nothing (I won), else the side is removed.
            const int leaving = r.b & 7;
            human_[size_t(leaving)] = false;
            ++next_seq_[s];
            left_[size_t(leaving)] = false;
            if (r.a == 0xff) continue;
            if (r.a == 0) let_ai_take_over(leaving);
            else remove_side(leaving);
            // The host's leave (FUN_0045cda0, 0x45cf20): the host global
            // becomes 0 and the lowest remaining human takes the role
            // (FUN_0049fde0 is only Wignet "Set Mode", the transport), every
            // stored ping = the latency, the cap flag 0x6b98d0 = 0. Nothing
            // in the simulation changes; src/net/lockstep.cpp (after_tick)
            // follows the host role from human_active / has_left.
        } else if (r.type == order::kLatency) {
            bool keep = false;
            if (human_[s]) {
                if (r.tick == tick_count_ && next_seq_[s] == r.seq) {
                    last_run_seq_[s] = r.seq;
                    ++next_seq_[s];
                    if (r.c != 0) net_rates_.cap = r.c; // +0x1C the rate cap
                    net_rates_.resend_ms = r.b;         // +0x1A, 0x4e3b1c
                    const uint32_t l = uint32_t(r.a);   // +0x14 the new latency
                    if (l != 0 && new_latency < l) {
                        floor_ = std::max(latency_, l) + r.tick;
                        new_latency = l;
                        last_latency_tick_ = r.tick;
                    }
                } else if (tick_count_ <= r.tick) {
                    keep = true; // not due yet
                } else if (!left_[s]) {
                    // Arrived after its tick: counted, flagged "Latency is out
                    // of sync" (0x6b9890); only a rise still applies.
                    last_run_seq_[s] = r.seq;
                    latency_oos_ = true;
                    ++next_seq_[s];
                    const uint32_t l = uint32_t(r.a);
                    if (last_latency_tick_ <= r.tick && l > latency_) {
                        floor_ = l + r.tick;
                        new_latency = l;
                        last_latency_tick_ = r.tick;
                    }
                }
            }
            if (!keep) pending_orders_[i].done = true;
        }
    }
    if (new_latency != 0) {
        latency_pending_ = false;
        raise_to_floor();
        latency_ = new_latency;
    }
}

SoundEvent chat_sound() {
    // S_CHATMSG into the voice queue in mode 0 (FUN_0046f820(sample, 0, 0, 0)):
    // only when no voice waits or plays.
    SoundEvent e;
    e.name = "S_CHATMSG";
    e.voice = true;
    e.mode0 = true;
    return e;
}

void World::order_ally(int me, int target) {
    // ProcessOrder case 8 (0x457810): record +0x24 = me, +0x26 = the target
    // side. The text lines are "%s %s %s" with a player name first.
    me &= 7;
    target &= 7;
    auto line = [&](const std::string& key, int name_side, int arg_side) {
        Message m;
        m.key = key;
        m.prefix = net_names[size_t(name_side)];
        m.arg = net_names[size_t(arg_side)];
        messages.insert(messages.begin(), m); // FUN_00425720
        if (messages.size() > 5) messages.resize(5);
    };
    if (alliance_[size_t(me)][size_t(target)] == 1) {
        // FUN_004553e0: allowed while, with this alliance made, two living
        // sides would still be enemies (allying cannot end the game), and
        // never with an AI side.
        const uint8_t was = alliance_[size_t(me)][size_t(target)];
        alliance_[size_t(me)][size_t(target)] = 0;
        bool enemies = false;
        for (size_t i = 0; i < 8; ++i) {
            if (!has_units_[i] && !has_buildings_[i]) continue;
            for (size_t j = 0; j < 8; ++j) {
                if ((has_units_[j] || has_buildings_[j]) && (alliance_[i][j] != 0 || alliance_[j][i] != 0)) enemies = true;
            }
        }
        alliance_[size_t(me)][size_t(target)] = was;
        if (enemies && !ai_active(target)) {
            sounds.push_back(chat_sound()); // FUN_0046f820
            line("Ally", me, target);
            alliance_[size_t(me)][size_t(target)] = 0; // one way
            // FUN_0046eb00: my units and buildings drop targets of that side.
            for (auto& u : units_) {
                if (u.owner != me || u.target < 0) continue;
                const bool on_target = (u.target_kind == 1 && find_unit(u.target) >= 0 &&
                                        units_[size_t(find_unit(u.target))].owner == target) ||
                                       (u.target_kind == 2 && size_t(u.target) < buildings_.size() &&
                                        buildings_[size_t(u.target)].owner == target);
                if (on_target) {
                    u.target_kind = 0;
                    u.target = -1;
                }
            }
            for (auto& b : buildings_) {
                if (b.owner != me || b.target < 0) continue;
                const bool on_target = (b.target_kind == 1 && find_unit(b.target) >= 0 &&
                                        units_[size_t(find_unit(b.target))].owner == target) ||
                                       (b.target_kind == 2 && size_t(b.target) < buildings_.size() &&
                                        buildings_[size_t(b.target)].owner == target);
                if (on_target) {
                    b.target_kind = 0;
                    b.target = -1;
                }
            }
        } else {
            Message m;
            m.key = "UnableToAlly";
            messages.insert(messages.begin(), m);
            if (messages.size() > 5) messages.resize(5);
        }
        return;
    }
    // Already allied: war, both ways; a second line when the target had
    // allied with me too.
    line("BreakAlly", me, target);
    if (alliance_[size_t(target)][size_t(me)] == 0) line("BreakAlly", target, me);
    sounds.push_back(chat_sound()); // FUN_0046f820 (0x458397: on war too)
    alliance_[size_t(me)][size_t(target)] = 1;
    alliance_[size_t(target)][size_t(me)] = 1;
}

void World::let_ai_take_over(int s) {
    // FUN_0046e9c0: the side's AI byte on (already on is a fatal error in
    // the exe; ignored here), then TakeOver_AI (FUN_004235e0, docs/re/57a).
    s &= 7;
    if (ai_[size_t(s)].size() < 2 || ai_[size_t(s)][1] != 0) return;
    ai_[size_t(s)][1] = 1;
    ai_take_over(s);
}

void World::remove_side(int s) {
    // FUN_0046e6b0: every building not dying is destroyed (with flag
    // 0x2000), then every unit not dying except worms is killed (the
    // buildings' survivors included); the side is no longer active.
    for (size_t i = 0; i < buildings_.size(); ++i) {
        const auto& b = buildings_[i];
        if (b.owner != s || b.dying || b.removed) continue;
        destroy_building(int(i), true);
    }
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& u = units_[i];
        if (u.owner != s || u.state == 0x11 || u.remove) continue;
        if (templates_.unit_records[size_t(u.record)].kind == 5) continue;
        kill_unit(units_[i]);
    }
    side_removed_[size_t(s & 7)] = true;
}

} // namespace d2k::sim
