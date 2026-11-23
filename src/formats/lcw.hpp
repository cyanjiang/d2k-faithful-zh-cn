#pragma once
// Westwood LCW ("format80") decompression, used for VQA codebooks and
// vector-pointer tables. Format: docs/formats/vqa.md#lcw

#include <cstdint>
#include <span>

namespace d2k::formats {

// Decompresses `src` into `dst`. Returns the number of bytes written.
// Throws FormatError on malformed input or if `dst` is too small.
size_t lcw_decompress(std::span<const uint8_t> src, std::span<uint8_t> dst);

} // namespace d2k::formats
