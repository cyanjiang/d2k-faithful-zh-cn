#pragma once
// Westwood AUD (MUSIC\*.AUD, Data\GAMESFX\*.AUD). All 268 files in 1.06 are
// 22050 Hz 16-bit mono, compression 99 (IMA ADPCM). Format: docs/formats/audio.md

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "formats/ima_adpcm.hpp"

namespace d2k::formats {

struct AudHeader {
    uint16_t sample_rate = 0;
    uint32_t compressed_size = 0;   // bytes after the 12-byte header
    uint32_t uncompressed_size = 0; // bytes of 16-bit PCM
    uint8_t flags = 0;              // bit 0 stereo, bit 1 16-bit
    uint8_t compression = 0;        // 99 = IMA ADPCM
};

struct AudSound {
    AudHeader header;
    std::vector<int16_t> samples; // mono
};

AudSound decode_aud(std::span<const uint8_t> data);

// Streams an AUD the way the 1.06 music player does (docs/re/04-menu-flow.md
// #menu-music): whole chunks into fixed-size buffers, looping from the first
// chunk without resetting the decoder state.
class AudStream {
public:
    explicit AudStream(std::vector<uint8_t> file);

    const AudHeader& header() const { return header_; }

    // FUN_0046fcc0: zero-fills `out`, then appends whole chunks until it is
    // exactly full or the file ends. Returns the number of samples read.
    size_t fill(std::span<int16_t> out);

    // FUN_0046ff30: as fill(), but when nothing is left, seeks back to the
    // first chunk (decoder state kept) and fills again.
    size_t fill_looping(std::span<int16_t> out, bool* wrapped = nullptr);

private:
    std::vector<uint8_t> data_;
    AudHeader header_;
    size_t pos_ = 12;
    ImaAdpcmState state_;
    // The exe reads each chunk into one persistent 0x2000-byte buffer; a
    // final chunk that declares one sample more than it holds decodes that
    // sample from a stale byte left by the previous chunk.
    std::array<uint8_t, 0x2000> chunk_{};
    std::vector<int16_t> scratch_;
};

} // namespace d2k::formats
