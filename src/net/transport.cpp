#include "net/transport.hpp"

#include <algorithm>

namespace d2k::net {

namespace {
constexpr uint8_t kMagic[4] = {'D', '2', 'K', 'F'};
constexpr size_t kHeaderSize = 15;
} // namespace

Transport::Transport(UdpSocket& socket, const Config& config) : sock_(socket), cfg_(config), hub_(config.hub) {
    out_seq_.fill(1);
}

std::vector<uint8_t> Transport::build(const Header& h, std::span<const uint8_t> payload) {
    std::vector<uint8_t> v(kMagic, kMagic + 4);
    v.push_back(uint8_t(kProtocol));
    v.push_back(uint8_t(kProtocol >> 8));
    v.push_back(uint8_t(h.kind));
    v.push_back(h.reliable ? 1 : 0);
    v.push_back(uint8_t(h.from));
    v.push_back(uint8_t(h.to));
    v.push_back(uint8_t(h.hop));
    for (int i = 0; i < 4; ++i) v.push_back(uint8_t(h.seq >> (8 * i)));
    v.insert(v.end(), payload.begin(), payload.end());
    return v;
}

bool Transport::parse(std::span<const uint8_t> d, Header& h, std::span<const uint8_t>& payload) {
    if (d.size() < kHeaderSize || !std::equal(kMagic, kMagic + 4, d.begin())) return false;
    if (uint16_t(d[4] | d[5] << 8) != kProtocol) return false;
    h.kind = Kind(d[6]);
    h.reliable = d[7] & 1;
    h.from = d[8];
    h.to = d[9];
    h.hop = d[10];
    h.seq = uint32_t(d[11]) | uint32_t(d[12]) << 8 | uint32_t(d[13]) << 16 | uint32_t(d[14]) << 24;
    if (h.from > 7 || h.to > 7 || h.hop > 7) return false;
    payload = d.subspan(kHeaderSize);
    return true;
}

void Transport::raw(const Address& a, const Header& h, std::span<const uint8_t> payload) {
    if (drop_percent > 0) {
        lcg_ = lcg_ * 1103515245u + 12345u;
        if (int((lcg_ >> 16) % 100) < drop_percent) return;
    }
    const auto d = build(h, payload);
    sock_.send(a, d);
}

void Transport::transmit(const Header& h, std::span<const uint8_t> payload) {
    // The star: the hub sends straight to the receiver, everyone else to
    // the hub (docs/design/multiplayer.md section 6.3).
    const int via = is_hub() ? h.to : hub_;
    if (const auto& a = cfg_.addr[size_t(via)]) raw(*a, h, payload);
}

void Transport::send(const Packet& p, uint64_t now_ms) {
    if (!started_) {
        started_ = true;
        last_hub_ms_ = now_ms;
    }
    for (int to = 0; to < 8; ++to) {
        if (to == cfg_.local || !cfg_.players[size_t(to)] || gone_[size_t(to)]) continue;
        if (p.to >= 0 && p.to != to) continue;
        Header h;
        h.kind = Kind::Data;
        h.reliable = p.reliable;
        h.from = h.hop = cfg_.local;
        h.to = to;
        if (p.reliable) {
            h.seq = out_seq_[size_t(to)]++;
            pending_.push_back({to, h.seq, p.data, now_ms});
        }
        transmit(h, p.data);
    }
}

void Transport::set_gone(int side) {
    side &= 7;
    if (gone_[size_t(side)]) return;
    gone_[size_t(side)] = true;
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Pending& q) { return q.to == side; }),
                   pending_.end());
    // The hub's leave has run: move on at once (it routed while waiting
    // for its acks, doc 57 section 3.8).
    if (side == hub_ && !is_hub()) next_hub(last_hub_ms_);
}

