// Network games (docs/re/57-multiplayer.md, docs/design/multiplayer.md):
// the shared set-up and the lockstep simulation.
#include <doctest/doctest.h>

#include <filesystem>
#include <set>

#include "formats/file_io.hpp"
#include "net/lockstep.hpp"
#include "net/transport.hpp"
#include "platform/game_data.hpp"
#include "platform/sha256.hpp"
#include "sim/game_setup.hpp"

using namespace d2k;
namespace fs = std::filesystem;

namespace {

bool have_data() { return fs::exists(fs::path(D2K_GAMEDATA_DIR) / "Data/maps/4PLAY1.MAP"); }

sim::FileReader reader(const platform::GameData& gd) {
    return [&gd](const std::string& p) -> std::optional<std::vector<uint8_t>> {
        auto f = gd.find(p);
        if (!f) return std::nullopt;
        return formats::read_file(*f);
    };
}

// A network game of `humans` players plus `ai` computer players on 4PLAY1,
// as one machine (`local`) builds it from the host's settings and seed.
sim::GameSetup net_setup(int humans, int ai, int local) {
    sim::GameSetup s;
    s.type = sim::GameSetup::Type::Network;
    s.mission = "4PLAY1";
    s.local = local;
    s.humans = humans;
    s.ai_players = ai;
    s.credits = 5000;
    s.tech = 7;
    s.unit_count = 6;
    s.worms = 1;
    s.crates = true;
    for (int i = 0; i < humans; ++i) {
        s.house[size_t(i)] = uint8_t(i % 3);
        s.colour[size_t(i)] = uint8_t(i == 0 ? 3 : i == 1 ? 0 : 5);
        s.handicap[size_t(i)] = 1;
    }
    return s;
}

} // namespace

TEST_CASE("golden: build_game: the same set-up and seed give the same game (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    sim::GameSetup s;
    s.type = sim::GameSetup::Type::Skirmish;
    s.mission = "4PLAY1";
    s.ai_players = 3;
    s.credits = 7000;
    s.tech = 7;
    s.unit_count = 6;
    s.worms = 2;
    s.colour[0] = 2;
    s.house[0] = 1;
    sim::Rng ra{123}, rb{123};
    auto a = sim::build_game(s, ra, reader(gd));
    auto b = sim::build_game(s, rb, reader(gd));
    for (int i = 0; i < 1500; ++i) {
        a->world->tick();
        b->world->tick();
    }
    CHECK(platform::sha256_hex(a->world->save_state()) == platform::sha256_hex(b->world->save_state()));
    CHECK(a->world->player(0).house == 1);
    CHECK(a->world->side_colour[0] == 2);
}

TEST_CASE("golden: build_game: network set-up (docs/re/57 section 3.4) (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    sim::Rng rng{99};
    auto g = sim::build_game(net_setup(2, 2, 1), rng, reader(gd));
    const auto& w = *g->world;
    CHECK(w.network);
    // The humans keep their lobby colours; sides 2..5 are rolled, never 4
    // or 7, all different.
    CHECK(w.side_colour[0] == 3);
    CHECK(w.side_colour[1] == 0);
    std::set<int> seen;
    for (int i = 0; i < 6; ++i) {
        CHECK(w.side_colour[size_t(i)] != 4);
        CHECK(w.side_colour[size_t(i)] != 7);
        seen.insert(w.side_colour[size_t(i)]);
    }
    CHECK(seen.size() == 6);
    // Every side starts with gNetStartingCredits; the AI are sides 2 and 3.
    for (int p = 0; p < 4; ++p) CHECK(w.player(p).cash == 5000);
    std::array<int, 8> mcvs{};
    for (const auto& u : w.units()) {
        if (g->templates.unit_records[size_t(u.record)].kind == 6) ++mcvs[size_t(u.owner)];
    }
    for (int p = 0; p < 4; ++p) CHECK(mcvs[size_t(p)] == 1);
    CHECK(mcvs[4] == 0);
    CHECK(w.player(0).house == 0);
    CHECK(w.player(1).house == 1);
    // Cost by handicap: the humans normal (1), the AI sides' byte stays 0
    // (easy: 75 %), as FUN_004481b0 leaves it.
    CHECK(w.factor_level(0) == 1);
    CHECK(w.factor_level(2) == 0);
}

