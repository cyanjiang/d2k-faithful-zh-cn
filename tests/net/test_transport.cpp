// The UDP transport on localhost (docs/design/multiplayer.md section 6):
// star routing, end-to-end reliable delivery over a lossy link, and the
// hub hand-over.
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <set>

#include "net/transport.hpp"

using namespace d2k;

namespace {

// Three machines on 127.0.0.1, each with its own port; side 0 is the hub.
struct Lan {
    std::vector<std::unique_ptr<net::UdpSocket>> socks;
    std::vector<std::unique_ptr<net::Transport>> nets;
    std::vector<bool> down;
    uint64_t now = 0;
    explicit Lan(int n) {
        std::array<std::optional<net::Address>, 8> addr{};
        for (int i = 0; i < n; ++i) {
            socks.push_back(std::make_unique<net::UdpSocket>());
            REQUIRE(socks.back()->open(0, true));
            addr[size_t(i)] = net::Address{0x7F000001, socks.back()->port()};
        }
        for (int i = 0; i < n; ++i) {
            net::Transport::Config c;
            c.local = i;
            c.hub = 0;
            for (int p = 0; p < n; ++p) c.players[size_t(p)] = true;
            c.addr = addr; // every player's address, as the start message gives them
            nets.push_back(std::make_unique<net::Transport>(*socks[size_t(i)], c));
        }
        down.assign(size_t(n), false);
    }
    // One 16 ms pass on every live machine; what each received.
    std::vector<std::vector<net::Transport::Delivery>> step() {
        now += 16;
        std::vector<std::vector<net::Transport::Delivery>> got(nets.size());
        for (size_t i = 0; i < nets.size(); ++i) {
            if (!down[i]) got[i] = nets[i]->poll(now, 300);
        }
        return got;
    }
};

std::vector<uint8_t> msg(int a, int b) { return {uint8_t(a), uint8_t(b)}; }

} // namespace

TEST_CASE("transport: reliable broadcasts through the hub arrive once each, in spite of 30 % loss") {
    Lan lan(3);
    for (auto& n : lan.nets) n->drop_percent = 30;
    // Every machine sends 50 reliable broadcasts.
    for (int k = 0; k < 50; ++k) {
        for (int i = 0; i < 3; ++i) lan.nets[size_t(i)]->send({-1, true, msg(i, k)}, lan.now);
    }
    std::map<std::pair<int, int>, int> count; // (receiver, sender * 100 + k)
    for (int t = 0; t < 2000; ++t) {
        auto got = lan.step();
        for (int i = 0; i < 3; ++i) {
            for (const auto& d : got[size_t(i)]) {
                REQUIRE(d.data.size() == 2);
                CHECK(d.from == d.data[0]);
                ++count[{i, d.from * 100 + d.data[1]}];
            }
        }
        bool idle = true;
        for (auto& n : lan.nets) idle &= n->unacked() == 0;
        if (idle) break;
    }
    for (auto& n : lan.nets) CHECK(n->unacked() == 0);
    for (int i = 0; i < 3; ++i) {
        for (int s = 0; s < 3; ++s) {
            if (s == i) continue;
            for (int k = 0; k < 50; ++k) CHECK(count[{i, s * 100 + k}] == 1);
        }
    }
}

TEST_CASE("transport: when the hub goes silent, the lowest other player takes over and nothing is lost") {
    Lan lan(3);
    for (int t = 0; t < 10; ++t) {
        lan.nets[0]->send({-1, false, msg(0, 0)}, lan.now); // the hub is alive
        lan.step();
    }
    lan.down[0] = true;
    lan.socks[0]->close(); // the host is gone
    // Side 2 sends to side 1 now: it goes to the dead hub first.
    for (int k = 0; k < 5; ++k) lan.nets[2]->send({1, true, msg(2, k)}, lan.now);
    std::set<int> got_at_1;
    for (int t = 0; t < 400 && got_at_1.size() < 5; ++t) {
        auto got = lan.step();
        for (const auto& d : got[1]) got_at_1.insert(d.data[1]);
    }
    CHECK(lan.nets[1]->hub() == 1);
    CHECK(lan.nets[1]->is_hub());
    CHECK(lan.nets[2]->hub() == 1);
    CHECK(got_at_1.size() == 5);
    CHECK(lan.nets[2]->unacked() == 0);
}

TEST_CASE("transport: addresses parse") {
    auto a = net::Address::parse("127.0.0.1:2000", 1998);
    REQUIRE(a);
    CHECK(a->ip == 0x7F000001);
    CHECK(a->port == 2000);
    auto b = net::Address::parse("10.0.0.7", 1998);
    REQUIRE(b);
    CHECK(b->port == 1998);
    CHECK(b->text() == "10.0.0.7:1998");
    CHECK(!net::Address::parse("1.2.3.4:99999", 1998));
    CHECK(!net::Address::parse("", 1998));
}
