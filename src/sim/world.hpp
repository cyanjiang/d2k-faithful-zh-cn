#pragma once
// The running simulation: units and buildings on the game map, advanced one
// tick at a time. Unit fields mirror the exe's 0x94-byte unit object
// (docs/re/09-unit-movement.md); only movement and selection so far.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "formats/templates.hpp"
#include "sim/exe_tables.hpp"
#include "sim/game_map.hpp"
#include "sim/objects.hpp"
#include "sim/order.hpp"
#include "sim/player.hpp"
#include "sim/rng.hpp"

namespace d2k::sim {

// bin\SPEED.BIN: the first 32 float32 values, [terrain class][speed class].
using SpeedTable = std::array<float, 32>;
SpeedTable parse_speed_table(std::span<const uint8_t> data);

namespace state {
constexpr int kIdle = 2;
constexpr int kMove = 7;
constexpr int kMoveAttack = 8; // move, then attack what is in the destination
} // namespace state

struct Unit {
    int index = 0;            // our unique id (never reused; references use it)
    uint16_t slot = 0;        // +0x8c: the slot in its side's pool (docs/re/50); staggers the 8-tick and delay phases
    int owner = 0;            // side
    int house = 0;
    int type = 0;
    int record = 0;           // +0x18
    int32_t speed = 0;        // +0x00: speed of the current step (16.16)
    int32_t hit_points = 0;   // +0x04
    uint32_t flags = 0;       // +0x08
    int state = state::kIdle; // +0x10
    int next_state = 1;       // +0x14 (1 = none)
    bool selected = false;    // +0x19
    uint8_t turn_counter = 0; // +0x1a
    uint8_t turret_counter = 0; // +0x1b
    uint8_t frame = 0;        // +0x1e
    int anim_mode = 0;        // +0x20 (infantry)
    int32_t x = 0, y = 0;     // +0x34 / +0x38, 16.16 pixels
    int32_t step_x = 0, step_y = 0; // +0x40 / +0x44
    uint8_t dest_x = 0, dest_y = 0; // +0x48 / +0x49
    uint8_t cur_x = 0, cur_y = 0;   // +0x4a / +0x4b
    uint8_t to_x = 0, to_y = 0;     // +0x4c / +0x4d
    uint8_t half = 0;         // +0x4e
    uint8_t steps = 0;        // +0x4f
    uint8_t slot_next = 0, slot_cur = 0; // +0x52 / +0x53 (infantry)
    uint8_t facing = 0;       // +0x54
    uint8_t turret_facing = 0; // +0x55
    uint8_t turret_goal = 0;   // +0x56: the goal of the last turret turn (FUN_00493f40); the idle wander's goal
    std::array<uint8_t, 12> route_x{}, route_y{}; // +0x5c / +0x68 (next = [count-1])
    uint8_t route_count = 0;  // +0x74
    uint8_t blocked = 0;      // +0x75
    uint16_t path_length = 0; // +0x76
    uint16_t repath_delay = 0; // +0x78
    uint8_t waiting = 0;      // +0x7a
    // Harvesters (docs/re/13-harvesting.md). `frame` (+0x1e) is the dock frame.
    uint8_t timer = 0;        // +0x25
    uint8_t load = 0;         // +0x29: 0..7
    uint8_t home_x = 0, home_y = 0; // +0x50 / +0x51
    int refinery = -1;        // +0x58: the refinery's slot in this unit's side's pool, never cleared (docs/re/50 section 6)
    // Aircraft (docs/re/14-aircraft-and-effects.md).
    int32_t altitude = 0;     // +0x3c (16.16)
    uint8_t counter_2b = 0xff; // +0x2b: +1 every 8th tick; the carryall drop counter
    int delivery = -1;        // +0x2c: delivery entry index (delivery carryall)
    uint8_t countdown = 0;    // +0x2f: death countdown (state 0x11)
    bool remove = false;      // removed at the end of the tick
    // Combat (docs/re/18-combat.md).
    int target_kind = 0;      // 0 none, 1 unit (id), 2 building (index)
    int target = -1;          // +0x0c / +0x26
    int attacker = -1, attacker_side = -1; // +0x32 / +0x30
    uint16_t reload = 0;      // +0x1c
    bool prone = false;       // +0x24 (infantry)
    uint8_t hit_age = 0xff;   // +0x2a: 0 when hit, +1 every 8 ticks (FUN_00497b90)
    uint8_t since_stop = 0;   // +0x2c: +1 every 8 ticks while on its cell, else 0
    uint8_t ai_group = 0xff;  // +0x28: computer player's group (docs/re/23)
    // +0x2e: the delivery that dropped it (flag 0x40000). CreateUnit
    // (FUN_00455870) never writes it, and every pool slot starts zeroed (the
    // side clear FUN_0046b990, and FUN_0046bdb0 zeroes +0x00..+0x7B when an
    // object is freed), so it is 0 unless a delivery (0x49e687) or the
    // Deviator (0x49dc41) sets it. DoMCVs reads it (docs/re/30 section 1.6).
    uint8_t delivery_entry = 0;
    int repair_pad = -1;      // +0x5a: the repair pad's slot in this unit's side's pool (-1 = the nearest), never cleared (docs/re/50 section 6)
    // Carryall ferry (docs/re/33): the unit to carry (+0x0c, id), its saved
    // state (+0x26) and the request slot (+0x29).
    int carry_unit = -1;
    uint8_t carry_saved = 0;
    int8_t carry_slot = 0;
    uint8_t flash = 0;        // +0x1d: flashed as an order target (FUN_0049f310); grey while & 2, -1 per drawn frame (docs/re/44)
    // Stealth (docs/re/32): flag 0x10 can cloak, flag 4 cloaked; +0x31 the
    // 22-tick fade timer.
    uint8_t fade = 0;
    // Deviator (docs/re/40): +0x2d the original side; the deviation timer is
    // +0x2e, the same byte as `delivery_entry` (one byte in the exe).
    uint8_t orig_side = 0;
    uint8_t smoke = 0;        // +0x27: damage smoke timer (rand() when below half, docs/re/43)
    // Ornithopter (docs/re/35): wing direction +0x24, wing timer +0x25
    // (the wing frame is `frame`, +0x1e) and bombs left +0x29.
    uint8_t wing_dir = 0, wing_timer = 0, bombs = 0;
    // Devastator self-destruct (docs/re/37 section 1.4): the countdown, +0x29.
    uint8_t explode_delay = 0;
};

// A bullet in flight (FUN_00456a30 / FUN_0049be60).
struct Bullet {
    int type = 0, owner = 0;
    uint16_t slot = 0;        // its slot in the owner's pool (docs/re/50)
    int firer = -1;           // unit id, or -1000 - building index
    int target = -1;          // unit id (homing)
    uint32_t flags = 0;
    int32_t speed = 0, dx = 0, dy = 0, vz = 0;
    int32_t ticks = 0, delay = 0;
    int32_t x = 0, y = 0, height = 0; // 16.16
    int tx = 0, ty = 0;       // target pixel (scattered)
    int frame = 0;            // +0x44
    int rot = 0, last_rot = 0; // +0x45 / +0x46: homing rotation (in frames)
    bool dead = false;
};

// bin\VARS.BIN rule values (0x6b8818..), 1.06 defaults.
struct Vars {
    int32_t unload_delay = 60, blob_value = 100, load_delay = 60;
    int32_t refinery_fx_x = 62, refinery_fx_y = -79, drive_distance = 64;
    int worm_appetite = 3, worm_initial_sleep = 50, worm_fed_sleep = 40, worm_shot_sleep = 90;
    int32_t repair_points = 12, unit_repair_points = 8; // +0x30 / +0x34 (doc 21)
    // The Starport (+0x0c..+0x1c, docs/re/34 section 0).
    int32_t starport_update_delay = 1500, starport_stock_delay = 1500, starport_stock_prob = 10;
    int32_t starport_variation = 25, starport_frigate_delay = 1500;
    int32_t repair_carry_distance = 64; // +0x2c: a unit going to the pad asks for a carryall beyond this (doc 33)
    uint8_t number_of_fremen = 2;       // +0x3c (byte 0x6b8854): Fremen per palace call-in (docs/re/39)
    uint8_t death_hand_accuracy = 3;    // +0x4d (byte 0x6b8865): the target scatter (docs/re/41)
    uint8_t devastator_explode_delay = 100; // +0x43 (byte 0x6b885b): the self-destruct countdown (docs/re/37)
    int32_t chase_give_up = 2;          // +0x44 (0x6b885c): the AI drops a chase this far beyond its range (docs/re/53 section 7.2)
    int32_t crate_cash = 1000;          // +0x48 (0x6b8860): CrateCash (docs/re/54 section 3.1)
};

// A one-pixel particle (0x6b9920, 4096 slots): float position and speed,
// life in ticks; its colour is pixel `pixel` ((row << 5) | column) of tile
// `tile`, both read when the particle was made, darkened (FUN_0045d650).
struct Particle {
    float x = 0, y = 0, vx = 0, vy = 0;
    int life = 0;
    int32_t tile = 0, pixel = 0;
};

// A delivery-table entry (player +0x262ec, doc 13 section 5).
struct Delivery {
    int x = 0, y = 0;            // delivery cell
    bool started = false, active = false;
    uint32_t due = 0;            // tick
    std::vector<int> records;    // unit records; -2 = done (0xfe)
    int type = 1;                // 1 carryall, 2 Starport frigate
    bool silent = false;         // +0x36 = 4: no REINFORCE voice
    int action = 0;              // +0x36: deploy action (GUARD 0, ATTACK 1, RETREAT 2, STAY 3)
};

// An effect / explosion object (FUN_00456df0). Lifetime: FUN_0049dd80
// (tick_effect, docs/re/14 section 4.4); drawing: docs/re/14 section 5. The
// attached-unit field +0x08 is left at 0xFFFF by every creator (the callers
// only write +0x01, the muzzle frame), so its "unit died" branch never runs
// and is not kept here.
struct Effect {
    int explosion = 0;        // +0x00 type
    int frame = 0;            // +0x01
    int counter = 0;          // +0x02
    uint32_t flags = 0;       // +0x04: 0x6e5750[type] ^ caller flags
    int32_t x = 0, y = 0;     // pixels
    int32_t height = 0;       // pixels
    int owner = 0;
    int32_t move_x = 0, move_y = 0; // +0x30 / +0x34: one-time move (16.16, flag 0x1000)
    uint16_t slot = 0;        // its slot in the owner's pool (docs/re/50)
};

struct Building {
    int index = 0;            // position in buildings() (references use it)
    uint16_t slot = 0;        // +0x8c: the slot in its side's pool (docs/re/50); staggers the 8-tick phases
    int owner = 0;
    int house = 0;
    int type = 0;
    int record = 0;
    int cell_x = 0, cell_y = 0;
    int32_t x = 0, y = 0;     // 16.16, bottom-left of the footprint
    int32_t hit_points = 0;
    int frame = 0;            // +0x39
    bool selected = false;    // +0x35
    uint32_t flags = 0;       // +0x28: 2 building up, 0x10 working, 0x20 one animation
                              // cycle, 0x40 animates, 0x80 primary, 0x8000 not on concrete
    uint8_t buildup = 0;      // +0x3e: build-up counter (3 ticks per frame)
    uint8_t anim = 0;         // +0x3f: active animation counter
    // Combat (docs/re/18-combat.md).
    bool dying = false;       // state 0x11
    uint8_t death_timer = 0;  // +0x3b
    bool removed = false;     // gone from the map (kept so indices stay valid)
    uint8_t turret_facing = 0; // +0x21
    uint8_t wall_frame = 0;    // +0x20: walls / turrets, the image by the wall neighbours (FUN_0044e580)
    uint8_t turn_counter = 0, reload = 0; // +0x36 / +0x37
    int target_kind = 0, target = -1;     // +0x2c / +0x3c
    int state = 2;            // +0x30: 2 idle / auto-fire, 4 / 5 ordered attack (docs/re/38); dying uses `dying`
    uint8_t flash = 0;        // +0x38: flashed by an order (FUN_0049f320); grey while & 2, -1 per drawn frame (docs/re/44)
    uint8_t smoke = 0;        // +0x3d: damage smoke timer (rand() when below half, docs/re/43)
    uint32_t smoke_points = 0; // +0x4c: |= rand() with the smoke timer
    uint8_t hit_age = 0xff;   // +0x41: 0 when hit (flag 0x1000), +1 every 8 ticks
    int attacker_side = -1, attacker = -1; // +0x50 / +0x52: the first attacker (unit id)
};

// A placement grid cell (FUN_004285c0 / FUN_004288d0).
struct PlaceCell {
    int cx = 0, cy = 0;
    uint8_t look = 0; // DATA.R16 entry: 0 valid, 1 invalid, 2 valid on concrete
};

// A sound for the front end: a samples.uib name, positional when cx >= 0.
// `voice` = house voice queue (FUN_0046f900 / FUN_0046f820), else an effect.
struct SoundEvent {
    std::string name;
    int cx = -1, cy = -1;
    bool voice = false;
    int sample = -1;    // a samples.uib index instead of a name
    bool mode0 = false; // FUN_0046f820 mode 0: only when no voice waits (docs/re/59 section 3)
    int prio = 0;       // FUN_0046f820's ring: 1 plays first
    bool unique = false; // only when this sample is not playing already (FUN_0046f4f0)
};

SoundEvent chat_sound(); // S_CHATMSG as the network code queues it (FUN_0046f820 mode 0)

// The skirmish map set-up inputs (docs/re/52 sections 2.4 to 2.6).
struct SkirmishSetup {
    int ai_players = 1, unit_count = 6, tech = 7, worms = 0; // gNetAIPlayers / UnitCount / TechLevel / Worms
    int humans = 1;                                           // 0x7984c0: the human sides 0 .. humans - 1
    std::vector<uint8_t> techpos;                             // bin\TECHPOS.BIN, 10 x 10 x 5 bytes
    std::vector<std::pair<int, int>> starts;                  // map special 0x17 cells
    std::vector<Object> map_units;                            // the map's object list (worms are used)
};
// The start cells of a skirmish map (special 0x17), in map scan order.
std::vector<std::pair<int, int>> skirmish_starts(const formats::MapFile& map);

class World {
public:
    // Map-placed buildings go through create_building (pre-placed), which
    // lays concrete with `rng` (MSVC rand()).
    World(GameMap& map, const formats::Templates& templates, const ExeTables& tables,
          const SpeedTable& speed, const std::vector<Object>& placed, Rng& rng,
          std::span<const uint8_t> circles = {}, std::span<const uint8_t> vars_bin = {}, int local = 0);

