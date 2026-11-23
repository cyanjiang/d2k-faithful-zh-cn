#pragma once
// A minimal x87 emulation for the few expressions where the exe's FPU code
// matters, e.g. the AI's "(u64)(u32)off * 0.01f + 1" formulas whose
// negative offsets load as huge unsigned values (docs/re/23 section 4.1).
// Positive values only. Loads (FILD qword, FLD dword) are exact; every
// arithmetic result is rounded to nearest even at 53 bits, because the
// exe's C runtime sets that precision control at start-up: __fpmath
// (0x4b0f30, via _FPinit) calls __setdefaultprecision (0x4b5af0) =
// _controlfp(_PC_53, _MCW_PC), and no other code loads a control word
// except __ftol's own save / restore (docs/re/22 section 3.2).

#include <cstdint>
#include <cstring>

namespace d2k::sim::x87 {

struct Ext {
    uint64_t m = 0; // normalized: bit 63 set (or 0 for zero)
    int e = 0;      // value = m * 2^e
};

namespace detail {
inline void mul64(uint64_t a, uint64_t b, uint64_t& hi, uint64_t& lo) {
    const uint64_t a0 = a & 0xffffffffu, a1 = a >> 32, b0 = b & 0xffffffffu, b1 = b >> 32;
    const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
    lo = (p00 & 0xffffffffu) | (mid << 32);
    hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}
// Rounds the 128-bit value hi:lo (with a sticky bit for anything below)
// to `bits` significant bits (53: the precision control; 64: an exact
// load); returns the result for value = hi:lo * 2^e.
inline Ext round128(uint64_t hi, uint64_t lo, bool sticky, int e, int bits = 53) {
    if (hi == 0 && lo == 0) return {};
    while (!(hi >> 63)) { // normalize to bit 127
        hi = (hi << 1) | (lo >> 63);
        lo <<= 1;
        --e;
    }
    const int drop = 64 - bits; // low bits of hi below the kept ones
    bool half, rest;
    uint64_t m;
    if (drop == 0) {
        half = lo >> 63;
        rest = (lo << 1) != 0 || sticky;
        m = hi;
    } else {
        half = (hi >> (drop - 1)) & 1;
        rest = (hi & ((uint64_t(1) << (drop - 1)) - 1)) != 0 || lo != 0 || sticky;
        m = hi >> drop;
    }
    if (half && (rest || (m & 1))) {
        ++m;
        if (drop == 0 ? m == 0 : m == (uint64_t(1) << bits)) { // carry out
            m = uint64_t(1) << (bits - 1);
            ++e;
        }
    }
    return {drop == 0 ? m : m << drop, e + 64};
}
} // namespace detail

inline Ext from_u64(uint64_t v) { return detail::round128(v, 0, false, -64, 64); } // FILD: exact
inline Ext from_float(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    const int exp = int((bits >> 23) & 0xff);
    if (exp == 0) return {}; // zero (denormals are not needed here)
    const uint64_t mant = (bits & 0x7fffff) | 0x800000;
    return detail::round128(mant, 0, false, exp - 127 - 23 - 64, 64); // FLD dword: exact
}
inline Ext mul(Ext a, Ext b) {
    if (!a.m || !b.m) return {};
    uint64_t hi, lo;
    detail::mul64(a.m, b.m, hi, lo);
    return detail::round128(hi, lo, false, a.e + b.e);
}
inline Ext add(Ext a, Ext b) { // a, b >= 0
    if (!a.m) return b;
    if (!b.m) return a;
    if (a.e < b.e) std::swap(a, b);
    // a = a.m * 2^a.e as hi:lo = a.m:0 with exponent a.e - 64; shift b to it.
    const int d = a.e - b.e;
    uint64_t bhi = b.m, blo = 0;
    bool sticky = false;
    if (d >= 128) {
        sticky = true;
        bhi = blo = 0;
    } else if (d >= 64) {
        sticky = d > 64 ? (bhi << (128 - d)) != 0 : false;
        blo = d == 64 ? bhi : bhi >> (d - 64);
        bhi = 0;
    } else if (d > 0) {
        blo = bhi << (64 - d);
        bhi >>= d;
    }
    uint64_t lo = blo, hi = a.m + bhi;
    const bool carry = hi < a.m;
    int e = a.e - 64;
    if (carry) {
        sticky = sticky || (lo & 1);
        lo = (lo >> 1) | (hi << 63);
        hi = (hi >> 1) | (uint64_t(1) << 63);
        ++e;
    }
    return detail::round128(hi, lo, sticky, e);
}
// FISTP after __ftol (truncate) into a 64-bit integer.
inline int64_t trunc_i64(Ext a) {
    if (!a.m) return 0;
    if (a.e >= 0) return int64_t(a.m << a.e);
    if (a.e <= -64) return 0;
    return int64_t(a.m >> -a.e);
}
// FSTP to a float32 (round to nearest even).
inline float to_float(Ext a) {
    if (!a.m) return 0.0f;
    uint64_t m = a.m >> 40; // 24 bits
    const uint64_t rest = a.m & ((uint64_t(1) << 40) - 1);
    const uint64_t half = uint64_t(1) << 39;
    int e = a.e + 40;
    if (rest > half || (rest == half && (m & 1))) {
        if (++m == (uint64_t(1) << 24)) {
            m >>= 1;
            ++e;
        }
    }
    float f = float(m);
    // m * 2^e
    while (e > 0) { f *= 2.0f; --e; }
    while (e < 0) { f *= 0.5f; ++e; }
    return f;
}

// The AI's percentage formula (docs/re/23 section 4.1):
// ftol(((u64)(u32)off * 0.01f - (-1.0f)) * multiplier), low dword.
inline Ext percent_factor(int32_t off) {
    const Ext v = from_u64(uint64_t(uint32_t(off)));
    return add(mul(v, from_float(0.01f)), from_u64(1));
}
inline int32_t percent_of(int32_t off, int64_t multiplier) {
    const Ext r = mul(percent_factor(off), from_u64(uint64_t(multiplier)));
    return int32_t(uint32_t(uint64_t(trunc_i64(r))));
}

} // namespace d2k::sim::x87