TEST_CASE("golden: build_game: network machines build the same game whatever their local side (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    sim::Rng r0{4242}, r1{4242};
    auto a = sim::build_game(net_setup(2, 1, 0), r0, reader(gd));
    auto b = sim::build_game(net_setup(2, 1, 1), r1, reader(gd));
    // The local side's own data (shroud, voices) differs; the shared state
    // must not: compare the units' positions, owners and states.
    for (int i = 0; i < 1500; ++i) {
        a->world->tick();
        b->world->tick();
    }
    const auto& ua = a->world->units();
    const auto& ub = b->world->units();
    REQUIRE(ua.size() == ub.size());
    for (size_t i = 0; i < ua.size(); ++i) {
        CHECK(ua[i].index == ub[i].index);
        CHECK(ua[i].owner == ub[i].owner);
        CHECK(ua[i].x == ub[i].x);
        CHECK(ua[i].y == ub[i].y);
        CHECK(ua[i].state == ub[i].state);
    }
    for (int p = 0; p < 3; ++p) CHECK(a->world->player(p).cash == b->world->player(p).cash);
}

namespace {

// One machine of a network game: its own RNG (seeded alike, as by the
// host's seed) and its own game, built with its local side.
struct Machine {
    sim::Rng rng{20261014};
    std::unique_ptr<sim::Game> g;
    Machine(const platform::GameData& gd, int humans, int ai, int local, uint8_t thick = 0) {
        auto s = net_setup(humans, ai, local);
        s.thick.fill(thick); // FUN_00466140's pattern: each machine rolls its own before the seed
        g = sim::build_game(s, rng, reader(gd));
    }
    sim::World& w() { return *g->world; }
};

// The state every machine must agree on (no selection, shroud, messages
// or sounds): RNG, units, buildings, cash, alliances.
std::vector<int64_t> shared_state(Machine& m) {
    auto& w = m.w();
    std::vector<int64_t> v{int64_t(m.rng.state()), int64_t(w.tick_count())};
    for (const auto& u : w.units()) {
        for (int64_t x : {int64_t(u.index), int64_t(u.owner), int64_t(u.x), int64_t(u.y), int64_t(u.state),
                          int64_t(u.hit_points), int64_t(u.target)}) {
            v.push_back(x);
        }
    }
    for (const auto& b : w.buildings()) {
        for (int64_t x : {int64_t(b.owner), int64_t(b.record), int64_t(b.hit_points), int64_t(b.removed), int64_t(b.dying)}) {
            v.push_back(x);
        }
    }
    for (int p = 0; p < 8; ++p) {
        v.push_back(w.player(p).cash);
        for (int q = 0; q < 8; ++q) v.push_back(w.alliance(p, q));
    }
    return v;
}

// The local player selects all its units and posts an order record; the
// other machine receives it at once (a perfect network).
sim::Order post_from(Machine& from, Machine& to, sim::Order o) {
    for (auto& u : from.w().units()) u.selected = u.owner == o.side;
    const sim::Order sent = from.w().post(o);
    for (auto& u : from.w().units()) u.selected = false;
    to.w().receive(sent);
    return sent;
}

int first_unit_x(Machine& m, int side) {
    for (const auto& u : m.w().units()) {
        if (u.owner == side) return u.cur_x;
    }
    return -1;
}

} // namespace

TEST_CASE("golden: network queue: orders of two machines run alike on both (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    Machine a(gd, 2, 1, 0), b(gd, 2, 1, 1);
    CHECK(a.w().latency() == 40);
    CHECK(shared_state(a) == shared_state(b));
    for (int t = 0; t < 900; ++t) {
        if (t == 50) {
            const int x = std::clamp(first_unit_x(a, 0) + 8, 0, a.w().map().width() - 1);
            const sim::Order o = post_from(a, b, sim::make_order(sim::order::kMove, 0, x, 30));
            CHECK(o.seq == 1);
            CHECK(o.tick == a.w().tick_count() + 40);
            CHECK(!o.ids.empty());
        }
        if (t == 80) {
            const int x = std::clamp(first_unit_x(b, 1) - 8, 0, b.w().map().width() - 1);
            post_from(b, a, sim::make_order(sim::order::kMove, 1, x, 20));
        }
        a.w().tick();
        b.w().tick();
        CHECK(a.w().sync_value() == b.w().sync_value());
    }
    CHECK(a.w().next_seq(0) == 2); // side 0's order ran on both
    CHECK(b.w().next_seq(0) == 2);
    CHECK(a.w().next_seq(1) == 2);
    CHECK(!a.w().model_out_of_sync());
    CHECK(shared_state(a) == shared_state(b));
}

