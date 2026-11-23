#include "net/lockstep.hpp"

#include <algorithm>
#include <cmath>

#include "net/wire.hpp"
#include "sim/world.hpp"

namespace d2k::net {

namespace {

void put8(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x)); }
void put16(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x));
    v.push_back(uint8_t(x >> 8));
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i)));
}

} // namespace

Lockstep::Lockstep(sim::World& world, const Config& config) : world_(world), cfg_(config) {
    // FUN_004481b0: confirmed ticks 0, rates 60, pings 2000.
    pulse_countdown_ = passes_per_second_ / 8;
}

std::vector<uint8_t> Lockstep::encode(const sim::Order& o) {
    std::vector<uint8_t> v;
    put8(v, uint32_t(Kind::Order));
    put8(v, o.type);
    put8(v, o.side);
    put32(v, o.tick);
    put32(v, o.seq);
    put32(v, uint32_t(o.a));
    put32(v, uint32_t(o.b));
    put32(v, uint32_t(o.c));
    put16(v, uint32_t(o.ids.size()));
    for (int32_t id : o.ids) put32(v, uint32_t(id));
    put16(v, uint32_t(o.bids.size()));
    for (int32_t id : o.bids) put32(v, uint32_t(id));
    return v;
}

bool Lockstep::decode(std::span<const uint8_t> data, sim::Order& o) {
    Reader r{data};
    if (Kind(r.get(1)) != Kind::Order) return false;
    o.type = uint8_t(r.get(1));
    o.side = uint8_t(r.get(1) & 7);
    o.tick = r.get(4);
    o.seq = r.get(4);
    o.a = int32_t(r.get(4));
    o.b = int32_t(r.get(4));
    o.c = int32_t(r.get(4));
    const uint32_t n = r.get(2);
    if (n > 1000) return false;
    o.ids.resize(n);
    for (auto& id : o.ids) id = int32_t(r.get(4));
    const uint32_t nb = r.get(2);
    if (nb > 1000) return false;
    o.bids.resize(nb);
    for (auto& id : o.bids) id = int32_t(r.get(4));
    return r.ok;
}

void Lockstep::sent(const sim::Order& o) {
    // FUN_0045bcc0: the record to every other player, reliable.
    out_.push_back({-1, true, encode(o)});
}

int Lockstep::host_side() const {
    for (int p = 0; p < 8; ++p) {
        if (world_.human_active(p) && !world_.has_left(p)) return p;
    }
    return cfg_.local;
}

bool Lockstep::other_active(int p) const {
    return p != cfg_.local && world_.human_active(p) && !world_.has_left(p);
}

void Lockstep::bookkeep(int p, uint32_t seq) {
    // FUN_0045bfa0: the highest contiguous sequence received, plus the ones
    // that came early.
    auto& peer = peers_[size_t(p & 7)];
    if (seq <= peer.contiguous) return;
    if (seq == peer.contiguous + 1) {
        ++peer.contiguous;
        for (;;) {
            auto it = std::find(peer.early.begin(), peer.early.end(), peer.contiguous + 1);
            if (it == peer.early.end()) break;
            peer.early.erase(it);
            ++peer.contiguous;
        }
    } else if (std::find(peer.early.begin(), peer.early.end(), seq) == peer.early.end()) {
        peer.early.push_back(seq);
    }
}

