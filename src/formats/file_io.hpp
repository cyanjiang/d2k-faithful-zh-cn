#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace d2k::formats {

// Reads a whole file; throws std::runtime_error on failure.
std::vector<uint8_t> read_file(const std::filesystem::path& path);

} // namespace d2k::formats
