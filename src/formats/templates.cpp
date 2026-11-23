#include "formats/templates.hpp"

#include <algorithm>
#include <cstring>

#include "formats/byte_reader.hpp"

namespace d2k::formats {

namespace {
// The blocks of Templates.bin in file order: exe address and size, as read
// by FUN_00466140 (writer at 0x4691e4 mirrors it).
struct BlockDef {
    uint32_t address, size;
};
constexpr BlockDef kBlocks[] = {
    {0x77e250, 0x3c00}, {0x6dc540, 0x68b0}, {0x782068, 0x700},  {0x781e58, 0x200},
    {0x782770, 0x168},  {0x7951d0, 0x168},  {0x795528, 4},      {0x795538, 1},
    {0x795534, 4},      {0x6d5fd0, 0x100},  {0x795544, 1},      {0x795530, 4},
    {0x795340, 0x100},  {0x795540, 1},      {0x79552c, 4},      {0x6e5a60, 0x1e0},
    {0x79553c, 1},      {0x6f9908, 0xafc8}, {0x6db390, 0xc80},  {0x7943a8, 0xc80},
    {0x7891b0, 0x6978}, {0x6da7d8, 0xbb8},  {0x787e08, 0x1388}, {0x6d60d8, 0x56},
    {0x6e5750, 0x100},  {0x7954b0, 0x64},   {0x714a98, 0x64},   {0x6e5850, 1},
    {0x6d9b50, 1},
};
constexpr size_t kTotal = 133140;

int32_t rd32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

std::string cstr(const uint8_t* p, size_t n) {
    size_t len = 0;
    while (len < n && p[len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}
} // namespace

Templates Templates::parse(std::span<const uint8_t> data) {
    if (data.size() != kTotal) throw FormatError("Templates.bin: unexpected size");
    Templates t;
    size_t off = 0;
    for (const auto& b : kBlocks) {
        Block blk{b.address, std::vector<uint8_t>(data.begin() + std::ptrdiff_t(off),
                                                  data.begin() + std::ptrdiff_t(off + b.size))};
        const uint8_t* p = blk.bytes.data();
        switch (b.address) {
        case 0x77e250:
            for (int i = 0; i < kUnitTypes; ++i) std::memcpy(t.unit[size_t(i)].data(), p + i * 0x100, 0x100);
            break;
        case 0x6dc540:
            for (int i = 0; i < kBuildingTypes; ++i) {
                std::memcpy(t.building[size_t(i)].data(), p + i * 0x10c, 0x10c);
            }
            break;
        case 0x6da7d8:
            for (int i = 0; i < kUnitTypes; ++i) t.unit_name[size_t(i)] = cstr(p + i * 50, 50);
            break;
        case 0x787e08:
            for (int i = 0; i < kBuildingTypes; ++i) t.building_name[size_t(i)] = cstr(p + i * 50, 50);
            break;
        case 0x6db390:
            for (int i = 0; i < 64; ++i) t.bullet_name[size_t(i)] = cstr(p + i * 50, 50);
            break;
        case 0x7943a8:
            for (int i = 0; i < 64; ++i) t.explosion_name[size_t(i)] = cstr(p + i * 50, 50);
            break;
        case 0x6e5750:
            for (int i = 0; i < 64; ++i) t.explosion_flags[size_t(i)] = uint32_t(rd32(p + 4 * i));
            break;
        case 0x781e58:
            for (int i = 0; i < 64; ++i) {
                t.explosion_blink[size_t(i)] = p[i * 8 + 1];
                t.explosion_sound[size_t(i)] = rd32(p + i * 8 + 4);
            }
            break;
        default:
            break;
        }
        t.blocks.push_back(std::move(blk));
        off += b.size;
    }

    // Records.
    const uint8_t n_units = t.block(0x795538)->bytes[0];
    const uint8_t n_buildings = t.block(0x79553c)->bytes[0];
    for (int i = 0; i < n_units && i < kUnitTypes; ++i) {
        const uint8_t* r = t.unit[size_t(i)].data();
        UnitRecord u;
        u.house_mask = r[0x00];
        u.name_index = r[0x01];
        u.hit_points = rd32(r + 0x04);
        u.infantry = r[0x11] != 0;
        u.base_element = rd32(r + 0x14);
        u.turret_element = rd32(r + 0x18);
        std::memcpy(u.facing_to_rotation.data(), r + 0x3c, 32);
        u.turn_delay = r[0x03];
        u.speed = rd32(r + 0x08);
        u.weapon = r[0x0c];
        u.turret_turn_delay = r[0x0f];
        u.has_turret = r[0x26] != 0;
        u.kind = r[0x2d];
        u.crusher = r[0x35] != 0;
        u.size_class = r[0x36];
        u.initial_flags = uint32_t(rd32(r + 0x38));
        u.speed_class = r[0xa9];
        u.cost = rd32(r + 0x1c);
        u.build_rate = rd32(r + 0x20);
        u.tech = r[0x24];
        u.prereq1_level = r[0x27];
        u.prereq1 = rd32(r + 0x28);
        u.prereq1_mask = r[0x2c];
        u.prereq2 = rd32(r + 0x30);
        u.skirmish_only = r[0xaa] != 0;
        u.starportable = r[0x25] != 0;
        u.view = r[0x10];
        u.armour = r[0x02];
        u.weapon2 = r[0x0d];
        u.reload = r[0x0e];
        u.muzzle_offset = r[0x37];
        u.muzzle_flash = r[0xa8] == 0xff ? -1 : int(r[0xa8]);
        u.death_explosion = r[0x2f] == 0xff ? -1 : int(r[0x2f]);
        for (size_t k = 0; k < 9; ++k) {
            u.select_voice[k] = uint32_t(rd32(r + 0x5c + 4 * k));
            u.ack_voice[k] = uint32_t(rd32(r + 0x80 + 4 * k));
        }
        u.voice_priority = uint32_t(rd32(r + 0xa4));
        t.unit_records.push_back(u);
    }
    for (int i = 0; i < n_buildings && i < kBuildingTypes; ++i) {
        const uint8_t* r = t.building[size_t(i)].data();
        BuildingRecord b;
        b.hit_points = rd32(r + 0x00);
        b.house_mask = r[0x04];
        b.base_element = rd32(r + 0x14);
        b.second_element = rd32(r + 0x18);
        b.footprint = uint16_t(rd32(r + 0x54));
        b.height_px = r[0x86];
        b.width_px = r[0x87];
        b.name_index = r[0x88];
        b.kind = r[0x5c];
        b.size_class = r[0x5f];
        b.sell_priority = r[0x5d];
        for (int k = 0; k < 4; ++k) {
            b.tech[size_t(k)] = r[0x10 + k];
            b.cost[size_t(k)] = rd32(r + 0x1c + 4 * k);
            b.build_rate[size_t(k)] = rd32(r + 0x2c + 4 * k);
        }
        b.power = rd32(r + 0x3c);
        b.prereq1 = rd32(r + 0x40);
        b.prereq1_mask = r[0x44];
        b.prereq1_level = r[0x45];
        b.prereq2 = rd32(r + 0x48);
        b.prereq2_mask = r[0x4c];
        b.prereq2_level = r[0x4d];
        b.view = r[0x0e];
        b.place_mask = uint16_t(rd32(r + 0x50));
        b.flags = uint32_t(rd32(r + 0x58));
        b.spawn_x = int8_t(r[0x60]);
        b.spawn_y = int8_t(r[0x61]);
        b.exit_x = int8_t(r[0x62]);
        b.exit_y = int8_t(r[0x63]);
        b.anim_rate = r[0x85];
        b.buildup_base = r[0x89];
        b.anim1 = r[0x8a];
        b.anim2 = r[0x8b];
        b.armour = r[0x05];
        b.turn_delay = r[0x06];
        b.reload = r[0x07];
        b.weapon = r[0x0c];
        b.weapon2 = r[0x0d];
        b.can_fire = r[0x0f] != 0;
        b.needs_power = r[0x4e] != 0;
        b.death_explosion = r[0x4f] == 0xff ? -1 : int(r[0x4f]);
        b.shakes = rd32(r + 0x08) != 0;
        b.muzzle_flash = r[0x8c] == 0xff ? -1 : int(r[0x8c]);
        t.building_records.push_back(b);
    }

    // DATA.R16 layout.
    int entry = 9 + 6 + 2 + 10 + 11 + 64 + 8 + 4 + 64 + 16 + 12; // 206 fixed entries
    const auto& ue_frames = t.block(0x782770)->bytes;
    const auto& ue_rots = t.block(0x7951d0)->bytes;
    const int n_ue = rd32(t.block(0x795528)->bytes.data());
    for (int e = 0; e < n_ue; ++e) {
        ArtIndex::UnitElement u{entry, rd32(ue_frames.data() + 4 * e), rd32(ue_rots.data() + 4 * e)};
        entry += u.frames * u.rotations;
        t.art.unit.push_back(u);
    }
    const auto& be_frames = t.block(0x6e5a60)->bytes;
    const int n_be = rd32(t.block(0x79552c)->bytes.data());
    for (int e = 0; e < n_be; ++e) {
        ArtIndex::BuildingElement b{entry, rd32(be_frames.data() + 4 * e)};
        entry += 1 + 2 * b.frames;
        t.art.building.push_back(b);
    }
    // Bullets, explosions, then the icons (FUN_004606d0).
    const int n_bullets = t.block(0x795540)->bytes[0];
    for (int i = 0; i < n_bullets && i < 64; ++i) {
        t.art.bullet_first[size_t(i)] = entry;
        t.art.bullet_frames[size_t(i)] = rd32(t.block(0x795340)->bytes.data() + 4 * i);
        entry += t.art.bullet_frames[size_t(i)];
        const uint8_t* b = t.block(0x782068)->bytes.data() + 28 * i;
        BulletRecord br;
        br.speed = rd32(b + 0x00);
        br.damage = rd32(b + 0x04);
        br.flags = uint32_t(rd32(b + 0x08));
        br.sound = rd32(b + 0x0c);
        br.image = b[0x10];
        br.impact = b[0x11] == 0xff ? -1 : int(b[0x11]);
        br.trail = b[0x12] == 0xff ? -1 : int(b[0x12]);
        br.anti_air = b[0x14] != 0;
        br.warhead = b[0x15];
        br.range = rd32(b + 0x18);
        t.bullets.push_back(br);
    }
    const int n_explosions = rd32(t.block(0x795534)->bytes.data());
    for (int i = 0; i < n_explosions && i < 64; ++i) {
        t.art.explosion_first[size_t(i)] = entry;
        t.art.explosion_frames[size_t(i)] = rd32(t.block(0x6d5fd0)->bytes.data() + 4 * i);
        entry += t.art.explosion_frames[size_t(i)];
    }
    t.art.unit_icons = entry;
    entry += n_units + 10; // unit icons, then 10 unit progress buffers (0x6e5858)
    t.art.building_icons = entry;
    entry += n_buildings + 1; // building icons, then the progress buffer (0x789194)
    const auto& a1 = t.block(0x714a98)->bytes;
    const auto& a2 = t.block(0x7954b0)->bytes;
    for (size_t i = 0; i < 100; ++i) {
        t.art.anim1_first[i] = entry;
        t.art.anim1_frames[i] = a1[i];
        entry += a1[i];
    }
    for (size_t i = 0; i < 100; ++i) {
        t.art.anim2_first[i] = entry;
        t.art.anim2_frames[i] = a2[i];
        entry += a2[i];
    }
    return t;
}

const Templates::Block* Templates::block(uint32_t address) const {
    for (const auto& b : blocks) {
        if (b.address == address) return &b;
    }
    return nullptr;
}

int Templates::unit_record(int type, int house) const {
    int found = -1;
    for (size_t i = 0; i < unit_records.size(); ++i) {
        if (unit_records[i].name_index != type) continue;
        if (found < 0 || (unit_records[i].house_mask & (1 << house))) found = int(i);
    }
    return found;
}

int Templates::building_record(int type, int house) const {
    int found = -1;
    for (size_t i = 0; i < building_records.size(); ++i) {
        if (building_records[i].name_index != type) continue;
        if (found < 0 || (building_records[i].house_mask & (1 << house))) found = int(i);
    }
    return found;
}

int Templates::explosion_index(const std::string& name) const {
    auto it = std::find(explosion_name.begin(), explosion_name.end(), name);
    return it == explosion_name.end() || name.empty() ? -1 : int(it - explosion_name.begin());
}

int Templates::unit_index(const std::string& name) const {
    auto it = std::find(unit_name.begin(), unit_name.end(), name);
    return it == unit_name.end() || name.empty() ? -1 : int(it - unit_name.begin());
}

int Templates::building_index(const std::string& name) const {
    auto it = std::find(building_name.begin(), building_name.end(), name);
    return it == building_name.end() || name.empty() ? -1 : int(it - building_name.begin());
}

ArmourTable ArmourTable::parse(std::span<const uint8_t> data) {
    // 30 x 20-byte warhead rows, names, armour names, then the counts.
    if (data.size() < 2702) throw FormatError("ARMOUR.BIN: too small");
    ArmourTable t;
    const int n = data[2700];
    t.armours = data[2701];
    for (int i = 0; i < n && i < 30; ++i) {
        const uint8_t* r = data.data() + 20 * i;
        Warhead w;
        for (size_t k = 0; k < 12; ++k) w.percent[k] = r[k];
        w.radius = uint32_t(r[0x0c] | r[0x0d] << 8 | r[0x0e] << 16 | uint32_t(r[0x0f]) << 24);
        w.infdeath = r[0x10];
        t.warheads.push_back(w);
    }
    return t;
}

TileData TileData::parse(std::span<const uint8_t> data) {
    if (data.size() != 4000) throw FormatError("TILEDATA.BIN: unexpected size");
    TileData t;
    ByteReader r(data);
    for (auto& e : t.entries) e = int32_t(r.u32());
    return t;
}

bool TileData::object(uint16_t special, MapObject& out) const {
    if (special >= entries.size() || entries[special] == -1) return false;
    uint32_t v = uint32_t(entries[special]);
    out.building = (v & 0x80000000u) != 0;
    out.owner = uint8_t(v >> 16);
    out.type = uint8_t(v);
    return true;
}

} // namespace d2k::formats