TEST_CASE("golden: network: machines with different thick-spice patterns stay identical; only the pictures differ (docs/re/57 section 1.9) (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    // Two humans and two AI on 4PLAY1 (spice fields, harvesting, worms):
    // machine 0 rolled pattern 0 everywhere, machine 1 pattern 3.
    Machine a(gd, 2, 2, 0, 0), b(gd, 2, 2, 1, 3);
    int differing = 0;
    for (int y = 0; y < a.w().map().height(); ++y) {
        for (int x = 0; x < a.w().map().width(); ++x) {
            differing += a.w().map().at(x, y).tile != b.w().map().at(x, y).tile;
        }
    }
    CHECK(differing > 0); // the test is meaningful: the thick cells show other tiles
    for (int t = 0; t < 6000; ++t) {
        a.w().tick();
        b.w().tick();
    }
    CHECK(shared_state(a) == shared_state(b));
    for (int y = 0; y < a.w().map().height(); ++y) {
        for (int x = 0; x < a.w().map().width(); ++x) {
            REQUIRE(a.w().map().at(x, y).flags == b.w().map().at(x, y).flags); // spice levels and all cell flags
        }
    }
}

TEST_CASE("golden: network queue: an order run late is \"Model Out Of Sync\" (FUN_0045d120) (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    Machine a(gd, 2, 0, 0), b(gd, 2, 0, 1);
    for (auto& u : a.w().units()) u.selected = u.owner == 0;
    const sim::Order o = a.w().post(sim::make_order(sim::order::kGuard, 0));
    for (int t = 0; t < 45; ++t) b.w().tick(); // past the order's tick
    b.w().receive(o);
    b.w().tick();
    CHECK(b.w().model_out_of_sync());
    CHECK(b.w().next_seq(0) == 1); // blocked
}

TEST_CASE("golden: network queue: a latency change raises later orders to the floor (FUN_0045cda0 / FUN_0045a540) (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    Machine a(gd, 2, 0, 0), b(gd, 2, 0, 1);
    for (int t = 0; t < 10; ++t) {
        a.w().tick();
        b.w().tick();
    }
    const uint32_t t0 = a.w().tick_count();
    // The host posts latency 8 for now + max(8, 40).
    sim::Order lat = sim::make_order(sim::order::kLatency, 0, 8, 300, 0);
    lat.tick = t0 + 40;
    b.w().receive(a.w().post_system(lat));
    for (int t = 0; t < 41; ++t) {
        a.w().tick();
        b.w().tick();
    }
    CHECK(a.w().latency() == 8);
    CHECK(b.w().latency() == 8);
    CHECK(a.w().next_seq(0) == 2);
    // floor = old latency 40 + the record's tick: an order posted now with
    // latency 8 waits until then.
    const uint32_t floor = t0 + 40 + 40;
    const sim::Order o = post_from(a, b, sim::make_order(sim::order::kGuard, 0));
    CHECK(o.tick < floor);
    while (a.w().tick_count() < floor - 1) {
        a.w().tick();
        b.w().tick();
    }
    CHECK(a.w().next_seq(0) == 2);
    a.w().tick();
    b.w().tick();
    CHECK(a.w().next_seq(0) == 3);
    CHECK(b.w().next_seq(0) == 3);
    CHECK(shared_state(a) == shared_state(b));
}

