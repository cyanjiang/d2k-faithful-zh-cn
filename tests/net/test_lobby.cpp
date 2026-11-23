// The lobby on localhost (docs/re/58-mp-front-end.md, our protocol in
// net/lobby.hpp): find, join with the original's refusals and colour fix,
// Accept and ResetAccept, Reject, the host's Cancel, the LAN presence and
// lobby chat; then the whole way into a running network game.
#include <doctest/doctest.h>

#include <filesystem>
#include <memory>

#include "formats/file_io.hpp"
#include "net/lobby.hpp"
#include "net/lockstep.hpp"
#include "platform/game_data.hpp"
#include "sim/world.hpp"

using namespace d2k;
namespace fs = std::filesystem;

namespace {

struct Room {
    net::UdpSocket host_sock, sock1, sock2;
    std::unique_ptr<net::LobbyHost> host;
    std::unique_ptr<net::LobbyClient> c1, c2;
    uint64_t now = 1000;
    Room() {
        REQUIRE(host_sock.open(0, true));
        REQUIRE(sock1.open(0, true));
        REQUIRE(sock2.open(0, true));
        net::LobbyPlayer me;
        me.name = "Host";
        me.house = 0;
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
    void join_both() {
        c1->join(host_addr(), now);
        step(5);
        c2->join(host_addr(), now);
        step(40);
        REQUIRE(c1->status() == net::LobbyClient::Status::InLobby);
        REQUIRE(c2->status() == net::LobbyClient::Status::InLobby);
    }
    net::LobbySettings map4() {
        net::LobbySettings s = host->state().settings;
        s.map = "4PLAY1";
        s.ai_players = 1;
        return s;
    }
};

} // namespace

TEST_CASE("lobby: a game found at an address; joining keeps the join order; the exe's defaults") {
    Room room;
    room.c1->add_address(room.host_addr());
    room.c1->probe(1); // no LAN listener on port 1: only the address answers
    room.step(5);
    REQUIRE(room.c1->games().size() == 1);
    const auto& g = room.c1->games()[0];
    CHECK(g.game_name == "Host's Game");
    CHECK(g.version == net::game_version());
    REQUIRE(g.members.size() == 1);
    CHECK(g.members[0].name == "Host");
    room.join_both();
    CHECK(room.c1->my_index() == 1);
    CHECK(room.c2->my_index() == 2);
    CHECK(room.c2->state().settings.credits == 4000); // gNetStartingCredits
    CHECK(room.c2->state().settings.map.empty());     // NoMapSelected
}

TEST_CASE("lobby: a game that answers from two addresses is one row") {
    // A host on two networks (LAN and loopback, a VPN) answers a probe once
    // per network. Here a relay socket passes the probe and the answer on,
    // so the same game also arrives from the relay's address.
    Room room;
    net::UdpSocket relay;
    REQUIRE(relay.open(0, true));
    const net::Address relay_addr{0x7F000001, relay.port()};
    room.c1->add_address(room.host_addr());
    room.c1->add_address(relay_addr);
    room.c1->probe(1);
    for (int i = 0; i < 10; ++i) {
        room.step();
        while (auto d = relay.receive()) {
            if (d->from.port == room.host_sock.port()) relay.send({0x7F000001, room.sock1.port()}, d->data);
            else relay.send(room.host_addr(), d->data);
        }
    }
    REQUIRE(room.c1->games().size() == 1);
    CHECK(room.c1->games()[0].host == room.host_addr()); // the first address that answered
}

TEST_CASE("lobby: a colour already used in the game becomes the lowest free one; names must differ") {
    Room room;
    room.c1->set_me("Alice", 1, 3); // the host's colour
    room.c1->join(room.host_addr(), room.now);
    room.step(20);
    REQUIRE(room.c1->status() == net::LobbyClient::Status::InLobby);
    CHECK(room.host->state().players[1].colour == 0);
    room.c2->set_me("Alice", 2, 5);
    room.c2->join(room.host_addr(), room.now);
    room.step(20);
    CHECK(room.c2->status() == net::LobbyClient::Status::Rejected);
    CHECK(room.c2->reject_reason() == "NameIsUsed");
}

TEST_CASE("lobby: Accept, ResetAccept on every change, and Start only with every Accept of the current settings") {
    Room room;
    room.join_both();
    room.host->set_settings(room.map4());
    room.step(20);
    CHECK(!room.host->can_start());
    room.c1->accept();
    room.c2->accept();
    room.step(20);
    CHECK(room.host->can_start());
    // The host moves a bar: every Accept is gone.
    auto s = room.map4();
    s.credits = 6000;
    room.host->set_settings(s);
    room.step(20);
    CHECK(!room.host->can_start());
    CHECK(!room.c1->state().players[1].ready);
    room.c1->accept();
    room.c2->accept();
    room.step(20);
    REQUIRE(room.host->can_start());
    // A handicap change clears them too (AlterHandicap).
    room.host->cycle_handicap(2);
    room.step(20);
    CHECK(room.c2->state().players[2].handicap == 2);
    CHECK(!room.host->can_start());
    // Chat does not.
    room.c1->accept();
    room.c2->accept();
    room.step(20);
    room.c1->chat("hello");
    room.step(20);
    CHECK(room.host->can_start());
    REQUIRE(!room.c2->state().chat.empty());
    CHECK(room.c2->state().chat.back().text == "hello");
    CHECK(room.c2->state().chat.back().name == "Alice");
}

TEST_CASE("lobby: humans + AI stay within 6; a full game refuses") {
    Room room;
    auto s = room.map4();
    s.ai_players = 5; // 1 human + 5
    room.host->set_settings(s);
    room.c1->join(room.host_addr(), room.now);
    room.step(20);
    REQUIRE(room.c1->status() == net::LobbyClient::Status::InLobby);
    CHECK(room.host->state().settings.ai_players == 4); // lowered to 6 - humans
}

TEST_CASE("lobby: Reject and the host's Cancel send the clients back without a message") {
    Room room;
    room.join_both();
    room.host->reject(room.host->state().players[1].id);
    room.step(20);
    CHECK(room.c1->status() == net::LobbyClient::Status::Rejected);
    CHECK(room.c1->reject_reason().empty());
    CHECK(room.c2->status() == net::LobbyClient::Status::InLobby);
    room.host->close();
    room.step(40);
    CHECK(room.c2->status() == net::LobbyClient::Status::Closed);
}

TEST_CASE("lobby: LAN presence and lobby chat on IPXLOBBY") {
    Room room;
    room.c1->set_lan_port(room.sock2.port()); // each "LAN" listener here is the other's port
    room.c2->set_lan_port(room.sock1.port());
    room.step(80);
    REQUIRE(room.c1->lobby_players().size() == 1);
    CHECK(room.c1->lobby_players()[0].name == "Bob");
    CHECK(room.c2->lobby_players()[0].name == "Alice");
    room.c1->lobby_say("anyone up for a game?");
    room.step(40);
    REQUIRE(!room.c2->lobby_chat().empty());
    CHECK(room.c2->lobby_chat().back().text == "anyone up for a game?");
    CHECK(room.c2->lobby_chat().size() == 1); // the three repeats are one line
}

TEST_CASE("lobby: a player on another port than the LAN port still hears the lobby (two copies on one PC)") {
    // c1 has the LAN port; c2 had to take another, so broadcasts to the
    // LAN port never reach it. c1 hears c2 and answers it directly.
    Room room;
    room.c1->set_lan_port(room.sock1.port());
    room.c2->set_lan_port(room.sock1.port());
    room.step(80);
    REQUIRE(room.c1->lobby_players().size() == 1);
    REQUIRE(room.c2->lobby_players().size() == 1);
    CHECK(room.c1->lobby_players()[0].name == "Bob");
    CHECK(room.c2->lobby_players()[0].name == "Alice");
    room.c1->lobby_say("hello");
    room.step(40);
    REQUIRE(room.c2->lobby_chat().size() == 1);
    CHECK(room.c2->lobby_chat().back().text == "hello");
}

TEST_CASE("golden: lobby to a running game: three machines start in step over UDP (skips without data)") {
    if (!fs::exists(fs::path(D2K_GAMEDATA_DIR) / "Data/maps/4PLAY1.MAP")) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    Room room;
    room.join_both();
    room.host->set_settings(room.map4());
    room.step(20);
    room.c1->accept();
    room.c2->accept();
    room.step(30);
    REQUIRE(room.host->can_start());
    std::array<net::GameStart, 3> starts{room.host->start(777), {}, {}};
    room.step(30);
    REQUIRE(room.c1->status() == net::LobbyClient::Status::Started);
    REQUIRE(room.c2->status() == net::LobbyClient::Status::Started);
    CHECK(room.host->all_confirmed());
    starts[1] = room.c1->game();
    starts[2] = room.c2->game();
    CHECK(starts[1].local == 1);
    CHECK(starts[2].setup().names[1] == "Alice");

    std::array<net::UdpSocket*, 3> socks{&room.host_sock, &room.sock1, &room.sock2};
    struct Node {
        sim::Rng rng;
        std::unique_ptr<sim::Game> g;
        std::unique_ptr<net::Transport> tr;
        std::unique_ptr<net::Lockstep> ls;
        bool started = false;
    };
    std::array<Node, 3> nodes;
    auto read = [&gd](const std::string& p) -> std::optional<std::vector<uint8_t>> {
        auto f = gd.find(p);
        if (!f) return std::nullopt;
        return formats::read_file(*f);
    };
    for (int i = 0; i < 3; ++i) {
        auto& n = nodes[size_t(i)];
        n.rng.seed(starts[size_t(i)].seed);
        n.g = sim::build_game(starts[size_t(i)].setup(), n.rng, read);
        n.tr = std::make_unique<net::Transport>(*socks[size_t(i)], starts[size_t(i)].transport());
        net::Lockstep::Config c;
        c.local = i;
        c.humans = 3;
        c.data_hash = n.g->data_hash;
        n.ls = std::make_unique<net::Lockstep>(*n.g->world, c);
    }
    uint64_t now = room.now;
    const uint32_t end = 400;
    for (uint64_t pass = 0; pass < 60 * 120; ++pass) {
        now += 16;
        for (int i = 0; i < 3; ++i) {
            auto& n = nodes[size_t(i)];
            for (auto& d : n.tr->poll(now, 300)) n.ls->on_message(d.from, d.data, now);
            n.tr->take_foreign(); // late lobby repeats
            if (!n.started) {
                n.started = n.ls->start_sync(now) == net::Lockstep::StartSync::Done;
            } else {
                n.ls->pass(now);
                auto& w = *n.g->world;
                if (pass % 3 == 0 && n.ls->may_tick() && w.tick_count() < end) {
                    n.ls->before_tick(20);
                    w.tick();
                    n.ls->after_tick();
                }
                if (pass % 60 == 59) n.ls->second(60);
            }
            for (auto& p : n.ls->take_packets()) n.tr->send(p, now);
        }
        bool done = true;
        for (auto& n : nodes) done &= n.g->world->tick_count() == end;
        if (done) break;
    }
    for (auto& n : nodes) {
        CHECK(n.started);
        CHECK(n.g->world->tick_count() == end);
        CHECK(!n.ls->take_lost_some());
        CHECK(n.ls->out_of_sync_tick() == -1);
    }
    CHECK(nodes[0].g->data_hash == nodes[1].g->data_hash);
    CHECK(nodes[0].g->data_hash != 0xcbf29ce484222325ull); // the files went in
    CHECK(nodes[0].rng.state() == nodes[1].rng.state());
    CHECK(nodes[0].rng.state() == nodes[2].rng.state());
    CHECK(nodes[1].g->world->player(2).house == 2);
}
