#pragma once
// The transport of a running network game (docs/design/multiplayer.md
// sections 6.1 to 6.3): a star through a hub, reliable messages end to
// end (per sender and receiver: sequence, ack, resend, duplicates dropped,
// no reordering, as the original), and the hub hand-over.

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

#include "net/lockstep.hpp"
#include "net/udp.hpp"

namespace d2k::net {

constexpr uint16_t kProtocol = 1;

class Transport {
public:
    struct Config {
        int local = 0;
        int hub = 0;                                 // the host at the start
        std::array<bool, 8> players{};               // the human sides of the game
        std::array<std::optional<Address>, 8> addr{}; // as the host knew them at the start
        uint32_t hub_timeout_ms = 2000;              // silence before the hub moves
    };
    struct Delivery {
        int from = -1;
        std::vector<uint8_t> data;
    };
    Transport(UdpSocket& socket, const Config& config);

    // A lockstep packet: to one side, or (-1) one copy per other player.
    void send(const Packet& p, uint64_t now_ms);
    // Reads the socket: forwards (as the hub), acks, drops duplicates,
    // returns the messages for this machine. Then resends what is due
    // (at most 16 per call) and moves the hub when it has been silent.
    std::vector<Delivery> poll(uint64_t now_ms, uint32_t resend_ms);
    // The side left the game: nothing more is sent to it, and it can no
    // longer become the hub.
    void set_gone(int side);
    int hub() const { return hub_; }
    bool is_hub() const { return hub_ == cfg_.local; }
    size_t unacked() const { return pending_.size(); }
    // Datagrams that were not transport messages (the lobby's, e.g. a
    // repeated start), for the lobby's handle().
    std::vector<Datagram> take_foreign() {
        std::vector<Datagram> v;
        v.swap(foreign_);
        return v;
    }
    // Test hook: this share (0..100) of outgoing datagrams is lost.
    int drop_percent = 0;

private:
    enum class Kind : uint8_t { Data = 1, Ack = 2, Hello = 3, HelloAck = 4 };
    struct Header {
        Kind kind = Kind::Data;
        bool reliable = false;
        int from = 0, to = 0, hop = 0;
        uint32_t seq = 0;
    };
    struct Pending {
        int to = 0;
        uint32_t seq = 0;
        std::vector<uint8_t> payload;
        uint64_t sent_ms = 0;
    };
    static std::vector<uint8_t> build(const Header& h, std::span<const uint8_t> payload);
    static bool parse(std::span<const uint8_t> d, Header& h, std::span<const uint8_t>& payload);
    void transmit(const Header& h, std::span<const uint8_t> payload); // by the route of h.to
    void raw(const Address& a, const Header& h, std::span<const uint8_t> payload);
    void next_hub(uint64_t now_ms);

    UdpSocket& sock_;
    Config cfg_;
    int hub_ = 0;
    std::array<bool, 8> gone_{};
    std::array<bool, 8> failed_hub_{};       // hubs that went silent
    std::array<uint32_t, 8> out_seq_{};      // the next sequence to each receiver
    std::array<std::set<uint32_t>, 8> seen_{}; // received sequences per sender (duplicates)
    std::vector<Pending> pending_;
    std::vector<Datagram> foreign_;
    uint64_t last_hub_ms_ = 0;
    bool started_ = false;
    uint32_t lcg_ = 0x2545F491;
};

} // namespace d2k::net
