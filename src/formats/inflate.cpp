#include "formats/inflate.hpp"

#include <array>

#include "formats/byte_reader.hpp"

namespace d2k::formats {

namespace {

class Bits {
public:
    explicit Bits(std::span<const uint8_t> in) : in_(in) {}

    uint32_t get(int n) {
        while (count_ < n) {
            if (pos_ >= in_.size()) throw FormatError("inflate: stream truncated");
            buf_ |= uint32_t(in_[pos_++]) << count_;
            count_ += 8;
        }
        const uint32_t v = buf_ & ((1u << n) - 1);
        buf_ >>= n;
        count_ -= n;
        return v;
    }
    void align() {
        buf_ = 0;
        count_ = 0;
    }
    uint8_t byte() {
        if (pos_ >= in_.size()) throw FormatError("inflate: stream truncated");
        return in_[pos_++];
    }

private:
    std::span<const uint8_t> in_;
    size_t pos_ = 0;
    uint32_t buf_ = 0;
    int count_ = 0;
};

// A canonical Huffman code: the number of codes per length and the symbols
// in code order.
struct Huffman {
    std::array<uint16_t, 16> count{};
    std::vector<uint16_t> symbol;

    void build(std::span<const uint8_t> lengths) {
        count.fill(0);
        for (uint8_t l : lengths) ++count[l];
        count[0] = 0;
        std::array<uint16_t, 16> offset{};
        for (int l = 1; l < 15; ++l) offset[size_t(l + 1)] = uint16_t(offset[size_t(l)] + count[size_t(l)]);
        symbol.assign(lengths.size(), 0);
        for (size_t s = 0; s < lengths.size(); ++s) {
            if (lengths[s]) symbol[offset[lengths[s]]++] = uint16_t(s);
        }
    }

    int decode(Bits& b) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= int(b.get(1));
            const int n = count[size_t(len)];
            if (code - n < first) return symbol[size_t(index + (code - first))];
            index += n;
            first = (first + n) << 1;
            code <<= 1;
        }
        throw FormatError("inflate: bad Huffman code");
    }
};

constexpr std::array<uint16_t, 29> kLenBase{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                            31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> kLenExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                            2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> kDistBase{1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                             33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                             1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> kDistExtra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                             6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void codes(Bits& b, std::vector<uint8_t>& out, const Huffman& lit, const Huffman& dist) {
    for (;;) {
        const int sym = lit.decode(b);
        if (sym < 256) {
            out.push_back(uint8_t(sym));
            continue;
        }
        if (sym == 256) return;
        const int li = sym - 257;
        if (li >= 29) throw FormatError("inflate: bad length symbol");
        const size_t len = kLenBase[size_t(li)] + b.get(kLenExtra[size_t(li)]);
        const int di = dist.decode(b);
        if (di >= 30) throw FormatError("inflate: bad distance symbol");
        const size_t d = kDistBase[size_t(di)] + b.get(kDistExtra[size_t(di)]);
        if (d > out.size()) throw FormatError("inflate: distance too far back");
        const size_t from = out.size() - d;
        for (size_t i = 0; i < len; ++i) out.push_back(out[from + i]); // may overlap
    }
}

} // namespace

std::vector<uint8_t> inflate_raw(std::span<const uint8_t> in, size_t size_hint) {
    std::vector<uint8_t> out;
    out.reserve(size_hint);
    Bits b(in);
    bool last = false;
    while (!last) {
        last = b.get(1) != 0;
        const uint32_t type = b.get(2);
        if (type == 0) { // stored
            b.align();
            const auto word = [&b] { // low byte first; separate statements keep the order
                const uint16_t lo = b.byte();
                return uint16_t(lo | (b.byte() << 8));
            };
            const uint16_t len = word();
            const uint16_t nlen = word();
            if (uint16_t(~nlen) != len) throw FormatError("inflate: stored length check");
            for (uint16_t i = 0; i < len; ++i) out.push_back(b.byte());
        } else if (type == 1) { // fixed codes
            static const auto fixed = [] {
                std::array<uint8_t, 288> l{};
                for (size_t i = 0; i < 144; ++i) l[i] = 8;
                for (size_t i = 144; i < 256; ++i) l[i] = 9;
                for (size_t i = 256; i < 280; ++i) l[i] = 7;
                for (size_t i = 280; i < 288; ++i) l[i] = 8;
                std::array<uint8_t, 30> d{};
                d.fill(5);
                std::pair<Huffman, Huffman> h;
                h.first.build(l);
                h.second.build(d);
                return h;
            }();
            codes(b, out, fixed.first, fixed.second);
        } else if (type == 2) { // dynamic codes
            const size_t nlen = b.get(5) + 257, ndist = b.get(5) + 1, ncode = b.get(4) + 4;
            if (nlen > 286 || ndist > 30) throw FormatError("inflate: bad code counts");
            static constexpr std::array<uint8_t, 19> kOrder{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            std::array<uint8_t, 19> cl{};
            for (size_t i = 0; i < ncode; ++i) cl[kOrder[i]] = uint8_t(b.get(3));
            Huffman clh;
            clh.build(cl);
            std::vector<uint8_t> lengths;
            while (lengths.size() < nlen + ndist) {
                const int sym = clh.decode(b);
                if (sym < 16) {
                    lengths.push_back(uint8_t(sym));
                } else {
                    uint8_t v = 0;
                    size_t rep;
                    if (sym == 16) {
                        if (lengths.empty()) throw FormatError("inflate: repeat with no length");
                        v = lengths.back();
                        rep = 3 + b.get(2);
                    } else if (sym == 17) {
                        rep = 3 + b.get(3);
                    } else {
                        rep = 11 + b.get(7);
                    }
                    if (lengths.size() + rep > nlen + ndist) throw FormatError("inflate: too many lengths");
                    lengths.insert(lengths.end(), rep, v);
                }
            }
            if (lengths[256] == 0) throw FormatError("inflate: no end-of-block code");
            Huffman lit, dist;
            lit.build(std::span(lengths).first(nlen));
            dist.build(std::span(lengths).subspan(nlen));
            codes(b, out, lit, dist);
        } else {
            throw FormatError("inflate: bad block type");
        }
    }
    return out;
}

} // namespace d2k::formats
