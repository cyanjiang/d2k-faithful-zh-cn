#include "formats/lcw.hpp"

#include "formats/byte_reader.hpp"

namespace d2k::formats {

namespace {

class Output {
public:
    explicit Output(std::span<uint8_t> dst) : dst_(dst) {}

    size_t pos() const { return pos_; }

    void put(uint8_t v) {
        if (pos_ >= dst_.size()) throw FormatError("LCW: output overflow");
        dst_[pos_++] = v;
    }

    // Byte-by-byte copy from earlier output; overlapping runs are intended.
    void copy_from(size_t from, size_t count) {
        if (from >= pos_) throw FormatError("LCW: back-reference beyond output");
        for (size_t i = 0; i < count; ++i) put(dst_[from + i]);
    }

private:
    std::span<uint8_t> dst_;
    size_t pos_ = 0;
};

} // namespace

size_t lcw_decompress(std::span<const uint8_t> src, std::span<uint8_t> dst) {
    ByteReader in(src);
    Output out(dst);
    // A leading 0 byte selects "relative" mode, where the long copy commands
    // address output relative to the current position. HiColor VQA uses it.
    bool relative = false;
    if (in.remaining() > 0 && src[0] == 0) {
        relative = true;
        in.skip(1);
    }
    auto absolute = [&](size_t offset) -> size_t {
        if (!relative) return offset;
        if (offset > out.pos()) throw FormatError("LCW: relative offset out of range");
        return out.pos() - offset;
    };

    while (in.remaining() > 0) {
        uint8_t cmd = in.u8();
        if ((cmd & 0x80) == 0) {
            // 0CCCPPPP PPPPPPPP: copy C+3 bytes from P bytes back.
            size_t count = ((cmd & 0x70) >> 4) + 3;
            size_t back = (size_t(cmd & 0x0F) << 8) | in.u8();
            if (back == 0 || back > out.pos()) throw FormatError("LCW: bad short back-reference");
            out.copy_from(out.pos() - back, count);
        } else if ((cmd & 0x40) == 0) {
            // 10CCCCCC: C literal bytes; C == 0 ends the stream.
            size_t count = cmd & 0x3F;
            if (count == 0) break;
            for (uint8_t v : in.bytes(count)) out.put(v);
        } else if (cmd == 0xFE) {
            size_t count = in.u16();
            uint8_t v = in.u8();
            for (size_t i = 0; i < count; ++i) out.put(v);
        } else if (cmd == 0xFF) {
            size_t count = in.u16();
            out.copy_from(absolute(in.u16()), count);
        } else {
            // 11CCCCCC PPPP: copy C+3 bytes from position P.
            size_t count = (cmd & 0x3F) + 3;
            out.copy_from(absolute(in.u16()), count);
        }
    }
    return out.pos();
}

} // namespace d2k::formats