void Lockstep::on_message(int from, std::span<const uint8_t> data, uint64_t now_ms) {
    if (data.empty()) return;
    Reader r{data};
    switch (Kind(r.get(1))) {
    case Kind::Order: {
        // GetNetworkOrders (FUN_0045c0e0): message 1000 / 0x3EF.
        sim::Order o;
        if (!decode(data, o)) return;
        const int p = o.side;
        if (!world_.human_active(p) || p == cfg_.local) return;
        peers_[size_t(p)].heard = world_.tick_count();
        if (o.type == sim::order::kLeave || o.type == sim::order::kLatency || o.type > 0x12) bookkeep(p, o.seq);
        world_.receive(o);
        break;
    }
    case Kind::Pulse: {
        // ProcessPulseSignal (FUN_0045c740, misc type 0x0C, doc 57 section 1.6).
        const uint32_t tick = r.get(4);
        const int p = int(r.get(1) & 7);
        const uint32_t seq = r.get(4);
        const uint16_t sync = uint16_t(r.get(2));
        const bool stamped = r.get(1) != 0;
        const uint32_t stamp = r.get(4);
        if (!r.ok || p == cfg_.local || !world_.human_active(p) || world_.has_left(p)) return;
        auto& peer = peers_[size_t(p)];
        peer.heard = world_.tick_count();
        peer.pulse_tick = tick;
        if (seq <= peer.contiguous) peer.confirmed = std::max(peer.confirmed, tick);
        sync_record(tick, sync);
        if (stamped) { // the pong, at once, to the sender, unreliable
            std::vector<uint8_t> v;
            put8(v, uint32_t(Kind::Pong));
            put8(v, uint32_t(cfg_.local));
            put32(v, stamp);
            out_.push_back({p, false, std::move(v)});
        }
        break;
    }
    case Kind::Pong: {
        // Misc type 0x0D: the round trip to that player.
        const int p = int(r.get(1) & 7);
        const uint32_t stamp = r.get(4);
        if (!r.ok) return;
        auto& peer = peers_[size_t(p)];
        peer.rtt_sum += uint32_t(now_ms) - stamp;
        ++peer.rtt_n;
        break;
    }
    case Kind::Speed: {
        const uint32_t tick = r.get(4), value = r.get(4);
        if (r.ok) speeds_.push_back({tick, value});
        break;
    }
    case Kind::Chat: {
        if (data.size() > 1 && data.size() <= 257) chats_.push_back({from, std::string(data.begin() + 1, data.end())});
        break;
    }
    case Kind::Ready: {
        const int p = int(r.get(1) & 7);
        const uint64_t lo = r.get(4), hi = r.get(4);
        if (!r.ok) break;
        ready_[size_t(p)] = true;
        if ((lo | hi << 32) != cfg_.data_hash && oos_tick_ < 0) oos_tick_ = 0; // different game data
        break;
    }
    case Kind::Report: {
        // Misc type 0x0E (0x3EB): only the host uses it.
        const int p = int(r.get(1) & 7);
        const uint32_t rate = r.get(4), ping = r.get(4);
        if (!r.ok || !is_host()) return;
        peers_[size_t(p)].rate = rate;
        peers_[size_t(p)].ping = ping;
        peers_[size_t(p)].reported = true;
        break;
    }
    }
    (void)from;
}

void Lockstep::pass(uint64_t now_ms) {
    // The end of FUN_0045c740 (doc 57 section 1.7): stalled while the
    // slowest other player's confirmed tick m gives m - 2 + latency < now.
    const int64_t now = world_.tick_count();
    int64_t m = -1;
    int q = -1;
    for (int p = 0; p < 8; ++p) {
        if (!other_active(p)) continue;
        if (m < 0 || int64_t(peers_[size_t(p)].confirmed) < m) {
            m = peers_[size_t(p)].confirmed;
            q = p;
        }
    }
    if (m >= 0 && m - 2 + int64_t(world_.latency()) < now) {
        if (stalled_) ++stalled_passes_;
        stalled_ = true;
    } else if (stalled_ || m < 0) {
        stalled_ = false;
        stalled_passes_ = 0;
        timeout_ = {};
        drop_posted_ = false;
    }
    if (stalled_passes_ > cfg_.stall_passes) {
        if (!timeout_.on || timeout_.player != q) {
            timeout_ = {true, q, 0}; // first time, or a different player is now the slowest
            drop_posted_ = false;
        } else if (timeout_.seconds > cfg_.drop_seconds && !drop_posted_) {
            // Drop the silent player: our leave record for q, kind 1 (its
            // side is removed), at now + latency (0x45ccfa).
            sim::Order o = sim::make_order(sim::order::kLeave, cfg_.local, 1, q);
            o.tick = world_.tick_count() + world_.latency();
            sent(world_.post_system(o));
            drop_posted_ = true;
            lost_player_ = true; // "LostOnePlayer"
        }
    }
    // The pulse, every loop passes per second / 8 passes (0x6b9830).
    if (pulse_countdown_ == 0) {
        send_pulse(now_ms);
        pulse_countdown_ = std::max<uint32_t>(1, passes_per_second_ / 8);
    } else {
        --pulse_countdown_;
    }
}