TEST_CASE("golden: network queue: leaving, surrender and alliances (docs/re/57 sections 3.6, 3.8) (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    Machine a(gd, 3, 1, 0), b(gd, 3, 1, 1), c(gd, 3, 1, 2);
    std::array<Machine*, 3> all{&a, &b, &c};
    auto tick = [&](int n) {
        for (int i = 0; i < n; ++i) {
            for (auto* m : all) m->w().tick();
        }
    };
    auto broadcast = [&](Machine& from, const sim::Order& o) {
        for (auto* m : all) {
            if (m != &from) m->w().receive(o);
        }
    };
    tick(20);
    // Side 0 allies with side 1 (an AI side 3 remains an enemy: allowed).
    for (auto& u : a.w().units()) u.selected = false;
    broadcast(a, a.w().post(sim::make_order(sim::order::kAlly, 0, 1)));
    // Side 1 leaves with Abort: the AI takes over its side. Posted a little
    // later: a leave due at the same tick runs first (FUN_0045cda0 before
    // the sides), and allying with an AI side is refused.
    tick(5);
    {
        sim::Order leave = sim::make_order(sim::order::kLeave, 1, 0, 1);
        leave.tick = b.w().tick_count() + b.w().latency();
        broadcast(b, b.w().post_system(leave));
    }
    // Side 2 surrenders: its side is destroyed.
    broadcast(c, c.w().post(sim::make_order(sim::order::kRetire, 2)));
    tick(60);
    for (auto* m : all) {
        CHECK(m->w().alliance(0, 1) == 0);
        CHECK(m->w().alliance(1, 0) == 1); // one way
        CHECK(!m->w().human_active(1));
        CHECK(m->w().ai_active(1));
        CHECK(!m->w().player_active(2));
        int alive = 0;
        for (const auto& u : m->w().units()) {
            if (u.owner == 2 && u.state != 0x11 && !u.remove) ++alive;
        }
        CHECK(alive == 0);
    }
    tick(300);
    CHECK(shared_state(a) == shared_state(b));
    CHECK(shared_state(a) == shared_state(c));
}

TEST_CASE("golden: network queue: two players, one aborts: the other plays on against the AI (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    Machine a(gd, 2, 0, 0), b(gd, 2, 0, 1);
    for (int i = 0; i < 20; ++i) {
        a.w().tick();
        b.w().tick();
    }
    sim::Order leave = sim::make_order(sim::order::kLeave, 0, 0, 0); // side 0 aborts: kind 0
    leave.tick = a.w().tick_count() + a.w().latency();
    b.w().receive(a.w().post_system(leave));
    for (int i = 0; i < 200; ++i) {
        a.w().tick();
        b.w().tick();
    }
    CHECK(!b.w().human_active(0));
    CHECK(b.w().ai_active(0));
    CHECK(b.w().player_active(0));
    CHECK(!b.w().won);
    CHECK(!b.w().lost);
}

namespace {

// A lockstep network in one process: each machine is a World plus a
// Lockstep; a fake link delivers messages after a delay (reliable ones
// always, in any order; unreliable ones lost at a rate). One loop pass is
// 1/60 s; the game ticks every 3rd pass (20 ticks per second) when the
// lockstep allows it.
struct Net {
    struct Node {
        std::unique_ptr<Machine> m;
        std::unique_ptr<net::Lockstep> ls;
        bool silent = false; // cut off: sends and receives nothing
    };
    struct Msg {
        uint64_t at;
        int to, from;
        std::vector<uint8_t> data;
    };
    std::vector<Node> nodes;
    std::vector<Msg> wire;
    uint64_t pass = 0;
    uint32_t lcg = 12345;
    int loss_percent = 10, delay_min = 3, delay_max = 12;
    uint32_t rnd() { return (lcg = lcg * 1103515245u + 12345u) >> 16; }

