#include "formats/map.hpp"

#include <cstring>

#include "formats/byte_reader.hpp"

namespace d2k::formats {

namespace {
constexpr size_t kMissionSize = 68066;
// Field offsets in the .MIS dump, in the order FUN_0044f8e0 reads them.
constexpr size_t kTechLevel = 0;        // 8 bytes
constexpr size_t kCredits = 8;          // 8 x u32; bytes 0x28..0x4F are never read (scan of 0x6b87c8..0x6b8818)
constexpr size_t kHouses = 0x50;        // 8 bytes
constexpr size_t kTileset = 0x50 + 8 + 0xedc0 + 0x40 + 0x1200 + 0x540; // 200 bytes
constexpr size_t kTileAttributes = kTileset + 200;                     // 200 bytes

std::string cstr(std::span<const uint8_t> b) {
    size_t n = 0;
    while (n < b.size() && b[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(b.data()), n);
}
} // namespace

MapFile parse_map(std::span<const uint8_t> data) {
    ByteReader r(data);
    MapFile m;
    m.width = r.u16();
    m.height = r.u16();
    // LoadMapData rejects sizes over 128.
    if (m.width == 0 || m.height == 0 || m.width > 128 || m.height > 128) {
        throw FormatError("MAP: bad size");
    }
    if (r.remaining() != size_t(m.width) * m.height * 4) throw FormatError("MAP: size mismatch");
    m.cells.resize(size_t(m.width) * m.height);
    for (auto& c : m.cells) {
        c.tile = r.u16();
        c.special = r.u16();
    }
    return m;
}

MissionFile parse_mission(std::span<const uint8_t> data) {
    if (data.size() != kMissionSize) throw FormatError("MIS: unexpected size");
    MissionFile mi;
    ByteReader r(data);
    for (auto& t : mi.tech_level) t = r.u8();
    r.seek(kCredits);
    for (auto& c : mi.credits) c = r.u32();
    r.seek(kHouses);
    for (auto& h : mi.house) h = r.u8();
    for (size_t a = 0; a < 8; ++a) {
        for (size_t b = 0; b < 8; ++b) mi.alliance[a][b] = data[0xEE18 + a * 8 + b];
        mi.ai[a].assign(data.begin() + std::ptrdiff_t(0x58 + a * 0x1DB8),
                        data.begin() + std::ptrdiff_t(0x58 + (a + 1) * 0x1DB8));
    }
    auto u32at = [&](size_t o) {
        return uint32_t(data[o] | data[o + 1] << 8 | data[o + 2] << 16 | uint32_t(data[o + 3]) << 24);
    };
    const int n_events = data[0x10728], n_conditions = data[0x10729];
    mi.timer = int32_t(u32at(0x1072a));
    for (int i = 0; i < n_conditions && i < 48; ++i) {
        const size_t o = 0x10058 + size_t(i) * 0x1c;
        MissionCondition c;
        c.a = u32at(o);
        c.b = u32at(o + 4);
        c.c = u32at(o + 8);
        c.x = int32_t(u32at(o + 0xc));
        c.y = int32_t(u32at(o + 0x10));
        const uint32_t rb = u32at(o + 0x14);
        std::memcpy(&c.ratio, &rb, 4);
        c.side = data[o + 0x18];
        c.type = data[o + 0x19];
        c.building = data[o + 0x1a];
        c.unit = data[o + 0x1b];
        mi.conditions.push_back(c);
    }
    for (int i = 0; i < n_events && i < 64; ++i) {
        const size_t o = 0xee58 + size_t(i) * 0x48;
        MissionEvent e;
        e.x = u32at(o);
        e.y = u32at(o + 4);
        e.value = u32at(o + 8);
        e.conditions = data[o + 0xc];
        e.type = data[o + 0xd];
        e.count = data[o + 0xe];
        e.side = data[o + 0xf];
        e.other = data[o + 0x10];
        e.allegiance = data[o + 0x11];
        e.action = data[o + 0x12];
        for (size_t k = 0; k < 14; ++k) {
            e.cond[k] = data[o + 0x13 + k];
            e.negate[k] = data[o + 0x21 + k];
        }
        for (size_t k = 0; k < 20; ++k) e.units[k] = data[o + 0x2f + k];
        e.message = u32at(o + 0x44);
        mi.events.push_back(e);
    }
    mi.tileset = cstr(data.subspan(kTileset, 200));
    mi.tile_attributes = cstr(data.subspan(kTileAttributes, 200));
    return mi;
}

TileAttributes TileAttributes::parse(std::span<const uint8_t> data) {
    if (data.size() < 800 * 4) throw FormatError("TILEATR: too small");
    ByteReader r(data);
    TileAttributes t;
    for (auto& f : t.flags) f = r.u32();
    if (data.size() >= 1600 * 4) {
        for (auto& x : t.text) x = r.u32();
    }
    return t;
}

} // namespace d2k::formats