    void tick();                       // one simulation tick (main loop "Game AI processing")
    // ---- order queue (docs/re/49-order-queue.md) ----
    // FUN_0045bf00: an order posted at tick T runs at tick T + 2 (latency
    // 0x4e3b24 in single player), first in first out, at the start of the
    // unit update, with the side's units that were selected at post time.
    // `run` is the order's handler (the immediate order_* call). Not saved.
    // The AI posts closures (every machine runs the AI itself); the UI posts
    // records (sim/order.hpp), which can travel to other machines.
    static constexpr uint32_t kOrderLatency = 2;
    void post_order(int side, std::function<void()> run);
    // Posts a player order record: sets its tick and its ids (the side's
    // selected units now) and queues it. Returns the queued record.
    Order post(Order o);
    // Network games: called with every record post() numbered for a human
    // side (FUN_0045bcc0 sends it), so the lockstep can send it.
    std::function<void(const Order&)> on_post;
    // ProcessOrder (FUN_00457810) for one record: its handler, with the
    // side's selection set to the record's ids.
    void execute(const Order& o);
    // Target refs in a record as the exe keeps them: (side, slot), resolved
    // when the handler runs (docs/re/50 section 6).
    void encode_refs(Order& o) const;
    Order decode_refs(const Order& o) const;

