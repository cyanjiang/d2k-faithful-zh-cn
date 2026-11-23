#pragma once
// The lockstep of a network game (docs/re/57 sections 1.3 to 1.7 and 2;
// docs/design/multiplayer.md section 5): order messages, pulses and the
// confirmed ticks, the stall rule, the timeout and drop, the sync check,
// ping and rate reports, and the host's latency adaptation. No sockets:
// it takes and gives messages as bytes, so a test can be the network.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "sim/order.hpp"

namespace d2k::sim {
class World;
}

namespace d2k::net {

// One message for the transport. `to` = a side, or -1 for every other
// player (a broadcast; through the host in the star topology).
struct Packet {
    int to = -1;
    bool reliable = false;
    std::vector<uint8_t> data;
};

// Message kinds (the exe's message ids, docs/re/57 sections 1.3 and 5).
enum class Kind : uint8_t {
    Order = 1,   // 1000 / 0x3EF: an order, leave or latency record
    Pulse = 2,   // 0x3EA
    Report = 3,  // 0x3EB: loop rate and ping to the host
    Pong = 4,    // 0x3ED
    Ready = 5,   // record type 7: the start sync "I am ready"
    Chat = 6,    // misc record type 0x12: a chat line (docs/re/58 section 6.5)
    Speed = 7,   // system order 0x0B: a game speed change (doc 57 section 1.8)
};

class Lockstep {
public:
    struct Config {
        int local = 0;              // gSideId
        int humans = 2;             // 0x7984c0
        // FUN_0045a7a0, game type 2 (LAN): stall passes, seconds before the
        // drop, resend ms, half-ping clamp, latency factor.
        uint32_t stall_passes = 180;
        uint32_t drop_seconds = 6;
        uint32_t resend_ms = 300;
        uint32_t half_ping_min = 150, half_ping_max = 1500;
        float factor = 2.0f;        // 0x40000000
        // Our addition: sim::Game::data_hash, sent with "ready"; another
        // value from any player is out of sync from tick 0.
        uint64_t data_hash = 0;
    };
    Lockstep(sim::World& world, const Config& config);

    // A record the local player just posted (World::post / post_system):
    // sent to everyone, reliably.
    void sent(const sim::Order& o);
    // A message from side `from`.
    void on_message(int from, std::span<const uint8_t> data, uint64_t now_ms);
    // Once per loop pass, before the tick (FUN_0045c740's tail, the pulse
    // countdown): updates the stall state and sends the pulse when due.
    void pass(uint64_t now_ms);
    // The stall rule (doc 57 section 1.7): the World may run its next tick.
    bool may_tick() const { return !stalled_; }
    // Before each World::tick: the host's latency computation when every
    // report is in (in the exe's tick block, before the counter goes up).
    // `ticks_per_second` = the game speed's bucket (doc 02).
    void before_tick(uint32_t ticks_per_second);
    // After each World::tick: the tick's sync value into the sync table,
    // and the rate cap / resend time of a latency record that ran.
    void after_tick();
    uint32_t rate_cap() const { return cap_; } // 0x6b9864: limits the tick rate
    uint32_t resend_ms() const { return cfg_.resend_ms; } // 0x4e3b1c, for the transport
    // The 1-second timer (FUN_004a3796): `passes` = loop passes in the
    // last second; every 9th second the rate and ping report.
    void second(uint32_t passes);
    // A player leaves on its own (end of the loop, docs/re/57 section 3.8):
    // kind 0 Abort (the AI takes over), 1 retire / dropped, 0xFF won.
    sim::Order leave(int kind);

    std::vector<Packet> take_packets();

    // In-game chat (FUN_0047bae0 / FUN_0045c740, docs/re/58 section 6.5):
    // a line to one side or (-1) to all, reliable, outside the lockstep.
    struct ChatLine {
        int from = -1;
        std::string text;
    };
    void chat(int to, const std::string& text);
    // The speed setter 0x45b590 in a network game: order 0x0B {now +
    // latency, speed} to all and to this machine; each applies it at the
    // first pass with the tick reached (pacing only, not tick-exact).
    void speed(uint32_t value);
    // The speed change due at tick `now`, or -1.
    int take_speed(uint32_t now);
    std::vector<ChatLine> take_chats() {
        std::vector<ChatLine> v;
        v.swap(chats_);
        return v;
    }

