#pragma once
// Westwood IMA ADPCM (VQA SND2 chunks; also AUD files with compression 99).
// 4 bits per sample, low nibble first. Format: docs/formats/vqa.md#audio

#include <cstdint>
#include <span>
#include <vector>

namespace d2k::formats {

struct ImaAdpcmState {
    int32_t predictor = 0;
    int32_t step_index = 0;
};

// Decodes all nibbles of `src`, appending 16-bit samples to `out` and
// updating `state` so consecutive chunks decode as one stream.
void ima_adpcm_decode(std::span<const uint8_t> src, ImaAdpcmState& state,
                      std::vector<int16_t>& out);

// Decodes exactly `samples` nibbles (low nibble first) from `src`, which
// must hold at least (samples + 1) / 2 bytes.
void ima_adpcm_decode_samples(std::span<const uint8_t> src, size_t samples,
                              ImaAdpcmState& state, std::vector<int16_t>& out);

} // namespace d2k::formats
