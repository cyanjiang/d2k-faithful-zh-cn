#include "formats/file_io.hpp"

#include <fstream>
#include <stdexcept>

namespace d2k::formats {

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    auto size = static_cast<size_t>(in.tellg());
    std::vector<uint8_t> data(size);
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return data;
}

} // namespace d2k::formats