    Net(const platform::GameData& gd, int humans, int ai) {
        for (int s = 0; s < humans; ++s) {
            Node n;
            n.m = std::make_unique<Machine>(gd, humans, ai, s);
            net::Lockstep::Config c;
            c.local = s;
            c.humans = humans;
            n.ls = std::make_unique<net::Lockstep>(n.m->w(), c);
            nodes.push_back(std::move(n));
        }
    }
    // Side s selects its own units and posts an order.
    void order(int s, sim::Order o) {
        auto& w = nodes[size_t(s)].m->w();
        for (auto& u : w.units()) u.selected = u.owner == s;
        nodes[size_t(s)].ls->sent(w.post(o));
        for (auto& u : w.units()) u.selected = false;
    }
    // One loop pass on every machine; ticks only up to `limit`.
    void step(uint32_t limit) {
        const uint64_t now_ms = pass * 1000 / 60;
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto& n = nodes[i];
            for (size_t k = 0; k < wire.size();) {
                if (wire[k].to == int(i) && wire[k].at <= pass) {
                    if (!n.silent) n.ls->on_message(wire[k].from, wire[k].data, now_ms);
                    wire.erase(wire.begin() + std::ptrdiff_t(k));
                } else {
                    ++k;
                }
            }
            n.ls->pass(now_ms);
            auto& w = n.m->w();
            if (pass % 3 == 0 && n.ls->may_tick() && w.tick_count() < limit) {
                n.ls->before_tick(20);
                w.tick();
                n.ls->after_tick();
            }
            if (pass % 60 == 59) n.ls->second(60);
            for (auto& p : n.ls->take_packets()) {
                if (n.silent) continue;
                for (size_t j = 0; j < nodes.size(); ++j) {
                    if (j == i || (p.to >= 0 && p.to != int(j))) continue;
                    if (!p.reliable && int(rnd() % 100) < loss_percent) continue;
                    const uint64_t delay = uint64_t(delay_min) + rnd() % uint32_t(delay_max - delay_min + 1);
                    wire.push_back({pass + delay, int(j), int(i), p.data});
                }
            }
        }
        ++pass;
    }
};

} // namespace

TEST_CASE("golden: lockstep: different game data shows as out of sync at the start sync (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    for (bool same : {true, false}) {
        std::array<std::unique_ptr<Machine>, 2> m;
        std::array<std::unique_ptr<net::Lockstep>, 2> ls;
        for (int s = 0; s < 2; ++s) {
            m[size_t(s)] = std::make_unique<Machine>(gd, 2, 0, s);
            net::Lockstep::Config c;
            c.local = s;
            c.humans = 2;
            c.data_hash = (same || s == 0) ? 0x1234 : 0x1235;
            ls[size_t(s)] = std::make_unique<net::Lockstep>(m[size_t(s)]->w(), c);
        }
        for (int round = 0; round < 3; ++round) {
            for (int s = 0; s < 2; ++s) {
                ls[size_t(s)]->start_sync(1000);
                for (auto& p : ls[size_t(s)]->take_packets()) ls[size_t(1 - s)]->on_message(s, p.data, 1000);
            }
        }
        for (auto& l : ls) {
            CHECK(l->start_sync(1000) == net::Lockstep::StartSync::Done);
            CHECK(l->out_of_sync_tick() == (same ? -1 : 0));
        }
    }
}

TEST_CASE("golden: lockstep: chat lines and the speed order 0x0B reach the other machine (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    std::array<std::unique_ptr<Machine>, 2> m;
    std::array<std::unique_ptr<net::Lockstep>, 2> ls;
    for (int s = 0; s < 2; ++s) {
        m[size_t(s)] = std::make_unique<Machine>(gd, 2, 0, s);
        net::Lockstep::Config c;
        c.local = s;
        c.humans = 2;
        ls[size_t(s)] = std::make_unique<net::Lockstep>(m[size_t(s)]->w(), c);
    }
    auto deliver = [&](int from) {
        for (auto& p : ls[size_t(from)]->take_packets()) {
            CHECK(p.reliable);
            ls[size_t(1 - from)]->on_message(from, p.data, 1000);
        }
    };
    ls[0]->chat(-1, "Alice : hello");
    ls[0]->speed(40);
    deliver(0);
    const auto chats = ls[1]->take_chats();
    REQUIRE(chats.size() == 1);
    CHECK(chats[0].from == 0);
    CHECK(chats[0].text == "Alice : hello");
    const uint32_t due = m[0]->w().tick_count() + m[0]->w().latency();
    CHECK(ls[1]->take_speed(due - 1) == -1); // not before its tick
    CHECK(ls[1]->take_speed(due) == 40);
    CHECK(ls[0]->take_speed(due) == 40);    // queued on the sender too
    CHECK(ls[0]->take_speed(due) == -1);
}

