#include "formats/sound_rs.hpp"

#include "formats/byte_reader.hpp"

namespace d2k::formats {

std::vector<RsEntry> parse_sound_rs(std::span<const uint8_t> data) {
    ByteReader r(data);
    // u32 directory size, counted after this field.
    size_t dir_end = size_t(r.u32()) + 4;
    std::vector<RsEntry> entries;
    while (r.pos() < dir_end) {
        RsEntry e;
        for (uint8_t c = r.u8(); c != 0; c = r.u8()) e.name.push_back(char(c));
        e.offset = r.u32();
        e.size = r.u32();
        if (size_t(e.offset) + e.size > data.size()) {
            throw FormatError("SOUND.RS: entry " + e.name + " overruns file");
        }
        entries.push_back(std::move(e));
    }
    if (r.pos() != dir_end) throw FormatError("SOUND.RS: directory size mismatch");
    return entries;
}

PcmSound decode_wav(std::span<const uint8_t> data) {
    ByteReader r(data);
    if (r.fixed_string(4) != "RIFF") throw FormatError("WAV: not RIFF");
    r.u32();
    if (r.fixed_string(4) != "WAVE") throw FormatError("WAV: not WAVE");
    PcmSound out;
    uint16_t bits = 0;
    bool have_fmt = false;
    while (r.remaining() >= 8) {
        std::string id = r.fixed_string(4);
        uint32_t size = r.u32();
        auto body = r.bytes(std::min<size_t>(size, r.remaining()));
        if (size & 1 && r.remaining() > 0) r.skip(1);
        if (id == "fmt ") {
            ByteReader f(body);
            uint16_t tag = f.u16();
            uint16_t channels = f.u16();
            out.sample_rate = f.u32();
            f.u32();
            f.u16();
            bits = f.u16();
            if (tag != 1 || channels != 1 || (bits != 8 && bits != 16)) {
                throw FormatError("WAV: only mono 8/16-bit PCM is supported");
            }
            have_fmt = true;
        } else if (id == "data") {
            if (!have_fmt) throw FormatError("WAV: data before fmt");
            if (bits == 8) {
                for (uint8_t v : body) out.samples.push_back(int16_t((int(v) - 128) << 8));
            } else {
                for (size_t i = 0; i + 1 < body.size(); i += 2) {
                    out.samples.push_back(int16_t(body[i] | (body[i + 1] << 8)));
                }
            }
        }
    }
    if (!have_fmt) throw FormatError("WAV: no fmt chunk");
    return out;
}

} // namespace d2k::formats
