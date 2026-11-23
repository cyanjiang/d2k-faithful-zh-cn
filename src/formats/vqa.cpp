#include "formats/vqa.hpp"

#include <algorithm>
#include <cstring>
#include <string>

#include "formats/byte_reader.hpp"
#include "formats/lcw.hpp"

namespace d2k::formats {

namespace {

constexpr uint32_t fourcc(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 |
           uint32_t(uint8_t(s[2])) << 8 | uint32_t(uint8_t(s[3]));
}

// IFF chunk headers: big-endian id and size; bodies are padded to even size.
struct Chunk {
    uint32_t id;
    std::span<const uint8_t> body;
};

uint32_t be32(std::span<const uint8_t> d, size_t at) {
    return uint32_t(d[at]) << 24 | uint32_t(d[at + 1]) << 16 | uint32_t(d[at + 2]) << 8 | d[at + 3];
}

bool read_chunk(std::span<const uint8_t> data, size_t& pos, Chunk& out) {
    if (pos + 8 > data.size()) return false;
    uint32_t id = be32(data, pos);
    uint32_t size = be32(data, pos + 4);
    if (pos + 8 + size > data.size()) throw FormatError("VQA: chunk overruns file");
    out = {id, data.subspan(pos + 8, size)};
    pos += 8 + size + (size & 1);
    return true;
}

// Codebook indices in the pointer stream are at most 13 bits.
constexpr size_t kMaxCodebookEntries = 0x2000;

} // namespace

VqaDecoder::VqaDecoder(std::vector<uint8_t> file) : file_(std::move(file)) {
    std::span<const uint8_t> d(file_);
    if (d.size() < 12 || be32(d, 0) != fourcc("FORM") || be32(d, 8) != fourcc("WVQA")) {
        throw FormatError("VQA: not a FORM/WVQA file");
    }
    pos_ = 12;
    Chunk c{};
    if (!read_chunk(d, pos_, c) || c.id != fourcc("VQHD") || c.body.size() < 42) {
        throw FormatError("VQA: missing VQHD");
    }
    ByteReader r(c.body);
    header_.version = r.u16();
    header_.flags = r.u16();
    header_.num_frames = r.u16();
    header_.width = r.u16();
    header_.height = r.u16();
    header_.block_width = r.u8();
    header_.block_height = r.u8();
    header_.frame_rate = r.u8();
    header_.codebook_parts = r.u8();
    header_.colors = r.u16();
    header_.max_blocks = r.u16();
    // u16 x, u16 y (the draw offset when the player's option 8 is set,
    // 0x4acefc; 0 in every 1.06 movie), u16 largest compressed codebook
    // (the CBFZ buffer size, 0x4ac498).
    r.skip(6);
    header_.audio_rate = r.u16();
    header_.audio_channels = r.u8();
    header_.audio_bits = r.u8();

    if (header_.version != 2 || header_.colors != 0) {
        throw FormatError("VQA: only HiColor version 2 is supported");
    }
    if (header_.block_width != 4 || (header_.block_height != 2 && header_.block_height != 4) ||
        header_.width % header_.block_width || header_.height % header_.block_height) {
        throw FormatError("VQA: unsupported block geometry");
    }
    if (header_.has_audio() && (header_.audio_channels != 1 || header_.audio_bits != 16)) {
        throw FormatError("VQA: only mono 16-bit audio is supported");
    }
    frame_.assign(size_t(header_.width) * header_.height, 0);
    codebook_.assign(kMaxCodebookEntries * header_.block_width * header_.block_height, 0);
    size_t blocks = size_t(header_.width / header_.block_width) * (header_.height / header_.block_height);
    scratch_.resize(std::max(codebook_.size() * 2, blocks * 4));
}

std::vector<int16_t> VqaDecoder::take_audio() {
    std::vector<int16_t> out;
    out.swap(audio_);
    return out;
}

void VqaDecoder::load_codebook(std::span<const uint8_t> compressed) {
    size_t n = lcw_decompress(compressed, scratch_);
    size_t entry_bytes = size_t(header_.block_width) * header_.block_height * 2;
    // Some codebooks decompress to a few bytes more than a whole number of
    // entries (10 of 43 movies). Nothing indexes them: every 1.06 frame
    // decodes with indices below the whole-entry count (draw_block throws
    // otherwise), so they cannot show.
    if (n % entry_bytes != 0) stats_.codebook_trailing_bytes += n % entry_bytes;
    codebook_entries_ = n / entry_bytes;
    for (size_t i = 0; i < n / 2; ++i) {
        codebook_[i] = uint16_t(scratch_[i * 2] | (scratch_[i * 2 + 1] << 8));
    }
}

void VqaDecoder::draw_block(size_t bx, size_t by, uint32_t index) {
    if (index >= codebook_entries_) throw FormatError("VQA: codebook index out of range");
    const size_t bw = header_.block_width, bh = header_.block_height;
    const uint16_t* src = &codebook_[index * bw * bh];
    for (size_t y = 0; y < bh; ++y) {
        uint16_t* dst = &frame_[(by * bh + y) * header_.width + bx * bw];
        std::memcpy(dst, src + y * bw, bw * sizeof(uint16_t));
    }
}

// Vector pointer stream: little-endian u16 commands, one row of blocks at a
// time. Top 3 bits = opcode, low 13 bits = argument. See docs/formats/vqa.md.
void VqaDecoder::render_vectors(std::span<const uint8_t> vptr) {
    ByteReader r(vptr);
    const size_t cols = header_.width / header_.block_width;
    const size_t rows = header_.height / header_.block_height;
    for (size_t by = 0; by < rows; ++by) {
        size_t bx = 0;
        while (bx < cols) {
            uint16_t code = r.u16();
            unsigned op = code >> 13;
            uint32_t arg = code & 0x1FFF;
            stats_.opcode_counts[op]++;
            size_t count = 0;
            uint32_t index = 0;
            switch (op) {
            case 0: // skip: keep previous frame's blocks
                count = arg;
                if (bx + count > cols) throw FormatError("VQA: skip past row end");
                bx += count;
                continue;
            case 1: // run of one block (index < 256)
            case 2: // list of blocks: first index here, the rest one byte each
                index = arg & 0xFF;
                count = ((arg >> 8) & 0x1F) * 2 + 1 + op;
                break;
            case 3:
            case 4: // single block; 4 skips pixels equal to 0x8000 (0x4af0f4).
                    // Not used by D2K (no 1.06 frame has op 4 or 6).
                index = arg;
                count = 1;
                break;
            case 5:
            case 6: // run of one block, count in next byte; 6 = transparent as 4 (0x4aefe6)
                index = arg;
                count = r.u8();
                break;
            default:
                throw FormatError("VQA: unknown vector opcode 7");
            }
            if (bx + count > cols) throw FormatError("VQA: run past row end");
            for (size_t i = 0; i < count; ++i) {
                draw_block(bx++, by, index);
                if (op == 2 && i + 1 < count) index = r.u8();
            }
        }
    }
    if (r.remaining() > 0) stats_.vptr_leftover_frames++;
}

void VqaDecoder::decode_frame_chunk(std::span<const uint8_t> body) {
    size_t pos = 0;
    Chunk c{};
    while (read_chunk(body, pos, c)) {
        if (c.id == fourcc("CBFZ")) {
            load_codebook(c.body);
        } else if (c.id == fourcc("CBF0")) {
            throw FormatError("VQA: uncompressed codebook not supported (unused by D2K)");
        } else if (c.id == fourcc("VPRZ")) {
            size_t n = lcw_decompress(c.body, scratch_);
            render_vectors(std::span<const uint8_t>(scratch_).first(n));
        } else if (c.id == fourcc("VPTR")) {
            render_vectors(c.body);
        }
    }
}

bool VqaDecoder::next_frame() {
    std::span<const uint8_t> d(file_);
    Chunk c{};
    while (read_chunk(d, pos_, c)) {
        if (c.id == fourcc("SN2J") && c.body.size() >= 6) {
            // Saved decoder state at the start of the next SND2 chunk
            // (lets the original seek). We use it to check our decoder.
            ByteReader r(c.body);
            // The decoder's state word (0x4ab063 -> 0x4af6c9): step index
            // << 5, the low bits take the nibble during decoding.
            int32_t index = r.u16() >> 5;
            int32_t predictor = r.i32();
            stats_.sn2j_checked++;
            if (index != audio_state_.step_index || predictor != audio_state_.predictor) {
                stats_.sn2j_mismatches++;
            }
        } else if (c.id == fourcc("SND2")) {
            ima_adpcm_decode(c.body, audio_state_, audio_);
        } else if (c.id == fourcc("VQFL")) {
            decode_frame_chunk(c.body); // codebook-only update
        } else if (c.id == fourcc("VQFR")) {
            decode_frame_chunk(c.body);
            frame_index_++;
            return true;
        }
        // LINF/CINF/FINF: loop, codebook and frame-offset tables (not needed
        // for straight playback).
    }
    return false;
}

} // namespace d2k::formats