TEST_CASE("golden: lockstep: three machines over a lossy link stay identical; the host adapts the latency (docs/re/57 sections 1.5-1.7, 2) (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    Net net(gd, 3, 1);
    const uint32_t end = 1500; // 75 s of game
    bool stalled_once = false;
    while (net.pass < 200000) {
        // Each human gives a move order every ~2 s (different times).
        for (int s = 0; s < 3; ++s) {
            if (net.pass % 120 == uint64_t(s * 37 + 11) && net.nodes[size_t(s)].m->w().tick_count() < end - 100) {
                auto& w = net.nodes[size_t(s)].m->w();
                const int x = int(net.rnd() % uint32_t(w.map().width()));
                const int y = int(net.rnd() % uint32_t(w.map().height()));
                net.order(s, sim::make_order(sim::order::kMove, s, x, y));
            }
        }
        net.step(end);
        for (auto& n : net.nodes) stalled_once |= !n.ls->may_tick();
        bool done = true;
        for (auto& n : net.nodes) done &= n.m->w().tick_count() == end;
        if (done) break;
    }
    for (auto& n : net.nodes) {
        CHECK(n.m->w().tick_count() == end);
        CHECK(n.ls->out_of_sync_tick() == -1);
        CHECK(!n.m->w().model_out_of_sync());
        CHECK(n.m->w().latency() < 40);              // adapted down on a fast link
        CHECK(n.m->w().latency() == net.nodes[0].m->w().latency());
        CHECK(n.m->w().next_seq(1) > 10);            // the orders ran
    }
    CHECK(stalled_once); // the 40-tick start latency with a 50-200 ms link stalls now and then
    CHECK(shared_state(*net.nodes[0].m) == shared_state(*net.nodes[1].m));
    CHECK(shared_state(*net.nodes[0].m) == shared_state(*net.nodes[2].m));
}

TEST_CASE("golden: lockstep: a silent player stalls the others, then the timeout drops it (docs/re/57 section 1.7) (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    Net net(gd, 3, 0);
    net.loss_percent = 0;
    while (net.nodes[0].m->w().tick_count() < 200) net.step(100000);
    net.nodes[2].silent = true;
    bool stalled = false, box = false, lost = false;
    for (int i = 0; i < 60 * 30 && !net.nodes[0].m->w().has_left(2) && net.nodes[0].m->w().human_active(2); ++i) {
        net.step(100000);
        stalled |= !net.nodes[0].ls->may_tick();
        box |= net.nodes[0].ls->shows_timeout_box();
        lost |= net.nodes[0].ls->take_lost_player();
    }
    CHECK(stalled);
    CHECK(box);
    CHECK(lost);
    const uint32_t t0 = net.nodes[0].m->w().tick_count();
    for (int i = 0; i < 60 * 10; ++i) net.step(100000);
    for (int s = 0; s < 2; ++s) {
        auto& w = net.nodes[size_t(s)].m->w();
        CHECK(!w.human_active(2));
        CHECK(!w.player_active(2)); // removed (kind 1)
        CHECK(w.tick_count() > t0 + 100); // the game goes on
    }
    // Stop both at the same tick and compare.
    const uint32_t end = std::max(net.nodes[0].m->w().tick_count(), net.nodes[1].m->w().tick_count()) + 20;
    for (int i = 0; i < 60 * 20; ++i) net.step(end);
    CHECK(net.nodes[0].m->w().tick_count() == end);
    CHECK(net.nodes[1].m->w().tick_count() == end);
    CHECK(shared_state(*net.nodes[0].m) == shared_state(*net.nodes[1].m));
}

TEST_CASE("golden: lockstep: a diverged machine is caught by the sync check (FUN_0045aab0) (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    Net net(gd, 2, 1);
    net.loss_percent = 0;
    while (net.nodes[0].m->w().tick_count() < 100) net.step(100000);
    net.nodes[1].m->rng.next(); // one extra draw on machine 1 only
    for (int i = 0; i < 600; ++i) net.step(100000);
    CHECK(net.nodes[0].ls->out_of_sync_tick() > 100);
    CHECK(net.nodes[1].ls->out_of_sync_tick() > 100);
}

