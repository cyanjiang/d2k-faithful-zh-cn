#pragma once
// Upscaled replacement pictures for the VQA movies (display option
// `enhanced_movies`, off by default; docs/enhanced-movies.md). Not part of
// the original: the player makes these files on their own PC from their own
// movies. Only the picture is replaced; sound, timing, skipping and the end
// of the movie still come from the original VQA.
//
// Decodes an H.264 .mp4 to NV12 frames on a worker thread (Windows Media
// Foundation; on other systems open() returns nullptr and the VQA plays).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace d2k::video {

// `<dir>/<VQA file name without extension>.mp4`, e.g. MOVIES/A_BR01_E.VQA ->
// <dir>/A_BR01_E.mp4.
std::filesystem::path enhanced_path_for(const std::filesystem::path& dir,
                                        const std::filesystem::path& vqa);

struct EnhancedFrame {
    double seconds = 0;          // presentation time
    std::vector<uint8_t> nv12;   // width*height Y, then width*height/2 interleaved UV
};

class EnhancedVideo {
public:
    // nullptr when the file is missing, not decodable, or the system has no
    // decoder; the caller then shows the VQA's own pictures.
    static std::unique_ptr<EnhancedVideo> open(const std::filesystem::path& file);
    virtual ~EnhancedVideo() = default;

    virtual int width() const = 0;
    virtual int height() const = 0;
    // The newest decoded frame due at `seconds`, dropping older ones; nothing
    // when no new frame is due yet.
    virtual std::optional<EnhancedFrame> take_due(double seconds) = 0;
};

} // namespace d2k::video
