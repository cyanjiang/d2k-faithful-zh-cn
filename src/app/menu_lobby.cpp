// The network front end of the menu runner (docs/re/58-mp-front-end.md):
// IPXLOBBY (the game list, the LAN lobby's players and chat, our address
// box), HOSTHEADTOHEAD (the host's page) and CLIENTMULTIPLAYER (a joined
// player's page), driven by net::LobbyClient / net::LobbyHost on the
// session's NetGame. The original's DirectPlay lobby is replaced by our
// lobby protocol (net/lobby.hpp); what the player sees and the rules of
// doc 58 section 5 stay.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string_view>

#include "app/menu_runner.hpp"
#include "i18n/i18n.hpp"

namespace d2k::app {

namespace {

bool is_lobby_page(const std::string& name) {
    return name == "IPXLOBBY" || name == "HOSTHEADTOHEAD" || name == "CLIENTMULTIPLAYER";
}

std::string net_colour(int c) { return "NetColour" + std::to_string(c & 7); }

constexpr uint64_t kProbeMs = 2000; // EnterLobby retries every 2 s (FUN_00479b30)

} // namespace

bool MenuRunner::open_network() {
    // CreateWigNet (FUN_00480f30): our socket on the network port (another
    // free port when a second copy runs on this PC) and the lobby client.
    if (!net_slot_) return false;
    if (!*net_slot_) {
        auto n = std::make_unique<NetGame>();
        if (!n->socket.open(uint16_t(net_port_)) && !n->socket.open(0)) {
            // The wrapper's error callback (FUN_0045d2c0) opens WIGNET_ERROR
            // at once: "There has been a problem with the network", OK ->
            // MAINMENU. Ours: no UDP socket at all (lobby_pump opens it).
            SDL_Log("network: no socket (%s)", n->socket.error().c_str());
            wignet_pending_ = true;
            return false;
        }
        SDL_Log("network: UDP port %u", unsigned(n->socket.port()));
        *net_slot_ = std::move(n);
    }
    NetGame& n = **net_slot_;
    if (!n.client) {
        n.client = std::make_unique<net::LobbyClient>(n.socket);
        for (const auto& a : net_addresses_) {
            if (auto addr = net::Address::parse(a, net::kDefaultPort)) n.client->add_address(*addr);
        }
        client_status_ = net::LobbyClient::Status::Idle;
    }
    return true;
}

void MenuRunner::network_game_over() {
    // After a network game the front end goes back to IPXLOBBY (doc 58
    // section 3.7): the game's link goes; the socket stays.
    NetGame* n = net();
    if (!n) return;
    n->lockstep.reset();
    n->transport.reset();
    n->host.reset();
    n->client.reset();
    n->started = false;
    n->start = {};
    open_network();
}

void MenuRunner::lobby_init(Menu& m, size_t index, const std::string& cmd, const std::string& a) {
    auto& es = m.states[index];
    if (cmd == "NoHighlight") {
        es.no_highlight = true;
    } else if (cmd == "EnterLobby") {
        // FUN_00479b30: into the lobby (network flag on), announced to the
        // LAN; the page lists are filled by GetLobbyList.
        if (!open_network()) return;
        NetGame& n = *net();
        n.client->back_to_lobby();
        n.client->set_in_lobby(true);
        n.client->set_me(globals_["gNetPlayerName"], uint8_t(global_value("gNetPlayerSide")),
                         uint8_t(global_value("gNetPlayerColour")));
        n.client->probe(net::kDefaultPort);
        probe_due_ = SDL_GetTicks() + kProbeMs;
        client_status_ = n.client->status();
    } else if (cmd == "GetLobbyList") {
        lobby_lists(m, int(index), a);
    }
}

void MenuRunner::lobby_page_opened(Menu& /*m*/) {
    chat_page_.clear(); // the next RefreshChatList starts after the lines there now
    lobby_view_.clear();
}

void MenuRunner::add_response_line(Menu& m, const std::string& text, const std::string& colour) {
    // FUN_00486f10 rows (box width - 20), then the list shows its end.
    // responseList is a LISTBOX_CROPPED (constructor flag +0x814, set by
    // the loader at 0x4392e5): each new row first drops the oldest rows
    // while the list holds as many as it shows (FUN_004876f0, called by
    // FUN_00486d20), so only the visible rows are kept.
    const int li = find_element(m, "responseList");
    if (li < 0) return;
    const auto rect = ui::element_rect(m.screen.elements[size_t(li)]);
    if (!rect) return;
    auto& ls = m.states[size_t(li)];
    for (auto& row : ui::wrap_list_text(assets_.menu_font(), text, rect->w - 20)) {
        const int visible = ui::list_geometry(*rect, ls, assets_.menu_font()).visible;
        while (!ls.items.empty() && int(ls.items.size()) >= std::max(visible, 1)) {
            ls.items.erase(ls.items.begin());
            if (!ls.item_white.empty()) ls.item_white.erase(ls.item_white.begin());
            if (!ls.item_data.empty()) ls.item_data.erase(ls.item_data.begin());
            if (!ls.item_extra.empty()) ls.item_extra.erase(ls.item_extra.begin());
        }
        ls.items.push_back(std::move(row));
        ls.item_white.push_back(false);
        ls.item_data.push_back(0);
        ls.item_extra.resize(ls.items.size());
        ls.item_extra.back() = {"", colour, "", ""};
    }
    const auto g = ui::list_geometry(*rect, ls, assets_.menu_font());
    ls.first = std::max(0, int(ls.items.size()) - g.visible);
    invalidate(m);
}

void MenuRunner::net_message(Menu& m, const std::string& key) {
    // DisplayNetMessage (FUN_00492880): the text's line in responseList
    // with colour 0, which FUN_00486d20 replaces by the list's own font
    // colour (element +0x780).
    add_response_line(m, assets_.lookup_text(key), "");
}

void MenuRunner::chat_list(Menu& m, int /*list*/) {
    // RefreshChatList (FUN_0047d190): new chat lines, "<name> : <text>" in
    // the sender's NetColour (message 0x15).
    NetGame* n = net();
    if (!n || !n->client) return;
    const auto& lines = m.name == "IPXLOBBY" ? n->client->lobby_chat()
                        : n->host          ? n->host->state().chat
                                           : n->client->state().chat;
    if (chat_page_ != m.name) {
        // A page opened or came back to the top: only lines from now on
        // (the lines are kept by each page's list).
        chat_page_ = m.name;
        chat_shown_.clear();
        for (const auto& c : lines) chat_shown_.push_back({c.id, c.line});
        return;
    }
    for (const auto& c : lines) {
        const std::pair<uint32_t, uint32_t> key{c.id, c.line};
        if (std::find(chat_shown_.begin(), chat_shown_.end(), key) != chat_shown_.end()) continue;
        chat_shown_.push_back(key);
        add_response_line(m, c.name + " : " + c.text, net_colour(c.colour));
    }
}

void MenuRunner::lobby_lists(Menu& m, int list, const std::string& game_list) {
    // GetLobbyList / RefreshLobbyList (FUN_0047a6f0 / FUN_0047b4f0, drawn by
    // FUN_004885f0; docs/re/58 sections 2.3 and 3.1).
    NetGame* n = net();
    if (!n || !n->client || list < 0) return;
    auto clear = [](ui::ElementState& s) {
        s.items.clear();
        s.item_white.clear();
        s.item_data.clear();
        s.item_extra.clear();
    };
    auto add = [](ui::ElementState& s, const std::string& text, ui::ElementState::RowExtra extra, int data) {
        s.items.push_back(text);
        s.item_white.push_back(false);
        s.item_data.push_back(data);
        s.item_extra.push_back(std::move(extra));
    };
    auto house = [&](int h) {
        static const char* const kHouses[] = {"Atreides", "Harkonnen", "Ordos"};
        return " (" + assets_.lookup_text(kHouses[std::clamp(h, 0, 2)]) + ")";
    };
    auto& pl = m.states[size_t(list)];
    const int old_sel = pl.selection;
    clear(pl);
    if (m.name == "IPXLOBBY") {
        int sel = 1;
        if (const int gi = find_element(m, game_list); gi >= 0) {
            // gameList: row 1 "Dune2000 Lobby"; then each game in its host's
            // colour with icon_d2 and " members/6" (gGameType 2).
            auto& gl = m.states[size_t(gi)];
            const int keep = gl.selection;
            clear(gl);
            shown_games_.clear();
            add(gl, assets_.lookup_text("Dune2000Lobby"), {}, 0);
            for (const auto& g : n->client->games()) {
                if (g.started || g.members.empty()) continue; // a started game left the lobby
                shown_games_.push_back({g.host, g.members});
                const std::string count = " " + std::to_string(g.members.size()) + "/6";
                // game_name is a network-synchronised field, so never change
                // what goes over the wire: translate only the displayed label.
                constexpr std::string_view kSuffix = "'s Game";
                std::string room = g.game_name;
                if (room.size() >= kSuffix.size() &&
                    room.compare(room.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
                    room.resize(room.size() - kSuffix.size());
                    room += i18n::localize("'s Game");
                }
                add(gl, room, {"icon_d2", net_colour(g.members[0].colour), count, ""}, int(shown_games_.size()));
            }
            // The selection stays, only lowered to the row count (0x47a873);
            // a new list starts with none (+0x7e8 = 0, FUN_00486440
            // 0x486580), which lists the lobby's players like row 1.
            gl.selection = std::clamp(keep, 0, int(gl.items.size()));
            if (const auto r = ui::element_rect(m.screen.elements[size_t(gi)])) {
                const auto g = ui::list_geometry(*r, gl, assets_.menu_font());
                gl.first = std::clamp(gl.first, 0, std::max(0, int(gl.items.size()) - g.visible));
            }
            sel = gl.selection;
        }
        // playerList: the members of the selected game with " (house)", or
        // the players on IPXLOBBY on this LAN, each with icon_ui in their
        // colour. The exe lists group 0 in DirectPlay's enumeration order
        // (FUN_004a07a0), which our transport does not have: me first,
        // then the others as heard.
        if (sel >= 2 && size_t(sel - 2) < shown_games_.size()) {
            for (const auto& mb : shown_games_[size_t(sel - 2)].members) {
                add(pl, mb.name, {"icon_ui", net_colour(mb.colour), house(mb.house), ""}, 0);
            }
        } else {
            const std::string me = globals_["gNetPlayerName"];
            if (!me.empty()) add(pl, me, {"icon_ui", net_colour(int(global_value("gNetPlayerColour"))), "", ""}, 0);
            for (const auto& mb : n->client->lobby_players()) {
                add(pl, mb.name, {"icon_ui", net_colour(mb.colour), "", ""}, 0);
            }
        }
    } else {
        // The game pages: hand<h+1>, the name in the player's colour, the
        // house, then icon_ho for the host or icon_ac for an Accept.
        const net::LobbyState& s = n->host ? n->host->state() : n->client->state();
        for (size_t i = 0; i < s.players.size(); ++i) {
            const auto& p = s.players[i];
            const std::string mark = i == 0 ? "icon_ho" : p.ready ? "icon_ac" : "";
            add(pl, p.name, {"hand" + std::to_string(int(p.handicap) + 1), net_colour(p.colour), house(p.house), mark},
                int(i));
        }
    }
    pl.selection = std::clamp(old_sel, 0, int(pl.items.size()));
    invalidate(m);
}

void MenuRunner::lobby_refresh(Menu& m) {
    // FUN_00474b60(1): every element's refresh commands.
    for (size_t i = 0; i < m.screen.elements.size(); ++i) {
        const auto& e = m.screen.elements[i];
        auto& es = m.states[i];
        for (const auto& line : element_actions(e)) {
            std::istringstream ss(line);
            std::string cmd, a;
            ss >> cmd >> a;
            if (cmd == "RefreshLobbyList") {
                lobby_lists(m, int(i), a);
            } else if (cmd == "RefreshChatList") {
                chat_list(m, int(i));
            } else if (cmd.rfind("RefreshValue", 0) == 0 && cmd != "RefreshValueCH") {
                // FUN_0047d210..FUN_0047d780: the control takes the global's
                // value, clamped like GetValue*Init.
                const int v = int(global_value(a));
                if (e.type == ui::ElementType::ValueBar) {
                    const int nv = std::clamp(v, es.vmin, es.vmax);
                    if (nv != es.value) es.drag_thumb_x = -1; // the thumb follows a changed value
                    es.value = nv;
                    bar_dirty(m, int(i));
                } else if (e.type == ui::ElementType::CheckBox) {
                    es.checked = v != 0;
                } else if (e.type == ui::ElementType::MultiStateBox) {
                    es.value = v;
                }
                m.dirty[i] = 1;
            }
        }
    }
    invalidate(m);
}

void MenuRunner::set_button(Menu& m, const std::string& name, bool enabled, const std::string& enabled_action,
                            const std::string& disabled_action) {
    // The Start / Accept looks (FUN_00492400 / FUN_00492110, FUN_004925a0 /
    // FUN_00492310): gfx, font colours and the command change at run time.
    const int bi = find_element(m, name);
    if (bi < 0) return;
    auto& body = m.screen.elements[size_t(bi)].body;
    if (body.size() < 11) return;
    std::vector<std::string> want(body.begin(), body.begin() + 11);
    want[7] = enabled ? "JRubberText" : "JRubberTextDn";
    want[8] = "JRubberTextDn";
    want[9] = enabled ? "rbut4_up" : "rb4_dis";
    want[10] = enabled ? "rbut4_dn" : "rb4_dis";
    const std::string& action = enabled ? enabled_action : disabled_action;
    if (!action.empty()) want.push_back(action);
    if (want == body) return;
    body = std::move(want);
    m.dirty[size_t(bi)] = 1;
}

void MenuRunner::host_settings_changed() {
    // The host's setters (value bars, crates, worms, the map) and
    // SendUIMsgToAll <setting>: every client follows, every Accept is
    // cleared (ResetAccept).
    NetGame* n = net();
    if (!n || !n->host) return;
    net::LobbySettings s = n->host->state().settings;
    s.map = globals_["gNetMap"];
    s.credits = int(global_value("gNetStartingCredits"));
    s.ai_players = int(global_value("gNetAIPlayers"));
    s.worms = int(global_value("gNetWorms"));
    s.unit_count = int(global_value("gNetUnitCount"));
    s.tech = int(global_value("gNetTechLevel"));
    s.crates = global_value("gNetCrates") != 0;
    s.speed = game_speed_;
    n->host->set_settings(s);
}

void MenuRunner::client_settings_to_globals(Menu& m) {
    // The host's setting messages (0xE..0x13) and the map (6): the globals,
    // the crates / worms labels and chosenMap.
    NetGame* n = net();
    if (!n || !n->client) return;
    const net::LobbyState& s = n->client->state();
    if (s.revision == client_rev_) return;
    client_rev_ = s.revision;
    const auto& g = s.settings;
    globals_["gNetUnitCount"] = std::to_string(g.unit_count);
    globals_["gNetTechLevel"] = std::to_string(g.tech);
    globals_["gNetStartingCredits"] = std::to_string(g.credits);
    globals_["gNetAIPlayers"] = std::to_string(g.ai_players);
    globals_["gNetCrates"] = g.crates ? "1" : "0";
    globals_["gNetWorms"] = std::to_string(g.worms);
    // Message 6 (FUN_0048e820 case 6): a map whose file is not here
    // (FUN_00491b40) is cleared and reported to the group as an empty map.
    const bool have_map = g.map.empty() || gd_.find("Data/maps/" + g.map + ".map");
    if (!have_map) n->client->no_map(g.map);
    globals_["gNetMap"] = have_map ? g.map : std::string();
    // FUN_00493050: the map's text, or NoMapSelected.
    const auto desc = !have_map || g.map.empty() ? std::nullopt : assets_.text().get("map_" + g.map);
    globals_["gNetMapDesc"] = desc ? *desc : assets_.lookup_text("NoMapSelected");
    if (const int me = n->client->my_index(); me >= 0) {
        globals_["gNetPlayerColour"] = std::to_string(s.players[size_t(me)].colour);
        globals_["gNetPlayerHandicap"] = std::to_string(s.players[size_t(me)].handicap);
    }
    if (m.name != "CLIENTMULTIPLAYER") return;
    set_box_text(m, "chosenMap", globals_["gNetMapDesc"]);
    set_text_key(m, "crates", g.crates ? "CratesOn" : "CratesOff");
    static const char* const kWorms[] = {"WormsOff", "WormsLow", "WormsMedium", "WormsHigh"};
    set_text_key(m, "worms", kWorms[std::clamp(g.worms, 0, 3)]);
}

void MenuRunner::join_game(Menu& m) {
    // Join / double click (ClientWantsToJoin, then JoinSession; doc 58
    // section 2.3): a game row; the host refuses a full game, a used name
    // or another version.
    NetGame* n = net();
    if (!n || !n->client || n->client->status() != net::LobbyClient::Status::Idle) return;
    const std::string name = globals_["gNetPlayerName"];
    const int gi = find_element(m, "gameList");
    const int sel = gi >= 0 ? m.states[size_t(gi)].selection : 0;
    if (sel < 2 || size_t(sel - 2) >= shown_games_.size()) return; // ClientWantsToJoin needs a game row
    if (name.empty()) {
        net_message(m, "NoNameEntered");
        return;
    }
    const auto& g = shown_games_[size_t(sel - 2)];
    if (g.members.size() >= size_t(net::kMaxPlayers)) {
        net_message(m, "GameIsFull");
        return;
    }
    n->client->set_me(name, uint8_t(global_value("gNetPlayerSide")), uint8_t(global_value("gNetPlayerColour")));
    n->client->join(g.host, SDL_GetTicks());
    client_status_ = n->client->status();
}

void MenuRunner::probe_address(Menu& m, int self) {
    // Ours (docs/re/58 section 7.2): Enter in the address box asks that
    // address for its game; it is remembered for the next visits.
    NetGame* n = net();
    if (!n || !n->client || self < 0) return;
    std::string text = m.states[size_t(self)].edit_text;
    while (!text.empty() && text.back() == ' ') text.pop_back();
    const auto addr = net::Address::parse(text, net::kDefaultPort);
    if (!addr) return;
    n->client->add_address(*addr);
    n->client->probe(net::kDefaultPort);
    probe_due_ = SDL_GetTicks() + kProbeMs;
    net_addresses_.erase(std::remove(net_addresses_.begin(), net_addresses_.end(), text), net_addresses_.end());
    net_addresses_.insert(net_addresses_.begin(), text);
    if (net_addresses_.size() > 8) net_addresses_.resize(8);
}

void MenuRunner::alter_handicap(Menu& m, int list, int row) {
    // AlterHandicap (FUN_00491c60): a click on the hand icon (x <= list x +
    // 20) of a row on the host's page cycles that player's handicap 0 -> 1
    // -> 2 -> 0; every Accept is cleared.
    NetGame* n = net();
    if (!n || !n->host || m.name != "HOSTHEADTOHEAD") return;
    const auto& e = m.screen.elements[size_t(list)];
    const auto actions = element_actions(e);
    if (std::none_of(actions.begin(), actions.end(), [](const std::string& l) { return l.rfind("AlterHandicap", 0) == 0; })) {
        return;
    }
    const auto rect = ui::element_rect(e);
    if (!rect || int(std::floor(mouse_x_)) > rect->x + 20) return;
    n->host->cycle_handicap(size_t(row));
}

bool MenuRunner::no_change_options(Menu& m, int i) {
    // FUN_00482850 (0x4828cd..0x48291a): on a network game page other than
    // IPXLOBBY, a client's click on a checkbox or multistate box below
    // y 100 (element +0x0c, the rect's top; not x) only shows "Only the
    // host can change the options" (docs/re/55 section 1.4). A value bar's press
    // (FUN_0048c640) has the same rule without the y test (docs/re/55 2.4).
    NetGame* n = net();
    if (!n || n->host || !is_lobby_page(m.name) || m.name == "IPXLOBBY") return false;
    if (!n->client || n->client->status() != net::LobbyClient::Status::InLobby) return false;
    const auto rect = ui::element_rect(m.screen.elements[size_t(i)]);
    const bool bar = m.screen.elements[size_t(i)].type == ui::ElementType::ValueBar;
    if (!rect || (!bar && rect->y <= 100)) return false;
    net_message(m, "NoChangeOptions");
    return true;
}

bool MenuRunner::lobby_command(const std::string& cmd, const std::string& a, const std::string& b, Menu* m, int self) {
    NetGame* n = net();
    if (cmd == "CreateWigNet") {
        open_network();
    } else if (cmd == "DestroyWigNet" || cmd == "DisableThis") {
        // DestroyWigNet does nothing in 1.06 (FUN_00481030(0)); DisableThis
        // only guards a double click on a page that is being left.
    } else if (cmd == "JoinSessionDbl" || cmd == "AlterHandicap" || cmd.rfind("Refresh", 0) == 0) {
        // Double-click, row-click and refresh commands: not click commands.
    } else if (cmd == "SetEditFocus") {
        // 0x0B: the focus moves to the named edit box.
        if (!m || self < 0) return true;
        const int ti = find_element(*m, a);
        if (ti < 0) return true;
        m->states[size_t(self)].edit_focus = false;
        m->states[size_t(ti)].edit_focus = true;
        m->states[size_t(ti)].edit_cursor = int(m->states[size_t(ti)].edit_text.size());
        invalidate(*m);
    } else if (cmd == "SetFromUIStringSpecial" && m && self >= 0 &&
               m->screen.elements[size_t(self)].type == ui::ElementType::ListBox) {
        // A list gives its selected row's text (playerList: gNetRejectPlayerName).
        const auto& ls = m->states[size_t(self)];
        globals_[a] = ls.selection > 0 && ls.selection <= int(ls.items.size()) ? ls.items[size_t(ls.selection - 1)]
                                                                              : std::string();
    } else if (cmd == "GetMapFromList" && m && m->name == "HOSTHEADTOHEAD") {
        if (self >= 0) get_map_from_list(*m, self);
        host_settings_changed(); // message 6 to the group, ResetAccept
    } else if (cmd == "DisplayNetMessage") {
        if (m) net_message(*m, a);
    } else if (cmd == "SendMsgToAll") {
        // FUN_0047bd00: the chat line to my group (on IPXLOBBY: the LAN lobby).
        if (!m || self < 0 || !n || !n->client) return true;
        auto& es = m->states[size_t(self)];
        if (es.edit_text.empty()) return true;
        if (m->name == "IPXLOBBY") n->client->lobby_say(es.edit_text);
        else if (n->host) n->host->chat(es.edit_text);
        else n->client->chat(es.edit_text);
        es.edit_text.clear();
        es.edit_cursor = 0;
        edit_changed(*m, self);
        lobby_view_.clear();
    } else if (cmd == "SendUIMsgToAll") {
        if (!m || !n || !n->client) return true;
        if (a == "ClientWantsToJoin") {
            join_game(*m);
        } else if (a == "ClientWantsToQuit") {
            // Message 0x19: refused once the host pressed Start.
            if (n->client->status() == net::LobbyClient::Status::Started) return true;
            n->client->leave();
            n->client->set_in_lobby(true);
            client_status_ = n->client->status();
            nav_ = NavCommand{"BackMenu", "", ""};
        } else if (a == "AcceptSettings") {
            n->client->accept();
        } else if (a == "Reject") {
            // Message 4: the selected row's player, never myself.
            if (!n->host) return true;
            const std::string who = globals_["gNetRejectPlayerName"];
            const auto& players = n->host->state().players;
            for (size_t i = 1; i < players.size(); ++i) {
                if (players[i].name == who) {
                    n->host->reject(players[i].id);
                    break;
                }
            }
        } else if (a == "UnitCount" || a == "TechLevel" || a == "StartingCredits" || a == "AIPlayers" ||
                   a == "Crates" || a == "Worms") {
            host_settings_changed();
        }
    } else if (cmd == "StartSession") {
        // New (FUN_0047ac10): a name is needed; I host a new game.
        if (!m || !n || !n->client) return true;
        const std::string name = globals_["gNetPlayerName"];
        if (name.empty()) {
            net_message(*m, "NoNameEntered");
            return true;
        }
        net::LobbyPlayer me;
        me.name = name;
        me.house = uint8_t(std::clamp<long>(global_value("gNetPlayerSide"), 0, 2));
        me.colour = uint8_t(global_value("gNetPlayerColour") & 7);
        me.handicap = uint8_t(std::clamp<long>(global_value("gNetPlayerHandicap"), 0, 2));
        n->host = std::make_unique<net::LobbyHost>(n->socket, me);
        n->client->set_in_lobby(false);
        host_settings_changed();
        nav_ = NavCommand{"JumpToMenu", "HOSTHEADTOHEAD", ""};
    } else if (cmd == "BreakFromSession") {
        // The host's Cancel: every client back to IPXLOBBY without a message.
        if (n && n->host) {
            n->host->close();
            closing_hosts_.push_back(std::move(n->host));
        }
        if (n && n->client) n->client->set_in_lobby(true);
    } else if (cmd == "BreakFromLobby") {
        // FUN_00479d40: out of the lobby, the network closed.
        if (n) {
            for (auto& h : closing_hosts_) {
                for (uint64_t t = SDL_GetTicks(); !h->finished(); t += 1000) h->tick(t);
            }
            closing_hosts_.clear();
            net_slot_->reset();
        }
    } else if (cmd == "StartGame" && global_value("gGameType") == 2) {
        // FUN_00479e80 on the host's page: silently nothing without two
        // players and a map; the start goes to every client with the seed
        // (the host's clock in ms, doc 57 section 1.9).
        if (!n || !n->host || !n->host->can_start()) return true;
        n->start = n->host->start(uint32_t(SDL_GetTicks()));
        n->started = true;
        globals_["gGameState"] = "2";
        globals_["gMissionMap"] = globals_["gNetMap"];
        globals_["gSideId"] = "0";
        nav_ = NavCommand{"QuitMenu", "", ""};
    } else if (cmd == "AbortGame" || cmd == "LeftGame" || cmd == "SendInGameMsgTo") {
        // The in-game pages (FUN_00492eb0 / FUN_00492e20 / FUN_0047bae0): the game does them.
        if (ingame_net_) ingame_net_(cmd);
    } else if (cmd == "WigNetError") {
        // FUN_00493190: gGameState 2 -> 1, QuitAndJumpToMenu MAINMENU. In
        // a game: the loop ends and the front end opens MAINMENU (main.cpp).
        globals_["gGameState"] = "1";
        if (ingame_) {
            ingame_event_ = IngameEvent::Abort;
            wignet_exit_ = true;
        } else {
            nav_ = NavCommand{"QuitAndJumpToMenu", "MAINMENU", ""};
        }
    } else if (cmd == "ProbeAddress") {
        if (m) probe_address(*m, self);
    } else {
        return false;
    }
    (void)b;
    return true;
}

void MenuRunner::lobby_pump(uint64_t now) {
    // FUN_0048e660, every menu pass: the lobby's messages, then what they
    // change on the page (a refresh, a page change).
    if (wignet_pending_ && !ingame_) { // FUN_00470e60: close all, then the error page
        wignet_pending_ = false;
        nav_ = NavCommand{"QuitAndJumpToMenu", "WIGNET_ERROR", ""};
        return;
    }
    NetGame* n = net();
    if (!n || n->started) return; // a started game's socket belongs to the game
    while (auto d = n->socket.receive()) {
        if (n->host) n->host->handle(*d, now);
        if (n->client) n->client->handle(*d, now);
    }
    if (n->host) n->host->tick(now);
    for (auto& h : closing_hosts_) h->tick(now);
    std::erase_if(closing_hosts_, [](const auto& h) { return h->finished(); });
    if (!n->client) return;
    auto& c = *n->client;
    c.tick(now);
    if (stack_.empty() || ingame_ || transition_.pending()) return;
    Menu& top = *stack_.back();
    const auto status = c.status();
    using St = net::LobbyClient::Status;
    if (top.name == "IPXLOBBY") {
        const std::string name = globals_["gNetPlayerName"];
        c.set_me(name, uint8_t(global_value("gNetPlayerSide")), uint8_t(global_value("gNetPlayerColour")));
        if (status == St::Idle && now >= probe_due_) {
            c.probe(net::kDefaultPort);
            probe_due_ = now + kProbeMs;
        }
        if (status == St::InLobby && client_status_ != St::InLobby && !nav_) {
            // The host said yes (0x1C): JoinSession, then CLIENTMULTIPLAYER
            // with the host's settings (gNetAIPlayers = 0 first).
            globals_["gNetAIPlayers"] = "0";
            client_rev_ = 0;
            client_settings_to_globals(top);
            nav_ = NavCommand{"JumpToMenu", "CLIENTMULTIPLAYER", ""};
        } else if (status == St::Rejected) {
            const std::string why = c.reject_reason();
            if (why == "FAILED_MP_CONNECT") net_message(top, "ConnectError"); // ours: no answer at all
            else if (!why.empty()) net_message(top, why);
            c.back_to_lobby();
            c.set_in_lobby(true);
        }
    } else if (top.name == "CLIENTMULTIPLAYER") {
        if (status == St::Rejected || status == St::Closed) {
            // Reject (4) or the host's Cancel (3): back to IPXLOBBY, no message.
            c.back_to_lobby();
            c.set_in_lobby(true);
            if (!nav_) nav_ = NavCommand{"BackMenu", "", ""};
        } else if (status == St::Started && !nav_) {
            // Message 5: the game starts (FUN_004752c0).
            n->start = c.game();
            n->started = true;
            globals_["gGameState"] = "2";
            globals_["gMissionMap"] = n->start.state.settings.map;
            globals_["gSideId"] = std::to_string(n->start.local);
            nav_ = NavCommand{"QuitMenu", "", ""};
        } else {
            client_settings_to_globals(top);
            const int me = c.my_index();
            const bool confirmed = me >= 0 && c.state().players[size_t(me)].ready;
            set_button(top, "accept", !confirmed, "SendUIMsgToAll AcceptSettings", "");
        }
    } else if (top.name == "HOSTHEADTOHEAD" && n->host) {
        // The AI bar's maximum is 6 - humans (FUN_004885f0 0x48a67a); the
        // host's lobby lowers gNetAIPlayers on a join.
        const auto& s = n->host->state();
        if (const int ai = find_element(top, "aiBar"); ai >= 0) {
            auto& bar = top.states[size_t(ai)];
            const int max_ai = std::max(0, net::kMaxPlayers - int(s.players.size()));
            if (bar.vmax != max_ai) {
                bar.vmax = max_ai;
                bar_dirty(top, ai);
            }
        }
        if (global_value("gNetAIPlayers") != s.settings.ai_players) {
            globals_["gNetAIPlayers"] = std::to_string(s.settings.ai_players);
            if (const int ai = find_element(top, "aiBar"); ai >= 0) {
                top.states[size_t(ai)].value = s.settings.ai_players;
                top.states[size_t(ai)].drag_thumb_x = -1;
                bar_dirty(top, ai);
            }
        }
        set_button(top, "start", n->host->can_start(), "StartGame mapList", "DisplayNetMessage CannotStartGame");
    }
    client_status_ = c.status();
    if (!is_lobby_page(top.name)) return;
    // What the lists show; a change refreshes the page (the exe refreshes
    // on every lobby message and DirectPlay player change, section 4.1).
    std::ostringstream v;
    if (top.name == "IPXLOBBY") {
        for (const auto& g : c.games()) {
            v << g.host.text() << g.game_name << g.started;
            for (const auto& mb : g.members) v << '|' << mb.name << int(mb.house) << int(mb.colour);
        }
        v << '#';
        for (const auto& mb : c.lobby_players()) v << mb.name << int(mb.house) << int(mb.colour) << '|';
        v << '#' << c.lobby_chat().size();
        if (!c.lobby_chat().empty()) v << c.lobby_chat().back().id << ':' << c.lobby_chat().back().line;
        v << '#' << globals_["gNetPlayerName"] << global_value("gNetPlayerColour");
    } else {
        const net::LobbyState& s = n->host ? n->host->state() : c.state();
        v << s.revision << '#' << s.players.size();
    }
    if (v.str() != lobby_view_) {
        lobby_view_ = v.str();
        lobby_refresh(top);
    }
}

} // namespace d2k::app
