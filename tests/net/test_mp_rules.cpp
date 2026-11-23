// Multiplayer rules settled from the exe while closing docs 57 / 58:
// the lobby's Accept counter (FUN_0048e820 case 0x16), message 6 with an
// empty map, and the ping a client reports with no pong (FUN_0045d350).
#include <doctest/doctest.h>

#include <filesystem>
#include <memory>

#include "formats/file_io.hpp"
#include "net/lobby.hpp"
#include "net/lockstep.hpp"
#include "platform/game_data.hpp"
#include "sim/game_setup.hpp"

using namespace d2k;
namespace fs = std::filesystem;

namespace {

struct Lobby3 {
    net::UdpSocket host_sock, sock1, sock2;
    std::unique_ptr<net::LobbyHost> host;
    std::unique_ptr<net::LobbyClient> c1, c2;
    uint64_t now = 1000;
    Lobby3() {
        REQUIRE(host_sock.open(0, true));
        REQUIRE(sock1.open(0, true));
        REQUIRE(sock2.open(0, true));
        net::LobbyPlayer me;
        me.name = "Host";
        me.colour = 3;
        host = std::make_unique<net::LobbyHost>(host_sock, me);
        c1 = std::make_unique<net::LobbyClient>(sock1);
        c2 = std::make_unique<net::LobbyClient>(sock2);
        c1->set_me("Alice", 1, 0);
        c2->set_me("Bob", 2, 5);
    }
    net::Address host_addr() const { return {0x7F000001, host_sock.port()}; }
    void step(int n = 1) {
        for (int i = 0; i < n; ++i) {
            now += 16;
            host->poll(now);
            c1->poll(now);
            c2->poll(now);
        }
    }
    void join(net::LobbyClient& c) {
        c.join(host_addr(), now);
        step(30);
        REQUIRE(c.status() == net::LobbyClient::Status::InLobby);
    }
    net::LobbySettings with_map(int credits) {
        net::LobbySettings s = host->state().settings;
        s.map = "4PLAY1";
        s.credits = credits;
        return s;
    }
};

} // namespace

TEST_CASE("lobby: a mismatched Accept counts too (FUN_0048e820 case 0x16): Start with one client still out") {
    Lobby3 room;
    room.join(*room.c1);
    room.join(*room.c2);
    room.host->set_settings(room.with_map(4000));
    room.step(20);
    // Alice presses Accept while the host moves a bar: her Accept is for
    // the old settings (0x18 {0}), but the counter goes up anyway.
    room.c1->accept();
    room.host->set_settings(room.with_map(4500));
    room.step(20);
    CHECK(!room.host->can_start());
    CHECK(!room.host->state().players[1].ready);
    // Her second Accept matches: counter 2 = members - 1, so Start lights
    // up although Bob never accepted (doc 58 section 5.4).
    room.c1->accept();
    room.step(20);
    CHECK(room.host->state().players[1].ready);
    CHECK(!room.host->state().players[2].ready);
    CHECK(room.host->can_start());
    // Any ResetAccept clears it again; then the normal way works.
    room.host->set_settings(room.with_map(5000));
    room.step(20);
    CHECK(!room.host->can_start());
    room.c1->accept();
    room.c2->accept();
    room.step(20);
    CHECK(room.host->can_start());
}

TEST_CASE("lobby: two players, a mismatched then a matching Accept: the count passed members - 1, no Start") {
    Lobby3 room;
    room.join(*room.c1);
    room.host->set_settings(room.with_map(4000));
    room.step(20);
    room.c1->accept();
    room.host->set_settings(room.with_map(4500)); // the Accept arrives stale: count 1, no match
    room.step(20);
    room.c1->accept(); // count 2 != 1: Start stays off (0x48f665 tests ==)
    room.step(20);
    CHECK(room.host->state().players[1].ready);
    CHECK(!room.host->can_start());
    room.host->set_settings(room.with_map(5000)); // ResetAccept: count 0
    room.step(20);
    room.c1->accept();
    room.step(20);
    CHECK(room.host->can_start());
}

TEST_CASE("lobby: a client without the map sends message 6 with an empty map; the map is cleared for all") {
    Lobby3 room;
    room.join(*room.c1);
    room.join(*room.c2);
    room.host->set_settings(room.with_map(4000));
    room.step(20);
    room.c1->accept();
    room.c2->accept();
    room.step(20);
    REQUIRE(room.host->can_start());
    room.c2->no_map("4PLAY1");
    room.step(20);
    CHECK(room.host->state().settings.map.empty());
    CHECK(room.c1->state().settings.map.empty());
    CHECK(!room.host->can_start()); // StartGame needs a map
    // The Accept marks stay on the host (FUN_004925a0 acts on client pages).
    CHECK(room.host->state().players[1].ready);
}

TEST_CASE("golden: lockstep: a client with no pong reports its loop rate as its ping (FUN_0045d350 with ECX) (skips without data)") {
    if (!fs::exists(fs::path(D2K_GAMEDATA_DIR) / "Data/maps/4PLAY1.MAP")) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    sim::GameSetup s;
    s.type = sim::GameSetup::Type::Network;
    s.mission = "4PLAY1";
    s.local = 1;
    s.humans = 2;
    s.colour[1] = 1;
    sim::Rng rng{1};
    auto g = sim::build_game(s, rng, [&gd](const std::string& p) -> std::optional<std::vector<uint8_t>> {
        auto f = gd.find(p);
        if (!f) return std::nullopt;
        return formats::read_file(*f);
    });
    net::Lockstep::Config c;
    c.local = 1;
    c.humans = 2;
    net::Lockstep ls(*g->world, c);
    for (int i = 0; i < 9; ++i) ls.second(57);
    bool found = false;
    for (const auto& p : ls.take_packets()) {
        if (p.data.size() < 10 || p.data[0] != 3) continue; // Kind::Report
        found = true;
        CHECK(p.to == 0);
        auto u32 = [&](size_t at) {
            return uint32_t(p.data[at]) | uint32_t(p.data[at + 1]) << 8 | uint32_t(p.data[at + 2]) << 16 |
                   uint32_t(p.data[at + 3]) << 24;
        };
        CHECK(u32(2) == 57); // the average loop rate over the 9 seconds
        CHECK(u32(6) == 57); // no pong: ECX = the rate's low word (0x45d3a2)
    }
    CHECK(found);
}