namespace {

// The same machines over real UDP on 127.0.0.1 through net::Transport
// (the star with its hand-over). Time is simulated (16 ms per pass); the
// sockets are real.
struct UdpNet {
    struct Node {
        std::unique_ptr<Machine> m;
        std::unique_ptr<net::Lockstep> ls;
        std::unique_ptr<net::UdpSocket> sock;
        std::unique_ptr<net::Transport> tr;
        bool down = false;
    };
    std::vector<Node> nodes;
    uint64_t pass = 0;
    UdpNet(const platform::GameData& gd, int humans, int ai, int loss) {
        std::array<std::optional<net::Address>, 8> addr{};
        for (int s = 0; s < humans; ++s) {
            Node n;
            n.m = std::make_unique<Machine>(gd, humans, ai, s);
            net::Lockstep::Config c;
            c.local = s;
            c.humans = humans;
            n.ls = std::make_unique<net::Lockstep>(n.m->w(), c);
            n.sock = std::make_unique<net::UdpSocket>();
            REQUIRE(n.sock->open(0, true));
            addr[size_t(s)] = net::Address{0x7F000001, n.sock->port()};
            nodes.push_back(std::move(n));
        }
        for (int s = 0; s < humans; ++s) {
            net::Transport::Config t;
            t.local = s;
            t.hub = 0;
            for (int p = 0; p < humans; ++p) t.players[size_t(p)] = true;
            t.addr = addr;
            nodes[size_t(s)].tr = std::make_unique<net::Transport>(*nodes[size_t(s)].sock, t);
            nodes[size_t(s)].tr->drop_percent = loss;
        }
    }
    void order(int s, sim::Order o) {
        auto& w = nodes[size_t(s)].m->w();
        for (auto& u : w.units()) u.selected = u.owner == s;
        nodes[size_t(s)].ls->sent(w.post(o));
        for (auto& u : w.units()) u.selected = false;
    }
    void kill(int s) {
        nodes[size_t(s)].down = true;
        nodes[size_t(s)].sock->close();
    }
    // A machine that freezes (its socket stays open; datagrams wait).
    void freeze(int s, bool on) { nodes[size_t(s)].down = on; }
    void step(uint32_t limit) {
        const uint64_t now_ms = pass * 1000 / 60;
        for (auto& n : nodes) {
            if (n.down) continue;
            for (auto& d : n.tr->poll(now_ms, 300)) n.ls->on_message(d.from, d.data, now_ms);
            n.ls->pass(now_ms);
            auto& w = n.m->w();
            if (pass % 3 == 0 && n.ls->may_tick() && w.tick_count() < limit) {
                n.ls->before_tick(20);
                w.tick();
                n.ls->after_tick();
                for (int p = 0; p < 8; ++p) {
                    if (!w.human_active(p)) n.tr->set_gone(p);
                }
            }
            if (pass % 60 == 59) n.ls->second(60);
            for (auto& p : n.ls->take_packets()) n.tr->send(p, now_ms);
        }
        ++pass;
    }
};

} // namespace

TEST_CASE("golden: lockstep over UDP: three machines through the hub stay identical with 15 % loss (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    UdpNet net(gd, 3, 1, 15);
    const uint32_t end = 900;
    for (int i = 0; i < 60 * 300; ++i) {
        for (int s = 0; s < 3; ++s) {
            if (net.pass % 120 == uint64_t(s * 37 + 11) && net.nodes[size_t(s)].m->w().tick_count() < end - 100) {
                auto& w = net.nodes[size_t(s)].m->w();
                net.order(s, sim::make_order(sim::order::kMove, s, int(net.pass * 7 % uint64_t(w.map().width())),
                                             int(net.pass * 13 % uint64_t(w.map().height()))));
            }
        }
        net.step(end);
        bool done = true;
        for (auto& n : net.nodes) done &= n.m->w().tick_count() == end;
        if (done) break;
    }
    for (auto& n : net.nodes) {
        CHECK(n.m->w().tick_count() == end);
        CHECK(n.ls->out_of_sync_tick() == -1);
        CHECK(!n.m->w().model_out_of_sync());
    }
    CHECK(shared_state(*net.nodes[0].m) == shared_state(*net.nodes[1].m));
    CHECK(shared_state(*net.nodes[0].m) == shared_state(*net.nodes[2].m));
}

