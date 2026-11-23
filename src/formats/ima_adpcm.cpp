#include "formats/ima_adpcm.hpp"

#include <algorithm>
#include <array>

namespace d2k::formats {

namespace {

// Standard IMA ADPCM tables.
constexpr std::array<int16_t, 89> kStepTable = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

constexpr std::array<int8_t, 8> kIndexAdjust = {-1, -1, -1, -1, 2, 4, 6, 8};

int16_t decode_nibble(uint8_t nibble, ImaAdpcmState& s) {
    int32_t step = kStepTable[size_t(s.step_index)];
    int32_t diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    s.predictor += (nibble & 8) ? -diff : diff;
    s.predictor = std::clamp(s.predictor, -32768, 32767);
    s.step_index = std::clamp(s.step_index + kIndexAdjust[nibble & 7], 0, 88);
    return static_cast<int16_t>(s.predictor);
}

} // namespace

void ima_adpcm_decode(std::span<const uint8_t> src, ImaAdpcmState& state,
                      std::vector<int16_t>& out) {
    out.reserve(out.size() + src.size() * 2);
    for (uint8_t b : src) {
        out.push_back(decode_nibble(b & 0x0F, state));
        out.push_back(decode_nibble(b >> 4, state));
    }
}

void ima_adpcm_decode_samples(std::span<const uint8_t> src, size_t samples,
                              ImaAdpcmState& state, std::vector<int16_t>& out) {
    out.reserve(out.size() + samples);
    for (size_t i = 0; i < samples; ++i) {
        uint8_t b = src[i / 2];
        out.push_back(decode_nibble((i & 1) ? uint8_t(b >> 4) : uint8_t(b & 0x0F), state));
    }
}

} // namespace d2k::formats
