#pragma once
// Plays a VQA movie in the game window, timed off the audio clock.

#include <filesystem>

namespace d2k::render {
class Display;
}

namespace d2k::video {

enum class MovieResult { Finished, Skipped, Quit };

// Blocks until the movie ends, the player skips it, or the window closes.
// Draws at (0,0) of the 640x400 screen.
MovieResult play_movie(render::Display& display, const std::filesystem::path& path);

// Display option `enhanced_movies` (docs/enhanced-movies.md): when set, a
// movie whose `<dir>/<NAME>.mp4` exists shows that file's pictures; sound,
// timing, skipping and the end stay the VQA's. Empty = off (the default).
void set_enhanced_movies_dir(const std::filesystem::path& dir);

} // namespace d2k::video
