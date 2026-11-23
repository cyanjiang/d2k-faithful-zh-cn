#pragma once
// Data\bin\Templates.bin: the game's rules (unit / building templates and
// related tables), a dump of exe globals in the order the data set-up
// (1.06 FUN_00466140) reads them. Data\bin\TILEDATA.BIN maps map special
// values to objects. Format: docs/formats/templates.md

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::formats {

constexpr int kUnitTypes = 60;      // slots; 26 used in 1.06
constexpr int kBuildingTypes = 100; // slots; 24 used

// Decoded fields (docs/re/08-templates.md); raw records stay available.
struct UnitRecord {
    uint8_t house_mask = 0;   // +0x00: A=1 H=2 O=4 E=8 F=16 S=32 M=64 X=128
    uint8_t name_index = 0;   // +0x01: index into the unit names (= type id)
    int32_t hit_points = 0;   // +0x04
    int32_t base_element = -1;   // +0x14: unit graphic element
    int32_t turret_element = -1; // +0x18 (-1 = none)
    bool infantry = false;       // +0x11
    std::array<uint8_t, 32> facing_to_rotation{}; // +0x3c
    uint8_t turn_delay = 0;      // +0x03: ticks between body facing steps
    int32_t speed = 0;           // +0x08: 16.16 pixels per tick
    uint8_t weapon = 0xff;       // +0x0c (0xff = none)
    uint8_t turret_turn_delay = 0; // +0x0f
    bool has_turret = false;     // +0x26
    uint8_t kind = 0;            // +0x2d: 1 harvester, 5 sandworm, ...
    bool crusher = false;        // +0x35
    uint8_t size_class = 0;      // +0x36: selection bracket size 0..5
    uint32_t initial_flags = 0;  // +0x38
    uint8_t speed_class = 0;     // +0xa9: 0 infantry, 1 tracked, 2 wheeled, 3 flying
    uint8_t view = 0;            // +0x10: view distance (shroud reveal radius)
    int death_explosion = -1;    // +0x2f: explosion index (0xff = none)
    // Combat (docs/re/18-combat.md).
    uint8_t armour = 0;          // +0x02
    uint8_t weapon2 = 0xff;      // +0x0d: secondary weapon
    uint8_t reload = 0;          // +0x0e: reload delay in ticks
    uint8_t muzzle_offset = 0;   // +0x37: px along the facing
    int muzzle_flash = -1;       // +0xa8: explosion (0xff none)
    // Unit responses (FUN_0049f240): samples.uib indices, [house * 3 + tick % 3].
    std::array<uint32_t, 9> select_voice{}; // +0x5c
    std::array<uint32_t, 9> ack_voice{};    // +0x80
    uint32_t voice_priority = 0;            // +0xa4
    // Production (docs/re/11-sidebar.md §6-7).
    int32_t cost = 0;            // +0x1c
    int32_t build_rate = 0;      // +0x20: progress per tick, out of 0x5a00
    uint8_t tech = 0;            // +0x24
    uint8_t prereq1_level = 0;   // +0x27: upgrade level of prerequisite 1
    int32_t prereq1 = -1;        // +0x28: building name index (-1 none)
    uint8_t prereq1_mask = 0;    // +0x2c: house mask
    int32_t prereq2 = -1;        // +0x30
    bool skirmish_only = false;  // +0xaa
    bool starportable = false;   // +0x25: offered by the Starport (docs/re/34 section 1)
};

struct BuildingRecord {
    int32_t hit_points = 0;      // +0x00
    uint8_t house_mask = 0;      // +0x04
    int32_t base_element = -1;   // +0x14: building graphic element
    int32_t second_element = -1; // +0x18 (turret top, -1 = none)
    uint16_t footprint = 0;      // +0x54: occupied cells, bit = row*4 + col
    uint8_t height_px = 0;       // +0x86
    uint8_t width_px = 0;        // +0x87
    uint8_t name_index = 0;      // +0x88
    uint8_t kind = 0;            // +0x5c
    uint8_t size_class = 0;      // +0x5f: selection bracket size 0..5
    uint8_t sell_priority = 0;   // +0x5d: the AI's sell order in a power shortage (doc 29 section 2.2)
    // Production and power (docs/re/11-sidebar.md §6-8).
    std::array<uint8_t, 4> tech{};       // +0x10: build, upgrades 1..3
    std::array<int32_t, 4> cost{};       // +0x1c
    std::array<int32_t, 4> build_rate{}; // +0x2c: progress per tick, out of 0x5a00
    int32_t power = 0;           // +0x3c: positive uses, negative produces
    int32_t prereq1 = -1;        // +0x40: building name index (-1 none)
    uint8_t prereq1_mask = 0;    // +0x44
    uint8_t prereq1_level = 0;   // +0x45
    int32_t prereq2 = -1;        // +0x48
    uint8_t prereq2_mask = 0;    // +0x4c
    uint8_t prereq2_level = 0;   // +0x4d
    // Placement, spawn and animation (docs/re/12-placement-and-spawn.md).
    uint8_t view = 0;            // +0x0e: view range
    uint16_t place_mask = 0;     // +0x50: drawn size / placement grid / loses "buildable"
    uint32_t flags = 0;          // +0x58: initial object flags
    int8_t spawn_x = 0, spawn_y = 0; // +0x60/+0x61: factory spawn cell (turret offset for turrets)
    int8_t exit_x = 0, exit_y = 0;   // +0x62/+0x63: first move cell
    uint8_t anim_rate = 0;       // +0x85: ticks per frame (read via the record indexed by anim2)
    uint8_t buildup_base = 0;    // +0x89: build-up frame from which the base image shows
    uint8_t anim1 = 0;           // +0x8a: build-up animation index
    uint8_t anim2 = 0;           // +0x8b: active animation index
    // Combat (docs/re/18-combat.md).
    uint8_t armour = 0;          // +0x05
    uint8_t turn_delay = 0;      // +0x06: turret turn delay
    uint8_t reload = 0;          // +0x07
    uint8_t weapon = 0xff, weapon2 = 0xff; // +0x0c / +0x0d
    bool can_fire = false;       // +0x0f
    bool needs_power = false;    // +0x4e
    int death_explosion = -1;    // +0x4f
    bool shakes = false;         // +0x08 != 0: destruction shakes the screen (FUN_0049ca10)
    int muzzle_flash = -1;       // +0x8c
};

// A bullet type (block 0x782068, 64 x 28 bytes; docs/re/18 section 1.1).
struct BulletRecord {
    int32_t speed = 0;    // +0x00, 16.16 px per tick
    int32_t damage = 0;   // +0x04
    uint32_t flags = 0;   // +0x08
    int32_t sound = -1;   // +0x0c: samples.uib index
    uint8_t image = 0;    // +0x10: own index / image set
    int impact = -1;      // +0x11: explosion (0xff none)
    int trail = -1;       // +0x12
    bool anti_air = false; // +0x14
    uint8_t warhead = 0;  // +0x15: ARMOUR.BIN row
    int32_t range = 0;    // +0x18, px
};

// bin\ARMOUR.BIN (FUN_0046b0f0): warhead rows of damage percent per armour
// type, radius and infantry death animation.
struct ArmourTable {
    struct Warhead {
        std::array<uint8_t, 12> percent{};
        uint32_t radius = 16;
        uint8_t infdeath = 0;
    };
    std::vector<Warhead> warheads; // 14 in 1.06
    int armours = 10;
    static ArmourTable parse(std::span<const uint8_t> data);
};

// Where each graphic element's images sit in DATA.R16 (FUN_004606d0 reads
// it sequentially: 206 fixed entries, unit elements, building elements, ...).
struct ArtIndex {
    struct UnitElement {
        int first = 0, frames = 0, rotations = 0; // entry = first + frame*rotations + rotation
    };
    struct BuildingElement {
        int base = 0, frames = 0; // frames at base+1.., damaged at base+1+frames..
    };
    std::vector<UnitElement> unit;
    std::vector<BuildingElement> building;
    int unit_icons = 0;     // first unit icon (one per unit record, 0x6d9260)
    int building_icons = 0; // first building icon (one per building record, 0x787c78)
    // Building animations 1 (build-up, 0x782aa8) and 2 (active, 0x716b00):
    // 100 each; first DATA.R16 entry and frame count (0x714a98 / 0x7954b0).
    std::array<int, 100> anim1_first{}, anim1_frames{}, anim2_first{}, anim2_frames{};
    // Explosions / effects (0x6d5fd0 frame counts), after the bullets.
    std::array<int, 64> explosion_first{}, explosion_frames{};
    std::array<int, 64> bullet_first{}, bullet_frames{}; // 0x795340 frame counts
};

struct Templates {
    // Raw records; field meaning is being decoded (docs/formats/templates.md).
    std::array<std::array<uint8_t, 0x100>, kUnitTypes> unit{};       // 0x77e250
    std::array<std::array<uint8_t, 0x10c>, kBuildingTypes> building{}; // 0x6dc540
    std::array<std::string, kUnitTypes> unit_name;                   // 0x6da7d8, 50 bytes each
    std::array<std::string, kBuildingTypes> building_name;           // 0x787e08, 50 bytes each
    std::array<std::string, 64> explosion_name;                      // 0x7943a8, 50 bytes each
    std::array<uint32_t, 64> explosion_flags{};                      // 0x6e5750: default draw flags
    std::array<uint8_t, 64> explosion_blink{};                       // 0x781e58 +1: blink mask
    std::array<int32_t, 64> explosion_sound{};                       // 0x781e58 +4: sample, -1 none
    // Every block in file order (exe address, bytes) for the parts not yet
    // broken out above.
    struct Block {
        uint32_t address;
        std::vector<uint8_t> bytes;
    };
    std::vector<Block> blocks;

    std::vector<UnitRecord> unit_records;         // count from 0x795538
    std::vector<BuildingRecord> building_records; // count from 0x79553c
    std::vector<BulletRecord> bullets;            // count from 0x795540
    std::array<std::string, 64> bullet_name;      // 0x6db390, 50 bytes each
    ArtIndex art;

    const Block* block(uint32_t address) const;
    // FUN_0046c570 / FUN_0046c450: the record for a type as seen by a house
    // (0 Atreides, 1 Harkonnen, 2 Ordos, ...): the first record of that
    // name, replaced by any later one whose house mask has the house's bit.
    int unit_record(int type, int house) const;
    int building_record(int type, int house) const;

    static Templates parse(std::span<const uint8_t> data);
    int unit_index(const std::string& name) const;     // -1 if absent
    int building_index(const std::string& name) const; // -1 if absent
    int explosion_index(const std::string& name) const; // -1 if absent
};

// TILEDATA.BIN (FUN_00465e60): 1000 entries indexed by a map special value.
struct MapObject {
    bool building = false;
    uint8_t owner = 0;  // player slot (7 = sandworm / neutral in the campaign)
    uint8_t type = 0;   // index into the unit or building templates
};

struct TileData {
    std::array<int32_t, 1000> entries{};
    static TileData parse(std::span<const uint8_t> data);
    // Empty for -1 entries.
    bool object(uint16_t special, MapObject& out) const;
};

} // namespace d2k::formats