void Lockstep::send_pulse(uint64_t now_ms) {
    // Message 0x3EA, unreliable: {tick, side, last sequence posted, the
    // tick's sync value, every 25th a ping stamp (GetTickCount)}.
    ++pulse_count_;
    const bool stamped = pulse_count_ % 25 == 0;
    std::vector<uint8_t> v;
    put8(v, uint32_t(Kind::Pulse));
    put32(v, world_.tick_count());
    put8(v, uint32_t(cfg_.local));
    put32(v, world_.posted_seq(cfg_.local));
    put16(v, world_.sync_value());
    put8(v, stamped ? 1 : 0);
    put32(v, uint32_t(now_ms));
    out_.push_back({-1, false, std::move(v)});
}

void Lockstep::sync_record(uint32_t tick, uint16_t value) {
    // FUN_0045aab0: the same tick with another value is the mismatch; from
    // then on nothing is recorded. Otherwise the first empty entry, or the
    // one with the smallest tick.
    if (oos_tick_ >= 0) return;
    size_t slot = sync_.size();
    for (size_t i = 0; i < sync_.size(); ++i) {
        if (sync_[i].tick == int64_t(tick)) {
            if (sync_[i].value != value) oos_tick_ = tick;
            return;
        }
    }
    for (size_t i = 0; i < sync_.size(); ++i) {
        if (sync_[i].tick < 0) {
            slot = i;
            break;
        }
    }
    if (slot == sync_.size()) {
        slot = 0;
        for (size_t i = 1; i < sync_.size(); ++i) {
            if (sync_[i].tick < sync_[slot].tick) slot = i;
        }
    }
    sync_[slot] = {int64_t(tick), value};
}

void Lockstep::after_tick() {
    sync_record(world_.tick_count(), world_.sync_value());
    const auto rates = world_.take_net_rates();
    if (rates.cap != 0) cap_ = uint32_t(rates.cap);
    if (rates.resend_ms != 0) cfg_.resend_ms = uint32_t(rates.resend_ms);
    // A new host (the old one left): every stored ping becomes the latency
    // value and the cap flag is cleared, as FUN_0045cda0 does on a host's
    // leave; the reports start over.
    const int host = host_side();
    if (host != last_host_) {
        last_host_ = host;
        for (auto& peer : peers_) {
            peer.ping = world_.latency();
            peer.reported = false;
        }
        cap_changed_ = false;
    }
}

void Lockstep::before_tick(uint32_t ticks_per_second) {
    // The host, in the tick block when every active human has reported
    // (0x6b9874): FUN_0045c480.
    if (!is_host()) return;
    for (int p = 0; p < 8; ++p) {
        if (world_.human_active(p) && !world_.has_left(p) && !peers_[size_t(p)].reported) return;
    }
    for (auto& peer : peers_) peer.reported = false;
    host_latency(ticks_per_second);
}