void Transport::next_hub(uint64_t now_ms) {
    // The hand-over: the lowest-numbered player that has not left and has
    // not failed as hub; this machine itself when it is that player.
    failed_hub_[size_t(hub_)] = true;
    for (int p = 0; p < 8; ++p) {
        if (!cfg_.players[size_t(p)] || gone_[size_t(p)] || failed_hub_[size_t(p)]) continue;
        hub_ = p;
        last_hub_ms_ = now_ms;
        if (p != cfg_.local) {
            Header h;
            h.kind = Kind::Hello;
            h.from = h.hop = cfg_.local;
            h.to = p;
            if (const auto& a = cfg_.addr[size_t(p)]) raw(*a, h, {});
        }
        // Everything not yet acked goes again through the new hub.
        for (auto& q : pending_) q.sent_ms = 0;
        return;
    }
}

std::vector<Transport::Delivery> Transport::poll(uint64_t now_ms, uint32_t resend_ms) {
    if (!started_) {
        started_ = true;
        last_hub_ms_ = now_ms;
    }
    std::vector<Delivery> out;
    while (auto d = sock_.receive()) {
        Header h;
        std::span<const uint8_t> payload;
        if (!parse(d->data, h, payload)) {
            if (foreign_.size() < 64) foreign_.push_back(std::move(*d));
            continue;
        }
        // Learn where the sending machine is (a NAT mapping included).
        cfg_.addr[size_t(h.hop)] = d->from;
        if (h.hop == hub_) last_hub_ms_ = now_ms;
        if (is_hub() && h.hop != h.from && h.hop != cfg_.local) {
            // Another machine forwards traffic: it became the hub while this
            // one was silent (a lag spike, not a drop). Step down and join it.
            hub_ = h.hop;
            last_hub_ms_ = now_ms;
            Header hello;
            hello.kind = Kind::Hello;
            hello.from = hello.hop = cfg_.local;
            hello.to = hub_;
            raw(d->from, hello, {});
            for (auto& q : pending_) q.sent_ms = 0; // resend through it
        }
        if (h.kind == Kind::Hello) {
            // A client chose this machine as its new hub.
            if (h.to == cfg_.local) {
                hub_ = cfg_.local;
                Header r;
                r.kind = Kind::HelloAck;
                r.from = r.hop = cfg_.local;
                r.to = h.from;
                raw(d->from, r, {});
            }
            continue;
        }
        if (h.kind == Kind::HelloAck) continue; // its arrival already refreshed the hub
        if (h.to != cfg_.local) {
            // Not for us: the hub forwards it unchanged but for the hop.
            if (is_hub() && !gone_[size_t(h.to)]) {
                Header f = h;
                f.hop = cfg_.local;
                if (const auto& a = cfg_.addr[size_t(h.to)]) raw(*a, f, payload);
            }
            continue;
        }
        if (h.kind == Kind::Ack) {
            pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                          [&](const Pending& q) { return q.to == h.from && q.seq == h.seq; }),
                           pending_.end());
            continue;
        }
        if (h.reliable) {
            Header a;
            a.kind = Kind::Ack;
            a.from = a.hop = cfg_.local;
            a.to = h.from;
            a.seq = h.seq;
            transmit(a, {});
            if (!seen_[size_t(h.from)].insert(h.seq).second) continue; // a duplicate
        }
        out.push_back({h.from, std::vector<uint8_t>(payload.begin(), payload.end())});
    }
    // Resends: at most 16 per pass, after the resend time (doc 57 section 5).
    int resent = 0;
    for (auto& q : pending_) {
        if (resent >= 16) break;
        if (q.sent_ms != 0 && now_ms - q.sent_ms < resend_ms) continue;
        Header h;
        h.kind = Kind::Data;
        h.reliable = true;
        h.from = h.hop = cfg_.local;
        h.to = q.to;
        h.seq = q.seq;
        transmit(h, q.payload);
        q.sent_ms = now_ms;
        ++resent;
    }
    if (!is_hub() && now_ms - last_hub_ms_ > cfg_.hub_timeout_ms) next_hub(now_ms);
    return out;
}

} // namespace d2k::net
