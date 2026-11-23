#pragma once
// Westwood VQA, HiColor variant as used by Dune 2000 (all 43 movies:
// 640x400, 15 fps, RGB555 codebooks, 22050 Hz mono IMA ADPCM audio).
// Format: docs/formats/vqa.md

#include <cstdint>
#include <span>
#include <vector>

#include "formats/ima_adpcm.hpp"

namespace d2k::formats {

struct VqaHeader {
    uint16_t version = 0;
    uint16_t flags = 0;
    uint16_t num_frames = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t block_width = 0;
    uint8_t block_height = 0;
    uint8_t frame_rate = 0;
    uint8_t codebook_parts = 0;
    uint16_t colors = 0; // 0 = HiColor (no palette)
    uint16_t max_blocks = 0;
    uint16_t audio_rate = 0; // 0 = no audio
    uint8_t audio_channels = 0;
    uint8_t audio_bits = 0;

    bool has_audio() const { return audio_rate != 0; }
};

struct VqaStats {
    size_t sn2j_checked = 0;
    size_t sn2j_mismatches = 0;
    size_t vptr_leftover_frames = 0; // frames whose pointer stream had unused bytes
    size_t opcode_counts[8] = {};
    size_t codebook_trailing_bytes = 0;
};

class VqaDecoder {
public:
    // Takes ownership of the whole file. Throws FormatError if it is not a
    // HiColor VQA we understand.
    explicit VqaDecoder(std::vector<uint8_t> file);

    const VqaHeader& header() const { return header_; }

    // Decodes up to and including the next video frame. Audio chunks met on
    // the way are decoded and appended to the pending audio. Returns false
    // at end of file.
    bool next_frame();

    // Index of the frame last decoded by next_frame() (0-based).
    int frame_index() const { return frame_index_; }

    // Current frame, width*height RGB555 pixels (xRRRRRGGGGGBBBBB).
    std::span<const uint16_t> frame() const { return frame_; }

    // Returns and clears the audio decoded so far (mono int16 samples).
    std::vector<int16_t> take_audio();

    const VqaStats& stats() const { return stats_; }

private:
    void load_codebook(std::span<const uint8_t> compressed);
    void decode_frame_chunk(std::span<const uint8_t> body);
    void render_vectors(std::span<const uint8_t> vptr);
    void draw_block(size_t block_x, size_t block_y, uint32_t index);

    std::vector<uint8_t> file_;
    size_t pos_ = 0;
    VqaHeader header_;
    std::vector<uint16_t> frame_;
    std::vector<uint16_t> codebook_;
    size_t codebook_entries_ = 0;
    std::vector<uint8_t> scratch_;
    int frame_index_ = -1;
    ImaAdpcmState audio_state_;
    std::vector<int16_t> audio_;
    VqaStats stats_;
};

} // namespace d2k::formats
