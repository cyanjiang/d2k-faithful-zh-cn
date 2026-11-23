#pragma once
// Minimal dependency-free PNG writer (RGBA8, stored/uncompressed deflate).
// Good enough for debug output; files are larger than necessary.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace d2k::tool {

void write_png_rgba(const std::filesystem::path& path, uint32_t width, uint32_t height,
                    const std::vector<uint8_t>& rgba);

} // namespace d2k::tool