TEST_CASE("golden: lockstep over UDP: the host dies; the hub and the host role move to side 1 and the game goes on (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    UdpNet net(gd, 3, 0, 5);
    while (net.nodes[1].m->w().tick_count() < 200) net.step(100000);
    net.kill(0);
    for (int i = 0; i < 60 * 40 && net.nodes[1].m->w().human_active(0); ++i) net.step(100000);
    for (int i = 0; i < 60 * 5; ++i) net.step(100000);
    for (int s = 1; s < 3; ++s) {
        auto& n = net.nodes[size_t(s)];
        CHECK(n.tr->hub() == 1);
        CHECK(!n.m->w().human_active(0)); // dropped: kind 1, removed
        CHECK(!n.m->w().player_active(0));
        CHECK(n.ls->host_side() == 1);
    }
    CHECK(net.nodes[1].ls->is_host());
    const uint32_t end = std::max(net.nodes[1].m->w().tick_count(), net.nodes[2].m->w().tick_count()) + 60;
    for (int i = 0; i < 60 * 30; ++i) net.step(end);
    CHECK(net.nodes[1].m->w().tick_count() == end);
    CHECK(net.nodes[2].m->w().tick_count() == end);
    CHECK(shared_state(*net.nodes[1].m) == shared_state(*net.nodes[2].m));
}

TEST_CASE("golden: lockstep over UDP: the host freezes for 5 s and comes back; nobody is dropped and it joins the new hub (skips without data)") {
    if (!have_data()) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    UdpNet net(gd, 3, 0, 5);
    while (net.nodes[1].m->w().tick_count() < 200) net.step(100000);
    net.freeze(0, true);
    for (int i = 0; i < 60 * 5; ++i) net.step(100000); // 5 s: the hub moves, the timeout does not drop yet
    CHECK(net.nodes[1].tr->is_hub());
    net.freeze(0, false);
    for (int i = 0; i < 60 * 15; ++i) net.step(100000);
    for (auto& n : net.nodes) {
        CHECK(n.m->w().human_active(0)); // still in the game
        CHECK(n.tr->hub() == 1);         // the old host stepped down
    }
    const uint32_t end = std::max({net.nodes[0].m->w().tick_count(), net.nodes[1].m->w().tick_count(),
                                   net.nodes[2].m->w().tick_count()}) + 60;
    for (int i = 0; i < 60 * 30; ++i) net.step(end);
    for (auto& n : net.nodes) CHECK(n.m->w().tick_count() == end);
    CHECK(shared_state(*net.nodes[0].m) == shared_state(*net.nodes[1].m));
    CHECK(shared_state(*net.nodes[0].m) == shared_state(*net.nodes[2].m));
}

TEST_CASE("lockstep: order records encode and decode") {
    sim::Order o = sim::make_order(sim::order::kPlace, 3, 17, -2, 99);
    o.tick = 123456;
    o.seq = 42;
    o.ids = {1, 2, 70000};
    o.bids = {5};
    sim::Order back;
    REQUIRE(net::Lockstep::decode(net::Lockstep::encode(o), back));
    CHECK(back.type == o.type);
    CHECK(back.side == 3);
    CHECK(back.tick == 123456);
    CHECK(back.seq == 42);
    CHECK(back.a == 17);
    CHECK(back.b == -2);
    CHECK(back.c == 99);
    CHECK(back.ids == o.ids);
    CHECK(back.bids == o.bids);
    auto bytes = net::Lockstep::encode(o);
    bytes.resize(bytes.size() - 2);
    CHECK(!net::Lockstep::decode(bytes, back)); // short
}

TEST_CASE("golden: build_game: a campaign mission (skips without data)") {
    if (!fs::exists(fs::path(D2K_GAMEDATA_DIR) / "MISSIONS/A1V1.MAP")) return;
    platform::GameData gd(D2K_GAMEDATA_DIR);
    sim::GameSetup s;
    s.mission = "A1V1";
    sim::Rng rng{1};
    auto g = sim::build_game(s, rng, reader(gd));
    CHECK(g->world->campaign);
    CHECK(!g->world->units().empty());
    CHECK(!g->world->buildings().empty());
}
