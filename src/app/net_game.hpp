#pragma once
// A network game in progress (docs/design/multiplayer.md): the lobby's
// socket, the lobby (host or client: late start repeats still arrive),
// the start (seed, players, settings), and once the game is built the
// transport and the lockstep. Held by the GameSession, so it outlives the
// front-end pages and the game screen.

#include <memory>

#include "net/lobby.hpp"
#include "net/lockstep.hpp"
#include "net/transport.hpp"
#include "net/udp.hpp"

namespace d2k::app {

struct NetGame {
    net::UdpSocket socket;
    std::unique_ptr<net::LobbyHost> host;
    std::unique_ptr<net::LobbyClient> client;
    net::GameStart start;
    bool started = false;                       // start received / sent: the next game is this one
    std::unique_ptr<net::Transport> transport;  // made with the game
    std::unique_ptr<net::Lockstep> lockstep;
};

} // namespace d2k::app
