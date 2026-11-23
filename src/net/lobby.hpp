#pragma once
// The lobby of a network game (docs/re/58-mp-front-end.md; our transport,
// docs/design/multiplayer.md section 6.4). The original's model, with our
// messages instead of DirectPlay names:
// - IPXLOBBY: name, house and colour are chosen there; the game list
//   (LAN probes, and addresses typed in, decided 2026-10-15); the "Dune2000
//   Lobby" row lists the other players on IPXLOBBY on this LAN and lobby
//   chat reaches them (a LAN presence announcement, decided 2026-10-15).
// - HOSTHEADTOHEAD (host) / CLIENTMULTIPLAYER (client): only the host
//   changes the settings and every handicap; clients only Accept, chat or
//   leave. Any change clears every Accept; Start needs every client's
//   Accept of the current settings (doc 58 section 5).
// The host owns the game's state and sends it as a whole snapshot, twice a
// second and on every change; client requests repeat until the snapshot
// shows them, so no message needs an ack.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "net/transport.hpp"
#include "net/udp.hpp"
#include "sim/game_setup.hpp"

namespace d2k::net {

constexpr uint16_t kDefaultPort = 1998;
constexpr int kMaxPlayers = 6; // humans, and humans + AI (doc 58 section 5.3)

// The version every player must share (protocol and program version).
std::string game_version();

struct LobbySettings {     // HOSTHEADTOHEAD's values (doc 58 section 3.1), the exe's defaults
    std::string map;         // gNetMap; empty = none chosen ("NoMapSelected")
    int credits = 4000, ai_players = 0, worms = 0, unit_count = 6, tech = 7;
    bool crates = false;
    int speed = 75;          // the host's game speed (start message +0x22)
    bool operator==(const LobbySettings&) const = default;
};

struct LobbyPlayer {
    uint32_t id = 0;         // the player's join nonce (0 = the host)
    std::string name;
    uint8_t house = 0, colour = 0, handicap = 1;
    bool ready = false;      // Accept confirmed (the host always counts as ready)
    Address addr;            // as the host sees it (unset for the host itself)
};

struct LobbyChat {
    uint32_t id = 0;         // the sender's id and its line number, for repeats
    uint32_t line = 0;
    uint8_t colour = 0;      // the sender's lobby colour (the lobby list draws it)
    std::string name, text;
};

struct LobbyState {
    uint32_t revision = 0;          // any change
    uint32_t settings_revision = 0; // settings, players or handicaps: Accepts are for this one
    std::string game_name;
    LobbySettings settings;
    std::vector<LobbyPlayer> players; // [0] = the host; index = side once started
    std::vector<LobbyChat> chat;      // the last few lines
};

// What every machine needs to build and run the game.
struct GameStart {
    uint32_t seed = 0;
    int local = 0;                       // this machine's side
    LobbyState state;
    std::array<std::optional<Address>, 8> addr{}; // per side
    sim::GameSetup setup() const;        // for sim::build_game
    Transport::Config transport() const; // hub = side 0
};

struct LobbyMember {                     // a name in a list row
    std::string name;
    uint8_t house = 0, colour = 0;
};

struct GameInfo {                        // one answer to a probe: a gameList row
    Address host;
    std::string game_name, map, version;
    std::vector<LobbyMember> members;    // the host first
    int ai_players = 0;
    bool started = false;
    uint32_t session = 0;                // the hosting run: one row per game, whichever of
                                         // the host's addresses answered (LAN, loopback, VPN)
    uint64_t seen_ms = 0;
};

class LobbyHost {
public:
    LobbyHost(UdpSocket& socket, LobbyPlayer me);
    const LobbyState& state() const { return state_; }
    // The host's changes (HOSTHEADTOHEAD): each clears every Accept.
    void set_settings(const LobbySettings& s);
    void cycle_handicap(size_t index); // AlterHandicap: 0 -> 1 -> 2 -> 0
    void reject(uint32_t id);          // Reject: back to IPXLOBBY, no message
    void chat(const std::string& text);
    // CannotStartGame otherwise: two humans or more, a map, and Start
    // enabled by the Accept counter (doc 58 section 5.4: a matching Accept
    // when the Accepts counted since the last ResetAccept, matching or not,
    // are the members - 1).
    bool can_start() const;
    // Start: the seed (the host's clock in ms, doc 57 section 1.9) goes out
    // with the final players; repeated until each client confirms.
    GameStart start(uint32_t seed);
    bool all_confirmed() const;
    // Cancel (BreakFromSession): every client goes back to IPXLOBBY. From
    // then on the game answers nothing (no probe shows it); keep polling
    // until finished() so the repeats go out.
    void close();
    bool finished() const { return closed_ && close_repeats_ == 0; }
    // poll = read the socket, handle each datagram, then tick. After the
    // start the transport reads the socket and hands lobby datagrams here
    // (Transport::take_foreign), so handle + tick are separate.
    void poll(uint64_t now_ms);
    void handle(const Datagram& d, uint64_t now_ms);
    void tick(uint64_t now_ms);

private:
    void changed() { ++state_.revision; send_due_ = 0; }
    void reset_accept();               // ResetAccept (FUN_00492110)
    void limit_ai();                   // humans + AI <= 6
    void send_state();
    LobbyPlayer* find(uint32_t id);
    UdpSocket& sock_;
    LobbyState state_;
    uint32_t chat_line_ = 0;
    uint64_t send_due_ = 0;
    std::optional<GameStart> started_;
    std::vector<uint32_t> confirmed_;
    std::map<uint32_t, uint64_t> seen_; // last message per client id
    std::vector<Address> closed_to_;    // clients told the game is gone
    int close_repeats_ = 0;
    bool closed_ = false;
    uint8_t accept_count_ = 0;               // 0x797e24 (a byte)
    bool start_enabled_ = false;             // Start's enabled state (FUN_00492400 / FUN_00492110)
    std::map<uint32_t, uint32_t> accepts_;   // per client: the last Accept (settings revision) counted
    uint32_t session_ = 0; // set in the constructor; tells one game's answers apart from another's
};

class LobbyClient {
public:
    // InLobby = on HOSTHEADTOHEAD's client page; Closed = the host left;
    // Rejected = refused (reject_reason) or rejected by the host (empty
    // reason: back to IPXLOBBY without a message).
    enum class Status { Idle, Joining, InLobby, Rejected, Closed, Started };
    explicit LobbyClient(UdpSocket& socket);

