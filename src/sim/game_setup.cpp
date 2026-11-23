#include "sim/game_setup.hpp"

#include <algorithm>
#include <stdexcept>

#include "formats/pe_image.hpp"
#include "sim/objects.hpp"

namespace d2k::sim {

namespace {

bool same(const std::string& a, const char* b) {
    if (a.size() != std::char_traits<char>::length(b)) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// rand() & 7, never 4 or 7 (the lobby colours 0x44a54e / rand lines 0x15e3).
uint8_t roll_colour(Rng& rng) {
    uint32_t c;
    do c = rng.next() & 7; while (c == 4 || c == 7);
    return uint8_t(c);
}

} // namespace

std::unique_ptr<Game> build_game(const GameSetup& s, Rng& rng, const FileReader& read_file) {
    auto g = std::make_unique<Game>();
    // Every file read goes into data_hash (FNV-1a over path and contents).
    auto read = [&](const std::string& path) {
        auto data = read_file(path);
        auto mix = [&](uint8_t b) { g->data_hash = (g->data_hash ^ b) * 0x100000001b3ull; };
        for (char c : path) mix(uint8_t(c));
        mix(data ? 1 : 0);
        if (data)
            for (uint8_t b : *data) mix(b);
        return data;
    };
    auto need = [&](const std::string& path) {
        auto data = read(path);
        if (!data) throw std::runtime_error("missing game file: " + path);
        return std::move(*data);
    };
    using Type = GameSetup::Type;
    const bool multi = s.type != Type::Campaign; // skirmish or network: Data\maps and _PRAC.MIS
    const bool network = s.type == Type::Network;
    const int humans = network ? std::clamp(s.humans, 1, 6) : 1;

    // 3. The sides' colours (FUN_00448550, before the map is read).
    //    Skirmish (0x44a54e): side 0 is the player's colour; sides 1..5
    //    rolled, never a colour already taken by an earlier side.
    //    Network (docs/re/57 section 3.4): the humans keep their colours;
    //    every other side of 0..5 is rolled, never an earlier side's colour
    //    and never a human's.
    std::array<uint8_t, 8> colours{0, 1, 2, 3, 4, 5, 6, 7};
    if (s.type == Type::Skirmish) {
        colours[0] = uint8_t(s.colour[0] & 7);
        for (int i = 1; i <= 5; ++i) {
            uint8_t c = roll_colour(rng);
            auto taken = [&](uint8_t v) {
                for (int j = 0; j < i; ++j) {
                    if (colours[size_t(j)] == v) return true;
                }
                return false;
            };
            while (taken(c)) c = roll_colour(rng);
            colours[size_t(i)] = c;
        }
        // Sides 6 and 7: FUN_00461c00 reads lobby records 6 and 7, which
        // FUN_004481b0 (called by FUN_004470a0 at every set-up) zeroes and no
        // skirmish step fills: colour 0 (docs/re/52 section 2.1 step 7).
        colours[6] = 0;
        colours[7] = 0;
    } else if (network) {
        for (int i = 0; i < humans; ++i) colours[size_t(i)] = uint8_t(s.colour[size_t(i)] & 7);
        for (int i = humans; i < 6; ++i) {
            auto taken = [&](uint8_t v) {
                for (int j = 0; j < i; ++j) {
                    if (colours[size_t(j)] == v) return true;
                }
                for (int h = 0; h < humans; ++h) {
                    if (colours[size_t(h)] == v) return true;
                }
                return false;
            };
            uint8_t c = roll_colour(rng);
            while (taken(c)) c = roll_colour(rng);
            colours[size_t(i)] = c;
        }
        // Sides 6 and 7: lobby records 6 and 7, zeroed by FUN_004481b0 and
        // never filled with six or fewer humans: colour 0.
        colours[6] = 0;
        colours[7] = 0;
    }

    // 4. The mission file, then the map. Skirmish and network:
    //    Data\maps\<map>.map with Data\maps\_PRAC.MIS for every map
    //    (docs/re/51 section 5, docs/re/57 section 3.4).
    const std::string name = [&] {
        std::string n = s.mission;
        for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
        return n;
    }();
    g->map = formats::parse_map(need((multi ? "Data/maps/" : "MISSIONS/") + name + ".MAP"));
    g->mis = formats::parse_mission(need(multi ? std::string("Data/maps/_PRAC.MIS") : "MISSIONS/_" + name + ".MIS"));
    auto& mis = g->mis;
    const int ai_players = std::clamp(s.ai_players, 0, 7);
    if (multi) {
        // FUN_0044f8e0's overrides (docs/re/52 section 2.2): houses set
        // before the read, tech = gNetTechLevel for all. Credits: skirmish
        // gNetStartingCredits for side 0 and 7000 / 10500 / 14000 by
        // difficulty for the others; network gNetStartingCredits for every
        // side (0x44fff4).
        for (int i = 0; i < 8; ++i) mis.house[size_t(i)] = uint8_t(i);
        if (network) {
            // Humans: their lobby house. The other sides keep the house
            // array: index i while i + 1 < humans (that branch is for
            // inactive human slots), else values above 2 become 0. The
            // array holds i from the start (0x8cd4f0 is filled with i by
            // every campaign 0x448748 and skirmish 0x44a54e set-up). Before
            // any such game it is 0 (.bss, past the .data file image), but
            // the AI sides then get rand() % 3 (FUN_00469790 0x469df6), so
            // the difference only reaches sides with no AI and no objects,
            // whose house nothing reads: no effect.
            for (int i = 0; i < 8; ++i) {
                if (i < humans) mis.house[size_t(i)] = uint8_t(std::clamp(int(s.house[size_t(i)]), 0, 2));
                else if (i + 1 >= humans && mis.house[size_t(i)] > 2) mis.house[size_t(i)] = 0;
            }
        } else {
            mis.house[0] = uint8_t(std::clamp(int(s.house[0]), 0, 2));
        }
        const uint32_t ai_cash = s.difficulty == 1 ? 10500 : s.difficulty == 2 ? 14000 : 7000;
        for (int i = 0; i < 8; ++i) {
            mis.tech_level[size_t(i)] = uint8_t(s.tech);
            mis.credits[size_t(i)] = i == 0 || network ? uint32_t(s.credits) : ai_cash;
        }
        // The tileset by map name (__strcmpi with gNetMap).
        if (same(name, "2Play11")) {
            mis.tileset = "BLOXTREE";
            mis.tile_attributes = "TILEATR1";
        } else if (same(name, "2Play12")) {
            mis.tileset = "BLOXBASE";
            mis.tile_attributes = "TILEATR2";
        } else if (same(name, "6Play3")) {
            mis.tileset = "BLOXWAST";
            mis.tile_attributes = "TILEATR4";
        } else if (same(name, "2Play13") || same(name, "4Play9") || same(name, "4Play10") || same(name, "6Play4") ||
                   same(name, "6Play5") || same(name, "2Play14") || same(name, "4Play8")) {
            mis.tileset = "BLOXXMAS";
            mis.tile_attributes = "TILEATR7";
        }
        // FUN_00469790: no AI is active; blocks 1..7 = block 0 (the _PRAC.MIS
        // template) with their side byte; the AI sides (after the humans)
        // are switched on (0x469d6e).
        const auto tmpl = mis.ai[0];
        for (size_t i = 0; i < 8; ++i) {
            if (i > 0) mis.ai[i] = tmpl;
            if (mis.ai[i].size() > 1) {
                mis.ai[i][0] = uint8_t(i);
                mis.ai[i][1] = uint8_t(int(i) >= humans && int(i) < humans + ai_players ? 1 : 0);
            }
        }
    }

    g->attributes = formats::TileAttributes::parse(need("Data/bin/" + mis.tile_attributes + ".BIN"));
    g->tables = ExeTables::load(formats::PeImage(need("DUNE2000.DAT")));
    g->game_map = std::make_unique<GameMap>(g->map, g->attributes, g->tables, rng, s.thick);
    auto td = formats::TileData::parse(need("Data/bin/TILEDATA.BIN"));
    g->templates = formats::Templates::parse(need("Data/bin/Templates.bin"));
    auto placed = place_map_objects(g->map, td, g->templates, mis.house, g->tables);
    g->data_images = formats::parse_r8(need("Data/DATA.R16"));
    auto speed = parse_speed_table(need("Data/bin/SPEED.BIN"));
    const auto circles = need("Data/bin/CIRCLES.BIN");
    const auto vars = need("Data/bin/VARS.BIN");
    // Skirmish and network: map buildings and units are ignored; only its
    // worms are created, by the start (docs/re/52 section 2.3).
    g->world = std::make_unique<World>(*g->game_map, g->templates, g->tables, speed,
                                       multi ? std::vector<Object>{} : placed, rng, circles, vars, s.local);
    World& w = *g->world;
    w.side_colour = colours; // FUN_00461c00
    w.campaign = !multi;
    if (network) {
        w.handicap = s.handicap;
        w.net_names = s.names;
        w.start_network(humans, ai_players, s.latency);
    }
    if (multi) {
        w.net_worms = std::clamp(s.worms, 0, 3);
        // 0x548048 (0x44a814): VARS NumberOfCrates (+0x41) when crates are
        // on, times the humans when CratesPerPlayer (+0x42) is set and there
        // are two or more.
        if (s.crates && vars.size() > 0x42) {
            w.max_crates = vars[0x42] != 0 && humans >= 2 ? vars[0x41] * humans : vars[0x41];
        }
    }
    w.set_armour(need("Data/bin/ARMOUR.BIN"));
    w.set_smoke_points(need("Data/bin/BUILEXP.BIN"));
    const auto& img = g->data_images;
    for (const auto& e : g->templates.art.building) {
        const size_t i = size_t(e.base);
        // The smoke points' frame height: element image 0 (0x6e5c40[e * 32]),
        // or the base image when that is empty (docs/re/43 section 3.3).
        const size_t f0 = size_t(e.base + 1);
        const bool has_f0 = e.frames > 0 && f0 < img.size() && img[f0].frame_width != 0;
        const size_t hi = has_f0 ? f0 : i;
        w.element_frame0_h.push_back(hi < img.size() ? int(img[hi].frame_height) : 0);
        w.building_frame.emplace_back(uint8_t(i < img.size() ? img[i].frame_width : 0),
                                      uint8_t(i < img.size() ? img[i].frame_height : 0));
        std::array<uint8_t, 32> fw{};
        for (int k = 0; k < std::min(e.frames, 32); ++k) {
            const size_t j = size_t(e.base + 1 + k); // as the draw: element image k
            fw[size_t(k)] = uint8_t(j < img.size() ? img[j].frame_width : 0);
        }
        w.element_frame_w.push_back(fw);
        // HomeView: offset_y of element image 0 when it is not an empty R16
        // entry (null in the exe), else of the base image (docs/re/25 A.8).
        const bool has_i0 = e.frames > 0 && f0 < img.size() && img[f0].width != 0;
        const size_t vi = has_i0 ? f0 : i;
        w.element_view_dy.push_back(vi < img.size() ? int(img[vi].offset_y) : 0);
    }
    w.local_player = s.local;
    // gDifficultyLevel (0x4e8bf0): 0 easy, 1 normal, 2 hard (doc 22 section 4).
    // Network: the lobby has no difficulty, so the exe reads each machine's
    // leftover single-player value (the delivery skip FUN_0049e3e0 and the
    // AI build rate FUN_0041b250 would then differ between machines). We
    // use normal on every machine (docs/design/multiplayer-local-audit.md
    // B4 / B5).
    w.difficulty = network ? 1 : std::clamp(s.difficulty, 0, 2);
    w.init_players(mis);
    if (multi) {
        // FUN_00469790 (docs/re/52 sections 2.4 to 2.6): AI houses, starts,
        // MCVs, TECHPOS units and worms; then ActivateAI for the AI sides.
        SkirmishSetup k;
        k.humans = humans;
        k.ai_players = ai_players;
        k.unit_count = std::clamp(s.unit_count, 0, 10);
        k.tech = std::clamp(s.tech, 0, 9);
        k.worms = std::clamp(s.worms, 0, 3);
        if (auto f = read("Data/bin/TECHPOS.BIN")) k.techpos = std::move(*f);
        k.starts = skirmish_starts(g->map);
        k.map_units = placed;
        w.skirmish_start(k);
        w.set_ai(mis);     // ActivateAI per active slot (doc 22 section 2.1)
        w.set_script(mis); // no events or conditions in _PRAC.MIS; FUN_0044f770 at set-up
    } else {
        w.set_script(mis); // the campaign script (doc 19)
        w.set_ai(mis);     // ActivateAI per active slot (doc 22 section 2.1)
    }
    w.set_reveal_counter(s.reveal_counter); // 0x54804c carries over (doc 16 section 3.4)
    w.reveal_start(); // FUN_0049f4d0 (doc 16 section 2)
    return g;
}

} // namespace d2k::sim
