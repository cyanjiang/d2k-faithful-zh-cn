#include "formats/installshield3.hpp"

#include <array>

#include "formats/byte_reader.hpp"

namespace d2k::formats {

namespace {

constexpr uint32_t kSignature = 0x8C655D13;

// Canonical code from compact lengths: each byte = (repeat - 1) << 4 | length.
// DCL sends code bits inverted, most significant first, in an LSB-first stream.
struct DclCode {
    std::array<uint16_t, 14> count{};
    std::vector<uint16_t> symbol;

    explicit DclCode(std::span<const uint8_t> compact) {
        std::vector<uint8_t> lengths;
        for (uint8_t b : compact) lengths.insert(lengths.end(), size_t((b >> 4) + 1), uint8_t(b & 15));
        for (uint8_t l : lengths) ++count[l];
        count[0] = 0;
        std::array<uint16_t, 15> offset{};
        for (int l = 1; l < 14; ++l) offset[size_t(l + 1)] = uint16_t(offset[size_t(l)] + count[size_t(l)]);
        symbol.assign(lengths.size(), 0);
        for (size_t s = 0; s < lengths.size(); ++s) {
            if (lengths[s]) symbol[offset[lengths[s]]++] = uint16_t(s);
        }
    }
};

class DclBits {
public:
    explicit DclBits(std::span<const uint8_t> in) : in_(in) {}
    uint32_t get(int n) {
        while (count_ < n) {
            if (pos_ >= in_.size()) throw FormatError("DCL stream truncated");
            buf_ |= uint32_t(in_[pos_++]) << count_;
            count_ += 8;
        }
        const uint32_t v = buf_ & ((1u << n) - 1);
        buf_ >>= n;
        count_ -= n;
        return v;
    }
    int decode(const DclCode& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 14; ++len) {
            code |= int(get(1) ^ 1);
            const int n = h.count[size_t(len)];
            if (code - n < first) return h.symbol[size_t(index + (code - first))];
            index += n;
            first = (first + n) << 1;
            code <<= 1;
        }
        throw FormatError("DCL bad Huffman code");
    }

private:
    std::span<const uint8_t> in_;
    size_t pos_ = 0;
    uint32_t buf_ = 0;
    int count_ = 0;
};

constexpr uint8_t kLitLen[] = {
    11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8, 9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24,
    12, 11, 7, 9, 11, 12, 6, 7, 22, 5, 7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12,
    8, 12, 5, 38, 5, 38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27, 44, 253, 253, 253, 252, 252, 252, 13,
    12, 45, 12, 45, 12, 61, 12, 45, 44, 173};
constexpr uint8_t kLenLen[] = {2, 35, 36, 53, 38, 23};
constexpr uint8_t kDistLen[] = {2, 20, 53, 230, 247, 151, 248};
constexpr uint16_t kLenBase[] = {3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264};
constexpr uint8_t kLenExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};

} // namespace

std::vector<uint8_t> dcl_explode(std::span<const uint8_t> in, size_t size_hint) {
    static const DclCode lit(kLitLen), len(kLenLen), dist(kDistLen);
    DclBits s(in);
    const uint32_t coded_lit = s.get(8), dict_bits = s.get(8);
    if (coded_lit > 1 || dict_bits < 4 || dict_bits > 6) throw FormatError("DCL bad header");
    std::vector<uint8_t> out;
    out.reserve(size_hint);
    for (;;) {
        if (s.get(1)) {
            const int sym = s.decode(len);
            const uint32_t length = kLenBase[sym] + s.get(kLenExtra[sym]);
            if (length == 519) break; // end of stream
            const int shift = length == 2 ? 2 : int(dict_bits);
            // Two statements: the code comes before its extra bits in the stream
            // (the operands of + are unsequenced in C++).
            const size_t high = size_t(s.decode(dist)) << shift;
            const size_t d = high + s.get(shift) + 1;
            if (d > out.size()) throw FormatError("DCL distance too far back");
            const size_t from = out.size() - d;
            for (uint32_t i = 0; i < length; ++i) out.push_back(out[from + i]); // may overlap
        } else {
            out.push_back(uint8_t(coded_lit ? s.decode(lit) : int(s.get(8))));
        }
    }
    return out;
}

std::vector<Is3Entry> is3_index(std::span<const uint8_t> archive) {
    ByteReader r(archive);
    if (r.u32() != kSignature) throw FormatError("not an InstallShield 3 archive");
    r.seek(12);
    const uint16_t file_count = r.u16();
    r.seek(18);
    if (r.u32() != archive.size()) throw FormatError("InstallShield 3: size mismatch");
    r.seek(41);
    const uint32_t dir_offset = r.u32();
    r.seek(49);
    const uint16_t dir_count = r.u16();
    struct Dir {
        std::string name;
        uint16_t files;
    };
    std::vector<Dir> dirs;
    size_t p = dir_offset;
    for (uint16_t i = 0; i < dir_count; ++i) {
        r.seek(p);
        const uint16_t files = r.u16(), entry = r.u16(), name_len = r.u16();
        const auto name = r.bytes(name_len);
        dirs.push_back({std::string(name.begin(), name.end()), files});
        p += entry;
    }
    std::vector<Is3Entry> out;
    for (const auto& d : dirs) { // files are grouped by directory, in directory order
        for (uint16_t i = 0; i < d.files; ++i) {
            r.seek(p + 3);
            Is3Entry e;
            e.dir = d.name;
            e.size = r.u32();
            e.compressed_size = r.u32();
            e.offset = r.u32();
            r.seek(p + 23);
            const uint16_t entry = r.u16();
            r.seek(p + 29);
            const uint8_t name_len = r.u8();
            const auto name = r.bytes(name_len);
            e.name.assign(name.begin(), name.end());
            if (uint64_t(e.offset) + e.compressed_size > archive.size()) throw FormatError("InstallShield 3: bad entry");
            out.push_back(std::move(e));
            p += entry;
        }
    }
    if (out.size() != file_count) throw FormatError("InstallShield 3: file count mismatch");
    return out;
}

std::vector<uint8_t> is3_extract(std::span<const uint8_t> archive, const Is3Entry& e) {
    auto out = dcl_explode(archive.subspan(e.offset, e.compressed_size), e.size);
    if (out.size() != e.size) throw FormatError("InstallShield 3: wrong size for " + e.name);
    return out;
}

} // namespace d2k::formats