    // ---- IPXLOBBY -----------------------------------------------------
    // Who I am (name, house, colour): announced on the LAN every second
    // while on IPXLOBBY and Idle.
    void set_me(const std::string& name, uint8_t house, uint8_t colour);
    // Asks for games on every LAN broadcast address and this PC on `port`,
    // and at each remembered address.
    void probe(uint16_t port);
    void add_address(const Address& a) { addresses_.push_back(a); } // typed in IPXLOBBY's address box
    void set_lan_port(uint16_t port) { lan_port_ = port; }           // where LAN lobby players listen (1998)
    // On IPXLOBBY (group 0): the presence and lobby chat go out only then; a
    // player hosting a game is not in the lobby's player list.
    void set_in_lobby(bool on) { in_lobby_ = on; announce_due_ = 0; }
    const std::vector<GameInfo>& games() const { return games_; }
    const std::vector<LobbyMember>& lobby_players() const { return lan_players_; } // the "Dune2000 Lobby" row
    const std::vector<LobbyChat>& lobby_chat() const { return lan_chat_; }
    void lobby_say(const std::string& text); // chat on IPXLOBBY: to every LAN lobby player
    // ClientWantsToJoin / JoinSession with the name, house and colour of set_me.
    void join(const Address& host, uint64_t now_ms);

    // ---- CLIENTMULTIPLAYER -------------------------------------------
    void accept();                     // AcceptSettings for the settings now shown
    void chat(const std::string& text);
    void leave();                      // ClientWantsToQuit (refused once started)
    // The host's map is not on this PC: message 6 with an empty map to the
    // group (FUN_0048e820 case 6), once per map.
    void no_map(const std::string& map);
    void poll(uint64_t now_ms);
    void handle(const Datagram& d, uint64_t now_ms);
    void tick(uint64_t now_ms);
    Status status() const { return status_; }
    void back_to_lobby() { status_ = Status::Idle; } // after Rejected / Closed was shown
    std::string reject_reason() const { return reject_; } // a text.uib key, or empty
    const LobbyState& state() const { return state_; }
    int my_index() const;              // in state().players, -1 = not in it
    bool my_accept_pending() const;    // Accept pressed for the current settings, not yet confirmed
    const GameStart& game() const { return start_; }

private:
    void send_update();
    UdpSocket& sock_;
    Status status_ = Status::Idle;
    std::vector<GameInfo> games_;
    std::vector<Address> addresses_;
    // LAN lobby players heard so far: presence, lobby chat and probes also
    // go to each of them directly. Broadcasts miss a player on another port
    // (a second copy on the same PC) and are often lost on virtual networks
    // (Windows Sandbox, Hyper-V); the direct copies are not.
    std::vector<Address> lan_peers_;
    void heard_peer(const Address& a);
    std::vector<LobbyMember> lan_players_;
    std::vector<uint64_t> lan_seen_;
    std::vector<LobbyChat> lan_chat_;
    uint32_t lan_line_ = 0;
    std::vector<std::pair<LobbyChat, int>> lan_out_; // lines still to repeat (count)
    uint64_t announce_due_ = 0;
    uint16_t lan_port_ = kDefaultPort;
    bool in_lobby_ = true;
    Address host_;
    uint32_t id_ = 0;
    std::string name_;
    uint8_t house_ = 0, colour_ = 0;
    uint32_t accept_rev_ = 0;          // the settings revision I accepted (0 = none)
    std::string no_map_sent_;          // the last map reported missing
    uint32_t chat_line_ = 0;
    std::vector<LobbyChat> chat_out_;  // repeated until the state shows them
    LobbyState state_;
    GameStart start_;
    uint64_t send_due_ = 0, join_t0_ = 0;
    std::string reject_;
};

} // namespace d2k::net
