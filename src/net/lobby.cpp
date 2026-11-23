#include "net/lobby.hpp"

#include <algorithm>
#include <random>

#include "build_fingerprint.hpp"
#include "net/wire.hpp"

#ifndef D2K_VERSION
#define D2K_VERSION "dev"
#endif

namespace d2k::net {

namespace {

constexpr uint8_t kMagic[4] = {'D', '2', 'K', 'L'};
constexpr uint64_t kRepeatMs = 500;     // state snapshots, requests, start repeats
constexpr uint64_t kSilentMs = 10000;   // a client this long silent leaves the game page
constexpr uint64_t kAnnounceMs = 1000;  // LAN presence on IPXLOBBY
constexpr uint64_t kPresenceMs = 3500;  // a LAN lobby player unheard this long is gone
constexpr uint64_t kGameMs = 6000;      // a game row unheard this long is gone
constexpr size_t kChatKept = 8;
constexpr size_t kChatLength = 64;      // the chat edit box limit
constexpr size_t kNameLength = 15;

enum class Msg : uint8_t {
    Probe = 1, Info, Join, Reject, State, Update, Chat, Leave, Start, StartAck, Closed, Presence, LanChat, NoMap
};
// Reject reasons (the IPXLOBBY lines of doc 58 section 2.3).
enum : uint8_t { kSilent = 0, kFull = 1, kNameUsed = 2, kVersion = 3 };

Writer begin(Msg m) {
    Writer w;
    w.bytes(kMagic).u16(kProtocol).u8(uint32_t(m));
    return w;
}

bool open_msg(std::span<const uint8_t> d, Reader& r, Msg& m) {
    if (d.size() < 7 || !std::equal(kMagic, kMagic + 4, d.begin())) return false;
    r = Reader{d, 4};
    if (r.u16() != kProtocol) return false;
    m = Msg(r.u8());
    return r.ok;
}

void put_settings(Writer& w, const LobbySettings& g) {
    w.str(g.map).u32(uint32_t(g.credits)).u8(uint32_t(g.ai_players)).u8(uint32_t(g.worms));
    w.u8(uint32_t(g.unit_count)).u8(uint32_t(g.tech)).u8(g.crates ? 1 : 0).u8(uint32_t(g.speed));
}

void get_settings(Reader& r, LobbySettings& g) {
    g.map = r.str();
    g.credits = int(r.u32());
    g.ai_players = int(r.u8());
    g.worms = int(r.u8());
    g.unit_count = int(r.u8());
    g.tech = int(r.u8());
    g.crates = r.u8() != 0;
    g.speed = int(r.u8());
}

void put_state(Writer& w, const LobbyState& s) {
    w.u32(s.revision).u32(s.settings_revision).str(s.game_name);
    put_settings(w, s.settings);
    w.u8(uint32_t(s.players.size()));
    for (const auto& p : s.players) {
        w.u32(p.id).str(p.name).u8(p.house).u8(p.colour).u8(p.handicap).u8(p.ready ? 1 : 0);
        w.u32(p.addr.ip).u16(p.addr.port);
    }
    w.u8(uint32_t(s.chat.size()));
    for (const auto& c : s.chat) w.u32(c.id).u32(c.line).u8(c.colour).str(c.name).str(c.text);
}

bool get_state(Reader& r, LobbyState& s) {
    s.revision = r.u32();
    s.settings_revision = r.u32();
    s.game_name = r.str();
    get_settings(r, s.settings);
    const uint32_t n = r.u8();
    if (n > 8) return false;
    s.players.assign(n, {});
    for (auto& p : s.players) {
        p.id = r.u32();
        p.name = r.str();
        p.house = uint8_t(r.u8());
        p.colour = uint8_t(r.u8());
        p.handicap = uint8_t(r.u8());
        p.ready = r.u8() != 0;
        p.addr.ip = r.u32();
        p.addr.port = uint16_t(r.u16());
    }
    const uint32_t k = r.u8();
    if (k > 16) return false;
    s.chat.assign(k, {});
    for (auto& c : s.chat) {
        c.id = r.u32();
        c.line = r.u32();
        c.colour = uint8_t(r.u8());
        c.name = r.str();
        c.text = r.str();
    }
    return r.ok;
}

std::string clip(std::string s, size_t n) {
    if (s.size() > n) s.resize(n);
    return s;
}

uint32_t new_id() {
    std::random_device rd; // the lobby's own nonces; not game state
    uint32_t v = rd();
    return v ? v : 1;
}

void broadcast(UdpSocket& sock, const std::vector<uint8_t>& v, uint16_t port) {
    for (uint32_t b : UdpSocket::broadcast_addresses()) sock.send(Address{b, port}, v);
    sock.send(Address{0x7F000001, port}, v); // a lobby on this PC
}

} // namespace

// The program version, the protocol and the build fingerprint (a hash of the
// sources and the compiler): builds that could play differently refuse each
// other with WrongNetworkVersion instead of going out of sync in the game.
std::string game_version() {
    return std::string("d2k-faithful ") + D2K_VERSION + " p" + std::to_string(kProtocol) + " " + D2K_BUILD_FINGERPRINT;
}

sim::GameSetup GameStart::setup() const {
    sim::GameSetup s;
    s.type = sim::GameSetup::Type::Network;
    s.mission = state.settings.map;
    s.local = local;
    s.humans = int(state.players.size());
    s.ai_players = state.settings.ai_players;
    s.credits = state.settings.credits;
    s.tech = state.settings.tech;
    s.unit_count = state.settings.unit_count;
    s.worms = state.settings.worms;
    s.crates = state.settings.crates;
    for (size_t i = 0; i < state.players.size() && i < 8; ++i) {
        s.house[i] = state.players[i].house;
        s.colour[i] = state.players[i].colour;
        s.handicap[i] = state.players[i].handicap;
        s.names[i] = state.players[i].name;
    }
    s.latency = 40; // gGameType 2, LAN / IP (FUN_0045a7a0)
    return s;
}

Transport::Config GameStart::transport() const {
    Transport::Config c;
    c.local = local;
    c.hub = 0;
    for (size_t i = 0; i < state.players.size() && i < 8; ++i) c.players[i] = true;
    c.addr = addr;
    return c;
}

// ---- host -------------------------------------------------------------------

LobbyHost::LobbyHost(UdpSocket& socket, LobbyPlayer me) : sock_(socket), session_(new_id()) {
    me.id = 0;
    me.ready = true;
    me.name = clip(me.name, kNameLength);
    state_.game_name = me.name + "'s Game"; // `'s Game` (0x4e84d4)
    state_.players.push_back(me);
    state_.revision = state_.settings_revision = 1;
}

LobbyPlayer* LobbyHost::find(uint32_t id) {
    for (size_t i = 1; i < state_.players.size(); ++i) {
        if (state_.players[i].id == id) return &state_.players[i];
    }
    return nullptr;
}

void LobbyHost::reset_accept() {
    // FUN_00492110: every mark cleared; the Accepts that come in are for the
    // new settings revision.
    ++state_.settings_revision;
    for (size_t i = 1; i < state_.players.size(); ++i) state_.players[i].ready = false;
    accept_count_ = 0;      // 0x797e24
    start_enabled_ = false; // Start back to rb4_dis with CannotStartGame
    changed();
}

void LobbyHost::limit_ai() {
    // The AI bar's maximum is 6 - humans; gNetAIPlayers is lowered to it
    // (FUN_004885f0 0x48a67a, message 0x11).
    const int max_ai = std::max(0, kMaxPlayers - int(state_.players.size()));
    if (state_.settings.ai_players > max_ai) state_.settings.ai_players = max_ai;
}

void LobbyHost::set_settings(const LobbySettings& s) {
    if (started_ || s == state_.settings) return;
    state_.settings = s;
    limit_ai();
    reset_accept();
}

void LobbyHost::cycle_handicap(size_t index) {
    if (started_ || index >= state_.players.size()) return;
    auto& h = state_.players[index].handicap;
    h = uint8_t((h + 1) % 3);
    reset_accept();
}

void LobbyHost::reject(uint32_t id) {
    // Reject (message 4): the named client goes back without a message.
    if (started_ || id == 0) return;
    auto& v = state_.players;
    auto it = std::find_if(v.begin() + 1, v.end(), [&](const LobbyPlayer& p) { return p.id == id; });
    if (it == v.end()) return;
    Writer w = begin(Msg::Reject);
    w.u32(id).u8(kSilent);
    sock_.send(it->addr, w.v);
    v.erase(it);
    seen_.erase(id);
    reset_accept();
}

void LobbyHost::chat(const std::string& text) {
    state_.chat.push_back({0, ++chat_line_, state_.players[0].colour, state_.players[0].name, clip(text, kChatLength)});
    if (state_.chat.size() > kChatKept) state_.chat.erase(state_.chat.begin());
    changed();
}

bool LobbyHost::can_start() const {
    if (started_ || state_.players.size() < 2 || state_.settings.map.empty()) return false;
    if (int(state_.players.size()) + state_.settings.ai_players > kMaxPlayers) return false;
    return start_enabled_;
}

GameStart LobbyHost::start(uint32_t seed) {
    GameStart g;
    g.seed = seed;
    g.local = 0;
    g.state = state_;
    for (size_t i = 1; i < state_.players.size() && i < 8; ++i) g.addr[i] = state_.players[i].addr;
    started_ = g;
    confirmed_.clear();
    send_due_ = 0;
    return g;
}

bool LobbyHost::all_confirmed() const {
    if (!started_) return false;
    for (size_t i = 1; i < started_->state.players.size(); ++i) {
        if (std::find(confirmed_.begin(), confirmed_.end(), started_->state.players[i].id) == confirmed_.end()) return false;
    }
    return true;
}

void LobbyHost::close() {
    // Host Cancel: every client back to IPXLOBBY at once, without a message.
    for (size_t i = 1; i < state_.players.size(); ++i) closed_to_.push_back(state_.players[i].addr);
    state_.players.resize(1);
    close_repeats_ = 3;
    send_due_ = 0;
    closed_ = true;
}

void LobbyHost::send_state() {
    Writer w = begin(started_ ? Msg::Start : Msg::State);
    if (started_) w.u32(started_->seed);
    put_state(w, started_ ? started_->state : state_);
    for (size_t i = 1; i < state_.players.size(); ++i) {
        const auto& p = state_.players[i];
        if (started_ && std::find(confirmed_.begin(), confirmed_.end(), p.id) != confirmed_.end()) continue;
        sock_.send(p.addr, w.v);
    }
}

void LobbyHost::poll(uint64_t now_ms) {
    while (auto d = sock_.receive()) handle(*d, now_ms);
    tick(now_ms);
}

void LobbyHost::handle(const Datagram& d, uint64_t now_ms) {
    Reader r;
    Msg m;
    if (closed_ || !open_msg(d.data, r, m)) return;
    switch (m) {
    case Msg::Probe: {
        Writer w = begin(Msg::Info);
        w.str(state_.game_name).str(state_.settings.map).str(game_version());
        w.u8(uint32_t(state_.players.size()));
        for (const auto& p : state_.players) w.str(p.name).u8(p.house).u8(p.colour);
        w.u8(uint32_t(state_.settings.ai_players)).u8(started_ ? 1 : 0).u32(session_);
        sock_.send(d.from, w.v);
        break;
    }
    case Msg::Join: {
        // The original's order (doc 58 section 2.3): the version check of
        // ClientWantsToJoin, no answer while starting, then JoinSession's
        // GameIsFull and NameIsUsed, then the colour fix.
        const uint32_t id = r.u32();
        const std::string name = clip(r.str(), kNameLength);
        const uint8_t house = uint8_t(std::min<uint32_t>(r.u8(), 2));
        uint8_t colour = uint8_t(r.u8() & 7);
        const std::string version = r.str();
        if (!r.ok || id == 0) break;
        auto refuse = [&](uint8_t why) {
            Writer w = begin(Msg::Reject);
            w.u32(id).u8(why);
            sock_.send(d.from, w.v);
        };
        if (LobbyPlayer* p = find(id)) { // a repeat
            p->addr = d.from;
            seen_[id] = now_ms;
            break;
        }
        if (version != game_version()) {
            refuse(kVersion);
            break;
        }
        if (started_) break; // nothing while starting
        if (state_.players.size() >= size_t(kMaxPlayers)) {
            refuse(kFull);
            break;
        }
        if (std::any_of(state_.players.begin(), state_.players.end(), [&](const LobbyPlayer& p) { return p.name == name; })) {
            refuse(kNameUsed);
            break;
        }
        // A colour already used in this game becomes the lowest free 0..7
        // (FUN_00491a10; 4 and 7 included).
        auto used = [&](uint8_t c) {
            return std::any_of(state_.players.begin(), state_.players.end(), [&](const LobbyPlayer& p) { return p.colour == c; });
        };
        if (used(colour)) {
            for (uint8_t c = 0; c < 8; ++c) {
                if (!used(c)) {
                    colour = c;
                    break;
                }
            }
        }
        LobbyPlayer p;
        p.id = id;
        p.name = name;
        p.house = house;
        p.colour = colour;
        p.addr = d.from;
        state_.players.push_back(p);
        seen_[id] = now_ms;
        limit_ai();
        reset_accept();
        break;
    }
    case Msg::Update: {
        // The client's keep-alive with the settings revision it accepted.
        // A revision not seen from this client before is one AcceptSettings
        // (message 0x16, FUN_0048e820 case 0x16): the counter 0x797e24 goes
        // up first, whether the settings match or not (0x48f5c3). An Accept
        // of the current settings is confirmed (0x18 {1}, the row's mark)
        // and enables Start when the counter is exactly the members - 1
        // (0x48f665, FUN_00492400). An older one is the mismatch (0x18 {0}):
        // the client sees the new settings and may accept again. So, as in
        // the exe, a mismatched Accept counts too: with three players, A's
        // mismatched and then matching Accept enable Start before B has
        // accepted, and a count past members - 1 never enables it until the
        // next ResetAccept.
        const uint32_t id = r.u32(), accepted = r.u32();
        LobbyPlayer* p = find(id);
        if (!r.ok || !p) break;
        p->addr = d.from;
        seen_[id] = now_ms;
        if (started_ || accepted == 0) break;
        uint32_t& last = accepts_[id];
        if (accepted == last) break;
        last = accepted;
        accept_count_ = uint8_t(accept_count_ + 1);
        if (accepted == state_.settings_revision) {
            if (accept_count_ == state_.players.size() - 1) start_enabled_ = true;
            p->ready = true;
        }
        changed();
        break;
    }
    case Msg::Chat: {
        const uint32_t id = r.u32(), line = r.u32();
        const std::string text = clip(r.str(), kChatLength);
        const LobbyPlayer* p = find(id);
        if (!r.ok || !p) break;
        const bool have = std::any_of(state_.chat.begin(), state_.chat.end(),
                                      [&](const LobbyChat& c) { return c.id == id && c.line == line; });
        if (!have) {
            state_.chat.push_back({id, line, p->colour, p->name, text});
            if (state_.chat.size() > kChatKept) state_.chat.erase(state_.chat.begin());
            changed();
        }
        break;
    }
    case Msg::Leave: {
        // ClientWantsToQuit (0x19): yes unless starting; Accepts cleared.
        const uint32_t id = r.u32();
        if (!r.ok || started_ || !find(id)) break;
        auto& v = state_.players;
        v.erase(std::remove_if(v.begin() + 1, v.end(), [&](const LobbyPlayer& p) { return p.id == id; }), v.end());
        seen_.erase(id);
        reset_accept();
        break;
    }
    case Msg::NoMap: {
        // Message 6 with an empty map from a client without the map file
        // (FUN_0048e820 case 6, 0x48ee..): every receiver, the host too,
        // takes the empty map (FUN_00493050: "No map selected"). The host's
        // Accept marks and Start stay as they are (FUN_004925a0 acts only
        // on the client pages); StartGame refuses without a map (doc 58
        // section 3.4) until the host picks another one.
        const uint32_t id = r.u32();
        const std::string map = r.str();
        if (!r.ok || started_ || !find(id) || map.empty() || map != state_.settings.map) break;
        state_.settings.map.clear();
        changed();
        break;
    }
    case Msg::StartAck: {
        const uint32_t id = r.u32();
        if (r.ok && std::find(confirmed_.begin(), confirmed_.end(), id) == confirmed_.end()) confirmed_.push_back(id);
        break;
    }
    default:
        break;
    }
}

void LobbyHost::tick(uint64_t now_ms) {
    // Clients silent for 10 s leave the game page (not once started).
    if (!started_ && !closed_) {
        for (size_t i = 1; i < state_.players.size();) {
            const auto it = seen_.find(state_.players[i].id);
            if (it != seen_.end() && now_ms - it->second > kSilentMs) {
                seen_.erase(it);
                state_.players.erase(state_.players.begin() + std::ptrdiff_t(i));
                reset_accept();
            } else {
                ++i;
            }
        }
    }
    if (now_ms >= send_due_) {
        if (close_repeats_ > 0) {
            const Writer w = begin(Msg::Closed);
            for (const auto& a : closed_to_) sock_.send(a, w.v);
            if (--close_repeats_ == 0) closed_to_.clear();
        }
        if (!closed_) send_state();
        send_due_ = now_ms + kRepeatMs;
    }
}

// ---- client -----------------------------------------------------------------

LobbyClient::LobbyClient(UdpSocket& socket) : sock_(socket), id_(new_id()) {}

void LobbyClient::set_me(const std::string& name, uint8_t house, uint8_t colour) {
    const std::string n = clip(name, kNameLength);
    const uint8_t h = uint8_t(std::min<int>(house, 2)), c = uint8_t(colour & 7);
    if (n == name_ && h == house_ && c == colour_) return;
    name_ = n;
    house_ = h;
    colour_ = c;
    announce_due_ = 0; // a change is announced at once
}

void LobbyClient::probe(uint16_t port) {
    const Writer w = begin(Msg::Probe);
    broadcast(sock_, w.v, port);
    for (const auto& a : addresses_) sock_.send(a, w.v);
    for (const auto& a : lan_peers_) sock_.send(a, w.v);
}

void LobbyClient::heard_peer(const Address& a) {
    if (std::find(lan_peers_.begin(), lan_peers_.end(), a) != lan_peers_.end()) return;
    if (lan_peers_.size() >= 16) lan_peers_.erase(lan_peers_.begin());
    lan_peers_.push_back(a);
}

void LobbyClient::lobby_say(const std::string& text) {
    LobbyChat c{id_, ++lan_line_, colour_, name_, clip(text, kChatLength)};
    lan_chat_.push_back(c); // shown here at once (SendMsgToAll)
    if (lan_chat_.size() > kChatKept) lan_chat_.erase(lan_chat_.begin());
    lan_out_.push_back({c, 3}); // sent three times: there is no ack on the LAN lobby
    announce_due_ = 0;
}

void LobbyClient::join(const Address& host, uint64_t now_ms) {
    host_ = host;
    accept_rev_ = 0;
    state_ = {};
    status_ = Status::Joining;
    join_t0_ = now_ms;
    send_due_ = 0;
    reject_.clear();
}

void LobbyClient::accept() {
    accept_rev_ = state_.settings_revision;
    send_due_ = 0;
}

void LobbyClient::chat(const std::string& text) {
    chat_out_.push_back({id_, ++chat_line_, colour_, name_, clip(text, kChatLength)});
    send_due_ = 0;
}

void LobbyClient::no_map(const std::string& map) {
    if (status_ != Status::InLobby || map.empty() || map == no_map_sent_) return;
    no_map_sent_ = map;
    Writer w = begin(Msg::NoMap);
    w.u32(id_).str(map);
    for (int i = 0; i < 3; ++i) sock_.send(host_, w.v);
}

void LobbyClient::leave() {
    if (status_ == Status::InLobby || status_ == Status::Joining) {
        Writer w = begin(Msg::Leave);
        w.u32(id_);
        for (int i = 0; i < 3; ++i) sock_.send(host_, w.v);
    }
    status_ = Status::Idle;
}

int LobbyClient::my_index() const {
    for (size_t i = 0; i < state_.players.size(); ++i) {
        if (state_.players[i].id == id_) return int(i);
    }
    return -1;
}

bool LobbyClient::my_accept_pending() const {
    const int me = my_index();
    return me > 0 && accept_rev_ == state_.settings_revision && !state_.players[size_t(me)].ready;
}

void LobbyClient::send_update() {
    if (status_ == Status::Joining) {
        Writer w = begin(Msg::Join);
        w.u32(id_).str(name_).u8(house_).u8(colour_).str(game_version());
        sock_.send(host_, w.v);
        return;
    }
    Writer w = begin(Msg::Update);
    w.u32(id_).u32(accept_rev_);
    sock_.send(host_, w.v);
    for (const auto& c : chat_out_) {
        Writer cw = begin(Msg::Chat);
        cw.u32(id_).u32(c.line).str(c.text);
        sock_.send(host_, cw.v);
    }
}

void LobbyClient::poll(uint64_t now_ms) {
    while (auto d = sock_.receive()) handle(*d, now_ms);
    tick(now_ms);
}

void LobbyClient::handle(const Datagram& d, uint64_t now_ms) {
    Reader r;
    Msg m;
    if (!open_msg(d.data, r, m)) return;
    switch (m) {
    case Msg::Info: {
        GameInfo g;
        g.host = d.from;
        g.game_name = r.str();
        g.map = r.str();
        g.version = r.str();
        const uint32_t n = r.u8();
        if (n > 8) break;
        for (uint32_t i = 0; i < n; ++i) {
            LobbyMember mb;
            mb.name = r.str();
            mb.house = uint8_t(r.u8());
            mb.colour = uint8_t(r.u8());
            g.members.push_back(mb);
        }
        g.ai_players = int(r.u8());
        g.started = r.u8() != 0;
        g.session = r.u32();
        g.seen_ms = now_ms;
        if (!r.ok) break;
        // A host answers a broadcast once per network it is on, so the same
        // game can arrive from several addresses: one row, the first address.
        auto it = std::find_if(games_.begin(), games_.end(),
                               [&](const GameInfo& x) { return x.host == g.host || x.session == g.session; });
        if (it != games_.end()) {
            if (it->session == g.session) g.host = it->host;
            *it = g;
        } else {
            games_.push_back(g);
        }
        break;
    }
    case Msg::Presence: {
        const uint32_t id = r.u32();
        LobbyMember mb;
        mb.name = r.str();
        mb.house = uint8_t(r.u8());
        mb.colour = uint8_t(r.u8());
        if (!r.ok || id == id_) break;
        heard_peer(d.from);
        // Keyed by name (the list shows names); the newest wins.
        auto it = std::find_if(lan_players_.begin(), lan_players_.end(), [&](const LobbyMember& x) { return x.name == mb.name; });
        if (it != lan_players_.end()) {
            *it = mb;
            lan_seen_[size_t(it - lan_players_.begin())] = now_ms;
        } else {
            lan_players_.push_back(mb);
            lan_seen_.push_back(now_ms);
        }
        break;
    }
    case Msg::LanChat: {
        LobbyChat c;
        c.id = r.u32();
        c.line = r.u32();
        c.colour = uint8_t(r.u8());
        c.name = r.str();
        c.text = r.str();
        if (!r.ok || c.id == id_ || status_ != Status::Idle) break;
        heard_peer(d.from);
        const bool have = std::any_of(lan_chat_.begin(), lan_chat_.end(),
                                      [&](const LobbyChat& x) { return x.id == c.id && x.line == c.line; });
        if (!have) {
            lan_chat_.push_back(c);
            if (lan_chat_.size() > kChatKept) lan_chat_.erase(lan_chat_.begin());
        }
        break;
    }
    case Msg::Reject: {
        const uint32_t id = r.u32();
        const uint32_t why = r.u8();
        if (!r.ok || id != id_ || (status_ != Status::Joining && status_ != Status::InLobby)) break;
        status_ = Status::Rejected;
        reject_ = why == kVersion ? "WrongNetworkVersion" : why == kFull ? "GameIsFull" : why == kNameUsed ? "NameIsUsed" : "";
        break;
    }
    case Msg::Closed: {
        if (d.from == host_ && (status_ == Status::Joining || status_ == Status::InLobby)) status_ = Status::Closed;
        break;
    }
    case Msg::State: {
        if (d.from != host_ || (status_ != Status::Joining && status_ != Status::InLobby)) break;
        LobbyState s;
        if (!get_state(r, s)) break;
        state_ = s;
        if (my_index() < 0) {
            if (status_ == Status::InLobby) { // removed by the host: Reject
                status_ = Status::Rejected;
                reject_.clear();
            }
            break;
        }
        status_ = Status::InLobby;
        // Chat lines the host now shows are no longer repeated.
        chat_out_.erase(std::remove_if(chat_out_.begin(), chat_out_.end(),
                                       [&](const LobbyChat& c) {
                                           return std::any_of(state_.chat.begin(), state_.chat.end(), [&](const LobbyChat& x) {
                                               return x.id == id_ && x.line == c.line;
                                           });
                                       }),
                        chat_out_.end());
        break;
    }
    case Msg::Start: {
        if (d.from != host_) break;
        const uint32_t seed = r.u32();
        LobbyState s;
        if (!get_state(r, s)) break;
        if (status_ != Status::Started) {
            state_ = s;
            const int me = my_index();
            if (me < 0) break; // not in this game
            start_ = {};
            start_.seed = seed;
            start_.local = me;
            start_.state = s;
            start_.addr[0] = host_; // the host as this machine reaches it
            for (size_t i = 1; i < s.players.size() && i < 8; ++i) start_.addr[i] = s.players[i].addr;
            status_ = Status::Started;
        }
        Writer w = begin(Msg::StartAck);
        w.u32(id_);
        sock_.send(host_, w.v);
        break;
    }
    default:
        break;
    }
}

void LobbyClient::tick(uint64_t now_ms) {
    // Forget LAN lobby players and games no longer heard.
    for (size_t i = 0; i < lan_players_.size();) {
        if (now_ms - lan_seen_[i] > kPresenceMs) {
            lan_players_.erase(lan_players_.begin() + std::ptrdiff_t(i));
            lan_seen_.erase(lan_seen_.begin() + std::ptrdiff_t(i));
        } else {
            ++i;
        }
    }
    games_.erase(std::remove_if(games_.begin(), games_.end(), [&](const GameInfo& g) { return now_ms - g.seen_ms > kGameMs; }),
                 games_.end());
    // IPXLOBBY: my presence and lobby chat on the LAN.
    if (status_ == Status::Idle && in_lobby_ && !name_.empty() && now_ms >= announce_due_) {
        Writer w = begin(Msg::Presence);
        w.u32(id_).str(name_).u8(house_).u8(colour_);
        broadcast(sock_, w.v, lan_port_);
        for (const auto& a : lan_peers_) sock_.send(a, w.v);
        for (auto& [c, left] : lan_out_) {
            Writer cw = begin(Msg::LanChat);
            cw.u32(c.id).u32(c.line).u8(c.colour).str(c.name).str(c.text);
            broadcast(sock_, cw.v, lan_port_);
            for (const auto& a : lan_peers_) sock_.send(a, cw.v);
            --left;
        }
        lan_out_.erase(std::remove_if(lan_out_.begin(), lan_out_.end(), [](const auto& e) { return e.second <= 0; }),
                       lan_out_.end());
        announce_due_ = now_ms + (lan_out_.empty() ? kAnnounceMs : 200);
    }
    if (status_ == Status::Joining && now_ms - join_t0_ > 10000) {
        status_ = Status::Rejected;
        reject_ = "FAILED_MP_CONNECT"; // no answer at all
    }
    if ((status_ == Status::Joining || status_ == Status::InLobby) && now_ms >= send_due_) {
        send_update();
        send_due_ = now_ms + kRepeatMs;
    }
}

} // namespace d2k::net
