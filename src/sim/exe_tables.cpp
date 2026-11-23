#include "sim/exe_tables.hpp"

#include <cstring>

namespace d2k::sim {

namespace {
template <size_t N>
void read_u16(const formats::PeImage& exe, uint32_t va, std::array<uint16_t, N>& out) {
    auto b = exe.at(va, N * 2);
    for (size_t i = 0; i < N; ++i) out[i] = uint16_t(b[2 * i] | b[2 * i + 1] << 8);
}
} // namespace

ExeTables ExeTables::load(const formats::PeImage& exe) {
    ExeTables t;
    read_u16(exe, 0x4e0c18, t.spice_edge);
    read_u16(exe, 0x4e0b10, t.thick_spice);
    read_u16(exe, 0x4e9520, t.harvested_sand);
    auto shroud = exe.at(0x4e0b18, 256);
    std::memcpy(t.shroud_shape.data(), shroud.data(), 256);
    auto sub = exe.at(0x4c93e8, 5 * 8);
    for (size_t i = 0; i < 5; ++i) {
        for (size_t k = 0; k < 2; ++k) std::memcpy(&t.infantry_subpos[i][k], sub.data() + (i * 2 + k) * 4, 4);
    }
    return t;
}

} // namespace d2k::sim
