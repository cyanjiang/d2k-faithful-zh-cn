#pragma once
// Raw DEFLATE decoder (RFC 1951): stored, fixed and dynamic Huffman blocks.
// Used for the 1.06 patch's ZIP entries (docs/first-run-setup.md).

#include <cstdint>
#include <span>
#include <vector>

namespace d2k::formats {

// Decodes a raw DEFLATE stream (no zlib or gzip header). `size_hint`
// reserves the output; throws FormatError on a corrupt stream.
std::vector<uint8_t> inflate_raw(std::span<const uint8_t> in, size_t size_hint = 0);

} // namespace d2k::formats
