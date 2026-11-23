#include "campaign/regions.hpp"

#include <cstring>

#include "formats/byte_reader.hpp"

namespace d2k::campaign {

namespace {
constexpr uint32_t kRegionTable = 0x4be3f4; // 1.06
constexpr size_t kRecordSize = 0x48;
constexpr size_t kMaxPoints = 7;
} // namespace

bool Region::contains(int px, int py) const {
    // The even-odd test at the integer point itself: equal to GDI's
    // PtInRegion on a CreatePolygonRgn(ALTERNATE) region for every pixel
    // around all 27 territories (checked against gdi32 on Windows 11).
    double cx = px, cy = py;
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto& a = polygon[i];
        const auto& b = polygon[j];
        if ((a.y > cy) != (b.y > cy) && cx < (b.x - a.x) * (cy - a.y) / double(b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

std::array<Region, 27> load_regions(const formats::PeImage& exe) {
    std::array<Region, 27> out;
    auto table = exe.at(kRegionTable, kRecordSize * out.size());
    for (size_t i = 0; i < out.size(); ++i) {
        formats::ByteReader r(table.subspan(i * kRecordSize, kRecordSize));
        Region& g = out[i];
        g.x = int32_t(r.u32());
        g.y = int32_t(r.u32());
        uint32_t n = r.u32() & 0xff; // CreateGlobe reads the count as a byte
        if (n == 0 || n > kMaxPoints) throw formats::FormatError("region table: bad point count");
        int sx = 0, sy = 0;
        for (uint32_t k = 0; k < n; ++k) {
            MapPoint p{int32_t(r.u32()), int32_t(r.u32())};
            sx += p.x;
            sy += p.y;
            g.polygon.push_back(p);
        }
        g.label = {sx / int(n) - 16, sy / int(n) - 32};
    }
    return out;
}

} // namespace d2k::campaign
