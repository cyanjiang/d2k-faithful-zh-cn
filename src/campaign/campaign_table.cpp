#include "campaign/campaign_table.hpp"

#include <cstring>

#include "formats/byte_reader.hpp"

namespace d2k::campaign {

namespace {
constexpr size_t kRecordSize = 0x104;

std::string cstr(std::span<const uint8_t> b) {
    size_t n = 0;
    while (n < b.size() && b[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(b.data()), n);
}

int32_t i32(std::span<const uint8_t> r, size_t off) {
    int32_t v;
    std::memcpy(&v, r.data() + off, 4);
    return v;
}
} // namespace

CampaignTable CampaignTable::parse(std::span<const uint8_t> data) {
    if (data.size() != kRecordSize * kHouses * kRecords) {
        throw formats::FormatError("campaign.uib: unexpected size");
    }
    CampaignTable t;
    for (int h = 0; h < kHouses; ++h) {
        for (int m = 0; m < kRecords; ++m) {
            auto r = data.subspan(size_t(h * kRecords + m) * kRecordSize, kRecordSize);
            auto& c = t.records[size_t(h)][size_t(m)];
            c.territory = {r[0x00], r[0x01]};
            c.mission = {cstr(r.subspan(0x02, 5)), cstr(r.subspan(0x07, 5))};
            c.arrow_direction = {i32(r, 0x0c), i32(r, 0x10)};
            c.arrow_x = {i32(r, 0x14), i32(r, 0x1c)};
            c.arrow_y = {i32(r, 0x18), i32(r, 0x20)};
            for (int k = 0; k < kHouses; ++k) {
                c.gained[size_t(k)] = {r[0x24 + 3 * k], r[0x25 + 3 * k], r[0x26 + 3 * k]};
            }
            std::memcpy(&c.value, r.data() + 0x30, 4);
            c.reveal_house = {i32(r, 0x34), i32(r, 0x38), i32(r, 0x3c)};
            c.reveal_speech = {cstr(r.subspan(0x40, 0x3c)), cstr(r.subspan(0x7c, 0x3c)),
                               cstr(r.subspan(0xb8, 0x3c))};
            c.track = cstr(r.subspan(0xf4, 0x0c));
            c.track_house = i32(r, 0x100);
        }
    }
    return t;
}

} // namespace d2k::campaign
