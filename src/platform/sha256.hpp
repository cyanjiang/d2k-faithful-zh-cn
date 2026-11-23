#pragma once
// SHA-256 (FIPS 180-4), used to check that the player's files are the
// version our exe-derived tables expect.

#include <cstdint>
#include <span>
#include <string>

namespace d2k::platform {

// Lower-case hex digest.
std::string sha256_hex(std::span<const uint8_t> data);

} // namespace d2k::platform