    // The start sync (FUN_00448230, doc 57 section 3.3), called every pass
    // before tick 0 until it is not Waiting: sends "ready" once, waits for
    // every other human's. After 20 s the missing players leave (kind 1)
    // and the game starts with "You have lost connection with one or more
    // players" (take_lost_some); with nobody else ready: Alone
    // (NO_FIND_PLAYERS, back to the lobby).
    enum class StartSync { Waiting, Done, Alone };
    StartSync start_sync(uint64_t now_ms);
    // For the FOUND_PLAYERS box while the start sync waits: the humans
    // ready so far (me included) and the seconds left before the timeout.
    int sync_found() const;
    int sync_seconds_left(uint64_t now_ms) const;
    bool take_lost_some() {
        const bool b = lost_some_;
        lost_some_ = false;
        return b;
    }

    // State for the screen (MP_TIMEOUT, OUT_OF_SYNC, messages).
    struct Timeout {
        bool on = false;     // 0x6b98a0
        int player = -1;     // 0x6b93f0: the slow player
        uint32_t seconds = 0; // 0x6b93d8
    };
    const Timeout& timeout() const { return timeout_; }
    bool shows_timeout_box() const { return timeout_.on && timeout_.seconds >= 1; } // "Network Timeout section"
    int64_t out_of_sync_tick() const { return oos_tick_; } // 0x4e3b2c, -1 = none
    // "You have lost connection with one of the players": set once, taken
    // by the screen.
    bool take_lost_player() {
        const bool b = lost_player_;
        lost_player_ = false;
        return b;
    }
    uint32_t confirmed_tick(int p) const { return peers_[size_t(p & 7)].confirmed; }
    // The host role (the latency computation, wrapper byte +0): the
    // lowest-numbered human who is active and has not left
    // (docs/design/multiplayer.md section 6.3).
    int host_side() const;
    bool is_host() const { return host_side() == cfg_.local; }

    // Encoding of order records (also used by the transport tests).
    static std::vector<uint8_t> encode(const sim::Order& o);
    static bool decode(std::span<const uint8_t> data, sim::Order& o);

private:
    struct Peer {                 // docs/re/57 section 0, the per-player record
        uint32_t confirmed = 0;   // +0x00
        uint32_t rate = 60;       // +0x04 loop passes per second (report)
        uint32_t ping = 2000;     // +0x08 ms (report)
        uint32_t contiguous = 0;  // +0x18 highest contiguous sequence received
        std::vector<uint32_t> early; // +0x24 sequences received out of order
        uint32_t heard = 0;       // +0x2C tick when last heard
        uint32_t pulse_tick = 0;  // 0x6b8ad0
        bool reported = false;    // a rate report this period (FUN_0045c440)
        uint64_t rtt_sum = 0;     // 0x6b94d0
        uint32_t rtt_n = 0;       // 0x6b94d4
    };
    void bookkeep(int p, uint32_t seq); // FUN_0045bfa0
    void sync_record(uint32_t tick, uint16_t value); // FUN_0045aab0
    void send_pulse(uint64_t now_ms);
    void host_latency(uint32_t ticks_per_second); // FUN_0045c480
    uint32_t reported_ping(uint32_t ecx) const; // FUN_0045d350 (quirks kept)
    bool other_active(int p) const;

    sim::World& world_;
    Config cfg_;
    std::array<Peer, 8> peers_{};
    std::vector<Packet> out_;
    bool stalled_ = false;
    uint32_t stalled_passes_ = 0;  // 0x6b97a0
    Timeout timeout_;
    bool drop_posted_ = false;
    bool lost_player_ = false;
    uint32_t passes_per_second_ = 60; // 0x798518
    uint32_t pulse_countdown_ = 0;    // 0x6b9830
    uint32_t pulse_count_ = 0;        // 0x4dfb0c (every 25th carries a ping stamp)
    uint32_t seconds_ = 0;            // for the 9-second report
    uint64_t rate_sum_ = 0;           // loop passes over the report period
    uint32_t cap_ = 60;               // 0x6b9864, the shared rate cap
    bool cap_changed_ = false;        // 0x6b98d0
    struct SyncEntry {
        int64_t tick = -1;
        uint16_t value = 0;
    };
    std::array<SyncEntry, 150> sync_{}; // 0x6b8af0
    int last_host_ = 0;                 // to see the host change (pings reset)
    std::array<bool, 8> ready_{};       // start sync: "ready" received
    bool sync_started_ = false, sync_done_ = false, lost_some_ = false;
    uint64_t sync_t0_ = 0;
    int64_t oos_tick_ = -1;
    std::vector<ChatLine> chats_;
    std::vector<std::pair<uint32_t, uint32_t>> speeds_; // (tick, speed) not yet applied
};

} // namespace d2k::net