void Lockstep::host_latency(uint32_t ticks_per_second) {
    // FUN_0045c480 (doc 57 section 1.5).
    uint32_t min_rate = 0xffffffff, max_ping = 0;
    int humans = 0;
    for (int p = 0; p < 8; ++p) {
        if (!world_.human_active(p) || world_.has_left(p)) continue;
        ++humans;
        min_rate = std::min(min_rate, peers_[size_t(p)].rate);
        max_ping = std::max(max_ping, peers_[size_t(p)].ping);
    }
    min_rate = std::max<uint32_t>(min_rate, 8);
    uint32_t half_min = cfg_.half_ping_min;
    if (humans == 1) {
        half_min = 100;
        max_ping = 2;
    }
    uint32_t new_cap = 0;
    if (min_rate != cap_ || cap_changed_) {
        if (min_rate < cap_) new_cap = min_rate;
        else if (min_rate - cap_ >= 4) new_cap = cap_ + 3;
    }
    const uint32_t h = std::clamp(max_ping / 2, half_min, cfg_.half_ping_max);
    const uint32_t resend = 2 * h;
    uint32_t new_latency = 0;
    const uint32_t lat = world_.latency();
    if (!world_.latency_change_pending() && !world_.has_left(cfg_.local)) {
        const uint32_t x = std::min(ticks_per_second, cap_);
        // ftol(((x * h) / 1000 + 1) * factor): integer division, then float32.
        uint32_t l = uint32_t(float(x * h / 1000 + 1) * cfg_.factor);
        if (l < lat) l = std::max(l, lat * 72 / 100);
        if (l > lat) l = std::min(l, lat * 100 / 72);
        if (l != lat) {
            new_latency = l;
            world_.set_latency_change_pending();
        }
    }
    sim::Order o = sim::make_order(sim::order::kLatency, cfg_.local, int(new_latency), int(resend), int(new_cap));
    o.tick = world_.tick_count() + std::max(new_latency, lat);
    sent(world_.post_system(o));
}

uint32_t Lockstep::reported_ping(uint32_t ecx) const {
    // FUN_0045d350: meant to be the largest average round trip over the
    // active players (+0x37; the local record never has a pong), but it
    // returns the last one computed (the loop temporary). Its first
    // instruction pushes ECX and loads it into that temporary
    // (0x45d350..0x45d351), so with no pong at all it returns the caller's
    // ECX: see second().
    uint32_t last = ecx;
    for (int p = 0; p < 8; ++p) {
        if (!world_.human_active(p) || peers_[size_t(p)].rtt_n == 0) continue;
        last = uint32_t(peers_[size_t(p)].rtt_sum / peers_[size_t(p)].rtt_n);
    }
    return last;
}

void Lockstep::second(uint32_t passes) {
    // The 1-second timer: the loop rate, the timeout's seconds, and every
    // 9th second the report (FUN_0045d390), then the round trips cleared.
    passes_per_second_ = std::max<uint32_t>(passes, 1);
    if (timeout_.on) ++timeout_.seconds;
    rate_sum_ += passes;
    if (++seconds_ % 9 != 0) return;
    const uint32_t rate = uint32_t(rate_sum_ / 9);
    rate_sum_ = 0;
    // ECX at the call to FUN_0045d350 (0x4a37be: 9, the divisor). The host
    // (0x45d3e4) loads gSideId into CL: ECX = its side. A client (0x45d3a2)
    // clears CL with the wrapper's host byte and loads the rate's low word
    // into CX: ECX = rate. The client's report is 16-bit fields (0x45d3a7,
    // 0x45d3bf), the host stores the whole value (0x45d417).
    if (is_host()) {
        peers_[size_t(cfg_.local)].rate = rate;
        peers_[size_t(cfg_.local)].ping = reported_ping(uint32_t(cfg_.local));
        peers_[size_t(cfg_.local)].reported = true;
    } else {
        const uint32_t ping = reported_ping(rate & 0xffff) & 0xffff;
        std::vector<uint8_t> v;
        put8(v, uint32_t(Kind::Report));
        put8(v, uint32_t(cfg_.local));
        put32(v, rate & 0xffff);
        put32(v, ping);
        out_.push_back({host_side(), true, std::move(v)}); // to the host
    }
    for (auto& peer : peers_) {
        peer.rtt_sum = 0;
        peer.rtt_n = 0;
    }
}