    // ---- network games (docs/re/57 sections 1, 3.8; docs/design/multiplayer.md section 4) ----
    // Switches the order queue to the network rules: sides 0 .. humans - 1
    // are active humans, the AI the next `ai_players` sides; the latency
    // starts at `latency` (40 LAN, 75 modem / Internet, FUN_0045a7a0, set
    // before ActivateAI posts anything). build_game calls it.
    void start_network(int humans, int ai_players, uint32_t latency);
    // A leave (9) or latency (0x11) record of `o.side`: numbered from that
    // side's counter, run at `o.tick` (set by the poster), queued here.
    Order post_system(Order o);
    // A record from another machine (GetNetworkOrders, FUN_0045c0e0): kept
    // as sent (tick, seq, ids), raised to the latency floor. Records of a
    // side that is no active human, and types above 0x34, are dropped.
    void receive(const Order& o);
    uint32_t latency() const { return latency_; }            // 0x4e3ae8
    bool latency_change_pending() const { return latency_pending_; } // 0x6b988c
    void set_latency_change_pending() { latency_pending_ = true; }
    uint16_t sync_value() const { return sync_; }            // 0x5179c0: drawn at the start of this tick
    bool human_active(int s) const { return human_[size_t(s & 7)]; } // +0x37
    bool has_left(int s) const { return left_[size_t(s & 7)]; }      // +0x3A
    uint32_t next_seq(int s) const { return next_seq_[size_t(s & 7)]; } // +0x1C
    uint32_t posted_seq(int s) const { return posted_seq_[size_t(s & 7)]; } // +0x18: the last number this machine gave
    // "ProcessOrderQueue() Model Out Of Sync": a human order whose tick had
    // passed (FUN_0045d120); its side's queue is blocked from then on.
    bool model_out_of_sync() const { return model_oos_; }
    bool latency_out_of_sync() const { return latency_oos_; }   // 0x6b9890: a late latency record
    // The rate cap and resend time of the last latency record run (0 = no
    // change); the game loop takes them (pacing is not simulation state).
    struct NetRates {
        int cap = 0;
        int resend_ms = 0;
    };
    NetRates take_net_rates() {
        NetRates r = net_rates_;
        net_rates_ = {};
        return r;
    }
    // Order 0x1B (ProcessOrder case 8, docs/re/57 section 3.6): ally with
    // `target`'s side, or declare war when already allied.
    void order_ally(int me, int target);
    void let_ai_take_over(int s); // FUN_0046e9c0
    void ai_take_over(int s);     // FUN_004235e0 "TakeOver_AI" (docs/re/57a)
    bool player_active(int p) const; // +0x24250 (cleared only by remove_side)
    void remove_side(int s);      // FUN_0046e6b0
    void clear_orders() { pending_orders_.clear(); }
    size_t pending_orders() const { return pending_orders_.size(); }
    // Saved games (save_state.cpp): the whole advancing state. Load it into a
    // World freshly set up for the same mission.
    std::vector<uint8_t> save_state();
    bool load_state(std::span<const uint8_t> data);
    uint32_t tick_count() const { return tick_count_; } // 0x5173f4

    std::vector<Unit>& units() { return units_; }
    // Position in units() of the unit with this id (Unit::index), -1 if gone.
    // Positions change when units are removed (end of a tick); ids do not.
    int find_unit(int id) const {
        for (size_t i = 0; i < units_.size(); ++i) {
            if (units_[i].index == id) return int(i);
        }
        return -1;
    }
    const std::vector<Unit>& units() const { return units_; }
    std::vector<Building>& buildings() { return buildings_; }
    const std::vector<Building>& buildings() const { return buildings_; }
    const GameMap& map() const { return map_; }
    const formats::Templates& templates() const { return templates_; }

