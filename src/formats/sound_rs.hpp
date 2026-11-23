#pragma once
// Data\GAMESFX\SOUND.RS: an archive of plain RIFF WAV files, plus a
// minimal PCM WAV reader. Format: docs/formats/audio.md

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::formats {

struct RsEntry {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
};

// Parses the directory; entries reference spans of `data`.
std::vector<RsEntry> parse_sound_rs(std::span<const uint8_t> data);

struct PcmSound {
    uint32_t sample_rate = 0;
    std::vector<int16_t> samples; // mono, converted to 16-bit
};

// Reads an uncompressed mono 8- or 16-bit PCM WAV.
PcmSound decode_wav(std::span<const uint8_t> data);

} // namespace d2k::formats