sim::Order Lockstep::leave(int kind) {
    // 0x44a114: the leave record of my side, at now + latency.
    sim::Order o = sim::make_order(sim::order::kLeave, cfg_.local, kind & 0xff, cfg_.local);
    o.tick = world_.tick_count() + world_.latency();
    o = world_.post_system(o);
    sent(o);
    return o;
}

Lockstep::StartSync Lockstep::start_sync(uint64_t now_ms) {
    if (sync_done_) return StartSync::Done;
    if (!sync_started_) {
        sync_started_ = true;
        sync_t0_ = now_ms;
        std::vector<uint8_t> v;
        put8(v, uint32_t(Kind::Ready));
        put8(v, uint32_t(cfg_.local));
        put32(v, uint32_t(cfg_.data_hash));
        put32(v, uint32_t(cfg_.data_hash >> 32));
        out_.push_back({-1, true, std::move(v)});
    }
    bool all = true, any = false;
    for (int p = 0; p < 8; ++p) {
        if (p == cfg_.local || !world_.human_active(p)) continue;
        all &= ready_[size_t(p)];
        any |= ready_[size_t(p)];
    }
    if (all) {
        sync_done_ = true;
        return StartSync::Done;
    }
    if (now_ms - sync_t0_ < 20000) return StartSync::Waiting; // 60000 / 3 ms
    if (!any) return StartSync::Alone;
    // The missing players leave, kind 1 (0x44841a).
    for (int p = 0; p < 8; ++p) {
        if (p == cfg_.local || !world_.human_active(p) || ready_[size_t(p)]) continue;
        sim::Order o = sim::make_order(sim::order::kLeave, cfg_.local, 1, p);
        o.tick = world_.tick_count() + world_.latency();
        sent(world_.post_system(o));
    }
    lost_some_ = true; // "LostSomePlayers"
    sync_done_ = true;
    return StartSync::Done;
}

int Lockstep::sync_found() const {
    int n = 1;
    for (int p = 0; p < 8; ++p) {
        if (p != cfg_.local && world_.human_active(p) && ready_[size_t(p)]) ++n;
    }
    return n;
}

int Lockstep::sync_seconds_left(uint64_t now_ms) const {
    if (!sync_started_ || now_ms - sync_t0_ >= 20000) return 0;
    return int((20000 - (now_ms - sync_t0_)) / 1000);
}

void Lockstep::chat(int to, const std::string& text) {
    std::vector<uint8_t> v;
    put8(v, uint32_t(Kind::Chat));
    v.insert(v.end(), text.begin(), text.begin() + std::ptrdiff_t(std::min<size_t>(text.size(), 256)));
    out_.push_back({to, true, std::move(v)});
}

void Lockstep::speed(uint32_t value) {
    const uint32_t tick = world_.tick_count() + world_.latency();
    speeds_.push_back({tick, value}); // queued here too (FUN_0045c740 applies it)
    std::vector<uint8_t> v;
    put8(v, uint32_t(Kind::Speed));
    put32(v, tick);
    put32(v, value);
    out_.push_back({-1, true, std::move(v)});
}

int Lockstep::take_speed(uint32_t now) {
    for (size_t i = 0; i < speeds_.size(); ++i) {
        if (speeds_[i].first <= now) {
            const int v = int(speeds_[i].second);
            speeds_.erase(speeds_.begin() + std::ptrdiff_t(i));
            return v;
        }
    }
    return -1;
}

std::vector<Packet> Lockstep::take_packets() {
    std::vector<Packet> v;
    v.swap(out_);
    return v;
}

} // namespace d2k::net