    // ProcessOrder case 0 (type 0x13, posted by GenerateUnitMoveOrder and
    // run 2 ticks later, docs/re/49): every selected unit of `owner` heads
    // for the cell.
    void order_move(int owner, int cell_x, int cell_y);
    // Dock order 0x17 (FUN_00459540): selected harvesters go to that refinery.
    void order_dock(int owner, int building);
    bool is_harvester(const Unit& u) const { return templates_.unit_records[size_t(u.record)].kind == 1; }
    std::vector<Effect> effects;
    std::vector<Particle> particles;
    std::vector<Bullet> bullets;
    // Frame size (width, height) of each building element's first image
    // (runtime image +0x1a / +0x19), for the debris origin. Set by the app.
    std::vector<std::pair<uint8_t, uint8_t>> building_frame;
    // Frame width (runtime image +0x1a) of each building element's images
    // 0x6e5c40[element * 32 + k], for the turret muzzle flash. Set by the app.
    std::vector<std::array<uint8_t, 32>> element_frame_w;
    // HomeView's (FUN_0046cf10) image offset_y of each building element:
    // element image 0 (0x6e5c40[e * 32]) when not empty, else the base image
    // (0x6e5880[e]); runtime image +0xc (docs/re/25 A.8).
    std::vector<int> element_view_dy;
    // Frame height of each building element's frame-0 image (0x6e5c40[e * 32],
    // else the base image; runtime image +0x19), for the smoke points. Set by the app.
    std::vector<int> element_frame0_h;
    int shake_ticks = 0;  // 0x5178d8
    int shake_y = 0;      // 0x5178d4: the whole screen is blitted this many px lower
    // ---- mission script (docs/re/19-mission-script.md, sim/mission.cpp) ----
    void set_script(const formats::MissionFile& mis);
    void set_ai(const formats::MissionFile& mis);     // the AI blocks, then ActivateAI per active slot
    // The live conditions (counters included), e.g. for tests that skip ahead in time.
    std::vector<formats::MissionCondition>& script_conditions() { return conditions_; }
    // AI block fields by exe offset (docs/re/22 section 1).
    uint8_t ai_u8(int s, size_t o) const;
    uint32_t ai_u32(int s, size_t o) const;
    float ai_f32(int s, size_t o) const; // the conditions, events and timer
    uint16_t ai_u16(int s, size_t o) const;
    bool campaign = true;            // the script runs only in the campaign
    bool won = false, lost = false;  // 0x6b8274 / 0x6b8278
    int32_t mission_timer = -1;      // 0x6b8270 (display only)
    struct Message {
        uint32_t text = 0xffffffff;  // text.uib entry index
        int ticks = 201;             // expiry = tick + 200, dropped once passed (FUN_004255e0)
        std::string key;             // or a text.uib key (FUN_0048dc10), when text is unset
        // Network lines "%s %s %s" (ProcessOrder case 8): `prefix` (a player
        // name), the text, `arg` (the other player's name).
        std::string prefix, arg;
        std::string literal;         // a line as is (chat, received in the UI pass)
    };
    std::array<std::string, 8> net_names{}; // network players' names (0x798630 + p * 0x3c)
    std::vector<Message> messages;   // newest first, up to 5 (FUN_00425720)
    // The colour slot of each side (0x5175d8, FUN_00461c00): the side itself
    // in the campaign, the lobby colours in skirmish (docs/re/52 section 2.1).
    std::array<uint8_t, 8> side_colour{0, 1, 2, 3, 4, 5, 6, 7};
    int max_crates = 0; // 0x548048: VARS NumberOfCrates when crates are on in skirmish, else 0
    int net_worms = 0;  // gNetWorms 0x6b9838 (0..3; saved with the game)
    bool game_over() const { return game_over_; }
    bool human_alive() const { return human_alive_; }
    // Finishing places 0x797b70 (0xFF = none) and the next one 0x797e38:
    // reset at every game start, not saved (docs/re/56 section 2).
    std::array<uint8_t, 8> place_{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint8_t next_place_ = 0;
    int colour_of(int side) const { return side_colour[size_t(side & 7)]; }
    bool ai_active(int side) const { return ai_[size_t(side & 7)][1] != 0; } // AI block +0x01
    void defeat_check(int side, int mode); // FUN_00455290
    void defeat_check_all();               // FUN_0044f770
    // FUN_0049ca10; `flag_2000` = its third argument (FUN_0046e6b0's side
    // removal), which sets building flag 0x2000. Its only reader is the
    // survivors release FUN_004977a0 (0x4977cf `test ah, 0x20`: no
    // survivors), ported in release_survivors.
    void destroy_building(int index, bool flag_2000 = false);
    // The per-side object pool (docs/re/50): 1000 slots shared by units,
    // buildings, bullets and effects (FUN_0046bd40).
    static constexpr int kPoolSize = 1000;
    bool pool_full(int side) const { return pool_free_[size_t(side & 7)].empty(); }
    // The side's free slots, the next one to be taken last (the LIFO head,
    // side +4). A fresh side hands out 999, 998, ... (the reset at 0x46ba3d).
    const std::vector<uint16_t>& pool_free_slots(int side) const { return pool_free_[size_t(side & 7)]; }
    // The object now in slot `slot` of `side`'s pool, as the exe reads a
    // stored (side, index) pair: whatever holds the slot today, which can be
    // a newer object than the one first stored (docs/re/50 section 6).
    // Null when the slot holds no unit / building.
    const Unit* unit_in_slot(int side, int slot) const;
    const Building* building_in_slot(int side, int slot) const;
    // The pool slot of a firer in our convention (unit id, -1000 - building
    // index), -1 for none.
    int firer_slot(int firer) const;
    // GetBuilding (FUN_004a4740): index in buildings() of the building now in
    // that slot, -1 if none or the side is inactive (+0x24250).
    int building_index_in_slot(int side, int slot) const {
        const Building* b = slot >= 0 && player_active(side) ? building_in_slot(side, slot) : nullptr;
        return b ? b->index : -1;
    }
    int building_slot(int index) const { return index >= 0 ? buildings_[size_t(index)].slot : -1; }
    // The harvester's refinery (+0x58) / the unit's repair pad (+0x5a) as the
    // exe reads them: GetBuilding(own side, slot), whatever building holds it.
    int refinery_of(const Unit& u) const { return building_index_in_slot(u.owner, u.refinery); }
    int repair_pad_of(const Unit& u) const { return building_index_in_slot(u.owner, u.repair_pad); }
    void reached_unit_limit(); // the local line, for any side (no side test)
    void set_armour(std::span<const uint8_t> armour_bin); // Data\bin\ARMOUR.BIN
    // Data\bin\BUILEXP.BIN (FUN_0046ad80): per building record, up to 8
    // damage smoke / flame points (docs/re/43 section 3.1); 3,300 bytes.
    struct SmokePoints {
        uint8_t n = 0;
        int8_t x[8] = {}, y[8] = {};
        uint8_t type[8] = {}, period[8] = {};
    };
    void set_smoke_points(std::span<const uint8_t> buildexp_bin);
    const std::vector<SmokePoints>& smoke_points() const { return smoke_points_; }
    bool enemy(int a, int b) const;                  // alliance[a][b] == 1
    void damage_at(int x, int y, int32_t height, int type, int attacker, int firer, bool deviate = false); // FUN_0049d2e0
    bool on_building_cell(const Unit& u) const;      // FUN_0049d280
    bool deviation_runs(const Unit& u) const;        // FUN_0049fb40
    void damage_tile(int x, int y, int32_t height, int type); // its crater tail (docs/re/26)
    int alliance(int a, int b) const { return alliance_[size_t(a & 7)][size_t(b & 7)]; } // 0 allied, 1 enemy, 2 neutral
    int unit_at_cell(int x, int y) const { bool inf; return unit_in_cell(x, y, inf); } // FUN_004a5270
    // FUN_004a5270(px, py, .., skip_cloaked) on a world pixel (docs/re/10
    // "Hit test"): cell based, the cell owner's vehicle on the cell, else
    // its infantry nearest to the pixel. Unit vector index or -1.
    int unit_at_pixel(int px, int py, bool skip_cloaked, bool* infantry = nullptr) const;
    // The playfield click / cursor unit lookup (0x445683, 0x443562): skip
    // cloaked units on a cell with another side's unit bits, then a cloaked
    // unit of a side not allied to `player` counts as none.
    int click_unit(int player, int px, int py) const;
    bool building_alive(int b) const;
    void order_attack_unit(int owner, int target_id);    // order 0x14
    void order_attack_building(int owner, int building); // order 0x15
    void order_attack_ground(int owner, int cx, int cy);  // order 0x16 (FUN_004598d0, Ctrl on an empty cell)
    bool can_repair_building(int p, int index) const;
    bool can_sell_building(int p, int index) const;
    void order_repair_building(int p, int index);       // order 0x22 (toggle)
    void order_sell(int p, int index);                  // order 0x23
    void order_guard(int p);                            // order 0x18
    bool retreat_destination(int p, int& x, int& y) const; // FUN_0046e3f0
    void order_retreat(int p, int x, int y);            // order 0x1a
    // Control groups and keyboard orders (docs/re/25, sim/groups.cpp).
    void assign_group(int p, int d);                    // FUN_0044b170 (Ctrl + digit)
    bool select_group(int p, int d, bool exclusive);    // FUN_0046cb40 (digit / Shift + digit), own side only
    void deselect_other_sides(int p);                   // FUN_0046cb00 / FUN_0046cb20 for every side != p
    bool group_position(int p, int d, int& px, int& py) const; // FUN_0046cbd0 (Alt + digit)
    void order_stop(int p);                             // S, order 0x1c / 0x1d
    int selected_turret(int p) const;                   // FUN_004a5860: building index or -1
    // Orders 0x1E / 0x1F (docs/re/38 section 4): the selected turrets
    // attack a unit (id) or a building (index).
    void order_turret_attack_unit(int p, int unit_id);
    void order_turret_attack_building(int p, int index);
    // FUN_00497140: range from the building's top-left cell centre to the
    // centre of (cx, cy); `air` uses the anti-air weapon rules.
    bool building_in_range(const Building& b, int cx, int cy, bool air) const;
    bool building_in_range_of(const Building& b, int target, int& cx, int& cy) const; // FUN_00497490
    void order_scatter(int p);                          // X, order 0x19
    void order_primary(int p);                          // P, order 0x20
    // Unit repair (docs/re/21 section 4): order 0x21 for the selected
    // vehicles to a pad (FUN_004595e0), or one unit to the nearest pad
    // (repair mode, FUN_00459690, pad -1).
    void order_repair_units(int p, int pad);
    void order_repair_unit(int p, int unit_id);
    void unit_voice(int record, bool ack);              // one unit's select / ack voice (FUN_0046f820)
    // FUN_004a5f80: the nearest building of a kind (Manhattan to its far
    // corner +0x18 / +0x19), and its entry cell (top-left + rec +0x60 / +0x61).
    int nearest_kind(int p, int kind, int x, int y, int& cx, int& cy) const;
    bool can_be_primary(int p, int bi) const;           // FUN_004432f0
    void next_unit(int p);                              // N, FUN_004a5de0
    bool selection_centre(int p, int& px, int& py) const; // FUN_004a5c00 / FUN_004a5cd0
    int building_at_cell(int x, int y) const;          // FUN_004a5640
    int wall_frame_at(int x, int y) const;        // FUN_0044e580
    void update_walls_around(int x, int y);       // FUN_0044e6d0
    Vars vars;
    void set_vars(std::span<const uint8_t> vars_bin); // Data\bin\VARS.BIN
    // The local view (scroll) in world pixels, for on-screen tests (worm sign).
    int view_x = 0, view_y = 0;
    // Queues a delivery (FUN_004563b0 for a new refinery): first entry
    // that is not active, up to 10.
    void queue_delivery(int p, int x, int y, std::vector<int> records, int type, bool silent, int action = 0);
    const std::vector<Delivery>& deliveries(int p) const { return deliveries_[size_t(p)]; }

    // Selection (FUN_004a5a50 / FUN_004a5ab0 / FUN_004a5790 / FUN_004a5ae0).
    bool selectable(const Unit& u, int player) const;
    void deselect_all();
    void select_unit(size_t i, bool toggle);
    void view_unit(size_t i);                   // FUN_004a5a50(..., 0) for a unit of any owner
    void select_building(size_t i);
    void select_building_checked(size_t i);        // FUN_004a5ab0
    bool box_select(int player, int x0, int y0, int x1, int y1); // world pixels
    bool any_unit_selected(int player) const;

    // ---- economy and production (docs/re/11-sidebar.md, sim/economy.cpp) ----
    // Mission start: houses, tech levels and cash from the .MIS
    // (FUN_0046bd20), then the build lists.
    void init_players(const formats::MissionFile& mis);
    Player& player(int p) { return players_[size_t(p)]; }
    const Player& player(int p) const { return players_[size_t(p)]; }
    void rebuild_lists(int p);                       // FUN_0046be50
    // Orders 0x24 / 0x25 / 0x27 / 0x28 (FUN_00455fc0 / 004560f0 / 00455510 / 00455750).
    void start_building(int p, int record);
    // Upgrades (docs/re/28-upgrades.md).
    void build_upgrade_lists();                      // FUN_0046e7a0 (mission start)
    bool owns_name(int p, int name) const;           // player +0x245c1[name] != 0
    bool can_upgrade(int p, int name) const;         // FUN_004431d0
    int upgrade_cost(int p, int record, int level) const; // FUN_00442be0(record, level, p)
    void start_upgrade(int p, int name);             // order 0x2D (FUN_00457350)
    void hold_or_cancel_upgrade(int p, int record);  // order 0x2E (FUN_00457490)
    void tick_upgrade(int p);                        // FUN_004467e0 0x446b3b
    void ai_upgrade_buildings(int s);                // FUN_00421c40
    void hold_or_cancel_building(int p, int record);
    void start_unit(int p, int record);
    void auto_start_specials();                     // FUN_004466e0
    // Order 0x30 (FUN_00457810 case 0x1d): a ready special of `kind` used
    // at cell (x, y); kind 9 launches the airstrike.
    void order_special(int side, int kind, int x, int y);
    void launch_airstrike(int side, int ox, int oy, int tx, int ty); // FUN_0049e770
    void launch_death_hand(int side, int tx, int ty);               // FUN_0049e860
    int death_hand_step(Unit& u);                                   // FUN_00494cd0
    // HomeView, FUN_0046cf10 (docs/re/25 A.8): the camera top-left (px) that
    // centres the side's base; false when the side has no building or unit
    // (the exe then returns the local camera). The only implementation: the
    // mission start, the H key (select_mcv = true: a chosen MCV becomes the
    // local side's only selection) and the airstrike use it.
    bool home_view(int side, int& x, int& y, bool select_mcv = false);
    void ai_special_weapons(int s);                 // FUN_00422270 "DoSpecialWeapons"
    void ai_do_fremen(int s);                       // FUN_00422a80 "DoFremen"
    void ai_do_saboteur(int s);                     // FUN_00422900 "DoSaboteur"
    void ai_merge_groups(int s, int g, int into);   // FUN_00421710 "MergeGroups"
    int ai_group_strength_job(int s, int g, int job) const; // GetGroupStrength with an explicit job
    int ai_item_ready(int s, int kind) const;       // FUN_00422730: the slot, or -1 (not ready)
    // FUN_00422800 "GetBestTarget": 1 + unit position or 2 + building
    // position in (x, y) px; 0 = none.
    int ai_best_target(int side, int kind, int& x, int& y);
    void hold_or_cancel_unit(int p, int record);
    // A unit item that no other slot's factory blocks (FUN_004430d0).
    bool unit_allowed(int p, int record) const;
    bool is_special(int record) const;               // kinds 9, 10, 4, 12
    int unit_cost(int p, int record) const;          // FUN_00442bb0
    int building_cost(int p, int record) const;      // FUN_00442be0
    bool spend(int p, int32_t amount);               // FUN_0046c720
    void refund(int p, int32_t amount);              // FUN_0046c7b0
    int difficulty = 1;                              // 0x4e8bf0: 0 easy, 1 normal, 2 hard
    // A network game (0x7984c4) and each side's handicap byte (0x6b922e +
    // p * 0x3c: 0 easy, 1 normal, 2 hard; docs/re/57 section 3.4).
    bool network = false;
    std::array<uint8_t, 8> handicap{};
    int factor_level(int p) const; // the cost / speed level of side p (FUN_00442960 / FUN_004429b0)

    // ---- placement and spawn (docs/re/12-placement-and-spawn.md, sim/placement.cpp) ----
    // The client grid: cells to draw and whether the click is allowed.
    // (cx, cy) is the cell under the mouse (the mask's top-left).
    bool placement_check(int p, int record, int cx, int cy, std::vector<PlaceCell>& cells) const;
    void place_building(int p, int record, int cx, int cy);              // order 0x26
    // FUN_004563b0(p, rec, x, y, f5 = preplaced, f6 = captured, f7 = no crane).
    int create_building(int p, int record, int cx, int cy, bool preplaced, bool captured = false,
                        bool no_crane = false);
    // MCV deploy, engineers, saboteurs (docs/re/30, capture.cpp).
    bool can_deploy(int x, int y) const;                // FUN_00443250
    // The Starport cart (docs/re/34 section 3): orders 0x29 add, 0x2a
    // remove, 0x2b purchase, 0x2c cancel.
    void starport_add(int p, int record);
    void starport_remove(int p, int record);
    void starport_purchase(int p);
    void starport_cancel(int p);
    bool cart_empty(int p) const;                       // FUN_00443370
    void order_deploy(int p, int unit_id);              // order 0x2F
    bool capture_building(int victim, int captor, int index); // FUN_0046d6d0
    int convert_unit(int from, int to, int unit_id, bool keep_state); // FUN_0046d2e0, the new id or -1
    void make_hostile(int a, int b);                    // FUN_0049f870
    // FUN_00442c20: the factory for a unit and its spawn / first move cells; -1 if none.
    int find_factory(int unit_record, int p, bool check, int& sx, int& sy, int& mx, int& my) const;
    // Returns the new unit's position in units() (valid until the end of the tick), -1 on failure.
    int create_unit(int p, int record, int x1, int y1, int x2, int y2); // FUN_00455870
    // The skirmish map set-up after the mission read (FUN_00469790, docs/re/52).
    void skirmish_start(const SkirmishSetup& setup);
    void add_map_unit(const Object& o); // a map unit, as FUN_00469790 creates it
    int local_player = 0;                            // 0x798544
    std::vector<SoundEvent> sounds;                  // drained by the front end
    // A house voice for the local player ("BUILDREADY" -> S_/H_/O_), with
    // the repeat limit in ticks (FUN_0046f820).
    void voice(int p, const char* base, uint32_t limit = 0);
    // FUN_0049f240: the select (ack = false) or acknowledge response of the
    // local player's selected unit with the highest priority (ties: later).
    void unit_response(bool ack);

    // ---- shroud (docs/re/16-shroud.md, sim/shroud.cpp) ----
    void set_circles(std::span<const uint8_t> circles_bin); // Data\bin\CIRCLES.BIN
    void reveal_circle(int cx, int cy, int r);       // FUN_0044e3b0
    void edge_pass(int x0, int y0, int x1, int y1);  // FUN_0044cd10, half-open rect
    void reveal_building(int record, int cx, int cy); // FUN_0044e330
    void reveal_start();                             // FUN_0049f4d0
    void open_map();                                 // FUN_0044c480: every shroud byte 0 (the radar is not touched)
    // FUN_004418b0's radar rebuild after a load: the shown radar is black
    // except the cells whose shroud byte is 0 (doc 16 section 4.3).
    void rebuild_shown_radar();
    // 0x54804c: the exe never resets it, so its phase carries over from the
    // games played before in the same run (doc 16 section 3.4).
    uint32_t reveal_counter() const { return reveal_counter_; }
    void set_reveal_counter(uint32_t v) { reveal_counter_ = v; }
    // Full shroud (byte & 0xf == 1) and fully open (byte == 0): the two tests.
    bool shrouded(int x, int y) const { return (map_.at(x, y).state & 0xf) == 1; }
    bool open(int x, int y) const { return map_.at(x, y).state == 0; }

    // Passable(cell, unit, mode) (FUN_0045d990); mode 0 respects units,
    // modes 1..3 ignore more and more of them (doc 09 section 8; every
    // branch checked against the exe).
    bool passable(int cx, int cy, const Unit& u, int mode) const;

private:
    friend struct WorldTestAccess; // unit tests of private exe routines (tests/sim/test_ai.cpp)
    bool set_state(Unit& u, int s);                  // FUN_0049deb0
    int move_step(Unit& u);                          // FUN_00494260
    void next_square(Unit& u);                       // FUN_004936a0
    // `complete` = the route reaches the destination (else partial).
    // FUN_0045eaf0: 1 reached, 3 / 4 partial or none (pathfind.cpp).
    int find_path(const Unit& u, int mode, std::vector<std::array<int, 2>>& out) const;
    void smooth_path(const Unit& u, std::vector<std::array<int, 2>>& p) const; // FUN_0045fcc0
    bool free_adjacent_cell(const Unit& u, int x, int y, int mode, int& ox, int& oy) const; // FUN_0045dee0
    void ask_to_scatter(const Unit& u, int x, int y);  // FUN_00497af0
    bool takes_orders(const Unit& v) const;            // FUN_0049f4a0(v, 0)
    void tick_scatter(Unit& u);
    void tick_repair(Unit& u);
    void tick_deploy(Unit& u);                    // state 0x1E (FUN_00497b90 case 0x1C)
    void tick_self_destruct(Unit& u);             // state 0x1F (FUN_00497b90 case 0x1D)
    void tick_thump(Unit& u);                     // state 0x20 (0x49b45f, docs/re/54 section 4.4)
    // The carryall ferry (docs/re/33, ferry.cpp).
    void launch_carryall(Unit& u, int x1, int y1, int x2, int y2); // FUN_00455870 kind 2
    void carryall_removed(const Unit& u);         // the counts at removal (FUN_00458e90)
    int find_carry_slot(int p, const Unit& u) const; // FUN_0046cca0
    bool add_to_queue(int p, const Unit& u, int s, int kind, int saved); // FUN_0046cd00
    int cell_dist2(const Unit& u, int x, int y) const; // FUN_00421800
    int find_request_for(int p, const Unit& c) const;  // FUN_0046ce00 / FUN_0046ce70
    void request_monitor(int p);                  // FUN_0046d180
    void carry_request(Unit& u, int threshold);   // the lift request of states 0xb / 0xd / 0x18
    void tick_waiting(Unit& u);                   // state 0x12
    bool ferry_idle(Unit& u);                     // carryall state 2, ferry branch
    bool ferry_job_cancelled(Unit& u);            // carryall state 7
    void ferry_descend(Unit& u);                  // carryall state 0x15, ferry branch
    void carry_tail(Unit& u);                     // the cargo follows its carryall
    void carryall_killed(Unit& u);                // KillUnit: cargo and taken slots
    void tick_aircraft_state(Unit& u);            // the aircraft states (tick_aircraft adds the cargo tail)
    void tick_enter(Unit& u);                     // state 0x17 (case 0x15)
    int enter_building(Unit& u);                  // FUN_00496d10 for kinds 3 / 4
    bool entry_cell(int px, int py, const Building& b, int& ex, int& ey) const; // FUN_004a47f0
    int move_simple(Unit& u, bool clear_old, bool reserve); // FUN_00495560
    bool takes_orders_forced(const Unit& u) const; // FUN_0049f4a0(u, 1)
    void clear_building_cells(const Building& b);  // FUN_0044df60 + the wall neighbours, then FUN_0044f2f0
    bool art_name_is(int record, const char* name) const; // strcmpi on 0x7891b0 + record * 0x1C2
    void convert_footprint_units(int p, int x, int y); // FUN_0044dbd0's unit loop for one cell
    void write_skirt(int record, int cx, int cy);  // FUN_0044f1b0
    void clear_skirt(int record, int cx, int cy);  // FUN_0044f2f0 (rand() per sand cell)
    // The kill / loss counters, totals and per record (docs/re/60).
    void count_unit_lost(int side, int record);    // +0x24a08, +0x24a10[rec]
    void count_unit_killed(int side, int record, int index_side);     // +0x24c90, +0x24c98[rec * 8 + i]
    void count_building_killed(int side, int record, int victim);     // +0x24c94, +0x25418[rec * 8 + v]
    void ai_mcvs(int s);
    bool ai_mcv_site(int s, int x, int y) const;  // FUN_00423020 "CanIDeployMCVHere"
    void ai_deal_with_mcv(int s, Unit& u);        // FUN_00422ec0 "DealWithMCV"                           // FUN_00422140 "DoMCVs"                    // states 0x18 / 0x19 / 0x1a (FUN_00497b90 cases 0x16-0x18)                        // state 9
    void tick_attack_blocker(Unit& u);                 // state 0xa
    bool turn_body(Unit& u, int dir);                // FUN_00493eb0
    int32_t cell_speed(const Unit& u, int cx, int cy) const; // FUN_0049f3b0
    void occupy(Unit& u, int cx, int cy, bool placing);
    void vacate(Unit& u, int cx, int cy, uint8_t slot);
    int free_slot(int cx, int cy, int owner, int from) const;
    void tick_economy();                             // per tick, sim/economy.cpp
    void update_power(int p);                        // FUN_0046ca00
    void update_capacity(int p);                     // FUN_0044abe0
    void drip_money(int p);                          // FUN_0046c7d0
    void storage_pass();                             // FUN_0044abe0: drip + capacity, every side
    void tick_production(int p);                     // FUN_004467e0 (progress part)
    int factory_factor(int p, int prereq) const;     // FUN_00442a00 / FUN_00442b10
    void lay_concrete(int p, int record, int cx, int cy, uint16_t mask); // FUN_00456210
    void reveal_units();                             // FUN_0044cff0
    void tick_buildings(int p);                           // FUN_0049b7f0 (build-up, decay, animation)
    void spawn_ready(int p, ProductionSlot& s);      // FUN_00442c20 + FUN_00455870
    // ---- harvesting (sim/harvest.cpp) ----
    void tick_harvester(Unit& u);                    // FUN_00497b90, states 0xb..0x10
    bool find_spice(const Unit& u, int& x, int& y) const;   // FUN_0044d760
    bool prefer_thick(int& x, int& y) const;                 // FUN_0044d970
    void eat_spice(int x, int y);                            // state 0xc
    int least_used_refinery(int p) const;                    // FUN_004a5e80
    int nearest_refinery(int p, int x, int y) const;         // FUN_004a5f80 (kind 4)
    bool dock_cell(int building, int& x, int& y) const;
    bool free_cell_near(int p, int& x, int& y) const;        // FUN_0049e290
    bool valid_refinery(int building) const;
    // FUN_004a4740(side, +0x58): the harvester's refinery looked up in its
    // OWN side's list; -1 when that is not a refinery (docs/re/40 section 7).
    int own_refinery(const Unit& u) const;
    void spawn_effect(const char* name, int32_t x, int32_t y, int owner);
    // ---- aircraft, deliveries, effects (sim/aircraft.cpp) ----
    int flight_step(Unit& u, int mode);              // FUN_00494fd0
    void turn_air(Unit& u, int want);                // FUN_004954b0
    void tick_aircraft(Unit& u);                     // states 2 / 7 / 0x11 / 0x15 / 0x16 for flag 0x40
    int drop_step(Unit& u);                          // FUN_0049e3e0
    void tick_deliveries(int p);                     // FUN_0046def0
    int edge_point(int& x, int& y, int k) const;     // FUN_0049e6b0
    bool spiral_free_cell(int& x, int& y, int r) const; // FUN_004a60e0
    void tick_effects(int side);                     // FUN_0049dd80
    void tick_effect(Effect& e);
    // ---- sandworm (sim/worm.cpp) ----
    void tick_worm(Unit& u);                         // states 0x1b / 0x1c / 0x1d
    int unit_in_cell(int x, int y, bool& infantry, bool skip_cloaked = false) const; // FUN_004a5270
    // Stealth (docs/re/32 section 1).
    bool is_stealthed(const Unit& u) const;                 // FUN_0049f5f0
    bool uncloak(Unit& u);                                  // FUN_0049f5c0
    bool cloak(Unit& u);                                    // FUN_0049f580
    bool stealth_blocker(int cx, int cy, int side, int r) const; // FUN_0049f750
    void tick_stealth(Unit& u);                             // FUN_0049f610
    void uncloak_last_units(int side);                      // FUN_00455480
    void default_end_check();                               // FUN_00453b90, skirmish / network branch
    void assign_places();                                   // FUN_0044c520
    bool nearest_vehicle_on_sand(int cx, int cy, int& vx, int& vy) const; // FUN_0044d630
    bool nearest_thumper(int cx, int cy, int& tx, int& ty) const;          // FUN_0044d500
    void ripple(int32_t x, int32_t y);               // FUN_0045d730
    void worm_sign(const Unit& u);
    void tick_particles();                           // FUN_0045d4f0
    void tick_script();                              // FUN_00453b90
    bool evaluate(formats::MissionCondition& c);
    void run_event(const formats::MissionEvent& e);  // FUN_00454120
    // ---- combat (sim/combat.cpp) ----
    int bullet_range(int weapon) const;
    int percent(int weapon, int armour) const;
    int choose_weapon(int prim, int sec, int64_t dist2, bool air_target, int armour) const; // FUN_0049e9d0
    bool can_attack_cell(const Unit& u, int cx, int cy, bool air) const; // FUN_00497270
    int target_unit_in_range(int px, int py, int side, int r, int prim, int sec, bool air, bool worms) const; // FUN_004a4900
    void tick_homing(Bullet& b);                  // FUN_0049be60 flag 0x20
    void tick_bullet(Bullet& b);                  // FUN_0049be60
    int target_building_in_range(int cx, int cy, int side, int r, bool turrets_only, int& tx, int& ty) const;
    void closest_cell(int building, int x, int y, int& cx, int& cy) const; // FUN_00497390
    void closest_cell_manhattan(int building, int x, int y, int& cx, int& cy) const; // FUN_0049d170
    // FUN_00456df0; height and the one-time move (flag 0x1000) are 16.16.
    void spawn_effect_at(int explosion, int32_t x, int32_t y, int owner, int frame, uint32_t caller_flags, int delay,
                         int32_t height = 0, int32_t move_x = 0, int32_t move_y = 0);
    void play_at(int sample, int cx, int cy);
    void shooter_reveal(int cx, int cy);
    int add_bullet(int side, int type, int firer, int x0, int y0, int tx, int ty, int target); // bullets index or -1
    void tick_shake(); // FUN_00446690
    // Spice blooms and the cell objects (docs/re/31-spice-blooms.md).
    void tick_cell_objects();                        // FUN_0044ea70
    void bloom_burst(int x, int y, int n);           // FUN_0044ef10
    void bloom_rearm(int i);                         // FUN_0044f110
    int find_cell_object(int x, int y, int& index) const; // FUN_0044e970: type, 0xFF none
    int pick_up_cell_object(int x, int y);           // FUN_0044e9d0: type, 0x0B none
    int drive_over(Unit& u);                         // FUN_0049ebf0: 1 = units created on the cell
    void add_random_crate();                         // FUN_0044e7c0 / FUN_0044e800
    void spice_lands(int px, int py);                // FUN_0049be60 SpiceExpl impact
    void tick_bullets(int side);
    void kill_unit(Unit& u);                         // FUN_0049cde0 (counts nothing; the callers do)
    void remove_building(int index);
    bool unit_fire(Unit& u, int tx, int ty, int kind, int target); // FUN_00495b90
    void turn_turret(Unit& u, int dir);              // FUN_00493f40
    int auto_fire(Unit& u);                          // FUN_00496480
    bool target_valid(const Unit& u) const;
    void tick_combat_state(Unit& u);                 // states 2 / 3 / 4 / 5 / 6
    int attack_unit(Unit& u);                        // FUN_00496b00
    int attack_building(Unit& u);                    // FUN_00496d10
    void tick_infantry_anim(Unit& u);                // FUN_004940d0 (docs/re/45)
    void turn_step(Unit& u, int goal);               // FUN_004954b0
    void power_sounds();                             // FUN_0044aae0 (sound part)
    // ---- computer player (docs/re/22, 23; sim/ai.cpp) ----
    std::array<std::array<uint8_t, 0x1DB8>, 8> ai_{}; // 0x4f4118
    void set_ai_u8(int s, size_t o, uint8_t v);
    void set_ai_u32(int s, size_t o, uint32_t v);
    // Group g's field as the exe addresses it (block s + g * 0x2C + off), so
    // group 0xFF lands in block s + 1; null past the 8 blocks (other globals).
    uint8_t* ai_group_field(int s, int g, size_t off);
    void ai_activate(int s);                         // FUN_0041b250
    void tick_ai();                                  // FUN_0041b610 per active slot
    int owned_units(int p, int record) const;        // +0x24628[rec]
    int owned_kind(int p, int kind) const;
    int owned_building_kind(int p, int kind) const;
    bool unit_slot_free(int p, int record) const;    // FUN_004430d0(side, rec, 1)
    bool ai_can_build_unit(int s, int record) const; // FUN_0041b850
    int ai_best_unit(int s);                         // FUN_0041b910
    void ai_build_units(int s);                      // FUN_0041bfe0
    void ai_repairs(int s);                          // FUN_0041bf00
    void ai_monitor_harvesters(int s);               // FUN_00421fa0
    bool ai_can_spend(int s, int job, uint32_t amount) const; // FUN_0041e8c0 "CanSpendCash"
    void ai_repair_units(int s, int base, int guard); // FUN_0041e650 "DoRepairUnits"
    void ai_auto_beserk(int s);                      // FUN_00423850
    // Groups and attacks (sim/ai_groups.cpp, docs/re/23).
    std::array<std::array<uint8_t, 32>, 8> ai_target_kind_{}; // 1 unit, 2 building
    void set_ai_u16(int s, size_t o, uint16_t v);
    void base_box(int p, int& x0, int& x1, int& y0, int& y1) const; // +0x262e8..eb
    void snapshot_base_box(int p);                   // the box part of FUN_0046be50
    void ai_base_position(int s, int& x, int& y);        // FUN_0041e180 (remembers the first result)
    int ai_unit_strength(int s, const Unit& u, int job) const; // FUN_0041cb70
    int ai_group_strength(int s, int g) const;
    void ai_group_position(int s, int g, int& x, int& y) const; // FUN_00420a70
    int ai_free_group(int s) const;
    void ai_clear_selection(int s);                  // FUN_0041cdb0
    void ai_select_group(int s, int g, bool all);    // FUN_0041cce0
    void ai_splinter(int s, int from, int to, int strength); // FUN_0041ca60
    bool ai_target_alive(int s, int g) const;
    void ai_commence(int s, int g);                  // FUN_0041fc80
    void ai_return_to_base(int s, int g);            // FUN_0041e340
    bool ai_morale_ok(int s, int g) const;           // FUN_0041e420
    void ai_become_rampaging(int s, int g);
    int ai_recalculate_side(int s);                  // FUN_0041ce50
    int ai_best_side(int s);                         // FUN_0041cdf0
    int ai_best_building(int s, int side, int x, int y, float f); // FUN_0041bbb0
    void ai_attack_best_building(int s, int g, int side, int x, int y); // FUN_0041fee0
    bool ai_nearest_hostile(int s, int g);           // FUN_00420700
    void ai_group_new_units(int s, int group, bool force); // FUN_0041c3e0
    void ai_attack_buildings(int s);                 // FUN_0041c920
    bool ai_recently_in_action(const Unit& u) const; // FUN_0041e2c0
    bool ai_stop_start(int s, int g, int n, bool retarget); // FUN_0041f670
    void ai_attack_object(int s, Unit& u, int kind, int id); // FUN_00420010 (kind 1 unit id, 2 building)
    bool ai_hostile(int s, int side) const;          // alliance 0x798830 != 0
    bool ai_object_is_unit_with(int kind, int id, uint32_t flags) const; // FUN_004218a0 / FUN_00421860 (flags 0)
    void ai_flee_cell(int s, int& x, int& y) const;  // FUN_0049e290
    bool ai_most_appropriate_target(int s, const Unit& u, int& kind, int& id) const; // FUN_00420140
    int ai_percentage(int s, int g, int pct) const;  // FUN_0041e5d0 "GetPercentage"
    int ai_defend_group_id(int s, int a) const;      // FUN_00422230 "GetDefendGroupId"
    void ai_send_group_to_point(int s, int x, int y, int g); // FUN_0041e3c0
    bool ai_send_some_units_to_attack_building(int s, int g, int b, int pct); // FUN_00423440
    void ai_enemy_base_waypoint(int& x, int& y, int tx, int ty); // FUN_00420b20
    bool ai_group_full_health(int s, int g) const;   // FUN_0041e860 "GroupAtFullHealth"
    void ai_defend_area(int s, int g);               // MonitorGroups job 0xF
    void ai_monitor_groups(int s, int off);          // FUN_0041d070
    void ai_defend_checks(int s);                    // FUN_004214a0
    void ai_beserk(int s);
    void ai_protection(int s);                       // FUN_0041c070 (docs/re/29)
    void ai_overrides(int s);                        // FUN_00422c20
    void ai_build_carryalls(int s);                  // FUN_004203d0
    void ai_buy_units(int s);                        // FUN_00421cf0 "BuyUnitsFromStarport"
    bool ai_should_buy(int s, int record);           // FUN_00421e50
    int owned_record(int p, int record) const;       // units of the record (+0x24628[rec])
    void build_starport_list(int p);                 // FUN_0046e7a0 (the Starport part)
    void tick_starport(int p);                       // FUN_0046ddc0
    void starport_lost(int p);                       // FUN_0046d9a0, the last Starport removed
    // Base rebuilding (ai_rebuild.cpp, docs/re/22 section 3.4).
    void ai_initial_buildings(int s);                // FUN_0041e920
    void ai_monitor_buildings(int s);                // FUN_0041eab0
    void ai_rebuild(int s);                          // FUN_0041ebc0
    void ai_rebuild_place(int s);                    // FUN_0041ef30
    void ai_rebuild_next(int s);                     // FUN_0041f560
    bool ai_rebuild_give_up(int s);                  // FUN_0041f5c0
    int ai_best_building(int s) const;               // FUN_00420d50
    bool ai_need_building(int s, int group) const;   // FUN_004218e0
    bool ai_new_position(int s, int record, int& x, int& y) const;      // FUN_00421950
    bool ai_alternative_place(int& x, int& y) const;                    // FUN_00421b60
    bool ai_random_position(int s, int record, int& x, int& y, int tries); // FUN_00420e90
    void ai_near_own(int s, int x, int y, uint8_t& f) const;            // FUN_00428c30
    uint32_t ai_can_place_building(int s, int record, int x, int y) const; // FUN_00421000
    bool ai_can_place_concrete(int s, int record, int x, int y) const;   // FUN_004212a0
    bool ai_concrete_already(int x, int y) const;                       // FUN_00421a90
    bool ai_no_friendly_units(int s, int record, int x, int y) const;   // FUN_00423500
    bool ai_place_to_move(int x0, int y0, int x1, int y1, int range, int& x, int& y) const; // FUN_00423220
    void ai_kick_units(int s, int x0, int y0, int x1, int y1, int exclude = -1); // FUN_00423100
    bool placed_this_pass_ = false;
    template <class Ar>
    void state_io(Ar& a);
    void ai_defence(int s);                          // FUN_0041c270
    bool ai_send_nearest_unit(int s, int g, int x, int y); // FUN_00423360
    void ai_activate_groups(int s);
    void ai_unit_facings(int s);                     // FUN_00420c80 "DoUnitFacings"
    void turret_wander(Unit& u);                     // FUN_00494210
    int ai_group_for_delivery(int s, int entry);     // FUN_00420480
    void ai_delivery_done(int s, int entry);         // the carryall's last drop (0x4989xx)
    // ---- sell / repair / survivors (docs/re/21, sim/orders.cpp) ----
    void do_repairs();                               // FUN_0044a9f0
    bool tick_selling(Building& b);                  // FUN_0049b7f0 (selling part)
    void release_survivors(Building& b);             // FUN_004977a0
    bool free_cell_3x3(int& x, int& y);              // FUN_004979a0
    void reassign_primary(const Building& gone);     // FUN_0046d9a0
    void sold_refund(const Building& b);             // FUN_00458e90 (refund part)
    void set_inf_anim(Unit& u, int mode);            // FUN_00494050
    void tick_building_combat(Building& b);          // FUN_0049b7f0 (weapons)
    bool turret_auto_fire(Building& b);              // FUN_00496790 (state 2)
    void building_fire(Building& b, int tx, int ty, int kind, int target); // FUN_00496030
    void crush_check(Unit& u);                       // FUN_0049fa50
    void crush(Unit& u, int side, int by);       // FUN_0049f8f0

    GameMap& map_;
    const formats::Templates& templates_;
    const ExeTables& tables_;
    SpeedTable speed_;
    std::vector<Unit> units_;
    std::vector<Building> buildings_;
    std::array<Player, 8> players_{};
    int next_unit_id_ = 0;
    // The per-side pools (docs/re/50): free slots as a stack, back = the
    // free-list head. pool_pop (FUN_0046bd40) takes the head, -1 when empty;
    // pool_push (FUN_0046bdb0) makes the slot the new head.
    std::array<std::vector<uint16_t>, 8> pool_free_ = fresh_pools();
    static std::array<std::vector<uint16_t>, 8> fresh_pools();
    int pool_pop(int side);
    void pool_push(int side, int slot);
    void upgrade_pools(); // a save from before version 21: slots given afresh
    void ai_upgrade_rebuild_slots();

    std::array<std::vector<uint8_t>, 8> circles_{};
    uint32_t reveal_counter_ = 0;                    // 0x54804c (never reset in the exe)
    formats::ArmourTable armour_;
    std::vector<SmokePoints> smoke_points_; // BUILEXP.BIN, by building record
    struct PendingOrder {
        Order rec;                 // tick, side, the selected unit ids at post time (and the record)
        std::function<void()> run; // an AI order's handler; empty: execute(rec)
        bool done = false;         // run (network queue: removed after the pass)
    };
    void dispatch(const Order& o); // execute() without the selection swap
    // Network queue state (docs/re/57 section 0, the per-player record).
    // Not saved: a network game has no saves.
    uint32_t latency_ = kOrderLatency;     // 0x4e3ae8
    uint32_t floor_ = 0;                   // 0x6b9810
    uint32_t last_latency_tick_ = 0;       // 0x6b9514
    bool latency_pending_ = false;         // 0x6b988c
    uint16_t sync_ = 0;                    // 0x5179c0
    int net_sides_ = 0;                    // humans + AI players: the sides the queue runs
    std::array<bool, 8> human_{}, left_{}; // +0x37 / +0x3A
    std::array<uint32_t, 8> posted_seq_{}; // +0x18 (local poster)
    std::array<uint32_t, 8> next_seq_{1, 1, 1, 1, 1, 1, 1, 1}; // +0x1C
    std::array<uint32_t, 8> last_run_seq_{}; // +0x20
    std::array<bool, 8> side_removed_{};   // +0x24250 cleared by FUN_0046e6b0
    bool model_oos_ = false, latency_oos_ = false;
    NetRates net_rates_;
    void run_orders_network();            // FUN_0045d230, network branch
    void run_side_orders(int s);           // FUN_0045d120
    void run_system_orders();              // FUN_0045cda0
    void raise_to_floor();                 // FUN_0045a540
    void mark_leaving(const Order& o);     // +0x3A at once for a leave record
    void with_selection(const Order& o, const std::function<void()>& run);
    void capture_selection(Order& o) const; // the side's selected unit and building ids
    std::vector<PendingOrder> pending_orders_; // list 0, post order
    void run_orders();                         // FUN_0045d230 -> FUN_0045d120(0)
    std::array<std::array<uint8_t, 8>, 8> alliance_{};
    std::vector<formats::MissionCondition> conditions_;
    std::vector<formats::MissionEvent> events_;
    std::array<bool, 8> has_buildings_{}, has_units_{}; // 0x6b87c0 / 0x6b8268
    std::array<uint8_t, 8> unit_mask_{};                // 0x6b8258: what a side can still fight with (docs/re/52 section 5.1)
    bool game_over_ = false;                            // 0x6b63c8
    bool human_alive_ = false;                          // 0x6b7050
    std::array<std::vector<Delivery>, 8> deliveries_{};
    Rng& rng_;
    std::map<std::string, uint32_t> last_voice_;
    int32_t power_shown_ = 0xffff; // local player +0x245b4 (last drawn power bar value)
    bool radar_said_ = false;      // 0x516481
    uint32_t tick_count_ = 0;
};

// 8-way facing (0, 4, .., 28) of the step from (ax, ay) to (bx, by): 0 = north,
// clockwise (FUN_0045e020: FUN_0049e160 rounded).
int facing8(int ax, int ay, int bx, int by);
// FUN_0049e160(dx, dy): 0..0xfff, 0 = north, clockwise; dx = from.x - to.x,
// dy = to.y - from.y.
int angle12(int dx, int dy);
// FUN_0045dfe0: the 32-step facing from (ax, ay) to (bx, by), rounded.
int facing32(int ax, int ay, int bx, int by);
// The flight step tables DX 0x6e9840 / DY 0x704a98 (FUN_0044b090): facing
// 0..31, s in 1/32 px (0..511).
int32_t flight_dx(int facing, int s);
int32_t flight_dy(int facing, int s);

} // namespace d2k::sim
