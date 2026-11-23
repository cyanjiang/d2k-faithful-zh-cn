#include <doctest/doctest.h>

#include <array>

#include "formats/byte_reader.hpp"

using d2k::formats::ByteReader;
using d2k::formats::FormatError;

TEST_CASE("ByteReader reads little-endian integers") {
    std::array<uint8_t, 7> buf{0x13, 0x5d, 0x65, 0x8c, 0x34, 0x12, 0xff};
    ByteReader r(buf);
    CHECK(r.u32() == 0x8c655d13u);
    CHECK(r.u16() == 0x1234);
    CHECK(r.u8() == 0xff);
    CHECK(r.remaining() == 0);
}

TEST_CASE("ByteReader rejects overruns") {
    std::array<uint8_t, 3> buf{1, 2, 3};
    ByteReader r(buf);
    CHECK_THROWS_AS(r.u32(), FormatError);
    CHECK(r.pos() == 0);
    CHECK_THROWS_AS(r.seek(4), FormatError);
}

TEST_CASE("ByteReader fixed_string stops at NUL") {
    std::array<uint8_t, 6> buf{'D', 'U', 'N', 'E', 0, 'x'};
    ByteReader r(buf);
    CHECK(r.fixed_string(6) == "DUNE");
    CHECK(r.remaining() == 0);
}
