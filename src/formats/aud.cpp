#include "formats/aud.hpp"

#include <algorithm>

#include "formats/byte_reader.hpp"
#include "formats/ima_adpcm.hpp"

namespace d2k::formats {

namespace {
constexpr uint32_t kChunkMagic = 0x0000DEAF;
constexpr uint8_t kCompressionImaAdpcm = 99;
} // namespace

namespace {
AudHeader read_header(ByteReader& r) {
    AudHeader h;
    h.sample_rate = r.u16();
    h.compressed_size = r.u32();
    h.uncompressed_size = r.u32();
    h.flags = r.u8();
    h.compression = r.u8();
    return h;
}
} // namespace

AudSound decode_aud(std::span<const uint8_t> data) {
    ByteReader r(data);
    AudSound out;
    auto& h = out.header;
    h = read_header(r);
    if (h.compression != kCompressionImaAdpcm || h.flags != 2) {
        throw FormatError("AUD: only mono 16-bit IMA ADPCM (type 99) is supported");
    }
    if (size_t(h.compressed_size) + 12 != data.size()) {
        throw FormatError("AUD: size field does not match file size");
    }
    // Chunks: u16 compressed size, u16 output bytes, u32 0xDEAF, data.
    // Decoder state carries across chunks (verified: no discontinuities).
    ImaAdpcmState state;
    out.samples.reserve(h.uncompressed_size / 2);
    while (r.remaining() > 0) {
        uint16_t csize = r.u16();
        uint16_t usize = r.u16();
        if (r.u32() != kChunkMagic) throw FormatError("AUD: bad chunk magic");
        size_t before = out.samples.size();
        ima_adpcm_decode(r.bytes(csize), state, out.samples);
        // In 173 of 268 files the final chunk declares exactly one sample
        // more than its nibbles hold (an encoder quirk). The exe's decoder
        // (FUN_004048c0) decodes out/2 samples, so the extra one comes from
        // the low nibble of the byte after the chunk data: for sounds played
        // from the 64 KB sample buffer (FUN_0046f9a0) whatever an earlier
        // longer file left there. One sample (45 us) at the end; we repeat
        // the last sample. (The music stream, AudStream, reproduces it.)
        size_t want = usize / 2;
        size_t got = out.samples.size() - before;
        bool last_chunk = r.remaining() == 0;
        if (got + 1 == want && last_chunk && got > 0) {
            out.samples.push_back(out.samples.back());
        } else if (got != want) {
            throw FormatError("AUD: chunk sample count mismatch");
        }
    }
    if (out.samples.size() * 2 != h.uncompressed_size) {
        throw FormatError("AUD: total output size mismatch");
    }
    return out;
}

AudStream::AudStream(std::vector<uint8_t> file) : data_(std::move(file)) {
    ByteReader r(data_);
    header_ = read_header(r);
    if (header_.compression != kCompressionImaAdpcm || header_.flags != 2) {
        throw FormatError("AUD: only mono 16-bit IMA ADPCM (type 99) is supported");
    }
}

size_t AudStream::fill(std::span<int16_t> out) {
    std::fill(out.begin(), out.end(), int16_t(0));
    size_t filled = 0;
    while (filled < out.size()) {
        if (data_.size() - pos_ < 8) break; // fread of the chunk header fails
        std::copy_n(data_.begin() + std::ptrdiff_t(pos_), 8, chunk_.begin());
        pos_ += 8;
        uint16_t csize = uint16_t(chunk_[0] | chunk_[1] << 8);
        uint16_t usize = uint16_t(chunk_[2] | chunk_[3] << 8);
        uint32_t magic = uint32_t(chunk_[4] | chunk_[5] << 8 | chunk_[6] << 16 | chunk_[7] << 24);
        if (magic != kChunkMagic) break;
        size_t n = std::min<size_t>({csize, data_.size() - pos_, chunk_.size()});
        std::copy_n(data_.begin() + std::ptrdiff_t(pos_), n, chunk_.begin());
        pos_ += n;
        size_t samples = usize / 2;
        // The exe trusts the stream; a chunk past the buffer end would
        // overrun it. Ours stops at the end instead.
        samples = std::min(samples, out.size() - filled);
        if (csize == usize) {
            for (size_t i = 0; i < samples; ++i) {
                out[filled + i] = int16_t(chunk_[2 * i] | chunk_[2 * i + 1] << 8);
            }
        } else {
            scratch_.clear();
            ima_adpcm_decode_samples(chunk_, samples, state_, scratch_);
            std::copy(scratch_.begin(), scratch_.end(), out.begin() + std::ptrdiff_t(filled));
        }
        filled += samples;
    }
    return filled;
}

size_t AudStream::fill_looping(std::span<int16_t> out, bool* wrapped) {
    size_t n = fill(out);
    if (n == 0) {
        pos_ = 12;
        n = fill(out);
        if (wrapped) *wrapped = true;
    }
    return n;
}

} // namespace d2k::formats
